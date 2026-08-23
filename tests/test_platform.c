/**
 * @file test_platform.c
 * @brief Test-specific platform implementation
 *
 * Replaces sm_platform_weak.c in test builds. Provides the same HAL stubs
 * but with a longjmp-based SM_Platform_Assert so tests can verify that
 * assertions fire correctly.
 *
 * This file also provides test_sim_time_reset() to zero the sim clock
 * between tests, and the assert-capture globals used by TEST_EXPECT_ASSERT.
 */

#include "sm_framework/sm_platform.h"
#include "sm_framework/sm_types.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

/* =============================================================================
 * ASSERT CAPTURE STATE
 * ===========================================================================*/

jmp_buf test_assert_jmp_buf;
volatile bool test_assert_expecting = false;
volatile bool test_assert_fired = false;
const char *test_assert_module = NULL;
int test_assert_id = 0;

void test_assert_expect(void)
{
    test_assert_expecting = true;
    test_assert_fired = false;
    test_assert_module = NULL;
    test_assert_id = 0;
}

void test_assert_clear(void)
{
    test_assert_expecting = false;
    test_assert_fired = false;
    test_assert_module = NULL;
    test_assert_id = 0;
}

/* =============================================================================
 * TIMING (same as sm_platform_weak.c but with resettable counter)
 * ===========================================================================*/

static uint32_t sim_time_ms = 0U;

uint32_t SM_Platform_GetTimeMs(void)
{
    return sim_time_ms;
}

void SM_Platform_SimTick(void)
{
    sim_time_ms++;
}

void test_sim_time_reset(void)
{
    sim_time_ms = 0U;
}

bool SM_Platform_IsTimeout(uint32_t start, uint32_t timeout_ms)
{
    uint32_t now = SM_Platform_GetTimeMs();
    return (now - start) >= timeout_ms;
}

/* =============================================================================
 * CRITICAL SECTIONS
 * ===========================================================================*/

static uint32_t critical_nesting = 0U;

/* --- ISR interleaving harness (W2a). Limits are documented in test_common.h;
 * in particular this cannot observe a torn DIS pair -- see G16. ----------- */

static test_isr_hook_t s_isr_hook = NULL;
static bool s_isr_in_critsec = false;
static bool s_isr_running = false;      /* re-entrancy guard */
static uint32_t s_isr_fires = 0U;
static uint32_t s_isr_budget = 0xFFFFFFFFU;

void test_isr_hook_set(test_isr_hook_t h, bool fire_inside_critsec)
{
    s_isr_hook = h;
    s_isr_in_critsec = fire_inside_critsec;
    s_isr_running = false;
    s_isr_fires = 0U;
    s_isr_budget = 0xFFFFFFFFU;
}

void test_isr_hook_clear(void)
{
    s_isr_hook = NULL;
    s_isr_in_critsec = false;
    s_isr_running = false;
    s_isr_fires = 0U;
    s_isr_budget = 0xFFFFFFFFU;
}

uint32_t test_isr_hook_fire_count(void)
{
    return s_isr_fires;
}

void test_isr_hook_limit(uint32_t n)
{
    s_isr_budget = n;
}

static void isr_fire(int phase)
{
    test_isr_hook_t h = s_isr_hook;

    if (h == NULL || s_isr_running || s_isr_budget == 0U) {
        return;
    }
    /* nesting > 0 is the NMI / second-core model, not the documented one. */
    if (phase == SM_TEST_ISR_IN_CRITSEC && !s_isr_in_critsec) {
        return;
    }
    s_isr_running = true;
    s_isr_fires++;
    if (s_isr_budget != 0xFFFFFFFFU) {
        s_isr_budget--;
    }
    h(phase, critical_nesting);
    s_isr_running = false;
}

void SM_Platform_EnterCritical(void)
{
    /* An interrupt can land right up to the instruction that masks them. */
    if (critical_nesting == 0U) {
        isr_fire(SM_TEST_ISR_PRE_ENTER);
    } else {
        isr_fire(SM_TEST_ISR_IN_CRITSEC);
    }
    critical_nesting++;
}

void SM_Platform_ExitCritical(void)
{
    if (critical_nesting > 0U) {
        critical_nesting--;
    }
    /* ...and again from the instruction that unmasks them onward. */
    if (critical_nesting == 0U) {
        isr_fire(SM_TEST_ISR_POST_EXIT);
    } else {
        isr_fire(SM_TEST_ISR_IN_CRITSEC);
    }
}

uint32_t SM_Platform_GetCriticalNesting(void)
{
    return critical_nesting;
}

/* =============================================================================
 * OUTPUT
 * ===========================================================================*/

bool SM_Platform_OutputInit(uint8_t interface)
{
    (void)interface;
    return true;
}

uint32_t SM_Platform_OutputSend(const uint8_t *data, uint32_t len)
{
    /* Suppress debug output during tests to keep output clean */
    (void)data;
    return len;
}

/* =============================================================================
 * WATCHDOG (no-op stubs)
 * ===========================================================================*/

void SM_Platform_WatchdogKick(void) { }
void SM_Platform_WatchdogStart(uint32_t timeout_ms) { (void)timeout_ms; }
void SM_Platform_WatchdogStop(void) { }

/* =============================================================================
 * SLEEP (no-op stubs)
 * ===========================================================================*/

void SM_Platform_EnterSleep(SM_SleepMode_t mode) { (void)mode; }
void SM_Platform_ExitSleep(void) { }

/* =============================================================================
 * NVS (no-op stubs)
 * ===========================================================================*/

bool SM_Platform_NVS_Write(uint16_t key, const void *data, uint16_t len)
{
    (void)key; (void)data; (void)len;
    return false;
}

bool SM_Platform_NVS_Read(uint16_t key, void *data, uint16_t len)
{
    (void)key; (void)data; (void)len;
    return false;
}

/* =============================================================================
 * RESET REASON
 * ===========================================================================*/

SM_ResetReason_t SM_Platform_GetResetReason(void)
{
    return SM_RESET_POR;
}

/* =============================================================================
 * CAPABILITIES
 * ===========================================================================*/

bool SM_Platform_HasCapability(SM_PlatformCap_t cap)
{
    switch (cap) {
    case SM_CAP_OUTPUT:  return true;
    default:             return false;
    }
}

/* =============================================================================
 * ASSERTIONS -- longjmp-based for test capture
 * ===========================================================================*/

void SM_Platform_Assert(const char *module, int id)
{
    test_assert_module = module;
    test_assert_id = id;
    test_assert_fired = true;

    if (test_assert_expecting) {
        test_assert_expecting = false;
        longjmp(test_assert_jmp_buf, 1);
    }

    /* Unexpected assertion in tests -- print and abort */
    fprintf(stderr, "\n*** UNEXPECTED ASSERTION: %s:%d ***\n",
            module ? module : "(null)", id);
    fflush(stderr);

    /* Don't loop forever in tests -- abort so the test runner can continue */
    /* Use exit instead of abort to avoid core dumps */
    exit(1);
}

/* =============================================================================
 * LIBRARY-SIDE ABI PROBES (v4.2, W1)
 *
 * Compiled with the LIBRARY's configuration, not the test executable's.
 * See test_common.h and tests/test_abi_guard.c.
 * ===========================================================================*/

uint32_t test_lib_sizeof_context(void)
{
    return (uint32_t)sizeof(SM_Context_t);
}

uint32_t test_lib_abi_fingerprint(void)
{
    return (uint32_t)SM_ABI_FINGERPRINT;
}

uint32_t test_lib_event_queue_size(void)
{
    return (uint32_t)SM_EVENT_QUEUE_SIZE;
}
