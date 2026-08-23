# Review Findings — 2026-08-22 (flaws, gaps, ideas — NOT a plan)

**Purpose.** Raw material for the next planning session: every flaw, risk,
gap and idea found while reviewing v4.0.0 as a candidate "perfect" embedded
state machine. Nothing here is prioritised or scheduled; that is the next
step, deliberately not taken in this document.

**How it was produced.** graphify was rebuilt as a typed knowledge graph
(v2: scoped call resolution, macro variants, struct-field read/write edges,
critical-section spans, doc-comment ISR contracts, machine↔code bindings,
model round-trip, docs cross-reference, graph metrics, G1–G15 validators,
`graphify-out/graph.html` viewer) and its output was used as the map for a
full read of every header, `sm_engine.c`, `sm_error.c`, `sm_debug.c`,
`sm_platform_weak.c`, the tests, examples, smgen, models and plans. Two
claims were then reproduced by linking test programs against the built
library; struct sizes and flash were measured with `arm-none-eabi-gcc 14.3`
for Cortex-M4 at `-Os`.

**What did not happen.** The planned 14-lens Opus/Sonnet review fleet
(find → adversarial verify → cluster) was rejected at launch by the account's
monthly spend limit. Its script is saved
(`.claude/projects/…/workflows/scripts/smf-perfect-review-wf_c3683e9d-143.js`,
resumable via `resumeFromRunId: wf_c3683e9d-143`). Everything below is
therefore a **single-reviewer** list with explicit evidence tags — treat
`LIKELY` items as hypotheses for the fleet or a human to confirm.

**Evidence tags.** `VERIFIED:run` reproduced by executing code ·
`VERIFIED:code` established by reading the cited lines ·
`VERIFIED:graph` reported by a graphify G/V-check whose logic is unit-tested ·
`LIKELY` reasoned from code, not exercised · `IDEA` improvement proposal.

---

## 0. What the framework already does well (for fairness)

- Zero heap, zero globals in the engine, static allocation only — confirmed
  by the graph: no `malloc` callees anywhere; all state behind `SM_Handle_t`.
- Strict FIFO event delivery for all sources with an empty-queue fast path,
  bounded drain (`SM_MAX_EVENTS_PER_PROCESS`), atomic exit→action→entry,
  deadline-based drift-free timers with stall coalescing (v4 semantic fixes).
- The graph has **zero call cycles** (0 SCCs on 935 edges incl. callback
  re-entry): re-entry from callbacks only ever goes through `SM_PostEvent`
  → queue → next drain. That is the right shape for a run-to-completion engine.
- `SM_Context.current_state` has exactly three writers (`SM_Init`,
  `SM_Reset`, `sm_execute_transition`) — now pinned by `test_graphify_unit`.
- Honest documentation of known races (Disarm vs Tick phase-2, queue query
  TOCTOU, no re-entrant `SM_Process`).
- 226 Unity cases + 39 smgen + 29 graphify cases, all green (ctest 22/22).
- The three committed models round-trip against the extracted C machines
  (`MATCH` ×3) — the Phase B3 property already holds for hand-written tables.

---

## 1. Engine semantics & concurrency

**1.1 — Prebuilt library bakes `SM_STATE_COUNT=4U / SM_EVENT_COUNT=8U`; every application that differs is broken in one of three ways.** `VERIFIED:run` · critical
- Location: `CMakeLists.txt:98-101` (PRIVATE defs on `sm_framework`),
  `sm_types.h:118` (`SM_EVT_TIMEOUT = SM_EVENT_COUNT`, evaluated per
  translation unit), `sm_engine.c:364` (REQUIRE 104), `:177-184`
  (`sm_get_state_desc` range check), `:621` (`SM_PostEvent` range check).
- Reproduced against `cmake-build-review/libsm_framework.a`:
  - app with 6 states → `SM_Init(initial_state=5)` hits **assertion 104**
    and the weak `SM_Platform_Assert` spins forever;
  - app with 2 events and a `{S0, SM_EVT_TIMEOUT, S1}` row + `timeout_ms=10`
    → after 50 ms the state is still S0: the engine posts event **8**, the
    table expects **2** — the route is dead;
  - `SM_PostEvent(7)` is **accepted** by a 2-event app; so is event 2, i.e.
    the app can forge what it believes is the engine-only timeout signal.
- Consequence: `examples/basic_example.c:150-156`'s "failsafe" timeout route
  can never fire in the shipped build (the comment says "never fires here";
  it is "never fires at all"). graphify G15 now reports ERROR for it and
  WARN for the other five examples. `examples/CMakeLists.txt:8-10` claims
  only the stats-array layout is affected — wrong.
