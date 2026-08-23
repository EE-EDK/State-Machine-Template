/**
 * @file test_error.c
 * @brief Unity tests for the 3-tier error handler with DIS protection
 *
 * Covers:
 *   1.  SM_Error_Report MINOR sets minor_active, records in history
 *   2.  SM_Error_Report NORMAL records in history, current error set
 *   3.  SM_Error_Report CRITICAL sets critical_lock=true, updates DIS
 *   4.  SM_Error_Report with invalid level returns false
 *   5.  SM_Error_Report with NULL sm returns false
 *   6.  SM_Error_Clear resets current error but NOT critical_lock
 *   7.  SM_Error_IsCriticalLock returns false initially, true after CRITICAL
 *   8.  SM_Error_IsCriticalLock fires assertion 710 on corrupted DIS
 *   9.  SM_Error_GetHistory index 0 = most recent error
 *  10.  SM_Error_GetHistoryCount tracks actual count, not max
 *  11.  History wraps correctly (ring buffer overflow)
 *  12.  SM_Error_AttemptRecovery calls registered callback
 *  13.  SM_Error_AttemptRecovery with no callback returns false
 *  14.  SM_Error_AttemptRecovery max retries exceeded returns false
 *  15.  Recovery callback returning true marks error recovered
 *  16.  SM_Error_RegisterNotifyCallback invoked on every Report
 *  17.  SM_Error_GetStats returns per-level counts, recovery metrics
 *  18.  Stats accumulate across multiple Report/Recovery calls
 *
 * Compile defs (from tests/CMakeLists.txt):
 *   SM_ERROR_HISTORY_SIZE=8, SM_ERROR_MAX_RECOVERY=3,
 *   SM_STATE_COUNT=4, SM_EVENT_COUNT=8, SM_FEATURE_ASSERT=1
 */

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

/* =============================================================================
 * FILE-SCOPE CALLBACK COUNTERS
 * ===========================================================================*/

static uint32_t s_recovery_call_count   = 0U;
static bool     s_recovery_return_value = false;
static uint16_t s_recovery_last_code    = 0U;

static uint32_t       s_notify_call_count = 0U;
static SM_ErrorLevel_t s_notify_last_level = SM_ERROR_NONE;
static uint16_t       s_notify_last_code  = 0U;

/**
 * @brief Recovery callback that tracks invocations and returns a
 *        configurable success/fail value.
 */
static bool test_recovery_cb(SM_Handle_t sm, uint16_t error_code)
{
    (void)sm;
    s_recovery_call_count++;
    s_recovery_last_code = error_code;
    return s_recovery_return_value;
}

/**
 * @brief Error notification callback that tracks every Report invocation.
 */
static void test_notify_cb(SM_Handle_t sm, SM_ErrorLevel_t level, uint16_t code)
{
    (void)sm;
    s_notify_call_count++;
    s_notify_last_level = level;
    s_notify_last_code  = code;
}

static void counters_clear(void)
{
    s_recovery_call_count   = 0U;
    s_recovery_return_value = false;
    s_recovery_last_code    = 0U;

    s_notify_call_count = 0U;
    s_notify_last_level = SM_ERROR_NONE;
    s_notify_last_code  = 0U;
}

/* =============================================================================
 * MINIMAL STATE MACHINE CONFIGURATION
 *
 * The error handler tests do not exercise transitions, but SM_Init requires
 * a valid config.  Provide a bare-minimum 4-state machine with no transitions.
 * ===========================================================================*/

static const SM_StateDesc_t s_states[SM_STATE_COUNT] = {
    /* [TEST_STATE_INIT]    */ { NULL, NULL, NULL, 0U, 0U },
    /* [TEST_STATE_RUNNING] */ { NULL, NULL, NULL, 0U, 0U },
    /* [TEST_STATE_STOPPED] */ { NULL, NULL, NULL, 0U, 0U },
    /* [TEST_STATE_ERROR]   */ { NULL, NULL, NULL, 0U, 0U },
};

static const SM_Transition_t s_transitions[] = {
    { TEST_STATE_INIT, TEST_EVT_START, TEST_STATE_RUNNING, 0U, NULL, NULL },
};

