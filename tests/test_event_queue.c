/**
 * @file test_event_queue.c
 * @brief Unity tests for the ISR-safe event queue (frontEvt + ring buffer)
 *
 * Covers:
 *   1. Post single event -> depth = 1
 *   2. First post hits frontEvt slot (front_valid, ring count = 0)
 *   3. Second post overflows front to ring buffer
 *   4. Fill queue to capacity (8 ring + front) -> SM_EventQueueIsFull
 *   5. Post to full queue returns false (event dropped)
 *   6. SM_EventQueueIsEmpty on fresh init
 *   7. SM_EventQueueDepth at various fill levels
 *   8. SM_EventQueueFlush resets to empty
 *   9. SM_EventQueueGetMin (nMin watermark)
 *  10. SM_Process dequeues front first, then ring FIFO (delivery order)
 *
 * Compile defs (from tests/CMakeLists.txt):
 *   SM_EVENT_QUEUE_SIZE=8, SM_STATE_COUNT=4, SM_EVENT_COUNT=8
 */

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

/* =============================================================================
 * EVENT DELIVERY RECORDER
 *
 * A transition action callback that logs (event, data) pairs in the order
 * SM_Process dispatches them.  Used by test 10 to verify front-first + FIFO.
 * ===========================================================================*/

#define MAX_RECORDED_EVENTS  16

static struct {
    uint16_t event;
    uint32_t data;
} s_recorded[MAX_RECORDED_EVENTS];

static uint8_t s_record_count = 0U;

static void record_action(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    if (s_record_count < MAX_RECORDED_EVENTS) {
        s_recorded[s_record_count].event = event;
        s_recorded[s_record_count].data  = data;
        s_record_count++;
    }
}

static void record_clear(void)
{
    memset(s_recorded, 0, sizeof(s_recorded));
    s_record_count = 0U;
}

/* =============================================================================
 * MINIMAL 3-STATE MACHINE CONFIGURATION
 *
 * States:  INIT(0) -> RUNNING(1) -> STOPPED(2)
 * Events:  TEST_EVT_START triggers INIT->RUNNING
 *          TEST_EVT_STOP  triggers RUNNING->STOPPED
 *          TEST_EVT_DATA  triggers self-loop on RUNNING (exercises action)
 *
 * All transitions use record_action so delivery order is observable.
 * State callbacks are NULL -- only transition actions matter for these tests.
 * ===========================================================================*/

static const SM_StateDesc_t s_states[SM_STATE_COUNT] = {
    /* [TEST_STATE_INIT]    */ { NULL, NULL, NULL, 0U, 0U },
    /* [TEST_STATE_RUNNING] */ { NULL, NULL, NULL, 0U, 0U },
    /* [TEST_STATE_STOPPED] */ { NULL, NULL, NULL, 0U, 0U },
    /* [TEST_STATE_ERROR]   */ { NULL, NULL, NULL, 0U, 0U },
};

static const SM_Transition_t s_transitions[] = {
    /* INIT  --START--> RUNNING */
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0U, NULL, record_action },
    /* RUNNING --STOP--> STOPPED */
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0U, NULL, record_action },
    /* RUNNING --DATA--> RUNNING  (self-loop to exercise action with payload) */
    { TEST_STATE_RUNNING, TEST_EVT_DATA,  TEST_STATE_RUNNING, 0U, NULL, record_action },
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
    record_clear();

    /* Fresh init for every test */
    bool ok = SM_Init(s_sm, &s_config);
    TEST_ASSERT_TRUE_MESSAGE(ok, "SM_Init must succeed in setUp");
}

void tearDown(void)
{
    /* nothing to clean up -- static allocation */
}

/* =============================================================================
 * TEST 1 -- Post single event, depth = 1
 * ===========================================================================*/

void test_post_single_event_depth_is_one(void)
{
    bool ok = SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(1U, SM_EventQueueDepth(s_sm));
}

/* =============================================================================
 * TEST 2 -- First post hits frontEvt slot (front_valid, ring count = 0)
 * ===========================================================================*/

