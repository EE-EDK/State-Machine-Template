/**
 * @file test_hal.c
 * @brief Unity tests for the platform abstraction layer (HAL)
 *
 * Covers:
 *   1.  SM_Platform_GetTimeMs returns 0 after test_sim_time_reset()
 *   2.  SM_Platform_SimTick increments time by 1 each call
 *   3.  Multiple SimTick calls produce correct accumulated time
 *   4.  SM_Platform_IsTimeout returns false before timeout
 *   5.  SM_Platform_IsTimeout returns true at exactly timeout_ms
 *   6.  SM_Platform_IsTimeout handles 32-bit wraparound correctly
 *   7.  SM_Platform_EnterCritical increments nesting depth
 *   8.  SM_Platform_ExitCritical decrements nesting depth
 *   9.  Nested critical sections -- enter 3, exit 3, depth returns to 0
 *  10.  SM_Platform_ExitCritical does not underflow below 0
 *  11.  SM_Platform_GetCriticalNesting returns 0 when not in critical section
 *  12.  SM_Platform_HasCapability(SM_CAP_OUTPUT) returns true
 *  13.  SM_Platform_HasCapability(SM_CAP_WATCHDOG) returns false (simulation)
 *  14.  SM_Platform_HasCapability(SM_CAP_NVS) returns false (simulation)
 *  15.  SM_Platform_GetResetReason returns SM_RESET_POR (simulation default)
 *  16.  SM_Platform_OutputInit returns true
 *  17.  SM_Platform_NVS_Write returns false (not available in simulation)
 *  18.  SM_Platform_NVS_Read returns false (not available in simulation)
 *
 * Compile defs (from tests/CMakeLists.txt):
 *   SM_STATE_COUNT=4, SM_EVENT_COUNT=8
 */

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

/* =============================================================================
 * UNITY SETUP / TEARDOWN
 * ===========================================================================*/

void setUp(void)
{
    /* Reset simulation time to 0 */
    test_sim_time_reset();

    /* Drain any leftover critical section nesting from previous tests */
    while (SM_Platform_GetCriticalNesting() > 0U) {
        SM_Platform_ExitCritical();
    }

    /* Clear assert-capture state */
    test_assert_clear();
}

void tearDown(void)
{
    /* nothing to clean up */
}

/* =============================================================================
 * TEST 1 -- SM_Platform_GetTimeMs returns 0 after reset
 * ===========================================================================*/

void test_get_time_returns_zero_after_reset(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetTimeMs());
}

/* =============================================================================
 * TEST 2 -- SM_Platform_SimTick increments time by 1
 * ===========================================================================*/

void test_sim_tick_increments_by_one(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetTimeMs());
    SM_Platform_SimTick();
    TEST_ASSERT_EQUAL_UINT32(1U, SM_Platform_GetTimeMs());
}

/* =============================================================================
 * TEST 3 -- Multiple SimTick calls produce correct accumulated time
 * ===========================================================================*/

void test_multiple_sim_ticks_accumulate(void)
{
    const uint32_t n = 100U;
    for (uint32_t i = 0U; i < n; i++) {
        SM_Platform_SimTick();
    }
    TEST_ASSERT_EQUAL_UINT32(n, SM_Platform_GetTimeMs());
}

/* =============================================================================
 * TEST 4 -- SM_Platform_IsTimeout returns false before timeout
 * ===========================================================================*/

void test_is_timeout_false_before_expiry(void)
{
    uint32_t start = SM_Platform_GetTimeMs();  /* 0 */

    /* Advance 9 ms, timeout is 10 ms -- should not be timed out */
    for (uint32_t i = 0U; i < 9U; i++) {
        SM_Platform_SimTick();
    }

    TEST_ASSERT_FALSE(SM_Platform_IsTimeout(start, 10U));
}

/* =============================================================================
 * TEST 5 -- SM_Platform_IsTimeout returns true at exactly timeout_ms
 * ===========================================================================*/

void test_is_timeout_true_at_exact_expiry(void)
{
    uint32_t start = SM_Platform_GetTimeMs();  /* 0 */

    /* Advance exactly 10 ms, timeout is 10 ms -- (now - start) >= timeout */
    for (uint32_t i = 0U; i < 10U; i++) {
        SM_Platform_SimTick();
    }

    TEST_ASSERT_TRUE(SM_Platform_IsTimeout(start, 10U));
}

