/**
 * @file dmntp_codec.c
 * @brief NTP packet codec and time conversion functions.
 *
 * Implements building and parsing NTPv4 packets (RFC 5905), and
 * converting between NTP timestamps and Unix time.
 */

#include "dmntp_internal.h"

#include <string.h>
#include <math.h>

/*
 * NTP timestamp <-> Unix time conversion.
 */

/**
 * Convert NTP timestamp to Unix seconds.
 *
 * @param ntp NTP timestamp (seconds since 1900-01-01).
 * @return Unix timestamp (seconds since 1970-01-01), or 0 on overflow.
 */
uint64_t dmntp_ntp_to_unix(uint32_t ntp_seconds)
{
    if (ntp_seconds < DMNTP_EPOCH_OFFSET) {
        return 0;  /* Before Unix epoch */
    }
    uint64_t result = (uint64_t)ntp_seconds - DMNTP_EPOCH_OFFSET;
}

/**
 * Convert Unix seconds to NTP timestamp.
 *
 * @param unix Unix timestamp (seconds since 1970-01-01).
 * @return NTP timestamp (seconds since 1900-01-01), or 0 on overflow.
 */
uint32_t dmntp_unix_to_ntp(uint64_t unix_seconds)
{
    if (unix_seconds > UINT32_MAX - DMNTP_EPOCH_OFFSET) {
        return 0;  /* Would overflow */
    }
    return (uint32_t)(unix_seconds + DMNTP_EPOCH_OFFSET);
}

/**
 * Convert NTP fraction (32-bit fixed point) to nanoseconds.
 *
 * @param fraction NTP fraction part (0..2^32-1).
 * @return Nanoseconds (0..999999999).
 */
uint32_t dmntp_frac_to_ns(uint32_t fraction)
{
    /* fraction / 2^32 * 1e9 */
    uint64_t ns = ((uint64_t)fraction * 1000000000ULL) >> 32;
    return (uint32_t)ns;
}

/**
 * Convert nanoseconds to NTP fraction (32-bit fixed point).
 *
 * @param ns Nanoseconds (0..999999999).
 * @return NTP fraction (0..2^32-1).
 */
uint32_t dmntp_ns_to_frac(uint32_t ns)
{
    /* ns * 2^32 / 1e9 */
    uint64_t frac = ((uint64_t)ns << 32) / 1000000000ULL;
    return (uint32_t)frac;
}

/**
 * Add nanoseconds to an NTP timestamp.
 *
 * @param ts Input timestamp.
 * @param ns Nanoseconds to add (must be < 1e9).
 * @return Updated timestamp.
 */
dmntp_timestamp_t dmntp_timestamp_add_ns(dmntp_timestamp_t ts, uint32_t ns)
{
    uint32_t new_frac = ts.fraction + ns;
    if (new_frac < ts.fraction) {
        /* Overflow into seconds */
        ts.seconds++;
    }
    if (new_frac >= 1000000000U) {
        new_frac -= 1000000000U;
        ts.seconds++;
    }
    ts.fraction = dmntp_ns_to_frac(new_frac);
    return ts;
}

/*
 * NTP packet building and parsing.
 */

/**
 * Initialize an NTP packet with client request defaults.
 *
 * Sets LI=0, VN=4, MODE=3 (client), stratum=0, poll=0, precision=0,
 * and zero all other fields.
 *
 * @param pkt Packet to initialize.
 */
void dmntp_packet_init_client(dmntp_packet_t* pkt)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->li_vn_mode = DMNTP_LI_VN_MODE(DMNTP_LI_NO_WARNING,
                                         DMNTP_VERSION_4,
                                         DMNTP_MODE_CLIENT);
    pkt->stratum = DMNTP_STRATUM_UNSPECIFIED;
    pkt->poll = 0;
    pkt->precision = 0;
}

/**
 * Validate a received NTP packet.
 *
 * Checks:
 * - Packet size >= 48 bytes
 * - Version == 4
 * - Mode == server (4) or symmetric passive (2)
 * - LI != 3 (not alarm)
 * - Stratum != 0 and != 16 (kiss)
 *
 * @param pkt Packet to validate.
 * @param size Packet size in bytes.
 * @return 0 on success, -EBADMSG if invalid.
 */
int dmntp_packet_validate(const dmntp_packet_t* pkt, size_t size)
{
    if (size < DMNTP_PACKET_SIZE) {
        return -EBADMSG;
    }

    /* Version must be 4 */
    if (DMNTP_VN(pkt) != DMNTP_VERSION_4) {
        return -EBADMSG;
    }

    /* Mode must be server (4) or symmetric passive (2) */
    uint8_t mode = DMNTP_MODE(pkt);
    if (mode != DMNTP_MODE_SERVER && mode != DMNTP_MODE_PASSIVE) {
        return -EBADMSG;
    }

    /* Leap indicator must not be alarm (not synchronized) */
    if (DMNTP_LI(pkt) == DMNTP_LI_ALARM) {
        return -EBADMSG;
    }

    /* Stratum must be valid (1-15) */
    if (pkt->stratum == DMNTP_STRATUM_UNSPECIFIED ||
        pkt->stratum == DMNTP_STRATUM_KISS) {
        return -EBADMSG;
    }

    return 0;
}