void test_first_post_uses_front_slot(void)
{
    bool ok = SM_PostEvent(s_sm, TEST_EVT_START, 42U);
    TEST_ASSERT_TRUE(ok);

    /* front_valid should be set, ring count should still be 0 */
    TEST_ASSERT_TRUE(s_ctx.event_queue.front_valid);
    TEST_ASSERT_EQUAL_UINT8(0U, s_ctx.event_queue.count);

    /* Verify the front slot carries the correct event + payload */
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_START, s_ctx.event_queue.front.event);
    TEST_ASSERT_EQUAL_UINT32(42U, s_ctx.event_queue.front.data);
}

/* =============================================================================
 * TEST 3 -- Second post overflows front to ring buffer
 * ===========================================================================*/

void test_second_post_enters_ring_buffer(void)
{
    SM_PostEvent(s_sm, TEST_EVT_START, 1U);  /* -> front */
    SM_PostEvent(s_sm, TEST_EVT_STOP, 2U);   /* -> ring */

    TEST_ASSERT_TRUE(s_ctx.event_queue.front_valid);
    TEST_ASSERT_EQUAL_UINT8(1U, s_ctx.event_queue.count);
    TEST_ASSERT_EQUAL_UINT8(2U, SM_EventQueueDepth(s_sm));  /* front + ring */
}

/* =============================================================================
 * TEST 4 -- Fill queue to capacity (8 ring + 1 front = 9 total)
 * ===========================================================================*/

void test_fill_to_capacity_reports_full(void)
{
    /* Total capacity = SM_EVENT_QUEUE_SIZE (ring) + 1 (front) = 9 */
    const uint8_t total_capacity = SM_EVENT_QUEUE_SIZE + 1U;

    for (uint8_t i = 0U; i < total_capacity; i++) {
        bool ok = SM_PostEvent(s_sm, (uint16_t)(i % SM_EVENT_COUNT), i);
        TEST_ASSERT_TRUE_MESSAGE(ok, "SM_PostEvent should succeed while not full");
    }

    TEST_ASSERT_TRUE(SM_EventQueueIsFull(s_sm));
    TEST_ASSERT_EQUAL_UINT8(total_capacity, SM_EventQueueDepth(s_sm));
}

/* =============================================================================
 * TEST 5 -- Post to full queue returns false (no crash, event dropped)
 * ===========================================================================*/

void test_post_to_full_queue_returns_false(void)
{
    const uint8_t total_capacity = SM_EVENT_QUEUE_SIZE + 1U;

    /* Fill the queue */
    for (uint8_t i = 0U; i < total_capacity; i++) {
        SM_PostEvent(s_sm, (uint16_t)(i % SM_EVENT_COUNT), i);
    }

    TEST_ASSERT_TRUE(SM_EventQueueIsFull(s_sm));

    /* This must not crash -- just return false */
    bool ok = SM_PostEvent(s_sm, TEST_EVT_ACK, 0xDEADU);
    TEST_ASSERT_FALSE(ok);

    /* Depth must not have changed */
    TEST_ASSERT_EQUAL_UINT8(total_capacity, SM_EventQueueDepth(s_sm));
}

/* =============================================================================
 * TEST 6 -- SM_EventQueueIsEmpty on fresh init
 * ===========================================================================*/

void test_queue_empty_after_init(void)
{
    /* setUp already called SM_Init -- queue should be empty */
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(s_sm));
    TEST_ASSERT_EQUAL_UINT8(0U, SM_EventQueueDepth(s_sm));
    TEST_ASSERT_FALSE(SM_EventQueueIsFull(s_sm));
}

/* =============================================================================
 * TEST 7 -- SM_EventQueueDepth at various fill levels
 * ===========================================================================*/