- Root cause is architectural (see 4.1): counts are global compile-time
  constants consumed by both the library TU and every app TU.

**1.2 — Field + DIS-shadow written as two separate stores outside any critical section while the readers are documented ISR-safe and DIS-verify.** `VERIFIED:code` · high
- `sm_engine.c:317-318` (`current_state` then `state_dis`) vs `SM_GetState`
  (`:697`, "ISR-SAFE", `SM_DIS_VERIFY … 250`).
- `sm_error.c:96-99` (`critical_lock` then `critical_lock_dis`) vs
  `SM_Error_IsCriticalLock` (`:145`, "ISR-safe", verify 710).
- `SM_Reset` `:597-598` same pattern.
- An ISR calling the ISR-safe reader between the two stores fails the DIS
  check → `SM_Platform_Assert` → system halt on a perfectly healthy machine.
  The graph's G14 list (volatile writes outside critsec) is exactly this set.

**1.3 — Armed time events survive `SM_Reset`.** `VERIFIED:code` · high
- `SM_Reset` (`sm_engine.c:558-607`) flushes the queue and deferred queue
  and clears errors but never touches `time_evt_head`; `SM_Init` sets it to
  NULL only via `memset`. Timers armed by the old state's `on_entry` keep
  firing into the freshly reset machine (the old state's `on_exit` runs and
  may disarm — but only if the app wrote it that way).

**1.4 — `SM_TimeEvt_Init` on an armed timer truncates the owner's timer list.** `VERIFIED:code` · medium
- `sm_engine.c:849` sets `te->next = NULL` and `armed = false` without
  unlinking; every timer after it in the intrusive list is silently lost.
  Same for re-`Init`ing a timer onto a different instance.

**1.5 — Self-transitions always run exit/entry; there is no internal transition.** `VERIFIED:code` · medium (semantics)
- `sm_execute_transition` treats `to_state == from_state` like any other
  transition. `blinky_example` therefore disarms and re-arms (`Init`+`Arm`,
  deadline = now+5) its periodic timer on **every** tick, so the advertised
  drift-free phase grid is never exercised and each period = 5 ms +
  processing latency. UML's internal transition (handle event, no
  exit/entry) is missing.

**1.6 — `on_execute` semantics break under multi-event drain.** `LIKELY` · medium
- `on_execute` runs once per `SM_Process` for the state current at call
  entry (`:481-486`); with `SM_MAX_EVENTS_PER_PROCESS = 8` a state entered
  and left within one call never gets an `on_execute`; a callback posting on
  every entry consumes the whole budget each call (bounded livelock,
  documented as "intentional RTC chaining").

**1.7 — Inconsistent fail policy for bad tables.** `VERIFIED:code` · medium
- Missing descriptor → `SM_REQUIRE(202)` (halts with asserts on, silently
  returns with asserts off, queue keeps filling); `to_state >= SM_STATE_COUNT`
  → WARN and event consumed (`:548-552`); bad `initial_state` → assert +
  `return false`. Three different policies for the same class of defect.

**1.8 — Engine reads the clock 3+ times per `SM_Process`.** `VERIFIED:code` · low
- Timeout check, `SM_TimeEvt_Tick_`, per-event dwell check and
  `sm_execute_transition` each call `SM_Platform_GetTimeMs()`; "now" is not
  consistent within one call and each call is a HAL function call. `IDEA`:
  one snapshot per `SM_Process` passed down.

**1.9 — `SM_DeferEvent` does not validate the event id.** `VERIFIED:code` · low
- `sm_engine.c:1052-1077` accepts any `uint16_t`, so a deferred
  `SM_EVT_TIMEOUT` or out-of-range id is recalled into the main queue
  bypassing `SM_PostEvent`'s check.

**1.10 — Timeout-post retry floods the log.** `VERIFIED:code` · low
- `:501-503` logs WARN every cycle while the queue stays full.

**1.11 — `SM_Init` on a live context orphans timers / `initialized` is not DIS-checked on the post path.** `LIKELY` · low
- `memset` wipes `time_evt_head` leaving nodes `armed=true`; `SM_PostEvent`
  trusts `sm->initialized` without the shadow (a garbage context with a
  true-looking byte passes).

