/**
 * @file sm_safety.h
 * @brief Safety macros: DIS, bounded loops, numeric assertions (v3.0)
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Implements:
 *   - SM_DEFINE_MODULE / SM_REQUIRE / SM_ASSERT_ID / SM_INVARIANT
 *     Numeric assertion pattern (module name + integer ID) inspired by QP/C.
 *     Replaces __FILE__/__LINE__ with compact IDs for flash-constrained targets.
 *
 *   - SM_DIS_UPDATE / SM_DIS_VERIFY
 *     Duplicate Inverse Storage: stores bitwise inverse of critical fields
 *     so corruption is detected at runtime.
 *
 *   - SM_BOUNDED_LOOP_BEGIN / SM_BOUNDED_LOOP_END
 *     Hard-bounded for-loop; postcondition verifies the counter never ran past
 *     the declared bound (covers both normal exhaustion and early break).
 *
 * All macros depend on SM_FEATURE_ASSERT -- when it is 0 every macro
 * compiles to ((void)0) or an empty body, leaving zero overhead.
 */

#ifndef SM_SAFETY_H
#define SM_SAFETY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sm_config.h"
#include "sm_platform.h"

/* =============================================================================
 * MODULE-LEVEL NUMERIC ASSERTIONS
 *
 * Usage:
 *   SM_DEFINE_MODULE("sm_engine")           // once per .c file (file scope)
 *   SM_REQUIRE(100, ptr != NULL);           // assertion with numeric ID
 *   SM_ASSERT_ID(200, count < MAX);         // alias
 *   SM_INVARIANT(300, loop_cnt < bound);    // semantic alias for loop bounds
 * ===========================================================================*/

/**
 * @brief Declare the current module name (one per .c file, file scope)
 *
 * Creates a static const string used by SM_REQUIRE / SM_ASSERT_ID / SM_INVARIANT.
 */
#define SM_DEFINE_MODULE(name_) \
    static char const sm_this_module_[] = name_

#if SM_FEATURE_ASSERT

/**
 * @brief Numeric assertion -- calls SM_Platform_Assert(module, id) on failure
 *
 * @param id_   Integer assertion ID (unique within the module)
 * @param expr_ Boolean expression that must be true
 */
#define SM_REQUIRE(id_, expr_) \
    ((expr_) ? ((void)0) : SM_Platform_Assert(sm_this_module_, (id_)))

#else /* SM_FEATURE_ASSERT == 0 */

#define SM_REQUIRE(id_, expr_) ((void)0)

#endif /* SM_FEATURE_ASSERT */

/** @brief Alias for SM_REQUIRE -- semantic emphasis on assertion */
#define SM_ASSERT_ID(id_, expr_)  SM_REQUIRE(id_, expr_)

/** @brief Alias for SM_REQUIRE -- semantic emphasis on loop invariant */
#define SM_INVARIANT(id_, expr_)  SM_REQUIRE(id_, expr_)

/* =============================================================================
 * DUPLICATE INVERSE STORAGE (DIS)
 *
 * Stores the bitwise NOT of a critical variable alongside it. Before using
 * the variable, verify that (field == (type)~dis_field). Catches memory
 * corruption, single-event upsets, or accidental overwrites.
 *
 * Usage:
 *   SM_DIS_UPDATE(sm->current_state, sm->state_dis, uint16_t);
 *   SM_DIS_VERIFY(sm->current_state, sm->state_dis, uint16_t, 200);
 * ===========================================================================*/

/**
 * @brief Update a DIS shadow field to the inverse of the primary field
 *
 * @param field_    Primary field (lvalue)
 * @param dis_      DIS shadow field (lvalue, same width)
 * @param type_     Unsigned integer type (uint8_t, uint16_t, etc.)
 */
#define SM_DIS_UPDATE(field_, dis_, type_) \
    ((dis_) = (type_)(~(type_)(field_)))

/**
 * @brief Verify that a primary field matches its DIS shadow
 *
 * Fires SM_REQUIRE(id_) if the field and its inverse are inconsistent.
 *
 * @param field_  Primary field
 * @param dis_    DIS shadow field
 * @param type_   Unsigned integer type
 * @param id_     Assertion ID (used by SM_REQUIRE)
 */
#define SM_DIS_VERIFY(field_, dis_, type_, id_) \
    SM_REQUIRE((id_), (type_)(field_) == (type_)(~(type_)(dis_)))

/* =============================================================================
 * BOUNDED LOOP
 *
 * Wraps `for (; var < bound; var++)`. After the loop, verifies
 * `var <= cached_bound` so both exhaustive iteration (var == bound) and
 * early `break` (var < bound) satisfy the invariant. The for-condition itself
 * prevents more than `bound` iterations.
 *
 * Usage:
 *   SM_BOUNDED_LOOP_BEGIN(i, max_iters, 350)
 *   {
 *       // body -- optional break when done
 *   }
 *   SM_BOUNDED_LOOP_END(i, max_iters, 350)
 * ===========================================================================*/

/**
 * @brief Begin a hard-bounded loop
 *
 * @param var_    Loop counter variable name (declared by macro as uint32_t)
 * @param bound_  Maximum iterations allowed
 * @param id_     Assertion ID fired if loop exceeds bound
 */
#define SM_BOUNDED_LOOP_BEGIN(var_, bound_, id_) \
    { \
        uint32_t var_ = 0U; \
        const uint32_t var_##_bound_ = (uint32_t)(bound_); \
        for (; var_ < var_##_bound_; var_++) {

/**
 * @brief End a hard-bounded loop -- verifies counter did not exceed bound
 *
 * @param var_    Same counter name passed to SM_BOUNDED_LOOP_BEGIN
 * @param bound_  Same bound value (not re-evaluated, uses cached copy)
 * @param id_     Same assertion ID
 */
#define SM_BOUNDED_LOOP_END(var_, bound_, id_) \
        } \
        SM_INVARIANT(id_, var_ <= var_##_bound_); \
    }

#ifdef __cplusplus
}
#endif

#endif /* SM_SAFETY_H */
