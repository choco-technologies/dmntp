#include "dmntp_internal.h"
#include "dmudp.h"
#include "dmdns.h"
#include <string.h>
#include <limits.h>

typedef struct {
    dmip_addr_t server;
    uint16_t port;
    uint32_t started;
    dmntp_timestamp_t sent;
    dmosi_semaphore_t ready;
    bool received;
    dmntp_result_t result;
} query_t;
static dmosi_mutex_t g_query_lock, g_receive_lock;
static query_t* g_active;
static uint32_t g_sequence, g_started;

static dmntp_timestamp_t local_time(uint32_t ms)
{
    ms -= g_started;
    dmntp_timestamp_t t = {DMNTP_EPOCH_OFFSET + ms / 1000, dmntp_ns_to_frac((ms % 1000) * 1000000u)};
    return t;
}
static double as_seconds(dmntp_timestamp_t t)
{
    return (double)t.seconds + t.fraction / 4294967296.0;
}
static int sample_response(query_t* q, const uint8_t* bytes, size_t length)
{
    dmntp_packet_t packet;
    if (length < DMNTP_PACKET_SIZE) return -EBADMSG;
    dmntp_packet_decode(bytes, &packet);
    if (dmntp_packet_validate(&packet, length) ||
        memcmp(&packet.orig, &q->sent, sizeof(q->sent)) ||
        packet.recv.seconds < DMNTP_EPOCH_OFFSET || packet.trans.seconds < DMNTP_EPOCH_OFFSET)
        return -EBADMSG;
    uint32_t elapsed = (uint32_t)Dmod_GetUptime() - q->started;
    dmntp_timestamp_t now = q->sent;
    now.seconds += elapsed / 1000;
    now = dmntp_timestamp_add_ns(now, (elapsed % 1000) * 1000000u);
    int rc = dmntp_calculate_offset(&q->sent, &packet.recv, &packet.trans, &now,
        &q->result.offset, &q->result.delay);
    if (rc) return rc;
    double root_delay = (double)(int32_t)packet.root_delay / 65536.0;
    if (root_delay < 0) root_delay = 0;
    q->result.distance = q->result.delay / 2 + root_delay / 2 + packet.root_dispersion / 65536.0;
    return q->result.distance > DMNTP_MAX_ROOT_DISTANCE ? -EBADMSG : 0;
}
static void receive_response(const dmip_addr_t* src, uint16_t source_port, uint16_t dest_port,
    dmnetif_iface_t iface, const uint8_t* bytes, size_t length)
{
    (void)iface;
    dmosi_mutex_lock(g_receive_lock);
    query_t* q = g_active;
    if (q && !q->received && bytes && src && source_port == DMNTP_PORT && dest_port == q->port &&
        src->family == dmip_family_v4 && !memcmp(src->addr.v4, q->server.addr.v4, 4) &&
        !sample_response(q, bytes, length)) {
        q->received = true;
        dmosi_semaphore_post(q->ready, 1);
    }
    dmosi_mutex_unlock(g_receive_lock);
}
/* Module callback pointers need loader relocation, including on Cortex-M7. */
static dmudp_datagram_handler_t const volatile receiver DMOD_GLOBAL_POINTER = receive_response;

