"""Report rendering: GRAPH_REPORT.md, wiki/index.md, graph.json, graph.html.

Everything here is presentation. The numbers come from analyze.py /
link.py / metrics.py; this module only formats them deterministically.
"""

from __future__ import annotations

import json
from collections import defaultdict
from dataclasses import dataclass, field

from .analyze import Graph, Function
from .link import Binding, DocsReport, ModelCheck, TestInventory
from .machines import Machine
from .metrics import (Adj, articulation_points, betweenness, bowtie,
                      clustering, label_propagation, layers, modularity,
                      pagerank, scc, undirected)
from .pytools import PyGraph

GOD_NODE_COUNT = 15
ROLE_ORDER = ("on_entry", "on_execute", "on_exit", "guard", "action",
              "recovery_cb", "error_cb")


@dataclass
class Finding:
    severity: str     # ERROR | WARN | INFO
    check: str
    message: str


@dataclass
class Analysis:
    """Everything the renderers need, computed once."""
    g: Graph
    machines: list[Machine]
    bindings: list[Binding]
    model_checks: list[ModelCheck]
    docs: DocsReport
    tests: TestInventory
    pyg: PyGraph
    adj: Adj = field(default_factory=dict)            # direct calls
    adj_full: Adj = field(default_factory=dict)       # + macro expansion + invokes
    bc: dict[str, float] = field(default_factory=dict)
    pr: dict[str, float] = field(default_factory=dict)
    sccs: list[list[str]] = field(default_factory=list)
    aps: set[str] = field(default_factory=set)
    lp: dict[str, int] = field(default_factory=dict)
    lp_lib: dict[str, int] = field(default_factory=dict)
    q_lib: float = 0.0
    layer: dict[str, int] = field(default_factory=dict)
    q_dir: float = 0.0
    q_lp: float = 0.0
    cc: float = 0.0
    tie: dict[str, set[str]] = field(default_factory=dict)
    test_defs: dict[str, str] = field(default_factory=dict)
    findings: list[Finding] = field(default_factory=list)


def short(key: str) -> str:
    return key.split("@")[0].replace("decl:", "").replace("macro:", "")


def _file_of(key: str) -> str:
    return key.split("@")[1] if "@" in key else ""


def analyze(g: Graph, machines: list[Machine], bindings: list[Binding],
            model_checks: list[ModelCheck], docs: DocsReport,
            tests: TestInventory, pyg: PyGraph,
            test_defs: dict[str, str] | None = None) -> Analysis:
    a = Analysis(g=g, machines=machines, bindings=bindings,
                 model_checks=model_checks, docs=docs, tests=tests, pyg=pyg,
                 test_defs=dict(test_defs or {}))

    # Direct call graph over function definitions (decl targets dropped)
    adj: Adj = {k: set() for k in g.functions}
    for k, fn in g.functions.items():
        for c in fn.calls:
            if c in g.functions:
                adj[k].add(c)
    a.adj = adj

    # Full graph: + macro expansion reaching functions, + indirect invokes
    full: Adj = {k: set(v) for k, v in adj.items()}
    by_name: dict[str, list[Function]] = defaultdict(list)
    for fn in g.functions.values():
        by_name[fn.name].append(fn)

    def macro_targets(mname: str, seen: set[str]) -> set[str]:
        out: set[str] = set()
        if mname in seen or mname not in g.macros:
            return out
        seen.add(mname)
        for callee in g.macros[mname].calls:
            lib = [f for f in by_name.get(callee, []) if f.unit == "lib"]
            if lib:
                out.add(lib[0].key)
            if callee in g.macros:
                out |= macro_targets(callee, seen)
        return out

    for k, fn in g.functions.items():
        for m in fn.expands:
            full[k] |= macro_targets(m, set())
    for src, dst, kind, _ in g.edges:
        if kind == "invokes" and src in full and dst in full:
            full[src].add(dst)
    a.adj_full = full

    a.bc = betweenness(full)
    a.pr = pagerank(full)
    a.sccs = [c for c in scc(full) if len(c) > 1]
    u = undirected(full)
    a.aps = articulation_points(u)
    a.lp = label_propagation(u)
    lib_nodes = {k for k in full if g.functions[k].unit == "lib"}
    lib_adj = {k: {t for t in full[k] if t in lib_nodes} for k in lib_nodes}
    ulib = undirected(lib_adj)
    a.lp_lib = label_propagation(ulib)
    a.q_lib = modularity(ulib, a.lp_lib)
    a.layer = layers(full)
    a.cc = clustering(u)
    dir_part = {k: g.community_of(_file_of(k)) for k in full}
    a.q_dir = modularity(u, dir_part)
    a.q_lp = modularity(u, a.lp)
    waist = {f.key for f in g.api_functions()
             if f.file.startswith("src/core/")}
    a.tie = bowtie(full, waist)
    a.findings = _validate(a)
    return a


# ----------------------------------------------------------------------
# Graph-level validators (G-checks) -- facts, not opinions
# ----------------------------------------------------------------------