**1.12 — Watermark and fullness disagree about the front slot.** `VERIFIED:code` · low
- `sm_queue_update_watermark` counts the front slot as used (`:70-74`),
  `SM_EventQueueIsFull` ignores it (`:640`); "capacity" is SIZE or SIZE+1
  depending on which API you ask.

**1.13 — Multicore is not covered by the concurrency model.** `LIKELY` · medium
- All instances share one process-global critical section
  (`sm_platform_weak.c:79-104`, IRQ-disable semantics); on RP2040/RP2354 or
  ESP32-S3 dual-core (both present in this workspace: Plug-n-Play-Server,
  LED-Matrix core-1 driver) an IRQ mask on one core does not exclude the
  other core's `SM_PostEvent`. No per-instance lock, no memory barriers.

**1.14 — Guard evaluation order and side effects are contractual but undocumented in the header.** `LIKELY` · low
- Guards are called during the table scan (`:213-223`), possibly several
  per event; a guard with side effects runs even when a later row wins.

**1.15 — HSM is a half-feature.** `VERIFIED:graph` · medium
- `SM_FEATURE_HSM` only adds parent fallback in `sm_resolve_transition`
  (`:238-268`): no LCA exit/entry chains, no initial substates, no history.
  The region is never compiled under test (G10: `SM_FEATURE_HSM=0U` in
  `tests/CMakeLists.txt`) and no example uses it.

---

## 2. Time events & deferred events

**2.1 — Periodic tick silently lost when the queue is full.** `VERIFIED:code` · medium
- `SM_TimeEvt_Tick_` phase 2 (`:1033-1041`) drops the fire with a WARN; the
  timer's deadline has already advanced, so the period is gone. `IDEA`: a
  sticky "pending" flag re-posted next cycle, or post-before-advance.

**2.2 — `fired[SM_FEATURE_MAX_TIME_EVENTS]` is a fixed 128 B stack array per `SM_Process`.** `VERIFIED:code` · low
- `:979-983`. Fine at 16; undocumented stack cost, scales with the config.

**2.3 — `SM_TimeEvt_Arm` is ISR-safe but logs with `vsnprintf`.** `VERIFIED:code` · medium
- `:911-914` `SM_LOG_WARN` → `SM_Debug_Print` → `vsnprintf` + 384 B of stack
  inside an interrupt when capacity is reached. Same class: any ISR-safe
  path that can reach `SM_LOG_*`.

**2.4 — Re-arming an armed timer silently changes its phase; arming is O(n).** `VERIFIED:code` · low
- `:878-907` scans the list to find membership (bounded by 16) on every arm.

**2.5 — Disarm-vs-Tick race is documented, not solved.** `VERIFIED:code` · info
- `:796-798`. A "cancelled but already collected" fire is delivered. QP/C
  has the same contract; a perfect engine could drop collected fires whose
  `armed` flag cleared before phase 2.

**2.6 — Deferred queue: volatile fields without protection, no ISR contract on `SM_FlushDeferred`.** `VERIFIED:code` · low
- Defer/recall are documented NOT ISR-safe but the fields are `volatile`
  (same `SM_EventQueue_t` type) — cost without benefit; `SM_FlushDeferred`
  has no contract at all.

**2.7 — `SM_RecallEvent` logic verified correct** for the three cases
(front free / front occupied + ring space / full) — `VERIFIED:code`, no
finding; listed so the next reviewer does not re-derive it.

**2.8 — `IDEA`s from the state of the art:** tick-rate-independent
deadlines already exist; missing are multiple tick rates / 64-bit monotonic
option, an `SM_NextDeadline()` query for tickless idle, timer ordering
(sorted list or heap) so `Tick_` is O(1) when nothing is due, and a
"sticky" fire semantics option.

---

## 3. Error handler & safety machinery

**3.1 — `critical_lock` is a one-way trap with no acknowledge API.** `VERIFIED:code` · medium
- `SM_Reset` refuses while locked (`:568-574`), `SM_Error_Clear` never
  clears it (`sm_error.c:117-135`); only `SM_Init` (wipes history) or a
  hardware reset escapes. Documented, but there is no "acknowledge critical
  and re-init safely" path that preserves the error record.

**3.2 — MINOR tier "auto-recovery" is not implemented.** `VERIFIED:code` · medium
- `minor_active` / `minor_timestamp` are written (`:84-86`) and never read
  anywhere (graph: 0 readers). The three-tier model is effectively two-tier.

**3.3 — Recovery allows `SM_ERROR_MAX_RECOVERY − 1` callback attempts.** `VERIFIED:code` · low (documented as implementation detail in `tests/test_error.c:479-487`)
- `:206-213` increments before comparing. The header's wording ("Maximum
  recovery attempts before escalation") reads as 3 attempts.

