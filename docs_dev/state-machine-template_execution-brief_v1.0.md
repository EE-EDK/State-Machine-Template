# state-machine-template — v4.2.0 Execution Brief

**Produced:** 2026-08-23 · **Author:** Cowork review session (read-only) · **Target:** Claude Code CLI session on `KunzPrime`
**Repo:** `C:\Users\edk7c\ENGINEERING-PROJECTS\ACTIVE-PROJECTS\embedded\state-machine-template` (submodule of MASTER)
**Baseline:** v4.1.0, four commits `00920be` ← `6ee7768` ← `9427166` ← `6bf7bd0`
**Status of this document:** directive. Decisions D18–D24 are ruled, not proposed. Work items W1–W8 are dependency-ordered.

---

## 0a. EXECUTION STATUS — appended 2026-08-23 by the Claude Code session

Additive record. Nothing below this block was rewritten; §1–§8 are the brief as
authored. Read this first for what actually happened.

### State verification (§1) — PASSED, with one reconciliation

| Check | Brief expected | Measured |
|---|---|---|
| HEAD | `6bf7bd0` | **`00920be`** — `6bf7bd0` is its ancestor. The brief mislabelled which of the four v4.1 commits is the tip; `00920be` is the correct v4.1 head and contains all four. Substance unaffected, brief not stale. |
| Tree | clean + 5 untracked | clean + 4 untracked (`build-arm/` is empty, so git never listed it) |
| `ctest` | 23/23 | 23/23 |
| Compiler warnings | 0 | 0 |
| G-checks | 0 ERROR / 8 WARN / 16 INFO | exact match |
| MACHINES.md app V-checks | 0 WARN / 1 INFO | exact match |
| §1 WARN-composition correction | 7×G7 + 1×G10 | **confirmed verbatim** |

Also verified: this repo's `.git` is a gitdir pointer left from the removed
submodule system, but MASTER reports **0 submodules**, so §5's
`git submodule status` gate is a no-op here. F-E confirmed and closed: `git
ls-files` showed **0 tracked files** under `build-arm/` or `build_runner/` —
nothing had leaked. `.gitignore` now uses a `build*/` glob.

### Work order

| Item | Status | Commit |
|---|---|---|
| W1 ABI fingerprint | **DONE** | `77a1a11` |
| W2b graphify G16 | **DONE** | `ffdc2f7` |
| W2a ISR harness | **DONE** | `609b93f` |
| W3 MINOR accessors | **DONE** | `d437af0` |
| W4 parent-fallback rename | open | — |
| W5 HAL disposition | open | — |
| W6 doc/measurement truth | open | — |
| W7 test-build divergence | open | — |
| W8 feature matrix + CI | open | — |

W2b was taken before W2a: it is the item that retires the "DIS unproven"
caveat, and knowing what G16 already covers is what keeps W2a's claims honest.

### Findings the execution produced

- **F-C upgraded from `[DERIVED]` to measured.** The brief reasoned that a
  boundary-injected ISR cannot observe the DIS tear. It was run against
  `9427166~1` — the pre-fix engine G16 reports **6 ERRORs** against — and
  **all 12 harness cases pass**. The runtime harness gives genuinely torn code
  a clean bill of health. F-C was right, and it is now measured rather than
  argued.
- **F-A reproduced with a number.** App at `SM_EVENT_QUEUE_SIZE=4` against the
  library at 8: v4.1 returns true and writes **64 bytes past the end of the
  caller's context**; v4.2 fires assertion 107 with the canary intact.
- **v4.1's own dimension guard (105/106) shipped with no test at all.** Not in
  the brief. `tests/test_abi_guard.c` now covers it.
- **graphify precision bug, found by W2a's own test.** `_isr_contract` matched
  "ISR-safe" anywhere in a doc comment, so a comment naming *another*
  function's contract made the enclosing function ISR-safe (false G5). Three
  prose mentions in the shipped headers have the same shape — latent, not
  novel. Fixed rather than reworded around; the declared-contract table was
  diffed before and after and is identical.
- **G8 caught a stale test count mid-run** (CLAUDE.md said `test_error.c` had
  18; it had 26). The checkers are earning their keep.

### Environment hazard worth carrying forward

This shell's heredocs **eat backslash-newline pairs**. That silently collapsed a
multi-line C macro onto one line and turned a regex `\b` into a literal
backspace (the same trap the 2026-08-22 session hit). Write patch scripts to a
file and run them; never heredoc anything containing a backslash. Also: Unity's
`FetchContent` dependency files blow Windows `MAX_PATH` under the scratchpad —
scratch builds need a short path such as `C:/smfb`.

### Correction to §4's W1 note

The fingerprint references `SM_FEATURE_HSM`, not `SM_FEATURE_PARENT_FALLBACK`,
because W4 has not run. **W4 must rename it there too** — `sm_types.h`'s
`SM_ABI_FINGERPRINT` is now a fifth site for that rename, in addition to the
sites §4 lists.

---

## 0. How to use this document

