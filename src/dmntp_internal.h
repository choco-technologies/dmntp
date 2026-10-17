#ifndef DMNTP_INTERNAL_H
#define DMNTP_INTERNAL_H

/**
 * @file dmntp_internal.h
 * @brief What the dmntp sources share: the list of servers, the protocol
 *        arithmetic (dmntp_codec.c) and the exchanges (dmntp_query.c).
 *
 * Times are int64_t nanoseconds: UTC ones since 1970-01-01, local ones
 * since the start (Dmod_GetUptime()). No floating point.
 */

#include <errno.h>

#include "dmod.h"
#include "dmosi.h"
#include "dmntp.h"
#include "dmntp_protocol.h"

/** dmntp_t's magic: "DMNT" */
#define DMNTP_MAGIC             0x444D4E54u

/** Default timeout of a name lookup and of an exchange */
#define DMNTP_DEFAULT_TIMEOUT_MS    3000u

/** Longest timeout accepted */
#define DMNTP_MAX_TIMEOUT_MS    60000u

/** Resolution of the local clock (Dmod_GetUptime(): milliseconds) */
#define DMNTP_CLOCK_RESOLUTION_NS   1000000LL

/** A server of a list: its name or address, as given */
typedef struct dmntp_server
{
    struct dmntp_server*    next;
    char*                   host;
} dmntp_server_t;

/** A list of servers (dmntp_t) */
struct dmntp
{
    uint32_t            magic;
    dmosi_mutex_t       lock;
    dmntp_server_t*     servers;
    size_t              count;
};

/** One exchange's result */
typedef struct
{
    int64_t     offset_ns;      /**< UTC minus the local clock */
    int64_t     delay_ns;       /**< Round trip, without the server's own time */
    int64_t     distance_ns;    /**< How far offset_ns may be off */
} dmntp_sample_t;

/* ---- dmntp_codec.c: the protocol's arithmetic ---- */

/**
 * @brief An NTP timestamp as UTC nanoseconds since 1970
 *
 * NTP's seconds wrap in February 2036: a timestamp before 1968 (the top
 * bit clear) is taken as one of the next era - right until 2104.
 *
 * @param t The timestamp
 *
 * @return Nanoseconds since 1970-01-01 UTC
 */
int64_t dmntp_timestamp_to_ns(const dmntp_timestamp_t* t);

/**
 * @brief A client's request: version 4, mode client, everything else 0
 *
 * @param packet    The packet to fill
 */
void dmntp_packet_init_client(dmntp_packet_t* packet);

/**
 * @brief A packet as it goes on the wire (big endian)
 *
 * @param packet    The packet
 * @param bytes     DMNTP_PACKET_SIZE bytes
 */
void dmntp_packet_encode(const dmntp_packet_t* packet, uint8_t* bytes);

/**
 * @brief A packet from the wire
 *
 * @param bytes     DMNTP_PACKET_SIZE bytes
 * @param packet    The packet
 */
void dmntp_packet_decode(const uint8_t* bytes, dmntp_packet_t* packet);

/**
 * @brief Whether a reply is a usable server's one
 *
 * @param packet    The reply
 *
 * @return 0; -EBADMSG: not version 3/4, not a server, not synchronized,
 *         a kiss-o'-death (stratum 0) or stratum above 15
 */
int dmntp_packet_validate(const dmntp_packet_t* packet);

/**
 * @brief The result of an exchange (RFC 5905's on-wire protocol)
 *
 * @param reply         The server's reply (validated)
 * @param sent_ns       Local time the request was sent at
 * @param received_ns   Local time the reply came at
 * @param sample        The result
 *
 * @return 0; -EBADMSG: the times do not add up, or the server is too far
 *         from a reference (root distance above 1 s)
 */
int dmntp_sample(const dmntp_packet_t* reply, int64_t sent_ns, int64_t received_ns, dmntp_sample_t* sample);

/**
 * @brief The offset the samples agree on (Marzullo's algorithm)
 *
 * Each sample is the interval offset +/- distance. The point most of them
 * contain is found; the intervals that contain it are intersected.
 *
 * @param samples   The samples
 * @param count     How many
 * @param offset_ns The middle of the intersection
 * @param error_ns  Half its width
 * @param agreeing  How many samples it is of
 *
 * @return 0; -EAGAIN: no more than half of them agree; -EINVAL: none
 */
int dmntp_select(const dmntp_sample_t* samples, size_t count, int64_t* offset_ns, uint64_t* error_ns, uint32_t* agreeing);

/* ---- dmntp_query.c: the exchanges ---- */

/**
 * @brief Prepare the exchanges (dmod_init)
 *
 * @return 0, -ENOMEM
 */
int dmntp_query_init(void);

/**
 * @brief Free what dmntp_query_init() made (dmod_deinit)
 */
void dmntp_query_deinit(void);

/* ---- dmntp_servers.c, dmntp_config.c, dmntp_discovery.c: the servers ---- */

/**
 * @brief Remove every server of a list
 *
 * @param ntp The list
 */
void dmntp_clear_servers(dmntp_t ntp);

/**
 * @brief The servers of dmntp_sync(): DMNTP_SERVERS, else the ones the
 *        dmntp_provide_servers DIF reports, else the default ones
 *
 * @return A new list (to dmntp_destroy()), NULL: no memory
 */
dmntp_t dmntp_default_servers(void);

/**
 * @brief Add the servers the dmntp_provide_servers DIF reports
 *
 * @param ntp The list
 */
void dmntp_discover(dmntp_t ntp);

#endif /* DMNTP_INTERNAL_H */
