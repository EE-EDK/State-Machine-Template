/**
 * @file test_state_query_null.c
 * @brief State / history query API null and invalid buffer handling
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

void test_sq_contract_get_state_null_returns_zero(void)
{
    TEST_ASSERT_EQUAL_UINT16(0U, SM_GetState(NULL));
}

void test_sq_get_previous_null_zero(void)
{
    TEST_ASSERT_EQUAL_UINT16(0U, SM_GetPreviousState(NULL));
}

void test_sq_state_time_null_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, SM_GetStateTime(NULL));
}

void test_sq_exec_count_null_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, SM_GetExecCount(NULL));
}

void test_sq_history_null_sm_false(void)
{
    uint16_t buf[4];
    uint8_t cnt = 0;
    TEST_ASSERT_FALSE(SM_GetStateHistory(NULL, buf, 4, &cnt));
}

void test_sq_history_null_buf_false(void)
{
    uint8_t cnt = 0;
    TEST_ASSERT_FALSE(SM_GetStateHistory(s_sm, NULL, 4, &cnt));
}

void test_sq_history_null_count_false(void)
{
    uint16_t buf[4];
    TEST_ASSERT_FALSE(SM_GetStateHistory(s_sm, buf, 4, NULL));
}

void test_sq_history_zero_buf_len_false(void)
{
    uint16_t buf[4];
    uint8_t cnt = 0;
    TEST_ASSERT_FALSE(SM_GetStateHistory(s_sm, buf, 0, &cnt));
}

void test_sq_history_ok_after_init(void)
{
    uint16_t buf[SM_STATE_HISTORY_DEPTH];
    uint8_t cnt = 0;
    SM_Process(s_sm);
    TEST_ASSERT_TRUE(SM_GetStateHistory(s_sm, buf, SM_STATE_HISTORY_DEPTH, &cnt));
    TEST_ASSERT_TRUE(cnt > 0U);
}

void test_sq_get_state_matches_after_init_process(void)
{
    SM_Process(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sq_contract_get_state_null_returns_zero);
    RUN_TEST(test_sq_get_previous_null_zero);
    RUN_TEST(test_sq_state_time_null_zero);
    RUN_TEST(test_sq_exec_count_null_zero);
    RUN_TEST(test_sq_history_null_sm_false);
    RUN_TEST(test_sq_history_null_buf_false);
    RUN_TEST(test_sq_history_null_count_false);
    RUN_TEST(test_sq_history_zero_buf_len_false);
    RUN_TEST(test_sq_history_ok_after_init);
    RUN_TEST(test_sq_get_state_matches_after_init_process);
    return UNITY_END();
}
