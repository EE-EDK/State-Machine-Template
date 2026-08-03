# Phase B Plan: Model as Source of Truth (smgen)

**Status: PLANNED — not started.** Decisions D12–D17 recorded below.
Prerequisite: v4.0.0 semantic release (complete) and the graphify
machine-level extractor + validator (complete, `graphify/machines.py`).

## Goal

Invert the current relationship between the model and the code. Today a
state machine is hand-written C (`SM_Transition_t[]`, `SM_StateDesc_t[]`,
`SM_Config_t`) and graphify *extracts* a model from it, validating after
the fact. After Phase B, the machine is defined once in a declarative
model file and a generator (`smgen`) *emits* the C tables — so the
validator runs before the C exists and structural defects cannot be
compiled at all. The trusted runtime (the v4 engine) does not change; only
where the tables come from changes.

The proof this is worth doing already exists in-repo: graphify's validator
found a shipped defect (`basic_example` dead timeout) on its first run,
in a codebase that had just survived a deep manual review. Generation
makes that entire defect class unrepresentable.

## Success Criteria

- A machine defined in one TOML file generates: state/event enums, const
  transition + descriptor tables, an `SM_Config_t`, extern callback
  prototypes, a Mermaid/markdown rendering, and a model hash.
- All V1–V5 validator checks (plus the new checks below) run at
  generation time and **fail generation** at ERROR severity.
- Round-trip property holds in CI: graphify's extractor, run on the
  generated C, reproduces the model (two independent code paths agree).
- At least `blinky` and `basic` examples are migrated to generated tables
  with zero behavioral change (all 19 test suites stay green; example
  output identical).
- Hand-written machines remain fully supported — generation is opt-in
  per machine, forever. The engine cannot tell the difference.

## Non-Goals (Phase B)

- No engine changes except the optional per-state index feature (B4).
- No mission/sequence DSL yet — that layers on top of this schema later.
- No Python simulator (`sm-sim` is Phase C; this plan only reserves the
  artifacts it will need: the symbol table and model hash).
- No HSM support in the schema until the engine's hierarchical semantics
  are upgraded (Phase D of the roadmap). The schema reserves a `parent:`
  key but the generator rejects it for now.

## Architecture Decisions (continuing D1–D11)

### D12 — Generated C is committed, not build-time ephemeral
The generator writes C files into the repo; they are reviewed, diffed,
and versioned like any source. CI regenerates and fails on drift
(`smgen check`). Rationale: safety-critical review and certification want
a stable, inspectable artifact — "what exactly is in flash" must be
answerable from the repo without running tooling. This also keeps the
firmware build free of any Python dependency.

### D13 — TOML via stdlib `tomllib`, not YAML/JSON
Model files are TOML, parsed with Python ≥3.11 stdlib `tomllib`
(available in all target environments; verified 3.11.15 here). Rationale:
zero third-party dependencies keeps the tooling supply-chain audit
surface at nil — the same argument as the stdlib-only graphify package.
JSON lacks comments (unacceptable for reviewed models); YAML buys nicer
nesting at the cost of a dependency and a notoriously permissive parser.
The internal model object is format-agnostic, so a YAML front-end can be
added later without touching the emitters.

### D14 — Callbacks bind by name; the linker enforces existence
The model references guard/action/entry/execute/exit callbacks by C
identifier. The generator emits `extern` prototypes for every referenced
callback into the generated header; the user implements them in ordinary
C. A missing or misspelled callback is a **link error** — existence
checking for free, with zero runtime cost and no registration tables.

### D15 — The schema makes unrouted timeouts unrepresentable
`timeout` is a table that **requires** a `goto`:
`timeout = { after_ms = 5000, goto = "FAULT" }`. There is no way to
express "timeout fires but nothing consumes it" — the V2 defect class is
eliminated at the schema level, which is strictly stronger than
validating for it. (Extraction-side V2 stays, for hand-written machines.)

### D16 — Model hash compiled into firmware
The generator canonicalizes the model (sorted keys, normalized values),
hashes it (SHA-256, truncated to 64 bits), and emits it as
`<PREFIX>_MODEL_HASH` plus a const accessible at runtime. Every future
host tool (simulator, trace decoder, supervisor) refuses to interpret a
device whose hash it does not hold. Rationale: a host tool that silently
interprets stale firmware with a newer model *lies*, which is worse than
no tool. This is the single most important integrity property of the
whole host-tooling roadmap and it costs 8 bytes of flash.

