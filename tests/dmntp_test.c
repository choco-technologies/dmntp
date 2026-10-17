#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmntp_internal.h"

#include <string.h>

#define NS          1000000000LL    /* A second */
#define MS          1000000LL       /* A millisecond */

/* 2026-10-17T08:00:00Z: POSIX seconds, and as NTP's (since 1900) */
#define UNIX_2026   1792224000LL
#define NTP_2026    (uint32_t)(UNIX_2026 + DMNTP_EPOCH_OFFSET)

/**
 * @brief A server's reply: received at `t2`, sent at `t3` (UTC ns, era 0)
 */
static dmntp_packet_t reply_at(int64_t t2, int64_t t3)
{
    dmntp_packet_t p;
    memset(&p, 0, sizeof(p));
    p.li_vn_mode = DMNTP_LI_VN_MODE(0, 4, DMNTP_MODE_SERVER);
    p.stratum = 2;
    p.recv.seconds = (uint32_t)(t2 / NS + DMNTP_EPOCH_OFFSET);
    p.recv.fraction = (uint32_t)(((uint64_t)(t2 % NS) << 32) / (uint64_t)NS);
    p.trans.seconds = (uint32_t)(t3 / NS + DMNTP_EPOCH_OFFSET);
    p.trans.fraction = (uint32_t)(((uint64_t)(t3 % NS) << 32) / (uint64_t)NS);
    return p;
}

/**
 * @brief Whether two times are within a nanosecond of each other (the 2^-32 fractions)
 */
static bool near(int64_t a, int64_t b)
{
    return a - b <= 1 && b - a <= 1;
}

/* ===========================================================================
 *                                Timestamps
 * ========================================================================== */

DMOD_TEST_STEP(timestamps_are_utc_nanoseconds)
{
    dmntp_timestamp_t epoch = { DMNTP_EPOCH_OFFSET, 0 };
    DMOD_TEST_EXPECT_EQ(dmntp_timestamp_to_ns(&epoch), 0);

    dmntp_timestamp_t half = { NTP_2026, 0x80000000u };
    DMOD_TEST_EXPECT_EQ(dmntp_timestamp_to_ns(&half), UNIX_2026 * NS + 500000000LL);
}

DMOD_TEST_STEP(timestamps_after_2036_are_of_the_next_era)
{
    /* NTP's seconds wrap at 2036-02-07T06:28:16Z: 0 is that moment */
    dmntp_timestamp_t wrap = { 0, 0 };
    DMOD_TEST_EXPECT_EQ(dmntp_timestamp_to_ns(&wrap), 2085978496LL * NS);

    dmntp_timestamp_t last = { 0xFFFFFFFFu, 0 };
    DMOD_TEST_EXPECT_EQ(dmntp_timestamp_to_ns(&last), (2085978496LL - 1) * NS);
}

/* ===========================================================================
 *                                 Packets
 * ========================================================================== */

DMOD_TEST_STEP(packets_go_on_the_wire_big_endian)
{
    dmntp_packet_t p, back;
    uint8_t bytes[DMNTP_PACKET_SIZE];

    dmntp_packet_init_client(&p);
    p.trans.seconds = 0x01020304u;
    p.trans.fraction = 0xA0B0C0D0u;
    dmntp_packet_encode(&p, bytes);

    DMOD_TEST_EXPECT_EQ(bytes[0], 0x23);     /* LI 0, version 4, mode 3 (client) */
    DMOD_TEST_EXPECT_EQ(bytes[40], 0x01);
    DMOD_TEST_EXPECT_EQ(bytes[43], 0x04);
    DMOD_TEST_EXPECT_EQ(bytes[44], 0xA0);
    DMOD_TEST_EXPECT_EQ(bytes[47], 0xD0);

    dmntp_packet_decode(bytes, &back);
    DMOD_TEST_EXPECT_EQ(memcmp(&p, &back, sizeof(p)), 0);
}

