/* Hardware integration: injected DHCP frames traverse the real stack;
 * subsequent NTP queries use eth0 and real Internet servers. The fixture
 * interface has no default route, so it cannot replace eth0's routing. */
#define DMOD_ENABLE_REGISTRATION ON
#define ENABLE_DIF_REGISTRATIONS ON

#include "dmod.h"
#include "dmntp.h"
#include "dmdhcp.h"
#include "dmudp.h"
#include "dmip.h"
#include "dmnetif.h"
#include "dmnetbridge.h"
#include "dmosi.h"
#include "libsystemd.h"

#include <string.h>
#include <errno.h>


/* ===========================================================================
 *                                  Constants
 * ========================================================================== */

/** NTP server option code per RFC 2132. */
#define TEST_DHCP_OPT_NTP_SERVER  42u

/** Maximum DHCP payload size we have to encode. */
#define TEST_MAX_DHCP_LEN         320u

/** RFC 894 Ethernet header length. */
#define TEST_ETH_HEADER_LEN       14u

/** IPv4 pseudo-header length used by the UDP checksum. */
#define TEST_V4_PSEUDO_HEADER_LEN 12u

/** EtherType for IPv4. */
#define TEST_ETHERTYPE_IPV4       0x0800u

/** Sample timeout for dmntp_sync in this test. */
#define TEST_NTP_TIMEOUT_MS       1000u

/** Minimum plausible Unix time used to validate a real NTP response. */
#define TEST_MIN_PLAUSIBLE_UNIX   1700000000ULL

/** NTP option code (RFC 5905) - placed in `stratum` to express "kiss". */
#define TEST_NTP_STRATUM_KISS     0u

/** Server that never answers (RFC 5737 TEST-NET-1). */
static const uint8_t TEST_SILENT_SERVER[4] = {192, 0, 2, 1};

/** Live time.google.com server. */
static const uint8_t TEST_LIVE_SERVER[4]   = {216, 239, 35, 0};


/* ===========================================================================
 *                       Fixture-wide NTP option override
 * ========================================================================== */

/**
 * @brief When non-NULL, the next DHCP message built will include this data
 *        as option 42 (NTP servers). Length is `g_fixture_ntp_length`.
 */
static const uint8_t* g_fixture_ntp;
static uint8_t g_fixture_ntp_length;


/* ===========================================================================
 *                              Test helpers
 * ========================================================================== */

/**
 * @brief Construct a `dmroute_addr_t` from four IPv4 octets.
 */
static dmroute_addr_t make_v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    dmroute_addr_t addr = { 0 };

    addr.family = dmroute_family_v4;
    addr.addr.v4[0] = a;
    addr.addr.v4[1] = b;
    addr.addr.v4[2] = c;
    addr.addr.v4[3] = d;

    return addr;
}

/** @brief Write a 16-bit value to a byte buffer in network byte order. */
static void write_u16_be(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value & 0xFFu);
}

/** @brief Write a 32-bit value to a byte buffer in network byte order. */
static void write_u32_be(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)(value & 0xFFu);
}


/* ===========================================================================
 *                          Frame injection plumbing
 * ========================================================================== */

/**
 * @brief Push a raw IPv4 datagram through dmnetbridge as if it had just
 *        arrived on `iface`.
 */
static void feed_frame(dmnetif_iface_t iface, const uint8_t* packet, size_t packet_len)
{
    size_t frame_len = TEST_ETH_HEADER_LEN + packet_len;
    uint8_t* frame = Dmod_Malloc(frame_len);

    memset(frame, 0, TEST_ETH_HEADER_LEN);
    write_u16_be(&frame[12], TEST_ETHERTYPE_IPV4);
    memcpy(frame + TEST_ETH_HEADER_LEN, packet, packet_len);

    Dmod_Context_t* implementor = NULL;
    while ((implementor = Dmod_GetNextDifModule(dmod_dmnetbridge_packet_received_sig, implementor)) != NULL)
    {
        dmod_dmnetbridge_packet_received_t fn =
            (dmod_dmnetbridge_packet_received_t)Dmod_GetDifFunction(implementor, dmod_dmnetbridge_packet_received_sig);

        if (fn != NULL) {
            fn(iface, frame, frame_len);
        }
    }

    Dmod_Free(frame);
}


/* ===========================================================================
 *                              DHCP payload
 * ========================================================================== */

