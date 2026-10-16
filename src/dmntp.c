/**
 * @file dmntp.c
 * @brief DMOD lifecycle (dmod_init()/_deinit()) for dmntp
 */
#include "dmod.h"
#include "dmntp_internal.h"

#include <string.h>

dmod_dmntp_api_declaration(1.0, dmntp_t, _create, ( void ))
{
    struct dmntp* instance = Dmod_Malloc(sizeof(*instance));
    if (instance == NULL) {
        return NULL;
    }

    memset(instance, 0, sizeof(*instance));
    instance->magic = DMNTP_MAGIC;
    instance->servers = NULL;
    instance->server_count = 0;
    instance->initialized = false;

    return instance;
}

dmod_dmntp_api_declaration(1.0, void, _destroy, ( dmntp_t handle ))
{
    if (handle == NULL) {
        return;
    }

    if (handle->magic != DMNTP_MAGIC) {
        return;  /* Invalid handle */
    }

    /* Free server list */
    dmntp_clear_servers(handle);

    handle->magic = 0;
    Dmod_Free(handle);
}

dmod_dmntp_api_declaration(1.0, bool, _is_valid, ( dmntp_t handle ))
{
    return handle != NULL && handle->magic == DMNTP_MAGIC;
}

/* Global instance for singleton use */
static dmntp_t g_instance = NULL;

dmntp_t dmntp_get_instance(void)
{
    if (g_instance == NULL)
    {
        g_instance = dmntp_create();
    }
    return g_instance;
}

/**
 * @brief Pre-initialization function for the module.
 */
void dmod_preinit(void)
{
    /* No pre-init needed */
}

/**
 * @brief Initialization function for the module.
 */
int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    
    /* Get or create the global dmntp instance */
    dmntp_t h = dmntp_get_instance();
    if (h == NULL)
    {
        return -ENOMEM;
    }
    
    /* Initialize server configuration */
    int result = dmntp_config_init(h);
    if (result != 0)
    {
        return result;
    }
    
    h->initialized = true;
    
    if (Dmod_IsFunctionConnected(Dmod_Printf)) {
        Dmod_Printf("[dmntp] initialized with %u servers\n", h->server_count);
    }
    return 0;
}

/**
 * @brief De-initialization function for the module.
 */
int dmod_deinit(void)
{
    if (g_instance != NULL)
    {
        dmntp_destroy(g_instance);
        g_instance = NULL;
    }
    
    if (Dmod_IsFunctionConnected(Dmod_Printf)) {
        Dmod_Printf("[dmntp] deinitialized\n");
    }
    return 0;
}
