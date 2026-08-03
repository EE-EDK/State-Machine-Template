/**
 * @file test_rt_transition.c
 * @brief SM_AddTransition bounds + table capacity (runtime transitions enabled)
 *
 * v4.0 contract: valid events are the user range (< SM_EVENT_COUNT) PLUS
 * SM_EVT_TIMEOUT (== SM_EVENT_COUNT), so runtime transitions can handle
 * state timeouts. Anything above SM_EVT_TIMEOUT is rejected.
 */
#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

#if SM_FEATURE_RUNTIME_TRANSITIONS

static const SM_StateDesc_t s_states[SM_STATE_COUNT] = {
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
};

static const SM_Transition_t s_base[] = {
    { TEST_STATE_INIT, TEST_EVT_START, TEST_STATE_RUNNING, 0U, NULL, NULL },
};

static const SM_Config_t s_cfg = {
    .states = s_states,
    .transitions = s_base,
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

/* 1a — v4.0 contract: SM_EVT_TIMEOUT (== SM_EVENT_COUNT) is ACCEPTED so
 * runtime transitions can handle state timeouts */
void test_rt_accepts_timeout_event(void)
{
    SM_Transition_t t = {
        .from_state = TEST_STATE_INIT,
        .to_state   = TEST_STATE_RUNNING,
        .event      = SM_EVT_TIMEOUT,
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_TRUE(SM_AddTransition(s_sm, &t));
}

/* 1b — contract: events above SM_EVT_TIMEOUT are rejected */
void test_rt_rejects_event_above_timeout(void)
{
    SM_Transition_t t = {
        .from_state = TEST_STATE_INIT,
        .to_state   = TEST_STATE_RUNNING,
        .event      = (uint16_t)(SM_EVENT_COUNT + 1U),
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_FALSE(SM_AddTransition(s_sm, &t));
}

void test_rt_rejects_from_state_out_of_range(void)
{
    SM_Transition_t t = {
        .from_state = SM_STATE_COUNT,
        .to_state   = TEST_STATE_INIT,
        .event      = TEST_EVT_DATA,
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_FALSE(SM_AddTransition(s_sm, &t));
}

void test_rt_rejects_to_state_out_of_range(void)
{
    SM_Transition_t t = {
        .from_state = TEST_STATE_INIT,
        .to_state   = SM_STATE_COUNT,
        .event      = TEST_EVT_DATA,
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_FALSE(SM_AddTransition(s_sm, &t));
}

void test_rt_rejects_null_sm(void)
{
    SM_Transition_t t = {
        .from_state = TEST_STATE_INIT,
        .to_state   = TEST_STATE_RUNNING,
        .event      = TEST_EVT_DATA,
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_FALSE(SM_AddTransition(NULL, &t));
}

void test_rt_rejects_null_transition(void)
{
    TEST_ASSERT_FALSE(SM_AddTransition(s_sm, NULL));
}

void test_rt_rejects_uninitialized_handle(void)
{
    SM_Context_t raw;
    memset(&raw, 0, sizeof(raw));
    raw.initialized = false;
    SM_Transition_t t = {
        .from_state = TEST_STATE_INIT,
        .to_state   = TEST_STATE_RUNNING,
        .event      = TEST_EVT_DATA,
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_FALSE(SM_AddTransition((SM_Handle_t)&raw, &t));
}

void test_rt_accepts_max_valid_event_index(void)
{
    uint16_t last = (uint16_t)(SM_EVENT_COUNT - 1U);
    SM_Transition_t t = {
        .from_state = TEST_STATE_INIT,
        .to_state   = TEST_STATE_INIT,
        .event      = last,
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_TRUE(SM_AddTransition(s_sm, &t));
}

void test_rt_accepts_valid_row(void)
{
    SM_Transition_t t = {
        .from_state = TEST_STATE_RUNNING,
        .to_state   = TEST_STATE_STOPPED,
        .event      = TEST_EVT_STOP,
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_TRUE(SM_AddTransition(s_sm, &t));
}

void test_rt_fails_when_table_full(void)
{
    for (uint32_t i = 0; i < (uint32_t)SM_MAX_TRANSITIONS; i++) {
        SM_Transition_t t = {
            .from_state = TEST_STATE_INIT,
            .to_state   = TEST_STATE_RUNNING,
            .event      = TEST_EVT_START,
            ._reserved  = 0U,
            .guard      = NULL,
            .action     = NULL
        };
        TEST_ASSERT_TRUE(SM_AddTransition(s_sm, &t));
    }
    SM_Transition_t overflow = {
        .from_state = TEST_STATE_INIT,
        .to_state   = TEST_STATE_RUNNING,
        .event      = TEST_EVT_START,
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_FALSE(SM_AddTransition(s_sm, &overflow));
}

void test_rt_first_event_boundary_zero(void)
{
    SM_Transition_t t = {
        .from_state = TEST_STATE_INIT,
        .to_state   = TEST_STATE_INIT,
        .event      = 0U,
        ._reserved  = 0U,
        .guard      = NULL,
        .action     = NULL
    };
    TEST_ASSERT_TRUE(SM_AddTransition(s_sm, &t));
}

#else /* SM_FEATURE_RUNTIME_TRANSITIONS */

void setUp(void)
{
}

void test_runtime_transitions_disabled_stub(void)
{
    TEST_IGNORE_MESSAGE("SM_FEATURE_RUNTIME_TRANSITIONS off");
}

#endif

void tearDown(void)
{
}

int main(void)
{
    UNITY_BEGIN();
#if SM_FEATURE_RUNTIME_TRANSITIONS
    RUN_TEST(test_rt_accepts_timeout_event);
    RUN_TEST(test_rt_rejects_event_above_timeout);
    RUN_TEST(test_rt_rejects_from_state_out_of_range);
    RUN_TEST(test_rt_rejects_to_state_out_of_range);
    RUN_TEST(test_rt_rejects_null_sm);
    RUN_TEST(test_rt_rejects_null_transition);
    RUN_TEST(test_rt_rejects_uninitialized_handle);
    RUN_TEST(test_rt_accepts_max_valid_event_index);
    RUN_TEST(test_rt_accepts_valid_row);
    RUN_TEST(test_rt_fails_when_table_full);
    RUN_TEST(test_rt_first_event_boundary_zero);
#else
    RUN_TEST(test_runtime_transitions_disabled_stub);
#endif
    return UNITY_END();
}
