"""C source analysis: function definitions, call edges, include edges.

Pragmatic regex-based extraction tuned to this repository's uniform coding
style (functions defined at column 0, K&R-adjacent brace placement). Not a
general C parser -- good enough to build a navigable, honest graph of this
codebase.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

SOURCE_DIRS = ("include", "src", "tests", "examples", "config")
C_KEYWORDS = {
    "if", "for", "while", "switch", "return", "sizeof", "do", "else",
    "case", "break", "continue", "goto", "typedef", "struct", "enum",
    "union", "defined",
}

# A definition candidate: line starting at column 0 with type tokens then
# an identifier and an open paren (e.g. "static bool sm_post_internal(").
DEF_RE = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_]*[ \t\*]+)+([A-Za-z_][A-Za-z0-9_]*)\s*\(",
    re.MULTILINE,
)
CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')


@dataclass
class Function:
    name: str
    file: str
    line: int
    calls: set[str] = field(default_factory=set)
    callers: set[str] = field(default_factory=set)

    @property
    def degree(self) -> int:
        return len(self.calls) + len(self.callers)


@dataclass
class Graph:
    functions: dict[str, Function]
    includes: dict[str, list[str]]   # file -> included headers
    files: list[str]

    def community_of(self, file: str) -> str:
        """Map a file path to its subsystem community."""
        if file.startswith("include/"):
            return "Public API (headers)"
        if file.startswith("src/core/"):
            return "Core engine"
        if file.startswith("src/platform/") or "platform" in file:
            return "Platform / HAL"
        if file.startswith("src/app/"):
            return "App glue"
        if file.startswith("tests/"):
            return "Tests"
        if file.startswith("examples/"):
            return "Examples"
        if file.startswith("config/"):
            return "Configuration"
        return "Other"


def _strip_noise(text: str) -> str:
    """Remove comments and string literals so they can't fake call edges."""
    text = COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    return STRING_RE.sub('""', text)


def _body_extent(text: str, open_paren: int) -> tuple[int, int] | None:
    """From a definition's '(' offset, find its body '{'..'}' extent.

    Returns (body_start, body_end) offsets, or None for prototypes.
    """
    depth = 0
    i = open_paren
    while i < len(text):
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    else:
        return None
    # After the closing paren: skip whitespace; must hit '{' (else prototype)
    j = i + 1
    while j < len(text) and text[j] in " \t\r\n":
        j += 1
    if j >= len(text) or text[j] != "{":
        return None
    # Match braces for the body
    depth = 0
    k = j
    while k < len(text):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return (j, k + 1)
        k += 1
    return None


def build_graph(root: Path) -> Graph:
    functions: dict[str, Function] = {}
    includes: dict[str, list[str]] = {}
    bodies: list[tuple[str, str]] = []  # (function name, body text)
    files: list[str] = []

    paths: list[Path] = []
    for d in SOURCE_DIRS:
        base = root / d
        if base.is_dir():
            paths.extend(sorted(base.rglob("*.c")))
            paths.extend(sorted(base.rglob("*.h")))

    for path in paths:
        rel = path.relative_to(root).as_posix()
        files.append(rel)
        raw = path.read_text(encoding="utf-8", errors="replace")
        includes[rel] = sorted(set(INCLUDE_RE.findall(raw)))
        text = _strip_noise(raw)

        for m in DEF_RE.finditer(text):
            name = m.group(1)
            if name in C_KEYWORDS:
                continue
            extent = _body_extent(text, m.end() - 1)
            if extent is None:
                continue  # prototype or macro-ish construct
            line = text.count("\n", 0, m.start()) + 1
            # First definition wins; tests redefine setUp/tearDown etc. --
            # keep per-file uniqueness by qualifying duplicates.
            key = name if name not in functions else f"{name}@{rel}"
            functions[key] = Function(name=name, file=rel, line=line)
            bodies.append((key, text[extent[0]:extent[1]]))

    names_by_bare: dict[str, list[str]] = {}
    for key, fn in functions.items():
        names_by_bare.setdefault(fn.name, []).append(key)

    for key, body in bodies:
        fn = functions[key]
        for cm in CALL_RE.finditer(body):
            callee = cm.group(1)
            if callee in C_KEYWORDS or callee == fn.name:
                continue
            for target_key in names_by_bare.get(callee, ()):
                fn.calls.add(target_key)
                functions[target_key].callers.add(key)

    return Graph(functions=functions, includes=includes, files=files)
