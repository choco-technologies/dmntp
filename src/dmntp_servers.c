/**
 * @file dmntp_servers.c
 * @brief Server list management for dmntp.
 */

#include "dmntp_internal.h"
#include "dmod.h"

#include <string.h>

/**
 * Free a server list.
 */
static void free_server_list(dmntp_server_t** head)
{
    while (*head) {
        dmntp_server_t* next = (*head)->next;
        Dmod_Free((*head)->host);
        Dmod_Free(*head);
        *head = next;
    }
}

/**
 * Add a server to the list (no duplicates).
 */
int dmntp_add_server(dmntp_t h, const char* host)
{
    if (!h || !host || !h->servers) {
        return -EINVAL;
    }

    /* Check for duplicate */

    dmntp_server_t* s = h->servers;
    while (s) {
        if (strcmp(s->host, host) == 0) {
            return 0;  /* Already exists */
        }
        s = s->next;
    }

    /* Allocate new server entry */
    dmntp_server_t* new_server = Dmod_Malloc(sizeof(*new_server));
    if (!new_server) {
        return -ENOMEM;
    }

    memset(new_server, 0, sizeof(*new_server));
    new_server->host = Dmod_StrDup(host);
    if (!new_server->host) {
        Dmod_Free(new_server);
        return -ENOMEM;
    }

    /* Add to front of list */
    new_server->next = h->servers;
    h->servers = new_server;
    h->server_count++;

    return 0;
}

/**
 * Remove a server from the list.
 */
int dmntp_remove_server(dmntp_t h, const char* host)
{
    if (!h || !host || !h->servers) {
        return -EINVAL;
    }

    dmntp_server_t** prev = &h->servers;
    dmntp_server_t* s = h->servers;

    while (s) {
        if (strcmp(s->host, host) == 0) {
            *prev = s->next;
            Dmod_Free(s->host);
            Dmod_Free(s);
            h->server_count--;
            return 0;
        }
        prev = &s->next;
        s = s->next;
    }

    return -ENOENT;
}

/**
 * Clear all servers from the list.
 */
void dmntp_clear_servers(dmntp_t h)
{
    if (!h) {
        return;
    }
    free_server_list(&h->servers);
    h->server_count = 0;
}

/**
 * Get a copy of the current server list.
 *
 * @param h         dmntp handle.
 * @param out_servers  Output array (caller must free each->host and the array).
 * @return Number of servers, or negative on error.
 */
size_t dmntp_get_server_list(dmntp_t h, dmntp_server_t** out_servers)
{
    if (!h || !out_servers) {
        return 0;
    }

    *out_servers = NULL;

    if (h->server_count == 0) {
        return 0;
    }

    dmntp_server_t* list = Dmod_Malloc(h->server_count * sizeof(*list));
    if (!list) {
        return 0;
    }

    dmntp_server_t* src = h->servers;
    size_t copied = 0;

    while (src && copied < h->server_count) {
        list[copied].host = Dmod_StrDup(src->host);
        if (!list[copied].host) {
            /* Cleanup on failure */
            for (size_t i = 0; i < copied; i++) {
                Dmod_Free(list[i].host);
            }
            Dmod_Free(list);
            return -ENOMEM;
        }
        memcpy(list[copied].addr, src->addr, sizeof(src->addr));
        list[copied].addr_len = src->addr_len;
        list[copied].is_ipv6 = src->is_ipv6;
        list[copied].next = NULL;
        src = src->next;
        copied++;
    }

    *out_servers = list;
    return copied;
}