1. Read §1 (state verification) and run its commands **before** touching anything. Do not trust §2–§7 until §1 passes — this brief was written from a read-only mount with no git access.
2. §2 lists findings discovered **this session** that are not in `docs_dev/review_findings_2026-08-22.md`. Two of them are defects of the same class v4.1 partially fixed.
3. §3 rules the five open decisions plus two new ones.
4. §4 is the work order. Execute in sequence. Each item has exit criteria.
5. §5 is the verification protocol — the discipline that caught two wrong fixes last session. It is mandatory, not advisory.
6. §6 is the do-not list. §7 is the Save Point.

**Confidence tags used throughout:** `[VERIFIED:code]` read from cited file:line this session · `[VERIFIED:graph]` from `graphify-out/GRAPH_REPORT.md` this session · `[DERIVED]` computed from verified facts · `[RECALLED]` from the prior session's report, not re-verified here.

---

## 1. State verification — run first

This brief was written from the Cowork device mount. **`git` was not reachable**: `.git` is a gitdir pointer (`gitdir: ../../../.git/modules/embedded/state-machine-template`) resolving outside the mounted folder, so no `git status`, `git log`, `git ls-files`, or `git submodule status` could be run. `[VERIFIED:code]` Every claim below about commit state is `[RECALLED]` from the prior session's report only.

Run these before starting. If any disagrees with what follows, **stop and reconcile — this brief is stale.**

```bash
cd /c/Users/edk7c/ENGINEERING-PROJECTS/ACTIVE-PROJECTS/embedded/state-machine-template

git status --short                    # expect clean + 5 untracked: qpc-master/ AGENTS.md GROK.md .grok/ (+ build-arm/ build_runner/ — see F-E)
git log --oneline -6                  # expect 6bf7bd0 at HEAD
git describe --tags --always
git submodule status                  # this repo IS a submodule; confirm MASTER's pointer before any MASTER commit

# From-scratch build + test (NOT incremental — the prior session's ABI defect hid in incremental builds)
rm -rf build_v42 && cmake -S . -B build_v42 -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_v42 --clean-first
cd build_v42 && ctest --output-on-failure && cd ..

# Graph state
python3 -c "from graphify.watch import _rebuild_code; from pathlib import Path; _rebuild_code(Path('.'))"
grep -n "ERROR\|WARN" graphify-out/GRAPH_REPORT.md | head -20
```

**Expected baseline** `[RECALLED]` from prior session, `[VERIFIED:graph]` for the G-check line:

| Metric | Expected |
|---|---|
| `ctest` | 23/23 pass |
| Compiler warnings | 0 |
| G-checks | **0 ERROR, 8 WARN**, 16 INFO |
| MACHINES.md V-checks (application machines) | 0 WARN, 1 INFO |
| Example stdout | byte-identical to pre-v4.1 golden baseline |

**Correction to carry forward** `[VERIFIED:graph]`: the prior session described the 8 WARNs as "seven never-exercised HAL stubs and the never-compiled HSM region." The actual composition is **7 × G7-untested-api + 1 × G10-feature-untested**, and the seven are: `App_Main_GetVersion`, `SM_Platform_EnterSleep`, `SM_Platform_ExitSleep`, `SM_Platform_OutputSend`, `SM_Platform_WatchdogKick`, `SM_Platform_WatchdogStart`, `SM_Platform_WatchdogStop`. `App_Main_GetVersion` is not HAL. `SM_Platform_OutputSend` is not un-exercised — `test_platform.c` overrides it as a no-op and the engine calls it via `sm_debug.c`; it has no *direct* test caller. Conversely `SM_Platform_IsTimeout`, `NVS_Read`, `NVS_Write`, `GetResetReason`, `HasCapability`, `GetCriticalNesting` **are** called by `tests/test_hal.c` and are not warned. This changes what W5 must actually address.

---

## 2. New findings — this session

Numbered to continue the existing dossier. Add these to `docs_dev/review_findings_2026-08-22.md` as an appendix (additive only — Historical Preservation).

### F-A — The v4.1 ABI guard closes 2 of ~10 doors, and the 8 left open are worse. `[VERIFIED:code]` · **critical**

v4.1 made `SM_Init` a macro carrying the application's `SM_STATE_COUNT` / `SM_EVENT_COUNT` and rejecting a mismatch (assertions 105/106, `sm_engine.c:366-376`). That is correct as far as it goes. It does not go far enough.

The application allocates the context itself — `SM_Context_t sm_ctx;` appears in all six examples (`basic_example.c:189`, `blinky_example.c:229`, `error_recovery_example.c:245`, `multi_fsm_example.c:322-323`, `sensor_pipeline_example.c:320`, `simulation_example.c:128`) and `SM_Handle_t` is `SM_Context_t *` (`sm_types.h:101`). The struct's layout (`sm_types.h:411-467`) depends on **at least eight** macros:

