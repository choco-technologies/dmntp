/* Deterministic integration test of the real query, codec and lifecycle.
 * Only the platform/transport boundary is replaced; packets use real wire bytes. */
#include "dmntp_internal.h"
#include <assert.h>
#include <stdio.h>
static uint32_t ticks;
static int mode, binds, semaphores, sends;
static dmudp_datagram_handler_t receive;
static dmip_addr_t server = {dmip_family_v4, {{192, 0, 2, 1}}};
enum { VALID, BAD_ORIGIN, BAD_SOURCE, BAD_PORT, SHORT, KISS, ALARM, BAD_STRATUM,
    REVERSED, DISPERSION, SILENT, SEND_ERROR, DNS_ERROR, BAD_THEN_GOOD, DUPLICATE };
uint64_t Dmod_GetUptime(void) { return ticks; }
dmosi_mutex_t dmosi_mutex_create(bool recursive) {
    (void)recursive; pthread_mutex_t* m = malloc(sizeof(*m)); assert(m);
    assert(!pthread_mutex_init(m, NULL)); return m;
}
void dmosi_mutex_destroy(dmosi_mutex_t m) { assert(!pthread_mutex_destroy(m)); free(m); }
int dmosi_mutex_lock(dmosi_mutex_t m) { return pthread_mutex_lock(m); }
int dmosi_mutex_unlock(dmosi_mutex_t m) { return pthread_mutex_unlock(m); }
dmosi_semaphore_t dmosi_semaphore_create(uint32_t count, uint32_t max) {
    (void)max; sem_t* s = malloc(sizeof(*s)); assert(s); assert(!sem_init(s, 0, count)); ++semaphores; return s;
}
void dmosi_semaphore_destroy(dmosi_semaphore_t s) { assert(!sem_destroy(s)); free(s); --semaphores; }
int dmosi_semaphore_post(dmosi_semaphore_t s, uint32_t n) { assert(n == 1); return sem_post(s); }
int dmosi_semaphore_wait(dmosi_semaphore_t s, uint32_t n, int32_t timeout) {
    assert(n == 1); if (!sem_trywait(s)) return 0; ticks += timeout; return -1;
}
int dmudp_bind_any(dmudp_datagram_handler_t fn, uint16_t* port) {
    assert(!binds); receive = fn; *port = 50000; ++binds; return 0;
}
void dmudp_unbind(uint16_t port) { assert(port == 50000); --binds; }
int dmdns_resolve(const char* host, dmip_family_t family, dmip_addr_t** addrs, size_t* count, uint32_t timeout) {
    (void)host; (void)timeout; assert(family == dmip_family_v4);
    if (mode == DNS_ERROR) return -ENOENT;
    *addrs = malloc(sizeof(server)); assert(*addrs); **addrs = server; *count = 1; return 0;
}
static void deliver(const dmip_addr_t* from, uint16_t source, const uint8_t* bytes, size_t length) {
    /* A real interface pointer must never be treated as a query context. */
    receive(from, source, 50000, (void*)(uintptr_t)0x1234, bytes, length);
}
int dmudp_send(const dmip_addr_t* to, uint16_t source, uint16_t dest, const void* input, size_t len, uint32_t timeout) {
    (void)timeout; ++sends;
    assert(!memcmp(to, &server, sizeof(server)) && source == 50000 && dest == 123 && len == 48);
    const uint8_t* request = input; assert(request[0] == 0x23);
    for (unsigned i = 24; i < 40; ++i) assert(!request[i]);
    if (mode == SEND_ERROR) return -ENETUNREACH;
    if (mode == SILENT) return 0;
    uint8_t bytes[48] = {0x24, 2, 0, 0};
    memcpy(bytes + 24, request + 40, 8);
    /* Receive: Unix 1700000000.25; transmit: 1700000000.26.
     * RTT 30ms minus server processing 10ms => 20ms network delay. */
    bytes[32] = bytes[40] = 0xe8; bytes[33] = bytes[41] = 0xfe;
    bytes[34] = bytes[42] = 0x6f; bytes[35] = bytes[43] = 0x80;
    bytes[36] = 0x40; bytes[44] = 0x42; bytes[45] = 0x8f; bytes[46] = 0x5c; bytes[47] = 0x29;
    ticks += 30;
    dmip_addr_t wrong = server; wrong.addr.v4[3]++;
    switch (mode) {
    case BAD_ORIGIN: bytes[24] ^= 1; break;
    case SHORT: deliver(&server, 123, bytes, 47); return 0;
    case KISS: bytes[1] = 0; break;
    case ALARM: bytes[0] |= 0xc0; break;
    case BAD_STRATUM: bytes[1] = 17; break;
    case REVERSED: bytes[44] = 0x30; break;
    case DISPERSION: bytes[9] = 2; break;
    case BAD_SOURCE: deliver(&wrong, 123, bytes, 48); return 0;
    case BAD_PORT: deliver(&server, 124, bytes, 48); return 0;
    case BAD_THEN_GOOD:
        bytes[24] ^= 1; deliver(&server, 123, bytes, 48); bytes[24] ^= 1; break;
    }
    deliver(&server, 123, bytes, 48);
    if (mode == DUPLICATE) deliver(&server, 123, bytes, 48);
    return 0;
}
extern int dmod_init(const Dmod_Config_t*);
extern int dmod_deinit(void);
static void check_sync(dmntp_t h, int expected) {
    uint64_t sec = 99; uint32_t ns = 99, rtt = 99, jitter = 99;
    int rc = dmntp_sync_with(h, 100, &sec, &ns, &rtt, &jitter);
    assert(rc == expected);
    assert(!binds && !semaphores);
    if (!rc) {
        assert(sec == 1700000000ULL); assert(ns > 269000000 && ns < 271000000);
        assert(rtt == 20 && jitter == 0);
    } else assert(!sec && !ns && !rtt && !jitter);
}
int main(void) {
    unsetenv("DMNTP_SERVERS"); assert(!dmod_init(NULL));
    dmntp_t h = dmntp_create(); assert(h); assert(!dmntp_add_server(h, "fixture"));
    for (mode = VALID; mode <= DUPLICATE; ++mode) {
        ticks = 1234;
        int expected = mode == VALID || mode == BAD_THEN_GOOD || mode == DUPLICATE ? 0 :
            mode == SEND_ERROR ? -ENETUNREACH : mode == DNS_ERROR ? -ENOENT : -ETIMEDOUT;
        check_sync(h, expected);
        /* Inject a callback after timeout/teardown: must not touch freed state. */
        if (receive) { uint8_t b[48] = {0}; deliver(&server, 123, b, 48); }
    }
    mode = VALID; ticks = UINT32_MAX - 10; check_sync(h, 0);
    for (unsigned i = 0; i < 100; ++i) check_sync(h, 0);
    dmntp_destroy(h); assert(!dmod_deinit());
    setenv("DMNTP_SERVERS", "192.0.2.1,fixture", 1); assert(!dmod_init(NULL));
    assert(dmntp_get_instance()->server_count == 2); assert(!dmod_deinit());
    setenv("DMNTP_SERVERS", "", 1); assert(dmod_init(NULL) == -EINVAL);
    unsetenv("DMNTP_SERVERS");
    printf("NTP transport/lifecycle: 15 response modes, tick wrap, 100 repeat calls, configuration PASS (sends=%d)\n", sends);
    return 0;
}
