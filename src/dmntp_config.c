/**
 * @file dmntp_config.c
 * @brief Configuration loading - ini file + dmdhcp integration
 */

#include "dmntp_internal.h"

#include "dmod.h"
#include "dmdhcp.h"

#include <string.h>
#include <errno.h>

/* Default NTP servers if none configured */
static const char* g_default_servers[] = {
    "pool.ntp.org",
    "time.google.com",
    NULL
};

/**
 * Load servers from ini section.
 * Format: serverN=hostname (e.g., server0=pool.ntp.org)
 */
static int load_from_ini(dmntp_t h, const char* section)
{
    (void)section;
    /* TODO: Parse from ini config file
     * For now, use default servers if none configured */
    if (h->server_count == 0)
    {
        for (size_t i = 0; g_default_servers[i] != NULL; i++)
        {
            int result = dmntp_add_server(h, g_default_servers[i]);
            if (result != 0 && result != -EEXIST)
            {
                return result;
            }
        }
    }
    return 0;
}

/**
 * Load servers from dmdhcp NTP option.
 */
static int load_from_dhcp(dmntp_t h)
{
    /* Note: dmdhcp_get_ntp_servers() will be available when dmdhcp is updated
     * For now, this function is a placeholder that returns -ENOSYS */
    (void)h;
    return -ENOSYS;
    
    /* TODO: Enable when dmdhcp is updated:
    dmroute_addr_t servers[8];
    int count = dmdhcp_get_ntp_servers(servers, 8);
    ...
    */
}

/**
 * Initialize server list from all sources.
 */
int dmntp_config_init(dmntp_t h)
{
    if (!h || h->magic != DMNTP_MAGIC)
    {
        return -EINVAL;
    }

    /* Load from ini config first */
    int result = load_from_ini(h, "ntp");
    if (result != 0)
    {
        return result;
    }

    /* Then try to get servers from DHCP */
    load_from_dhcp(h);

    return 0;
}

/**
 * Reload configuration (e.g., on network change).
 */
int dmntp_config_reload(dmntp_t h)
{
    if (!h || h->magic != DMNTP_MAGIC)
    {
        return -EINVAL;
    }

    /* Clear and reload */
    dmntp_clear_servers(h);
    return dmntp_config_init(h);
}
