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
    dmroute_addr_t servers[8];
    int count = dmdhcp_get_ntp_servers(servers, 8);
    
    if (count <= 0)
    {
        return count;  /* No servers or error */
    }

    /* Convert addresses to strings and add to server list */
    for (int i = 0; i < count; i++)
    {
        char buf[32];
        
        /* Format as IP string */
        if (servers[i].family == dmroute_family_v4)
        {
            Dmod_Snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                servers[i].addr.v4[0], servers[i].addr.v4[1],
                servers[i].addr.v4[2], servers[i].addr.v4[3]);
        }
        else
        {
            /* IPv6 not fully supported yet */
            continue;
        }

        dmntp_add_server(h, buf);
    }

    return 0;
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
    (void)load_from_dhcp;  /* Not called yet - dmdhcp not always available */

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
