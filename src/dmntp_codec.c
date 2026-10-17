#include "dmntp_internal.h"
#include <string.h>
uint64_t dmntp_ntp_to_unix(uint32_t sec)
{
    return sec < DMNTP_EPOCH_OFFSET ? 0 : (uint64_t)sec - DMNTP_EPOCH_OFFSET;
}
uint32_t dmntp_unix_to_ntp(uint64_t sec)
{
    return sec > UINT32_MAX - DMNTP_EPOCH_OFFSET ? 0 : (uint32_t)(sec + DMNTP_EPOCH_OFFSET);
}
uint32_t dmntp_frac_to_ns(uint32_t fraction)
{
    return (uint32_t)(((uint64_t)fraction * 1000000000ULL) >> 32);
}
uint32_t dmntp_ns_to_frac(uint32_t ns)
{
    return (uint32_t)(((uint64_t)ns << 32) / 1000000000ULL);
}
dmntp_timestamp_t dmntp_timestamp_add_ns(dmntp_timestamp_t ts, uint32_t ns)
{
    uint64_t sum = (uint64_t)ts.fraction + (((uint64_t)ns << 32) / 1000000000ULL);
    ts.seconds += (uint32_t)(sum >> 32); ts.fraction = (uint32_t)sum;
    return ts;
}
void dmntp_packet_init_client(dmntp_packet_t* pkt)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->li_vn_mode = DMNTP_LI_VN_MODE(0, 4, DMNTP_MODE_CLIENT);
}
int dmntp_packet_validate(const dmntp_packet_t* pkt, size_t size)
{
    if (!pkt || size < DMNTP_PACKET_SIZE || DMNTP_VN(pkt) != 4 ||
        DMNTP_MODE(pkt) != DMNTP_MODE_SERVER || DMNTP_LI(pkt) == 3 ||
        pkt->stratum < 1 || pkt->stratum > 15) return -EBADMSG;
    return 0;
}
static double difference(const dmntp_timestamp_t* a, const dmntp_timestamp_t* b)
{
    return (double)((int64_t)a->seconds - b->seconds) +
        (double)((int64_t)a->fraction - b->fraction) / 4294967296.0;
}
int dmntp_calculate_offset(const dmntp_timestamp_t* t1, const dmntp_timestamp_t* t2,
    const dmntp_timestamp_t* t3, const dmntp_timestamp_t* t4, double* offset, double* delay)
{
    if (!t1 || !t2 || !t3 || !t4 || !offset || !delay) return -EINVAL;
    double processing = difference(t3, t2), elapsed = difference(t4, t1);
    if (processing < 0 || elapsed < processing) return -EBADMSG;
    *offset = (difference(t2, t1) + difference(t3, t4)) / 2;
    *delay = elapsed - processing;
    return 0;
}
static double square_root(double value)
{
    if (value <= 0) return 0;
    double x = value > 1 ? value : 1;
    for (unsigned i = 0; i < 64; ++i) {
        double next = (x + value / x) / 2;
        if (next == x) break;
        x = next;
    }
    return x;
}
double dmntp_calculate_jitter(const double* offsets, size_t count)
{
    if (!offsets || count < 2) return 0;
    double mean = 0, sum = 0;
    for (size_t i = 0; i < count; ++i) mean += offsets[i] / count;
    for (size_t i = 0; i < count; ++i) {
        double d = offsets[i] - mean; sum += d * d;
    }
    return square_root(sum / (count - 1));
}
static uint32_t read32(const uint8_t* b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
void dmntp_packet_decode(const uint8_t* bytes, dmntp_packet_t* packet)
{
    memcpy(packet, bytes, 4);
    for (size_t i = 4; i < DMNTP_PACKET_SIZE; i += 4) {
        uint32_t v = read32(bytes + i); memcpy((uint8_t*)packet + i, &v, 4);
    }
}
void dmntp_packet_encode(const dmntp_packet_t* packet, uint8_t* bytes)
{
    memcpy(bytes, packet, 4);
    for (size_t i = 4; i < DMNTP_PACKET_SIZE; i += 4) {
        uint32_t v; memcpy(&v, (const uint8_t*)packet + i, 4);
        bytes[i] = v >> 24; bytes[i+1] = v >> 16; bytes[i+2] = v >> 8; bytes[i+3] = v;
    }
}
/* Maximum interval overlap (Marzullo): require a strict majority when
 * multiple sources reply. Reject disagreement rather than trusting an outlier. */
int dmntp_select(const dmntp_result_t* samples, size_t count, double* offset, double* jitter)
{
    if (!samples || !count || count > DMNTP_MAX_SERVERS || !offset || !jitter) return -EINVAL;
    size_t best = 0; double point = 0;
    for (size_t i = 0; i < count; ++i) {
        double candidate = samples[i].offset - samples[i].distance;
        size_t overlap = 0;
        for (size_t j = 0; j < count; ++j)
            if (candidate >= samples[j].offset - samples[j].distance &&
                candidate <= samples[j].offset + samples[j].distance) ++overlap;
        if (overlap > best) { best = overlap; point = candidate; }
    }
    if (best <= count / 2) return -EAGAIN;
    double low = -1e20, high = 1e20, offsets[DMNTP_MAX_SERVERS]; size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        double l = samples[i].offset - samples[i].distance;
        double h = samples[i].offset + samples[i].distance;
        if (point < l || point > h) continue;
        if (l > low) low = l;
        if (h < high) high = h;
        offsets[used++] = samples[i].offset;
    }
    *offset = (low + high) / 2; *jitter = dmntp_calculate_jitter(offsets, used);
    return 0;
}
