#ifndef DMNTP_H
#define DMNTP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmntp_defs.h"

/* Note: dmntp_protocol.h contains NTP protocol definitions (timestamps, packet format).
   Other module-specific types are auto-generated during build to dmntp_defs.h */

/**
 * Public API for the dmntp module.
 *
 * Functions are declared with the dmod_dmntp_api(...) macro - dmod's
 * standard pattern for functions callable from other modules (or from this
 * module's own tests/), resolved dynamically by the loader rather than
 * through normal static linkage. See dm_sw_ring/include/dm_sw_ring.h for a
 * fully worked real-world example of the same shape.
 *
 * Definitions in src/dmntp.c use the matching
 * dmod_dmntp_api_declaration(...) macro - a plain C function
 * definition here will NOT satisfy these declarations at link time.
 *
 * This is an example interface using the usual "opaque handle" pattern -
 * replace the handle, functions, and struct definition in
 * src/dmntp.c with your module's real API.
 */

/* Opaque handle - the real struct is defined in src/dmntp.c */
typedef struct dmntp* dmntp_t;

/**
 * Create a new dmntp instance.
 *
 * @return A valid handle on success, or NULL on allocation failure.
 */
dmod_dmntp_api(1.0, dmntp_t, _create, ( void ));

/**
 * Destroy an instance created by dmntp_create(). Safe to call with
 * NULL.
 */
dmod_dmntp_api(1.0, void, _destroy, ( dmntp_t handle ));

dmod_dmntp_api(1.0, bool, _is_valid, ( dmntp_t handle ));

dmod_dmntp_api(1.0, int, _add_server, ( dmntp_t h, const char* host ));

dmod_dmntp_api(1.0, int, _remove_server, ( dmntp_t h, const char* host ));

#endif /* DMNTP_H */
