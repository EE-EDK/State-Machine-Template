/**
 * @file test_reset_extras.c
 * @brief SM_Reset null safety and error clear integration
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

void test_reset_contract_null_sm_safe(void)
{
    SM_Reset(NULL);
    TEST_PASS();
}

void test_reset_clears_reported_error_layer(void)
{
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 1U);
    SM_Reset(s_sm);
    SM_ErrorInfo_t ci;
    TEST_ASSERT_TRUE(SM_Error_GetCurrent(s_sm, &ci));
    TEST_ASSERT_EQUAL_INT(SM_ERROR_NONE, ci.level);
}

void test_reset_returns_to_initial_state(void)
{
    SM_Process(s_sm);
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    SM_Process(s_sm);
    SM_Reset(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
}

void test_reset_flushes_queue_integration(void)
{
    SM_PostEvent(s_sm, TEST_EVT_STOP, 1U);
    SM_Reset(s_sm);
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(s_sm));
}

void test_double_reset_stable(void)
{
    SM_Reset(s_sm);
    SM_Reset(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
}

void test_reset_then_process_tick_safe(void)
{
    SM_Reset(s_sm);
    SM_Process(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
}

void test_reset_after_minor_error(void)
{
    SM_Error_Report(s_sm, SM_ERROR_MINOR, 9U);
    SM_Reset(s_sm);
    SM_ErrorInfo_t ci;
    SM_Error_GetCurrent(s_sm, &ci);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_NONE, ci.level);
}

void test_reset_then_transition_still_works(void)
{
    SM_Reset(s_sm);
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    SM_Process(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(s_sm));
}

void test_reset_state_exec_count_zeroed(void)
{
    SM_Process(s_sm);
    SM_Reset(s_sm);
    TEST_ASSERT_EQUAL_UINT32(0U, SM_GetExecCount(s_sm));
}

void test_reset_clears_previous_error_code_field(void)
{
    SM_Error_Report(s_sm, SM_ERROR_NORMAL, 0x55U);
    SM_Reset(s_sm);
    SM_ErrorInfo_t ci;
    SM_Error_GetCurrent(s_sm, &ci);
    TEST_ASSERT_EQUAL_UINT16(0U, ci.code);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_contract_null_sm_safe);
    RUN_TEST(test_reset_clears_reported_error_layer);
    RUN_TEST(test_reset_returns_to_initial_state);
    RUN_TEST(test_reset_flushes_queue_integration);
    RUN_TEST(test_double_reset_stable);
    RUN_TEST(test_reset_then_process_tick_safe);
    RUN_TEST(test_reset_after_minor_error);
    RUN_TEST(test_reset_then_transition_still_works);
    RUN_TEST(test_reset_state_exec_count_zeroed);
    RUN_TEST(test_reset_clears_previous_error_code_field);
    return UNITY_END();
}
