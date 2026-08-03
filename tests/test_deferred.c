/**
 * @file test_deferred.c
 * @brief Unity tests for the deferred event subsystem (SM_FEATURE_DEFER)
 *
 * Covers SM_DeferEvent, SM_RecallEvent, SM_FlushDeferred.
 * The defer queue is SM_DEFER_QUEUE_SIZE=4 in the test build.
 *
 * Key behavior under test:
 *   - SM_DeferEvent stores events into the defer ring buffer (FIFO insert)
 *   - SM_RecallEvent pops the oldest deferred event (FIFO recall from defer
 *     queue) and posts it to the FRONT of the main event queue, so it is
 *     processed before any events already in the main ring buffer
 *   - SM_FlushDeferred discards all deferred events
 *
 * Assertion ID ranges exercised: 400-499 (sm_engine deferred events).
 */

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

/* =============================================================================
 * TEST FIXTURE -- minimal 2-state FSM
 *
 * INIT --[START]--> RUNNING
 * RUNNING --[STOP]--> STOPPED
 *
 * on_entry/on_execute/on_exit are NULL (no-op) for these tests.
 * We only need a valid, initialized SM_Context_t.
 * ===========================================================================*/

/* Track which event was last processed during SM_Process */
static uint16_t last_processed_event;
static uint32_t last_processed_data;
static uint16_t processed_event_log[16];
static uint8_t  processed_event_count;

/**
 * @brief on_execute callback that records the event for verification.
 *
 * NOTE: SM_Process dispatches events internally. We cannot intercept the
 * dequeue directly. Instead, we use transition actions to observe which
 * event triggered a transition.
 */
static void action_record_event(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    last_processed_event = event;
    last_processed_data = data;
    if (processed_event_count < 16U) {
        processed_event_log[processed_event_count++] = event;
    }
}

/* State descriptors -- all callbacks NULL */
static const SM_StateDesc_t test_states[SM_STATE_COUNT] = {
    [TEST_STATE_INIT]    = { NULL, NULL, NULL, 0, 0 },
    [TEST_STATE_RUNNING] = { NULL, NULL, NULL, 0, 0 },
    [TEST_STATE_STOPPED] = { NULL, NULL, NULL, 0, 0 },
    [TEST_STATE_ERROR]   = { NULL, NULL, NULL, 0, 0 },
};

/* Transition table with action recorder */
static const SM_Transition_t test_transitions[] = {
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0, NULL, action_record_event },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0, NULL, action_record_event },
    { TEST_STATE_RUNNING, TEST_EVT_DATA,  TEST_STATE_RUNNING, 0, NULL, action_record_event },
    { TEST_STATE_RUNNING, TEST_EVT_ACK,   TEST_STATE_RUNNING, 0, NULL, action_record_event },
    { TEST_STATE_INIT,    TEST_EVT_DATA,  TEST_STATE_INIT,    0, NULL, action_record_event },
    { TEST_STATE_INIT,    TEST_EVT_ACK,   TEST_STATE_INIT,    0, NULL, action_record_event },
    { TEST_STATE_INIT,    TEST_EVT_CUSTOM,TEST_STATE_INIT,    0, NULL, action_record_event },
};

static const SM_Config_t test_config = {
    .states           = test_states,
    .transitions      = test_transitions,
    .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
    .initial_state    = TEST_STATE_INIT,
};

static SM_Context_t sm_ctx;
static SM_Handle_t  sm;

/* =============================================================================
 * SETUP / TEARDOWN
 * ===========================================================================*/

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();

    memset(&sm_ctx, 0, sizeof(sm_ctx));
    sm = &sm_ctx;

    bool ok = SM_Init(sm, &test_config);
    TEST_ASSERT_TRUE_MESSAGE(ok, "SM_Init failed in setUp");

    /* Clear the event log */
    last_processed_event = UINT16_MAX;
    last_processed_data = 0;
    processed_event_count = 0;
    memset(processed_event_log, 0xFF, sizeof(processed_event_log));

    /* Run one SM_Process to execute the initial on_entry (state_entered flag) */
    SM_Process(sm);
}

void tearDown(void)
{
    /* Nothing to clean up -- all static allocation */
}

/* =============================================================================
 * TEST CASES
 * ===========================================================================*/

/**
 * Test 1: SM_DeferEvent stores an event and returns true.
 */