### D17 — Project-level model computes shared counts
`SM_STATE_COUNT` / `SM_EVENT_COUNT` are single compile-time constants
shared by every machine in a binary (context arrays are sized by them —
existing engine constraint, same one `multi_fsm_example` lives with
today). A `project.toml` lists the machines in the build; the generator
emits one `app_config` header with
`SM_STATE_COUNT = max(states per machine)`,
`SM_EVENT_COUNT = max(events per machine)`, and per-machine enums that
fit inside those bounds. Single-machine projects may skip `project.toml`
and generate directly from one machine file.

## Model Schema (v1)

One TOML file per machine. Complete example (blinky, plus a timeout to
show D15):

```toml
schema = 1
machine = "blinky"
prefix = "BLINKY"          # C identifier prefix for enums/artifacts
initial = "OFF"

events = ["TOGGLE", "BLINK_TICK", "PAUSE"]

[states.OFF]
entry = "on_off_entry"
execute = "on_off_execute"
exit = "on_off_exit"
on = { TOGGLE = "BLINKING" }

[states.BLINKING]
entry = "on_blinking_entry"     # arms the periodic timer
exit = "on_blinking_exit"       # disarms it
timeout = { after_ms = 60000, goto = "OFF" }   # goto REQUIRED (D15)
on.PAUSE = "PAUSED"
on.BLINK_TICK = { goto = "BLINKING", action = "action_toggle_led" }

[states.PAUSED]
entry = "on_paused_entry"
min_dwell_ms = 100
on = { TOGGLE = "BLINKING" }
```

Guard fallthrough (maps 1:1 to the engine's multi-guard search order —
first row whose guard passes wins; array order is significant):

```toml
[states.PROCESS]
on.PROCESS_DONE = [
  { if = "guard_data_valid", goto = "TRANSMIT", action = "capture" },
  { goto = "IDLE" },          # unguarded fallthrough (optional)
]
```

Schema rules enforced by the parser (before validation even starts):
- `schema = 1` mandatory; unknown top-level keys rejected.
- Every `goto` / `initial` must name a declared state; every `on.` key a
  declared event. No implicit declarations.
- `timeout` requires both `after_ms` (1..2^31-1) and `goto` (D15).
- `after_ms` / `min_dwell_ms` are integers in engine-valid ranges.
- `parent` is reserved and rejected (see Non-Goals).
- Identifier fields must be valid C identifiers.

## Generated Artifacts (per machine, under `generated/<machine>/`)

| File | Contents |
|---|---|
| `<machine>_machine.h` | State/event enums (`<PREFIX>_STATE_*`, `<PREFIX>_EVT_*`), extern callback prototypes (D14), `extern const SM_Config_t <machine>_config`, `<PREFIX>_MODEL_HASH` |
| `<machine>_machine.c` | `const SM_StateDesc_t[]`, `const SM_Transition_t[]`, `const SM_Config_t`, model-hash const. Provenance header: generator version, schema version, model hash, DO NOT EDIT |
| `<machine>_machine.md` | Mermaid stateDiagram + transition/timing tables (same renderer as graphify MACHINES.md) |
| `<machine>_symbols.json` | state/event index → name map + hash, for the Phase C trace decoder and simulator |
| `app_config_gen.h` (project-level, D17) | `SM_STATE_COUNT`, `SM_EVENT_COUNT`, feature flags the models require |

## Validation (generation-time; extends graphify V1–V5)

| ID | Check | Severity |
|---|---|---|
| V1 | State unreachable from `initial` | **ERROR** (was WARN in extraction — a modeled machine has no excuse) |
| V2 | Timeout without route | impossible by schema (D15); kept in extractor for hand-written machines |
| V3 | `min_dwell_ms` > `after_ms` | WARN (effective timeout is max(); legal but almost always a mistake in a model) |
| V4 | Terminal state | INFO (legitimate: final/halt states) |
| V5 | All transitions for (state, event) guarded | WARN (event silently droppable; add an unguarded fallthrough or accept explicitly with `allow_drop = true` on the group) |
| V6 | Unreachable transition row: unguarded row precedes guarded rows for same (state, event) | **ERROR** (later rows can never be evaluated — engine searches in order) |
| V7 | Duplicate exact rows | **ERROR** |
| V8 | Event declared but never used in any `on.` | WARN |
| V9 | Callback name referenced under two different roles with incompatible signatures (e.g. same symbol as guard and action) | **ERROR** |
| V10 | State/event count exceeds engine limits (255 / 65534) or per-project bounds (D17) | **ERROR** |

