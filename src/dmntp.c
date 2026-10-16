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
    if (Dmod_IsFunctionConnected(Dmod_Printf)) {
        Dmod_Printf("[dmntp] initialized\n");
    }
    return 0;
}

/**
 * @brief De-initialization function for the module.
 */
int dmod_deinit(void)
{
    if (Dmod_IsFunctionConnected(Dmod_Printf)) {
        Dmod_Printf("[dmntp] deinitialized\n");
    }
    return 0;
}
