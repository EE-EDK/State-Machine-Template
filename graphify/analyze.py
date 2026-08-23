"""C source analysis (v2): typed symbol graph.

Extracts, per file under include/ src/ tests/ examples/ config/:

  functions   definitions with bodies (static / weak / inline attributes,
              preprocessor feature gate, module, doc-comment ISR contract,
              critical-section usage, assertion sites)
  decls       prototypes (the interface layer -- every definition
              `implements` its declaration; strong definitions in other link
              units `override` the weak library default)
  macros      one node per macro *name* with a variant per #define (each
              variant carries its gate and the calls it expands to)
  types       structs (with gated fields), enums, callback typedefs, aliases
  config      `#ifndef X / #define X` override-pattern macros
  edges       calls (scope- and link-unit-resolved), expands (macro use),
              reads / writes / traverses (struct field access), uses_type,
              uses_config, implements, overrides, includes

The regex layer is deliberately small; everything downstream (link.py,
metrics.py, render.py) consumes only the dataclasses below, which is the
contract the planned libclang backend must satisfy.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from .cparse import (
    CALL_RE, COMMENT_RE, C_KEYWORDS, IDENT_RE, body_extent, clean_value,
    doc_comment_before, gate_lines, join_continuations, line_of,
    match_paren, split_args, strip_comments, strip_noise,
)

SOURCE_DIRS = ("include", "src", "tests", "examples", "config")

DEF_RE = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_]*[ \t\*]+)+([A-Za-z_][A-Za-z0-9_]*)\s*\(",
    re.MULTILINE,
)
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
DEFINE_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)(\(([^)]*)\))?[ \t]*(.*)$")
MODULE_RE = re.compile(r'^\s*SM_DEFINE_MODULE\s*\(\s*"([^"]+)"\s*\)', re.MULTILINE)
ASSERT_RE = re.compile(
    r"\b(SM_REQUIRE|SM_ASSERT_ID|SM_INVARIANT|SM_DIS_VERIFY|"
    r"SM_BOUNDED_LOOP_BEGIN|SM_BOUNDED_LOOP_END)\s*\(")
ASSERT_ID_ARG = {"SM_REQUIRE": 0, "SM_ASSERT_ID": 0, "SM_INVARIANT": 0,
                 "SM_DIS_VERIFY": 3, "SM_BOUNDED_LOOP_BEGIN": 2,
                 "SM_BOUNDED_LOOP_END": 2}
CRIT_ENTER, CRIT_EXIT = "SM_Platform_EnterCritical", "SM_Platform_ExitCritical"
ISR_SAFE_RE = re.compile(r"\bISR[- ]SAFE\b", re.IGNORECASE)
ISR_UNSAFE_RE = re.compile(
    r"\b(NOT\s+ISR[- ]safe|non-ISR-safe|Do not call from ISR|"
    r"never from an interrupt|not from ISR|only from (?:state callbacks|"
    r"boot))", re.IGNORECASE)
ACCESS_RE = re.compile(
    r"\b([A-Za-z_]\w*)((?:\s*\[[^\]]*\])*(?:\s*(?:->|\.)\s*[A-Za-z_]\w*(?:\s*\[[^\]]*\])*)+)")
WRITE_AFTER_RE = re.compile(r"^\s*(?:=(?!=)|\+\+|--|\+=|-=|\|=|&=|\^=|<<=|>>=)")
WRITE_BEFORE_RE = re.compile(r"(?:\+\+|--)\s*$")
TYPEDEF_STRUCT_RE = re.compile(
    r"typedef\s+struct(?:\s+(\w+))?\s*\{", re.MULTILINE)
STRUCT_RE = re.compile(r"^struct\s+(\w+)\s*\{", re.MULTILINE)
TYPEDEF_ENUM_RE = re.compile(r"typedef\s+enum(?:\s+\w+)?\s*\{", re.MULTILINE)
TYPEDEF_FPTR_RE = re.compile(
    r"typedef\s+([A-Za-z_][\w\s\*]*?)\s*\(\s*\*\s*(\w+)\s*\)\s*\(([^;]*?)\)\s*;",
    re.DOTALL)
TYPEDEF_ALIAS_RE = re.compile(r"typedef\s+(?:struct\s+)?(\w+)\s*(\*?)\s*(\w+)\s*;")


@dataclass
class Function:
    name: str
    file: str
    line: int
    calls: set[str] = field(default_factory=set)
    callers: set[str] = field(default_factory=set)
    static: bool = False
    weak: bool = False
    inline: bool = False
    gate: tuple[str, ...] = ()
    unit: str = ""
    module: str = ""
    ret: str = ""
    params: str = ""
    doc: str = ""
    isr: str = ""                    # "safe" | "unsafe" | ""
    crit_enter: int = 0
    crit_exit: int = 0
    body_lines: int = 0
    expands: set[str] = field(default_factory=set)   # macro names used
    reads: set[str] = field(default_factory=set)     # "Struct.field"
    writes: set[str] = field(default_factory=set)
    traverses: set[str] = field(default_factory=set)
    uses_types: set[str] = field(default_factory=set)
    uses_config: set[str] = field(default_factory=set)
    in_critsec_calls: set[str] = field(default_factory=set)  # callee names
    invokes_roles: set[str] = field(default_factory=set)     # callback roles
    crit_spans: list[tuple[int, int]] = field(default_factory=list)
    unprotected_volatile: set[str] = field(default_factory=set)  # writes
    decl: str | None = None         # key of the declaration it implements
    overrides: str | None = None    # key of the weak definition it replaces

    @property
    def key(self) -> str:
        return f"{self.name}@{self.file}"

    @property
    def degree(self) -> int:
        return len(self.calls) + len(self.callers)

    @property
    def public(self) -> bool:
        return not self.static


@dataclass
class Decl:
    name: str
    file: str
    line: int
    gate: tuple[str, ...] = ()
    ret: str = ""
    params: str = ""
    doc: str = ""
    isr: str = ""
    implementations: list[str] = field(default_factory=list)  # function keys

    @property
    def key(self) -> str:
        return f"decl:{self.name}"


@dataclass
class MacroVariant:
    file: str
    line: int
    gate: tuple[str, ...]
    params: str | None
    body: str
    calls: set[str] = field(default_factory=set)   # names (functions/macros)

    @property
    def function_like(self) -> bool:
        return self.params is not None

    @property
    def noop(self) -> bool:
        b = self.body.strip()
        return b in ("((void)0)", "", "0", "false", "true") or \
            b.startswith("((void)0)")


@dataclass
class Macro:
    name: str
    variants: list[MacroVariant] = field(default_factory=list)
    users: set[str] = field(default_factory=set)   # function keys / macro names

    @property
    def key(self) -> str:
        return f"macro:{self.name}"

    @property
    def function_like(self) -> bool:
        return any(v.function_like for v in self.variants)

    @property
    def file(self) -> str:
        return self.variants[0].file

    @property
    def line(self) -> int:
        return self.variants[0].line

    @property
    def calls(self) -> set[str]:
        out: set[str] = set()
        for v in self.variants:
            out |= v.calls
        return out


@dataclass
class Field:
    name: str
    ctype: str
    gate: tuple[str, ...] = ()
    volatile: bool = False
    line: int = 0


@dataclass
class Type:
    name: str
    kind: str            # struct | enum | callback | alias
    file: str
    line: int
    gate: tuple[str, ...] = ()
    fields: dict[str, Field] = field(default_factory=dict)
    enumerators: list[str] = field(default_factory=list)
    signature: str = ""
    target: str = ""     # alias target / callback return type
    users: set[str] = field(default_factory=set)   # function keys

    @property
    def key(self) -> str:
        return f"type:{self.name}"


@dataclass
class Config:
    name: str
    default: str
    file: str
    line: int
    gate: tuple[str, ...] = ()
    users: set[str] = field(default_factory=set)   # function keys / type keys

    @property
    def key(self) -> str:
        return f"config:{self.name}"


@dataclass
class Assertion:
    module: str
    id: int
    macro: str
    function: str       # function key
    file: str
    line: int
    expr: str


@dataclass
class Graph:
    functions: dict[str, Function]
    includes: dict[str, list[str]]
    files: list[str]
    decls: dict[str, Decl] = field(default_factory=dict)
    macros: dict[str, Macro] = field(default_factory=dict)
    types: dict[str, Type] = field(default_factory=dict)
    configs: dict[str, Config] = field(default_factory=dict)
    assertions: list[Assertion] = field(default_factory=list)
    modules: dict[str, str] = field(default_factory=dict)   # file -> module
    units: dict[str, str] = field(default_factory=dict)     # file -> unit
    file_gates: dict[str, list[tuple[str, ...]]] = field(default_factory=dict)
    # extra typed edges added by link.py: (src_key, dst_key, kind, attrs)
    edges: list[tuple[str, str, str, dict]] = field(default_factory=list)

    def community_of(self, file: str) -> str:
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
        if file.startswith("smgen/") or file.startswith("graphify/"):
            return "Tooling (Python)"
        if file.startswith("models/"):
            return "Models"
        if file.endswith(".md"):
            return "Docs"
        return "Other"

    def unit_of(self, file: str) -> str:
        return self.units.get(file, link_unit(file))

    def fn_by_name(self, name: str) -> list[Function]:
        return [f for f in self.functions.values() if f.name == name]

    def api_functions(self) -> list[Function]:
        """Public functions defined in the library (src/ + include/)."""
        return sorted(
            (f for f in self.functions.values()
             if f.public and f.unit == "lib"),
            key=lambda f: (f.file, f.line))


def link_unit(file: str) -> str:
    if file.startswith(("src/", "include/", "config/")):
        return "lib"
    if file.startswith("tests/"):
        return "tests"
    if file.startswith("examples/platform/"):
        return "stm32"
    if file.startswith("examples/"):
        return "ex:" + Path(file).stem
    return "other"


# ----------------------------------------------------------------------
# Extraction
# ----------------------------------------------------------------------

def _scan_files(root: Path) -> list[Path]:
    paths: list[Path] = []
    for d in SOURCE_DIRS:
        base = root / d
        if base.is_dir():
            paths.extend(sorted(base.rglob("*.c")))
            paths.extend(sorted(base.rglob("*.h")))
    return paths


def _isr_contract(doc: str) -> str:
    if not doc:
        return ""
    if ISR_UNSAFE_RE.search(doc):
        return "unsafe"
    if ISR_SAFE_RE.search(doc):
        return "safe"
    return ""


def _extract_macros(rel: str, raw: str, gates: list[tuple[str, ...]],
                    g: Graph) -> None:
    for line_no, logical in join_continuations(raw):
        m = DEFINE_RE.match(logical)
        if not m:
            continue
        name, params, body = m.group(1), m.group(3), m.group(4)
        body = COMMENT_RE.sub(" ", body).strip()
        gate = gates[line_no] if line_no < len(gates) else ()
        variant = MacroVariant(file=rel, line=line_no, gate=gate,
                               params=params, body=body)
        for cm in CALL_RE.finditer(strip_noise(body)):
            callee = cm.group(1)
            if callee not in C_KEYWORDS and callee != name:
                variant.calls.add(callee)
        g.macros.setdefault(name, Macro(name=name)).variants.append(variant)

        # Config override pattern: `#ifndef NAME` directly gating this define
        if gate and gate[-1] == f"!defined({name})" and params is None:
            g.configs[name] = Config(name=name, default=clean_value(body),
                                     file=rel, line=line_no, gate=gate[:-1])


def _parse_struct_fields(body: str, base_line: int,
                         gates: list[tuple[str, ...]]) -> dict[str, Field]:
    fields: dict[str, Field] = {}
    pos = 0
    while pos < len(body):
        end = body.find(";", pos)
        if end < 0:
            break
        stmt = body[pos:end]
        pos = end + 1
        seg = COMMENT_RE.sub(" ", stmt)
        seg = "\n".join(l for l in seg.split("\n")
                        if not l.lstrip().startswith("#"))
        s_ = seg.strip()
        if not s_:
            continue
        # line of the statement's last token (the field name)
        line = base_line + body.count("\n", 0, end)
        m = re.match(r"^(.*?)\b([A-Za-z_]\w*)\s*((?:\[[^\]]*\])*)$", s_,
                     re.DOTALL)
        if not m:
            continue
        ctype = " ".join(m.group(1).split())
        name = m.group(2)
        if name in C_KEYWORDS or not ctype:
            continue
        gate = gates[line] if line < len(gates) else ()
        fields[name] = Field(name=name, ctype=ctype + (m.group(3) or ""),
                             gate=gate, volatile="volatile" in ctype,
                             line=line)
    return fields


def _extract_types(rel: str, text: str, gates: list[tuple[str, ...]],
                   g: Graph) -> None:
    for m in TYPEDEF_STRUCT_RE.finditer(text):
        open_idx = m.end() - 1
        close = match_paren(text, open_idx, "{}")
        tail = re.match(r"\s*(\w+)\s*;", text[close:])
        if not tail:
            continue
        name = tail.group(1)
        line = line_of(text, m.start())
        body = text[open_idx + 1:close - 1]
        t = Type(name=name, kind="struct", file=rel, line=line,
                 gate=gates[line] if line < len(gates) else ())
        t.fields = _parse_struct_fields(body, line_of(text, open_idx + 1), gates)
        g.types[name] = t
        tag = m.group(1)
        if tag and tag not in g.types:
            g.types[tag] = Type(name=tag, kind="alias", file=rel, line=line,
                                gate=t.gate, target=name)
    for m in STRUCT_RE.finditer(text):
        name = m.group(1)
        open_idx = m.end() - 1
        close = match_paren(text, open_idx, "{}")
        line = line_of(text, m.start())
        body = text[open_idx + 1:close - 1]
        t = Type(name=name, kind="struct", file=rel, line=line,
                 gate=gates[line] if line < len(gates) else ())
        t.fields = _parse_struct_fields(body, line_of(text, open_idx + 1), gates)
        g.types[name] = t
    for m in TYPEDEF_ENUM_RE.finditer(text):
        open_idx = m.end() - 1
        close = match_paren(text, open_idx, "{}")
        tail = re.match(r"\s*(\w+)\s*;", text[close:])
        if not tail:
            continue
        name = tail.group(1)
        line = line_of(text, m.start())
        t = Type(name=name, kind="enum", file=rel, line=line,
                 gate=gates[line] if line < len(gates) else ())
        for entry in split_args(text[open_idx + 1:close - 1]):
            em = re.match(r"^(\w+)", entry.strip())
            if em:
                t.enumerators.append(em.group(1))
        g.types[name] = t
    for m in TYPEDEF_FPTR_RE.finditer(text):
        name = m.group(2)
        line = line_of(text, m.start())
        t = Type(name=name, kind="callback", file=rel, line=line,
                 gate=gates[line] if line < len(gates) else (),
                 signature=" ".join(m.group(3).split()),
                 target=" ".join(m.group(1).split()))
        g.types[name] = t
    for m in TYPEDEF_ALIAS_RE.finditer(text):
        target, star, name = m.group(1), m.group(2), m.group(3)
        if target in ("struct", "enum") or name in g.types:
            continue
        line = line_of(text, m.start())
        g.types[name] = Type(name=name, kind="alias", file=rel, line=line,
                             gate=gates[line] if line < len(gates) else (),
                             target=target + star)


def resolve_struct(g: Graph, name: str) -> str | None:
    """Follow typedef aliases (SM_Handle_t -> SM_Context_t -> SM_Context)
    to the underlying struct type name, or None."""
    seen: set[str] = set()
    cur = name.rstrip("*").strip()
    while cur in g.types and cur not in seen:
        seen.add(cur)
        t = g.types[cur]
        if t.kind == "struct":
            return cur
        if t.kind == "alias":
            cur = t.target.rstrip("*").strip()
            continue
        return None
    return None


def _decl_regex(g: Graph) -> re.Pattern | None:
    names = sorted((n for n in g.types
                    if resolve_struct(g, n) is not None), key=len,
                   reverse=True)
    if not names:
        return None
    alt = "|".join(re.escape(n) for n in names)
    return re.compile(
        r"\b(?:const\s+)?(?:struct\s+)?(" + alt + r")\s*(\*+)?\s*"
        r"(?:const\s+)?([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?=[;,=)])")


def _var_types(fn: Function, body: str, g: Graph,
               decl_re: re.Pattern | None) -> dict[str, str]:
    """Map local variable / parameter names to struct type names."""
    out: dict[str, str] = {}
    if decl_re is None:
        return out
    for src in (fn.params + ";", body):
        for m in decl_re.finditer(src):
            st = resolve_struct(g, m.group(1))
            if st:
                out[m.group(3)] = st
    return out


def _field_type(g: Graph, struct: str, fname: str) -> str | None:
    t = g.types.get(struct)
    if not t or fname not in t.fields:
        return None
    ctype = re.sub(r"\[.*\]", "", t.fields[fname].ctype)
    for tok in IDENT_RE.findall(ctype):
        st = resolve_struct(g, tok)
        if st:
            return st
    return None


def _extract_accesses(fn: Function, body: str, g: Graph,
                      decl_re: re.Pattern | None) -> None:
    vtypes = _var_types(fn, body, g, decl_re)
    for m in ACCESS_RE.finditer(body):
        var = m.group(1)
        if var not in vtypes:
            continue
        chain = re.findall(r"[A-Za-z_]\w*",
                           re.sub(r"\[[^\]]*\]", "", m.group(2)))
        before = body[max(0, m.start() - 4):m.start()]
        after = body[m.end():m.end() + 4]
        is_write = bool(WRITE_AFTER_RE.match(after) or
                        WRITE_BEFORE_RE.search(before))
        struct = vtypes[var]
        for i, fname in enumerate(chain):
            t = g.types.get(struct)
            if not t or fname not in t.fields:
                break
            ref = f"{struct}.{fname}"
            last = i == len(chain) - 1
            if last:
                (fn.writes if is_write else fn.reads).add(ref)
                if is_write and t.fields[fname].volatile and \
                        not _in_spans(fn.crit_spans, m.start()):
                    fn.unprotected_volatile.add(ref)
            else:
                fn.traverses.add(ref)
            nxt = _field_type(g, struct, fname)
            if nxt is None:
                break
            struct = nxt


def _extract_assertions(fn: Function, body: str, body_start_line: int,
                        module: str, g: Graph) -> None:
    for m in ASSERT_RE.finditer(body):
        macro = m.group(1)
        end = match_paren(body, m.end() - 1)
        args = split_args(body[m.end():end - 1])
        idx = ASSERT_ID_ARG[macro]
        if idx >= len(args):
            continue
        try:
            aid = int(clean_value(args[idx]), 0)
        except ValueError:
            continue
        expr = args[1] if macro != "SM_DIS_VERIFY" and len(args) > 1 else \
            ", ".join(args[:3])
        g.assertions.append(Assertion(
            module=module, id=aid, macro=macro, function=fn.key,
            file=fn.file, line=body_start_line + body.count("\n", 0, m.start()),
            expr=" ".join(expr.split())))


def _scan_calls(fn: Function, body: str) -> list[tuple[str, bool]]:
    """(callee name, inside critical section) in textual order. Also
    records the critical-section spans on `fn.crit_spans` (offsets into
    body) so field accesses can be classified as protected or not."""
    out: list[tuple[str, bool]] = []
    depth = 0
    span_start = -1
    for cm in CALL_RE.finditer(body):
        callee = cm.group(1)
        if callee in C_KEYWORDS:
            continue
        if callee == CRIT_ENTER:
            if depth == 0:
                span_start = cm.start()
            depth += 1
            fn.crit_enter += 1
            out.append((callee, False))
            continue
        if callee == CRIT_EXIT:
            depth = max(0, depth - 1)
            fn.crit_exit += 1
            if depth == 0 and span_start >= 0:
                fn.crit_spans.append((span_start, cm.end()))
                span_start = -1
            out.append((callee, False))
            continue
        out.append((callee, depth > 0))
    if span_start >= 0:
        fn.crit_spans.append((span_start, len(body)))
    return out


def _in_spans(spans: list[tuple[int, int]], pos: int) -> bool:
    return any(a <= pos < b for a, b in spans)


def build_graph(root: Path) -> Graph:
    g = Graph(functions={}, includes={}, files=[])
    bodies: list[tuple[str, str, int]] = []   # (key, body, body_start_line)
    texts: dict[str, str] = {}

    for path in _scan_files(root):
        rel = path.relative_to(root).as_posix()
        g.files.append(rel)
        g.units[rel] = link_unit(rel)
        raw = path.read_text(encoding="utf-8", errors="replace")
        g.includes[rel] = sorted(set(INCLUDE_RE.findall(raw)))
        gates = gate_lines(raw)
        g.file_gates[rel] = gates
        text = strip_noise(raw)
        texts[rel] = text
        mm = MODULE_RE.search(strip_comments(raw))
        if mm:
            g.modules[rel] = mm.group(1)
        _extract_macros(rel, raw, gates, g)
        _extract_types(rel, text, gates, g)

    # Second pass: functions + declarations (types must exist first so
    # field access can be typed).
    for rel in g.files:
        text = texts[rel]
        raw = (root / rel).read_text(encoding="utf-8", errors="replace")
        gates = g.file_gates[rel]
        module = g.modules.get(rel, "")
        for m in DEF_RE.finditer(text):
            name = m.group(1)
            if name in C_KEYWORDS:
                continue
            prefix = m.group(0)[:m.start(1) - m.start()]
            toks = prefix.replace("*", " * ").split()
            if any(t in ("return", "else", "case", "goto") for t in toks):
                continue
            open_paren = m.end() - 1
            close_paren = match_paren(text, open_paren)
            params = " ".join(text[open_paren + 1:close_paren - 1].split())
            line = line_of(text, m.start())
            gate = gates[line] if line < len(gates) else ()
            ret = " ".join(t for t in toks
                           if t not in ("static", "inline", "SM_WEAK", "extern"))
            doc = doc_comment_before(raw, m.start())
            extent = body_extent(text, open_paren)
            if extent is None:
                # prototype?
                j = close_paren
                while j < len(text) and text[j] in " \t\r\n":
                    j += 1
                if j < len(text) and text[j] == ";" and "static" not in toks:
                    d = g.decls.setdefault(name, Decl(
                        name=name, file=rel, line=line, gate=gate,
                        ret=ret, params=params, doc=doc))
                    if not d.isr:
                        d.isr = _isr_contract(doc)
                continue
            fn = Function(name=name, file=rel, line=line,
                          static="static" in toks, weak="SM_WEAK" in toks,
                          inline="inline" in toks, gate=gate,
                          unit=g.units[rel], module=module, ret=ret,
                          params=params, doc=doc, isr=_isr_contract(doc))
            body = text[extent[0]:extent[1]]
            fn.body_lines = body.count("\n") + 1
            g.functions[fn.key] = fn
            bodies.append((fn.key, body, line_of(text, extent[0])))

    # Declarations ↔ definitions, weak ↔ override
    for fn in g.functions.values():
        if fn.public and fn.name in g.decls:
            fn.decl = g.decls[fn.name].key
            g.decls[fn.name].implementations.append(fn.key)
            if not fn.isr:
                fn.isr = g.decls[fn.name].isr
    for name, d in g.decls.items():
        weak = [k for k in d.implementations if g.functions[k].weak or
                g.functions[k].unit == "lib"]
        for k in d.implementations:
            if k not in weak and weak:
                g.functions[k].overrides = weak[0]

    # Resolve calls, macro expansion, field access, assertions, config use
    by_name: dict[str, list[Function]] = {}
    for fn in g.functions.values():
        by_name.setdefault(fn.name, []).append(fn)
    config_names = set(g.configs)
    type_names = set(g.types)
    decl_re = _decl_regex(g)

    for key, body, body_line in bodies:
        fn = g.functions[key]
        for callee, in_crit in _scan_calls(fn, body):
            if callee == fn.name:
                continue
            target = _resolve(fn, callee, by_name, g)
            if target is not None:
                fn.calls.add(target.key)
                target.callers.add(fn.key)
                if in_crit:
                    fn.in_critsec_calls.add(callee)
            elif callee in g.macros and g.macros[callee].function_like:
                fn.expands.add(callee)
                g.macros[callee].users.add(fn.key)
                if in_crit:
                    fn.in_critsec_calls.add(callee)
            elif callee in g.decls:
                # external interface with no definition in scope
                fn.calls.add(g.decls[callee].key)
                if in_crit:
                    fn.in_critsec_calls.add(callee)
        # callback roles invoked through pointers: `->on_entry(` etc.
        for rm in re.finditer(r"(?:->|\.)\s*([A-Za-z_]\w*)\s*\(", body):
            fn.invokes_roles.add(rm.group(1))
        _extract_accesses(fn, body, g, decl_re)
        _extract_assertions(fn, body, body_line, fn.module, g)
        idents = set(IDENT_RE.findall(body)) | set(IDENT_RE.findall(fn.params))
        for tok in idents & config_names:
            fn.uses_config.add(tok)
            g.configs[tok].users.add(fn.key)
        for tok in idents & type_names:
            fn.uses_types.add(tok)
            g.types[tok].users.add(fn.key)

    # Macro → macro expansion users; config usage by types/macros
    for mname, mac in g.macros.items():
        for v in mac.variants:
            for callee in v.calls:
                if callee in g.macros and callee != mname:
                    g.macros[callee].users.add(mac.key)
    for t in g.types.values():
        for f in t.fields.values():
            for tok in set(IDENT_RE.findall(f.ctype)) & config_names:
                g.configs[tok].users.add(t.key)
    return g


def _resolve(fn: Function, callee: str, by_name: dict[str, list[Function]],
             g: Graph) -> Function | None:
    cands = by_name.get(callee)
    if not cands:
        return None
    # 1. static function in the same file
    for c in cands:
        if c.static and c.file == fn.file:
            return c
    ext = [c for c in cands if not c.static]
    if not ext:
        return None
    # 2. definition in the caller's link unit
    for c in ext:
        if c.unit == fn.unit:
            return c
    # 3. library definition (weak default or core)
    for c in ext:
        if c.unit == "lib":
            return c
    return ext[0]
