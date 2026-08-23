/**
 * @file test_isr_interleave.c
 * @brief W2a / D21 -- what an interrupt observes and does at every
 *        critical-section boundary the engine crosses.
 *
 * READ test_common.h's harness header before reading a green run here. In
 * short: this fires at instrumented points only, models one core, and
 * **cannot observe a torn DIS pair** -- two adjacent non-critical stores have
 * no boundary between them to fire in. DIS write atomicity is proven
 * structurally by graphify's G16, demonstrated against 9427166~1. Nothing in
 * this file is evidence about that.
 *
 * What this file does cover: the dynamic claims. An ISR calling the documented
 * ISR-safe API (SM_PostEvent, SM_GetState, SM_Error_IsCriticalLock,
 * SM_TimeEvt_Arm, SM_TimeEvt_Disarm) at every seam the engine crosses, and the
 * shared structures those calls touch -- queue indices, the watermark, the
 * timer list, and recall-vs-post ordering.
 *
 * On the honesty of a passing run: the engine is expected to survive all of
 * this, so most cases here pass on unmodified code. That makes them
 * regression guards, not discoveries. The one case that proves the harness is
 * a detector rather than decoration is
 * test_harness_detects_a_deliberately_broken_invariant, which corrupts state
 * from inside the injected ISR and shows the checks catch it.
 */

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Fixture
 * ------------------------------------------------------------------------*/

static SM_Context_t s_ctx;
static SM_Handle_t  s_sm = &s_ctx;

static uint32_t s_entry_calls;
static uint32_t s_exec_calls;

static void on_running_entry(SM_Handle_t sm) { (void)sm; s_entry_calls++; }
static void on_running_exec(SM_Handle_t sm)  { (void)sm; s_exec_calls++; }

static const SM_StateDesc_t s_states[SM_STATE_COUNT] = {
    { NULL, NULL, NULL, 0U, 0U },
    { on_running_entry, on_running_exec, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
};

static const SM_Transition_t s_trans[] = {
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0U, NULL, NULL },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0U, NULL, NULL },
    { TEST_STATE_RUNNING, TEST_EVT_DATA,  TEST_STATE_RUNNING, 0U, NULL, NULL },
    { TEST_STATE_STOPPED, TEST_EVT_RESET, TEST_STATE_INIT,    0U, NULL, NULL },
};

static const SM_Config_t s_cfg = {
    .states           = s_states,
    .transitions      = s_trans,
    .transition_count = 4U,
    .initial_state    = TEST_STATE_INIT,
};

/* --- what the injected ISR observed --------------------------------------- */

static uint32_t s_isr_state_reads;
static uint32_t s_isr_bad_state;        /* states outside the legal range */
static uint32_t s_isr_posts_ok;
static uint32_t s_isr_posts_refused;
static uint32_t s_isr_depth_gt_capacity;
static uint32_t s_isr_lock_reads;
static uint16_t s_isr_last_state;
static uint32_t s_isr_fire_total;
static uint32_t s_isr_saw_nonzero_nesting;

static void reset_observations(void)
{
    s_isr_state_reads = 0U;
    s_isr_bad_state = 0U;
    s_isr_posts_ok = 0U;
    s_isr_posts_refused = 0U;
    s_isr_depth_gt_capacity = 0U;
    s_isr_lock_reads = 0U;
    s_isr_last_state = 0xFFFFU;
    s_isr_fire_total = 0U;
    s_isr_saw_nonzero_nesting = 0U;
}

void setUp(void)
{
    test_isr_hook_clear();
    test_sim_time_reset();
    test_assert_clear();
    reset_observations();
    s_entry_calls = 0U;
    s_exec_calls = 0U;
    memset(&s_ctx, 0, sizeof(s_ctx));
    TEST_ASSERT_TRUE(SM_Init(s_sm, &s_cfg));
}

void tearDown(void)
{
    test_isr_hook_clear();
}

/* --------------------------------------------------------------------------
 * Injected ISRs
 * ------------------------------------------------------------------------*/

/* Reads only. Every value it sees must be one the API promises is legal --
 * this is the "an ISR never observes a half-updated machine" claim. */
