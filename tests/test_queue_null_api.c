/**
 * @file test_queue_null_api.c
 * @brief Event queue helpers — null-handle semantics
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

void test_q_contract_is_full_null_reads_true(void)
{
    TEST_ASSERT_TRUE(SM_EventQueueIsFull(NULL));
}

void test_q_is_empty_null_reads_true(void)
{
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(NULL));
}

void test_q_depth_null_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, SM_EventQueueDepth(NULL));
}

void test_q_flush_null_safe(void)
{
    SM_EventQueueFlush(NULL);
    TEST_PASS();
}

void test_q_get_min_null_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, SM_EventQueueGetMin(NULL));
}

void test_q_empty_after_init(void)
{
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(s_sm));
}

void test_q_depth_one_after_single_post(void)
{
    SM_PostEvent(s_sm, TEST_EVT_STOP, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, SM_EventQueueDepth(s_sm));
}

void test_q_not_full_when_sparse(void)
{
    TEST_ASSERT_FALSE(SM_EventQueueIsFull(s_sm));
}

void test_q_flush_clears_depth(void)
{
    SM_PostEvent(s_sm, TEST_EVT_STOP, 0U);
    SM_EventQueueFlush(s_sm);
    TEST_ASSERT_EQUAL_UINT8(0U, SM_EventQueueDepth(s_sm));
}

void test_q_depth_counts_front_and_ring(void)
{
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    SM_PostEvent(s_sm, TEST_EVT_STOP, 0U);
    uint8_t d = SM_EventQueueDepth(s_sm);
    TEST_ASSERT_TRUE(d >= 2U);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_q_contract_is_full_null_reads_true);
    RUN_TEST(test_q_is_empty_null_reads_true);
    RUN_TEST(test_q_depth_null_zero);
    RUN_TEST(test_q_flush_null_safe);
    RUN_TEST(test_q_get_min_null_zero);
    RUN_TEST(test_q_empty_after_init);
    RUN_TEST(test_q_depth_one_after_single_post);
    RUN_TEST(test_q_not_full_when_sparse);
    RUN_TEST(test_q_flush_clears_depth);
    RUN_TEST(test_q_depth_counts_front_and_ring);
    return UNITY_END();
}