void test_queue_depth_at_various_levels(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, SM_EventQueueDepth(s_sm));

    /* 1 event -> front only */
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, SM_EventQueueDepth(s_sm));

    /* 2 events -> front + 1 ring */
    SM_PostEvent(s_sm, TEST_EVT_DATA, 0U);
    TEST_ASSERT_EQUAL_UINT8(2U, SM_EventQueueDepth(s_sm));

    /* 3 events */
    SM_PostEvent(s_sm, TEST_EVT_STOP, 0U);
    TEST_ASSERT_EQUAL_UINT8(3U, SM_EventQueueDepth(s_sm));

    /* Fill to half the ring (front + 4 ring = 5) */
    SM_PostEvent(s_sm, TEST_EVT_ACK, 0U);
    SM_PostEvent(s_sm, TEST_EVT_CUSTOM, 0U);
    TEST_ASSERT_EQUAL_UINT8(5U, SM_EventQueueDepth(s_sm));

    /* Fill the rest to capacity (front + 8 ring = 9) */
    SM_PostEvent(s_sm, TEST_EVT_RESET, 0U);
    SM_PostEvent(s_sm, TEST_EVT_ERROR, 0U);
    SM_PostEvent(s_sm, TEST_EVT_TIMEOUT, 0U);
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_EQUAL_UINT8(9U, SM_EventQueueDepth(s_sm));
}

/* =============================================================================
 * TEST 8 -- SM_EventQueueFlush resets to empty
 * ===========================================================================*/

void test_flush_resets_to_empty(void)
{
    /* Post several events */
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    SM_PostEvent(s_sm, TEST_EVT_STOP, 0U);
    SM_PostEvent(s_sm, TEST_EVT_DATA, 0U);

    TEST_ASSERT_EQUAL_UINT8(3U, SM_EventQueueDepth(s_sm));
    TEST_ASSERT_FALSE(SM_EventQueueIsEmpty(s_sm));

    SM_EventQueueFlush(s_sm);

    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(s_sm));
    TEST_ASSERT_EQUAL_UINT8(0U, SM_EventQueueDepth(s_sm));
    TEST_ASSERT_FALSE(SM_EventQueueIsFull(s_sm));
    TEST_ASSERT_FALSE(s_ctx.event_queue.front_valid);
    TEST_ASSERT_EQUAL_UINT8(0U, s_ctx.event_queue.count);
}

/* =============================================================================
 * TEST 9 -- SM_EventQueueGetMin (nMin watermark)
 *
 * nMin starts at SM_EVENT_QUEUE_SIZE (8) after SM_Init.  It tracks the
 * minimum number of free ring+front slots ever observed.  Each post
 * recalculates: free = SM_EVENT_QUEUE_SIZE - used, where used includes
 * the front slot.
 *
 * After posting N events (N <= capacity):
 *   used = N, free = SM_EVENT_QUEUE_SIZE - N  (clamped to 0 when N > SIZE)
 *   nMin = min(previous_nMin, free)
 *
 * Note: nMin is NOT reset by SM_EventQueueFlush -- it is a lifetime watermark.
 * ===========================================================================*/

void test_nmin_watermark_tracks_peak_usage(void)
{
    /* Fresh after init, nMin should be SM_EVENT_QUEUE_SIZE (8) */
    TEST_ASSERT_EQUAL_UINT8(SM_EVENT_QUEUE_SIZE, SM_EventQueueGetMin(s_sm));

    /* Post 1 event: used=1, free = 8-1 = 7 */
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_EQUAL_UINT8(7U, SM_EventQueueGetMin(s_sm));

    /* Post 4 more: used=5, free = 8-5 = 3 */
    SM_PostEvent(s_sm, TEST_EVT_DATA, 0U);
    SM_PostEvent(s_sm, TEST_EVT_STOP, 0U);
    SM_PostEvent(s_sm, TEST_EVT_ACK, 0U);
    SM_PostEvent(s_sm, TEST_EVT_CUSTOM, 0U);
    TEST_ASSERT_EQUAL_UINT8(3U, SM_EventQueueGetMin(s_sm));

    /* Flush and verify nMin did NOT reset (lifetime watermark) */
    SM_EventQueueFlush(s_sm);
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(s_sm));
    TEST_ASSERT_EQUAL_UINT8(3U, SM_EventQueueGetMin(s_sm));

    /* Fill to capacity: used=9, free = 8-8 = 0 (clamped) */
    for (uint8_t i = 0U; i < SM_EVENT_QUEUE_SIZE + 1U; i++) {
        SM_PostEvent(s_sm, (uint16_t)(i % SM_EVENT_COUNT), i);
    }
    TEST_ASSERT_EQUAL_UINT8(0U, SM_EventQueueGetMin(s_sm));

    /* Even after flush, nMin stays at 0 */
    SM_EventQueueFlush(s_sm);
    TEST_ASSERT_EQUAL_UINT8(0U, SM_EventQueueGetMin(s_sm));
}