static const SM_Config_t s_config = {
    .states           = s_states,
    .transitions      = s_transitions,
    .transition_count = (uint16_t)(sizeof(s_transitions) / sizeof(s_transitions[0])),
    .initial_state    = TEST_STATE_INIT,
};

/* Instance storage -- reused across tests, re-initialized in setUp() */
static SM_Context_t s_ctx;
static SM_Handle_t  s_sm = &s_ctx;

/* =============================================================================
 * UNITY SETUP / TEARDOWN
 * ===========================================================================*/

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
    counters_clear();

    /* Fresh init for every test */
    bool ok = SM_Init(s_sm, &s_config);
    TEST_ASSERT_TRUE_MESSAGE(ok, "SM_Init must succeed in setUp");
}

void tearDown(void)
{
    /* nothing to clean up -- static allocation */
}

/* =============================================================================
 * TEST 1 -- SM_Error_Report MINOR sets minor_active, records in history
 * ===========================================================================*/

void test_report_minor_sets_minor_active_and_records_history(void)
{
    bool ok = SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x0001U);
    TEST_ASSERT_TRUE(ok);

    /* Public API since v4.2 (D18). One white-box read is kept below as a
     * cross-check that the accessor reports the field and not a constant. */
    TEST_ASSERT_TRUE(SM_Error_IsMinorActive(s_sm));
    TEST_ASSERT_EQUAL(s_ctx.error.minor_active, SM_Error_IsMinorActive(s_sm));

    /* History count should be 1 */
    TEST_ASSERT_EQUAL_UINT8(1U, SM_Error_GetHistoryCount(s_sm));

    /* Retrieve from history -- index 0 = most recent */
    SM_ErrorInfo_t info;
    ok = SM_Error_GetHistory(s_sm, 0U, &info);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_MINOR, info.level);
    TEST_ASSERT_EQUAL_UINT16(0x0001U, info.code);
    TEST_ASSERT_FALSE(info.recovered);
    TEST_ASSERT_EQUAL_UINT8(0U, info.retry_count);
}

/* =============================================================================
 * TEST 2 -- SM_Error_Report NORMAL records in history, current error set
 * ===========================================================================*/

void test_report_normal_records_history_and_sets_current(void)
{
    bool ok = SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0042U);
    TEST_ASSERT_TRUE(ok);

    /* Current error should be NORMAL with the user code */
    SM_ErrorInfo_t current;
    ok = SM_Error_GetCurrent(s_sm, &current);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_NORMAL, current.level);
    TEST_ASSERT_EQUAL_UINT16(0x0042U, current.code);

    /* History should have 1 entry */
    TEST_ASSERT_EQUAL_UINT8(1U, SM_Error_GetHistoryCount(s_sm));

    SM_ErrorInfo_t hist;
    ok = SM_Error_GetHistory(s_sm, 0U, &hist);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_NORMAL, hist.level);
    TEST_ASSERT_EQUAL_UINT16(0x0042U, hist.code);
}

/* =============================================================================
 * TEST 3 -- SM_Error_Report CRITICAL sets critical_lock=true, updates DIS
 * ===========================================================================*/

void test_report_critical_sets_lock_and_dis(void)
{
    /* Before: no critical lock */
    TEST_ASSERT_FALSE(SM_Error_IsCriticalLock(s_sm));

    bool ok = SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 0x00FFU);
    TEST_ASSERT_TRUE(ok);

    /* critical_lock should be true */
    TEST_ASSERT_TRUE(s_ctx.error.critical_lock);
    TEST_ASSERT_TRUE(SM_Error_IsCriticalLock(s_sm));

    /* DIS shadow should be valid (inverse of 1U) */
    uint8_t expected_dis = (uint8_t)(~(uint8_t)1U);
    TEST_ASSERT_EQUAL_UINT8(expected_dis, s_ctx.error.critical_lock_dis);

    /* Current error should be CRITICAL */
    SM_ErrorInfo_t current;
    ok = SM_Error_GetCurrent(s_sm, &current);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_CRITICAL, current.level);
    TEST_ASSERT_EQUAL_UINT16(0x00FFU, current.code);
}

