/**
 * @file test_debug.c
 * @brief Unity tests for the debug messaging system (sm_debug.c)
 *
 * Covers:
 *   1.  SM_Debug_Init returns true
 *   2.  SM_Debug_EnableLevel disables info level, verified via IsLevelEnabled
 *   3.  SM_Debug_EnableLevel re-enables info level
 *   4.  All 4 levels enabled by default after Init
 *   5.  SM_Debug_RegisterTag returns sequential IDs (0, 1, 2, ...)
 *   6.  SM_Debug_RegisterTag returns -1 when tag table is full (17th tag)
 *   7.  SM_Debug_EnableTag(id, false) disables tag, IsTagEnabled returns false
 *   8.  SM_Debug_IsTagEnabled with invalid tag_id returns true (fail-open)
 *   9.  SM_Debug_SetPeriodicInterval + CheckPeriodic fires when interval elapses
 *  10.  SM_Debug_CheckPeriodic with interval=0 always returns true
 *  11.  SM_Debug_HexDump with NULL data does not crash
 *  12.  SM_Debug_HexDump with len=0 does not crash
 *  13.  SM_Debug_Print does not crash with various format strings
 *  14.  SM_Debug_PrintRaw does not crash
 *
 * Compile defs (from tests/CMakeLists.txt):
 *   SM_FEATURE_DEBUG=1, SM_DEBUG_LEVEL=4 (all levels compiled in)
 *
 * Debug output is suppressed by test_platform.c (SM_Platform_OutputSend
 * returns len without printing), so these tests verify state correctness
 * and absence of crashes rather than output content.
 */

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

/* =============================================================================
 * UNITY SETUP / TEARDOWN
 * ===========================================================================*/

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();

    /* Re-initialize debug system for each test (resets level mask, tag table,
     * periodic interval). Interface 0 is the simulation/test stub. */
    SM_Debug_Init(0);
}

void tearDown(void)
{
    /* nothing to clean up -- static allocation */
}

/* =============================================================================
 * TEST 1 -- SM_Debug_Init returns true
 * ===========================================================================*/

void test_debug_init_returns_true(void)
{
    bool ok = SM_Debug_Init(0);
    TEST_ASSERT_TRUE(ok);
}

/* =============================================================================
 * TEST 2 -- Disable info level, verify IsLevelEnabled returns false
 * ===========================================================================*/

void test_disable_info_level(void)
{
    /* Level 3 = info, should be enabled after Init */
    TEST_ASSERT_TRUE(SM_Debug_IsLevelEnabled(3));

    /* Disable it */
    SM_Debug_EnableLevel(3, false);
    TEST_ASSERT_FALSE(SM_Debug_IsLevelEnabled(3));
}

/* =============================================================================
 * TEST 3 -- Re-enable info level, verify returns true
 * ===========================================================================*/

void test_reenable_info_level(void)
{
    SM_Debug_EnableLevel(3, false);
    TEST_ASSERT_FALSE(SM_Debug_IsLevelEnabled(3));

    SM_Debug_EnableLevel(3, true);
    TEST_ASSERT_TRUE(SM_Debug_IsLevelEnabled(3));
}

/* =============================================================================
 * TEST 4 -- All 4 levels enabled by default after Init
 * ===========================================================================*/

void test_all_levels_enabled_after_init(void)
{
    TEST_ASSERT_TRUE_MESSAGE(SM_Debug_IsLevelEnabled(1), "ERROR level (1) should be enabled");
    TEST_ASSERT_TRUE_MESSAGE(SM_Debug_IsLevelEnabled(2), "WARN level (2) should be enabled");
    TEST_ASSERT_TRUE_MESSAGE(SM_Debug_IsLevelEnabled(3), "INFO level (3) should be enabled");
    TEST_ASSERT_TRUE_MESSAGE(SM_Debug_IsLevelEnabled(4), "VERBOSE level (4) should be enabled");
}