/**
 * @brief Build a DHCP message with the chosen options.
 *
 * Any of `server_id`/`netmask`/`router`/`dns` may be NULL to omit that
 * option, `lease_time_sec` == 0 to omit options 51/58/59. If the fixture
 * NTP option is set (`g_fixture_ntp` non-NULL), option 42 is included.
 *
 * @return Number of bytes written to `out`, or 0 on failure.
 */
static size_t build_dhcp_payload(uint8_t* out, size_t out_cap, dmdhcp_msg_type_t msg_type, uint32_t xid,
                                  const dmroute_addr_t* yiaddr, const dmroute_addr_t* server_id,
                                  const dmroute_addr_t* netmask, const dmroute_addr_t* router,
                                  const dmroute_addr_t* dns, uint32_t lease_time_sec)
{
    dmdhcp_message_t header = { 0 };

    header.op = dmdhcp_op_bootreply;
    header.htype = 1;
    header.hlen = 6;
    header.xid = xid;
    header.flags = DMDHCP_FLAG_BROADCAST;

    if (yiaddr != NULL) {
        header.yiaddr = *yiaddr;
    }

    dmdhcp_build_message(out, out_cap, &header);

    size_t options_offset = DMDHCP_FIXED_HEADER_LEN + 4u;
    uint8_t msg_type_byte = (uint8_t)msg_type;
    uint8_t lease_bytes[4];
    write_u32_be(lease_bytes, lease_time_sec);

    dmdhcp_option_t options[7];
    size_t n = 0;

    options[n++] = (dmdhcp_option_t){ .code = DMDHCP_OPT_MSG_TYPE, .length = 1, .data = &msg_type_byte };
    if (server_id != NULL) options[n++] = (dmdhcp_option_t){ .code = DMDHCP_OPT_SERVER_ID, .length = 4, .data = server_id->addr.v4 };
    if (netmask  != NULL) options[n++] = (dmdhcp_option_t){ .code = DMDHCP_OPT_SUBNET_MASK, .length = 4, .data = netmask->addr.v4 };
    if (router   != NULL) options[n++] = (dmdhcp_option_t){ .code = DMDHCP_OPT_ROUTER,    .length = 4, .data = router->addr.v4 };
    if (dns      != NULL) options[n++] = (dmdhcp_option_t){ .code = DMDHCP_OPT_DNS_SERVER, .length = 4, .data = dns->addr.v4 };
    if (lease_time_sec > 0) options[n++] = (dmdhcp_option_t){ .code = DMDHCP_OPT_LEASE_TIME, .length = 4, .data = lease_bytes };
    if (g_fixture_ntp)      options[n++] = (dmdhcp_option_t){ .code = TEST_DHCP_OPT_NTP_SERVER, .length = g_fixture_ntp_length, .data = g_fixture_ntp };

    size_t options_len = 0;
    dmdhcp_options_write(&out[options_offset], out_cap - options_offset, options, n, &options_len);

    return options_offset + options_len;
}


/* ===========================================================================
 *                       UDP / IPv4 framing of a DHCP reply
 * ========================================================================== */

/**
 * @brief Wrap a DHCP payload in a correctly-checksummed UDP-over-IPv4
 *        datagram (server port 67 -> client port 68) and feed it into the
 *        fixture interface.
 */