/* =============================================================================
 * TEST 4 -- SM_Error_Report with invalid level returns false
 * ===========================================================================*/

void test_report_invalid_level_returns_false(void)
{
    /* SM_ERROR_NONE is not a valid report level */
    bool ok = SM_Error_Report(s_sm, SM_ERROR_NONE, 0x0001U);
    TEST_ASSERT_FALSE(ok);

    /* Sentinel / out-of-range level */
    ok = SM_Error_Report(s_sm, SM_ERROR_LEVEL_COUNT, 0x0001U);
    TEST_ASSERT_FALSE(ok);

    /* Beyond the sentinel */
    ok = SM_Error_Report(s_sm, (SM_ErrorLevel_t)(SM_ERROR_LEVEL_COUNT + 1), 0x0001U);
    TEST_ASSERT_FALSE(ok);

    /* History should be empty -- no valid report succeeded */
    TEST_ASSERT_EQUAL_UINT8(0U, SM_Error_GetHistoryCount(s_sm));
}

/* =============================================================================
 * TEST 5 -- SM_Error_Report with NULL sm returns false
 * ===========================================================================*/

void test_report_null_sm_returns_false(void)
{
    bool ok = SM_Error_Report(NULL, SM_ERROR_MINOR, 0x0001U);
    TEST_ASSERT_FALSE(ok);
}

/* =============================================================================
 * TEST 6 -- SM_Error_Clear resets current error but NOT critical_lock
 * ===========================================================================*/

void test_clear_resets_current_but_not_critical_lock(void)
{
    /* Report a CRITICAL error to set the lock */
    SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 0x00AAU);
    TEST_ASSERT_TRUE(SM_Error_IsCriticalLock(s_sm));

    /* Clear the current error */
    SM_Error_Clear(s_sm);

    /* Current error should be NONE */
    SM_ErrorInfo_t current;
    bool ok = SM_Error_GetCurrent(s_sm, &current);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_NONE, current.level);
    TEST_ASSERT_EQUAL_UINT16(0U, current.code);
    TEST_ASSERT_EQUAL_UINT16(0U, current.state);
    TEST_ASSERT_EQUAL_UINT32(0U, current.timestamp);

    /* critical_lock must still be set -- Clear does not reset it */
    TEST_ASSERT_TRUE(SM_Error_IsCriticalLock(s_sm));
    TEST_ASSERT_TRUE(s_ctx.error.critical_lock);

    /* minor_active should be cleared */
    TEST_ASSERT_FALSE(SM_Error_IsMinorActive(s_sm));
}

/* =============================================================================
 * TEST 7 -- SM_Error_IsCriticalLock returns false initially, true after CRITICAL
 * ===========================================================================*/

void test_critical_lock_false_initially_true_after_critical(void)
{
    /* Freshly initialized -- no critical lock */
    TEST_ASSERT_FALSE(SM_Error_IsCriticalLock(s_sm));

    /* Report MINOR -- critical lock stays false */
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x0001U);
    TEST_ASSERT_FALSE(SM_Error_IsCriticalLock(s_sm));

    /* Report NORMAL -- critical lock stays false */
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0002U);
    TEST_ASSERT_FALSE(SM_Error_IsCriticalLock(s_sm));

    /* Report CRITICAL -- critical lock becomes true */
    SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 0x0003U);
    TEST_ASSERT_TRUE(SM_Error_IsCriticalLock(s_sm));
}

/* =============================================================================
 * TEST 8 -- SM_Error_IsCriticalLock fires assertion 710 on corrupted DIS
 *
 * After a CRITICAL error, manually corrupt the DIS shadow field, then
 * call SM_Error_IsCriticalLock.  The SM_DIS_VERIFY inside should fire
 * SM_REQUIRE(710, ...) which is captured by TEST_EXPECT_ASSERT.
 * ===========================================================================*/