| Macro | Layout effect |
|---|---|
| `SM_STATE_HISTORY_DEPTH` | `state_history[]` |
| `SM_EVENT_QUEUE_SIZE` | `event_queue` (via `SM_EventQueue_t`) |
| `SM_ERROR_HISTORY_SIZE` | `error` (via `SM_ErrorHandler_t`) |
| `SM_FEATURE_RUNTIME_TRANSITIONS` + `SM_MAX_TRANSITIONS` | `rt_transitions[]`, `rt_transition_count` |
| `SM_FEATURE_TIME_EVENTS` | `time_evt_head` |
| `SM_FEATURE_DEFER` + `SM_DEFER_QUEUE_SIZE` | `defer_queue` |
| `SM_FEATURE_STATISTICS` (+ `SM_STATE_COUNT`) | `stats` |

**None of these are checked.** Only the two count macros are. A build where the application and the library disagree on, say, `SM_EVENT_QUEUE_SIZE` sails past assertions 105 and 106 and then has the library write past the end of an object the application allocated — stack or BSS corruption, not a failed range check.

The project already knows this: `config/sm_config_template.h:49-52` explicitly names the six other layout-affecting macros and says the same rule applies to them; `basic_example.c:33` and `simulation_example.c:22` both warn about queue size in comments. There is documentation and no enforcement. `[VERIFIED:code]`

**This is the same defect class v4.1 was released to fix**, and the residue is more dangerous than the original (memory corruption vs. a wrong accept range). Treat as W1.

### F-B — `ctest` never exercises the shipped library target or its dimension propagation. `[VERIFIED:code]` · high

`tests/CMakeLists.txt:36-56` builds a **separate** `sm_framework_test` target that recompiles `src/core/*.c` with a hardcoded `SM_TEST_DEFS` list — including `SM_STATE_COUNT=4U` and `SM_EVENT_COUNT=8U` literals, *not* the new `SM_DIMENSION_DEFS` cache variables from `CMakeLists.txt:40-46`.

Consequences:
1. The `PUBLIC`-propagation mechanism that is the entire v4.1 fix is **never exercised by any test**. The only thing that exercises it is building the examples.
2. Configuring with `-DSM_STATE_COUNT=6` changes the library and examples but silently leaves the test build at 4/8.
3. The feature matrix (W8) must parameterize `tests/CMakeLists.txt`, not just the root — otherwise every matrix cell runs identical test defs.

### F-C — The ISR harness as sketched cannot observe the DIS tear, and does not need to. `[DERIVED]` from `[VERIFIED:code]`

`tests/test_platform.c:76-88`: `SM_Platform_EnterCritical` / `ExitCritical` are a bare nesting counter — a clean injection seam, as the prior session said. But two corrections to the plan:

1. **On a single core an ISR cannot run inside a critical section.** Firing the injected ISR at *every* boundary, including while `nesting > 0`, models an NMI or a second core (finding 1.13), not the documented single-core contract. Injecting there and then reporting a tear is a false positive against the contract the framework actually makes. Default the hook to fire **only when `nesting == 0`**; provide an explicit opt-in "fire inside critsec" mode labelled as the multicore/NMI model.
2. **A boundary-triggered hook can never land between two adjacent non-critical stores.** Pre-fix, `current_state` and `state_dis` were two consecutive stores with no Enter/Exit between them — so a boundary hook has no seam to fire in, and the harness would report "no tear" against the *buggy* code. It would fail the detector test in §5 and rightly so.

**Therefore:** DIS write-atomicity is a *structural* property and must be proven statically, not dynamically — a new graphify **G16** check that both stores of every DIS pair fall inside one critical-section span. The machinery already exists: GRAPH_REPORT has a "Critical sections" section and G14 already flags volatile writes outside critical sections `[VERIFIED:graph]`, and §12 of the dossier already lists this as an IDEA. The runtime harness proves the *dynamic* claims (queue index races, watermark, timer-list corruption during tick, recall-vs-post). Split the work accordingly — W2a and W2b. This is what actually retires the "unproven by test" caveat.

### F-D — Documentation residues the v4.1 doc pass did not reach. `[VERIFIED:code]` · low, but they are exactly the class that caused the original defect

| Location | Residue |
|---|---|
| `README.md:364-367` | RAM table still claims **~544 B baseline / ~580 B with defer**. Finding 6.1 measured **316 B** on Cortex-M4 `-Os`. |
| `CLAUDE.md` "Key Architecture (v4.1)" | Same stale `~544 bytes RAM baseline, ~580 with defer queue` line. |
| `CLAUDE.md` "Key Architecture (v4.1)" | `3-tier errors: MINOR (auto-recover)` — promises behaviour that does not exist (finding 3.2). |
| `include/sm_framework/sm_error.h:10` and `:43` | `MINOR: Auto-recovery, no state change required` / `application can auto-recover` — the application **cannot**: `minor_active` has no public accessor. The only readers are tests reaching into `s_ctx.error.minor_active` directly (`test_error.c:141, 269`). |
| `include/sm_framework/sm_config.h:259` | Doc comment points at **`sm_find_transition_hsm`, a function that does not exist**. The real code is the `SM_FEATURE_HSM` region inside `sm_resolve_transition` (`sm_engine.c:244-289`). |
| `config/sm_config_template.h:10-16` | USAGE step 2 still reads "Define `SM_STATE_COUNT` and `SM_EVENT_COUNT` (REQUIRED)" — i.e. define them in this header, the exact thing that does not work. The correcting block is 20 lines below at `:25-51`. A reader who follows the numbered list never reaches it. |

