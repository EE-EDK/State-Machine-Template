/**
 * @file test_debug_null_safe.c
 * @brief Debug API null / empty format handling (no crash)
 */
#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
    SM_Debug_Init(0);
}

void tearDown(void)
{
}

void test_dbg_contract_print_null_fmt_safe(void)
{
    SM_Debug_Print(1U, NULL);
    TEST_PASS();
}

void test_dbg_print_tagged_null_fmt_safe(void)
{
    SM_Debug_PrintTagged(0, 1U, NULL);
    TEST_PASS();
}

void test_dbg_print_raw_null_safe(void)
{
    SM_Debug_PrintRaw(NULL, 10U);
    TEST_PASS();
}

void test_dbg_print_raw_zero_len_safe(void)
{
    SM_Debug_PrintRaw("hi", 0U);
    TEST_PASS();
}

void test_dbg_hexdump_null_safe(void)
{
    SM_Debug_HexDump(NULL, 10U);
    TEST_PASS();
}

void test_dbg_hexdump_zero_len_safe(void)
{
    uint8_t b = 0xAB;
    SM_Debug_HexDump(&b, 0U);
    TEST_PASS();
}

void test_dbg_print_literal_ok(void)
{
    SM_Debug_Print(1U, "ok");
    TEST_PASS();
}

void test_dbg_hexdump_small_buffer_ok(void)
{
    uint8_t buf[4] = { 1, 2, 3, 4 };
    SM_Debug_HexDump(buf, 4U);
    TEST_PASS();
}

void test_dbg_enable_level_toggle(void)
{
    SM_Debug_EnableLevel(3U, false);
    SM_Debug_EnableLevel(3U, true);
    TEST_ASSERT_TRUE(SM_Debug_IsLevelEnabled(3U));
}

void test_dbg_invalid_tag_id_fail_open(void)
{
    TEST_ASSERT_TRUE(SM_Debug_IsTagEnabled(-1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dbg_contract_print_null_fmt_safe);
    RUN_TEST(test_dbg_print_tagged_null_fmt_safe);
    RUN_TEST(test_dbg_print_raw_null_safe);
    RUN_TEST(test_dbg_print_raw_zero_len_safe);
    RUN_TEST(test_dbg_hexdump_null_safe);
    RUN_TEST(test_dbg_hexdump_zero_len_safe);
    RUN_TEST(test_dbg_print_literal_ok);
    RUN_TEST(test_dbg_hexdump_small_buffer_ok);
    RUN_TEST(test_dbg_enable_level_toggle);
    RUN_TEST(test_dbg_invalid_tag_id_fail_open);
    return UNITY_END();
}