def _validate(a: Analysis) -> list[Finding]:
    g = a.g
    out: list[Finding] = []

    # G1 duplicate assertion IDs within a module (BOUNDED_LOOP pairs share by design)
    seen: dict[tuple[str, int], list] = defaultdict(list)
    for asr in g.assertions:
        if asr.macro == "SM_BOUNDED_LOOP_END":
            continue
        seen[(asr.module, asr.id)].append(asr)
    for (mod, aid), sites in sorted(seen.items()):
        if len(sites) > 1 and mod and \
                all(x.file.startswith("src/") for x in sites):
            locs = ", ".join(f"{s.file}:{s.line}" for s in sites)
            out.append(Finding("WARN", "G1-duplicate-assert-id",
                               f"`{mod}` id {aid} used at {locs}"))
    # G2 function mixes assertion-ID hundreds blocks
    by_fn: dict[str, set[int]] = defaultdict(set)
    for asr in g.assertions:
        by_fn[asr.function].add(asr.id // 100)
    for fk, blocks in sorted(by_fn.items()):
        if len(blocks) > 1:
            out.append(Finding("INFO", "G2-assert-block-mix",
                               f"`{short(fk)}` uses ID blocks "
                               f"{sorted(b * 100 for b in blocks)}"))
    # G3 core source file without SM_DEFINE_MODULE / without any assertion
    for rel in g.files:
        if rel.startswith("src/core/") and rel.endswith(".c"):
            if rel not in g.modules:
                out.append(Finding("INFO", "G3-no-module",
                                   f"`{rel}` has no SM_DEFINE_MODULE "
                                   f"(no numeric assertions possible)"))
    # G4 unbalanced critical sections (textual count)
    for fn in g.functions.values():
        if fn.unit == "lib" and fn.crit_enter != fn.crit_exit:
            out.append(Finding("WARN", "G4-critsec-unbalanced",
                               f"`{short(fn.key)}` enters {fn.crit_enter}x, "
                               f"exits {fn.crit_exit}x"))
    # G5 documented ISR-safe function calls a documented NOT-ISR-safe one
    for fn in g.functions.values():
        if fn.isr != "safe":
            continue
        for c in a.adj_full.get(fn.key, ()):
            cf = g.functions[c]
            if cf.isr == "unsafe":
                out.append(Finding("WARN", "G5-isr-contract",
                                   f"ISR-safe `{short(fn.key)}` calls "
                                   f"NOT-ISR-safe `{short(c)}`"))
    # G6 call cycles (recursion / RTC re-entry paths)
    for comp in a.sccs:
        names = ", ".join(short(k) for k in comp[:8])
        more = f" (+{len(comp) - 8})" if len(comp) > 8 else ""
        out.append(Finding("INFO", "G6-call-cycle",
                           f"{len(comp)}-node cycle: {names}{more}"))
    # G7 public API function with no test caller
    for name in a.tests.uncovered_api:
        out.append(Finding("WARN", "G7-untested-api",
                           f"`{name}` is never called from tests/"))
    # G8 undocumented API / stale doc references / test-count claims
    for name in a.docs.undocumented_api:
        out.append(Finding("INFO", "G8-undocumented-api",
                           f"`{name}` not mentioned in "
                           f"{' or '.join(a.docs.user_docs)}"))
    for doc, toks in sorted(a.docs.stale.items()):
        out.append(Finding("WARN", "G8-stale-doc-symbol",
                           f"{doc} references unknown symbol(s): "
                           f"{', '.join(f'`{t}`' for t in toks)}"))
    for fname, claimed, actual in a.docs.test_claims:
        if actual >= 0 and claimed != actual:
            out.append(Finding("WARN", "G8-test-count-claim",
                               f"CLAUDE.md says `{fname}` has {claimed} "
                               f"tests; RUN_TEST count is {actual}"))
    # G9 model <-> example round-trip
    for mc in a.model_checks:
        if not mc.ok:
            for d in mc.diffs:
                out.append(Finding("WARN", "G9-model-roundtrip",
                                   f"{mc.model}: {d}"))
    # G10 feature-gated code with no test configuration exercising it
    gated = defaultdict(list)
    for fn in g.functions.values():
        if fn.unit == "lib":
            for cond in fn.gate:
                if cond.startswith("SM_FEATURE_") and " " not in cond:
                    gated[cond].append(fn.name)
    test_cfg = a.test_defs
    for fn in g.functions.values():
        if fn.unit == "lib":
            for c in fn.uses_config:
                if c.startswith("SM_FEATURE_") and c not in fn.gate:
                    gated[c].append(fn.name + " (region)")
    for cond, fns in sorted(gated.items()):
        val = test_cfg.get(cond)
        if val is not None and val.rstrip("Uu") in ("0", "(0)"):
            out.append(Finding("WARN", "G10-feature-untested",
                               f"{cond}=0 in tests/CMakeLists.txt; never "
                               f"compiled under test: "
                               f"{', '.join(sorted(set(fns)))}"))
    # G14 volatile field written outside any critical section (library)
    for fn in sorted(g.functions.values(), key=lambda f: (f.file, f.line)):
        if fn.unit == "lib" and fn.unprotected_volatile:
            out.append(Finding("INFO", "G14-volatile-write-unprotected",
                               f"`{short(fn.key)}` writes volatile "
                               f"{', '.join(f'`{r}`' for r in sorted(fn.unprotected_volatile))} "
                               f"outside a critical section"))
    # G11 callback bound in a table but never invoked by any engine path
    for b in a.bindings:
        if not b.indirect_from:
            out.append(Finding("INFO", "G11-unreached-callback",
                               f"`{short(b.callback)}` bound as {b.role} "
                               f"but no library function invokes that role"))
    # G12 weak HAL default with no strong override anywhere in-repo
    for d in g.decls.values():
        impls = [g.functions[k] for k in d.implementations]
        if any(f.weak for f in impls) and not any(f.overrides for f in impls):
            out.append(Finding("INFO", "G12-never-overridden",
                               f"weak `{d.name}` has no strong override "
                               f"in tests/ or examples/"))
    # G13 articulation points inside the library (single points of failure
    # for connectivity -- informative, not a defect)
    lib_aps = sorted(k for k in a.aps if _file_of(k).startswith("src/"))
    if lib_aps:
        out.append(Finding("INFO", "G13-articulation",
                           f"library articulation points: "
                           f"{', '.join(short(k) for k in lib_aps[:10])}"))
    return out


# ----------------------------------------------------------------------
# GRAPH_REPORT.md
# ----------------------------------------------------------------------

def _gate_str(gate: tuple[str, ...]) -> str:
    return " && ".join(gate) if gate else "-"


def render_report(a: Analysis) -> str:
    g = a.g
    L: list[str] = []
    w = L.append
    w("# Graph Report")
    w("")
    w("Generated by the repo-local `graphify` package "
      "(`python3 -m graphify.watch`). Regenerate after code changes; do not "
      "edit by hand. Machine-level graph: [MACHINES.md](MACHINES.md). "
      "Interactive view: `graph.html` (self-contained). Raw data: "
      "`graph.json`.")
    w("")

    # ---- Summary
    n_edges = sum(len(v) for v in a.adj.values())
    n_full = sum(len(v) for v in a.adj_full.values())
    n_binds = sum(1 for e in g.edges if e[2] == "binds")
    n_inv = sum(1 for e in g.edges if e[2] == "invokes")
    n_access = sum(len(f.reads) + len(f.writes) for f in g.functions.values())
    w("## Summary")
    w("")
    w(f"- Files scanned: **{len(g.files)}** C/H + "
      f"{len(a.pyg.modules)} Python modules + {len(a.docs.docs)} docs + "
      f"{len(a.model_checks)} models")
    w(f"- Nodes: **{len(g.functions)}** functions, {len(g.decls)} "
      f"declarations, {len(g.macros)} macros "
      f"({sum(len(m.variants) for m in g.macros.values())} variants), "
      f"{len(g.types)} types "
      f"({sum(len(t.fields) for t in g.types.values())} fields), "
      f"{len(g.configs)} config macros, {len(g.assertions)} assertion sites, "
      f"{len(a.machines)} machines, {len(a.pyg.funcs)} Python functions")
    w(f"- Edges: **{n_edges}** direct calls; {n_full} with macro expansion "
      f"+ indirect callback invocation; {n_binds} table bindings; "
      f"{n_inv} engine→callback invocations; {n_access} field accesses; "
      f"{sum(len(v) for v in g.includes.values())} includes")
    w(f"- Communities: {len({g.community_of(f) for f in g.files})} by "
      f"directory (modularity Q={a.q_dir:.3f}); "
      f"{len(set(a.lp.values()))} structural (label propagation, "
      f"Q={a.q_lp:.3f}); library-only subgraph: "
      f"{len(set(a.lp_lib.values()))} structural (Q={a.q_lib:.3f}); "
      f"mean clustering coefficient {a.cc:.3f}")
    w("")

    # ---- Findings summary
    sev = defaultdict(int)
    for f in a.findings:
        sev[f.severity] += 1
    w("## Validator findings (graph-level G-checks)")
    w("")
    w(f"**{sev['ERROR']} ERROR**, **{sev['WARN']} WARN**, {sev['INFO']} INFO. "
      f"Machine-level V-checks are in MACHINES.md.")
    w("")
    for f in a.findings:
        w(f"- **{f.severity}** ({f.check}): {f.message}")
    w("")

    # ---- Topology
    w("## Topology")
    w("")
    w("Computed on the full graph (direct calls + macro expansion + "
      "engine→callback invocation), so the re-entrant paths a "
      "callback→`SM_PostEvent`→engine cycle creates are visible.")
    w("")
    tie = a.tie
    w(f"- **Bow-tie** around the core API waist "
      f"({len(tie['waist'])} public `src/core` functions): "
      f"IN = {len(tie['in'])} (callers that reach the waist: tests, "
      f"examples, callbacks), OUT = {len(tie['out'])} (everything the waist "
      f"reaches: engine internals, HAL), tubes/tendrils = "
      f"{len(tie['tubes'])}, disconnected = {len(tie['disconnected'])}")
    ly = defaultdict(int)
    for k, d in a.layer.items():
        ly[d] += 1
    w(f"- **Layers** (longest path to a sink; sinks = leaf HAL/helpers at 0):")
    for d in sorted(ly):
        sample = sorted((k for k, v in a.layer.items() if v == d),
                        key=lambda k: (-len(a.adj_full[k]), k))[:4]
        w(f"  - L{d}: {ly[d]} nodes — e.g. "
          f"{', '.join(f'`{short(k)}`' for k in sample)}")
    w(f"- **Cycles (SCCs > 1)**: {len(a.sccs)}")
    for comp in a.sccs[:6]:
        w(f"  - {len(comp)} nodes: "
          f"{', '.join(f'`{short(k)}`' for k in comp[:10])}"
          f"{' …' if len(comp) > 10 else ''}")
    lib_aps = sorted(k for k in a.aps if _file_of(k).startswith("src/"))
    w(f"- **Articulation points** (removing one disconnects the undirected "
      f"graph): {len(a.aps)} total, {len(lib_aps)} in the library: "
      f"{', '.join(f'`{short(k)}`' for k in lib_aps[:12])}")
    # degree sequence signature
    degs = sorted((len(a.adj_full[k]) + sum(1 for v in a.adj_full.values() if k in v))
                  for k in a.adj_full)
    top = degs[-5:][::-1]
    w(f"- **Degree signature**: max {top[0]}, top-5 {top}, median "
      f"{degs[len(degs)//2]}, isolated {sum(1 for d in degs if d == 0)}")
    w("")

    # ---- God nodes
    w("## God nodes (full graph)")
    w("")
    w("Duplicate-name inflation from the old extractor is gone: each call is "
      "resolved to one definition (static in-file, then same link unit, "
      "then the library default).")
    w("")
    w("| Function | File | In | Out | Betweenness | PageRank | Layer | Struct. comm. |")
    w("|---|---|---:|---:|---:|---:|---:|---:|")
    indeg = defaultdict(int)
    for k, v in a.adj_full.items():
        for t in v:
            indeg[t] += 1
    ranked = sorted(g.functions.values(),
                    key=lambda f: (-(indeg[f.key] + len(a.adj_full[f.key])),
                                   f.name))[:GOD_NODE_COUNT]
    for f in ranked:
        w(f"| `{f.name}` | `{f.file}` | {indeg[f.key]} | "
          f"{len(a.adj_full[f.key])} | {a.bc[f.key]:.0f} | "
          f"{a.pr[f.key]*1000:.1f}‰ | L{a.layer[f.key]} | "
          f"{a.lp[f.key]} |")
    w("")

    # ---- Interface layer
    w("## Interface layer (declarations → implementations)")
    w("")
    w("| Declared | Implementations (unit) | ISR contract |")
    w("|---|---|---|")
    for name, d in sorted(g.decls.items()):
        impls = []
        for k in d.implementations:
            f = g.functions[k]
            tag = f.unit + (" weak" if f.weak else "") + \
                (" override" if f.overrides else "")
            impls.append(f"`{f.file}` ({tag})")
        if not impls:
            impls = ["*none in scope*"]
        w(f"| `{name}` | {'<br>'.join(impls)} | {d.isr or '-'} |")
    w("")

    # ---- Feature gates
    w("## Feature gates")
    w("")
    w("What each compile-time flag adds or removes (library scope).")
    w("")
    gate_fns: dict[str, set[str]] = defaultdict(set)
    gate_fields: dict[str, set[str]] = defaultdict(set)
    gate_macros: dict[str, set[str]] = defaultdict(set)
    for f in g.functions.values():
        if f.unit == "lib":
            for c in f.gate:
                gate_fns[c].add(f.name)
    for t in g.types.values():
        for fl in t.fields.values():
            for c in fl.gate:
                gate_fields[c].add(f"{t.name}.{fl.name}")
        for c in t.gate:
            gate_fields[c].add(f"{t.name} (type)")
    for m in g.macros.values():
        for v in m.variants:
            for c in v.gate:
                gate_macros[c].add(m.name)
    for cond in sorted(set(gate_fns) | set(gate_fields) | set(gate_macros)):
        if not (gate_fns[cond] or gate_fields[cond]) and \
                "SM_FEATURE" not in cond and "SM_DEBUG_LEVEL" not in cond:
            continue
        w(f"- `{cond}`: {len(gate_fns[cond])} functions, "
          f"{len(gate_fields[cond])} fields/types, "
          f"{len(gate_macros[cond])} macros")
        if gate_fns[cond]:
            w(f"  - functions: {', '.join(f'`{x}`' for x in sorted(gate_fns[cond])[:12])}"
              f"{' …' if len(gate_fns[cond]) > 12 else ''}")
        if gate_fields[cond]:
            w(f"  - fields: {', '.join(f'`{x}`' for x in sorted(gate_fields[cond]))}")
    w("")

    # ---- Config macros
    w("## Configuration macros (override pattern)")
    w("")
    w("| Macro | Default | Used by |")
    w("|---|---|---|")
    for name, c in sorted(g.configs.items()):
        users = sorted(short(u) for u in c.users)
        w(f"| `{name}` | `{c.default}` | {len(users)}: "
          f"{', '.join(f'`{u}`' for u in users[:8])}"
          f"{' …' if len(users) > 8 else ''} |")
    w("")

    # ---- Field access matrix
    w("## State access matrix (`SM_Context` and sub-structs)")
    w("")
    w("Who writes each field — the invariant surface. Readers counted; "
      "library functions only.")
    w("")
    w("| Field | Writers | Readers |")
    w("|---|---|---:|")
    writers: dict[str, set[str]] = defaultdict(set)
    readers: dict[str, set[str]] = defaultdict(set)
    for f in g.functions.values():
        if f.unit != "lib":
            continue
        for r in f.writes:
            writers[r].add(f.name)
        for r in f.reads:
            readers[r].add(f.name)
    order = []
    for tname in ("SM_Context", "SM_EventQueue_t", "SM_ErrorHandler_t",
                  "SM_TimeEvt_t", "SM_Stats_t", "SM_ErrorInfo_t",
                  "SM_ErrorStats_t"):
        t = g.types.get(tname)
        if t:
            order += [f"{tname}.{fl}" for fl in t.fields]
    for ref in order:
        if ref in writers or ref in readers:
            vol = ""
            tname, fname = ref.split(".")
            if g.types[tname].fields[fname].volatile:
                vol = " *(volatile)*"
            w(f"| `{ref}`{vol} | {', '.join(f'`{x}`' for x in sorted(writers[ref])) or '-'} "
              f"| {len(readers[ref])} |")
    w("")

    # ---- Critical sections
    w("## Critical sections")
    w("")
    w("| Function | Enter/Exit | Calls made while interrupts masked | ISR contract |")
    w("|---|---:|---|---|")
    for f in sorted(g.functions.values(), key=lambda f: (f.file, f.line)):
        if f.unit == "lib" and (f.crit_enter or f.crit_exit):
            w(f"| `{f.name}` | {f.crit_enter}/{f.crit_exit} | "
              f"{', '.join(f'`{c}`' for c in sorted(f.in_critsec_calls)) or '-'} "
              f"| {f.isr or '-'} |")
    w("")
    w("Documented ISR contracts (from header/definition doc comments):")
    w("")
    for f in sorted(g.functions.values(), key=lambda f: f.name):
        if f.unit == "lib" and f.isr:
            lib_c = sorted({short(c) for c in a.adj_full[f.key]
                            if g.functions[c].unit == "lib"})
            cbs = sum(1 for c in a.adj_full[f.key]
                      if g.functions[c].unit != "lib")
            w(f"- `{f.name}`: **{f.isr}** — calls "
              f"{', '.join(f'`{c}`' for c in lib_c) or 'nothing'}"
              + (f" + {cbs} application callbacks" if cbs else ""))
    w("")
    w("Volatile fields written outside any critical section (library):")
    w("")
    for f in sorted(g.functions.values(), key=lambda f: (f.file, f.line)):
        if f.unit == "lib" and f.unprotected_volatile:
            w(f"- `{f.name}`: {', '.join(f'`{r}`' for r in sorted(f.unprotected_volatile))}")
    w("")

    # ---- Assertions
    w("## Assertion map (numeric IDs)")
    w("")
    w("| Module | ID | Macro | Function | Site | Expression |")
    w("|---|---:|---|---|---|---|")
    for asr in sorted(g.assertions, key=lambda x: (x.module, x.id, x.line)):
        w(f"| {asr.module or '-'} | {asr.id} | {asr.macro} | "
          f"`{short(asr.function)}` | {asr.file}:{asr.line} | "
          f"`{asr.expr[:60]}` |")
    w("")

    # ---- Macro expansion
    w("## Macro expansion map")
    w("")
    w("Function-like macros, each variant with its gate and what it "
      "expands to (transitively resolved to library functions).")
    w("")
    for name, m in sorted(g.macros.items()):
        if not m.function_like or not (m.users or m.calls):
            continue
        w(f"- `{name}` — used by {len(m.users)}")
        for v in m.variants:
            calls = sorted(v.calls)
            w(f"  - `{_gate_str(v.gate)}` → "
              f"{', '.join(f'`{c}`' for c in calls) if calls else ('no-op' if v.noop else '`' + v.body[:50] + '`')}")
    w("")

    # ---- Callback bindings
    w("## Machine ↔ code bindings")
    w("")
    w("Callbacks bound in transition/descriptor tables (and runtime "
      "registrations), with the engine functions that invoke them "
      "indirectly. Application machines only; test fixtures counted.")
    w("")
    app_b = [b for b in a.bindings if not b.machine.startswith("tests/")]
    fix_b = [b for b in a.bindings if b.machine.startswith("tests/")]
    by_machine: dict[str, list[Binding]] = defaultdict(list)
    for b in app_b:
        by_machine[b.machine].append(b)
    for mid, bs in sorted(by_machine.items()):
        w(f"### `{mid}`")
        w("")
        w("| Context | Role | Callback | Invoked by |")
        w("|---|---|---|---|")
        for b in sorted(bs, key=lambda b: (b.context, ROLE_ORDER.index(b.role))):
            w(f"| {b.context} | {b.role} | `{short(b.callback)}` | "
              f"{', '.join(f'`{short(k)}`' for k in b.indirect_from) or '-'} |")
        w("")
    w(f"Test fixtures: {len(fix_b)} bindings across "
      f"{len({b.machine for b in fix_b})} fixture machines.")
    w("")

    # ---- Tests
    w("## Test inventory and API coverage")
    w("")
    total = sum(a.tests.run_tests.values())
    w(f"**{total} RUN_TEST cases** across {len(a.tests.run_tests)} suites.")
    w("")
    w("| Suite | RUN_TEST | API functions exercised |")
    w("|---|---:|---:|")
    for rel, n in sorted(a.tests.run_tests.items()):
        used = sum(1 for name, files in a.tests.api_test_files.items()
                   if rel in files)
        w(f"| `{rel}` | {n if n else '(support)'} | {used} |")
    w("")
    w("| API function | Test files | Example files |")
    w("|---|---:|---:|")
    for f in g.api_functions():
        if not f.name.startswith(("SM_", "App_")):
            continue
        t = a.tests.api_test_files.get(f.name, set())
        e = a.tests.api_example_files.get(f.name, set())
        flag = " ⚠" if not t else ""
        w(f"| `{f.name}`{flag} | {len(t)} | {len(e)} |")
    w("")

    # ---- Docs
    w("## Documentation cross-reference")
    w("")
    w(f"Scanned: {', '.join(f'`{d}`' for d in a.docs.docs)}.")
    w("")
    if a.docs.undocumented_api:
        w(f"- API functions absent from {' and '.join(a.docs.user_docs)}: "
          f"{', '.join(f'`{n}`' for n in a.docs.undocumented_api)}")
    for doc, toks in sorted(a.docs.stale.items()):
        w(f"- `{doc}` references symbols that do not exist in the code: "
          f"{', '.join(f'`{t}`' for t in toks)}")
    if a.docs.test_claims:
        w("- CLAUDE.md test-count claims vs RUN_TEST: " +
          ", ".join(f"`{f}` {c}→{act}" + ("" if c == act else " ✗")
                    for f, c, act in a.docs.test_claims))
    w("")

    # ---- Models
    w("## Models ↔ examples (round-trip preview for Phase B3)")
    w("")
    for mc in a.model_checks:
        status = "MATCH" if mc.ok else f"{len(mc.diffs)} difference(s)"
        w(f"- `{mc.model}` (hash `{mc.hash or '?'}`) ↔ "
          f"`{mc.example or '?'}`: **{status}**")
        for d in mc.diffs:
            w(f"  - {d}")
    w("")

    # ---- Python tooling
    w("## Host tooling (Python)")
    w("")
    for mod, pm in sorted(a.pyg.modules.items()):
        imps = sorted(i for i in pm.imports if i.startswith(("smgen", "graphify")))
        w(f"- `{mod}` ({pm.lines} lines, {len(pm.funcs)} functions)"
          + (f" → imports {', '.join(f'`{i}`' for i in imps)}" if imps else ""))
    w("")

    # ---- Community structure (directory)
    w("## Community structure (by directory)")
    w("")
    by_comm: dict[str, list[str]] = defaultdict(list)
    for rel in g.files:
        by_comm[g.community_of(rel)].append(rel)
    internal: dict[str, int] = defaultdict(int)
    cross: dict[tuple[str, str], int] = defaultdict(int)
    for k, v in a.adj.items():
        src = g.community_of(_file_of(k))
        for t in v:
            dst = g.community_of(_file_of(t))
            if src == dst:
                internal[src] += 1
            else:
                cross[(src, dst)] += 1
    for comm in sorted(by_comm):
        fns = sum(1 for f in g.functions.values()
                  if g.community_of(f.file) == comm)
        w(f"### {comm}")
        w("")
        w(f"- Files: {len(by_comm[comm])}, functions: {fns}, internal call "
          f"edges: {internal.get(comm, 0)}")
        outgoing = sorted((d, n) for (s, d), n in cross.items() if s == comm)
        if outgoing:
            w("- Calls into: " + ", ".join(f"{d} ({n})" for d, n in outgoing))
        for rel in by_comm[comm]:
            w(f"  - `{rel}`")
        w("")

    # ---- Structural communities
    w("## Structural communities (label propagation)")
    w("")
    groups: dict[int, list[str]] = defaultdict(list)
    for k, c in a.lp.items():
        groups[c].append(k)
    for c in sorted(groups)[:12]:
        members = sorted(groups[c], key=lambda k: (-a.pr[k], k))
        files = defaultdict(int)
        for k in members:
            files[g.community_of(_file_of(k))] += 1
        mix = ", ".join(f"{n} {cm}" for cm, n in sorted(files.items(), key=lambda kv: -kv[1]))
        w(f"- C{c} ({len(members)} nodes: {mix}): "
          f"{', '.join(f'`{short(k)}`' for k in members[:8])}"
          f"{' …' if len(members) > 8 else ''}")
    w("")
    w("### Library-only subgraph (tests/examples removed)")
    w("")
    w(f"Modularity Q={a.q_lib:.3f}. The subsystems the call structure "
      f"itself carves out, independent of directory layout:")
    w("")
    lgroups: dict[int, list[str]] = defaultdict(list)
    for k, c in a.lp_lib.items():
        lgroups[c].append(k)
    singles: list[str] = []
    for c in sorted(lgroups):
        members = sorted(lgroups[c], key=lambda k: (-a.pr[k], k))
        if len(members) == 1:
            singles.append(short(members[0]))
            continue
        w(f"- L{c} ({len(members)}): "
          f"{', '.join(f'`{short(k)}`' for k in members[:20])}"
          f"{' …' if len(members) > 20 else ''}")
    if singles:
        w(f"- {len(singles)} leaf API functions with no library-internal "
          f"edges (called only by tests/examples): "
          f"{', '.join(f'`{n}`' for n in sorted(set(singles)))}")
    w("")
    return "\n".join(L) + "\n"


# ----------------------------------------------------------------------
# wiki/index.md
# ----------------------------------------------------------------------

def render_wiki(a: Analysis) -> str:
    g = a.g
    L: list[str] = []
    w = L.append
    w("# Codebase Wiki")
    w("")
    w("Topic-grouped navigation generated by `graphify`. Line numbers refer "
      "to the file at generation time.")
    w("")
    by_file_fn: dict[str, list[Function]] = defaultdict(list)
    for fn in g.functions.values():
        by_file_fn[fn.file].append(fn)
    by_file_macro: dict[str, list[str]] = defaultdict(list)
    for m in g.macros.values():
        for v in m.variants:
            by_file_macro[v.file].append(f"`{m.name}`:{v.line}")
    by_file_type: dict[str, list[str]] = defaultdict(list)
    for t in g.types.values():
        by_file_type[t.file].append(f"`{t.name}`:{t.line} ({t.kind})")
    by_file_asr: dict[str, list[int]] = defaultdict(list)
    for asr in g.assertions:
        by_file_asr[asr.file].append(asr.id)
    by_comm: dict[str, list[str]] = defaultdict(list)
    for rel in g.files:
        by_comm[g.community_of(rel)].append(rel)
    order = ["Public API (headers)", "Core engine", "Platform / HAL",
             "App glue", "Configuration", "Examples", "Tests", "Other"]
    for comm in order:
        if comm not in by_comm:
            continue
        w(f"## {comm}")
        w("")
        for rel in by_comm[comm]:
            w(f"### `{rel}`")
            inc = g.includes.get(rel, [])
            if inc:
                w(f"- Includes: {', '.join(f'`{i}`' for i in inc)}")
            if rel in g.modules:
                w(f"- Module: `{g.modules[rel]}`")
            fns = sorted(by_file_fn.get(rel, []), key=lambda f: f.line)
            if fns:
                w("- Defines: " + ", ".join(
                    f"`{f.name}`:{f.line}"
                    + ("" if not f.gate else f" [{_gate_str(f.gate)}]")
                    for f in fns))
            if by_file_type.get(rel):
                w("- Types: " + ", ".join(by_file_type[rel]))
            if by_file_macro.get(rel):
                w("- Macros: " + ", ".join(sorted(set(by_file_macro[rel]))))
            if by_file_asr.get(rel):
                ids = sorted(set(by_file_asr[rel]))
                w(f"- Assertion IDs: {ids[0]}..{ids[-1]} ({len(ids)} distinct)")
            if rel in a.tests.run_tests:
                w(f"- RUN_TEST cases: {a.tests.run_tests[rel]}")
            w("")
    w("## Host tooling (Python)")
    w("")
    for mod, pm in sorted(a.pyg.modules.items()):
        w(f"### `{pm.file}`")
        fns = sorted((a.pyg.funcs[k] for k in pm.funcs), key=lambda f: f.line)
        if fns:
            w("- Defines: " + ", ".join(f"`{f.name}`:{f.line}" for f in fns))
        w("")
    return "\n".join(L) + "\n"


# ----------------------------------------------------------------------
# graph.json + graph.html
# ----------------------------------------------------------------------

def graph_json(a: Analysis) -> dict:
    g = a.g
    nodes = []
    edges = []
    for k, f in g.functions.items():
        nodes.append({
            "id": k, "kind": "function", "name": f.name, "file": f.file,
            "line": f.line, "community": g.community_of(f.file),
            "unit": f.unit, "static": f.static, "weak": f.weak,
            "gate": list(f.gate), "isr": f.isr, "module": f.module,
            "critsec": f.crit_enter, "layer": a.layer.get(k, 0),
            "struct_comm": a.lp.get(k, -1), "lib_comm": a.lp_lib.get(k, -1),
            "unprotected_volatile": sorted(f.unprotected_volatile),
            "betweenness": round(a.bc.get(k, 0), 1),
            "pagerank": round(a.pr.get(k, 0), 6),
            "articulation": k in a.aps, "body_lines": f.body_lines,
            "reads": sorted(f.reads), "writes": sorted(f.writes),
        })
        for c in f.calls:
            edges.append({"src": k, "dst": c, "kind": "calls",
                          "critsec": any(short(c) == n for n in f.in_critsec_calls)})
        for m in f.expands:
            edges.append({"src": k, "dst": f"macro:{m}", "kind": "expands"})
        for t in f.uses_types:
            edges.append({"src": k, "dst": f"type:{t}", "kind": "uses_type"})
        for c in f.uses_config:
            edges.append({"src": k, "dst": f"config:{c}", "kind": "uses_config"})
        for r in f.writes:
            edges.append({"src": k, "dst": f"field:{r}", "kind": "writes"})
        for r in f.reads:
            edges.append({"src": k, "dst": f"field:{r}", "kind": "reads"})
        if f.decl:
            edges.append({"src": k, "dst": f.decl, "kind": "implements"})
        if f.overrides:
            edges.append({"src": k, "dst": f.overrides, "kind": "overrides"})
    for name, d in g.decls.items():
        nodes.append({"id": d.key, "kind": "decl", "name": name, "file": d.file,
                      "line": d.line, "community": g.community_of(d.file),
                      "isr": d.isr})
    for name, m in g.macros.items():
        nodes.append({"id": m.key, "kind": "macro", "name": name,
                      "file": m.file, "line": m.line,
                      "community": g.community_of(m.file),
                      "function_like": m.function_like,
                      "variants": [{"file": v.file, "line": v.line,
                                    "gate": list(v.gate), "noop": v.noop}
                                   for v in m.variants]})
        for c in m.calls:
            if c in g.macros:
                edges.append({"src": m.key, "dst": f"macro:{c}", "kind": "expands"})
            else:
                libs = [f for f in g.fn_by_name(c) if f.unit == "lib"]
                if libs:
                    edges.append({"src": m.key, "dst": libs[0].key, "kind": "calls"})
                elif c in g.decls:
                    edges.append({"src": m.key, "dst": f"decl:{c}", "kind": "calls"})
    for name, t in g.types.items():
        nodes.append({"id": t.key, "kind": "type", "name": name, "file": t.file,
                      "line": t.line, "community": g.community_of(t.file),
                      "type_kind": t.kind, "gate": list(t.gate),
                      "fields": [{"name": f.name, "ctype": f.ctype,
                                  "gate": list(f.gate), "volatile": f.volatile}
                                 for f in t.fields.values()],
                      "enumerators": t.enumerators})
        for f in t.fields.values():
            nodes.append({"id": f"field:{name}.{f.name}", "kind": "field",
                          "name": f"{name}.{f.name}", "file": t.file,
                          "line": f.line, "community": g.community_of(t.file),
                          "ctype": f.ctype, "volatile": f.volatile,
                          "gate": list(f.gate)})
            edges.append({"src": t.key, "dst": f"field:{name}.{f.name}",
                          "kind": "has_field"})
            for tok in f.ctype.replace("*", " ").replace("[", " ").split():
                if tok in g.types and tok != name:
                    edges.append({"src": f"field:{name}.{f.name}",
                                  "dst": f"type:{tok}", "kind": "of_type"})
    for name, c in g.configs.items():
        nodes.append({"id": c.key, "kind": "config", "name": name,
                      "file": c.file, "line": c.line, "default": c.default,
                      "community": g.community_of(c.file)})
    for m in a.machines:
        mid = f"machine:{m.file}:{m.name}"
        nodes.append({"id": mid, "kind": "machine", "name": m.name,
                      "file": m.file, "line": 0, "initial": m.initial_state,
                      "community": g.community_of(m.file),
                      "fixture": m.is_test_fixture,
                      "multiplicity": m.multiplicity})
        for s in m.states.values():
            sid = f"{mid}:{s.name}"
            nodes.append({"id": sid, "kind": "state", "name": s.name,
                          "file": m.file, "line": 0,
                          "community": g.community_of(m.file),
                          "timeout_ms": s.timeout_ms,
                          "min_dwell_ms": s.min_dwell_ms})
            edges.append({"src": mid, "dst": sid, "kind": "has_state"})
        for r in m.rows:
            edges.append({"src": f"{mid}:{r.from_state}",
                          "dst": f"{mid}:{r.to_state}", "kind": "transition",
                          "event": r.event, "guard": r.guard, "action": r.action})
    for src, dst, kind, attrs in g.edges:
        edges.append({"src": src, "dst": dst, "kind": kind, **attrs})
    for k, f in a.pyg.funcs.items():
        nodes.append({"id": k, "kind": "pyfunc", "name": f.name,
                      "file": a.pyg.modules[f.module].file, "line": f.line,
                      "community": "Tooling (Python)"})
        for c in f.calls:
            edges.append({"src": k, "dst": c, "kind": "calls"})
    for mod, pm in a.pyg.modules.items():
        nodes.append({"id": pm.key, "kind": "pymodule", "name": mod,
                      "file": pm.file, "line": 1, "community": "Tooling (Python)"})
        for fk in pm.funcs:
            edges.append({"src": pm.key, "dst": fk, "kind": "defines"})
        for i in pm.imports:
            if i in a.pyg.modules:
                edges.append({"src": pm.key, "dst": f"pymod:{i}", "kind": "imports"})
    ids = {n["id"] for n in nodes}
    edges = [e for e in edges if e["src"] in ids and e["dst"] in ids]
    edges.sort(key=lambda e: (e["src"], e["dst"], e["kind"]))
    nodes.sort(key=lambda n: n["id"])
    return {
        "nodes": nodes, "edges": edges,
        "metrics": {
            "modularity_directory": round(a.q_dir, 4),
            "modularity_structural": round(a.q_lp, 4),
            "clustering": round(a.cc, 4),
            "layers": {str(d): n for d, n in sorted(
                defaultdict(int, {d: sum(1 for v in a.layer.values() if v == d)
                                  for d in set(a.layer.values())}).items())},
            "bowtie": {k: len(v) for k, v in a.tie.items()},
            "sccs": a.sccs,
        },
        "findings": [{"severity": f.severity, "check": f.check,
                      "message": f.message} for f in a.findings],
    }


def render_html(data: dict) -> str:
    payload = json.dumps(data, separators=(",", ":"))
    return _HTML_TEMPLATE.replace("__DATA__", payload)


_HTML_TEMPLATE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<title>graphify — state-machine-template</title>
<style>
:root{--bg:#0f1115;--fg:#e6e6e6;--muted:#8a8f98;--panel:#171a21;--line:#2a2f3a}
html,body{margin:0;height:100%;background:var(--bg);color:var(--fg);font:13px/1.4 system-ui,Segoe UI,Roboto,sans-serif}
#wrap{display:grid;grid-template-columns:300px 1fr;height:100%}
#side{background:var(--panel);border-right:1px solid var(--line);padding:12px;overflow:auto}
#side h1{font-size:15px;margin:0 0 8px}
#side label{display:block;margin:3px 0;color:var(--muted)}
#side input[type=text]{width:100%;box-sizing:border-box;background:#0b0d11;color:var(--fg);border:1px solid var(--line);padding:4px 6px}
#info{margin-top:10px;font-size:12px;white-space:pre-wrap;word-break:break-word;color:#c8ccd4}
canvas{display:block;width:100%;height:100%}
.sw{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:5px;vertical-align:middle}
.k{color:var(--muted)}
</style></head><body><div id="wrap"><div id="side">
<h1>graphify — call graph</h1>
<input id="q" type="text" placeholder="filter by name (regex)…">
<div id="kinds"></div>
<div id="edgekinds"></div>
<label><input type="checkbox" id="labels" checked> labels</label>
<label><input type="checkbox" id="lib" checked> library only (src/ + include/)</label>
<label>size by <select id="sizeby"><option value="degree">degree</option><option value="betweenness">betweenness</option><option value="pagerank">pagerank</option><option value="body_lines">body lines</option></select></label>
<label>color by <select id="colorby"><option value="community">directory community</option><option value="struct_comm">structural community</option><option value="layer">layer</option><option value="kind">node kind</option></select></label>
<div id="legend"></div>
<div id="info">click a node</div>
</div><canvas id="c"></canvas></div>
<script>
const DATA = __DATA__;
const KIND_DEFAULT = {function:true, decl:false, macro:true, type:true, field:false, config:false, machine:false, state:false, pyfunc:false, pymodule:false};
const EDGE_DEFAULT = {calls:true, expands:true, invokes:true, implements:false, overrides:true, uses_type:false, uses_config:false, reads:false, writes:false, has_field:false, of_type:false, has_state:false, transition:false, binds:true, defines:false, imports:false};
const PALETTE = ["#5b8def","#e06c75","#98c379","#e5c07b","#c678dd","#56b6c2","#d19a66","#f08db5","#7fd3a3","#8ab4f8","#ff9f43","#a29bfe","#74b9ff","#55efc4","#fab1a0","#ffeaa7"];
const kinds = {}, ekinds = {};
DATA.nodes.forEach(n=>kinds[n.kind]=(kinds[n.kind]||0)+1);
DATA.edges.forEach(e=>ekinds[e.kind]=(ekinds[e.kind]||0)+1);
const state = {kinds:{...KIND_DEFAULT}, ekinds:{...EDGE_DEFAULT}, q:"", lib:true, labels:true, sizeby:"degree", colorby:"community", sel:null};
function checks(el, obj, counts, title){
  el.innerHTML = "<div class=k>"+title+"</div>"+Object.keys(counts).sort().map(k=>`<label><input type=checkbox data-k="${k}" ${obj[k]?"checked":""}> ${k} <span class=k>(${counts[k]})</span></label>`).join("");
  el.querySelectorAll("input").forEach(i=>i.onchange=()=>{obj[i.dataset.k]=i.checked; rebuild();});
}
checks(document.getElementById("kinds"), state.kinds, kinds, "node kinds");
checks(document.getElementById("edgekinds"), state.ekinds, ekinds, "edge kinds");
document.getElementById("q").oninput=e=>{state.q=e.target.value; rebuild();};
document.getElementById("lib").onchange=e=>{state.lib=e.target.checked; rebuild();};
document.getElementById("labels").onchange=e=>{state.labels=e.target.checked;};
document.getElementById("sizeby").onchange=e=>{state.sizeby=e.target.value;};
document.getElementById("colorby").onchange=e=>{state.colorby=e.target.value; legend();};
const byId = {}; DATA.nodes.forEach(n=>byId[n.id]=n);
let nodes=[], edges=[], colorKeys=[];
function colorKey(n){ return String(n[state.colorby] ?? "?"); }
function color(n){ let i=colorKeys.indexOf(colorKey(n)); if(i<0){colorKeys.push(colorKey(n)); i=colorKeys.length-1;} return PALETTE[i%PALETTE.length]; }
function legend(){ colorKeys=[]; nodes.forEach(color); document.getElementById("legend").innerHTML = "<div class=k>legend</div>"+colorKeys.map((k,i)=>`<div><span class=sw style="background:${PALETTE[i%PALETTE.length]}"></span>${k}</div>`).join(""); }
function rebuild(){
  let re=null; try{ re = state.q? new RegExp(state.q,"i"):null; }catch(e){}
  const keep = n => state.kinds[n.kind] && (!state.lib || /^(src|include)\//.test(n.file||"") || n.kind==="machine"||n.kind==="state") && (!re || re.test(n.name));
  const vis = new Set(DATA.nodes.filter(keep).map(n=>n.id));
  edges = DATA.edges.filter(e=>state.ekinds[e.kind] && vis.has(e.src) && vis.has(e.dst));
  const deg = {}; edges.forEach(e=>{deg[e.src]=(deg[e.src]||0)+1; deg[e.dst]=(deg[e.dst]||0)+1;});
  const old = {}; nodes.forEach(n=>old[n.id]=n);
  nodes = DATA.nodes.filter(n=>vis.has(n.id)).map(n=>{ const o=old[n.id]; return Object.assign(n, {x:o?o.x:(Math.random()-0.5)*800, y:o?o.y:(Math.random()-0.5)*600, vx:0, vy:0, degree:deg[n.id]||0}); });
  legend(); alpha = 1;
}
const cv=document.getElementById("c"), ctx=cv.getContext("2d");
let W,H,tx=0,ty=0,scale=1,alpha=1,drag=null,pan=null;
function resize(){ W=cv.width=cv.clientWidth*devicePixelRatio; H=cv.height=cv.clientHeight*devicePixelRatio; }
window.onresize=resize; resize();
function radius(n){ const v = state.sizeby==="degree"? n.degree : (n[state.sizeby]||0); const max = Math.max(1,...nodes.map(m=>state.sizeby==="degree"?m.degree:(m[state.sizeby]||0))); return 3+10*Math.sqrt(v/max); }
function tick(){
  if(alpha>0.003){
    const k=0.02, rep=1800, link=0.03;
    for(let i=0;i<nodes.length;i++){ const a=nodes[i]; a.vx-=a.x*0.0008; a.vy-=a.y*0.0008;
      for(let j=i+1;j<nodes.length;j++){ const b=nodes[j]; let dx=a.x-b.x, dy=a.y-b.y, d2=dx*dx+dy*dy+0.01; if(d2>250000) continue; const f=rep/d2; dx*=f; dy*=f; a.vx+=dx; a.vy+=dy; b.vx-=dx; b.vy-=dy; } }
    edges.forEach(e=>{ const a=byId[e.src], b=byId[e.dst]; if(!a||!b||a.x===undefined||b.x===undefined) return; const dx=b.x-a.x, dy=b.y-a.y; const d=Math.sqrt(dx*dx+dy*dy)||1; const f=(d-60)*link; a.vx+=dx/d*f; a.vy+=dy/d*f; b.vx-=dx/d*f; b.vy-=dy/d*f; });
    nodes.forEach(n=>{ if(n===drag) return; n.x+=n.vx*alpha; n.y+=n.vy*alpha; n.vx*=0.6; n.vy*=0.6; });
    alpha*=0.985;
  }
  draw(); requestAnimationFrame(tick);
}
function draw(){
  ctx.setTransform(1,0,0,1,0,0); ctx.clearRect(0,0,W,H);
  ctx.setTransform(scale*devicePixelRatio,0,0,scale*devicePixelRatio,W/2+tx*devicePixelRatio,H/2+ty*devicePixelRatio);
  ctx.lineWidth=0.6;
  edges.forEach(e=>{ const a=byId[e.src], b=byId[e.dst]; if(a.x===undefined||b.x===undefined) return;
    const hot = state.sel && (e.src===state.sel.id||e.dst===state.sel.id);
    ctx.strokeStyle = hot? "#ffffff" : (e.kind==="invokes"? "rgba(230,160,80,0.45)" : e.kind==="expands"? "rgba(160,120,220,0.35)" : e.kind==="overrides"? "rgba(80,200,120,0.5)" : "rgba(140,150,170,0.25)");
    ctx.beginPath(); ctx.moveTo(a.x,a.y); ctx.lineTo(b.x,b.y); ctx.stroke();
    const dx=b.x-a.x, dy=b.y-a.y, d=Math.sqrt(dx*dx+dy*dy)||1, r=radius(b); const px=b.x-dx/d*r, py=b.y-dy/d*r;
    ctx.beginPath(); ctx.moveTo(px,py); ctx.lineTo(px-dx/d*4-dy/d*2, py-dy/d*4+dx/d*2); ctx.lineTo(px-dx/d*4+dy/d*2, py-dy/d*4-dx/d*2); ctx.closePath(); ctx.fillStyle=ctx.strokeStyle; ctx.fill();
  });
  nodes.forEach(n=>{ const r=radius(n); ctx.beginPath(); ctx.arc(n.x,n.y,r,0,6.283); ctx.fillStyle=color(n); ctx.fill();
    if(n.articulation){ ctx.strokeStyle="#fff"; ctx.lineWidth=1.2; ctx.stroke(); }
    if(n===state.sel){ ctx.strokeStyle="#ffd166"; ctx.lineWidth=2; ctx.stroke(); }
    if(state.labels && (r>5 || scale>1.6 || n===state.sel)){ ctx.fillStyle="#dfe3ea"; ctx.font=`${10/Math.max(0.7,Math.sqrt(scale))}px system-ui`; ctx.fillText(n.name, n.x+r+2, n.y+3); }
  });
}
function pick(ev){ const rect=cv.getBoundingClientRect(); const mx=(ev.clientX-rect.left-cv.clientWidth/2-tx)/scale, my=(ev.clientY-rect.top-cv.clientHeight/2-ty)/scale; let best=null,bd=1e9; nodes.forEach(n=>{ const d=Math.hypot(n.x-mx,n.y-my); if(d<radius(n)+4 && d<bd){bd=d;best=n;} }); return {n:best,mx,my}; }
cv.onmousedown=ev=>{ const p=pick(ev); if(p.n){ drag=p.n; } else { pan={x:ev.clientX-tx,y:ev.clientY-ty}; } };
cv.onmousemove=ev=>{ if(drag){ const p=pick(ev); drag.x=p.mx; drag.y=p.my; alpha=Math.max(alpha,0.05);} else if(pan){ tx=ev.clientX-pan.x; ty=ev.clientY-pan.y; } };
cv.onmouseup=ev=>{ if(drag){ const p=pick(ev); if(p.n===drag) select(drag);} drag=null; pan=null; };
cv.onwheel=ev=>{ ev.preventDefault(); scale*=ev.deltaY<0?1.1:0.9; };
function select(n){ state.sel=n; const ins=edges.filter(e=>e.dst===n.id).map(e=>byId[e.src].name+" ("+e.kind+")"), outs=edges.filter(e=>e.src===n.id).map(e=>byId[e.dst].name+" ("+e.kind+")");
  const skip = new Set(["x","y","vx","vy","id"]);
  const attrs = Object.keys(n).filter(k=>!skip.has(k) && n[k]!=="" && !(Array.isArray(n[k])&&n[k].length===0) && typeof n[k]!=="object").map(k=>k+": "+n[k]).join("\n");
  const arr = Object.keys(n).filter(k=>Array.isArray(n[k])&&n[k].length&&typeof n[k][0]!=="object").map(k=>k+": "+n[k].join(", ")).join("\n");
  document.getElementById("info").textContent = n.name+"\n"+attrs+"\n"+arr+"\n\n← in ("+ins.length+"):\n"+ins.join("\n")+"\n\n→ out ("+outs.length+"):\n"+outs.join("\n"); }
rebuild(); tick();
</script></body></html>
"""
