/**
 * @file test_process_null.c
 * @brief SM_Process guard rails
 */
#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

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

void test_proc_contract_null_sm_returns(void)
{
    SM_Process(NULL);
    TEST_PASS();
}

void test_proc_uninitialized_returns_early(void)
{
    SM_Context_t raw;
    memset(&raw, 0, sizeof(raw));
    raw.initialized = false;
    /* DIS pair for primary 0 — otherwise SM_DIS_VERIFY(init) fires assertion 200 */
    raw.init_dis = (uint8_t)(~(uint8_t)0U);
    SM_Process((SM_Handle_t)&raw);
    TEST_PASS();
}

void test_proc_runs_after_init(void)
{
    SM_Process(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
}

void test_proc_safe_after_critical_lock_skips_body(void)
{
    SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 1U);
    SM_Process(s_sm); /* locked — should return without executing state machine body */
    TEST_PASS();
}

void test_proc_multiple_ticks_increment_exec_if_not_locked(void)
{
    SM_Process(s_sm);
    uint32_t a = SM_GetExecCount(s_sm);
    SM_Process(s_sm);
    uint32_t b = SM_GetExecCount(s_sm);
    TEST_ASSERT_TRUE(a > 0U);
    TEST_ASSERT_TRUE(b > a);
}

void test_proc_after_reset_runs_entry_path(void)
{
    SM_Process(s_sm);
    SM_Reset(s_sm);
    SM_Process(s_sm);
    TEST_ASSERT_TRUE(SM_GetExecCount(s_sm) > 0U);
}

void test_proc_idempotent_on_empty_queue(void)
{
    SM_Process(s_sm);
    SM_Process(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
}

void test_proc_after_post_delivers_when_unlocked(void)
{
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    SM_Process(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(s_sm));
}

void test_proc_two_handles_isolated(void)
{
    SM_Context_t other;
    TEST_ASSERT_TRUE(SM_Init(&other, &s_cfg));
    SM_Process(s_sm);
    SM_Process(&other);
    TEST_ASSERT_EQUAL_UINT16(SM_GetState(s_sm), SM_GetState(&other));
}

void test_proc_zero_cost_null_second_call_pattern(void)
{
    SM_Process(NULL);
    SM_Process(s_sm);
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_proc_contract_null_sm_returns);
    RUN_TEST(test_proc_uninitialized_returns_early);
    RUN_TEST(test_proc_runs_after_init);
    RUN_TEST(test_proc_safe_after_critical_lock_skips_body);
    RUN_TEST(test_proc_multiple_ticks_increment_exec_if_not_locked);
    RUN_TEST(test_proc_after_reset_runs_entry_path);
    RUN_TEST(test_proc_idempotent_on_empty_queue);
    RUN_TEST(test_proc_after_post_delivers_when_unlocked);
    RUN_TEST(test_proc_two_handles_isolated);
    RUN_TEST(test_proc_zero_cost_null_second_call_pattern);
    return UNITY_END();
}
