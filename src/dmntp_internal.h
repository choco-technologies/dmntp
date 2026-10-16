/**
 * @file dmntp_internal.h
 * @brief Internal definitions for dmntp module.
 */

#ifndef DMNTP_INTERNAL_H
#define DMNTP_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>

#include "dmod_types.h"
#include "dmntp.h"
#include "dmntp_protocol.h"

/**
 * Magic number for dmntp context validation.
 */
#define DMNTP_MAGIC 0x444D4E54U  /* 'DMNT' */

/**
 * Internal server entry.
 */
typedef struct dmntp_server
{
    char* host;                  /**< Hostname or IP string. */
    uint8_t addr[16];            /**< Binary address (IPv4 or IPv6). */
    size_t addr_len;             /**< Address length (4 or 16). */
    bool is_ipv6;                /**< True if IPv6. */
    struct dmntp_server* next;   /**< Next in list. */
} dmntp_server_t;

/**
 * Pending NTP query state.
 */
typedef struct dmntp_query
{
    uint8_t  server_addr[16];    /**< Server address. */
    size_t   addr_len;           /**< 4 or 16. */
    uint16_t local_port;         /**< Ephemeral port we bound. */
    uint64_t t1;                 /**< Origin timestamp (client send). */
    bool     responded;           /**< Got a response. */
    struct dmntp_query* next;    /**< Next in list. */
} dmntp_query_t;

/**
 * NTP result from a single server.
 */
typedef struct dmntp_result
{
    uint8_t  addr[16];           /**< Server address. */
    size_t   addr_len;
    double   offset;              /**< Clock offset in seconds. */
    double   delay;               /**< Round-trip delay in seconds. */
    uint8_t  stratum;             /**< Server stratum. */
    double   jitter;              /**< Jitter estimate (for future use). */
    bool     valid;               /**< Whether this result is usable. */
} dmntp_result_t;

/**
 * Main dmntp context structure.
 */
struct dmntp
{
    uint32_t magic;               /**< Magic for validation. */
    dmntp_server_t* servers;      /**< List of configured NTP servers. */
    dmntp_query_t* pending;       /**< Pending queries. */
    uint32_t server_count;        /**< Number of configured servers. */
    bool     initialized;         /**< True after dmod_init(). */
};

/* Codec functions (in dmntp_codec.c) */
uint64_t dmntp_ntp_to_unix(uint32_t ntp_seconds);
uint32_t dmntp_unix_to_ntp(uint64_t unix_seconds);
uint32_t dmntp_frac_to_ns(uint32_t fraction);
uint32_t dmntp_ns_to_frac(uint32_t ns);
dmntp_timestamp_t dmntp_timestamp_add_ns(dmntp_timestamp_t ts, uint32_t ns);
void dmntp_packet_init_client(dmntp_packet_t* pkt);
int dmntp_packet_validate(const dmntp_packet_t* pkt, size_t size);
int dmntp_calculate_offset(const dmntp_timestamp_t* orig,
                           const dmntp_timestamp_t* recv,
                           const dmntp_timestamp_t* trans,
                           const dmntp_timestamp_t* now,
                           double* offset,
                           double* delay);
double dmntp_calculate_jitter(const double* offsets, size_t count);

/* Query (in dmntp_query.c) - to be implemented */
int dmntp_query_sync(dmntp_t h, uint64_t* unix_seconds, uint32_t* nanoseconds,
                     uint32_t* rtt_ms, uint32_t* jitter_ms, uint32_t timeout_ms);

#endif /* DMNTP_INTERNAL_H */
