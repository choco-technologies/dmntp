/**
 * @file dmntp.c
 * @brief The module's lifecycle and the lists of servers (dmntp_t).
 */

#include "dmntp_internal.h"

#include <string.h>

/* ===========================================================================
 *                              Lists of servers
 * ========================================================================== */

dmod_dmntp_api_declaration(2.0, dmntp_t, _create, (void))
{
    dmntp_t ntp = Dmod_Malloc(sizeof(*ntp));
    if (ntp == NULL)
        return NULL;

    memset(ntp, 0, sizeof(*ntp));
    ntp->lock = dmosi_mutex_create(false);
    if (ntp->lock == NULL)
    {
        Dmod_Free(ntp);
        return NULL;
    }

    ntp->magic = DMNTP_MAGIC;
    return ntp;
}

dmod_dmntp_api_declaration(2.0, void, _destroy, (dmntp_t ntp))
{
    if (!dmntp_is_valid(ntp))
        return;

    dmntp_clear_servers(ntp);
    dmosi_mutex_destroy(ntp->lock);
    ntp->magic = 0;
    Dmod_Free(ntp);
}

dmod_dmntp_api_declaration(2.0, bool, _is_valid, (dmntp_t ntp))
{
    return ntp != NULL && ntp->magic == DMNTP_MAGIC;
}

/* ===========================================================================
 *                                 Lifecycle
 * ========================================================================== */

int dmod_init(const Dmod_Config_t* config)
{
    (void)config;
    return dmntp_query_init();
}

int dmod_deinit(void)
{
    dmntp_query_deinit();
    return 0;
}
