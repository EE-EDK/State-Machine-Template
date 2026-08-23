"""Low-level C text utilities shared by the extractors.

Everything here is regex/stack based and tuned to this repository's uniform
style (definitions at column 0, braces on their own lines or K&R). It is
*not* a C parser; the planned libclang backend replaces these helpers while
keeping every downstream data structure the same.
"""

from __future__ import annotations

import re

COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')
CHAR_RE = re.compile(r"'(?:\\.|[^'\\])'")
IDENT_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\b")
CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")

C_KEYWORDS = {
    "if", "for", "while", "switch", "return", "sizeof", "do", "else",
    "case", "break", "continue", "goto", "typedef", "struct", "enum",
    "union", "defined", "static", "const", "volatile", "inline", "extern",
    "register", "void", "int", "char", "unsigned", "signed", "long",
    "short", "float", "double", "bool", "_Static_assert",
}

PP_RE = re.compile(
    r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b[ \t]*(.*?)[ \t]*$",
    re.MULTILINE,
)
# Header include guards and extern "C" blocks are not semantic gates.
_NOISE_GATE = re.compile(r"^!?defined\(\s*(?:\w+_H|__cplusplus)\s*\)$")


def _blank(m: "re.Match[str]") -> str:
    """Replace a match with same-length whitespace (newlines kept) so
    offsets in the stripped text equal offsets in the raw text."""
    return "".join(ch if ch == "\n" else " " for ch in m.group(0))


def strip_noise(text: str) -> str:
    """Blank comments/strings/char literals. Length- and line-preserving:
    every offset in the result maps to the same offset in `text`."""
    text = COMMENT_RE.sub(_blank, text)
    text = STRING_RE.sub(lambda m: '"' + " " * (len(m.group(0)) - 2) + '"',
                         text)
    return CHAR_RE.sub(lambda m: "'" + " " * (len(m.group(0)) - 2) + "'",
                       text)


def strip_comments(text: str) -> str:
    """Blank comments only (strings kept), length-preserving."""
    return COMMENT_RE.sub(_blank, text)


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def match_paren(text: str, open_idx: int, pair: str = "()") -> int:
    """Index just past the closer matching text[open_idx] (an opener).
    Returns len(text) when unbalanced."""
    o, c = pair[0], pair[1]
    depth = 0
    for i in range(open_idx, len(text)):
        ch = text[i]
        if ch == o:
            depth += 1
        elif ch == c:
            depth -= 1
            if depth == 0:
                return i + 1
    return len(text)


def split_args(text: str) -> list[str]:
    """Split at top-level commas (respects (), [], {})."""
    parts, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    tail = text[start:]
    if tail.strip():
        parts.append(tail)
    return [p.strip() for p in parts]


def body_extent(text: str, open_paren: int) -> tuple[int, int] | None:
    """From a definition's '(' offset, return the '{'..'}' body extent or
    None when the parenthesised head is followed by ';' (a prototype)."""
    end_paren = match_paren(text, open_paren)
    if end_paren >= len(text):
        return None
    j = end_paren
    while j < len(text) and text[j] in " \t\r\n":
        j += 1
    if j >= len(text) or text[j] != "{":
        return None
    k = match_paren(text, j, "{}")
    if k > len(text):
        return None
    return (j, k)


def gate_lines(text: str) -> list[tuple[str, ...]]:
    """Per-line tuple of active preprocessor conditions (1-based index; entry
    0 is unused). `#else`/`#elif` branches are recorded as negations of the
    preceding branch conditions so a definition in an `#else` block carries
    the gate `!(COND)`."""
    lines = text.split("\n")
    result: list[tuple[str, ...]] = [()]
    stack: list[list[str]] = []        # per level: conditions active now
    prev_conds: list[list[str]] = []   # per level: earlier branch conditions

    def active() -> tuple[str, ...]:
        out: list[str] = []
        for level in stack:
            for c in level:
                if c and not _NOISE_GATE.match(c):
                    out.append(c)
        return tuple(out)

    for raw in lines:
        m = PP_RE.match(raw)
        if m:
            kw, expr = m.group(1), m.group(2).strip()
            expr = COMMENT_RE.sub("", expr).strip()
            if kw == "if":
                stack.append([expr])
                prev_conds.append([expr])
            elif kw == "ifdef":
                stack.append([f"defined({expr})"])
                prev_conds.append([f"defined({expr})"])
            elif kw == "ifndef":
                stack.append([f"!defined({expr})"])
                prev_conds.append([f"!defined({expr})"])
            elif kw == "elif" and stack:
                neg = "!(" + " || ".join(prev_conds[-1]) + ")"
                stack[-1] = [neg, expr]
                prev_conds[-1].append(expr)
            elif kw == "else" and stack:
                stack[-1] = ["!(" + " || ".join(prev_conds[-1]) + ")"]
            elif kw == "endif" and stack:
                stack.pop()
                prev_conds.pop()
        result.append(active())
    return result


def join_continuations(text: str) -> list[tuple[int, str]]:
    """Return (start_line, logical_line) pairs with backslash-newline
    continuations joined. Line numbers are 1-based and refer to the
    original text."""
    out: list[tuple[int, str]] = []
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        start = i + 1
        buf = lines[i]
        while buf.rstrip().endswith("\\") and i + 1 < len(lines):
            buf = buf.rstrip()[:-1] + " " + lines[i + 1]
            i += 1
        out.append((start, buf))
        i += 1
    return out


def doc_comment_before(raw: str, offset: int) -> str:
    """The `/** ... */` (or `/* ... */`) block that ends immediately before
    `offset` (ignoring whitespace and attribute-style macro lines), or ''."""
    j = offset
    while j > 0 and raw[j - 1] in " \t\r\n":
        j -= 1
    if raw[max(0, j - 2):j] != "*/":
        return ""
    start = raw.rfind("/*", 0, j)
    if start < 0:
        return ""
    return raw[start:j]


def parse_enums(text: str) -> dict[str, int]:
    """enumerator -> value for simple enums (literals + auto-increment)."""
    values: dict[str, int] = {}
    for m in re.finditer(r"enum\s+\w*\s*\{([^}]*)\}", text, re.DOTALL):
        next_val = 0
        for entry in split_args(m.group(1)):
            em = re.match(r"^(\w+)(?:\s*=\s*(.+))?$", entry.strip(), re.DOTALL)
            if not em:
                continue
            if em.group(2) is not None:
                try:
                    next_val = int(clean_value(em.group(2)), 0)
                except ValueError:
                    continue
            values[em.group(1)] = next_val
            next_val += 1
    return values


_CAST_RE = re.compile(r"^\((?:uint(?:8|16|32)_t|int|unsigned|size_t)\)")


def clean_value(value: str) -> str:
    """Normalize an initializer value: strip casts, suffixes, parens."""
    v = value.strip()
    while True:
        stripped = _CAST_RE.sub("", v).strip()
        if stripped != v:
            v = stripped
            continue
        if v.startswith("(") and v.endswith(")") and \
                match_paren(v, 0) == len(v):
            v = v[1:-1].strip()
            continue
        break
    return re.sub(r"([0-9])[uUlL]+$", r"\1", v)