void test_DeferEvent_stores_event_returns_true(void)
{
    bool result = SM_DeferEvent(sm, TEST_EVT_DATA, 42U);
    TEST_ASSERT_TRUE(result);
}

/**
 * Test 2: SM_DeferEvent with a full defer queue returns false.
 * SM_DEFER_QUEUE_SIZE=4, so 5th defer should fail.
 */
void test_DeferEvent_full_queue_returns_false(void)
{
    /* Fill the defer queue to capacity */
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_DATA,   1U));
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_ACK,    2U));
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_CUSTOM, 3U));
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_DATA,   4U));

    /* 5th defer should fail -- queue is full */
    bool result = SM_DeferEvent(sm, TEST_EVT_STOP, 5U);
    TEST_ASSERT_FALSE(result);
}

/**
 * Test 3: SM_RecallEvent posts to the front slot of the main event queue.
 *
 * When the main queue's front slot is empty, SM_RecallEvent places the
 * recalled event there. sm_event_dequeue checks the front slot before the
 * ring buffer, so the recalled event is processed first.
 *
 * Sequence:
 *   1. Defer ACK
 *   2. Recall ACK -> goes into front slot (empty)
 *   3. Post DATA  -> front slot occupied, goes to ring buffer
 *   4. SM_Process -> dequeues front slot (ACK) first
 *   5. SM_Process -> dequeues ring buffer (DATA) second
 */
void test_RecallEvent_posts_to_front_of_main_queue(void)
{
    /* Defer an event */
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_ACK, 0xACE));

    /* Recall FIRST -- front slot is empty, so recalled event goes there */
    bool recalled = SM_RecallEvent(sm);
    TEST_ASSERT_TRUE(recalled);

    /* THEN post a different event -- front slot is now occupied,
     * so this goes into the ring buffer */
    TEST_ASSERT_TRUE(SM_PostEvent(sm, TEST_EVT_DATA, 0xDA7A));

    /* One SM_Process drains both (v4.0). Verify delivery ORDER via the
     * log: recalled ACK first, then the posted DATA. */
    SM_Process(sm);
    TEST_ASSERT_EQUAL_UINT8(2U, processed_event_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_ACK,  processed_event_log[0]);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, processed_event_log[1]);
    TEST_ASSERT_EQUAL_UINT32(0xDA7A, last_processed_data);
}

/**
 * Test 3b (v4.0): Recall with an OCCUPIED front slot inserts at the true
 * front -- the recalled event is processed next, the displaced front event
 * immediately after, then the rest of the backlog.
 *
 * v3.0 defect: this path appended the recalled event to the BACK of the
 * ring, contradicting the recall-to-front contract.
 */
void test_RecallEvent_displaces_occupied_front(void)
{
    /* Defer CUSTOM */
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_CUSTOM, 0xC0C0));

    /* Post two events: DATA -> front slot, ACK -> ring backlog */
    TEST_ASSERT_TRUE(SM_PostEvent(sm, TEST_EVT_DATA, 0xD1));
    TEST_ASSERT_TRUE(SM_PostEvent(sm, TEST_EVT_ACK,  0xA1));

    /* Recall with the front occupied */
    TEST_ASSERT_TRUE(SM_RecallEvent(sm));

    /* One SM_Process drains all three. Order must be:
     * CUSTOM (recalled, true front), DATA (displaced front), ACK (backlog) */
    SM_Process(sm);
    TEST_ASSERT_EQUAL_UINT8(3U, processed_event_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_CUSTOM, processed_event_log[0]);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA,   processed_event_log[1]);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_ACK,    processed_event_log[2]);
}

/**
 * Test 4: SM_RecallEvent when defer queue is empty returns false.
 */
void test_RecallEvent_empty_queue_returns_false(void)
{
    bool result = SM_RecallEvent(sm);
    TEST_ASSERT_FALSE(result);
}

/**
 * Test 5: Multiple defers then recall -- oldest deferred event is recalled
 *         first (FIFO from defer queue). Each recalled event is posted to
 *         the front of the main queue.
 *
 *         Defer order:  DATA(1), ACK(2), CUSTOM(3)
 *         Recall order: DATA(1), ACK(2), CUSTOM(3) (FIFO from defer queue)
 */