**3.4 — "Assert then also return" makes behaviour config-dependent and the return path untested.** `VERIFIED:code` · medium
- e.g. `SM_Init :350-368`, `SM_TimeEvt_Arm :861-870`. With
  `SM_FEATURE_ASSERT=1` the return is dead; with `0` the function silently
  fails. Only the `=1` configuration is ever compiled under test (G10).
  The contorted `SM_REQUIRE(311, (te == NULL) || (te->sm != NULL))` exists
  only to keep both paths alive.

**3.5 — The engine never uses `SM_BOUNDED_LOOP_*`; its `SM_INVARIANT`s restate the loop condition.** `VERIFIED:graph` · medium
- Macro users: only `tests/test_safety.c`. `SM_INVARIANT(210, i < count)`
  inside `for (i < count)` (`:213-214`), 230, 260, 330, 331 likewise — they
  can never fire and give no D8 protection. The QP/C pattern bounds a
  *separate* counter.

**3.6 — DIS covers three fields; the dangerous ones are unshadowed.** `VERIFIED:graph` · medium
- Shadowed: `current_state`, `initialized`, `critical_lock`. Unshadowed:
  `config` (pointer to the tables), `recovery_cb`/`error_cb` (function
  pointers), `time_evt_head` (pointer walked in a critical section), queue
  indices (used as array indices). A corrupted `config` or `head` is the
  memory-safety event DIS exists for.

**3.7 — No integrity on the const tables, no control-flow monitoring, no stack monitoring.** `IDEA` · safety profile
- CRC over `SM_Config_t` tables verified at init / periodically; redundant
  state encoding beyond bitwise-inverse; stack high-water hook; watchdog
  kick inside `SM_Process` with a "machine is alive" criterion.

**3.8 — Weak `SM_Platform_Assert` prints then spins with interrupts possibly disabled.** `VERIFIED:code` · low
- Called from inside critical sections (INVARIANT 313/321/330). With no
  reset hook the watchdog is the only exit; on the test platform the
  `longjmp` escape leaves `critical_nesting` unbalanced for later tests.

**3.9 — `sm_debug.c` has no module and no assertions.** `VERIFIED:graph` G3 · low
- The `SM_DEFINE_MODULE("sm_debug")` line is a comment (`sm_debug.c:21`).

**3.10 — Assertion IDs are unique per module (G1 clean) but the 100-block convention is informal.** `VERIFIED:graph` · info
- `SM_GetState` uses 250 and `SM_GetStateHistory` 260 under the "200-299
  SM_Process" block; no machine-readable ID registry; IDs are not tied to
  requirements (`IDEA`: assertion IDs as requirement IDs with a generated
  table — the graph already extracts the map).

---

## 4. API design, configuration architecture, ABI

**4.1 — Global `SM_STATE_COUNT`/`SM_EVENT_COUNT` is the root of 1.1 and of D17's waste.** `VERIFIED:code` · high (design)
- Sizes the stats array and the timeout id; forces every machine in one
  binary to share counts; makes the library non-reusable as a binary.
  `IDEA`s: counts in `SM_Config_t` (`state_count`, `event_count`) checked at
  `SM_Init`; `SM_EVT_TIMEOUT` as a fixed reserved id (`0xFFFF`) independent
  of counts; stats array sized by a separate `SM_MAX_STATES` or provided by
  the app; or ship as an INTERFACE/`add_subdirectory`-only library and
  refuse to build a prebuilt archive.

**4.2 — Callbacks receive no user context.** `VERIFIED:code` · medium
- `SM_StateCallback_t(SM_Handle_t)`, guards/actions likewise
  (`sm_types.h:169-190`); `SM_Context` has no `user_data`. Apps must use
  file-scope globals, which is why `multi_fsm_example` needs a second copy
  of every callback. `IDEA`: `void *user_data` in `SM_Config_t` or
  `SM_SetUserData()`.

**4.3 — The full `SM_Context` is visible; "do not touch" is convention.** `VERIFIED:code` · low
- `IDEA`: opaque handle + `SM_CONTEXT_SIZE` static assert, or a
  `SM_DECLARE_CONTEXT` macro.

**4.4 — Event payload is one `uint32_t`.** `VERIFIED:code` · low
- No pointer/typed payload (pointers do not fit on 64-bit sim hosts), no
  event pools (value-copy is the deliberate choice — note the trade-off).