static void feed_dhcp_message(dmnetif_iface_t iface, dmdhcp_msg_type_t msg_type, uint32_t xid,
                               const dmroute_addr_t* server_ip, const dmroute_addr_t* yiaddr,
                               const dmroute_addr_t* server_id, const dmroute_addr_t* netmask,
                               const dmroute_addr_t* router, const dmroute_addr_t* dns,
                               uint32_t lease_time_sec)
{
    uint8_t* dhcp = Dmod_Malloc(TEST_MAX_DHCP_LEN);
    size_t dhcp_len = build_dhcp_payload(dhcp, TEST_MAX_DHCP_LEN, msg_type, xid,
                                         yiaddr, server_id, netmask, router, dns, lease_time_sec);

    uint8_t* udp = Dmod_Malloc(DMUDP_HEADER_LEN + TEST_MAX_DHCP_LEN);
    size_t udp_len = DMUDP_HEADER_LEN + dhcp_len;
    dmudp_header_t udp_header = {
        .src_port = DMDHCP_SERVER_PORT,
        .dst_port = DMDHCP_CLIENT_PORT,
        .length = (uint16_t)udp_len,
    };
    dmudp_build_header(udp, udp_len, &udp_header);
    memcpy(udp + DMUDP_HEADER_LEN, dhcp, dhcp_len);

    dmroute_addr_t broadcast = make_v4(255, 255, 255, 255);
    const dmroute_addr_t* dst = &broadcast;

    uint8_t* pseudo_and_segment = Dmod_Malloc(TEST_V4_PSEUDO_HEADER_LEN + DMUDP_HEADER_LEN + TEST_MAX_DHCP_LEN);
    memcpy(&pseudo_and_segment[0], server_ip->addr.v4, DMROUTE_IPV4_ADDR_LEN);
    memcpy(&pseudo_and_segment[4], dst->addr.v4, DMROUTE_IPV4_ADDR_LEN);
    pseudo_and_segment[8] = 0;
    pseudo_and_segment[9] = DMIP_PROTO_UDP;
    write_u16_be(&pseudo_and_segment[10], (uint16_t)udp_len);
    memcpy(&pseudo_and_segment[TEST_V4_PSEUDO_HEADER_LEN], udp, udp_len);
    write_u16_be(&udp[6], dmip_checksum(pseudo_and_segment, TEST_V4_PSEUDO_HEADER_LEN + udp_len));

    dmip_v4_header_t ip_header = { 0 };
    ip_header.total_length = (uint16_t)(DMIP_V4_HEADER_LEN + udp_len);
    ip_header.ttl = DMIP_DEFAULT_TTL;
    ip_header.protocol = DMIP_PROTO_UDP;
    ip_header.src = *server_ip;
    ip_header.dst = *dst;

    uint8_t* packet = Dmod_Malloc(DMIP_V4_HEADER_LEN + DMUDP_HEADER_LEN + TEST_MAX_DHCP_LEN);
    dmip_v4_build_header(packet, DMIP_V4_HEADER_LEN + DMUDP_HEADER_LEN + TEST_MAX_DHCP_LEN, &ip_header);
    memcpy(packet + DMIP_V4_HEADER_LEN, udp, udp_len);

    feed_frame(iface, packet, DMIP_V4_HEADER_LEN + udp_len);

    Dmod_Free(packet);
    Dmod_Free(pseudo_and_segment);
    Dmod_Free(udp);
    Dmod_Free(dhcp);
}


/* ===========================================================================
 *                              DHCP state polling
 * ========================================================================== */

/**
 * @brief Poll dmdhcp_get_state() until it leaves `from_state`, or `max_ms`
 *        elapses.
 *
 * Used to wait out the RFC 2131 §4.4.1 conflict-probe hand-off (a
 * dmosi_timer_t firing on a different thread) without hard-coding exactly
 * how long dmarp_resolve() takes in this fixture.
 */
static void wait_while_state(dmdhcp_lease_t lease, dmdhcp_state_t from_state, uint32_t max_ms)
{
    uint32_t waited = 0;

    while (dmdhcp_get_state(lease) == from_state && waited < max_ms) {
        dmosi_thread_sleep(20);
        waited += 20;
    }
}


/* ===========================================================================
 *                              Sample assertions
 * ========================================================================== */

/**
 * @brief Run one dmntp_sync() and decide whether the result is the one
 *        the test expects.
 *
 * @param expect_timeout   true if the test expects no NTP server to reply
 *                        and sync() to time out; false if it expects a
 *                        real (post-2023) timestamp back.
 *
 * @return 0 if the observation matches the expectation, 1 otherwise.
 */
static int sample(bool expect_timeout)
{
    uint64_t sec = 0;
    uint32_t ns = 0;

    int rc = dmntp_sync(TEST_NTP_TIMEOUT_MS, &sec, &ns, NULL, NULL);

    Dmod_Printf("DHCP_NTP_SAMPLE rc=%d unix=%u ns=%u expected_timeout=%u\n",
                rc, (unsigned)sec, ns, expect_timeout);

    if (expect_timeout) {
        return (rc == -ETIMEDOUT && sec == 0 && ns == 0) ? 0 : 1;
    }

    return (rc == 0 && sec > TEST_MIN_PLAUSIBLE_UNIX) ? 0 : 1;
}


/* ===========================================================================
 *                                  Scenarios
 * ========================================================================== */

/**
 * @brief Step 1: lease bound with a silent NTP option - sync() must time out
 *        and prove that the default DNS fallback is NOT being used.
 */