/**
 * Calculate offset and delay from an NTP exchange.
 *
 * Given timestamps T1-T4 (RFC 5905 Section 8):
 * - T1 = orig (sent by client)
 * - T2 = recv (received by server)
 * - T3 = trans (sent by server)
 * - T4 = now (received by client)
 *
 * offset = ((T2 - T1) + (T3 - T4)) / 2
 * delay  = (T4 - T1) - (T3 - T2)
 *
 * All timestamps are in NTP format (seconds + fraction).
 *
 * @param orig    Origin timestamp (T1) - when we sent the request.
 * @param recv    Receive timestamp (T2) - when server got it.
 * @param trans   Transmit timestamp (T3) - when server sent reply.
 * @param now     Receive timestamp (T4) - when we got reply.
 * @param offset  Output: calculated offset in seconds (positive = local clock ahead).
 * @param delay   Output: round-trip delay in seconds.
 * @return 0 on success, -EINVAL on overflow.
 */
int dmntp_calculate_offset(const dmntp_timestamp_t* orig,
                           const dmntp_timestamp_t* recv,
                           const dmntp_timestamp_t* trans,
                           const dmntp_timestamp_t* now,
                           double* offset,
                           double* delay)
{
    /* Calculate T2 - T1 */
    uint64_t t2_t1 = 0;
    if (recv->seconds >= orig->seconds) {
        t2_t1 = ((uint64_t)recv->seconds - orig->seconds) << 32;
        t2_t1 += (int64_t)recv->fraction - (int64_t)orig->fraction;
    } else {
        /* Negative, shouldn't happen in practice */
        return -EINVAL;
    }

    /* Calculate T3 - T4 */
    uint64_t t3_t4 = 0;
    if (trans->seconds >= now->seconds) {
        t3_t4 = ((uint64_t)trans->seconds - now->seconds) << 32;
        t3_t4 += (int64_t)trans->fraction - (int64_t)now->fraction;
    } else {
        /* Negative, shouldn't happen in practice */
        return -EINVAL;
    }

    /* offset = ((T2 - T1) + (T3 - T4)) / 2 */
    int64_t offset_raw = (int64_t)((t2_t1 + t3_t4) >> 1);
    *offset = (double)offset_raw / (double)(1ULL << 32);

    /* delay = (T4 - T1) - (T3 - T2) */
    uint64_t t4_t1 = 0;
    if (now->seconds >= orig->seconds) {
        t4_t1 = ((uint64_t)now->seconds - orig->seconds) << 32;
        t4_t1 += (int64_t)now->fraction - (int64_t)orig->fraction;
    } else {
        return -EINVAL;
    }

    int64_t t3_t2 = 0;
    if (trans->seconds >= recv->seconds) {
        t3_t2 = ((uint64_t)trans->seconds - recv->seconds) << 32;
        t3_t2 += (int64_t)trans->fraction - (int64_t)recv->fraction;
    } else {
        return -EINVAL;
    }

    if (t4_t1 < t3_t2) {
        return -EINVAL;  /* Negative delay, discard */
    }

    uint64_t delay_raw = t4_t1 - t3_t2;
    *delay = (double)delay_raw / (double)(1ULL << 32);

    return 0;
}

/**
 * Calculate jitter from a series of offset measurements.
 *
 * Uses the algorithm from RFC 5905: compute the root mean square
 * of the differences between each sample and the median.
 *
 * @param offsets Array of offset measurements (in seconds).
 * @param count   Number of measurements.
 * @return Jitter in seconds.
 */
double dmntp_calculate_jitter(const double* offsets, size_t count)
{
    if (count < 2) {
        return 0.0;
    }

    /* Find median (simple sort for small arrays) */
    double sorted[16];
    size_t n = count > 16 ? 16 : count;
    memcpy(sorted, offsets, n * sizeof(double));

    /* Simple insertion sort */
    for (size_t i = 1; i < n; i++) {
        double key = sorted[i];
        size_t j = i;
        while (j > 0 && sorted[j - 1] > key) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = key;
    }

    double median = sorted[n / 2];

    /* Calculate RMS of differences from median */
    double sum_sq = 0.0;
    for (size_t i = 0; i < n; i++) {
        double diff = offsets[i] - median;
        sum_sq += diff * diff;
    }

    return sqrt(sum_sq / n);
}