**4.5 — Dead or misleading API surface.** `VERIFIED:graph` · low
- `SM_Platform_IsTimeout` is weak-overridable but the engine never calls it
  (it inlines `now - start`); `SM_TASK_PERIOD_MS` is referenced by nothing;
  `SM_Platform_SimTick` is a production header symbol; watchdog / sleep /
  NVS / reset-reason / capabilities are declared and stubbed but nothing in
  the engine uses them (9 of 67 API functions have no test caller, G7).

**4.6 — Version drift.** `VERIFIED:graph` G8 · low
- Every header carries `@version 3.0.0`, `CMakeLists.txt` `VERSION 3.0.0`,
  README/Quick-Guide titles "v3.0", CLAUDE.md "Public API headers (v3.0)",
  while `SM_FRAMEWORK_VERSION_STRING` is 4.0.0. `IDEA`: one source of truth
  and a G-check comparing `@version` tags to it.

**4.7 — No introspection for tracing.** `IDEA`
- No state/event name tables, no `SM_GetConfig`, no model hash accessor
  (D16 planned). A trace decoder (Phase C) needs these.

**4.8 — Critical-section HAL lacks a save/restore form and per-instance locks.** `VERIFIED:code` · medium
- `SM_Platform_EnterCritical(void)` cannot express PRIMASK save/restore or
  `taskENTER_CRITICAL_FROM_ISR`; see 1.13.

**4.9 — `sm_platform.h` includes `sm_config.h` at the bottom after use of feature macros.** `VERIFIED:code` · low
- `sm_platform.h:805-807` — works only because every include path goes
  through `sm_framework.h` first.

---

## 5. Debug / logging subsystem

**5.1 — Library debug level is the library's, not the app's.** `VERIFIED:code` · medium
- The prebuilt library is compiled with default `SM_DEBUG_LEVEL=4`; an app
  defining `SM_DEBUG_LEVEL 0` changes only its own TU. Engine INFO logs for
  every transition stay in. Same per-TU hazard family as 1.1.

**5.2 — 384 B stack per log call, two formatting passes, output return ignored.** `VERIFIED:code` · low
- `sm_debug.c:212-260`: `msg_buf[128]` + `out_buf[256]`, `vsnprintf` then
  `snprintf`; `SM_Platform_OutputSend` result discarded (no backpressure).

**5.3 — Process-global state and init-order coupling.** `VERIFIED:code` · low
- `SM_Debug_Init` resets the tag table (`:86-113`); tags registered before
  it are lost. Documented as process-global; multi-instance filtering
  therefore impossible.

**5.4 — Engine logs are untagged; `SM_LOG_TAG` is unused by the framework.** `VERIFIED:graph` · low

**5.5 — No binary trace.** `IDEA` · high value
- QP/C QS-style ring of (timestamp, instance, from, event, to) records with
  a host decoder keyed by the D16 model hash would replace printf logging in
  the engine entirely and is what Phase C needs.

---

## 6. Memory, performance, WCET

**6.1 — Documented RAM figures are stale/unmeasured.** `VERIFIED:run` · low
- Cortex-M4 `-Os`: `sizeof(SM_Context_t)` = **316 B** at defaults
  (`SM_EventQueue_t` 80, `SM_ErrorHandler_t` 184, `SM_TimeEvt_t` 28,
  `SM_Transition_t` 16, `SM_StateDesc_t` 20, `SM_Config_t` 12). README /
  CLAUDE.md / findings.md say ~544 B baseline, ~580 with defer. Flash:
  engine 2526 B, error 863 B, debug 954 B (+76 B BSS), platform 323 B
  (+8 B) ≈ 4.67 KB; engine alone 1878 B with `SM_FEATURE_DEBUG=0`. The
  README's "4.3 KB / 84 B BSS" is in the right range; the BSS matches.
- `IDEA`: a size report (`arm-none-eabi-size` + `sizeof` table) generated
  into the graph report so the numbers cannot drift again.

**6.2 — Error handler dominates RAM (184 of 316 B).** `VERIFIED:run` · info
- `SM_ERROR_HISTORY_SIZE=8 × 16 B`; a machine that never reports errors
  still pays for it. `IDEA`: `SM_FEATURE_ERROR_HISTORY`, or history depth 0.

**6.3 — Linear transition search, guards re-run per row.** `VERIFIED:code` · medium
- `sm_find_transition` scans the whole table per event; WCET = rows ×
  guard cost. B4 (per-state index) is planned; alternatives: sorted table +
  binary search, per-state bitmap of handled events for O(1) "no handler".