int dmntp_query_init(void)
{
    g_query_lock = dmosi_mutex_create(false);
    g_receive_lock = dmosi_mutex_create(false);
    if (g_query_lock && g_receive_lock) return 0;
    dmntp_query_deinit(); return -ENOMEM;
}
void dmntp_query_deinit(void)
{
    if (g_query_lock) dmosi_mutex_destroy(g_query_lock);
    if (g_receive_lock) dmosi_mutex_destroy(g_receive_lock);
    g_query_lock = g_receive_lock = NULL;
}
static int resolve_server(const char* host, dmip_addr_t* server, uint32_t timeout)
{
    dmip_addr_t* addresses = NULL; size_t count = 0;
    int rc = dmdns_resolve(host, dmip_family_v4, &addresses, &count, timeout);
    if (!rc && count) *server = addresses[0];
    else if (!rc) rc = -ENODATA;
    Dmod_Free(addresses);
    return rc;
}
static int exchange(query_t* q, uint32_t timeout)
{
    uint8_t bytes[DMNTP_PACKET_SIZE]; dmntp_packet_t packet;
    dmntp_packet_init_client(&packet);
    q->started = (uint32_t)Dmod_GetUptime();
    q->sent = local_time(q->started);
    /* Distinguish exchanges made in the same millisecond. */
    q->sent.fraction += (++g_sequence & 0xffffu);
    packet.trans = q->sent;
    dmntp_packet_encode(&packet, bytes);
    dmosi_mutex_lock(g_receive_lock); g_active = q; dmosi_mutex_unlock(g_receive_lock);
    int rc = dmudp_send(&q->server, q->port, DMNTP_PORT, bytes, sizeof(bytes), timeout);
    if (!rc) {
        uint32_t elapsed = (uint32_t)Dmod_GetUptime() - q->started;
        uint32_t remaining = elapsed < timeout ? timeout - elapsed : 0;
        rc = dmosi_semaphore_wait(q->ready, 1, (int32_t)remaining);
        if (rc) rc = -ETIMEDOUT;
    }
    /* Drain any callback already using q before its semaphore/stack is freed. */
    dmosi_mutex_lock(g_receive_lock); g_active = NULL; dmosi_mutex_unlock(g_receive_lock);
    return rc;
}
static int query_server(const char* host, uint32_t timeout, dmntp_result_t* result)
{
    query_t q; memset(&q, 0, sizeof(q));
    int rc = resolve_server(host, &q.server, timeout);
    if (rc) return rc;
    q.ready = dmosi_semaphore_create(0, 1);
    if (!q.ready) return -ENOMEM;
    rc = dmudp_bind_any(receiver, &q.port);
    if (!rc) {
        rc = exchange(&q, timeout);
        dmudp_unbind(q.port);
    }
    if (!rc) *result = q.result;
    dmosi_semaphore_destroy(q.ready);
    return rc;
}
static int finish_samples(const dmntp_result_t* samples, size_t count, uint64_t* seconds,
    uint32_t* ns, uint32_t* rtt, uint32_t* jitter)
{
    double offset, variation;
    int rc = dmntp_select(samples, count, &offset, &variation);
    if (rc) return rc;
    double now = as_seconds(local_time((uint32_t)Dmod_GetUptime())) + offset - DMNTP_EPOCH_OFFSET;
    if (now < 0 || now >= (double)(UINT32_MAX - DMNTP_EPOCH_OFFSET)) return -ERANGE;
    *seconds = (uint64_t)now;
    *ns = (uint32_t)((now - *seconds) * 1000000000.0);
    double min_delay = samples[0].delay;
    for (size_t i = 1; i < count; ++i) if (samples[i].delay < min_delay) min_delay = samples[i].delay;
    if (rtt) *rtt = (uint32_t)(min_delay * 1000 + 0.5);
    if (jitter) *jitter = (uint32_t)(variation * 1000 + 0.5);
    return 0;
}
int dmntp_query_sync(dmntp_t h, uint32_t timeout, uint64_t* seconds,
    uint32_t* ns, uint32_t* rtt, uint32_t* jitter)
{
    if (seconds) *seconds = 0;
    if (ns) *ns = 0;
    if (rtt) *rtt = 0;
    if (jitter) *jitter = 0;
    if (!dmntp_is_valid(h) || !seconds || !ns || timeout > 60000) return -EINVAL;
    if (!timeout) timeout = 3000;
    dmosi_mutex_lock(g_query_lock);
    dmosi_mutex_lock(h->lock);
    g_started = (uint32_t)Dmod_GetUptime();
    dmntp_result_t samples[DMNTP_MAX_SERVERS]; size_t count = 0;
    int rc = -ENODATA;
    for (dmntp_server_t* s = h->servers; s; s = s->next) {
        int result = query_server(s->host, timeout, &samples[count]);
        if (!result) ++count;
        else rc = result;
    }
    if (count) rc = finish_samples(samples, count, seconds, ns, rtt, jitter);
    dmosi_mutex_unlock(h->lock);
    dmosi_mutex_unlock(g_query_lock);
    return rc;
}
dmod_dmntp_api_declaration(1.0, int, _sync_with, (dmntp_t h, uint32_t timeout,
    uint64_t* seconds, uint32_t* ns, uint32_t* rtt, uint32_t* jitter))
{
    return dmntp_query_sync(h, timeout, seconds, ns, rtt, jitter);
}
dmod_dmntp_api_declaration(1.0, int, _sync, (uint32_t timeout,
    uint64_t* seconds, uint32_t* ns, uint32_t* rtt, uint32_t* jitter))
{
    return dmntp_query_sync(dmntp_get_instance(), timeout, seconds, ns, rtt, jitter);
}
