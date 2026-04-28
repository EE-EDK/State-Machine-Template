/**
 * @file test_recovery_edges.c
 * @brief SM_Error_AttemptRecovery edge semantics
 */
#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

static const SM_StateDesc_t s_states[SM_STATE_COUNT] = {
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
};
static const SM_Transition_t s_trans[] = {
    { TEST_STATE_INIT, TEST_EVT_START, TEST_STATE_RUNNING, 0U, NULL, NULL },
};
static const SM_Config_t s_cfg = {
    .states = s_states,
    .transitions = s_trans,
    .transition_count = 1U,
    .initial_state = TEST_STATE_INIT,
};

static SM_Context_t s_ctx;
static SM_Handle_t  s_sm = &s_ctx;

static uint32_t s_cb_invocations;

static bool recovery_ok(SM_Handle_t sm, uint16_t code)
{
    (void)sm;
    (void)code;
    s_cb_invocations++;
    return true;
}

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
    s_cb_invocations = 0U;
    TEST_ASSERT_TRUE(SM_Init(s_sm, &s_cfg));
}

void tearDown(void)
{
}

void test_rec_contract_none_level_returns_true(void)
{
    TEST_ASSERT_TRUE(SM_Error_AttemptRecovery(s_sm));
}

void test_rec_blocked_under_critical_lock(void)
{
    SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 1U);
    TEST_ASSERT_FALSE(SM_Error_AttemptRecovery(s_sm));
}

void test_rec_with_normal_error_and_callback(void)
{
    SM_Error_RegisterRecoveryCallback(s_sm, recovery_ok);
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 3U);
    TEST_ASSERT_TRUE(SM_Error_AttemptRecovery(s_sm));
    TEST_ASSERT_EQUAL_UINT32(1U, s_cb_invocations);
}

void test_rec_false_when_no_callback_and_error(void)
{
    SM_Error_RegisterRecoveryCallback(s_sm, NULL);
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 2U);
    TEST_ASSERT_FALSE(SM_Error_AttemptRecovery(s_sm));
}

void test_rec_cleared_error_short_circuits_true(void)
{
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 9U);
    SM_Error_Clear(s_sm);
    TEST_ASSERT_TRUE(SM_Error_AttemptRecovery(s_sm));
}

void test_rec_minor_then_increment_retry(void)
{
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 1U);
    SM_Error_RegisterRecoveryCallback(s_sm, recovery_ok);
    SM_Error_AttemptRecovery(s_sm);
    SM_ErrorInfo_t ci;
    SM_Error_GetCurrent(s_sm, &ci);
    TEST_ASSERT_TRUE(ci.retry_count > 0U);
}

void test_rec_unregister_callback_mid_episode(void)
{
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 5U);
    SM_Error_RegisterRecoveryCallback(s_sm, recovery_ok);
    SM_Error_RegisterRecoveryCallback(s_sm, NULL);
    TEST_ASSERT_FALSE(SM_Error_AttemptRecovery(s_sm));
}

void test_rec_invalid_sm_false(void)
{
    TEST_ASSERT_FALSE(SM_Error_AttemptRecovery(NULL));
}

void test_rec_after_success_still_has_level_until_clear(void)
{
    SM_Error_RegisterRecoveryCallback(s_sm, recovery_ok);
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 7U);
    SM_Error_AttemptRecovery(s_sm);
    SM_ErrorInfo_t ci;
    SM_Error_GetCurrent(s_sm, &ci);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_NORMAL, ci.level);
}

void test_rec_stats_fail_when_no_callback(void)
{
    SM_Error_RegisterRecoveryCallback(s_sm, NULL);
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 4U);
    SM_Error_AttemptRecovery(s_sm);
    SM_ErrorStats_t st;
    SM_Error_GetStats(s_sm, &st);
    TEST_ASSERT_TRUE(st.recovery_fail > 0U);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rec_contract_none_level_returns_true);
    RUN_TEST(test_rec_blocked_under_critical_lock);
    RUN_TEST(test_rec_with_normal_error_and_callback);
    RUN_TEST(test_rec_false_when_no_callback_and_error);
    RUN_TEST(test_rec_cleared_error_short_circuits_true);
    RUN_TEST(test_rec_minor_then_increment_retry);
    RUN_TEST(test_rec_unregister_callback_mid_episode);
    RUN_TEST(test_rec_invalid_sm_false);
    RUN_TEST(test_rec_after_success_still_has_level_until_clear);
    RUN_TEST(test_rec_stats_fail_when_no_callback);
    return UNITY_END();
}
