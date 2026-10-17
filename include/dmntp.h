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
typedef struct dmntp* dmntp_t;
dmod_dmntp_api(1.0, dmntp_t, _create, (void));
dmod_dmntp_api(1.0, void, _destroy, (dmntp_t handle));
dmod_dmntp_api(1.0, bool, _is_valid, (dmntp_t handle));
/* IPv4 literals or DNS names, at most 8 distinct servers. Duplicate adds
 * succeed without adding another entry. remove returns -ENOENT if absent.
 * Handles start empty. Do not destroy a handle while another call uses it. */
dmod_dmntp_api(1.0, int, _add_server, (dmntp_t handle, const char* host));
dmod_dmntp_api(1.0, int, _remove_server, (dmntp_t handle, const char* host));
/* Blocking time sampling; no RTC writes or background polling. Never call
 * from a network receive callback (the network pump must remain runnable).
 * timeout_ms: per DNS server attempt and per NTP exchange; 0 selects 3000ms; maximum 60000ms.
 * seconds and nanoseconds are required; rtt/jitter may be NULL. All supplied
 * outputs are zeroed on failure. Returned time is POSIX UTC, NTP era 0
 * (1970..2036). More than one responding server requires majority agreement.
 * Calls serialize across handles. NTP is unauthenticated.
 * sync uses DMNTP_SERVERS (comma-separated, loaded at library initialization),
 * or pool.ntp.org/time.google.com. sync_with uses the caller's server list. */
dmod_dmntp_api(1.0, int, _sync, (uint32_t timeout_ms, uint64_t* seconds,
    uint32_t* nanoseconds, uint32_t* rtt_ms, uint32_t* jitter_ms));
dmod_dmntp_api(1.0, int, _sync_with, (dmntp_t handle, uint32_t timeout_ms,
    uint64_t* seconds, uint32_t* nanoseconds, uint32_t* rtt_ms, uint32_t* jitter_ms));
#ifdef __cplusplus
}
#endif
#endif