/* =============================================================================
 * TEST 6 -- SM_Platform_IsTimeout handles 32-bit wraparound
 *
 * Strategy:
 *   After test_sim_time_reset(), sim_time_ms = 0.  Advance by 5 ticks so
 *   current time = 5.  Then call IsTimeout with start = 0xFFFFFFF0 and
 *   timeout_ms = 20.
 *
 *   Unsigned arithmetic: (now - start) = (5 - 0xFFFFFFF0) = 0x15 = 21.
 *   21 >= 20 is true, so the timeout is correctly detected despite the
 *   32-bit counter having wrapped around.
 *
 *   Also verify the inverse: with timeout_ms = 22, the same scenario
 *   returns false (21 < 22), proving the arithmetic is correct in both
 *   directions.
 * ===========================================================================*/

void test_is_timeout_handles_uint32_wraparound(void)
{
    /* Advance sim time to 5 */
    for (uint32_t i = 0U; i < 5U; i++) {
        SM_Platform_SimTick();
    }
    TEST_ASSERT_EQUAL_UINT32(5U, SM_Platform_GetTimeMs());

    /* start near UINT32_MAX: elapsed = (5 - 0xFFFFFFF0) = 0x15 = 21 */
    const uint32_t start_near_max = 0xFFFFFFF0U;

    /* 21 >= 20 -> true: timeout detected across wrap */
    TEST_ASSERT_TRUE(SM_Platform_IsTimeout(start_near_max, 20U));

    /* 21 >= 22 -> false: not yet timed out */
    TEST_ASSERT_FALSE(SM_Platform_IsTimeout(start_near_max, 22U));
}

/* =============================================================================
 * TEST 7 -- SM_Platform_EnterCritical increments nesting depth
 * ===========================================================================*/

void test_enter_critical_increments_nesting(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());

    SM_Platform_EnterCritical();
    TEST_ASSERT_EQUAL_UINT32(1U, SM_Platform_GetCriticalNesting());

    SM_Platform_EnterCritical();
    TEST_ASSERT_EQUAL_UINT32(2U, SM_Platform_GetCriticalNesting());

    /* Clean up */
    SM_Platform_ExitCritical();
    SM_Platform_ExitCritical();
}

/* =============================================================================
 * TEST 8 -- SM_Platform_ExitCritical decrements nesting depth
 * ===========================================================================*/

void test_exit_critical_decrements_nesting(void)
{
    SM_Platform_EnterCritical();
    SM_Platform_EnterCritical();
    TEST_ASSERT_EQUAL_UINT32(2U, SM_Platform_GetCriticalNesting());

    SM_Platform_ExitCritical();
    TEST_ASSERT_EQUAL_UINT32(1U, SM_Platform_GetCriticalNesting());

    SM_Platform_ExitCritical();
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());
}

/* =============================================================================
 * TEST 9 -- Nested critical sections: enter 3, exit 3, depth returns to 0
 * ===========================================================================*/

void test_nested_critical_sections_balanced(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());

    SM_Platform_EnterCritical();
    TEST_ASSERT_EQUAL_UINT32(1U, SM_Platform_GetCriticalNesting());

    SM_Platform_EnterCritical();
    TEST_ASSERT_EQUAL_UINT32(2U, SM_Platform_GetCriticalNesting());

    SM_Platform_EnterCritical();
    TEST_ASSERT_EQUAL_UINT32(3U, SM_Platform_GetCriticalNesting());

    SM_Platform_ExitCritical();
    TEST_ASSERT_EQUAL_UINT32(2U, SM_Platform_GetCriticalNesting());

    SM_Platform_ExitCritical();
    TEST_ASSERT_EQUAL_UINT32(1U, SM_Platform_GetCriticalNesting());

    SM_Platform_ExitCritical();
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());
}

/* =============================================================================
 * TEST 10 -- SM_Platform_ExitCritical does not underflow below 0
 * ===========================================================================*/

void test_exit_critical_no_underflow(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());

    /* ExitCritical when already at 0 -- must stay at 0, not wrap to UINT32_MAX */
    SM_Platform_ExitCritical();
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());

    /* Multiple calls at 0 should all remain at 0 */
    SM_Platform_ExitCritical();
    SM_Platform_ExitCritical();
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());
}

