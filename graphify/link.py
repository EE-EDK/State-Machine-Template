"""Cross-layer linking: the edges that tie the separate graphs together.

  bind_callbacks      machine tables (SM_StateDesc_t / SM_Transition_t /
                      SM_Error_Register*Callback calls) -> callback
                      functions, plus the *indirect* engine -> callback
                      invocation edges that a call-graph cannot see
  crosscheck_models   models/*.toml (smgen schema) vs the machine graphify
                      extracts from the matching example -- the Phase B3
                      round-trip property, checked today
  docs_xref           API symbols <-> README / Quick-Guide / CLAUDE.md
                      mentions (undocumented API, stale doc references,
                      CLAUDE.md test-count claims vs RUN_TEST reality)
  test_inventory      which test files exercise which API function
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from .analyze import Function, Graph
from .cparse import strip_noise
from .machines import Machine, TIMEOUT_NAMES

CALLBACK_ROLES = ("on_entry", "on_execute", "on_exit", "guard", "action",
                  "recovery_cb", "error_cb")
REGISTER_RE = re.compile(
    r"\bSM_Error_Register(Recovery|Notify)Callback\s*\(\s*[^,]+,\s*"
    r"([A-Za-z_]\w*)\s*\)")
RUN_TEST_RE = re.compile(r"\bRUN_TEST\s*\(\s*([A-Za-z_]\w*)\s*\)")
DOC_TOKEN_RE = re.compile(r"\b(SM_[A-Za-z0-9_]+|App_Main_\w+)\b")
DOC_FUNCLIKE_RE = re.compile(r"^SM_(?:[A-Z][a-z]\w*|[A-Za-z]+_[A-Z][a-z]\w*)$")
CLAUDE_TEST_CLAIM_RE = re.compile(r"(test_\w+\.c)\s*#\s*(\d+)\s+tests")


@dataclass
class Binding:
    machine: str        # "<file>:<config name>"
    context: str        # state name or "from --event--> to"
    role: str
    callback: str       # function key
    indirect_from: list[str] = field(default_factory=list)  # engine fn keys


def _resolve_callback(g: Graph, name: str, file: str) -> Function | None:
    cands = g.fn_by_name(name)
    for c in cands:
        if c.file == file:
            return c
    for c in cands:
        if not c.static:
            return c
    return None


def bind_callbacks(root: Path, g: Graph,
                   machines: list[Machine]) -> list[Binding]:
    invokers: dict[str, list[Function]] = {r: [] for r in CALLBACK_ROLES}
    for fn in g.functions.values():
        if fn.unit != "lib":
            continue
        for role in CALLBACK_ROLES:
            if role in fn.invokes_roles:
                invokers[role].append(fn)

    bindings: list[Binding] = []

    def add(machine: str, ctx: str, role: str, name: str, file: str) -> None:
        fn = _resolve_callback(g, name, file)
        if fn is None:
            return
        b = Binding(machine=machine, context=ctx, role=role, callback=fn.key,
                    indirect_from=sorted(i.key for i in invokers[role]))
        bindings.append(b)
        g.edges.append((f"machine:{machine}:{ctx}", fn.key, "binds",
                        {"role": role}))
        for inv in invokers[role]:
            g.edges.append((inv.key, fn.key, "invokes",
                            {"role": role, "via": machine}))

    for m in machines:
        mid = f"{m.file}:{m.name}"
        for s in m.states.values():
            for role in ("on_entry", "on_execute", "on_exit"):
                name = getattr(s, role)
                if name:
                    add(mid, s.name, role, name, m.file)
        for r in m.rows:
            ctx = f"{r.from_state} --{r.event}--> {r.to_state}"
            if r.guard:
                add(mid, ctx, "guard", r.guard, m.file)
            if r.action:
                add(mid, ctx, "action", r.action, m.file)

    # Runtime-registered callbacks (error handler)
    for rel in g.files:
        text = strip_noise((root / rel).read_text(encoding="utf-8",
                                                  errors="replace"))
        for rm in REGISTER_RE.finditer(text):
            role = "recovery_cb" if rm.group(1) == "Recovery" else "error_cb"
            add(f"{rel}:(runtime)", f"SM_Error_Register{rm.group(1)}Callback",
                role, rm.group(2), rel)
    return bindings


# ----------------------------------------------------------------------
# Models <-> examples round-trip
# ----------------------------------------------------------------------

@dataclass
class ModelCheck:
    model: str
    example: str | None
    machine: str | None
    hash: str
    diffs: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.example is not None and not self.diffs


def _strip_prefix(name: str, prefixes: tuple[str, ...]) -> str:
    for p in prefixes:
        if name.startswith(p):
            return name[len(p):]
    return name


def crosscheck_models(root: Path, machines: list[Machine]) -> list[ModelCheck]:
    models_dir = root / "models"
    out: list[ModelCheck] = []
    if not models_dir.is_dir():
        return out
    try:
        import tomllib
    except ImportError:            # Python < 3.11
        return out
    try:
        from smgen.model import parse_machine
    except Exception:               # smgen absent -- compare raw TOML
        parse_machine = None

    for path in sorted(models_dir.glob("*.toml")):
        data = tomllib.loads(path.read_text(encoding="utf-8"))
        name = data.get("machine", path.stem)
        example = f"examples/{name}_example.c"
        cands = [m for m in machines if m.file == example]
        mc = ModelCheck(model=path.relative_to(root).as_posix(),
                        example=example if cands else None,
                        machine=cands[0].name if cands else None, hash="")
        if parse_machine is not None:
            try:
                mm = parse_machine(data, source=str(path))
                mc.hash = mm.hash()
            except Exception as e:   # schema error -- report, keep going
                mc.diffs.append(f"smgen schema error: {e}")
        if not cands:
            mc.diffs.append(f"no machine extracted from {example}")
            out.append(mc)
            continue
        m = cands[0]

        # --- model side ---
        m_states = list(data["states"].keys())
        m_events = list(data["events"])
        m_rows: set[tuple] = set()
        m_cbs: dict[str, dict[str, str | None]] = {}
        m_timeout: dict[str, int] = {}
        m_dwell: dict[str, int] = {}
        for sname, st in data["states"].items():
            m_cbs[sname] = {r: st.get(r) for r in ("entry", "execute", "exit")}
            m_dwell[sname] = int(st.get("min_dwell_ms", 0))
            if "timeout" in st:
                t = st["timeout"]
                m_timeout[sname] = int(t["after_ms"])
                m_rows.add((sname, "TIMEOUT", t["goto"], None,
                            t.get("action")))
            for ev, spec in st.get("on", {}).items():
                specs = spec if isinstance(spec, list) else [spec]
                for sp in specs:
                    if isinstance(sp, str):
                        m_rows.add((sname, ev, sp, None, None))
                    else:
                        m_rows.add((sname, ev, sp["goto"], sp.get("if"),
                                    sp.get("action")))

        # --- C side (strip STATE_/EVT_ prefixes) ---
        sp = ("STATE_",)
        ep = ("EVT_",)
        c_states = {_strip_prefix(s, sp): s for s in m.states}
        c_rows = set()
        for r in m.rows:
            ev = "TIMEOUT" if r.event in TIMEOUT_NAMES else _strip_prefix(r.event, ep)
            c_rows.add((_strip_prefix(r.from_state, sp), ev,
                        _strip_prefix(r.to_state, sp), r.guard, r.action))
        for s in m_states:
            if s not in c_states:
                mc.diffs.append(f"model state `{s}` not in C table")
        for s in c_states:
            if s not in m_states:
                mc.diffs.append(f"C state `{c_states[s]}` not in model")
        for row in sorted(m_rows - c_rows, key=str):
            mc.diffs.append(f"model row {row} missing from C table")
        for row in sorted(c_rows - m_rows, key=str):
            mc.diffs.append(f"C row {row} missing from model")
        for s, cname in c_states.items():
            if s not in m_cbs:
                continue
            info = m.states[cname]
            for role, attr in (("entry", "on_entry"), ("execute", "on_execute"),
                               ("exit", "on_exit")):
                if m_cbs[s].get(role) != getattr(info, attr):
                    mc.diffs.append(
                        f"state `{s}` {role}: model={m_cbs[s].get(role)} "
                        f"C={getattr(info, attr)}")
            if m_timeout.get(s, 0) != info.timeout_ms:
                mc.diffs.append(f"state `{s}` timeout: model="
                                f"{m_timeout.get(s, 0)} C={info.timeout_ms}")
            if m_dwell.get(s, 0) != info.min_dwell_ms:
                mc.diffs.append(f"state `{s}` min_dwell: model="
                                f"{m_dwell.get(s, 0)} C={info.min_dwell_ms}")
        c_initial = _strip_prefix(m.initial_state, sp)
        if data.get("initial") != c_initial:
            mc.diffs.append(f"initial: model={data.get('initial')} "
                            f"C={m.initial_state}")
        # events declared in model but absent from the C enum universe
        c_events = {_strip_prefix(r.event, ep) for r in m.rows}
        for e in m_events:
            if e not in c_events:
                mc.diffs.append(f"model event `{e}` never appears in a C row")
        out.append(mc)
    return out


# ----------------------------------------------------------------------
# Docs <-> API
# ----------------------------------------------------------------------

@dataclass
class DocsReport:
    docs: list[str]
    mentions: dict[str, dict[str, int]]         # symbol -> doc -> count
    undocumented_api: list[str]                 # API fns with 0 user-doc hits
    stale: dict[str, list[str]]                 # doc -> unknown SM_ tokens
    test_claims: list[tuple[str, int, int]]     # (file, claimed, actual)
    user_docs: tuple[str, ...] = ("README.md", "Quick-Guide.md")


def docs_xref(root: Path, g: Graph,
              run_test_counts: dict[str, int]) -> DocsReport:
    doc_paths = [p for p in ("README.md", "Quick-Guide.md", "MIGRATION.md",
                             "CLAUDE.md", "AGENTS.md", "GEMINI.md", "GROK.md")
                 if (root / p).is_file()]
    doc_paths += sorted(p.relative_to(root).as_posix()
                        for p in (root / "docs_dev").glob("*.md")) \
        if (root / "docs_dev").is_dir() else []

    known = set(g.decls) | {f.name for f in g.functions.values()} | \
        set(g.macros) | set(g.types) | set(g.configs)
    for t in g.types.values():
        known |= set(t.enumerators)
    api = sorted({f.name for f in g.api_functions()
                  if f.name.startswith(("SM_", "App_"))})

    mentions: dict[str, dict[str, int]] = {}
    stale: dict[str, list[str]] = {}
    claims: list[tuple[str, int, int]] = []
    for rel in doc_paths:
        text = (root / rel).read_text(encoding="utf-8", errors="replace")
        toks = DOC_TOKEN_RE.findall(text)
        for tok in toks:
            mentions.setdefault(tok, {}).setdefault(rel, 0)
            mentions[tok][rel] += 1
        if rel in ("README.md", "Quick-Guide.md", "CLAUDE.md"):
            unknown = sorted({t for t in toks
                              if t not in known and not t.endswith("_")
                              and DOC_FUNCLIKE_RE.match(t)})
            if unknown:
                stale[rel] = unknown
        if rel == "CLAUDE.md":
            for fname, n in CLAUDE_TEST_CLAIM_RE.findall(text):
                actual = run_test_counts.get(f"tests/{fname}", -1)
                claims.append((fname, int(n), actual))

    user_docs = ("README.md", "Quick-Guide.md")
    # A trailing underscore marks an internal entry point in this codebase
    # (SM_TimeEvt_Tick_, SM_Init_): the documented surface is the macro or
    # wrapper that calls it, so absence from the user docs is by design.
    undocumented = [name for name in api
                    if not name.endswith("_")
                    and not any(mentions.get(name, {}).get(d, 0)
                                for d in user_docs)]
    return DocsReport(docs=doc_paths, mentions=mentions,
                      undocumented_api=undocumented, stale=stale,
                      test_claims=claims, user_docs=user_docs)


# ----------------------------------------------------------------------
# Tests <-> API
# ----------------------------------------------------------------------

@dataclass
class TestInventory:
    run_tests: dict[str, int]                      # test file -> RUN_TEST count
    api_test_files: dict[str, set[str]]            # API fn name -> test files
    api_example_files: dict[str, set[str]]         # API fn name -> example files
    uncovered_api: list[str]                       # API fns no test calls


def test_inventory(root: Path, g: Graph) -> TestInventory:
    run_tests: dict[str, int] = {}
    for rel in g.files:
        if rel.startswith("tests/") and rel.endswith(".c"):
            text = (root / rel).read_text(encoding="utf-8", errors="replace")
            run_tests[rel] = len(RUN_TEST_RE.findall(text))

    api = g.api_functions()
    api_names = {f.name for f in api}
    by_name_test: dict[str, set[str]] = {n: set() for n in api_names}
    by_name_ex: dict[str, set[str]] = {n: set() for n in api_names}

    def macro_reaches(macro: str, seen: set[str]) -> set[str]:
        """API names a function-like macro resolves to, transitively."""
        out: set[str] = set()
        if macro in seen or macro not in g.macros:
            return out
        seen.add(macro)
        for callee in g.macros[macro].calls:
            if callee in api_names:
                out.add(callee)
            if callee in g.macros:
                out |= macro_reaches(callee, seen)
        return out

    for fn in g.functions.values():
        reached = set()
        for macro in fn.expands:
            reached |= macro_reaches(macro, set())
        for name in reached:
            if fn.unit == "tests":
                by_name_test[name].add(fn.file)
            elif fn.unit.startswith("ex:") or fn.unit == "stm32":
                by_name_ex[name].add(fn.file)
        for callee_key in fn.calls:
            callee = g.functions.get(callee_key)
            if callee is None:
                # decl-only target
                if callee_key.startswith("decl:"):
                    name = callee_key[5:]
                else:
                    continue
            else:
                name = callee.name
            if name not in api_names:
                continue
            if fn.unit == "tests":
                by_name_test[name].add(fn.file)
            elif fn.unit.startswith("ex:") or fn.unit == "stm32":
                by_name_ex[name].add(fn.file)
    # A trailing underscore marks an internal entry point (SM_TimeEvt_Tick_,
    # SM_Init_): exercised through the public wrapper, not called directly.
    uncovered = sorted(n for n in api_names
                       if not by_name_test[n] and not n.endswith("_"))
    return TestInventory(run_tests=run_tests, api_test_files=by_name_test,
                         api_example_files=by_name_ex, uncovered_api=uncovered)


# ----------------------------------------------------------------------
# Library <-> application compile-time configuration (ABI) consistency
# ----------------------------------------------------------------------

LAYOUT_MACROS = ("SM_STATE_COUNT", "SM_EVENT_COUNT", "SM_EVENT_QUEUE_SIZE",
                 "SM_ERROR_HISTORY_SIZE", "SM_STATE_HISTORY_DEPTH",
                 "SM_MAX_TRANSITIONS", "SM_DEFER_QUEUE_SIZE",
                 "SM_FEATURE_HSM", "SM_FEATURE_RUNTIME_TRANSITIONS",
                 "SM_FEATURE_STATISTICS", "SM_FEATURE_TIME_EVENTS",
                 "SM_FEATURE_DEFER")
CMAKE_DEFS_RE = re.compile(
    r"target_compile_definitions\(\s*sm_framework\s+PRIVATE([^)]*)\)", re.S)
CMAKE_KV_RE = re.compile(r"(SM_\w+)=\(?(\d+)U?\)?")
APP_DEF_RE = re.compile(r"^\s*#\s*define\s+(SM_\w+)\s+\(?\s*(\d+)U?\s*\)?",
                        re.M)


@dataclass
class AbiIssue:
    severity: str
    file: str
    message: str


def abi_check(root: Path, g: Graph) -> list[AbiIssue]:
    """Compare the values the prebuilt library was compiled with against
    the values each example translation unit defines before including the
    framework. SM_STATE_COUNT / SM_EVENT_COUNT are baked into the library
    (range checks, SM_EVT_TIMEOUT) and the other macros change the
    SM_Context_t layout, so any divergence is a real ABI defect."""
    cm = root / "CMakeLists.txt"
    if not cm.is_file():
        return []
    m = CMAKE_DEFS_RE.search(cm.read_text(encoding="utf-8", errors="replace"))
    if not m:
        return []
    lib: dict[str, int] = {k: int(v) for k, v in CMAKE_KV_RE.findall(m.group(1))}
    for name, c in g.configs.items():
        if name in LAYOUT_MACROS and name not in lib:
            try:
                lib[name] = int(c.default, 0)
            except ValueError:
                pass
    out: list[AbiIssue] = []
    for rel in g.files:
        if not (rel.startswith("examples/") and rel.endswith(".c")) or \
                rel.startswith("examples/platform/"):
            continue
        text = (root / rel).read_text(encoding="utf-8", errors="replace")
        app = {k: int(v) for k, v in APP_DEF_RE.findall(text)}
        uses_timeout = "SM_EVT_TIMEOUT" in strip_noise(text)
        for name in LAYOUT_MACROS:
            if name not in app or name not in lib:
                continue
            a, l = app[name], lib[name]
            if a == l:
                continue
            if name == "SM_STATE_COUNT":
                if a > l:
                    out.append(AbiIssue("ERROR", rel,
                        f"declares {a} states but the library was built with "
                        f"SM_STATE_COUNT={l}: states >= {l} fail SM_Init "
                        f"(SM_REQUIRE 104) or sm_get_state_desc"))
                else:
                    out.append(AbiIssue("INFO", rel,
                        f"SM_STATE_COUNT={a} < library {l}: library range "
                        f"checks are looser than the app's enum"))
            elif name == "SM_EVENT_COUNT":
                msg = (f"SM_EVENT_COUNT={a} but the library was built with "
                       f"{l}: SM_PostEvent accepts ids < {l}, and the engine "
                       f"posts SM_EVT_TIMEOUT={l} while this TU's tables use "
                       f"SM_EVT_TIMEOUT={a}")
                if uses_timeout:
                    out.append(AbiIssue("ERROR", rel, msg +
                        " -- every SM_EVT_TIMEOUT route here is dead"))
                else:
                    out.append(AbiIssue("WARN", rel, msg))
            else:
                out.append(AbiIssue("ERROR", rel,
                    f"{name}={a} differs from the library's {l}: "
                    f"SM_Context_t layout differs between app and library"))
    return out
