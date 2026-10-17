#include "dmntp_internal.h"
#include <string.h>
dmod_dmntp_api_declaration(1.0, int, _add_server, (dmntp_t h, const char* host))
{
    if (!dmntp_is_valid(h) || !host || !*host || strlen(host) > 253) return -EINVAL;
    dmosi_mutex_lock(h->lock);
    int rc = 0;
    for (dmntp_server_t* s = h->servers; s; s = s->next)
        if (!strcmp(s->host, host)) goto done;
    if (h->server_count == DMNTP_MAX_SERVERS) { rc = -ENOSPC; goto done; }
    dmntp_server_t* s = Dmod_Malloc(sizeof(*s));
    if (!s) { rc = -ENOMEM; goto done; }
    s->host = Dmod_StrDup(host);
    if (!s->host) { Dmod_Free(s); rc = -ENOMEM; goto done; }
    s->next = h->servers; h->servers = s; ++h->server_count;
done:
    dmosi_mutex_unlock(h->lock);
    return rc;
}
dmod_dmntp_api_declaration(1.0, int, _remove_server, (dmntp_t h, const char* host))
{
    if (!dmntp_is_valid(h) || !host) return -EINVAL;
    dmosi_mutex_lock(h->lock);
    int rc = -ENOENT;
    for (dmntp_server_t** slot = &h->servers; *slot; slot = &(*slot)->next) {
        dmntp_server_t* s = *slot;
        if (strcmp(s->host, host)) continue;
        *slot = s->next; Dmod_Free(s->host); Dmod_Free(s);
        --h->server_count; rc = 0; break;
    }
    dmosi_mutex_unlock(h->lock);
    return rc;
}
void dmntp_clear_servers(dmntp_t h)
{
    if (!dmntp_is_valid(h)) return;
    dmosi_mutex_lock(h->lock);
    while (h->servers) {
        dmntp_server_t* s = h->servers; h->servers = s->next;
        Dmod_Free(s->host); Dmod_Free(s);
    }
    h->server_count = 0;
    dmosi_mutex_unlock(h->lock);
}
