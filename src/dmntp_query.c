/**
 * @file dmntp_query.c
 * @brief The exchanges with the servers and dmntp_sync().
 *
 * One exchange at a time (g_sync_lock): the request goes from a port of its
 * own, the reply is taken by receive() - in the network's receive path - if
 * it comes from that server and port and echoes the request's transmit
 * timestamp (a random one), and the waiting thread is woken.
 */

#include "dmntp_internal.h"
#include "dmudp.h"
#include "dmdns.h"

#include <string.h>

/** One exchange, while it is waited for */
typedef struct
{
    dmip_addr_t         server;
    uint16_t            port;           /* The local one */
    dmntp_timestamp_t   nonce;          /* The request's transmit timestamp */
    int64_t             sent_ns;
    dmosi_semaphore_t   ready;
    bool                done;
    int                 result;
    dmntp_sample_t      sample;
} exchange_t;

static dmosi_mutex_t    g_sync_lock;    /* One dmntp_sync() at a time */
static dmosi_mutex_t    g_active_lock;  /* g_active, against receive() */
static exchange_t*      g_active;

/**
 * @brief The local clock, in nanoseconds
 */
static int64_t local_ns(void)
{
    return (int64_t)Dmod_GetUptime() * 1000000LL;
}

/* ===========================================================================
 *                                 Receiving
 * ========================================================================== */

/**
 * @brief Whether a datagram is the reply to the exchange
 */
static bool is_reply(const exchange_t* x, const dmip_addr_t* src, uint16_t source_port, uint16_t dest_port)
{
    return src != NULL && src->family == dmip_family_v4 && source_port == DMNTP_PORT &&
           dest_port == x->port && memcmp(src->addr.v4, x->server.addr.v4, 4) == 0;
}

/**
 * @brief The UDP handler of the exchanges' ports
 */
static void receive(const dmip_addr_t* src, uint16_t source_port, uint16_t dest_port,
                    dmnetif_iface_t iface, const uint8_t* bytes, size_t length)
{
    (void)iface;
    int64_t received_ns = local_ns();

    dmosi_mutex_lock(g_active_lock);
    exchange_t* x = g_active;
    if (x != NULL && !x->done && bytes != NULL && length >= DMNTP_PACKET_SIZE &&
        is_reply(x, src, source_port, dest_port))
    {
        dmntp_packet_t reply;
        dmntp_packet_decode(bytes, &reply);

        /* (a reply to another request - e.g. an earlier one - is not this one's) */
        if (reply.orig.seconds == x->nonce.seconds && reply.orig.fraction == x->nonce.fraction)
        {
            x->result = dmntp_packet_validate(&reply);
            if (x->result == 0)
                x->result = dmntp_sample(&reply, x->sent_ns, received_ns, &x->sample);
            x->done = true;
            (void)dmosi_semaphore_post(x->ready, 1);
        }
    }
    dmosi_mutex_unlock(g_active_lock);
}

/* The loader relocates a module's GOT but not the pointers in its .data:
 * the handler's address has to be taken through DMOD_GLOBAL_POINTER. */
static dmudp_datagram_handler_t const volatile receiver DMOD_GLOBAL_POINTER = receive;

/* ===========================================================================
 *                                An exchange
 * ========================================================================== */

/**
 * @brief Send the request and wait for the reply
 */
static int send_and_wait(exchange_t* x, uint32_t timeout_ms)
{
    dmntp_packet_t request;
    uint8_t bytes[DMNTP_PACKET_SIZE];

    dmntp_packet_init_client(&request);
    x->nonce.seconds = dmosi_rand32();
    x->nonce.fraction = dmosi_rand32();
    request.trans = x->nonce;
    dmntp_packet_encode(&request, bytes);

    dmosi_mutex_lock(g_active_lock);
    g_active = x;
    dmosi_mutex_unlock(g_active_lock);

    x->sent_ns = local_ns();
    int rc = dmudp_send(&x->server, x->port, DMNTP_PORT, bytes, sizeof(bytes), timeout_ms);
    if (rc == 0 && dmosi_semaphore_wait(x->ready, 1, (int32_t)timeout_ms) != 0)
        rc = -ETIMEDOUT;

    /* After this receive() no longer sees x (it lives on this stack) */
    dmosi_mutex_lock(g_active_lock);
    g_active = NULL;
    rc = (rc == 0 && x->done) ? x->result : (rc == 0 ? -ETIMEDOUT : rc);
    dmosi_mutex_unlock(g_active_lock);

    return rc;
}

/**
 * @brief The first IPv4 address of a host
 */
static int resolve(const char* host, dmip_addr_t* address, uint32_t timeout_ms)
{
    dmip_addr_t* addresses = NULL;
    size_t count = 0;

    int rc = dmdns_resolve(host, dmip_family_v4, &addresses, &count, timeout_ms);
    if (rc == 0 && count == 0)
        rc = -ENODATA;
    if (rc == 0)
        *address = addresses[0];

    Dmod_Free(addresses);
    return rc;
}

