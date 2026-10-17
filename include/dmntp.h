#ifndef DMNTP_H
#define DMNTP_H

/**
 * @file dmntp.h
 * @brief dmntp - the time of NTP servers (SNTP client, NTPv4, IPv4).
 *
 * dmntp_sync() asks the servers once - each of them one request - and
 * returns UTC together with how far it may be off. It keeps no clock of its
 * own and never touches the RTC: dmutc does that. dmntp is one of dmutc's
 * external UTC sources (the dmutc_read_source DIF): dmutc asks it after the
 * start and then once a day, and sets the RTC from it.
 *
 * Servers: DMNTP_SERVERS (a comma-separated list) when it is set, else the
 * ones the dmntp_provide_servers DIF reports (e.g. dmdhcp: DHCP option 42),
 * else pool.ntp.org and time.google.com.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "dmod_types.h"
#include "dmntp_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A list of NTP servers (opaque)
 */
typedef struct dmntp* dmntp_t;

/**
 * @brief What dmntp_sync() found out
 */
typedef struct
{
    int64_t     seconds;        /**< UTC: POSIX seconds, at the return of dmntp_sync() */
    uint32_t    nanosecond;     /**< 0 .. 999999999 */
    uint32_t    servers;        /**< The servers that agree on it */
    uint64_t    error_ns;       /**< How far it may be from UTC: half the width of the servers' common interval */
    uint32_t    delay_us;       /**< The shortest round trip of them */
} dmntp_time_t;

/**
 * @brief Callback of a dmntp_provide_servers implementation: one server
 *
 * @param ctx   As passed to dmntp_provide_servers()
 * @param ipv4  The server's IPv4 address, four bytes in network order (borrowed)
 *
 * @return 0 to go on, else the provider stops
 */
typedef int (*dmntp_server_sink_t)(void* ctx, const uint8_t* ipv4);

/**
 * @brief DIF of a module that knows NTP servers - e.g. dmdhcp (option 42)
 *
 * Asked on every dmntp_sync() (unless DMNTP_SERVERS is set): report what is
 * valid now. Called from the syncing thread, never from a receive callback.
 *
 * @param add       Callback to report one server with
 * @param sink_ctx  Context to pass back to `add`
 */
dmod_dmntp_dif(1.0, void, _provide_servers, (dmntp_server_sink_t add, void* sink_ctx));

/**
 * @brief Make an empty list of servers
 *
 * @return The list, NULL: no memory
 */
dmod_dmntp_api(2.0, dmntp_t, _create, (void));

/**
 * @brief Free a list made by dmntp_create()
 *
 * @param ntp The list (NULL: nothing)
 */
dmod_dmntp_api(2.0, void, _destroy, (dmntp_t ntp));

/**
 * @brief Whether a list is a valid one
 *
 * @param ntp The list
 *
 * @return true: made by dmntp_create() and not freed
 */
dmod_dmntp_api(2.0, bool, _is_valid, (dmntp_t ntp));

/**
 * @brief Add a server to a list
 *
 * @param ntp   The list
 * @param host  An IPv4 address ("192.0.2.1") or a name ("pool.ntp.org"), copied
 *
 * @return 0 (also when it is there already), -EINVAL, -ENOMEM
 */
dmod_dmntp_api(2.0, int, _add_server, (dmntp_t ntp, const char* host));

/**
 * @brief Remove a server from a list
 *
 * @param ntp   The list
 * @param host  As given to dmntp_add_server()
 *
 * @return 0, -ENOENT: not in the list, -EINVAL
 */
dmod_dmntp_api(2.0, int, _remove_server, (dmntp_t ntp, const char* host));

/**
 * @brief The time of the servers (see the file's description for which ones)
 *
 * Blocks: each server is resolved and asked in turn. Must not be called
 * from the network's receive path.
 *
 * @param timeout_ms    For each name lookup and each exchange (0: 3000 ms, at most 60000 ms)
 * @param time          The time, cleared on failure
 *
 * @return 0; -ENODATA: no server answered; -EAGAIN: no majority of them
 *         agrees; -EINVAL; else the error of the last server that failed
 */
dmod_dmntp_api(2.0, int, _sync, (uint32_t timeout_ms, dmntp_time_t* time));

/**
 * @brief The time of the servers of a list
 *
 * As dmntp_sync(), with the servers of `ntp` only.
 *
 * @param ntp           The list
 * @param timeout_ms    For each name lookup and each exchange (0: 3000 ms, at most 60000 ms)
 * @param time          The time, cleared on failure
 *
 * @return As dmntp_sync()
 */
dmod_dmntp_api(2.0, int, _sync_with, (dmntp_t ntp, uint32_t timeout_ms, dmntp_time_t* time));

#ifdef __cplusplus
}
#endif

#endif /* DMNTP_H */
