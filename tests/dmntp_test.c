/**
 * @file dmntp_test.c
 * @brief Test steps for dmntp module.
 *
 * These tests verify the basic create/destroy lifecycle of the dmntp module.
 */
#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmntp.h"

void dmod_test_setup(void)
{
    /* Setup code if needed */
}

void dmod_test_teardown(void)
{
    /* Teardown code if needed */
}

/*
 * Test: Create and destroy handle.
 */

DMOD_TEST_STEP(dmntp_create)
{
    dmntp_t handle = dmntp_create();
    DMOD_TEST_EXPECT_NOT_NULL(handle);
    dmntp_destroy(handle);
}

DMOD_TEST_STEP(dmntp_is_valid)
{
    dmntp_t handle = dmntp_create();
    DMOD_TEST_EXPECT_TRUE(dmntp_is_valid(handle));
    dmntp_destroy(handle);
}

DMOD_TEST_STEP(dmntp_destroy_null)
{
    /* Destroying NULL must not crash. */
    dmntp_destroy(NULL);
}

DMOD_TEST_STEP(dmntp_add_server)
{
    dmntp_t handle = dmntp_create();
    int result = dmntp_add_server(handle, "pool.ntp.org");
    DMOD_TEST_EXPECT_EQ(result, 0);
    dmntp_destroy(handle);
}

DMOD_TEST_STEP(dmntp_remove_server)
{
    dmntp_t handle = dmntp_create();
    dmntp_add_server(handle, "pool.ntp.org");
    int result = dmntp_remove_server(handle, "pool.ntp.org");
    DMOD_TEST_EXPECT_EQ(result, 0);
    dmntp_destroy(handle);
}

#include "dmntp_internal.h"
#include <string.h>
DMOD_TEST_STEP(invalid_and_duplicate_servers)
{
    dmntp_t h = dmntp_create();
    DMOD_TEST_EXPECT_NOT_NULL(h);
    DMOD_TEST_EXPECT_EQ(dmntp_add_server(h, NULL), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmntp_add_server(h, ""), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmntp_add_server(NULL, "a"), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmntp_add_server(h, "a"), 0);
    DMOD_TEST_EXPECT_EQ(dmntp_add_server(h, "a"), 0);
    DMOD_TEST_EXPECT_EQ(dmntp_remove_server(h, "a"), 0);
    DMOD_TEST_EXPECT_EQ(dmntp_remove_server(h, "a"), -ENOENT);
    DMOD_TEST_EXPECT_EQ(dmntp_add_server(h, "b"), 0);
    dmntp_destroy(h);
}
DMOD_TEST_STEP(empty_sync_and_invalid_outputs)
{
    dmntp_t h = dmntp_create(); uint64_t sec = 123; uint32_t ns = 5, rtt = 5, jitter = 5;
    DMOD_TEST_EXPECT_EQ(dmntp_sync_with(h, 10, &sec, &ns, &rtt, &jitter), -ENODATA);
    DMOD_TEST_EXPECT_EQ(sec, 0);
    DMOD_TEST_EXPECT_EQ(ns, 0);
    DMOD_TEST_EXPECT_EQ(rtt, 0);
    DMOD_TEST_EXPECT_EQ(jitter, 0);
    DMOD_TEST_EXPECT_EQ(dmntp_sync_with(h, 10, NULL, &ns, NULL, NULL), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmntp_sync_with(h, UINT32_MAX, &sec, &ns, NULL, NULL), -EINVAL);
    dmntp_destroy(h);
}
DMOD_TEST_STEP(timestamp_conversions_and_carry)
{
    DMOD_TEST_EXPECT_EQ(dmntp_ntp_to_unix(3908988800u), 1700000000ULL);
    DMOD_TEST_EXPECT_EQ(dmntp_unix_to_ntp(1700000000ULL), 3908988800u);
    DMOD_TEST_EXPECT_EQ(dmntp_unix_to_ntp(UINT64_MAX), 0);
    DMOD_TEST_EXPECT_EQ(dmntp_frac_to_ns(0x80000000u), 500000000u);
    DMOD_TEST_EXPECT_EQ(dmntp_ns_to_frac(500000000u), 0x80000000u);
    dmntp_timestamp_t t = {10, 0xc0000000u};
    t = dmntp_timestamp_add_ns(t, 500000000);
    DMOD_TEST_EXPECT_EQ(t.seconds, 11);
    DMOD_TEST_EXPECT_EQ(t.fraction, 0x40000000u);
}
DMOD_TEST_STEP(wire_format_and_validation)
{
    dmntp_packet_t p, decoded; uint8_t bytes[48];
    DMOD_TEST_EXPECT_TRUE(Dmod_ApiSignature_IsValid(dmod_dmntp_provide_servers_sig));
    DMOD_TEST_EXPECT_EQ(sizeof(p), 48);
    dmntp_packet_init_client(&p); p.trans.seconds = 0x12345678; p.trans.fraction = 0x90abcdef;
    dmntp_packet_encode(&p, bytes);
    DMOD_TEST_EXPECT_EQ(bytes[0], 0x23);
    DMOD_TEST_EXPECT_EQ(bytes[40], 0x12);
    DMOD_TEST_EXPECT_EQ(bytes[43], 0x78);
    DMOD_TEST_EXPECT_EQ(bytes[44], 0x90);
    dmntp_packet_decode(bytes, &decoded);
    DMOD_TEST_EXPECT_EQ(memcmp(&p, &decoded, sizeof(p)), 0);
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p, 48), -EBADMSG);
    p.li_vn_mode = 0x24; p.stratum = 2;
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p, 48), 0);
    p.stratum = 17;
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p, 48), -EBADMSG);
    p.stratum = 0;
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p, 48), -EBADMSG);
    p.stratum = 2; p.li_vn_mode = 0xe4;
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p, 48), -EBADMSG);
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(NULL, 48), -EBADMSG);
    DMOD_TEST_EXPECT_EQ(dmntp_packet_validate(&p, 47), -EBADMSG);
}
DMOD_TEST_STEP(signed_offsets)
{
    dmntp_timestamp_t a = {100, 0}, b = {99, 0x40000000}, c = {99, 0x80000000}, d = {101, 0};
    double offset = 0, delay = 0;
    DMOD_TEST_EXPECT_EQ(dmntp_calculate_offset(&a, &b, &c, &d, &offset, &delay), 0);
    DMOD_TEST_EXPECT_TRUE(offset < -1.124 && offset > -1.126);
    DMOD_TEST_EXPECT_TRUE(delay > .749 && delay < .751);
    DMOD_TEST_EXPECT_EQ(dmntp_calculate_offset(&a, &c, &b, &d, &offset, &delay), -EBADMSG);
}
DMOD_TEST_STEP(majority_selection_and_outliers)
{
    dmntp_result_t samples[3] = {{10, .2, .2}, {10.1, .2, .2}, {100, .2, .2}};
    double offset, jitter;
    DMOD_TEST_EXPECT_EQ(dmntp_select(samples, 3, &offset, &jitter), 0);
    DMOD_TEST_EXPECT_TRUE(offset > 10.04 && offset < 10.06);
    DMOD_TEST_EXPECT_TRUE(jitter > .07 && jitter < .08);
    DMOD_TEST_EXPECT_EQ(dmntp_select(samples + 1, 2, &offset, &jitter), -EAGAIN);
    DMOD_TEST_EXPECT_EQ(dmntp_select(samples, 1, &offset, &jitter), 0);
    DMOD_TEST_EXPECT_TRUE(offset > 9.99 && offset < 10.01);
}
