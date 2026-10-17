#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmntp.h"
#include <errno.h>
int main(int argc, char** argv)
{
    if (argc > 2) { Dmod_Printf("Usage: ntpprobe [SERVER]\n"); return 2; }
    dmntp_t h = NULL;
    if (argc == 2) {
        h = dmntp_create();
        if (!h) return 2;
        int rc = dmntp_add_server(h, argv[1]);
        if (rc) { dmntp_destroy(h); return 2; }
    }
    uint64_t sec = 0; uint32_t ns = 0, rtt = 0, jitter = 0;
    int rc = h ? dmntp_sync_with(h, 1000, &sec, &ns, &rtt, &jitter) :
        dmntp_sync(1000, &sec, &ns, &rtt, &jitter);
    Dmod_Printf("NTP_RESULT rc=%d seconds_hi=%u seconds_lo=%u ns=%u rtt_ms=%u jitter_ms=%u timed_out=%u\n",
        rc, (unsigned)(sec >> 32), (unsigned)sec, (unsigned)ns, (unsigned)rtt, (unsigned)jitter, (unsigned)(rc == -ETIMEDOUT));
    dmntp_destroy(h);
    return rc ? 1 : 0;
}