DMOD_TEST_STEP(only_synchronized_servers_replies_are_taken)
{
    dmntp_packet_t p = reply_at(UNIX_2026 * NS, UNIX_2026 * NS);
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p), 0);

    p.li_vn_mode = DMNTP_LI_VN_MODE(0, 3, DMNTP_MODE_SERVER);
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p), 0);              /* (version 3 servers) */

    p.li_vn_mode = DMNTP_LI_VN_MODE(0, 4, DMNTP_MODE_CLIENT);
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p), -EBADMSG);
    p.li_vn_mode = DMNTP_LI_VN_MODE(DMNTP_LI_ALARM, 4, DMNTP_MODE_SERVER);
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p), -EBADMSG);       /* Not synchronized */
    p.li_vn_mode = DMNTP_LI_VN_MODE(0, 2, DMNTP_MODE_SERVER);
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p), -EBADMSG);

    p.li_vn_mode = DMNTP_LI_VN_MODE(0, 4, DMNTP_MODE_SERVER);
    p.stratum = 0;
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p), -EBADMSG);       /* Kiss-o'-death */
    p.stratum = 16;
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p), -EBADMSG);
}

/* ===========================================================================
 *                            Result of an exchange
 * ========================================================================== */

DMOD_TEST_STEP(an_exchange_gives_the_offset_and_the_delay)
{
    /* Local clock at 5 s; 20 ms each way, 1 ms in the server; UTC = local + 2026 */
    int64_t offset = UNIX_2026 * NS;
    int64_t sent = 5 * NS;
    dmntp_packet_t p = reply_at(sent + 20 * MS + offset, sent + 21 * MS + offset);
    dmntp_sample_t s;

    DMOD_TEST_EXPECT_EQ(dmntp_sample(&p, sent, sent + 41 * MS, &s), 0);
    DMOD_TEST_EXPECT_TRUE(near(s.offset_ns, offset));
    DMOD_TEST_EXPECT_TRUE(near(s.delay_ns, 40 * MS));
    /* delay / 2 and the local clock's resolution (no root delay, no dispersion) */
    DMOD_TEST_EXPECT_TRUE(near(s.distance_ns, 20 * MS + DMNTP_CLOCK_RESOLUTION_NS));
}

DMOD_TEST_STEP(a_reply_within_the_same_millisecond_is_taken)
{
    /* A server next door: 30 us in it, the local (ms) clock shows nothing elapsed */
    int64_t offset = UNIX_2026 * NS;
    dmntp_packet_t p = reply_at(7 * NS + offset, 7 * NS + 30000 + offset);
    dmntp_sample_t s;

    DMOD_TEST_EXPECT_EQ(dmntp_sample(&p, 7 * NS, 7 * NS, &s), 0);
    DMOD_TEST_EXPECT_EQ(s.delay_ns, 0);
    DMOD_TEST_EXPECT_TRUE(s.offset_ns - offset < MS && offset - s.offset_ns < MS);
}

DMOD_TEST_STEP(replies_whose_times_do_not_add_up_are_refused)
{
    int64_t offset = UNIX_2026 * NS;
    dmntp_sample_t s;

    /* Sent before it was received */
    dmntp_packet_t back = reply_at(offset + 2 * NS, offset + NS);
    DMOD_TEST_EXPECT_EQ(dmntp_sample(&back, 0, 10 * MS, &s), -EBADMSG);

    /* Longer in the server than the whole exchange took */
    dmntp_packet_t slow = reply_at(offset, offset + 50 * MS);
    DMOD_TEST_EXPECT_EQ(dmntp_sample(&slow, 0, 10 * MS, &s), -EBADMSG);

    /* Too far from a reference: root dispersion of 2 s (16.16) */
    dmntp_packet_t lost = reply_at(offset, offset);
    lost.root_dispersion = 2u << 16;
    DMOD_TEST_EXPECT_EQ(dmntp_sample(&lost, 0, 10 * MS, &s), -EBADMSG);
}

/* ===========================================================================
 *                                Selection
 * ========================================================================== */

