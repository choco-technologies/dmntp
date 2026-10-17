#include "dmntp_internal.h"
#include <stdio.h>
static int add_address(void* context, const uint8_t* ipv4)
{
    if (!ipv4 || !ipv4[0] || ipv4[0] >= 224) return 0;
    /* IPv4 presentation is bounded by 15 characters plus NUL. */
    char host[16];
    Dmod_SnPrintf(host, sizeof(host), "%u.%u.%u.%u", ipv4[0], ipv4[1], ipv4[2], ipv4[3]);
    return dmntp_add_server((dmntp_t)context, host);
}
static dmntp_server_sink_t const volatile address_sink DMOD_GLOBAL_POINTER = add_address;
int dmntp_discover(dmntp_t* snapshot)
{
    *snapshot = NULL;
    Dmod_Context_t* provider = Dmod_GetNextDifModule(dmod_dmntp_provide_servers_sig, NULL);
    if (!provider) return 0;
    dmntp_t h = dmntp_create();
    if (!h) return -ENOMEM;
    do {
        dmod_dmntp_provide_servers_t fn = (dmod_dmntp_provide_servers_t)
            Dmod_GetDifFunction(provider, dmod_dmntp_provide_servers_sig);
        if (fn) fn(address_sink, h);
        provider = Dmod_GetNextDifModule(dmod_dmntp_provide_servers_sig, provider);
    } while (provider);
    if (h->server_count) *snapshot = h;
    else dmntp_destroy(h);
    return 0;
}