ERROR aborts generation with a nonzero exit; nothing is written. WARN
writes artifacts and reports; `--strict` promotes WARN to ERROR (CI runs
strict).

## Tooling Shape

`smgen/` package at repo root (stdlib-only, same convention as
`graphify/`):

```
smgen/
├── __init__.py      # contract + roadmap docstring
├── model.py         # schema parse (tomllib) -> Model dataclasses
├── validate.py      # V1..V10 on the Model
├── emit_c.py        # header + tables + config + hash
├── emit_docs.py     # mermaid/markdown (shared helpers with graphify)
└── __main__.py      # CLI: validate | generate | check | hash
```

CLI (`python3 -m smgen ...`):
- `validate <model.toml>` — parse + validate, report, exit code.
- `generate <model.toml> [-o generated/]` — validate then emit all
  artifacts. Deterministic output (stable ordering) so diffs are clean.
- `check <model.toml>` — regenerate to a temp dir and diff against the
  committed artifacts (drift detection, D12) **and** run the round-trip:
  graphify's extractor over the generated C must reproduce the model's
  states/rows/timeouts exactly. Both must pass. This is the CI gate.
- `hash <model.toml>` — print the canonical model hash (D16).

Round-trip is the load-bearing verification: emitter (model→C) and
extractor (C→model) are independent implementations, so agreement is
strong evidence against generator bugs. Divergence fails `check`.

## Execution Phases

### B1 — Schema + validator (no C emitted)
`model.py` + `validate.py` + `validate`/`hash` CLI. Python unit tests
for schema rejection cases and every V-check (positive + negative).
**Exit:** blinky + basic + sensor-pipeline models exist under `models/`,
validate clean; deliberately broken fixtures each trigger their intended
V-check.

### B2 — C emitters + pilot migration
`emit_c.py`, `emit_docs.py`, `generate` CLI. Migrate **blinky** first:
generate, point the example at `blinky_machine.h`/`blinky_config`,
delete its hand-written tables. Then **basic** (exercises D15 timeout).
**Exit:** both examples build and produce byte-identical stdout to their
hand-written versions; 19/19 suites green; generated .c/.h committed
with provenance headers.

### B3 — Round-trip + drift gate + remaining migrations
`check` CLI (drift + round-trip). Migrate sensor-pipeline (guard
fallthrough), error-recovery, multi-FSM (exercises D17 project.toml).
simulation-example stays hand-written deliberately — proof the
hand-written path remains first-class.
**Exit:** `smgen check` green for all models; a CMake `smgen-check`
target exists (optional to run — firmware build itself never needs
Python); README/Quick-Guide document the model workflow.

### B4 — Per-state transition index + hash API
Generator emits optional per-state index tables (offset+count into the
transition array, states sorted); engine gains
`SM_FEATURE_TRANSITION_INDEX` to use them for O(rows-from-state) lookup
— pure generated-const addition, no RAM cost, WCET per state becomes
table-provable. Add `SM_GetModelHash(sm)` (or a config field) so the
runtime can report D16's hash over any transport.
**Exit:** feature on = identical behavior across all suites (run matrix
both ways); lookup bound documented per machine in generated docs.

## Risks

| Risk | Mitigation |
|---|---|
| Generator emits subtly wrong tables | Round-trip vs independent extractor (B3); pilot migrations diffed against hand-written output (B2); full test suite both ways |
| Committed artifacts drift from models | `smgen check` in CI (B3); provenance headers make hand-edits obvious in review |
| Schema too weak for real machines | Migrating all six example machines in-phase forces the schema against every pattern the engine supports (guards, fallthrough, self-loops, timeouts, dwell, multi-instance) |
| Python 3.11 requirement (tomllib) | Tooling-only requirement; firmware build unaffected (D12). Documented in README |
| Scope creep toward the mission DSL | Hard non-goal; the DSL is a separate layer that *compiles to* this schema later |

## Relationship to the Roadmap

This is Phase B of the review roadmap (model → validator → emitters →
hash). It deliberately produces the two artifacts Phase C (ctypes
simulator, trace decoder) consumes: `<machine>_symbols.json` and the
model hash. Phase D (real HSM) extends the schema (`parent`, history)
only after the engine's hierarchical semantics are implemented —
schema-reserved, generator-rejected until then.
