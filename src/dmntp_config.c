/**
 * @file dmntp_config.c
 * @brief The servers of dmntp_sync().
 */

#include "dmntp_internal.h"

#include <string.h>

/** Asked when nothing else says which servers to ask */
static const char* const default_servers[] = { "pool.ntp.org", "time.google.com" };

/**
 * @brief Add the servers of a comma-separated list
 */
static int add_list(dmntp_t ntp, const char* list)
{
    char* copy = Dmod_StrDup(list);
    if (copy == NULL)
        return -ENOMEM;

    int rc = 0;
    char* host = copy;
    while (rc == 0 && host != NULL)
    {
        char* comma = strchr(host, ',');
        if (comma != NULL)
            *comma = '\0';
        if (host[0] != '\0')
            rc = dmntp_add_server(ntp, host);
        host = (comma != NULL) ? comma + 1 : NULL;
    }

    Dmod_Free(copy);
    return rc;
}

/**
 * @brief Add the default servers
 */
static int add_defaults(dmntp_t ntp)
{
    int rc = 0;
    for (size_t i = 0; rc == 0 && i < sizeof(default_servers) / sizeof(default_servers[0]); i++)
        rc = dmntp_add_server(ntp, default_servers[i]);
    return rc;
}

dmntp_t dmntp_default_servers(void)
{
    dmntp_t ntp = dmntp_create();
    if (ntp == NULL)
        return NULL;

    /* Read on every call: DMNTP_SERVERS can be changed while dmntp is loaded */
    const char* configured = Dmod_GetEnv("DMNTP_SERVERS");
    int rc = 0;
    if (configured != NULL && configured[0] != '\0')
        rc = add_list(ntp, configured);
    else
        dmntp_discover(ntp);

    if (rc == 0 && ntp->count == 0)
        rc = add_defaults(ntp);
    if (rc != 0)
    {
        dmntp_destroy(ntp);
        return NULL;
    }
    return ntp;
}
