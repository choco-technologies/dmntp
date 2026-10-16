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