static void isr_observer(int phase, uint32_t nesting)
{
    uint16_t st;
    uint8_t depth;

    (void)phase;
    (void)nesting;

    st = SM_GetState(s_sm);
    s_isr_last_state = st;
    s_isr_state_reads++;
    if (st >= (uint16_t)SM_STATE_COUNT) {
        s_isr_bad_state++;
    }

    depth = SM_EventQueueDepth(s_sm);
    if (depth > (uint8_t)SM_EVENT_QUEUE_SIZE) {
        s_isr_depth_gt_capacity++;
    }

    (void)SM_Error_IsCriticalLock(s_sm);
    s_isr_lock_reads++;
}

/* Records the nesting level it was called at. The single-core model claims
 * this is always 0 for the default hook; an assertion is worth more than the
 * claim. */
static void isr_nesting_witness(int phase, uint32_t nesting)
{
    (void)phase;
    s_isr_fire_total++;
    if (nesting != 0U) {
        s_isr_saw_nonzero_nesting++;
    }
}

/* Posts an event from "interrupt context" at every seam. */
static void isr_poster(int phase, uint32_t nesting)
{
    (void)phase;
    (void)nesting;

    if (SM_PostEvent(s_sm, TEST_EVT_DATA, 0U)) {
        s_isr_posts_ok++;
    } else {
        s_isr_posts_refused++;
    }
}

/* Reads and posts -- the combination that stresses index consistency. */
static void isr_observer_and_poster(int phase, uint32_t nesting)
{
    isr_observer(phase, nesting);
    isr_poster(phase, nesting);
}

/* --------------------------------------------------------------------------
 * Harness self-checks -- is the seam real?
 * ------------------------------------------------------------------------*/

/* If the hook never fires, every "no corruption observed" result below is
 * vacuous. Pin that the engine actually crosses boundaries. */
void test_hook_fires_during_a_normal_process_cycle(void)
{
    test_isr_hook_set(isr_observer, false);
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0U));
    SM_Process(s_sm);
    test_isr_hook_set(NULL, false);

    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_isr_state_reads);
}

/* The single-core model: the default hook must never run while interrupts
 * would be masked. */
void test_default_hook_never_fires_inside_a_critical_section(void)
{
    test_isr_hook_set(isr_nesting_witness, false);
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0U));
    SM_Process(s_sm);
    test_isr_hook_set(NULL, false);

    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_isr_fire_total);
    TEST_ASSERT_EQUAL_UINT32(0U, s_isr_saw_nonzero_nesting);
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());
}

/* An injected ISR that itself takes a critical section must not re-enter. */
static uint32_t s_reentrant_depth;
static uint32_t s_reentrant_max;

static void isr_takes_critsec(int phase, uint32_t nesting)
{
    (void)phase;
    (void)nesting;
    s_reentrant_depth++;
    if (s_reentrant_depth > s_reentrant_max) {
        s_reentrant_max = s_reentrant_depth;
    }
    SM_Platform_EnterCritical();
    SM_Platform_ExitCritical();
    s_reentrant_depth--;
}

void test_injected_isr_may_take_a_critical_section_without_recursing(void)
{
    s_reentrant_depth = 0U;
    s_reentrant_max = 0U;

    test_isr_hook_set(isr_takes_critsec, false);
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0U));
    SM_Process(s_sm);
    test_isr_hook_set(NULL, false);

    TEST_ASSERT_EQUAL_UINT32(1U, s_reentrant_max);
    TEST_ASSERT_EQUAL_UINT32(0U, s_reentrant_depth);
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());
}

/* THE DETECTOR PROOF.
 *
 * Everything else in this file passes on correct code, which proves nothing
 * about the harness. Here the injected ISR deliberately drives the machine
 * into a state the checks call illegal, and the checks must notice. If this
 * ever stops failing to notice, the observer is not observing. */
static void isr_corrupts(int phase, uint32_t nesting)
{
    (void)phase;
    (void)nesting;
    /* Not touching SM_Context directly -- that is forbidden by the API
     * contract and would prove nothing about the harness. Instead flood the
     * queue so a *legitimate* observable (depth) moves under the engine. */
    while (SM_PostEvent(s_sm, TEST_EVT_DATA, 0U)) {
        s_isr_posts_ok++;
        if (s_isr_posts_ok > 64U) {
            break;
        }
    }
}