void test_critical_lock_dis_corruption_fires_assertion_710(void)
{
    /* Set a CRITICAL error so critical_lock=true, DIS is valid */
    SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 0x0010U);
    TEST_ASSERT_TRUE(SM_Error_IsCriticalLock(s_sm));

    /* Corrupt the DIS shadow -- set it to an invalid value */
    s_ctx.error.critical_lock_dis = 0x00U;  /* Should be ~1U = 0xFE */

    /* SM_Error_IsCriticalLock should detect the mismatch and fire assertion 710 */
    TEST_EXPECT_ASSERT(SM_Error_IsCriticalLock(s_sm));
    TEST_ASSERT_EQUAL_STRING("sm_error", test_assert_module);
    TEST_ASSERT_EQUAL_INT(710, test_assert_id);
}

/* =============================================================================
 * TEST 9 -- SM_Error_GetHistory index 0 = most recent error
 * ===========================================================================*/

void test_get_history_index_zero_is_most_recent(void)
{
    SM_Error_Report(s_sm, SM_ERROR_MINOR,  0x0001U);
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0002U);
    SM_Error_Report(s_sm, SM_ERROR_MINOR,  0x0003U);

    SM_ErrorInfo_t info;
    bool ok;

    /* Index 0 = most recent (code 0x0003) */
    ok = SM_Error_GetHistory(s_sm, 0U, &info);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(0x0003U, info.code);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_MINOR, info.level);

    /* Index 1 = second most recent (code 0x0002) */
    ok = SM_Error_GetHistory(s_sm, 1U, &info);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(0x0002U, info.code);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_NORMAL, info.level);

    /* Index 2 = oldest (code 0x0001) */
    ok = SM_Error_GetHistory(s_sm, 2U, &info);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(0x0001U, info.code);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_MINOR, info.level);

    /* Index 3 = out of range */
    ok = SM_Error_GetHistory(s_sm, 3U, &info);
    TEST_ASSERT_FALSE(ok);
}

/* =============================================================================
 * TEST 10 -- SM_Error_GetHistoryCount tracks actual count, not max
 * ===========================================================================*/

void test_history_count_tracks_actual_not_max(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, SM_Error_GetHistoryCount(s_sm));

    SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x0001U);
    TEST_ASSERT_EQUAL_UINT8(1U, SM_Error_GetHistoryCount(s_sm));

    SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x0002U);
    TEST_ASSERT_EQUAL_UINT8(2U, SM_Error_GetHistoryCount(s_sm));

    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0003U);
    TEST_ASSERT_EQUAL_UINT8(3U, SM_Error_GetHistoryCount(s_sm));

    /* Report up to SM_ERROR_HISTORY_SIZE (8) -- count should cap at 8 */
    for (uint8_t i = 3U; i < SM_ERROR_HISTORY_SIZE; i++) {
        SM_Error_Report(s_sm, SM_ERROR_MINOR, (uint16_t)(0x0010U + i));
    }
    TEST_ASSERT_EQUAL_UINT8(SM_ERROR_HISTORY_SIZE, SM_Error_GetHistoryCount(s_sm));

    /* One more report -- count should NOT exceed SM_ERROR_HISTORY_SIZE */
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x00FFU);
    TEST_ASSERT_EQUAL_UINT8(SM_ERROR_HISTORY_SIZE, SM_Error_GetHistoryCount(s_sm));
}

/* =============================================================================
 * TEST 11 -- History wraps correctly (ring buffer overflow)
 *
 * Report more than SM_ERROR_HISTORY_SIZE (8) errors. Verify that the ring
 * buffer overwrites oldest entries and index 0 always returns the most recent.
 * ===========================================================================*/

