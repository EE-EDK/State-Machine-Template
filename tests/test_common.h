/**
 * @file test_common.h
 * @brief Shared test infrastructure for State Machine Framework v3.0
 *
 * Provides:
 *   - Test state/event enums matching SM_STATE_COUNT=4 / SM_EVENT_COUNT=8
 *   - Assert-capture macros (setjmp/longjmp) for verifying SM_REQUIRE fires
 *   - Simulation time reset helper
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

/* Test enums -- must match compile-time SM_STATE_COUNT=4, SM_EVENT_COUNT=8 */
enum TestStates {
    TEST_STATE_INIT    = 0,
    TEST_STATE_RUNNING = 1,
    TEST_STATE_STOPPED = 2,
    TEST_STATE_ERROR   = 3
};

enum TestEvents {
    TEST_EVT_START   = 0,
    TEST_EVT_STOP    = 1,
    TEST_EVT_RESET   = 2,
    TEST_EVT_ERROR   = 3,
    TEST_EVT_TIMEOUT = 4,
    TEST_EVT_DATA    = 5,
    TEST_EVT_ACK     = 6,
    TEST_EVT_CUSTOM  = 7
};

/* =============================================================================
 * ASSERT CAPTURE INFRASTRUCTURE
 *
 * SM_Platform_Assert normally loops forever. In test_platform.c, when
 * test_assert_expecting is true, it longjmps instead, so the test can
 * verify that an assertion fired with the correct module + ID.
 * ===========================================================================*/

extern jmp_buf test_assert_jmp_buf;
extern volatile bool test_assert_expecting;
extern volatile bool test_assert_fired;
extern const char *test_assert_module;
extern int test_assert_id;

/** Tell the assert handler to longjmp on next assertion instead of halting. */
void test_assert_expect(void);

/** Clear assert-capture state. Called in setUp(). */
void test_assert_clear(void);

/** Reset simulation time counter to 0. */
void test_sim_time_reset(void);

/* =============================================================================
 * ISR INTERLEAVING HARNESS (v4.2, W2a)
 *
 * WHAT THIS IS
 *   A hook on the critical-section boundary. Between an SM_* call and the next
 *   one, the harness runs a caller-supplied "ISR" so tests can check what a
 *   real interrupt would observe, and what it would do to shared state.
 *
 * WHAT THIS IS NOT -- read before trusting a green run
 *   1. It is not preemption at arbitrary instruction boundaries. It fires only
 *      at instrumented points. Code between two boundaries is atomic as far as
 *      this harness is concerned, whether or not it is atomic in reality.
 *   2. It says nothing about multicore or about memory ordering. One thread,
 *      one core, no reordering, no cache.
 *   3. It cannot observe a torn DIS pair. Two adjacent non-critical stores have
 *      no boundary between them, so there is no seam to fire in -- against the
 *      pre-fix engine this harness reports "no tear" on code that has one.
 *      That property is structural and is checked by graphify's G16 instead.
 *      Do not cite a green run here as evidence about DIS atomicity.
 *
 *   It is a large improvement over having nothing. It is not a proof.
 *
 * SINGLE-CORE MODEL
 *   On one core an interrupt cannot run inside a critical section -- that is
 *   what the critical section is for. So the hook fires by default only at
 *   nesting == 0: immediately before interrupts are masked, and immediately
 *   after they are unmasked. Firing while nesting > 0 models an NMI or a
 *   second core (review finding 1.13), which is a different contract from the
 *   one the framework documents; it is available, but opt-in and labelled.
 * ===========================================================================*/

/** Seam at which the injected ISR ran. */
#define SM_TEST_ISR_PRE_ENTER   (0)  /* just before interrupts are masked */
#define SM_TEST_ISR_POST_EXIT   (1)  /* just after interrupts are unmasked */
#define SM_TEST_ISR_IN_CRITSEC  (2)  /* opt-in: NMI / second-core model only */

typedef void (*test_isr_hook_t)(int phase, uint32_t nesting);

/**
 * @brief Install an injected ISR.
 * @param h                   Callback, or NULL to disable.
 * @param fire_inside_critsec When true, also fire at nesting > 0. This models
 *                            an NMI or another core, NOT the documented
 *                            single-core contract.
 *
 * Re-entrancy is guarded: an injected ISR that itself takes a critical section
 * will not re-enter the hook.
 */
void test_isr_hook_set(test_isr_hook_t h, bool fire_inside_critsec);

/** Remove the hook and zero its counter. Called from setUp(). */
void test_isr_hook_clear(void);

/** How many times the hook has fired since the last clear. */
uint32_t test_isr_hook_fire_count(void);

/** Fire the hook at most `n` more times, then stop. 0 disarms it. */
void test_isr_hook_limit(uint32_t n);

/* =============================================================================
 * LIBRARY-SIDE ABI PROBES (v4.2, W1)
 *
 * test_platform.c is compiled into sm_framework_test, so these report the
 * values the LIBRARY was built with. A test executable's own translation unit
 * reports its own. Comparing the two is how test_abi_guard demonstrates a real
 * application/library divergence instead of asserting a hardcoded constant.
 * ===========================================================================*/

/** sizeof(SM_Context_t) as the linked library sees it. */
uint32_t test_lib_sizeof_context(void);

/** SM_ABI_FINGERPRINT as the linked library computed it. */
uint32_t test_lib_abi_fingerprint(void);

/** SM_EVENT_QUEUE_SIZE the linked library was compiled with. */
uint32_t test_lib_event_queue_size(void);

/**
 * @brief Verify that a code block fires an assertion.
 *
 * Usage:
 *   TEST_EXPECT_ASSERT(SM_Init(NULL, NULL));
 *   TEST_ASSERT_EQUAL_STRING("sm_engine", test_assert_module);
 *   TEST_ASSERT_EQUAL_INT(100, test_assert_id);
 */
#define TEST_EXPECT_ASSERT(code_) \
    do { \
        test_assert_expect(); \
        if (setjmp(test_assert_jmp_buf) == 0) { \
            code_; \
            TEST_FAIL_MESSAGE("Expected assertion did not fire"); \
        } \
        /* If we get here via longjmp, assertion was captured */ \
    } while (0)

/**
 * @brief Verify that a code block does NOT fire an assertion.
 */
#define TEST_ASSERT_NO_ASSERT(code_) \
    do { \
        test_assert_expect(); \
        if (setjmp(test_assert_jmp_buf) == 0) { \
            code_; \
            /* Good -- no assertion fired */ \
            test_assert_clear(); \
        } else { \
            char msg[128]; \
            snprintf(msg, sizeof(msg), \
                     "Unexpected assertion: %s:%d", \
                     test_assert_module ? test_assert_module : "?", \
                     test_assert_id); \
            TEST_FAIL_MESSAGE(msg); \
        } \
    } while (0)

#endif /* TEST_COMMON_H */
