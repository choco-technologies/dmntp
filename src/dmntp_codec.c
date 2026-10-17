/**
 * @file dmntp_codec.c
 * @brief The protocol's arithmetic: timestamps, packets, the result of an
 *        exchange and the selection of the offset the servers agree on.
 *
 * Integer nanoseconds only: the offset of UTC from the local clock is about
 * 1.8e18 ns, an int64_t holds 9.2e18.
 */

#include "dmntp_internal.h"

#include <string.h>

#define NS_PER_SECOND       1000000000LL
#define ERA_SECONDS         4294967296LL    /* 2^32: one NTP era */

/* ===========================================================================
 *                                Timestamps
 * ========================================================================== */

/**
 * @brief Nanoseconds of a 2^-32 s fraction
 */
static int64_t fraction_to_ns(uint32_t fraction)
{
    return (int64_t)(((uint64_t)fraction * (uint64_t)NS_PER_SECOND) >> 32);
}

/**
 * @brief Nanoseconds of a 16.16 fixed point number of seconds
 *        (root delay, root dispersion)
 */
static int64_t short_to_ns(uint32_t value)
{
    return (int64_t)(((uint64_t)value * (uint64_t)NS_PER_SECOND) >> 16);
}

int64_t dmntp_timestamp_to_ns(const dmntp_timestamp_t* t)
{
    int64_t seconds = (int64_t)t->seconds;

    if ((t->seconds & 0x80000000u) == 0)
        seconds += ERA_SECONDS;         /* (era 1: from February 2036) */

    return (seconds - (int64_t)DMNTP_EPOCH_OFFSET) * NS_PER_SECOND + fraction_to_ns(t->fraction);
}

/* ===========================================================================
 *                                 Packets
 * ========================================================================== */

void dmntp_packet_init_client(dmntp_packet_t* packet)
{
    memset(packet, 0, sizeof(*packet));
    packet->li_vn_mode = DMNTP_LI_VN_MODE(0, DMNTP_VERSION_4, DMNTP_MODE_CLIENT);
}

/**
 * @brief A 32-bit big endian number
 */
static uint32_t read32(const uint8_t* b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

/**
 * @brief A 32-bit number, big endian
 */
static void write32(uint8_t* b, uint32_t value)
{
    b[0] = (uint8_t)(value >> 24);
    b[1] = (uint8_t)(value >> 16);
    b[2] = (uint8_t)(value >> 8);
    b[3] = (uint8_t)value;
}

void dmntp_packet_encode(const dmntp_packet_t* packet, uint8_t* bytes)
{
    /* The first four bytes are single bytes, the rest 32-bit words */
    memcpy(bytes, packet, 4);

    for (size_t i = 4; i < DMNTP_PACKET_SIZE; i += 4)
    {
        uint32_t value;
        memcpy(&value, (const uint8_t*)packet + i, sizeof(value));
        write32(bytes + i, value);
    }
}

void dmntp_packet_decode(const uint8_t* bytes, dmntp_packet_t* packet)
{
    memcpy(packet, bytes, 4);

    for (size_t i = 4; i < DMNTP_PACKET_SIZE; i += 4)
    {
        uint32_t value = read32(bytes + i);
        memcpy((uint8_t*)packet + i, &value, sizeof(value));
    }
}

int dmntp_packet_validate(const dmntp_packet_t* packet)
{
    unsigned version = DMNTP_VN(packet);

    if (version != 3 && version != DMNTP_VERSION_4)
        return -EBADMSG;
    if (DMNTP_MODE(packet) != DMNTP_MODE_SERVER)
        return -EBADMSG;
    if (DMNTP_LI(packet) == DMNTP_LI_ALARM)
        return -EBADMSG;                /* (not synchronized itself) */
    if (packet->stratum == DMNTP_STRATUM_KISS || packet->stratum > DMNTP_STRATUM_MAX)
        return -EBADMSG;
    if (packet->trans.seconds == 0 && packet->trans.fraction == 0)
        return -EBADMSG;

    return 0;
}

/* ===========================================================================
 *                            Result of an exchange
 * ========================================================================== */

int dmntp_sample(const dmntp_packet_t* reply, int64_t sent_ns, int64_t received_ns, dmntp_sample_t* sample)
{
    int64_t t2 = dmntp_timestamp_to_ns(&reply->recv);
    int64_t t3 = dmntp_timestamp_to_ns(&reply->trans);
    int64_t processing = t3 - t2;
    int64_t elapsed = received_ns - sent_ns;

    /* The local clock counts milliseconds: a reply within one may show 0 elapsed */
    if (processing < 0 || elapsed < 0 || elapsed + DMNTP_CLOCK_RESOLUTION_NS < processing)
        return -EBADMSG;

    sample->offset_ns = ((t2 - sent_ns) + (t3 - received_ns)) / 2;
    sample->delay_ns = (elapsed > processing) ? elapsed - processing : 0;

    int64_t root_delay = short_to_ns(reply->root_delay);
    int64_t root_dispersion = short_to_ns(reply->root_dispersion);

    /* RFC 5905's root distance, with the local clock's resolution */
    sample->distance_ns = (sample->delay_ns + root_delay) / 2 + root_dispersion + DMNTP_CLOCK_RESOLUTION_NS;
    if (sample->distance_ns > DMNTP_MAX_ROOT_DISTANCE_NS)
        return -EBADMSG;

    return 0;
}

/* ===========================================================================
 *                          Selection (Marzullo)
 * ========================================================================== */

/**
 * @brief How many samples' intervals contain a point
 */
static uint32_t containing(const dmntp_sample_t* samples, size_t count, int64_t point)
{
    uint32_t n = 0;

    for (size_t i = 0; i < count; i++)
    {
        if (point >= samples[i].offset_ns - samples[i].distance_ns &&
            point <= samples[i].offset_ns + samples[i].distance_ns)
            n++;
    }
    return n;
}

int dmntp_select(const dmntp_sample_t* samples, size_t count, int64_t* offset_ns, uint64_t* error_ns, uint32_t* agreeing)
{
    if (samples == NULL || count == 0)
        return -EINVAL;

    /* The most covered point is the start of one of the intervals */
    int64_t point = 0;
    uint32_t best = 0;
    for (size_t i = 0; i < count; i++)
    {
        int64_t start = samples[i].offset_ns - samples[i].distance_ns;
        uint32_t n = containing(samples, count, start);
        if (n > best)
        {
            best = n;
            point = start;
        }
    }
    if (best <= count / 2)
        return -EAGAIN;

    /* The intervals that contain it, intersected */
    int64_t low = INT64_MIN, high = INT64_MAX;
    for (size_t i = 0; i < count; i++)
    {
        int64_t start = samples[i].offset_ns - samples[i].distance_ns;
        int64_t end = samples[i].offset_ns + samples[i].distance_ns;
        if (point < start || point > end)
            continue;
        low = (start > low) ? start : low;
        high = (end < high) ? end : high;
    }

    *offset_ns = low + (high - low) / 2;
    *error_ns = (uint64_t)(high - low) / 2u;
    *agreeing = best;
    return 0;
}
