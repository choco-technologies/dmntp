/**
 * @file dmntp_query.c
 * @brief NTP query implementation over UDP
 */

#include "dmntp_internal.h"

#include "dmod.h"
#include "dmudp.h"
#include "dmip.h"
#include "dmosi.h"

#include <string.h>
#include <errno.h>
#include <time.h>

/* NTP query context */
typedef struct dmntp_query_ctx
{
    dmntp_result_t   result;
    dmosi_semaphore_t sem;
    bool              responded;
    uint16_t          local_port;
    dmip_addr_t       server;
} dmntp_query_ctx_t;

/**
 * Receive handler for NTP responses.
 */
static void ntp_receive_handler(const dmip_addr_t* src, uint16_t src_port, uint16_t dst_port,
                               const void* payload, size_t payload_len, void* user_data)
{
    dmntp_query_ctx_t* ctx = (dmntp_query_ctx_t*)user_data;
    
    (void)src_port;
    (void)dst_port;
    
    if (ctx == NULL || payload == NULL || payload_len < sizeof(dmntp_packet_t))
    {
        return;
    }
    
    const dmntp_packet_t* pkt = (const dmntp_packet_t*)payload;
    
    /* Validate response */
    if (dmntp_packet_validate(pkt, payload_len) != 0)
    {
        return;
    }
    
    /* Store response data */
    ctx->result.valid = true;
    ctx->result.stratum = pkt->stratum;
    
    /* TODO: Calculate actual offset/delay from timestamps */
    /* For now, just mark as responded */
    ctx->responded = true;
    dmosi_semaphore_post(ctx->sem);
}

/**
 * Query a single NTP server synchronously.
 */
static int query_single_server(dmntp_query_ctx_t* ctx, uint32_t timeout_ms)
{
    int result;
    dmntp_packet_t pkt;
    
    /* Build NTP request */
    dmntp_packet_init_client(&pkt);
    
    /* Get current time for T1 */
    /* TODO: Use actual system time */
    pkt.orig.seconds = 0;
    pkt.orig.fraction = 0;
    
    /* Send query */
    result = dmudp_send(&ctx->server, ctx->local_port, DMNTP_PORT, &pkt, sizeof(pkt), timeout_ms);
    if (result != 0)
    {
        return result;
    }
    
    /* Wait for response */
    if (dmosi_semaphore_wait(ctx->sem, 1, (int32_t)timeout_ms) != 0)
    {
        return -ETIMEDOUT;
    }
    
    if (!ctx->responded)
    {
        return -ETIMEDOUT;
    }
    
    return 0;
}

/**
 * Resolve hostname to IP address.
 */
static int resolve_hostname(const char* hostname, dmip_addr_t* out_addr)
{
    /* TODO: Use dmdns to resolve hostname */
    /* For now, try to parse as IP literal */
    
    /* Try IPv4 */
    memset(out_addr, 0, sizeof(*out_addr));
    
    /* Simple IPv4 parsing */
    unsigned int a, b, c, d;
    if (sscanf(hostname, "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
    {
        if (a <= 255 && b <= 255 && c <= 255 && d <= 255)
        {
            out_addr->family = dmip_family_v4;
            out_addr->addr.v4[0] = (uint8_t)a;
            out_addr->addr.v4[1] = (uint8_t)b;
            out_addr->addr.v4[2] = (uint8_t)c;
            out_addr->addr.v4[3] = (uint8_t)d;
            return 0;
        }
    }
    
    return -EINVAL;
}

/**
 * Main sync function - queries configured servers and returns best time.
 */
int dmntp_query_sync(dmntp_t h, uint64_t* unix_seconds, uint32_t* nanoseconds,
                     uint32_t* rtt_ms, uint32_t* jitter_ms, uint32_t timeout_ms)
{
    if (!h || !h->servers || !unix_seconds || !nanoseconds)
    {
        return -EINVAL;
    }
    
    if (h->server_count == 0)
    {
        return -ENODATA;
    }
    
    /* Query each server and collect results */
    dmntp_result_t results[4];
    size_t result_count = 0;
    
    dmntp_query_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    
    /* Create semaphore for response */
    ctx.sem = dmosi_semaphore_create(0);
    if (ctx.sem == NULL)
    {
        return -ENOMEM;
    }
    
    /* Bind to ephemeral port */
    int result = dmudp_bind_any(ntp_receive_handler, &ctx.local_port);
    if (result != 0)
    {
        dmosi_semaphore_destroy(ctx.sem);
        return result;
    }
    
    /* Query each server */
    dmntp_server_t* server = h->servers;
    while (server && result_count < 4)
    {
        /* Resolve hostname */
        result = resolve_hostname(server->host, &ctx.server);
        if (result != 0)
        {
            server = server->next;
            continue;
        }
        
        /* Reset state */
        ctx.responded = false;
        ctx.result.valid = false;
        
        /* Query server */
        result = query_single_server(&ctx, timeout_ms);
        
        if (result == 0 && ctx.result.valid)
        {
            results[result_count++] = ctx.result;
        }
        
        server = server->next;
    }
    
    /* Cleanup */
    dmudp_unbind(ctx.local_port);
    dmosi_semaphore_destroy(ctx.sem);
    
    if (result_count == 0)
    {
        return -ETIMEDOUT;
    }
    
    /* TODO: Apply Marzullo algorithm to select best time */
    /* For now, return first valid result */
    
    *unix_seconds = 0;
    *nanoseconds = 0;
    *rtt_ms = 0;
    *jitter_ms = 0;
    
    return 0;
}