void test_multiple_defers_recall_order_FIFO(void)
{
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_DATA,   1U));
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_ACK,    2U));
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_CUSTOM, 3U));

    /* Recall and process one at a time to verify order */
    TEST_ASSERT_TRUE(SM_RecallEvent(sm));
    SM_Process(sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, last_processed_event);
    TEST_ASSERT_EQUAL_UINT32(1U, last_processed_data);

    TEST_ASSERT_TRUE(SM_RecallEvent(sm));
    SM_Process(sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_ACK, last_processed_event);
    TEST_ASSERT_EQUAL_UINT32(2U, last_processed_data);

    TEST_ASSERT_TRUE(SM_RecallEvent(sm));
    SM_Process(sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_CUSTOM, last_processed_event);
    TEST_ASSERT_EQUAL_UINT32(3U, last_processed_data);

    /* Queue should now be empty */
    TEST_ASSERT_FALSE(SM_RecallEvent(sm));
}

/**
 * Test 6: SM_FlushDeferred discards all deferred events.
 */
void test_FlushDeferred_discards_all(void)
{
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_DATA,   10U));
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_ACK,    20U));
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_CUSTOM, 30U));

    SM_FlushDeferred(sm);

    /* Recall should now fail -- everything was flushed */
    TEST_ASSERT_FALSE(SM_RecallEvent(sm));

    /* We should be able to defer again (queue was emptied, not destroyed) */
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_DATA, 99U));
}

/**
 * Test 7: Deferred event preserves event ID and data payload.
 */
void test_DeferEvent_preserves_event_id_and_data(void)
{
    const uint16_t event_id = TEST_EVT_CUSTOM;
    const uint32_t payload  = 0xDEADBEEF;

    TEST_ASSERT_TRUE(SM_DeferEvent(sm, event_id, payload));

    /* Recall to main queue and process */
    TEST_ASSERT_TRUE(SM_RecallEvent(sm));
    SM_Process(sm);

    /* Verify the exact event ID and data came through */
    TEST_ASSERT_EQUAL_UINT16(event_id, last_processed_event);
    TEST_ASSERT_EQUAL_UINT32(payload, last_processed_data);
}

/**
 * Test 8: Recall after flush returns false (empty).
 */
void test_RecallEvent_after_flush_returns_false(void)
{
    /* Defer some events */
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_DATA, 1U));
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_ACK,  2U));

    /* Flush everything */
    SM_FlushDeferred(sm);

    /* Recall should return false */
    bool result = SM_RecallEvent(sm);
    TEST_ASSERT_FALSE(result);
}

/**
 * Test 9: Defer queue capacity is exactly SM_DEFER_QUEUE_SIZE=4.
 *         Verify that exactly 4 events can be deferred, no more, no fewer.
 */
void test_defer_queue_capacity_is_four(void)
{
    /* 4 events should all succeed */
    for (uint32_t i = 0; i < 4U; i++) {
        bool ok = SM_DeferEvent(sm, TEST_EVT_DATA, i + 100U);
        TEST_ASSERT_TRUE_MESSAGE(ok, "Defer should succeed for slots 0-3");
    }

    /* 5th should fail */
    TEST_ASSERT_FALSE(SM_DeferEvent(sm, TEST_EVT_DATA, 200U));

    /* Recall one to free a slot */
    TEST_ASSERT_TRUE(SM_RecallEvent(sm));

    /* Now one more defer should succeed */
    TEST_ASSERT_TRUE(SM_DeferEvent(sm, TEST_EVT_ACK, 201U));

    /* And once again the queue is full */
    TEST_ASSERT_FALSE(SM_DeferEvent(sm, TEST_EVT_DATA, 202U));
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_DeferEvent_stores_event_returns_true);
    RUN_TEST(test_DeferEvent_full_queue_returns_false);
    RUN_TEST(test_RecallEvent_posts_to_front_of_main_queue);
    RUN_TEST(test_RecallEvent_displaces_occupied_front);
    RUN_TEST(test_RecallEvent_empty_queue_returns_false);
    RUN_TEST(test_multiple_defers_recall_order_FIFO);
    RUN_TEST(test_FlushDeferred_discards_all);
    RUN_TEST(test_DeferEvent_preserves_event_id_and_data);
    RUN_TEST(test_RecallEvent_after_flush_returns_false);
    RUN_TEST(test_defer_queue_capacity_is_four);

    return UNITY_END();
}