/* =============================================================================
 * TEST 11 -- SM_Platform_GetCriticalNesting returns 0 when not in critical
 * ===========================================================================*/

void test_get_critical_nesting_zero_outside_critical(void)
{
    /* setUp drains nesting to 0, so this should already be the case */
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());
}

/* =============================================================================
 * TEST 12 -- SM_Platform_HasCapability(SM_CAP_OUTPUT) returns true
 * ===========================================================================*/

void test_has_capability_output_true(void)
{
    TEST_ASSERT_TRUE(SM_Platform_HasCapability(SM_CAP_OUTPUT));
}

/* =============================================================================
 * TEST 13 -- SM_Platform_HasCapability(SM_CAP_WATCHDOG) returns false
 * ===========================================================================*/

void test_has_capability_watchdog_false(void)
{
    TEST_ASSERT_FALSE(SM_Platform_HasCapability(SM_CAP_WATCHDOG));
}

/* =============================================================================
 * TEST 14 -- SM_Platform_HasCapability(SM_CAP_NVS) returns false
 * ===========================================================================*/

void test_has_capability_nvs_false(void)
{
    TEST_ASSERT_FALSE(SM_Platform_HasCapability(SM_CAP_NVS));
}

/* =============================================================================
 * TEST 15 -- SM_Platform_GetResetReason returns SM_RESET_POR
 * ===========================================================================*/

void test_get_reset_reason_is_por(void)
{
    TEST_ASSERT_EQUAL_INT(SM_RESET_POR, SM_Platform_GetResetReason());
}

/* =============================================================================
 * TEST 16 -- SM_Platform_OutputInit returns true
 * ===========================================================================*/

void test_output_init_returns_true(void)
{
    TEST_ASSERT_TRUE(SM_Platform_OutputInit(0U));

    /* Verify with a different interface ID -- stub always returns true */
    TEST_ASSERT_TRUE(SM_Platform_OutputInit(1U));
    TEST_ASSERT_TRUE(SM_Platform_OutputInit(255U));
}

/* =============================================================================
 * TEST 17 -- SM_Platform_NVS_Write returns false (not available)
 * ===========================================================================*/

void test_nvs_write_returns_false(void)
{
    uint8_t data[] = { 0xAA, 0xBB, 0xCC };
    TEST_ASSERT_FALSE(SM_Platform_NVS_Write(0U, data, sizeof(data)));
    TEST_ASSERT_FALSE(SM_Platform_NVS_Write(0xFFFFU, data, 1U));
}

/* =============================================================================
 * TEST 18 -- SM_Platform_NVS_Read returns false (not available)
 * ===========================================================================*/

void test_nvs_read_returns_false(void)
{
    uint8_t buf[16];
    TEST_ASSERT_FALSE(SM_Platform_NVS_Read(0U, buf, sizeof(buf)));
    TEST_ASSERT_FALSE(SM_Platform_NVS_Read(0xFFFFU, buf, 1U));
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    UNITY_BEGIN();

    /* Timing */
    RUN_TEST(test_get_time_returns_zero_after_reset);
    RUN_TEST(test_sim_tick_increments_by_one);
    RUN_TEST(test_multiple_sim_ticks_accumulate);

    /* Timeout */
    RUN_TEST(test_is_timeout_false_before_expiry);
    RUN_TEST(test_is_timeout_true_at_exact_expiry);
    RUN_TEST(test_is_timeout_handles_uint32_wraparound);

    /* Critical sections */
    RUN_TEST(test_enter_critical_increments_nesting);
    RUN_TEST(test_exit_critical_decrements_nesting);
    RUN_TEST(test_nested_critical_sections_balanced);
    RUN_TEST(test_exit_critical_no_underflow);
    RUN_TEST(test_get_critical_nesting_zero_outside_critical);

    /* Capabilities */
    RUN_TEST(test_has_capability_output_true);
    RUN_TEST(test_has_capability_watchdog_false);
    RUN_TEST(test_has_capability_nvs_false);

    /* Reset reason */
    RUN_TEST(test_get_reset_reason_is_por);

    /* Output */
    RUN_TEST(test_output_init_returns_true);

    /* NVS */
    RUN_TEST(test_nvs_write_returns_false);
    RUN_TEST(test_nvs_read_returns_false);

    return UNITY_END();
}
