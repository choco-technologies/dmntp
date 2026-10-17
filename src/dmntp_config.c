#include "dmntp_internal.h"
#include <string.h>
/* DMNTP_SERVERS is a comma-separated override, read once on module load.
 * Explicit per-handle servers are available through add_server/sync_with. */
int dmntp_config_init(dmntp_t h)
{
    const char* configured = Dmod_GetEnv("DMNTP_SERVERS");
    if (!configured) {
        h->discover = true;
        int rc = dmntp_add_server(h, "pool.ntp.org");
        return rc ? rc : dmntp_add_server(h, "time.google.com");
    }
    if (!*configured) return -EINVAL;
    char* copy = Dmod_StrDup(configured);
    if (!copy) return -ENOMEM;
    char* start = copy;
    int rc = 0;
    for (char* end = copy;; ++end) {
        if (*end && *end != ',') continue;
        bool last = !*end; *end = 0;
        rc = dmntp_add_server(h, start);
        if (rc || last) break;
        start = end + 1;
    }
    Dmod_Free(copy);
    return rc;
}