### F-E — `.gitignore` does not cover two build directories present on disk. `[VERIFIED:code]` · low

`.gitignore` ignores `build/`, `build_wtr/`, `Build/`, `out/`, `cmake-build-*/`. On disk there are also **`build-arm/`** and **`build_runner/`**, neither matched. Most of their contents are caught by the global `*.o` / `*.a` / `*.cmake` / `CMakeFiles/` rules, but not all (`compile_commands.json`, logs, generated headers). Tracked status could not be checked — no git access. Run `git ls-files | grep -E '^build-arm/|^build_runner/'` as part of the Git Security Gate before the next push.

---

## 3. Decisions — ruled

Continuing the project's D-numbering (D1–D11 in `task_plan.md`, D12–D17 in `phase_b_model_plan.md`).

### D18 — MINOR tier: **expose the state, do not implement recovery.** Re-document as two enforced tiers + one informational tier.

The framework cannot know what recovering from an application's minor error means. The only framework-level policy available is "auto-clear after N ms," which is a policy decision belonging to the application — and every other recovery path in this framework is already application-driven (`SM_Error_RegisterRecoveryCallback`, `SM_Error_AttemptRecovery`). Consistency wins.

- **Add** `bool SM_Error_IsMinorActive(SM_Handle_t sm)` and `bool SM_Error_GetMinorTimestamp(SM_Handle_t sm, uint32_t *out_ms)`, and `void SM_Error_ClearMinor(SM_Handle_t sm)` (clearing minor without wiping the current error record — distinct from `SM_Error_Clear`).
- **Do not** implement auto-clear. Record it as a future opt-in `SM_FEATURE_MINOR_AUTOCLEAR_MS` in the dossier; do not build it now.
- **Re-document** the tier model everywhere it appears (`sm_error.h:8-12`, `:41-45`, `sm_types.h:293-299`, `CLAUDE.md`, `README.md`) as: MINOR = recorded and queryable, application defines the policy; NORMAL = managed recovery; CRITICAL = system lock.
- Rejected alternative — deleting the fields — loses the timestamp that any application policy needs and breaks `test_error.c`'s existing coverage for no gain. 8 bytes is not the defect; the false promise is.

### D19 — HSM: **rename to what it is. Real hierarchy is a separately planned v5.0.**

`sm_engine.c:244-289` walks the parent chain looking for a handler. It computes no least-common-ancestor, runs no entry/exit chains for the states in between, has no initial substates and no history. `[VERIFIED:code]` A transition between nested states silently skips the parents' exit and entry actions. The feature is never compiled under test (`SM_FEATURE_HSM=0U`, `tests/CMakeLists.txt:47`) and no example uses it. `[VERIFIED:code]`

The hazard is the name. "HSM" invites UML statechart expectations and delivers quietly wrong behaviour.

- **Rename** `SM_FEATURE_HSM` → `SM_FEATURE_PARENT_FALLBACK` and `SM_HSM_MAX_DEPTH` → `SM_PARENT_MAX_DEPTH`. Keep `SM_FEATURE_HSM` as a deprecated alias for one minor version that emits `#warning` (or `#error` under a strict flag), documented in `MIGRATION.md`.
- **Compile it under test** — this is what clears the G10 WARN honestly. One dedicated suite with a 3-level parent chain asserting: handler found at parent; handler found at grandparent; depth bound respected; **and explicitly asserting that intermediate entry/exit actions do NOT run**, with a comment stating that this is the documented limit, not a bug.
- **Document the limits** in the header and README: what it does, what UML semantics it does not implement, and that `smgen` reserves and rejects a `parent` key pending v5.0.
- **Fix** the dangling `sm_find_transition_hsm` reference (F-D).
- Full LCA/entry-exit/history is a **v5.0 line item**, not this cycle. QP/C spends ~650 lines on it and it touches the transition executor, the state descriptor, the smgen schema and the graph extractor.

### D20 — HAL: **split three ways, do not treat it as one question.**

Engine call sites, measured `[VERIFIED:code]` (`grep` over `src/core/*.c`): `GetTimeMs` ×17, `EnterCritical` ×9, `ExitCritical` ×9, `OutputSend` ×4, `OutputInit` ×2. Nothing else.

**(a) Adopt into the run loop — behind opt-in feature flags:**
- `SM_FEATURE_WATCHDOG` (default `0`): `SM_Platform_WatchdogKick()` called once per `SM_Process` on the "machine is alive" criterion. Opt-in because enabling it unconditionally changes behaviour for every existing consumer.
- `bool SM_GetNextDeadline(SM_Handle_t sm, uint32_t *out_ms)` — returns `false` when the queue is empty and no timer is armed. This is the missing half of finding 6.4/2.8: the HAL has `EnterSleep` and nothing drives it. **Add the query; do not call `EnterSleep` from inside the engine** — sleeping is the scheduler's decision. Document the idle pattern in README and add a tickless-idle example.

