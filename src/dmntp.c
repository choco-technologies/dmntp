/**
 * @file dmntp.c
 * @brief DMOD lifecycle (dmod_init/_deinit) and handle primitives.
 */

#include "dmntp_internal.h"

#include <string.h>


/* ===========================================================================
 *                          Module-level singleton
 * ========================================================================== */

/**
 * @brief Process-wide handle created lazily by dmod_init().
 */
static dmntp_t g_instance;


/* ===========================================================================
 *                           Public handle API
 * ========================================================================== */

/**
 * @brief Allocate a new dmntp handle (see include/dmntp.h).
 */
dmod_dmntp_api_declaration(1.0, dmntp_t, _create, (void))
{
    dmntp_t h = Dmod_Malloc(sizeof(*h));

    if (!h) {
        return NULL;
    }

    memset(h, 0, sizeof(*h));

    h->lock = dmosi_mutex_create(false);
    if (!h->lock) {
        Dmod_Free(h);
        return NULL;
    }

    h->magic = DMNTP_MAGIC;

    return h;
}

/**
 * @brief Free a handle created by dmntp_create() (see include/dmntp.h).
 */
dmod_dmntp_api_declaration(1.0, void, _destroy, (dmntp_t h))
{
    if (!dmntp_is_valid(h)) {
        return;
    }

    dmntp_clear_servers(h);
    dmosi_mutex_destroy(h->lock);

    h->magic = 0;
    Dmod_Free(h);
}

/**
 * @brief Test whether a pointer is a live dmntp handle.
 */
dmod_dmntp_api_declaration(1.0, bool, _is_valid, (dmntp_t h))
{
    return (h != NULL) && (h->magic == DMNTP_MAGIC);
}


/* ===========================================================================
 *                              Module lifecycle
 * ========================================================================== */

/**
 * @brief Return the module-level singleton handle.
 *
 * Created on first call; NULL only if allocation failed during init.
 */
dmntp_t dmntp_get_instance(void)
{
    return g_instance;
}

/**
 * @brief DMOD entry point - brings up the UDP subsystem and singleton.
 */
int dmod_init(const Dmod_Config_t* config)
{
    (void)config;

    int rc = dmntp_query_init();
    if (rc != 0) {
        return rc;
    }

    g_instance = dmntp_create();
    if (!g_instance) {
        dmntp_query_deinit();
        return -ENOMEM;
    }

    rc = dmntp_config_init(g_instance);
    if (rc != 0) {
        dmntp_destroy(g_instance);
        g_instance = NULL;
        dmntp_query_deinit();
    }

    return rc;
}

/**
 * @brief DMOD exit point - tears down the singleton and UDP subsystem.
 */
int dmod_deinit(void)
{
    dmntp_destroy(g_instance);
    g_instance = NULL;

    dmntp_query_deinit();

    return 0;
}
