"""Graph rebuild entry point.

CLAUDE.md contract (unchanged since v1):

    python3 -c "from graphify.watch import _rebuild_code; \
                from pathlib import Path; _rebuild_code(Path('.'))"

Also runnable as `python3 -m graphify.watch` from the repo root.

Outputs (graphify-out/, gitignored):
    GRAPH_REPORT.md   summary, G-check findings, topology, god nodes,
                      interface layer, feature gates, config, state access
                      matrix, critical sections, assertion map, macro
                      expansion, machine<->code bindings, test inventory,
                      docs cross-reference, model round-trip, tooling,
                      directory + structural communities
    MACHINES.md       per-machine Mermaid diagrams + V1..V5 findings
    wiki/index.md     per-file navigation
    graph.json        typed nodes/edges/metrics/findings for downstream tools
    graph.html        self-contained interactive viewer (no external assets)
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

from .analyze import build_graph
from .link import bind_callbacks, crosscheck_models, docs_xref, test_inventory
from .machines import extract_machines, render_machines
from .pytools import build_pygraph
from .render import analyze, graph_json, render_html, render_report, render_wiki

OUT_DIR = "graphify-out"
TEST_DEF_RE = re.compile(r"^\s*(SM_\w+)=(\S+)\s*$", re.MULTILINE)


def _test_defs(root: Path) -> dict[str, str]:
    p = root / "tests" / "CMakeLists.txt"
    if not p.is_file():
        return {}
    return dict(TEST_DEF_RE.findall(p.read_text(encoding="utf-8",
                                                errors="replace")))


def build_analysis(root: Path):
    root = root.resolve()
    g = build_graph(root)
    machines = extract_machines(root, g.files)
    bindings = bind_callbacks(root, g, machines)
    checks = crosscheck_models(root, machines)
    tests = test_inventory(root, g)
    docs = docs_xref(root, g, tests.run_tests)
    pyg = build_pygraph(root)
    return analyze(g, machines, bindings, checks, docs, tests, pyg,
                   _test_defs(root))


def _rebuild_code(root: Path) -> None:
    """Rebuild graphify-out/ for the repository at `root`."""
    root = root.resolve()
    a = build_analysis(root)
    out = root / OUT_DIR
    out.mkdir(exist_ok=True)
    (out / "wiki").mkdir(exist_ok=True)

    (out / "GRAPH_REPORT.md").write_text(render_report(a), encoding="utf-8")
    (out / "wiki" / "index.md").write_text(render_wiki(a), encoding="utf-8")
    (out / "MACHINES.md").write_text(render_machines(a.machines),
                                     encoding="utf-8")
    data = graph_json(a)
    (out / "graph.json").write_text(json.dumps(data, indent=1),
                                    encoding="utf-8")
    (out / "graph.html").write_text(render_html(data), encoding="utf-8")

    g = a.g
    warn = sum(1 for m in a.machines
               for sev, _, _ in m.findings if sev == "WARN"
               and not m.is_test_fixture)
    gw = sum(1 for f in a.findings if f.severity == "WARN")
    ge = sum(1 for f in a.findings if f.severity == "ERROR")
    print(f"graphify: {len(g.functions)} functions, "
          f"{sum(len(v) for v in a.adj.values())} call edges "
          f"({sum(len(v) for v in a.adj_full.values())} incl. macro/indirect), "
          f"{len(g.macros)} macros, {len(g.types)} types, "
          f"{len(g.assertions)} assertions, "
          f"{len(a.machines)} machines ({warn} app WARN), "
          f"G-checks: {ge} ERROR / {gw} WARN -> {OUT_DIR}/")


if __name__ == "__main__":
    _rebuild_code(Path(sys.argv[1]) if len(sys.argv) > 1 else Path("."))
