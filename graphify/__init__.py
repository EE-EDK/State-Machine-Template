"""graphify -- repo-local code knowledge-graph generator (v2).

Dependency-free implementation of the graphify contract this project's
CLAUDE.md documents:

    python3 -c "from graphify.watch import _rebuild_code; \
                from pathlib import Path; _rebuild_code(Path('.'))"

v2 builds a *typed* graph that ties every layer of the repository together:

    cparse.py    length-preserving C text utilities, preprocessor gate map
    analyze.py   functions / declarations / macros (with variants) / types
                 (with gated fields) / config macros / assertion sites;
                 scope- and link-unit-resolved call edges, macro expansion,
                 struct-field read/write/traverse edges, critical-section
                 spans, doc-comment ISR contracts, weak/override edges
    machines.py  SM_Config_t / SM_Transition_t / SM_StateDesc_t tables ->
                 per-machine state graphs + V1..V5 validators
    link.py      machine<->code callback bindings (+ indirect engine->callback
                 invocation edges), models/*.toml <-> examples round-trip,
                 docs <-> API cross-reference, test inventory / API coverage
    pytools.py   smgen/ + graphify/ Python module/function graph (ast)
    metrics.py   betweenness, PageRank, SCC, articulation points, label
                 propagation, modularity, layering, bow-tie decomposition
    render.py    GRAPH_REPORT.md (with G1..G14 graph-level validators),
                 wiki/index.md, graph.json, self-contained graph.html
    watch.py     orchestration (the public contract)

Outputs land in graphify-out/ (gitignored, regenerable). Output is
deterministic so regenerated graphs diff cleanly.

Roadmap: the regex extraction in analyze.py / cparse.py is the only
approximate layer; a clang/libclang AST backend can replace it while
keeping every dataclass and report unchanged. Downstream tooling should
depend on the dataclasses and graph.json, not on the extraction internals.
"""

__version__ = "2.0.0"