void test_history_wraps_ring_buffer(void)
{
    /* Report 12 errors -- codes 0x0001 through 0x000C */
    for (uint16_t i = 1U; i <= 12U; i++) {
        SM_Error_Report(s_sm, SM_ERROR_MINOR, i);
    }

    /* Count should be capped at SM_ERROR_HISTORY_SIZE (8) */
    TEST_ASSERT_EQUAL_UINT8(SM_ERROR_HISTORY_SIZE, SM_Error_GetHistoryCount(s_sm));

    /* Index 0 = most recent = code 0x000C (12) */
    SM_ErrorInfo_t info;
    bool ok = SM_Error_GetHistory(s_sm, 0U, &info);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(12U, info.code);

    /* Index 1 = code 0x000B (11) */
    ok = SM_Error_GetHistory(s_sm, 1U, &info);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(11U, info.code);

    /* Verify the full ring: indices 0..7 should map to codes 12, 11, 10, 9, 8, 7, 6, 5 */
    for (uint8_t idx = 0U; idx < SM_ERROR_HISTORY_SIZE; idx++) {
        ok = SM_Error_GetHistory(s_sm, idx, &info);
        TEST_ASSERT_TRUE(ok);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(12U - idx), info.code);
    }

    /* Index 8 = out of range (count is capped at 8) */
    ok = SM_Error_GetHistory(s_sm, SM_ERROR_HISTORY_SIZE, &info);
    TEST_ASSERT_FALSE(ok);
}

/* =============================================================================
 * TEST 12 -- SM_Error_AttemptRecovery calls registered callback
 * ===========================================================================*/

void test_attempt_recovery_calls_registered_callback(void)
{
    SM_Error_RegisterRecoveryCallback(s_sm, test_recovery_cb);
    s_recovery_return_value = false;  /* recovery fails */

    /* Report an error to recover from */
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0042U);

    bool ok = SM_Error_AttemptRecovery(s_sm);
    TEST_ASSERT_FALSE(ok);

    /* Callback should have been invoked exactly once */
    TEST_ASSERT_EQUAL_UINT32(1U, s_recovery_call_count);
    TEST_ASSERT_EQUAL_UINT16(0x0042U, s_recovery_last_code);
}

/* =============================================================================
 * TEST 13 -- SM_Error_AttemptRecovery with no callback returns false
 * ===========================================================================*/

void test_attempt_recovery_no_callback_returns_false(void)
{
    /* Do NOT register a recovery callback */
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0001U);

    bool ok = SM_Error_AttemptRecovery(s_sm);
    TEST_ASSERT_FALSE(ok);

    /* No callback registered, so count should remain 0 */
    TEST_ASSERT_EQUAL_UINT32(0U, s_recovery_call_count);
}

/* =============================================================================
 * TEST 14 -- SM_Error_AttemptRecovery max retries exceeded returns false
 *
 * SM_ERROR_MAX_RECOVERY=3: retry_count increments each call, and when
 * retry_count >= SM_ERROR_MAX_RECOVERY the call returns false immediately
 * without invoking the callback.
 * ===========================================================================*/

void test_attempt_recovery_max_retries_exceeded(void)
{
    SM_Error_RegisterRecoveryCallback(s_sm, test_recovery_cb);
    s_recovery_return_value = false;  /* recovery always fails */

    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0099U);

    /* Attempt recovery SM_ERROR_MAX_RECOVERY (3) times */
    for (uint8_t i = 0U; i < SM_ERROR_MAX_RECOVERY; i++) {
        bool ok = SM_Error_AttemptRecovery(s_sm);
        TEST_ASSERT_FALSE(ok);
    }

    /*
     * Implementation detail: retry_count is incremented BEFORE the max check.
     * After SM_ERROR_MAX_RECOVERY calls, retry_count == SM_ERROR_MAX_RECOVERY.
     * The first two calls invoke the callback (retry_count < max), the third
     * call hits the limit and returns false without calling the callback.
     *
     * Calls 1..2: retry_count goes to 1, 2 (callback invoked each time).
     * Call 3:      retry_count goes to 3 >= SM_ERROR_MAX_RECOVERY -> immediate false.
     *
     * So callback should be invoked (SM_ERROR_MAX_RECOVERY - 1) = 2 times.
     */
    TEST_ASSERT_EQUAL_UINT32(SM_ERROR_MAX_RECOVERY - 1U, s_recovery_call_count);

    /* One more attempt -- should still return false (exceeded) */
    bool ok = SM_Error_AttemptRecovery(s_sm);
    TEST_ASSERT_FALSE(ok);
}

/* =============================================================================
 * TEST 15 -- Recovery callback returning true marks error recovered
 * ===========================================================================*/