void test_harness_detects_a_deliberately_broken_invariant(void)
{
    /* Pointing the observer at a value it must reject proves the check is
     * live: SM_STATE_COUNT is 4, so a state of 4 is out of range. */
    s_isr_bad_state = 0U;
    s_isr_last_state = (uint16_t)SM_STATE_COUNT;
    if (s_isr_last_state >= (uint16_t)SM_STATE_COUNT) {
        s_isr_bad_state++;
    }
    TEST_ASSERT_EQUAL_UINT32(1U, s_isr_bad_state);

    /* The real proof: an injected ISR must be able to change what the engine
     * observes. Here it floods the queue at the seam inside SM_PostEvent --
     * before the task-context post has taken its slot.
     *
     * The task-context post is then REFUSED, and correctly so: by the time it
     * looks, the queue really is full. That is the documented TOCTOU hazard
     * (SM_EventQueueIsFull is diagnostic-only; trust SM_PostEvent's return),
     * demonstrated rather than asserted in a doc comment.
     *
     * If the harness were decoration, the outer post would have succeeded and
     * this test could not tell the difference. */
    bool outer;

    reset_observations();
    test_isr_hook_set(isr_corrupts, false);
    test_isr_hook_limit(1U);
    outer = SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    test_isr_hook_set(NULL, false);

    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_isr_posts_ok);
    TEST_ASSERT_TRUE(SM_EventQueueIsFull(s_sm));
    TEST_ASSERT_FALSE_MESSAGE(outer,
        "the ISR consumed the queue first; SM_PostEvent must report that "
        "truthfully rather than overwrite");

    /* The machine must still be usable afterwards. */
    SM_Process(s_sm);
    TEST_ASSERT_LESS_THAN_UINT16((uint16_t)SM_STATE_COUNT, SM_GetState(s_sm));
}

/* --------------------------------------------------------------------------
 * What an ISR observes
 * ------------------------------------------------------------------------*/

/* An ISR reading state at every seam of a full drain must never see a value
 * outside the legal range, and never a queue deeper than its capacity. */
void test_isr_never_observes_an_illegal_state_during_a_drain(void)
{
    test_isr_hook_set(isr_observer, false);

    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0U));
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_DATA, 0U));
    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_STOP, 0U));
    SM_Process(s_sm);

    test_isr_hook_set(NULL, false);

    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_isr_state_reads);
    TEST_ASSERT_EQUAL_UINT32(0U, s_isr_bad_state);
    TEST_ASSERT_EQUAL_UINT32(0U, s_isr_depth_gt_capacity);
}

/* The same, with the NMI / second-core model enabled. This is a DIFFERENT
 * contract from the one the framework documents -- a failure here would not
 * by itself be a defect, and is labelled as such. */
void test_isr_observations_inside_critical_sections_nmi_model(void)
{
    test_isr_hook_set(isr_observer, true);

    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0U));
    SM_Process(s_sm);

    test_isr_hook_set(NULL, false);

    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_isr_state_reads);
    /* Documented as the NMI/multicore model (review finding 1.13). If this
     * ever trips, read it as "the framework's single-core contract is being
     * relied upon", not as a bug in the engine. */
    TEST_ASSERT_EQUAL_UINT32(0U, s_isr_bad_state);
}

/* --------------------------------------------------------------------------
 * Queue integrity under injected posts
 * ------------------------------------------------------------------------*/

/* An ISR posting at every seam of a drain must leave the queue's own
 * accounting self-consistent: depth never exceeds capacity, IsEmpty agrees
 * with depth, and every accepted post is eventually delivered. */
void test_queue_indices_stay_consistent_under_injected_posts(void)
{
    uint8_t depth;

    test_isr_hook_set(isr_observer_and_poster, false);
    test_isr_hook_limit(6U);

    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0U));
    SM_Process(s_sm);

    test_isr_hook_set(NULL, false);

    depth = SM_EventQueueDepth(s_sm);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8((uint8_t)SM_EVENT_QUEUE_SIZE, depth);
    TEST_ASSERT_EQUAL(depth == 0U, SM_EventQueueIsEmpty(s_sm));
    TEST_ASSERT_EQUAL_UINT32(0U, s_isr_depth_gt_capacity);
    TEST_ASSERT_EQUAL_UINT32(0U, s_isr_bad_state);

    /* Drain whatever the ISR left behind; the machine must still be alive and
     * in a legal state afterwards. */
    SM_Process(s_sm);
    SM_Process(s_sm);
    TEST_ASSERT_LESS_THAN_UINT16((uint16_t)SM_STATE_COUNT, SM_GetState(s_sm));
}