/**
 * @brief An exchange with a server
 */
static int ask(const char* host, uint32_t timeout_ms, dmntp_sample_t* sample)
{
    exchange_t x;
    memset(&x, 0, sizeof(x));

    int rc = resolve(host, &x.server, timeout_ms);
    if (rc != 0)
        return rc;

    x.ready = dmosi_semaphore_create(0, 1);
    if (x.ready == NULL)
        return -ENOMEM;

    rc = dmudp_bind_any(receiver, &x.port);
    if (rc == 0)
    {
        rc = send_and_wait(&x, timeout_ms);
        dmudp_unbind(x.port);
    }
    if (rc == 0)
        *sample = x.sample;

    dmosi_semaphore_destroy(x.ready);
    return rc;
}

/* ===========================================================================
 *                                 Syncing
 * ========================================================================== */

/**
 * @brief Ask every server of a list: the samples of the ones that answered
 *
 * @return How many samples; *error: the last failure's error
 */
static size_t ask_all(dmntp_t ntp, uint32_t timeout_ms, dmntp_sample_t* samples, int* error)
{
    size_t count = 0;

    for (dmntp_server_t* s = ntp->servers; s != NULL; s = s->next)
    {
        int rc = ask(s->host, timeout_ms, &samples[count]);
        if (rc == 0)
            count++;
        else
            *error = rc;
    }
    return count;
}

/**
 * @brief The time the samples agree on, now
 */
static int conclude(const dmntp_sample_t* samples, size_t count, dmntp_time_t* time)
{
    int64_t offset_ns;
    int rc = dmntp_select(samples, count, &offset_ns, &time->error_ns, &time->servers);
    if (rc != 0)
        return rc;

    int64_t now = local_ns() + offset_ns;
    if (now < 0)
        return -ERANGE;
    time->seconds = now / 1000000000LL;
    time->nanosecond = (uint32_t)(now % 1000000000LL);

    int64_t delay = samples[0].delay_ns;
    for (size_t i = 1; i < count; i++)
        delay = (samples[i].delay_ns < delay) ? samples[i].delay_ns : delay;
    time->delay_us = (uint32_t)(delay / 1000);
    return 0;
}

dmod_dmntp_api_declaration(2.0, int, _sync_with, (dmntp_t ntp, uint32_t timeout_ms, dmntp_time_t* time))
{
    if (time == NULL)
        return -EINVAL;
    memset(time, 0, sizeof(*time));
    if (!dmntp_is_valid(ntp) || timeout_ms > DMNTP_MAX_TIMEOUT_MS || g_sync_lock == NULL)
        return -EINVAL;
    if (timeout_ms == 0)
        timeout_ms = DMNTP_DEFAULT_TIMEOUT_MS;

    dmosi_mutex_lock(g_sync_lock);
    dmosi_mutex_lock(ntp->lock);

    int rc = -ENODATA;
    dmntp_sample_t* samples = (ntp->count > 0) ? Dmod_Malloc(ntp->count * sizeof(*samples)) : NULL;
    if (samples == NULL && ntp->count > 0)
        rc = -ENOMEM;
    else if (samples != NULL)
    {
        size_t count = ask_all(ntp, timeout_ms, samples, &rc);
        if (count > 0)
            rc = conclude(samples, count, time);
        Dmod_Free(samples);
    }

    dmosi_mutex_unlock(ntp->lock);
    dmosi_mutex_unlock(g_sync_lock);

    if (rc != 0)
        memset(time, 0, sizeof(*time));
    return rc;
}

dmod_dmntp_api_declaration(2.0, int, _sync, (uint32_t timeout_ms, dmntp_time_t* time))
{
    if (time == NULL)
        return -EINVAL;
    memset(time, 0, sizeof(*time));

    dmntp_t servers = dmntp_default_servers();
    if (servers == NULL)
        return -ENOMEM;

    int rc = dmntp_sync_with(servers, timeout_ms, time);
    dmntp_destroy(servers);
    return rc;
}

/* ===========================================================================
 *                                Lifecycle
 * ========================================================================== */

int dmntp_query_init(void)
{
    g_sync_lock = dmosi_mutex_create(false);
    g_active_lock = dmosi_mutex_create(false);
    if (g_sync_lock == NULL || g_active_lock == NULL)
    {
        dmntp_query_deinit();
        return -ENOMEM;
    }
    return 0;
}

void dmntp_query_deinit(void)
{
    if (g_sync_lock != NULL)
        dmosi_mutex_destroy(g_sync_lock);
    if (g_active_lock != NULL)
        dmosi_mutex_destroy(g_active_lock);
    g_sync_lock = NULL;
    g_active_lock = NULL;
}