**6.4 — No idle/tickless hint.** `IDEA`
- `on_execute` runs every tick even when idle; no `SM_NextDeadline()` /
  "queue empty and no timers" query for the scheduler to sleep on (the HAL
  has `EnterSleep`, nothing drives it).

**6.5 — Stack budget of `SM_Process` is undocumented.** `LIKELY` · low
- ≈ 128 B (`fired[]`) + 384 B (a log call) + callbacks; no `-fstack-usage`
  report.

---

## 7. Tests

**7.1 — One feature configuration is ever compiled.** `VERIFIED:graph` G10 · high
- `tests/CMakeLists.txt` builds all features on except HSM. Never built:
  `SM_FEATURE_HSM=1`, `ASSERT=0`, `DEBUG=0`, `TIME_EVENTS=0`, `DEFER=0`,
  `STATISTICS=0`, `SM_MAX_EVENTS_PER_PROCESS=1`, `SM_EVENT_QUEUE_SIZE` 1 and
  64, `SM_STATE_COUNT` 255, `SM_EVENT_COUNT` 65534, C99 fallback for
  `SM_STATIC_ASSERT`. `IDEA`: ctest matrix over configurations.

**7.2 — No ISR-interleaving tests.** `IDEA` · high value
- `test_platform.c`'s `EnterCritical`/`ExitCritical` could run an injected
  "ISR" (post / GetState / IsCriticalLock) at every critical-section
  boundary; that would catch 1.2 deterministically.

**7.3 — Missing scenario tests** (each one line to describe): 32-bit
time wrap for timeout/dwell/time events; `SM_Reset` with armed timers
(1.3); `SM_TimeEvt_Init` on an armed timer (1.4); `SM_Init` on a live
context; multi-instance with shared callbacks; `to_state` out of range at
runtime; callbacks calling `SM_Reset`/`SM_Process` re-entrantly (expected
to be rejected or documented); statistics counter overflow; queue
head/tail modulo wrap across many cycles; recall into a full ring;
`SM_DeferEvent` of an invalid id (1.9); assertion-ID coverage (which of
the 43 sites a test exercises — graph has the map).

**7.4 — No golden-output tests for the examples.** `VERIFIED:code` · medium
- Phase B2's exit criterion ("byte-identical stdout") has no harness.

**7.5 — No CI, no coverage, no sanitizers, cppcheck broken.** `VERIFIED:code` · medium
- CLAUDE.md TODO items; `build/`, `build-arm/`, `build_runner/`,
  `build_wtr/` litter the tree.

**7.6 — CLAUDE.md test counts stale.** `VERIFIED:graph` G8
- `test_time_events.c` 11 → 15, `test_deferred.c` 9 → 10; "19 suites" vs
  22 ctest entries.

**7.7 — Test platform leaves `critical_nesting` unbalanced after an expected assert.** `VERIFIED:code` · low (3.8)

**7.8 — No property-based / fuzz tests.** `IDEA`
- The smgen model is a ready oracle: random event sequences against a
  Python interpreter of the model vs the C engine.

---

## 8. Documentation & examples

**8.1 — `examples/CMakeLists.txt:8-10` understates the ABI problem** (1.1). `VERIFIED:run`
**8.2 — `basic_example.c:150-156` claims a failsafe that cannot fire.** `VERIFIED:run`
**8.3 — Version drift** (4.6).
**8.4 — Undocumented API** `VERIFIED:graph` G8: `SM_GetStats`,
`SM_ResetStats`, `SM_Debug_IsLevelEnabled`, `SM_Debug_IsTagEnabled`,
`SM_Platform_NVS_Read`, `SM_Platform_WatchdogStop`, `SM_Platform_ExitSleep`,
`App_Main_GetVersion` absent from README/Quick-Guide.
**8.5 — Memory claims unmeasured** (6.1).
**8.6 — Missing guides:** HSM usage (and its limits), runtime transitions,
statistics, deferred-event workflow, time-event 2^31 span limit, ISR
posting on real hardware, RTOS integration, multi-instance with different
state counts (currently impossible, 4.1), low-power integration.
**8.7 — Missing examples:** ISR posting, FreeRTOS/Zephyr task, HSM,
smgen-generated machine, tickless idle, NVS persistence of state.
**8.8 — `stm32_platform_stub.c` realism unverified** (never compiled by the
build; no ARM target in CMake).
**8.9 — `findings.md` still says "LIFO recall" in the QP/C table** (noted
there as QP/C naming; fine) and its memory tables are plan-era figures.
**8.10 — Doc-check false positives removed:** `SM_Platform_` / `SM_Debug_`
prefix tokens were wrongly flagged stale; graphify now ignores trailing-
underscore tokens.

