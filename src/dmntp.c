#include "dmntp_internal.h"
#include <string.h>
static dmntp_t g_instance;
dmod_dmntp_api_declaration(1.0, dmntp_t, _create, (void))
{
    dmntp_t h = Dmod_Malloc(sizeof(*h));
    if (!h) return NULL;
    memset(h, 0, sizeof(*h));
    h->lock = dmosi_mutex_create(false);
    if (!h->lock) { Dmod_Free(h); return NULL; }
    h->magic = DMNTP_MAGIC;
    return h;
}
dmod_dmntp_api_declaration(1.0, void, _destroy, (dmntp_t h))
{
    if (!dmntp_is_valid(h)) return;
    dmntp_clear_servers(h);
    dmosi_mutex_destroy(h->lock);
    h->magic = 0;
    Dmod_Free(h);
}
dmod_dmntp_api_declaration(1.0, bool, _is_valid, (dmntp_t h))
{
    return h && h->magic == DMNTP_MAGIC;
}
dmntp_t dmntp_get_instance(void) { return g_instance; }
int dmod_init(const Dmod_Config_t* config)
{
    (void)config;
    int rc = dmntp_query_init();
    if (rc) return rc;
    g_instance = dmntp_create();
    if (!g_instance) { dmntp_query_deinit(); return -ENOMEM; }
    rc = dmntp_config_init(g_instance);
    if (rc) {
        dmntp_destroy(g_instance); g_instance = NULL;
        dmntp_query_deinit();
    }
    return rc;
}
int dmod_deinit(void)
{
    dmntp_destroy(g_instance); g_instance = NULL;
    dmntp_query_deinit();
    return 0;
}