void test_recovery_success_marks_error_recovered(void)
{
    SM_Error_RegisterRecoveryCallback(s_sm, test_recovery_cb);
    s_recovery_return_value = true;  /* recovery succeeds */

    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0055U);

    bool ok = SM_Error_AttemptRecovery(s_sm);
    TEST_ASSERT_TRUE(ok);

    /* Current error should be marked as recovered */
    SM_ErrorInfo_t current;
    ok = SM_Error_GetCurrent(s_sm, &current);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(current.recovered);
    TEST_ASSERT_EQUAL_UINT16(0x0055U, current.code);
}

/* =============================================================================
 * TEST 16 -- SM_Error_RegisterNotifyCallback invoked on every Report
 * ===========================================================================*/

void test_notify_callback_invoked_on_every_report(void)
{
    SM_Error_RegisterNotifyCallback(s_sm, test_notify_cb);

    /* Report MINOR */
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x0001U);
    TEST_ASSERT_EQUAL_UINT32(1U, s_notify_call_count);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_MINOR, s_notify_last_level);
    TEST_ASSERT_EQUAL_UINT16(0x0001U, s_notify_last_code);

    /* Report NORMAL */
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0002U);
    TEST_ASSERT_EQUAL_UINT32(2U, s_notify_call_count);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_NORMAL, s_notify_last_level);
    TEST_ASSERT_EQUAL_UINT16(0x0002U, s_notify_last_code);

    /* Report CRITICAL */
    SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 0x0003U);
    TEST_ASSERT_EQUAL_UINT32(3U, s_notify_call_count);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_CRITICAL, s_notify_last_level);
    TEST_ASSERT_EQUAL_UINT16(0x0003U, s_notify_last_code);

    /* Invalid report should NOT invoke callback */
    SM_Error_Report(s_sm, SM_ERROR_NONE, 0x0004U);
    TEST_ASSERT_EQUAL_UINT32(3U, s_notify_call_count);  /* unchanged */
}

/* =============================================================================
 * TEST 17 -- SM_Error_GetStats returns per-level counts, recovery metrics
 * ===========================================================================*/

void test_get_stats_returns_correct_counters(void)
{
    /* Advance sim time so we can verify last_error_time */
    SM_Platform_SimTick();  /* t=1 */
    SM_Platform_SimTick();  /* t=2 */
    SM_Platform_SimTick();  /* t=3 */

    /* Report one of each level */
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x0001U);

    SM_Platform_SimTick();  /* t=4 */
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0002U);

    SM_Platform_SimTick();  /* t=5 */
    SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 0x0003U);

    /* AttemptRecovery is a no-op while critical_lock is active (no callback run). */

    SM_ErrorStats_t stats;
    bool ok = SM_Error_GetStats(s_sm, &stats);
    TEST_ASSERT_TRUE(ok);

    /* Per-level counts */
    TEST_ASSERT_EQUAL_UINT32(0U, stats.errors_by_level[SM_ERROR_NONE]);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.errors_by_level[SM_ERROR_MINOR]);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.errors_by_level[SM_ERROR_NORMAL]);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.errors_by_level[SM_ERROR_CRITICAL]);

    /* Recovery metrics */
    TEST_ASSERT_EQUAL_UINT32(0U, stats.recovery_success);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.recovery_fail);

    /* last_error_time should be 5 (time of the CRITICAL report) */
    TEST_ASSERT_EQUAL_UINT32(5U, stats.last_error_time);

    /* NULL stats pointer returns false */
    ok = SM_Error_GetStats(s_sm, NULL);
    TEST_ASSERT_FALSE(ok);
}

/* =============================================================================
 * TEST 18 -- Stats accumulate across multiple Report/Recovery calls
 * ===========================================================================*/