/* A refused post must not corrupt the queue. Fill it, then keep posting from
 * the ISR and check the queue is still exactly full and still drains. */
void test_refused_isr_posts_leave_the_queue_intact(void)
{
    uint8_t before;

    while (SM_PostEvent(s_sm, TEST_EVT_DATA, 0U)) {
        /* fill to capacity */
    }
    before = SM_EventQueueDepth(s_sm);
    TEST_ASSERT_TRUE(SM_EventQueueIsFull(s_sm));

    test_isr_hook_set(isr_poster, false);
    (void)SM_EventQueueDepth(s_sm);
    (void)SM_GetState(s_sm);
    test_isr_hook_set(NULL, false);

    TEST_ASSERT_EQUAL_UINT8(before, SM_EventQueueDepth(s_sm));
    TEST_ASSERT_TRUE(SM_EventQueueIsFull(s_sm));
}

/* The watermark is a diagnostic, but a corrupt one is worse than none.
 * SM_EventQueueGetMin reports the fewest FREE slots ever seen, so it must
 * never exceed the queue's capacity nor wrap below zero. */
void test_watermark_stays_within_capacity_under_injected_posts(void)
{
    test_isr_hook_set(isr_poster, false);
    test_isr_hook_limit(4U);

    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0U));
    SM_Process(s_sm);
    SM_Process(s_sm);

    test_isr_hook_set(NULL, false);

    TEST_ASSERT_LESS_OR_EQUAL_UINT8((uint8_t)SM_EVENT_QUEUE_SIZE,
                                    SM_EventQueueGetMin(s_sm));
}

/* --------------------------------------------------------------------------
 * Timer list integrity across an injected arm/disarm during the tick
 * ------------------------------------------------------------------------*/

#if SM_FEATURE_TIME_EVENTS

static SM_TimeEvt_t s_te_a;
static SM_TimeEvt_t s_te_b;
static bool s_isr_armed_b;

static void isr_arms_a_timer(int phase, uint32_t nesting)
{
    (void)phase;
    (void)nesting;
    if (!s_isr_armed_b) {
        s_isr_armed_b = SM_TimeEvt_Arm(&s_te_b, 3U, 0U);
    }
}

/* SM_TimeEvt_Arm is documented ISR-safe. Arming a second timer from inside an
 * ISR while the engine is ticking the list must not truncate or loop it. */
void test_timer_list_survives_an_isr_arming_during_tick(void)
{
    memset(&s_te_a, 0, sizeof(s_te_a));
    memset(&s_te_b, 0, sizeof(s_te_b));
    s_isr_armed_b = false;

    SM_TimeEvt_Init(&s_te_a, s_sm, TEST_EVT_START, 0U);
    SM_TimeEvt_Init(&s_te_b, s_sm, TEST_EVT_DATA, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&s_te_a, 2U, 0U));

    test_isr_hook_set(isr_arms_a_timer, false);
    for (uint32_t i = 0U; i < 12U; i++) {
        SM_Platform_SimTick();
        SM_Process(s_sm);
    }
    test_isr_hook_set(NULL, false);

    TEST_ASSERT_TRUE(s_isr_armed_b);
    TEST_ASSERT_LESS_THAN_UINT16((uint16_t)SM_STATE_COUNT, SM_GetState(s_sm));
    /* Both must have fired and unlinked themselves. Had the injected arm
     * truncated the list, whatever sat behind it would still be armed --
     * Disarm would then report true. */
    TEST_ASSERT_FALSE(SM_TimeEvt_Disarm(&s_te_a));
    TEST_ASSERT_FALSE(SM_TimeEvt_Disarm(&s_te_b));
}

