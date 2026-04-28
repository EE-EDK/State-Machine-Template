/**
 * @file test_error_null_api.c
 * @brief Error subsystem null-handle contracts
 *
 * Contract (test 1): SM_Error_Report(NULL,…) is false without crashing.
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

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
    TEST_ASSERT_TRUE(SM_Init(s_sm, &s_cfg));
}

void tearDown(void)
{
}

void test_err_contract_report_null_sm_false(void)
{
    TEST_ASSERT_FALSE(SM_Error_Report(NULL, SM_ERROR_MINOR, 1U));
}

void test_err_get_current_null_sm_false(void)
{
    SM_ErrorInfo_t info;
    TEST_ASSERT_FALSE(SM_Error_GetCurrent(NULL, &info));
}

void test_err_get_current_null_info_false(void)
{
    TEST_ASSERT_FALSE(SM_Error_GetCurrent(s_sm, NULL));
}

void test_err_get_stats_null_sm_false(void)
{
    SM_ErrorStats_t st;
    TEST_ASSERT_FALSE(SM_Error_GetStats(NULL, &st));
}

void test_err_get_stats_null_stats_false(void)
{
    TEST_ASSERT_FALSE(SM_Error_GetStats(s_sm, NULL));
}

void test_err_get_history_null_sm_false(void)
{
    SM_ErrorInfo_t info;
    TEST_ASSERT_FALSE(SM_Error_GetHistory(NULL, 0U, &info));
}

void test_err_get_history_null_info_false(void)
{
    TEST_ASSERT_FALSE(SM_Error_GetHistory(s_sm, 0U, NULL));
}

void test_err_clear_null_sm_safe(void)
{
    SM_Error_Clear(NULL); /* must not crash */
    TEST_PASS();
}

void test_err_attempt_recovery_null_false(void)
{
    TEST_ASSERT_FALSE(SM_Error_AttemptRecovery(NULL));
}

void test_err_register_callbacks_null_sm_safe(void)
{
    SM_Error_RegisterRecoveryCallback(NULL, NULL);
    SM_Error_RegisterNotifyCallback(NULL, NULL);
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_err_contract_report_null_sm_false);
    RUN_TEST(test_err_get_current_null_sm_false);
    RUN_TEST(test_err_get_current_null_info_false);
    RUN_TEST(test_err_get_stats_null_sm_false);
    RUN_TEST(test_err_get_stats_null_stats_false);
    RUN_TEST(test_err_get_history_null_sm_false);
    RUN_TEST(test_err_get_history_null_info_false);
    RUN_TEST(test_err_clear_null_sm_safe);
    RUN_TEST(test_err_attempt_recovery_null_false);
    RUN_TEST(test_err_register_callbacks_null_sm_safe);
    return UNITY_END();
}
