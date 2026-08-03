"""graphify -- repo-local code knowledge-graph generator.

Lightweight, dependency-free implementation of the graphify contract this
project's CLAUDE.md documents:

    python3 -c "from graphify.watch import _rebuild_code; \
                from pathlib import Path; _rebuild_code(Path('.'))"

Scans the C sources (include/, src/, tests/, examples/), extracts a
function-level call graph plus file include edges, and writes:

    graphify-out/GRAPH_REPORT.md   god nodes + community structure
    graphify-out/wiki/index.md     topic-grouped navigation

Output is deterministic (sorted) so regenerated graphs diff cleanly.
graphify-out/ is gitignored (ephemeral output) -- regenerate after code
changes rather than committing it.
"""

__version__ = "1.0.0"
