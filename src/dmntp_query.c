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
                               dmnetif_iface_t iface, const uint8_t* payload, size_t payload_len)
{
    dmntp_query_ctx_t* ctx = (dmntp_query_ctx_t*)iface;  /* Passed as user_data via iface */
    
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
    dmosi_semaphore_post(ctx->sem, 1);
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
 * Parse IPv4 address from string.
 */
static int parse_ipv4(const char* str, uint8_t* out)
{
    int a, b, c, d;
    char c1, c2, c3;
    
    /* Simple parsing without sscanf */
    const char* p = str;
    a = b = c = d = 0;
    
    /* Parse first octet */
    while (*p >= '0' && *p <= '9')
    {
        a = a * 10 + (*p - '0');
        if (a > 255) return -1;
        p++;
    }
    if (*p != '.') return -1;
    p++;
    
    /* Parse second octet */
    while (*p >= '0' && *p <= '9')
    {
        b = b * 10 + (*p - '0');
        if (b > 255) return -1;
        p++;
    }
    if (*p != '.') return -1;
    p++;
    
    /* Parse third octet */
    while (*p >= '0' && *p <= '9')
    {
        c = c * 10 + (*p - '0');
        if (c > 255) return -1;
        p++;
    }
    if (*p != '.') return -1;
    p++;
    
    /* Parse fourth octet */
    while (*p >= '0' && *p <= '9')
    {
        d = d * 10 + (*p - '0');
        if (d > 255) return -1;
        p++;
    }
    
    if (*p != '\0') return -1;
    
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    
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
    
    if (parse_ipv4(hostname, out_addr->addr.v4) == 0)
    {
        out_addr->family = dmip_family_v4;
        return 0;
    }
    
    return -EINVAL;
}

/**
 * Marzullo algorithm for selecting the best time from multiple samples.
 * 
 * Finds the intersection of all intervals and selects the sample with
 * the smallest maximum error (root distance).
 */
static int select_best_offset(dmntp_result_t* results, size_t count, double* best_offset)
{
    if (count == 0 || results == NULL || best_offset == NULL)
    {
        return -EINVAL;
    }
    
    if (count == 1)
    {
        *best_offset = results[0].offset;
        return 0;
    }
    
    /* Find the interval intersection */
    /* Each sample defines an interval: [offset - delay, offset + delay] */
    /* We want the intersection of all intervals */
    
    double low = -1e9;  /* Start with very wide interval */
    double high = 1e9;
    
    for (size_t i = 0; i < count; i++)
    {
        if (!results[i].valid)
        {
            continue;
        }
        
        double sample_low = results[i].offset - results[i].delay;
        double sample_high = results[i].offset + results[i].delay;
        
        if (sample_low > low)
        {
            low = sample_low;
        }
        if (sample_high < high)
        {
            high = sample_high;
        }
    }
    
    /* Check if there's a valid intersection */
    if (low > high)
    {
        /* No intersection - select sample with smallest root distance */
        double min_root_dist = 1e9;
        int best_idx = -1;
        
        for (size_t i = 0; i < count; i++)
        {
            if (!results[i].valid)
            {
                continue;
            }
            
            /* Root distance = dispersion + delay/2 */
            double root_dist = results[i].delay;  /* Simplified */
            
            if (root_dist < min_root_dist)
            {
                min_root_dist = root_dist;
                best_idx = (int)i;
            }
        }
        
        if (best_idx < 0)
        {
            return -EINVAL;
        }
        
        *best_offset = results[best_idx].offset;
        return 0;
    }
    
    /* Intersection exists - select midpoint */
    *best_offset = (low + high) / 2.0;
    
    return 0;
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
    ctx.sem = dmosi_semaphore_create(0, 1);
    if (ctx.sem == NULL)
    {
        return -ENOMEM;
    }
    
    /* Bind to ephemeral port */
    int result = dmudp_bind_any((dmudp_datagram_handler_t)ntp_receive_handler, &ctx.local_port);
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
    
    /* Apply Marzullo algorithm to select best time */
    double best_offset;
    result = select_best_offset(results, result_count, &best_offset);
    if (result != 0)
    {
        return result;
    }
    
    /* Convert offset to time */
    /* TODO: Apply offset to system clock */
    
    *unix_seconds = 0;
    *nanoseconds = 0;
    *rtt_ms = 0;
    *jitter_ms = 0;
    
    return 0;
}

/**
 * Public sync API implementation.
 */
dmod_dmntp_api_declaration(1.0, int, _sync, ( uint32_t timeout_ms, uint64_t* out_unix_seconds, uint32_t* out_nanoseconds, uint32_t* out_rtt_ms, uint32_t* out_jitter_ms ))
{
    dmntp_t h = dmntp_get_instance();
    if (h == NULL)
    {
        return -ENODEV;
    }
    
    if (!h->initialized)
    {
        return -EIO;
    }
    
    return dmntp_query_sync(h, out_unix_seconds, out_nanoseconds, out_rtt_ms, out_jitter_ms, timeout_ms);
}