DMOD_TEST_STEP(one_server_is_its_own_interval)
{
    dmntp_sample_t s[] = { { 1000 * MS, 4 * MS, 3 * MS } };
    int64_t offset;
    uint64_t error;
    uint32_t agreeing;

    DMOD_TEST_EXPECT_EQ(dmntp_select(s, 1, &offset, &error, &agreeing), 0);
    DMOD_TEST_EXPECT_EQ(offset, 1000 * MS);
    DMOD_TEST_EXPECT_EQ(error, (uint64_t)(3 * MS));
    DMOD_TEST_EXPECT_EQ(agreeing, 1u);
}

DMOD_TEST_STEP(servers_that_agree_narrow_the_interval)
{
    /* [995, 1005] and [1000, 1020] ms: [1000, 1005] */
    dmntp_sample_t s[] = { { 1000 * MS, 0, 5 * MS }, { 1010 * MS, 0, 10 * MS } };
    int64_t offset;
    uint64_t error;
    uint32_t agreeing;

    DMOD_TEST_EXPECT_EQ(dmntp_select(s, 2, &offset, &error, &agreeing), 0);
    DMOD_TEST_EXPECT_EQ(offset, 1002 * MS + 500000);
    DMOD_TEST_EXPECT_EQ(error, (uint64_t)(2 * MS + 500000));
    DMOD_TEST_EXPECT_EQ(agreeing, 2u);
}

DMOD_TEST_STEP(a_server_out_of_line_is_outvoted)
{
    dmntp_sample_t s[] = { { 1000 * MS, 0, 5 * MS }, { 9000 * MS, 0, 5 * MS }, { 1002 * MS, 0, 5 * MS } };
    int64_t offset;
    uint64_t error;
    uint32_t agreeing;

    DMOD_TEST_EXPECT_EQ(dmntp_select(s, 3, &offset, &error, &agreeing), 0);
    DMOD_TEST_EXPECT_EQ(offset, 1001 * MS);
    DMOD_TEST_EXPECT_EQ(agreeing, 2u);
}

DMOD_TEST_STEP(servers_that_disagree_give_no_time)
{
    dmntp_sample_t s[] = { { 1000 * MS, 0, 5 * MS }, { 9000 * MS, 0, 5 * MS } };
    int64_t offset;
    uint64_t error;
    uint32_t agreeing;

    DMOD_TEST_EXPECT_EQ(dmntp_select(s, 2, &offset, &error, &agreeing), -EAGAIN);
    DMOD_TEST_EXPECT_EQ(dmntp_select(s, 0, &offset, &error, &agreeing), -EINVAL);
}

/* ===========================================================================
 *                              Lists of servers
 * ========================================================================== */

DMOD_TEST_STEP(a_list_keeps_each_server_once)
{
    dmntp_t ntp = dmntp_create();
    DMOD_TEST_EXPECT_TRUE(dmntp_is_valid(ntp));

    DMOD_TEST_EXPECT_EQ(dmntp_add_server(ntp, "192.0.2.1"), 0);
    DMOD_TEST_EXPECT_EQ(dmntp_add_server(ntp, "192.0.2.1"), 0);
    DMOD_TEST_EXPECT_EQ(dmntp_add_server(ntp, "time.example"), 0);
    DMOD_TEST_EXPECT_EQ(dmntp_add_server(ntp, ""), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmntp_remove_server(ntp, "192.0.2.1"), 0);
    DMOD_TEST_EXPECT_EQ(dmntp_remove_server(ntp, "192.0.2.1"), -ENOENT);
    DMOD_TEST_EXPECT_EQ(dmntp_remove_server(ntp, "time.example"), 0);

    /* No servers: no time */
    dmntp_time_t time;
    DMOD_TEST_EXPECT_EQ(dmntp_sync_with(ntp, 100, &time), -ENODATA);
    DMOD_TEST_EXPECT_EQ(dmntp_sync_with(ntp, DMNTP_MAX_TIMEOUT_MS + 1, &time), -EINVAL);

    dmntp_destroy(ntp);
    DMOD_TEST_EXPECT_FALSE(dmntp_is_valid(NULL));
}
