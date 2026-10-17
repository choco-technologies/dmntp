#ifndef DMNTP_H
#define DMNTP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "dmod_types.h"
#include "dmntp_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a dmntp instance.
 *
 * The struct definition lives in src/dmntp.c - callers only ever see this
 * forward declaration and pass the pointer between dmntp API functions.
 */
typedef struct dmntp* dmntp_t;

/**
 * @brief Sink callback for discovery providers feeding IPv4 servers.
 *
 * @param ctx        Opaque pointer passed through from the caller of the
 *                   discovery provider; never inspected by dmntp itself.
 * @param ipv4       Borrowed pointer to exactly four bytes of an IPv4
 *                   address in network byte order.
 *
 * @return 0 to keep collecting, non-zero to abort the walk. The provider
 *         must not hold any lock that the sink needs, and the sink must
 *         not re-enter the provider.
 */
typedef int (*dmntp_server_sink_t)(void* ctx, const uint8_t* ipv4);

/**
 * @brief DIF signature type for dmntp discovery providers.
 *
 * Each provider publishes its own function via this type; dmntp walks every
 * provider once per sync() call (before falling back to the configured
 * server list) to collect IPv4 servers without taking a DHCP dependency.
 */
typedef void (*dmod_dmntp_provide_servers_t)(dmntp_server_sink_t add, void* sink_ctx);

/**
 * @brief DIF signature string for discovery providers.
 *
 * Literal storage avoids an unrelocated pointer in a module's read-only
 * data section.
 */
#define dmod_dmntp_provide_servers_sig DMOD_DIF_SIGNATURE_PREFIX "_provide_servers@dmntp:1.0/0.1"

/**
 * @brief Allocate a new dmntp handle.
 *
 * @return A valid handle on success, NULL if the allocation or the
 *         internal mutex could not be created. The handle starts empty
 *         (no servers configured); call dmntp_add_server() or set
 *         DMNTP_SERVERS in the environment before dmntp_sync().
 */
dmod_dmntp_api(1.0, dmntp_t, _create, (void));

/**
 * @brief Free a handle created by dmntp_create().
 *
 * Safe to call with NULL or with a handle from another module that was
 * never initialised. After the call the handle is invalid and must not
 * be passed to any other dmntp API.
 *
 * @param handle Handle returned by dmntp_create(); may be NULL.
 */
dmod_dmntp_api(1.0, void, _destroy, (dmntp_t handle));

/**
 * @brief Check whether a handle is currently valid.
 *
 * @param handle Handle to test; may be NULL.
 *
 * @return true if handle is non-NULL and its magic field matches
 *         DMNTP_MAGIC, false otherwise.
 */
dmod_dmntp_api(1.0, bool, _is_valid, (dmntp_t handle));

/**
 * @brief Append a server to the handle's static list.
 *
 * @param handle Valid dmntp handle.
 * @param host   IPv4 literal ("192.0.2.1") or DNS name ("pool.ntp.org").
 *
 * @return 0 on success, -EINVAL if the handle or host is invalid,
 *         -ENOSPC if the handle already holds 8 distinct servers,
 *         -ENOMEM on allocation failure. Duplicate adds succeed without
 *         adding another entry.
 */
dmod_dmntp_api(1.0, int, _add_server, (dmntp_t handle, const char* host));

/**
 * @brief Remove a server from the handle's static list.
 *
 * @param handle Valid dmntp handle.
 * @param host   Same string that was passed to dmntp_add_server().
 *
 * @return 0 on success, -ENOENT if the host is not in the list,
 *         -EINVAL if the handle is invalid.
 */
dmod_dmntp_api(1.0, int, _remove_server, (dmntp_t handle, const char* host));

/**
 * @brief Block until time has been sampled from at least one server.
 *
 * Resolves the configured server list (DMNTP_SERVERS environment plus
 * any discovery providers), runs the Marzullo selection over the
 * responses and writes the best UTC time back to the caller. No RTC is
 * touched and no background polling is started.
 *
 * Must not be called from a network receive callback (the pump must
 * stay runnable while sync() blocks on a semaphore).
 *
 * @param timeout_ms   Per-DNS attempt and per-NTP exchange; 0 selects
 *                     3000 ms. Maximum 60000 ms.
 * @param seconds      Required output: POSIX UTC seconds since 1970.
 * @param nanoseconds  Required output: sub-second component.
 * @param rtt_ms       Optional round-trip time in milliseconds (NULL ok).
 * @param jitter_ms    Optional jitter estimate in milliseconds (NULL ok).
 *
 * @return 0 on success, negative errno on failure. All supplied outputs
 *         are zeroed on failure. Returned time is NTP era 0 (1970..2036).
 */
dmod_dmntp_api(1.0, int, _sync, (uint32_t timeout_ms, uint64_t* seconds,
    uint32_t* nanoseconds, uint32_t* rtt_ms, uint32_t* jitter_ms));

/**
 * @brief Block until time has been sampled using the handle's own list.
 *
 * Same semantics as dmntp_sync() except the caller supplies the handle
 * (and therefore the server list) instead of the module-level singleton.
 *
 * @param handle       Valid dmntp handle with servers configured.
 * @param timeout_ms   Per-DNS attempt and per-NTP exchange; 0 selects
 *                     3000 ms. Maximum 60000 ms.
 * @param seconds      Required output: POSIX UTC seconds since 1970.
 * @param nanoseconds  Required output: sub-second component.
 * @param rtt_ms       Optional round-trip time in milliseconds (NULL ok).
 * @param jitter_ms    Optional jitter estimate in milliseconds (NULL ok).
 *
 * @return 0 on success, negative errno on failure. All supplied outputs
 *         are zeroed on failure.
 */
dmod_dmntp_api(1.0, int, _sync_with, (dmntp_t handle, uint32_t timeout_ms,
    uint64_t* seconds, uint32_t* nanoseconds, uint32_t* rtt_ms, uint32_t* jitter_ms));

#ifdef __cplusplus
}
#endif

#endif /* DMNTP_H */
