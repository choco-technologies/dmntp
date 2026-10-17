#ifndef DMNTP_INTERNAL_H
#define DMNTP_INTERNAL_H

/**
 * @file dmntp_internal.h
 * @brief Shared declarations for dmntp translation units.
 *
 * Only the dmntp sources include this header - public API lives in
 * include/dmntp.h and include/dmntp_protocol.h.
 */

#include <errno.h>

#include "dmod.h"
#include "dmntp.h"
#include "dmntp_protocol.h"
#include "dmosi.h"

/** Magic value stored at the start of every dmntp handle. */
#define DMNTP_MAGIC 0x444D4E54U

/** Maximum number of distinct servers a single handle keeps. */
#define DMNTP_MAX_SERVERS 8u

/** Singly-linked list node holding a copy of the user-supplied host string. */
typedef struct dmntp_server {
    char* host;
    struct dmntp_server* next;
} dmntp_server_t;

/** Concrete handle struct - definition is private to the dmntp sources. */
struct dmntp {
    uint32_t magic;
    dmntp_server_t* servers;
    size_t server_count;
    dmosi_mutex_t lock;
    bool discover;
};

/** Marzullo sample produced by one NTP exchange. */
typedef struct {
    double offset;
    double delay;
    double distance;
} dmntp_result_t;

/**
 * @brief Convert an NTP seconds-since-1900 value to POSIX Unix seconds.
 *
 * @param seconds NTP seconds field.
 *
 * @return Equivalent Unix timestamp, with the NTP era wrap handled.
 */
uint64_t dmntp_ntp_to_unix(uint32_t seconds);

/**
 * @brief Convert a POSIX Unix seconds value to NTP seconds-since-1900.
 *
 * @param seconds Unix timestamp.
 *
 * @return NTP seconds field (era 0 until 2106).
 */
uint32_t dmntp_unix_to_ntp(uint64_t seconds);

/**
 * @brief Convert an NTP 2^-32 fraction to nanoseconds.
 *
 * @param fraction NTP fraction field.
 *
 * @return Nanoseconds equivalent.
 */
uint32_t dmntp_frac_to_ns(uint32_t fraction);

/**
 * @brief Convert nanoseconds to an NTP 2^-32 fraction.
 *
 * @param ns Nanoseconds value.
 *
 * @return NTP fraction field.
 */
uint32_t dmntp_ns_to_frac(uint32_t ns);

/**
 * @brief Add an integer number of nanoseconds to an NTP timestamp.
 *
 * @param ts Timestamp to advance.
 * @param ns Nanoseconds to add (must be < 1 s).
 *
 * @return ts + ns with carry into the seconds field if needed.
 */
dmntp_timestamp_t dmntp_timestamp_add_ns(dmntp_timestamp_t ts, uint32_t ns);

/**
 * @brief Fill a packet with client-mode defaults (VN=4, mode=3).
 *
 * @param pkt Packet to initialise; all fields are written.
 */
void dmntp_packet_init_client(dmntp_packet_t* pkt);

/**
 * @brief Validate that a received packet looks like a sane server reply.
 *
 * @param pkt  Packet to inspect (borrowed).
 * @param size Byte length actually received.
 *
 * @return 0 on success, -EBADMSG on any version/stratum/mode problem.
 */
int dmntp_packet_validate(const dmntp_packet_t* pkt, size_t size);

/**
 * @brief Compute offset and round-trip delay from the four timestamps.
 *
 * @param t1     Originate (client send) timestamp.
 * @param t2     Receive (server recv) timestamp.
 * @param t3     Transmit (server send) timestamp.
 * @param t4     Destination (client recv) timestamp.
 * @param offset Output: clock offset in seconds (signed).
 * @param delay  Output: round-trip delay in seconds (unsigned).
 *
 * @return 0 on success, negative errno on arithmetic failure.
 */
int dmntp_calculate_offset(const dmntp_timestamp_t* t1, const dmntp_timestamp_t* t2,
    const dmntp_timestamp_t* t3, const dmntp_timestamp_t* t4, double* offset, double* delay);

/**
 * @brief Compute the root-mean-square jitter of a set of offsets.
 *
 * @param offsets Array of offset measurements in seconds.
 * @param count   Number of entries (must be >= 2).
 *
 * @return RMS deviation from the median.
 */
double dmntp_calculate_jitter(const double* offsets, size_t count);

/**
 * @brief Serialise an NTP packet to its on-wire byte representation.
 *
 * @param packet Packet to encode.
 * @param bytes  Output buffer of at least DMNTP_PACKET_SIZE bytes.
 */
void dmntp_packet_encode(const dmntp_packet_t* packet, uint8_t* bytes);

/**
 * @brief Parse a received NTP packet from its on-wire byte representation.
 *
 * @param bytes  Input buffer of at least DMNTP_PACKET_SIZE bytes.
 * @param packet Output: decoded packet.
 */
void dmntp_packet_decode(const uint8_t* bytes, dmntp_packet_t* packet);

/**
 * @brief Marzullo selection over a set of NTP samples.
 *
 * Picks the sample whose interval intersects the most other intervals,
 * falling back to the sample with the smallest distance if no
 * intersection exists.
 *
 * @param samples Array of Marzullo samples.
 * @param count   Number of samples.
 * @param offset  Output: selected clock offset in seconds.
 * @param jitter  Output: jitter estimate in seconds.
 *
 * @return 0 on success, -EINVAL on bad input.
 */
int dmntp_select(const dmntp_result_t* samples, size_t count, double* offset, double* jitter);

/**
 * @brief Free every server entry currently held by the handle.
 *
 * @param handle Valid dmntp handle.
 */
void dmntp_clear_servers(dmntp_t handle);

/**
 * @brief Initialise the handle from DMNTP_SERVERS / built-in defaults.
 *
 * @param handle Valid dmntp handle.
 *
 * @return 0 on success, negative errno on failure.
 */
int dmntp_config_init(dmntp_t handle);

/**
 * @brief Return the module-level singleton handle (or NULL).
 */
dmntp_t dmntp_get_instance(void);

/**
 * @brief Snapshot the discovery-provided servers into a fresh handle.
 *
 * @param snapshot Output: a new handle populated from the discovery DIF.
 *
 * @return 0 on success, negative errno on failure.
 */
int dmntp_discover(dmntp_t* snapshot);

/**
 * @brief Initialise the UDP query subsystem (binds the receive port).
 *
 * @return 0 on success, negative errno on failure.
 */
int dmntp_query_init(void);

/**
 * @brief Tear down the UDP query subsystem.
 */
void dmntp_query_deinit(void);

/**
 * @brief Synchronous NTP query using the handle's server list.
 *
 * @param handle     Valid dmntp handle with servers configured.
 * @param timeout    Per-server timeout in milliseconds.
 * @param seconds    Output: POSIX UTC seconds.
 * @param ns         Output: sub-second component in nanoseconds.
 * @param rtt        Output: round-trip time in milliseconds (may be NULL).
 * @param jitter     Output: jitter in milliseconds (may be NULL).
 *
 * @return 0 on success, negative errno on failure.
 */
int dmntp_query_sync(dmntp_t handle, uint32_t timeout, uint64_t* seconds,
    uint32_t* ns, uint32_t* rtt, uint32_t* jitter);

#endif /* DMNTP_INTERNAL_H */