/* =============================================================================
 * TEST 10 -- SM_Process dequeues front first, then ring FIFO
 *
 * Strategy:
 *   1. Move SM to RUNNING state (post START, process)
 *   2. Clear recorder
 *   3. Post 3 DATA events with distinct payloads (100, 200, 300)
 *      - Event 1 (data=100) lands in front slot
 *      - Event 2 (data=200) goes into ring[0]
 *      - Event 3 (data=300) goes into ring[1]
 *   4. Call SM_Process three times (RTC: one event per call)
 *   5. Verify recorded order is 100, 200, 300 (front first, then FIFO)
 *
 * The DATA event has a self-loop transition on RUNNING, so it stays in
 * RUNNING and the action fires every time.
 * ===========================================================================*/

void test_process_dequeues_front_then_fifo(void)
{
    /* --- Step 1: move from INIT to RUNNING --- */
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    SM_Process(s_sm);   /* on_entry of INIT (NULL), dequeue START, action fires, enter RUNNING */
    SM_Process(s_sm);   /* on_entry of RUNNING fires (NULL), then on_execute (NULL) */

    /* Confirm we are in RUNNING */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(s_sm));

    /* --- Step 2: clear recorder (ignore the START transition) --- */
    record_clear();

    /* --- Step 3: post 3 DATA events with distinct payloads --- */
    SM_PostEvent(s_sm, TEST_EVT_DATA, 100U);   /* -> front */
    SM_PostEvent(s_sm, TEST_EVT_DATA, 200U);   /* -> ring[0] */
    SM_PostEvent(s_sm, TEST_EVT_DATA, 300U);   /* -> ring[1] */

    TEST_ASSERT_EQUAL_UINT8(3U, SM_EventQueueDepth(s_sm));

    /* --- Step 4: process all 3 events. v4.0 drains up to
     * SM_MAX_EVENTS_PER_PROCESS per call, so the first call handles all
     * three; the extra calls verify nothing spurious follows. --- */
    SM_Process(s_sm);
    SM_Process(s_sm);
    SM_Process(s_sm);

    /* --- Step 5: verify delivery order --- */
    TEST_ASSERT_EQUAL_UINT8(3U, s_record_count);

    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, s_recorded[0].event);
    TEST_ASSERT_EQUAL_UINT32(100U, s_recorded[0].data);

    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, s_recorded[1].event);
    TEST_ASSERT_EQUAL_UINT32(200U, s_recorded[1].data);

    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, s_recorded[2].event);
    TEST_ASSERT_EQUAL_UINT32(300U, s_recorded[2].data);

    /* Queue should be empty now */
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(s_sm));
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_post_single_event_depth_is_one);
    RUN_TEST(test_first_post_uses_front_slot);
    RUN_TEST(test_second_post_enters_ring_buffer);
    RUN_TEST(test_fill_to_capacity_reports_full);
    RUN_TEST(test_post_to_full_queue_returns_false);
    RUN_TEST(test_queue_empty_after_init);
    RUN_TEST(test_queue_depth_at_various_levels);
    RUN_TEST(test_flush_resets_to_empty);
    RUN_TEST(test_nmin_watermark_tracks_peak_usage);
    RUN_TEST(test_process_dequeues_front_then_fifo);

    return UNITY_END();
}