void test_stats_accumulate_across_multiple_calls(void)
{
    SM_Error_RegisterRecoveryCallback(s_sm, test_recovery_cb);

    /* Report 3 MINOR errors */
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x0010U);
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x0011U);
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 0x0012U);

    /* Report 2 NORMAL errors */
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0020U);
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0021U);

    /* Attempt recovery on the most recent NORMAL error -- succeed */
    s_recovery_return_value = true;
    bool recovered = SM_Error_AttemptRecovery(s_sm);
    TEST_ASSERT_TRUE(recovered);

    /* Report another NORMAL error */
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x0022U);

    /* Attempt recovery -- fail this time */
    s_recovery_return_value = false;
    recovered = SM_Error_AttemptRecovery(s_sm);
    TEST_ASSERT_FALSE(recovered);

    /* Report 1 CRITICAL error */
    SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 0x00FFU);

    SM_ErrorStats_t stats;
    bool ok = SM_Error_GetStats(s_sm, &stats);
    TEST_ASSERT_TRUE(ok);

    /* Accumulated per-level counts */
    TEST_ASSERT_EQUAL_UINT32(3U, stats.errors_by_level[SM_ERROR_MINOR]);
    TEST_ASSERT_EQUAL_UINT32(3U, stats.errors_by_level[SM_ERROR_NORMAL]);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.errors_by_level[SM_ERROR_CRITICAL]);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.errors_by_level[SM_ERROR_NONE]);

    /* Recovery metrics: 1 success, 1 fail */
    TEST_ASSERT_EQUAL_UINT32(1U, stats.recovery_success);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.recovery_fail);
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

/* =============================================================================
 * MINOR TIER ACCESSORS (v4.2, W3 / D18)
 *
 * The tier used to be documented as "auto-recovery" and implemented as two
 * fields nothing read: minor_active had no reader anywhere in the framework
 * and no public accessor, so an application could not have implemented the
 * advertised behaviour even if it wanted to. D18 keeps the state and exposes
 * it; the policy stays with the application.
 * ===========================================================================*/

void test_minor_accessors_report_the_reported_error(void)
{
    uint32_t at = 0xDEADBEEFU;

    TEST_ASSERT_FALSE(SM_Error_IsMinorActive(s_sm));
    TEST_ASSERT_FALSE(SM_Error_GetMinorTimestamp(s_sm, &at));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFU, at);   /* untouched when inactive */

    /* Advance the clock first. Reporting at t=0 would let this pass even if
     * minor_timestamp were never written -- a weak assertion is worse than
     * none, because it reads as coverage. */
    for (uint32_t i = 0U; i < 5U; i++) {
        SM_Platform_SimTick();
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0U, SM_Platform_GetTimeMs());

    TEST_ASSERT_TRUE(SM_Error_Report(s_sm, SM_ERROR_MINOR, 42U));

    TEST_ASSERT_TRUE(SM_Error_IsMinorActive(s_sm));
    TEST_ASSERT_TRUE(SM_Error_GetMinorTimestamp(s_sm, &at));
    TEST_ASSERT_EQUAL_UINT32(SM_Platform_GetTimeMs(), at);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, at);
}

/* ClearMinor retires the minor flag WITHOUT wiping the current error record --
 * that distinction is the reason it exists alongside SM_Error_Clear. */
void test_clear_minor_leaves_the_current_error_record(void)
{
    SM_ErrorInfo_t cur;

    TEST_ASSERT_TRUE(SM_Error_Report(s_sm, SM_ERROR_MINOR, 7U));
    TEST_ASSERT_TRUE(SM_Error_IsMinorActive(s_sm));

    SM_Error_ClearMinor(s_sm);

    TEST_ASSERT_FALSE(SM_Error_IsMinorActive(s_sm));
    TEST_ASSERT_TRUE(SM_Error_GetCurrent(s_sm, &cur));
    TEST_ASSERT_EQUAL_UINT16(7U, cur.code);
    TEST_ASSERT_EQUAL(SM_ERROR_MINOR, cur.level);
}

/* SM_Error_Clear wipes both. */
void test_error_clear_also_clears_minor(void)
{
    TEST_ASSERT_TRUE(SM_Error_Report(s_sm, SM_ERROR_MINOR, 7U));
    SM_Error_Clear(s_sm);
    TEST_ASSERT_FALSE(SM_Error_IsMinorActive(s_sm));
}