---

## 9. Build, portability, tooling

**9.1 — Prebuilt-archive distribution model is incompatible with the header-configured design** (1.1/4.1). `VERIFIED:run` · critical (build)
**9.2 — Windows weak-symbol override relies on archive member resolution order.** `LIKELY` · medium
- `SM_WEAK` is empty on PE/COFF; `simulation_example` overrides
  `SM_Platform_GetTimeMs` and links — only because the linker never pulls
  `sm_platform_weak.o` for that symbol first. Fragile; undocumented beyond
  the header comment.
**9.3 — No ARM target, toolchain file, size regression check, CI, coverage, sanitizers, clang-tidy/MISRA checker** (7.5). `IDEA`
**9.4 — Python tooling hygiene.** `VERIFIED:run` · low
- A site-packages `graphify` shadows the repo package unless run from the
  repo root (documented now in CLAUDE.md); no `pyproject`; Python ≥3.11
  required by `tomllib`.
**9.5 — `qpc-master/` (QP/C, GPL/commercial dual licence) sits untracked inside the repo root.** `VERIFIED:run` · low (licence hygiene — keep it out of the history).
**9.6 — `.grok/`, `AGENTS.md`, `GROK.md` are untracked** despite the workspace doc standard requiring them. `VERIFIED:run` · low

---

## 10. Model-driven roadmap (smgen) & graphify

