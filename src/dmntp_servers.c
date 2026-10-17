/**
 * @file dmntp_servers.c
 * @brief Adding servers to a list and removing them.
 */

#include "dmntp_internal.h"

#include <string.h>

/** Longest host name (RFC 1035) */
#define MAX_HOST_LENGTH     253u

/**
 * @brief Whether a list has a server (with its lock taken)
 */
static bool has_server(dmntp_t ntp, const char* host)
{
    for (dmntp_server_t* s = ntp->servers; s != NULL; s = s->next)
    {
        if (strcmp(s->host, host) == 0)
            return true;
    }
    return false;
}

/**
 * @brief Free a server
 */
static void free_server(dmntp_server_t* server)
{
    Dmod_Free(server->host);
    Dmod_Free(server);
}

dmod_dmntp_api_declaration(2.0, int, _add_server, (dmntp_t ntp, const char* host))
{
    if (!dmntp_is_valid(ntp) || host == NULL || host[0] == '\0' || strlen(host) > MAX_HOST_LENGTH)
        return -EINVAL;

    dmntp_server_t* server = Dmod_Malloc(sizeof(*server));
    char* copy = Dmod_StrDup(host);
    if (server == NULL || copy == NULL)
    {
        Dmod_Free(server);
        Dmod_Free(copy);
        return -ENOMEM;
    }
    server->host = copy;

    dmosi_mutex_lock(ntp->lock);
    bool added = !has_server(ntp, host);
    if (added)
    {
        /* At the end: the servers are asked in the order they were added */
        dmntp_server_t** last = &ntp->servers;
        while (*last != NULL)
            last = &(*last)->next;
        server->next = NULL;
        *last = server;
        ntp->count++;
    }
    dmosi_mutex_unlock(ntp->lock);

    if (!added)
        free_server(server);
    return 0;
}

dmod_dmntp_api_declaration(2.0, int, _remove_server, (dmntp_t ntp, const char* host))
{
    if (!dmntp_is_valid(ntp) || host == NULL)
        return -EINVAL;

    dmntp_server_t* removed = NULL;

    dmosi_mutex_lock(ntp->lock);
    for (dmntp_server_t** slot = &ntp->servers; *slot != NULL; slot = &(*slot)->next)
    {
        if (strcmp((*slot)->host, host) == 0)
        {
            removed = *slot;
            *slot = removed->next;
            ntp->count--;
            break;
        }
    }
    dmosi_mutex_unlock(ntp->lock);

    if (removed == NULL)
        return -ENOENT;
    free_server(removed);
    return 0;
}

void dmntp_clear_servers(dmntp_t ntp)
{
    dmosi_mutex_lock(ntp->lock);
    while (ntp->servers != NULL)
    {
        dmntp_server_t* server = ntp->servers;
        ntp->servers = server->next;
        free_server(server);
    }
    ntp->count = 0;
    dmosi_mutex_unlock(ntp->lock);
}