**(b) Application-facing — document as such, test the documented defaults:** `NVS_Read` / `NVS_Write` / `GetResetReason` / `HasCapability`. These already have test callers (`test_hal.c`) and are not in the G7 warn list — the work here is documentation (they are in finding 8.4's undocumented-API list), not tests.

**(c) `SM_Platform_IsTimeout` — demote, do not adopt.** *This overrules the prior session's "use it or remove it."* Routing the engine through it would add a HAL call to a path that finding 1.8 already criticises for reading the clock 3+ times per `SM_Process`. Keep the inline wrap-safe arithmetic; re-document `IsTimeout` explicitly as an application utility with the sentence *"the engine does not call this; overriding it does not change engine timing."* An honest non-promise beats a broken promise or a pessimisation.

**(d) `SM_Platform_SimTick` stays but moves** behind `#if SM_PLATFORM_SIM` in the header — it is a simulation hook in a production header (finding 4.5).

**(e) `App_Main_GetVersion`** — the seventh G7 warn and not HAL at all. Either give it a test caller or delete `src/app/app_main.c`, which is 30 lines and serves no framework purpose. Recommend delete; it is scaffolding.

### D21 — ISR harness: **build it, second, split into runtime + static.** See F-C. Scope limits must be written into the harness's own file header, not just the commit message: instrumented-point injection is not preemption at arbitrary instruction boundaries, and it says nothing about multicore. It is a large improvement, not a proof.

### D22 — Feature matrix + CI: **compile-only sweep across all configs, full `ctest` on three, as the first GitHub Actions workflow.** Comes last because D18, D19 and D20 each change what the matrix must cover. There is currently **no `.github/` directory at all** `[VERIFIED:code]` — this is greenfield, and it is the natural home for `smgen check` when B3 lands.

### D23 — ABI guard completeness (new, from F-A): **one compile-time fingerprint over every layout- and semantics-affecting macro, carried through `SM_Init`.** `sizeof(SM_Context_t)` alone catches most of it in one number and needs no maintenance when fields are added; a composed fingerprint additionally catches non-layout divergence (`SM_FEATURE_ASSERT`, `SM_MAX_EVENTS_PER_PROCESS`). Use both. Keep 105/106 for their precise diagnostics and add 107 for the fingerprint.

### D24 — Version: **v4.2.0.** `SM_Init_`'s signature changes (D23) and `SM_FEATURE_HSM` is renamed (D19). Minor bump, not patch. `SM_Init_` is `_`-suffixed and graphify-exempt, but it is reachable — give it a `MIGRATION.md` v4.1 → v4.2 section.

---

## 4. Work order

Sequenced by dependency. Each is one commit. Do not batch.

### W1 — Close the ABI hole completely (F-A / D23)

**Change:**
1. In `sm_types.h`, after the `struct SM_Context` definition, add:
```c
/* Build-consistency fingerprint. Folds every macro that changes SM_Context_t's
 * layout or the engine's accept/range semantics into one compile-time value.
 * The APPLICATION's value travels with SM_Init and is compared against the
 * LIBRARY's. sizeof() catches layout divergence with zero maintenance; the
 * folded macros catch semantic divergence that does not move a field. */
#define SM_ABI_FINGERPRINT ( \
      ((uint32_t)sizeof(SM_Context_t)            * 0x9E3779B1U) \
    ^ ((uint32_t)(SM_STATE_COUNT)                * 0x85EBCA77U) \
    ^ ((uint32_t)(SM_EVENT_COUNT)                * 0xC2B2AE3DU) \
    ^ ((uint32_t)(SM_EVENT_QUEUE_SIZE)           <<  1) \
    ^ ((uint32_t)(SM_DEFER_QUEUE_SIZE)           <<  5) \
    ^ ((uint32_t)(SM_ERROR_HISTORY_SIZE)         <<  9) \
    ^ ((uint32_t)(SM_STATE_HISTORY_DEPTH)        << 13) \
    ^ ((uint32_t)(SM_MAX_TRANSITIONS)            << 17) \
    ^ ((uint32_t)(SM_MAX_EVENTS_PER_PROCESS)     << 20) \
    ^ ((uint32_t)(SM_FEATURE_MAX_TIME_EVENTS)    << 23) \
    ^ ((uint32_t)(SM_FEATURE_RUNTIME_TRANSITIONS)<< 24) \
    ^ ((uint32_t)(SM_FEATURE_TIME_EVENTS)        << 25) \
    ^ ((uint32_t)(SM_FEATURE_DEFER)              << 26) \
    ^ ((uint32_t)(SM_FEATURE_STATISTICS)         << 27) \
    ^ ((uint32_t)(SM_FEATURE_ASSERT)             << 28) \
    ^ ((uint32_t)(SM_FEATURE_DEBUG)              << 29) \
    ^ ((uint32_t)(SM_FEATURE_PARENT_FALLBACK)    << 30) )
```
   (`SM_FEATURE_PARENT_FALLBACK` lands in W4 — use `SM_FEATURE_HSM` here and rename in W4, or do W4 first. Do not leave the fingerprint referencing a macro that does not yet exist.)
2. Extend `SM_Init_` to take `uint32_t app_abi` and the `SM_Init` macro to pass `SM_ABI_FINGERPRINT`.
3. `SM_REQUIRE(107, app_abi == SM_ABI_FINGERPRINT)` plus the `return false` path, matching the 105/106 pattern at `sm_engine.c:366-376`, with a log line naming both values in hex.
4. Update the assertion-ID comment block at `sm_engine.c:43`.
5. `MIGRATION.md`: v4.1 → v4.2 section covering the `SM_Init_` signature.

**Exit criteria:**
- A deliberately mismatched program (compile a test TU with `-DSM_EVENT_QUEUE_SIZE=16` against a library built at 8) is **rejected at `SM_Init`**, not silently corrupting. This must be an actual reproduction, in the style of the v4.1 ABI reproduction — not an argument.
- New test asserts 107 fires and `SM_Init` returns false. It must **fail against pre-W1 HEAD** (§5).
- `ctest` green; six example stdouts byte-identical; G-checks no worse than 0 ERROR / 8 WARN.

### W2a — ISR-interleaving harness, runtime (D21 / F-C)

**Change:** `tests/test_platform.c` + `tests/test_common.h` only. Zero production-source edits.
```c
/* phase: SM_TEST_ISR_PRE_ENTER, SM_TEST_ISR_POST_EXIT (nesting==0 seams — the
 * single-core model) and SM_TEST_ISR_IN_CRITSEC (opt-in; models NMI or a second
 * core, NOT the documented single-core contract). */
typedef void (*test_isr_hook_t)(int phase, uint32_t nesting);
void test_isr_hook_set(test_isr_hook_t h, bool fire_inside_critsec);
```
Re-entrancy guard so an injected ISR that itself takes a critical section does not recurse. Default: fire only at `nesting == 0`.

**Scenarios to cover:** ISR `SM_PostEvent` during a drain; ISR `SM_PostEvent` during `SM_TimeEvt_Tick_` phase 1 vs phase 2; ISR `SM_GetState` / `SM_Error_IsCriticalLock` at every seam; queue head/tail index consistency under injected posts; watermark consistency; timer-list integrity across an injected arm/disarm during tick; recall-vs-post ordering.

**Exit criteria:**
- Harness limits documented in the file header (instrumented points ≠ arbitrary preemption; single-core only).
- At least one new test **fails against pre-W2a HEAD** — pick a scenario the current code genuinely gets wrong or that the harness genuinely detects. If every scenario passes on both old and new code, say so plainly in the commit message rather than claiming coverage the tests do not provide.
- No production source file changed. Verify with the diff.

### W2b — graphify **G16**: DIS write-atomicity (F-C)

**Change:** new validator in `graphify/analyze.py`: for every DIS field/shadow pair, both stores must fall inside one critical-section span. Reuses the critical-section span extraction G14 already depends on. Unit test in `graphify/tests/` with a fixture that has a torn pair and one that does not.

**Exit criteria:** G16 is **clean on current HEAD** and **reports ERROR when run against `9427166~1`** (pre-DIS-fix). That second half is the whole point — it converts the prior session's "unproven by test" caveat into "proven by static invariant, with the detector demonstrated against the known-bad revision." Record that result in `CLAUDE.md`.

### W3 — MINOR tier (D18)

Add the three accessors, re-document all five doc sites listed in F-D, add tests for the accessors on NULL handles and after `SM_Error_Clear`. Assertion IDs in the 700-799 block.

**Exit criteria:** no doc anywhere claims the framework auto-recovers. `test_error.c` stops reaching into `s_ctx.error.minor_active` directly and uses the public accessor (leave one direct-field assertion as a white-box check if useful, with a comment).

### W4 — Parent fallback rename + first compile under test (D19)

Rename, deprecated alias, limits documented, dedicated test suite, `sm_find_transition_hsm` reference fixed, `smgen`'s reserved `parent` key note updated.

**Exit criteria:** G10 WARN gone **because the region is genuinely compiled and tested**, not because the flag moved. `MIGRATION.md` v4.2 section covers the rename. `MACHINES.md` still 0 application WARNs.

### W5 — HAL disposition (D20)

(a) `SM_FEATURE_WATCHDOG` + `SM_GetNextDeadline` + tickless-idle example; (b) document the four application-facing calls; (c) demote `IsTimeout` in docs; (d) `SimTick` behind `SM_PLATFORM_SIM`; (e) delete `src/app/app_main.c` and its CMake entry.

**Exit criteria:** the remaining G7 WARNs are only those the project has consciously decided to leave. Whatever survives, record *why* in `CLAUDE.md` — the prior session's refusal to add stub tests purely to silence warnings is correct and must be preserved as a stated policy, not re-derived each time.

### W6 — Documentation and measurement truth pass (F-D)

Correct the RAM figures in `README.md:364-367` and `CLAUDE.md` to the measured 316 B (cite the measurement conditions: Cortex-M4, `arm-none-eabi-gcc 14.3`, `-Os`, defaults). Restructure `config/sm_config_template.h`'s USAGE list so step 2 states the build-system rule directly rather than deferring to a block below it. Sweep `README.md`, `Quick-Guide.md`, `CLAUDE.md`, `GEMINI.md`, `AGENTS.md`, `GROK.md` for the renamed HSM macro and the new APIs.

**Exit criteria:** finding 6.1's `IDEA` implemented — a generated size report (`arm-none-eabi-size` + a `sizeof` table) emitted into the graph output so these numbers cannot drift silently again. G8 clean.

### W7 — Fix the test-build divergence (F-B)

Make `tests/CMakeLists.txt` derive `SM_TEST_DEFS` from the root cache variables instead of hardcoded literals, and add **one** test executable that links the real `sm_framework` target (not `sm_framework_test`) so the `PUBLIC` dimension propagation is exercised by `ctest`. This is a prerequisite for W8 having any meaning.

### W8 — Feature matrix + CI (D22)

**Compile-only sweep** (library + examples, `-Werror`):

| # | Config |
|---|---|
| 1 | all features on (current default) |
| 2 | minimal: `PARENT_FALLBACK=0 ASSERT=0 DEBUG=0 TIME_EVENTS=0 DEFER=0 STATISTICS=0 RUNTIME_TRANSITIONS=0`, `MAX_EVENTS_PER_PROCESS=1`, `EVENT_QUEUE_SIZE=1` |
| 3 | `PARENT_FALLBACK=1` |
| 4 | `ASSERT=0` only |
| 5 | `DEBUG=0` only |
| 6 | extremes: `SM_STATE_COUNT=255`, `SM_EVENT_COUNT=65534`, `EVENT_QUEUE_SIZE=64`, `FEATURE_MAX_TIME_EVENTS` at bound |
| 7 | C99 `SM_STATIC_ASSERT` negative-array fallback path forced |
| 8 | `WATCHDOG=1` (new in W5) |

**Full `ctest`** on configs 1, 2 and 4.

**Named cost — do not let this surprise you mid-run:** config 4 (`ASSERT=0`) will break every `TEST_EXPECT_ASSERT`-based case, because no assertion fires. Those tests must be `#if SM_FEATURE_ASSERT`-guarded, and the `=0` build must instead assert the **return-value** path — which is precisely finding 3.4's untested branch and the entire reason this config matters. Budget for it.

**CI:** first `.github/workflows/` — job 1 compile sweep, job 2 `ctest` ×3, job 3 graphify G-checks + `smgen validate --strict`. `ubuntu-latest`; note `tests/CMakeLists.txt` uses `FetchContent` for Unity so the runner needs network.

**Exit criteria:** every config in the table builds; the three `ctest` configs pass; CI green on a pushed branch before merge.

---

## 5. Verification protocol — mandatory

The prior session got two fixes wrong on the first attempt and the tests caught both. That was not luck; it was this discipline. Apply it to every W item.

**1. Golden baseline before touching anything.**
```bash
mkdir -p /tmp/smf_golden
for e in basic simulation blinky sensor_pipeline error_recovery multi_fsm; do
  ./build_v42/examples/${e}_example > /tmp/smf_golden/$e.txt 2>&1
done
```
After each W item, re-run and `diff -r`. Any change is either a deliberate, stated behaviour change or a regression. There is no third case.

**2. Prove every new test is a detector, not decoration.**
```bash
git archive HEAD | (mkdir -p /tmp/smf_prefix && tar -x -C /tmp/smf_prefix)
# build the NEW tests against the PRE-change source; they must FAIL there
```
Record the fail count in the commit message, as the prior session did ("6 of 11 fail there"). A new test that passes against pre-fix code is proving nothing — either fix the test or say so.

**3. Graph delta on every item.** Rebuild, diff the G-check block. Never accept a WARN disappearing without knowing which change removed it.
```bash
python3 -c "from graphify.watch import _rebuild_code; from pathlib import Path; _rebuild_code(Path('.'))"
```
Run from the repo root — a workspace-level `graphify` in site-packages shadows the repo package otherwise. **Delete `graphify-out/cache/` for any item that renames or moves files** (W4 renames macros across the tree; W5 deletes `src/app/app_main.c`).

**4. From-scratch configure and build, not incremental**, for the final verification of each item. The v4.1 ABI defect was invisible to incremental builds.

**5. Before any push:** `git ls-files` audit against `.gitignore` (Git Security Gate) — and specifically check F-E's two directories. `qpc-master/` must never enter history: GPL/commercial dual licence.

**6. Before any MASTER commit:** `git submodule status`. This repo is a submodule; operate from this inner repo root for its own commits.

---

## 6. Do not

- **Do not commit `qpc-master/`.** Licence hygiene, finding 9.5.
- **Do not add stub tests purely to clear G7 warnings.** The prior session declined this deliberately and was right. If a warning is acceptable, record why in `CLAUDE.md`; do not launder it.
- **Do not delete resolved findings** from the dossier or `CLAUDE.md`. Annotate with status. Historical Preservation.
- **Do not implement full HSM in this cycle** (D19). If it starts to feel small, re-read `sm_execute_transition` and count the touch points.
- **Do not call `SM_Platform_EnterSleep` from inside the engine** (D20a). Provide the query; the scheduler decides.
- **Do not claim the DIS race is proven by the runtime harness** (F-C). G16 proves it structurally; say that, and say what the harness does and does not cover.
- **Do not re-`#define` `SM_STATE_COUNT` / `SM_EVENT_COUNT` in any application source.** After W1 this is enforced rather than merely documented — but the rule stands regardless.
- **Do not batch W items into one commit.** Each has its own detector proof and its own graph delta.

---

## 7. Save Point

```json
{
  "project": "state-machine-template",
  "path": "C:\\Users\\edk7c\\ENGINEERING-PROJECTS\\ACTIVE-PROJECTS\\embedded\\state-machine-template",
  "vcs": "git submodule of MASTER; gitdir -> ../../../.git/modules/embedded/state-machine-template",
  "baseline_version": "4.1.0",
  "baseline_head": "6bf7bd0",
  "target_version": "4.2.0",
  "brief": "docs_dev/state-machine-template_execution-brief_v1.0.md — v1.0, 2026-08-23",
  "decisions_ruled": {
    "D18": "MINOR tier: expose state via accessors; no framework auto-recovery; re-document as 2 enforced + 1 informational tier",
    "D19": "Rename SM_FEATURE_HSM -> SM_FEATURE_PARENT_FALLBACK; compile+test it; real hierarchy deferred to v5.0",
    "D20": "HAL split: adopt watchdog+next-deadline behind flags; document app-facing NVS/reset/caps; demote IsTimeout; SimTick behind SM_PLATFORM_SIM; delete app_main.c",
    "D21": "ISR harness: build second; runtime harness fires at nesting==0 by default, opt-in in-critsec mode for NMI/multicore",
    "D22": "Feature matrix compile-sweep (8 configs) + full ctest on 3, as first GitHub Actions workflow; last in sequence",
    "D23": "ABI guard completeness: SM_ABI_FINGERPRINT over sizeof(SM_Context_t) + all layout/semantics macros; assertion 107",
    "D24": "Version bump to 4.2.0; MIGRATION.md v4.1->v4.2 section"
  },
  "work_order": ["W1 ABI fingerprint", "W2a ISR harness", "W2b graphify G16", "W3 MINOR accessors", "W4 parent-fallback rename", "W5 HAL disposition", "W6 doc/measurement truth", "W7 test-build divergence", "W8 feature matrix + CI"],
  "new_findings": {
    "F-A": "ABI guard covers 2 of ~10 layout macros; app allocates SM_Context_t; mismatch = memory corruption. CRITICAL.",
    "F-B": "ctest never links the shipped sm_framework target; tests/CMakeLists.txt hardcodes 4U/8U, bypassing the v4.1 PUBLIC propagation.",
    "F-C": "Boundary-injected ISR cannot observe the DIS tear; DIS atomicity needs static G16, not a runtime race test.",
    "F-D": "Doc residues: 544B vs measured 316B; MINOR auto-recovery claims; dangling sm_find_transition_hsm ref; template USAGE step 2.",
    "F-E": ".gitignore misses build-arm/ and build_runner/."
  },
  "carried_open": "Everything else in docs_dev/review_findings_2026-08-22.md not marked FIXED. Notably 1.5 internal transitions, 1.13 multicore, 3.4 assert-then-return, 3.6 unshadowed DIS fields, 5.5 binary trace, 6.3 linear transition search, 11.1 c-bone frozen v3.0.0 fork.",
  "blocked": "Opus/Sonnet review fleet never ran (monthly spend limit). Workflow resumable: resumeFromRunId wf_c3683e9d-143. All findings remain single-reviewer.",
  "unverified_this_session": "No git access from the Cowork mount; no build or test execution. All commit-state claims are RECALLED from the prior session's report, not re-verified. Run section 1 first."
}
```

---

## 8. Sequencing rationale — why this order

W1 first, ahead of the ISR harness the prior session recommended, for one reason: it is the **unfinished half of the work that was just shipped**, it is roughly ten lines plus two tests, and the state it currently sits in is the worst one — a guard that looks complete, is documented as complete, and silently passes the mismatches that corrupt memory. A partially-closed door reads as a closed door to the next reader, including the next session.

W2a/W2b second, unchanged from the prior session's recommendation and for its reason: independent of every decision, cheap, and it retroactively validates a fix already in `main`.

W3–W5 are the three decision items. They are ordered by how much they change the surface the matrix has to cover: MINOR adds three functions, the rename touches every file mentioning the macro, the HAL work adds a feature flag and a new engine query.

W6 and W7 are enablers that must land before W8 means anything: W7 in particular, because until the test build stops hardcoding its own dimensions, a "matrix" of test configurations would run the same test configuration eight times.

W8 last, as the prior session said.

---

*Authored 2026-08-23 in a read-only review session; no source file was modified by that session. Placed at `docs_dev/state-machine-template_execution-brief_v1.0.md` by explicit authorization after the fact.*