/* =============================================================================
 * TEST 5 -- RegisterTag returns sequential IDs (0, 1, 2, ...)
 * ===========================================================================*/

void test_register_tag_sequential_ids(void)
{
    int8_t id0 = SM_Debug_RegisterTag("module_a");
    int8_t id1 = SM_Debug_RegisterTag("module_b");
    int8_t id2 = SM_Debug_RegisterTag("module_c");

    TEST_ASSERT_EQUAL_INT8(0, id0);
    TEST_ASSERT_EQUAL_INT8(1, id1);
    TEST_ASSERT_EQUAL_INT8(2, id2);
}

/* =============================================================================
 * TEST 6 -- RegisterTag returns -1 when tag table is full (17th tag)
 * ===========================================================================*/

void test_register_tag_overflow_returns_negative_one(void)
{
    char tag_name[32];

    /* Fill all 16 slots */
    for (int i = 0; i < SM_DEBUG_MAX_TAGS; i++) {
        snprintf(tag_name, sizeof(tag_name), "tag_%02d", i);
        int8_t id = SM_Debug_RegisterTag(tag_name);
        TEST_ASSERT_EQUAL_INT8((int8_t)i, id);
    }

    /* 17th registration must fail */
    int8_t overflow_id = SM_Debug_RegisterTag("tag_overflow");
    TEST_ASSERT_EQUAL_INT8(-1, overflow_id);
}

/* =============================================================================
 * TEST 7 -- EnableTag(id, false) disables tag, IsTagEnabled returns false
 * ===========================================================================*/

void test_enable_tag_disables_and_queries(void)
{
    int8_t id = SM_Debug_RegisterTag("sensor");
    TEST_ASSERT_TRUE(SM_Debug_IsTagEnabled(id));

    SM_Debug_EnableTag(id, false);
    TEST_ASSERT_FALSE(SM_Debug_IsTagEnabled(id));

    /* Re-enable and verify */
    SM_Debug_EnableTag(id, true);
    TEST_ASSERT_TRUE(SM_Debug_IsTagEnabled(id));
}

/* =============================================================================
 * TEST 8 -- IsTagEnabled with invalid tag_id returns true (fail-open)
 * ===========================================================================*/

void test_is_tag_enabled_invalid_id_returns_true(void)
{
    /* Negative ID */
    TEST_ASSERT_TRUE_MESSAGE(SM_Debug_IsTagEnabled(-1),
                             "Invalid tag_id (-1) should fail-open to true");

    /* Out of range (beyond SM_DEBUG_MAX_TAGS) */
    TEST_ASSERT_TRUE_MESSAGE(SM_Debug_IsTagEnabled((int8_t)SM_DEBUG_MAX_TAGS),
                             "Invalid tag_id (16) should fail-open to true");

    /* Large positive value */
    TEST_ASSERT_TRUE_MESSAGE(SM_Debug_IsTagEnabled(127),
                             "Invalid tag_id (127) should fail-open to true");
}

/* =============================================================================
 * TEST 9 -- SetPeriodicInterval + CheckPeriodic fires when interval elapses
 *
 * Strategy:
 *   1. Set interval to 100 ms
 *   2. Immediately after setting, CheckPeriodic should return false
 *   3. Advance sim time by 99 ms -- still false
 *   4. Advance 1 more ms (total 100) -- should return true
 *   5. After returning true, next immediate check should return false
 *      (last_check was updated)
 * ===========================================================================*/

void test_periodic_interval_fires_on_elapsed(void)
{
    SM_Debug_SetPeriodicInterval(100U);

    /* Immediately after setting, not enough time has passed */
    TEST_ASSERT_FALSE(SM_Debug_CheckPeriodic());

    /* Advance 99 ms -- still not elapsed */
    for (uint32_t i = 0U; i < 99U; i++) {
        SM_Platform_SimTick();
    }
    TEST_ASSERT_FALSE(SM_Debug_CheckPeriodic());

    /* Advance 1 more ms (total = 100) -- now elapsed */
    SM_Platform_SimTick();
    TEST_ASSERT_TRUE(SM_Debug_CheckPeriodic());

    /* Immediately after a true return, interval resets -- should be false */
    TEST_ASSERT_FALSE(SM_Debug_CheckPeriodic());
}

