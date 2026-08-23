/**
 * @file sm_platform_weak.c
 * @brief Default (weak) platform implementations for v3.0
 * @version 4.1.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Provides default weak implementations of all HAL functions.
 * Users override by providing strong definitions in their platform file.
 *
 * Fixes from v2:
 *   - SM_Platform_IsTimeout is now weak (overridable)
 *   - Simulation time uses a manually-ticked counter; GetTimeMs reads it
 *     without side-effects so IsTimeout does not cause a double-increment
 *   - SM_Platform_SimTick() advances the counter for unit-test harnesses
 *   - All new HAL functions (watchdog, sleep, NVS, reset reason) have stubs
 *   - Generalized output (SM_Platform_OutputInit/OutputSend) replaces
 *     per-protocol init/send
 *   - Critical sections track nesting depth
 *   - SM_Platform_ExitSleep() for post-wake peripheral restore
 *   - SM_Platform_HasCapability() for runtime capability queries
 */

#include "sm_framework/sm_platform.h"
#include <stdio.h>

/* =============================================================================
 * TIMING
 *
 * Simulation strategy:
 *   sim_time_ms is a static counter that ONLY advances via SM_Platform_SimTick().
 *   SM_Platform_GetTimeMs() is a pure read -- no side-effects.  This avoids
 *   the v2 bug where IsTimeout calling GetTimeMs would double-increment the
 *   counter, producing unpredictable timeout behavior.
 *
 *   In unit tests, call SM_Platform_SimTick() in your test loop to advance
 *   time deterministically.  On real platforms, override GetTimeMs with a
 *   hardware timer (HAL_GetTick, esp_timer_get_time, etc.).
 * ===========================================================================*/

/** Simulation millisecond counter -- advanced only by SM_Platform_SimTick(). */
static uint32_t sim_time_ms = 0U;

SM_WEAK uint32_t SM_Platform_GetTimeMs(void)
{
    return sim_time_ms;
}

SM_WEAK void SM_Platform_SimTick(void)
{
    sim_time_ms++;
}

SM_WEAK bool SM_Platform_IsTimeout(uint32_t start, uint32_t timeout_ms)
{
    uint32_t now = SM_Platform_GetTimeMs();

    /* Unsigned subtraction handles 32-bit wraparound correctly */
    return (now - start) >= timeout_ms;
}

/* =============================================================================
 * CRITICAL SECTIONS
 *
 * Nesting-aware implementation:
 *   - EnterCritical increments depth; on real HW, disable IRQs on 0->1.
 *   - ExitCritical decrements depth; on real HW, re-enable IRQs on 1->0.
 *   - GetCriticalNesting returns current depth (0 = outside critical section).
 *
 * The simulation default only tracks the counter (no actual interrupt disable).
 * On Cortex-M, replace with __disable_irq() / __enable_irq() guarded by depth.
 * ===========================================================================*/

/** Nesting counter for critical sections.  Must NOT be modified outside
 *  EnterCritical / ExitCritical. */
static uint32_t critical_nesting = 0U;

SM_WEAK void SM_Platform_EnterCritical(void)
{
    /*
     * On real hardware:
     *   if (critical_nesting == 0U) { __disable_irq(); }
     */
    critical_nesting++;
}

SM_WEAK void SM_Platform_ExitCritical(void)
{
    if (critical_nesting > 0U) {
        critical_nesting--;
    }
    /*
     * On real hardware:
     *   if (critical_nesting == 0U) { __enable_irq(); }
     */
}

SM_WEAK uint32_t SM_Platform_GetCriticalNesting(void)
{
    return critical_nesting;
}

/* =============================================================================
 * OUTPUT (generalized)
 * ===========================================================================*/

SM_WEAK bool SM_Platform_OutputInit(uint8_t interface)
{
    (void)interface;
    return true;  /* Simulation: always succeeds */
}

SM_WEAK uint32_t SM_Platform_OutputSend(const uint8_t *data, uint32_t len)
{
    /* Default: print to stdout for simulation/development */
    if (data != NULL && len > 0U) {
        for (uint32_t i = 0U; i < len; i++) {
            putchar((int)data[i]);
        }
        fflush(stdout);
    }
    return len;
}

/* =============================================================================
 * WATCHDOG
 * ===========================================================================*/

SM_WEAK void SM_Platform_WatchdogKick(void)
{
    /* No-op in simulation */
}

SM_WEAK void SM_Platform_WatchdogStart(uint32_t timeout_ms)
{
    (void)timeout_ms;
    /* No-op in simulation */
}

SM_WEAK void SM_Platform_WatchdogStop(void)
{
    /* No-op in simulation */
}

/* =============================================================================
 * SLEEP MODES
 * ===========================================================================*/

SM_WEAK void SM_Platform_EnterSleep(SM_SleepMode_t mode)
{
    (void)mode;
    /* No-op in simulation */
}

SM_WEAK void SM_Platform_ExitSleep(void)
{
    /* No-op in simulation.
     * Real implementations restore peripheral clocks, re-init comms, etc. */
}

/* =============================================================================
 * NON-VOLATILE STORAGE
 * ===========================================================================*/

SM_WEAK bool SM_Platform_NVS_Write(uint16_t key, const void *data, uint16_t len)
{
    (void)key;
    (void)data;
    (void)len;
    return false;  /* Not available in simulation */
}

SM_WEAK bool SM_Platform_NVS_Read(uint16_t key, void *data, uint16_t len)
{
    (void)key;
    (void)data;
    (void)len;
    return false;  /* Not available in simulation */
}

/* =============================================================================
 * RESET REASON
 * ===========================================================================*/

SM_WEAK SM_ResetReason_t SM_Platform_GetResetReason(void)
{
    return SM_RESET_POR;  /* Simulation: always power-on reset */
}

/* =============================================================================
 * PLATFORM CAPABILITY CHECK
 * ===========================================================================*/

SM_WEAK bool SM_Platform_HasCapability(SM_PlatformCap_t cap)
{
    /*
     * Simulation defaults:
     *   - Watchdog, NVS, Sleep: not available (stubs are no-ops)
     *   - Output: available (prints to stdout)
     *
     * Real platform implementations override to return true for
     * capabilities they actually provide.
     */
    switch (cap) {
    case SM_CAP_OUTPUT:
        return true;
    case SM_CAP_WATCHDOG: /* fall through */
    case SM_CAP_NVS:      /* fall through */
    case SM_CAP_SLEEP:    /* fall through */
    case SM_CAP_COUNT:    /* fall through */
    default:
        return false;
    }
}

/* =============================================================================
 * ASSERTIONS
 * ===========================================================================*/

SM_WEAK void SM_Platform_Assert(const char *module, int id)
{
    printf("\n*** SM ASSERTION FAILED ***\n");
    printf("Module: %s\n", module != NULL ? module : "(null)");
    printf("ID:     %d\n", id);
    printf("System halted.\n");
    fflush(stdout);

    /* Infinite loop -- on real hardware, could trigger BKPT or reset */
    while (1) {
        /* __asm("BKPT #0"); for ARM Cortex-M */
    }
}
