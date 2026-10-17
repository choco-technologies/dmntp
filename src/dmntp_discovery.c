/**
 * @file dmntp_discovery.c
 * @brief The servers other modules know: the dmntp_provide_servers DIF.
 *
 * The providers are looked for on every call, not registered: one that is
 * unloaded is simply not found again, and a lease's servers are never kept
 * after it ended.
 *
 * ENABLE_DIF_REGISTRATIONS is set here, in the only file that uses
 * dmod_dmntp_provide_servers_sig: the loader does not relocate pointers in
 * .data, so the signature has to be seen with its initialiser where it is
 * used (see dmdns_servers.c).
 */
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmntp_internal.h"

/** "255.255.255.255" and its NUL */
#define IPV4_TEXT_SIZE      16u

/**
 * @brief The sink given to the providers: one server into the list
 */
static int add_address(void* ctx, const uint8_t* ipv4)
{
    /* (0.x.x.x and multicast ones are no servers) */
    if (ipv4 == NULL || ipv4[0] == 0 || ipv4[0] >= 224)
        return 0;

    char host[IPV4_TEXT_SIZE];
    Dmod_SnPrintf(host, sizeof(host), "%u.%u.%u.%u", ipv4[0], ipv4[1], ipv4[2], ipv4[3]);
    (void)dmntp_add_server((dmntp_t)ctx, host);
    return 0;
}

/* The loader relocates a module's GOT but not the pointers in its .data */
static dmntp_server_sink_t const volatile address_sink DMOD_GLOBAL_POINTER = add_address;

void dmntp_discover(dmntp_t ntp)
{
    Dmod_Context_t* module = Dmod_GetNextDifModule(dmod_dmntp_provide_servers_sig, NULL);

    while (module != NULL)
    {
        dmod_dmntp_provide_servers_t provide = (dmod_dmntp_provide_servers_t)
            Dmod_GetDifFunction(module, dmod_dmntp_provide_servers_sig);
        if (provide != NULL)
            provide(address_sink, ntp);
        module = Dmod_GetNextDifModule(dmod_dmntp_provide_servers_sig, module);
    }
}