**10.1 — Schema gaps:** no HSM/parent (reserved), no internal transitions
(1.5), no history/regions/choice, no timers/time events in the model, no
typed payloads, no `user_data`, no state/event name tables for tracing, no
per-machine counts (D17's `max()` wastes RAM and does not fix 1.1).
**10.2 — Validators:** graphify's machine checks lacked smgen's V6/V7
(added this session: shadowed rows, duplicate rows); both lack: timeout
`goto` into an unreachable/terminal state, unguarded row after `allow_drop`,
cycles with no progress, initial state without entry action, event used
only by timeout routes.
**10.3 — Round-trip cannot see per-TU macro values** (1.1 was invisible to
V2 until G15 was added). Any model↔C check must also compare the build's
compile definitions.
**10.4 — D12 committed artifacts + regex-based round-trip as the only
verification** is weak for a code generator in a safety context (tool
qualification / output verification argument needed).
**10.5 — `IDEA`s:** simulator (Phase C) as test oracle; trace decoder keyed
by model hash; SCXML/PlantUML import-export; model-checking export
(NuSMV/TLA+/UPPAAL timed automata for timeouts); transition coverage from
traces; property-based test generation; model diff.
**10.6 — graphify v2 limits (honest):** regex extraction (no real parse);
no macro-value evaluation per TU (G15 is a targeted special case); field
access typing is heuristic (local declarations only); indirect calls only
for the known callback roles; docs stale-check is prefix-based; label
propagation collapses on hub graphs (see §12). The libclang backend remains
the right fix for the first two.

---

## 11. Workspace consumers

**11.1 — `embedded/c-bone/firmware/lib/sm_framework` is a frozen v3.0.0 fork** (`sm_engine.c` `@version 3.0.0 / 2026-04-18`, differs from the template). `VERIFIED:run`
- No dependency mechanism (package / subtree / submodule / version check);
  consumers never receive v4's semantic fixes. `IDEA`: CMake package +
  `find_package`, or subtree with `SM_FRAMEWORK_VERSION` compatibility assert.
**11.2 — Workspace targets imply requirements the template lacks** `LIKELY`:
dual-core RP2354/ESP32-S3 (Plug-n-Play-Server) and core-1 LED driver
(LED-Matrix) → multicore-safe posting (1.13); PIO/DMA ISR sources → ISR
posting example; BLE/WiFi stacks → RTOS task integration and event
priorities; sensor pipelines with timeouts (VL53L9) → timeout+dwell
semantics are the right primitives but need the 2^31 and ABI fixes; LED
animation sequencers → internal transitions + periodic timers without
exit/entry churn (1.5).

---

## 12. Topology & the "emergent symbol" question

Measured on the full graph (532 functions, 935 edges incl. macro expansion
and engine→callback invocation), `graphify-out/GRAPH_REPORT.md` §Topology:

- **Bow-tie:** IN = 226 (tests, examples, callbacks) → **waist = 46 public
  `src/core` functions** → OUT = 108 (engine internals + HAL); 17 tubes,
  135 disconnected (HAL-only / safety-only tests, stm32 stub).
- **Layers** (longest path to a sink): L0 225 · L1 81 · L2 41 · L3 101 ·
  L4 10 · **L5 = 1 node: `SM_Process`** · L6 52 (all `main`s) · L7 20 ·
  L8 1. The call graph has a literal **one-node neck**: everything above
  funnels through `SM_Process`, everything below fans out from it.
- **Cycles:** 0. The only re-entry path is callback → `SM_PostEvent` →
  queue → next `SM_Process`: the two lobes of the bow-tie connect through
  the event queue, not through calls.
- **Hubs:** `SM_Process` (in 59 / out 71, betweenness 8373), `SM_PostEvent`
  (in 62 / out 1 — pure sink for the IN lobe), `SM_Platform_Assert`
  (highest PageRank, 56‰ — every assertion macro resolves to it).
- **Library-only modularity Q = 0.044**: the library is one star, not a set
  of subsystems; 34 leaf API functions have no library-internal edges at
  all; 38 library functions are articulation points (every leaf API).
- **Single-writer set** for `current_state` = {`SM_Init`, `SM_Reset`,
  `sm_execute_transition`}; for `critical_lock` = {`SM_Init`,
  `SM_Error_Report`}.

**Verdict on the hypothesis.** There *is* a recognisable shape, and it is
the one a correct run-to-completion engine must have: a **bow-tie with a
one-node neck (the dispatcher) and a second hub (the post) that is a sink,
the two joined only by the queue** — in words, a figure-eight whose
crossing point is the event queue. It is "emergent" in the sense that
nobody drew it; it falls out of RTC semantics. It is **not unique to a
perfect machine** — any dispatcher-centric design produces it. What would
distinguish a *golden* instance is not the shape but a set of graph
invariants all holding at once, and this graph currently violates several:

| Invariant a golden machine should satisfy | Here |
|---|---|
| Exactly one writer set for each protected field, all inside critical sections or single-threaded context | writers exist, but the DIS pair is written non-atomically (1.2) |
| Closure of every ISR-safe node contains only critsec-protected or pure nodes | `SM_TimeEvt_Arm` reaches `vsnprintf` (2.3); `SM_GetState` reaches `SM_Platform_Assert` |
| Zero SCCs | holds |
| One-node neck = the dispatcher; no other library articulation point except leaf accessors | holds (38 leaf APs are accessors) |
| Every waist node reached from tests and from docs | 9 untested, 8 undocumented (G7/G8) |
| Every callback role invoked by the engine | holds (G11 clean) |
| Every feature gate compiled in at least one test config | HSM fails (G10) |
| Compile-time config identical across all link units | fails (G15, 1.1) |
| Model ↔ code round-trip | holds (3/3) |

`IDEA`s for making the structure enforceable (graphify validators): ISR
closure check (G5 extended to transitive closure with a "pure" whitelist);
write-atomicity check for DIS pairs (both stores inside one critsec span);
dominator tree of `SM_Process` (everything in OUT should be dominated by
it); neck-width assertion (exactly one L-layer singleton = dispatcher);
per-instance ownership edges (which instance's fields a function may touch)
for the multicore story; a *temporal* graph (ordering edges inside
`SM_Process`: entry → execute → timeout → tick → drain) so the RTC order is
machine-checkable.

---

## 13. Ideas from the state of the art (not yet gaps with a line number)

Internal transitions (UML) · full HSM with LCA exit/entry, initial
substates, shallow/deep history · orthogonal regions or a composition API
for parallel machines · choice/junction pseudostates and completion
transitions · event priority / multiple queues · publish-subscribe between
instances · active-object wrapper (queue + thread) for RTOS targets ·
typed events / event pools as an opt-in · per-state transition index (B4)
or per-state event bitmap · tickless idle via next-deadline query ·
binary trace (QS-like) + host decoder keyed by model hash · simulator and
model-checking export · property-based tests from the model · opaque
handles and `user_data` · name tables for tracing · MISRA-clean profile
(variadic `SM_Debug_Print`, stdio in the library, block-opening
`SM_BOUNDED_LOOP_BEGIN`, infinite loop in the assert default are the obvious
rule hits) · safety profile (table CRC, control-flow monitoring, stack
monitor, watchdog integration) · 8/16-bit MCU profile (16-bit time option)
· C++ wrapper with compile-time table validation.