/* A more serious error supersedes an outstanding minor one. */
void test_a_normal_error_retires_an_outstanding_minor(void)
{
    TEST_ASSERT_TRUE(SM_Error_Report(s_sm, SM_ERROR_MINOR, 1U));
    TEST_ASSERT_TRUE(SM_Error_IsMinorActive(s_sm));

    TEST_ASSERT_TRUE(SM_Error_Report(s_sm, SM_ERROR_NORMAL, 2U));
    TEST_ASSERT_FALSE(SM_Error_IsMinorActive(s_sm));
}

void test_reset_retires_an_outstanding_minor(void)
{
    TEST_ASSERT_TRUE(SM_Error_Report(s_sm, SM_ERROR_MINOR, 1U));
    SM_Reset(s_sm);
    TEST_ASSERT_FALSE(SM_Error_IsMinorActive(s_sm));
}

/* Repeated ClearMinor must be harmless -- an application polling a policy will
 * call it more than once. */
void test_clear_minor_is_idempotent(void)
{
    SM_Error_ClearMinor(s_sm);
    SM_Error_ClearMinor(s_sm);
    TEST_ASSERT_FALSE(SM_Error_IsMinorActive(s_sm));
}

/* NULL contracts: assert and stay safe, matching every other error API. */
void test_minor_accessors_reject_null_handle(void)
{
    uint32_t at = 0U;

    TEST_EXPECT_ASSERT((void)SM_Error_IsMinorActive(NULL));
    TEST_ASSERT_EQUAL_INT(750, test_assert_id);

    test_assert_clear();
    TEST_EXPECT_ASSERT((void)SM_Error_GetMinorTimestamp(NULL, &at));
    TEST_ASSERT_EQUAL_INT(751, test_assert_id);

    test_assert_clear();
    TEST_EXPECT_ASSERT(SM_Error_ClearMinor(NULL));
    TEST_ASSERT_EQUAL_INT(753, test_assert_id);
}

void test_get_minor_timestamp_rejects_null_out(void)
{
    TEST_ASSERT_TRUE(SM_Error_Report(s_sm, SM_ERROR_MINOR, 1U));
    TEST_EXPECT_ASSERT((void)SM_Error_GetMinorTimestamp(s_sm, NULL));
    TEST_ASSERT_EQUAL_INT(752, test_assert_id);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_report_minor_sets_minor_active_and_records_history);
    RUN_TEST(test_minor_accessors_report_the_reported_error);
    RUN_TEST(test_clear_minor_leaves_the_current_error_record);
    RUN_TEST(test_error_clear_also_clears_minor);
    RUN_TEST(test_a_normal_error_retires_an_outstanding_minor);
    RUN_TEST(test_reset_retires_an_outstanding_minor);
    RUN_TEST(test_clear_minor_is_idempotent);
    RUN_TEST(test_minor_accessors_reject_null_handle);
    RUN_TEST(test_get_minor_timestamp_rejects_null_out);
    RUN_TEST(test_report_normal_records_history_and_sets_current);
    RUN_TEST(test_report_critical_sets_lock_and_dis);
    RUN_TEST(test_report_invalid_level_returns_false);
    RUN_TEST(test_report_null_sm_returns_false);
    RUN_TEST(test_clear_resets_current_but_not_critical_lock);
    RUN_TEST(test_critical_lock_false_initially_true_after_critical);
    RUN_TEST(test_critical_lock_dis_corruption_fires_assertion_710);
    RUN_TEST(test_get_history_index_zero_is_most_recent);
    RUN_TEST(test_history_count_tracks_actual_not_max);
    RUN_TEST(test_history_wraps_ring_buffer);
    RUN_TEST(test_attempt_recovery_calls_registered_callback);
    RUN_TEST(test_attempt_recovery_no_callback_returns_false);
    RUN_TEST(test_attempt_recovery_max_retries_exceeded);
    RUN_TEST(test_recovery_success_marks_error_recovered);
    RUN_TEST(test_notify_callback_invoked_on_every_report);
    RUN_TEST(test_get_stats_returns_correct_counters);
    RUN_TEST(test_stats_accumulate_across_multiple_calls);

    return UNITY_END();
}