/* =============================================================================
 * TEST 10 -- CheckPeriodic with interval=0 always returns true
 * ===========================================================================*/

void test_periodic_interval_zero_always_true(void)
{
    SM_Debug_SetPeriodicInterval(0U);

    TEST_ASSERT_TRUE(SM_Debug_CheckPeriodic());
    TEST_ASSERT_TRUE(SM_Debug_CheckPeriodic());
    TEST_ASSERT_TRUE(SM_Debug_CheckPeriodic());
}

/* =============================================================================
 * TEST 11 -- HexDump with NULL data does not crash
 * ===========================================================================*/

void test_hexdump_null_data_no_crash(void)
{
    /* Should return early without crashing */
    SM_Debug_HexDump(NULL, 16U);

    /* If we reach this assertion, no crash occurred */
    TEST_PASS();
}

/* =============================================================================
 * TEST 12 -- HexDump with len=0 does not crash
 * ===========================================================================*/

void test_hexdump_zero_len_no_crash(void)
{
    const uint8_t dummy[] = { 0x48, 0x65, 0x6C, 0x6C, 0x6F };

    /* Should return early without crashing */
    SM_Debug_HexDump(dummy, 0U);

    /* If we reach this assertion, no crash occurred */
    TEST_PASS();
}

/* =============================================================================
 * TEST 13 -- SM_Debug_Print does not crash with various format strings
 * ===========================================================================*/

void test_print_various_formats_no_crash(void)
{
    /* Simple string */
    SM_Debug_Print(1, "error message");

    /* Integer format */
    SM_Debug_Print(2, "warning: code=%d", 42);

    /* Unsigned hex format */
    SM_Debug_Print(3, "info: addr=0x%08X", 0xDEADBEEFU);

    /* String format */
    SM_Debug_Print(4, "verbose: name=%s val=%u", "sensor", 123U);

    /* Empty format string */
    SM_Debug_Print(1, "");

    /* Long format string (tests buffer truncation, not overflow) */
    SM_Debug_Print(3, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                      "ccccccccccccccccccccccccccccccccccccccccccccccccccc");

    /* If we reach here, no crash occurred */
    TEST_PASS();
}

/* =============================================================================
 * TEST 14 -- SM_Debug_PrintRaw does not crash
 * ===========================================================================*/

void test_print_raw_no_crash(void)
{
    const char raw_msg[] = "raw debug output\r\n";

    SM_Debug_PrintRaw(raw_msg, (uint32_t)(sizeof(raw_msg) - 1U));

    /* NULL message should also not crash (early return) */
    SM_Debug_PrintRaw(NULL, 10U);

    /* Zero length should not crash */
    SM_Debug_PrintRaw(raw_msg, 0U);

    /* If we reach here, no crash occurred */
    TEST_PASS();
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_debug_init_returns_true);
    RUN_TEST(test_disable_info_level);
    RUN_TEST(test_reenable_info_level);
    RUN_TEST(test_all_levels_enabled_after_init);
    RUN_TEST(test_register_tag_sequential_ids);
    RUN_TEST(test_register_tag_overflow_returns_negative_one);
    RUN_TEST(test_enable_tag_disables_and_queries);
    RUN_TEST(test_is_tag_enabled_invalid_id_returns_true);
    RUN_TEST(test_periodic_interval_fires_on_elapsed);
    RUN_TEST(test_periodic_interval_zero_always_true);
    RUN_TEST(test_hexdump_null_data_no_crash);
    RUN_TEST(test_hexdump_zero_len_no_crash);
    RUN_TEST(test_print_various_formats_no_crash);
    RUN_TEST(test_print_raw_no_crash);

    return UNITY_END();
}
