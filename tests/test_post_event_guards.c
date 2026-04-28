/**
 * @file test_post_event_guards.c
 * @brief SM_PostEvent parameter and state guards
 *
 * Contract (test 1): posting disallowed event index fails.
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

void test_post_contract_rejects_event_out_of_range(void)
{
    TEST_ASSERT_FALSE(SM_PostEvent(s_sm, (uint16_t)SM_EVENT_COUNT, 0U));
}

void test_post_false_for_null_sm(void)
{
    TEST_ASSERT_FALSE(SM_PostEvent(NULL, TEST_EVT_START, 0U));
}

void test_post_false_when_not_initialized(void)
{
    SM_Context_t raw;
    memset(&raw, 0, sizeof(raw));
    TEST_ASSERT_FALSE(SM_PostEvent((SM_Handle_t)&raw, TEST_EVT_START, 0U));
}

void test_post_true_front_slot_first_event(void)
{
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_STOP, 42U));
}

void test_post_last_valid_event_index(void)
{
    uint16_t ev = (uint16_t)(SM_EVENT_COUNT - 1U);
    SM_EventQueueFlush(s_sm);
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, ev, 0U));
}

void test_post_preserves_data_payload(void)
{
    SM_EventQueueFlush(s_sm);
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0xDEADBEEFU));
}

void test_post_multiple_distinct_after_first(void)
{
    SM_PostEvent(s_sm, TEST_EVT_START, 1U);
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_STOP, 2U));
}

void test_post_zero_event_allowed_if_enum_includes_zero(void)
{
    SM_EventQueueFlush(s_sm);
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, 0U, 0U));
}

void test_post_event_zero_data(void)
{
    SM_EventQueueFlush(s_sm);
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0U));
}

void test_post_after_flush_empty(void)
{
    SM_PostEvent(s_sm, TEST_EVT_START, 1U);
    SM_EventQueueFlush(s_sm);
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(s_sm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_post_contract_rejects_event_out_of_range);
    RUN_TEST(test_post_false_for_null_sm);
    RUN_TEST(test_post_false_when_not_initialized);
    RUN_TEST(test_post_true_front_slot_first_event);
    RUN_TEST(test_post_last_valid_event_index);
    RUN_TEST(test_post_preserves_data_payload);
    RUN_TEST(test_post_multiple_distinct_after_first);
    RUN_TEST(test_post_zero_event_allowed_if_enum_includes_zero);
    RUN_TEST(test_post_event_zero_data);
    RUN_TEST(test_post_after_flush_empty);
    return UNITY_END();
}