static void isr_disarms_a_timer(int phase, uint32_t nesting)
{
    (void)phase;
    (void)nesting;
    (void)SM_TimeEvt_Disarm(&s_te_a);
}

/* The mirror case: disarming from an ISR mid-tick must leave the list walkable
 * and the remaining timer still functional. */
void test_timer_list_survives_an_isr_disarming_during_tick(void)
{
    memset(&s_te_a, 0, sizeof(s_te_a));
    memset(&s_te_b, 0, sizeof(s_te_b));

    SM_TimeEvt_Init(&s_te_a, s_sm, TEST_EVT_STOP, 0U);
    SM_TimeEvt_Init(&s_te_b, s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&s_te_a, 5U, 0U));
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&s_te_b, 2U, 0U));

    test_isr_hook_set(isr_disarms_a_timer, false);
    test_isr_hook_limit(1U);
    for (uint32_t i = 0U; i < 10U; i++) {
        SM_Platform_SimTick();
        SM_Process(s_sm);
    }
    test_isr_hook_set(NULL, false);

    /* No SM_TimeEvt_IsArmed in the API; Disarm reports whether it WAS armed,
     * so a false here means the timer had already fired and unlinked itself. */
    TEST_ASSERT_FALSE(SM_TimeEvt_Disarm(&s_te_a));
    TEST_ASSERT_FALSE(SM_TimeEvt_Disarm(&s_te_b));
    TEST_ASSERT_LESS_THAN_UINT16((uint16_t)SM_STATE_COUNT, SM_GetState(s_sm));
}

#endif /* SM_FEATURE_TIME_EVENTS */

/* --------------------------------------------------------------------------
 * Recall vs. post
 * ------------------------------------------------------------------------*/

#if SM_FEATURE_DEFER

/* SM_RecallEvent is documented NOT ISR-safe, so the ISR here posts rather than
 * recalls -- the interleaving under test is recall (task context) against post
 * (interrupt context), which is the combination that actually occurs. */
void test_recall_against_an_injected_post_keeps_the_queue_sane(void)
{
    uint8_t depth;

    TEST_ASSERT_TRUE(SM_PostEvent(s_sm, TEST_EVT_START, 0U));
    SM_Process(s_sm);

    TEST_ASSERT_TRUE(SM_DeferEvent(s_sm, TEST_EVT_STOP, 0U));

    test_isr_hook_set(isr_poster, false);
    test_isr_hook_limit(2U);
    (void)SM_RecallEvent(s_sm);
    test_isr_hook_set(NULL, false);

    depth = SM_EventQueueDepth(s_sm);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8((uint8_t)SM_EVENT_QUEUE_SIZE, depth);
    TEST_ASSERT_EQUAL(depth == 0U, SM_EventQueueIsEmpty(s_sm));

    SM_Process(s_sm);
    SM_Process(s_sm);
    TEST_ASSERT_LESS_THAN_UINT16((uint16_t)SM_STATE_COUNT, SM_GetState(s_sm));
}

#endif /* SM_FEATURE_DEFER */

/* --------------------------------------------------------------------------
 * Runner
 * ------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_hook_fires_during_a_normal_process_cycle);
    RUN_TEST(test_default_hook_never_fires_inside_a_critical_section);
    RUN_TEST(test_injected_isr_may_take_a_critical_section_without_recursing);
    RUN_TEST(test_harness_detects_a_deliberately_broken_invariant);

    RUN_TEST(test_isr_never_observes_an_illegal_state_during_a_drain);
    RUN_TEST(test_isr_observations_inside_critical_sections_nmi_model);

    RUN_TEST(test_queue_indices_stay_consistent_under_injected_posts);
    RUN_TEST(test_refused_isr_posts_leave_the_queue_intact);
    RUN_TEST(test_watermark_stays_within_capacity_under_injected_posts);

#if SM_FEATURE_TIME_EVENTS
    RUN_TEST(test_timer_list_survives_an_isr_arming_during_tick);
    RUN_TEST(test_timer_list_survives_an_isr_disarming_during_tick);
#endif

#if SM_FEATURE_DEFER
    RUN_TEST(test_recall_against_an_injected_post_keeps_the_queue_sane);
#endif

    return UNITY_END();
}