static int run_silent_scenario(dmnetif_iface_t iface, dmdhcp_lease_t lease,
                               const dmroute_addr_t* server, const dmroute_addr_t* offered,
                               const dmroute_addr_t* mask)
{
    g_fixture_ntp = TEST_SILENT_SERVER;
    g_fixture_ntp_length = sizeof(TEST_SILENT_SERVER);

    feed_dhcp_message(iface, dmdhcp_msg_offer, dmdhcp_get_xid(lease), server, offered, server, mask, NULL, NULL, 3600);
    wait_while_state(lease, dmdhcp_state_checking_offer, 1500);

    feed_dhcp_message(iface, dmdhcp_msg_ack,   dmdhcp_get_xid(lease), server, offered, server, mask, NULL, NULL, 3600);
    wait_while_state(lease, dmdhcp_state_checking_ack, 1500);

    if (dmdhcp_get_state(lease) != dmdhcp_state_bound) {
        return 1;
    }

    /* A default fallback would wrongly succeed against a real server. */
    return sample(true);
}

/**
 * @brief Step 2: option 42 changed to a live server - sync() must succeed.
 */
static int run_live_scenario(dmnetif_iface_t iface, dmdhcp_lease_t lease,
                             const dmroute_addr_t* server, const dmroute_addr_t* offered,
                             const dmroute_addr_t* mask)
{
    dmdhcp_renew(lease);

    g_fixture_ntp = TEST_LIVE_SERVER;
    g_fixture_ntp_length = sizeof(TEST_LIVE_SERVER);

    feed_dhcp_message(iface, dmdhcp_msg_ack, dmdhcp_get_xid(lease), server, offered, server, mask, NULL, NULL, 3600);

    /* New option 42 must immediately replace the silent source. */
    return sample(false);
}

/**
 * @brief Step 3: option 42 absent - documented default servers are used
 *        and sync() still succeeds.
 */
static int run_default_scenario(dmnetif_iface_t iface, dmdhcp_lease_t lease,
                                const dmroute_addr_t* server, const dmroute_addr_t* offered,
                                const dmroute_addr_t* mask)
{
    dmdhcp_renew(lease);

    g_fixture_ntp = NULL;
    g_fixture_ntp_length = 0;

    feed_dhcp_message(iface, dmdhcp_msg_ack, dmdhcp_get_xid(lease), server, offered, server, mask, NULL, NULL, 3600);

    return sample(false);
}


/* ===========================================================================
 *                              Fixture teardown
 * ========================================================================== */

/**
 * @brief Bring down the fixture interface and restore the system rules.
 *
 * @return 0 on success, non-zero if rules could not be restored.
 */
static int teardown_fixture(dmnetif_iface_t iface, dmdhcp_lease_t lease)
{
    dmdhcp_stop(lease);
    dmnetif_unregister(iface);
    Dmod_FileRemove("/ntp-probe-device");

    return libsystemd_load_rules("/configs/rules");
}


/* ===========================================================================
 *                                  main
 * ========================================================================== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    /* Isolate the fixture from automatic networkd/dhcpc device rules.
     * Existing eth0 services keep running. Restore rules after removing it. */
    Dmod_MakeDir("/ntp-probe-rules", 0755);
    if (libsystemd_load_rules("/ntp-probe-rules") != 0) {
        return 2;
    }

    void* file = Dmod_FileOpen("/ntp-probe-device", "w+");
    if (!file) {
        libsystemd_load_rules("/configs/rules");
        return 2;
    }
    Dmod_FileClose(file);

    dmnetif_iface_t iface = dmnetif_register("ntpfix", "/ntp-probe-device");
    if (!iface) {
        libsystemd_load_rules("/configs/rules");
        return 2;
    }

    dmdhcp_lease_t lease = dmdhcp_start(iface, NULL, NULL, NULL);
    if (!lease) {
        dmnetif_unregister(iface);
        libsystemd_load_rules("/configs/rules");
        return 2;
    }

    dmroute_addr_t server  = make_v4(10, 111, 0, 1);
    dmroute_addr_t offered = make_v4(10, 111, 0, 50);
    dmroute_addr_t mask    = make_v4(255, 255, 255, 0);

    int failed = 0;
    failed += run_silent_scenario(iface, lease, &server, &offered, &mask);
    failed += run_live_scenario (iface, lease, &server, &offered, &mask);
    failed += run_default_scenario(iface, lease, &server, &offered, &mask);

    if (teardown_fixture(iface, lease) != 0) {
        ++failed;
    }

    Dmod_Printf("NTP_DHCP_INTEGRATION %s\n", failed ? "FAIL" : "PASS");

    return failed ? 1 : 0;
}
