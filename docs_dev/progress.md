# Progress: State Machine Framework v3.0 Rewrite

## Session Log

### Session 1 — 2026-04-18 (planning + Phase 0 + Phase 1 + QP/C review)

**Duration:** ~3 hours

**Phase 0 — Cleanup (complete):**
- Deleted 4 legacy v1 files (1,329 lines removed)
- Renamed config template to `sm_config_template.h`, updated 6 files
- Removed unused `COMM_PACKET_SIZE`, `COMM_RETRY_COUNT`
- Fixed duplicate `#include <unistd.h>` in example
- Committed `ed92613`, pushed

**Phase 1 — Architecture Redesign (complete):**
- Created 7 new headers: `sm_framework.h`, `sm_config.h`, `sm_types.h`, `sm_engine.h`, `sm_error.h`, `sm_debug.h`, `sm_platform.h`
- Removed 2 old headers: `sm_state_machine.h`, `sm_error_handler.h`
- Rewrote all 5 source files for v3 API
- Implemented event queue ring buffer with ISR-safe posting
- Framework is now state-agnostic (user-defined states/events)
- All 30 hardcoded state callbacks removed
- Committed `7823e69`, pushed

**QP/C Review (complete):**
- Deep review of QP/C 8.1.4 (Quantum Leaps) at `qpc-master/`
- Read all core headers and source (~6,279 lines)
- Identified 7 patterns to adopt, 4 patterns to reference-only
- Decisions D6-D11 added to plan
- Plan revised: Phase 2 expanded from 12 tasks to 29 tasks

**Decisions Made (D1-D11):**
- D1-D5: Original architecture decisions (states, events, tables, HSM, handles)
- D6: frontEvt optimization (QP/C QEQueue.frontEvt)
- D7: DIS on critical fields (QP/C QP_DIS_UPDATE/VERIFY)
- D8: Hard-bounded loops + numeric assertion IDs (QP/C Q_DEFINE_THIS_MODULE)
- D9: Time events — linked-list, arm/disarm (QP/C QTimeEvt)
- D10: Deferred events — defer/recall with LIFO recall (QP/C QActive_defer)
- D11: NOT adopting Active Objects, dynamic pools, pub-sub, states-as-functions

**Files Modified:**
- `docs_dev/task_plan.md` — Phase 2 rewritten with 29 tasks (was 12), QP/C patterns table, "Not Adopted" reference section, decisions D6-D11
- `docs_dev/findings.md` — QP/C reference review section, memory impact analysis, key file references
- `docs_dev/progress.md` — this file

**Current Phase:** Phase 2 (ready to start)
**Next Step:** Launch embedded-systems agent for Phase 2 core rewrite

**Blockers:** None.

### Session 2 — 2026-04-18 (Phase 2 + Phase 4 + Phase 5, parallel agents)

**Duration:** ~7 minutes wall clock (3 agents in parallel)

**Phase 2 — Core Rewrite (complete):**
- Created `sm_safety.h` — SM_DEFINE_MODULE, SM_REQUIRE/SM_ASSERT_ID/SM_INVARIANT, SM_DIS_UPDATE/SM_DIS_VERIFY, SM_BOUNDED_LOOP macros
- Full SM_Process rewrite (707 lines): RTC dispatch, DIS verification, frontEvt dequeue, guard condition evaluation with fallthrough, transition search (const → runtime), state timeout (fire-once), min dwell time enforcement
- frontEvt optimization: bypass ring buffer when queue empty
- Time events: SM_TimeEvt_t intrusive linked-list, Init/Arm/Disarm/Tick_ with ISR-safe critical sections, one-shot + periodic, bounded by SM_FEATURE_MAX_TIME_EVENTS
- Deferred events: SM_DeferEvent/SM_RecallEvent (LIFO to front), SM_FlushDeferred, behind SM_FEATURE_DEFER
- HSM parent-state fallback bounded by SM_HSM_MAX_DEPTH
- Statistics: watermark tracking (nMin), all counters
- SM_Platform_Assert signature changed to (module, id) for numeric assertion pattern
- Committed `4238969`, pushed

**Phase 4 — HAL Expansion (complete):**
- SM_Platform_ExitSleep — weak no-op stub
- Nested critical section tracking — nesting counter, SM_Platform_GetCriticalNesting()
- Simulation time fix — SM_Platform_SimTick() replaces auto-increment, GetTimeMs is now side-effect-free
- Compile-time platform detection — SM_PLATFORM_ARM, SM_PLATFORM_POSIX, SM_PLATFORM_SIM
- Runtime capability check — SM_PlatformCap_t enum, SM_Platform_HasCapability()
- Committed `6b7fac6` on worktree branch

**Phase 5 — Debug Rewrite (complete):**
- Runtime enable/disable per level — SM_Debug_EnableLevel/IsLevelEnabled with uint8_t bitmask
- Per-module debug tags — SM_Debug_RegisterTag/EnableTag/IsTagEnabled/PrintTagged, 16 tags max, bitmask filtering, SM_LOG_TAG macro
- Periodic interval — SM_Debug_SetPeriodicInterval/CheckPeriodic with wraparound-safe timing
- HexDump ASCII column — non-printable chars as '.', aligned columns
- Static inline no-op functions for SM_FEATURE_DEBUG=0 (avoids -Wunused-value)
- Committed `4eb4e03` on worktree branch

**Merge & Verification:**
- Phase 4 branch merged into main — clean, no conflicts (`93e965e`)
- Phase 5 branch merged into main — clean, no conflicts (`7021442`)
- Build: zero warnings, 11/11 targets (GCC 13.2.0)
- Both examples run correctly with full transition processing
- Pushed `7021442` to origin main

**Current Phase:** Phase 3 (Error Handler Rewrite) — ready to start
**Next Step:** Phase 3 implements DIS on critical_lock, bounds-checked assertions, user-dispatched recovery

**Blockers:** None.
