/**
 * @file dmntp_registrations.c
 * @brief dmntp's Built-in API in .dmod.inputs, and the DIF it implements:
 *        dmutc_read_source - dmntp is one of dmutc's external UTC sources.
 *
 * In a translation unit of its own: the registration array covers every
 * function of dmntp.h, defining it twice makes duplicates. A DIF
 * implementation is registered only from such a unit.
 *
 * dmutc is header-only here: dmntp calls none of its functions, so it is no
 * required module of dmntp - dmutc finds dmntp through the DIF.
 */
#define DMOD_ENABLE_REGISTRATION ON
#include "dmntp.h"
#include "dmutc.h"

#include <errno.h>
#include <string.h>

/**
 * @brief dmutc_read_source (see dmutc.h): the servers' time and its error
 */
dmod_dmutc_dif_api_declaration(1.0, dmntp, int, _read_source, (dmutc_time_t* time, uint64_t* error_ns))
{
    if (time == NULL || error_ns == NULL)
        return -EINVAL;

    dmntp_time_t ntp;
    int rc = dmntp_sync(0, &ntp);

    memset(time, 0, sizeof(*time));
    *error_ns = 0;
    if (rc == 0)
    {
        time->seconds = ntp.seconds;
        time->nanosecond = ntp.nanosecond;
        *error_ns = ntp.error_ns;
    }
    return rc;
}
