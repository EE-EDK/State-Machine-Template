"""Machine-level graph extraction: the state machines themselves.

Where analyze.py builds the *code* graph (who calls whom), this module
builds the *machine* graph: which SM_Config_t wires which SM_StateDesc_t
array to which SM_Transition_t table, and what the resulting state graph
looks like. This is the artifact the Phase B host tooling (validators,
coverage maps, generated-table cross-checks) consumes.

Transition tables are declarative const data with a fixed shape, so
structured text parsing is reliable here in a way general C parsing is
not. The planned clang backend replaces the *extraction*; the Machine /
Row / StateInfo model and the MACHINES.md output are the stable contract.

Validators (Phase B seed):
  V1  unreachable states (no path from initial_state)          WARN
  V2  timeout_ms configured but no SM_EVT_TIMEOUT route        WARN
  V3  min_dwell_ms > timeout_ms (effective timeout is max())   INFO
  V4  terminal states (no outgoing transitions)                INFO
  V5  fully-guarded (from,event) sets -- event droppable       INFO
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from .cparse import COMMENT_RE

TIMEOUT_NAMES = {"SM_EVT_TIMEOUT", "SM_EVENT_COUNT"}

ARRAY_RE = re.compile(
    r"(?:static\s+)?const\s+(SM_Transition_t|SM_StateDesc_t)\s+"
    r"(\w+)\s*\[[^\]]*\]\s*=\s*\{"
)
CONFIG_RE = re.compile(
    r"(?:static\s+)?(?:const\s+)?SM_Config_t\s+(\w+)\s*=\s*\{"
)
ENUM_RE = re.compile(r"enum\s+\w*\s*\{([^}]*)\}", re.DOTALL)
CAST_RE = re.compile(r"^\((?:uint(?:8|16|32)_t|int)\)")


@dataclass
class Row:
    from_state: str
    event: str
    to_state: str
    guard: str | None
    action: str | None


@dataclass
class StateInfo:
    name: str
    on_entry: str | None = None
    on_execute: str | None = None
    on_exit: str | None = None
    timeout_ms: int = 0
    min_dwell_ms: int = 0


@dataclass
class Machine:
    name: str            # SM_Config_t variable name
    file: str
    initial_state: str
    rows: list[Row]
    states: dict[str, StateInfo]
    findings: list[tuple[str, str, str]] = field(default_factory=list)
    # (severity, check-id, message)
    multiplicity: int = 1   # identical definitions collapsed (test fixtures)

    @property
    def is_test_fixture(self) -> bool:
        return self.file.startswith("tests/")

    def signature(self) -> tuple:
        return (
            self.file, self.initial_state,
            tuple((r.from_state, r.event, r.to_state, r.guard, r.action)
                  for r in self.rows),
            tuple(sorted((s.name, s.on_entry, s.on_execute, s.on_exit,
                          s.timeout_ms, s.min_dwell_ms)
                         for s in self.states.values())),
        )


def _clean(value: str) -> str:
    """Normalize an initializer value: strip casts, suffixes, parens."""
    v = value.strip()
    while True:
        stripped = CAST_RE.sub("", v).strip()
        if stripped != v:
            v = stripped
            continue
        if v.startswith("(") and v.endswith(")"):
            v = v[1:-1].strip()
            continue
        break
    v = re.sub(r"([0-9])[uUlL]+$", r"\1", v)
    return v


def _int_or_zero(value: str) -> int:
    v = _clean(value)
    try:
        return int(v, 0)
    except ValueError:
        return 0


def _match_brace(text: str, open_idx: int) -> int:
    """Return index just past the '}' matching text[open_idx] == '{'."""
    depth = 0
    for i in range(open_idx, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
    return len(text)


def _split_top(text: str) -> list[str]:
    """Split initializer text at top-level commas."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c in "{([":
            depth += 1
        elif c in "})]":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    tail = text[start:].strip()
    if tail:
        parts.append(tail)
    return [p.strip() for p in parts if p.strip()]


def _parse_elements(body: str) -> list[tuple[str | None, str]]:
    """Split an array initializer body into (designator, inner) elements."""
    elements: list[tuple[str | None, str]] = []
    for part in _split_top(body):
        designator = None
        m = re.match(r"^\[\s*(\w+)\s*\]\s*=\s*", part)
        if m:
            designator = m.group(1)
            part = part[m.end():].strip()
        if part.startswith("{") and part.endswith("}"):
            elements.append((designator, part[1:-1]))
    return elements


def _fields(inner: str, positional_order: list[str]) -> dict[str, str]:
    """Parse one struct initializer into a field map."""
    out: dict[str, str] = {}
    parts = _split_top(inner)
    if parts and parts[0].lstrip().startswith("."):
        for p in parts:
            m = re.match(r"^\.(\w+)\s*=\s*(.*)$", p, re.DOTALL)
            if m:
                out[m.group(1)] = _clean(m.group(2))
    else:
        for name, value in zip(positional_order, parts):
            out[name] = _clean(value)
    return out


def _opt(value: str | None) -> str | None:
    return None if value in (None, "NULL", "0") else value


def _parse_enums(text: str) -> dict[str, int]:
    """Collect enumerator -> value for simple enums (literals + auto-inc)."""
    values: dict[str, int] = {}
    for m in ENUM_RE.finditer(text):
        next_val = 0
        for entry in _split_top(m.group(1)):
            em = re.match(r"^(\w+)(?:\s*=\s*(.+))?$", entry.strip(), re.DOTALL)
            if not em:
                continue
            name = em.group(1)
            if em.group(2) is not None:
                try:
                    next_val = int(_clean(em.group(2)), 0)
                except ValueError:
                    continue
            values[name] = next_val
            next_val += 1
    return values


def extract_machines(root: Path, files: list[str]) -> list[Machine]:
    machines: list[Machine] = []
    enum_values: dict[str, int] = {}
    texts: dict[str, str] = {}

    for rel in files:
        raw = (root / rel).read_text(encoding="utf-8", errors="replace")
        text = COMMENT_RE.sub(" ", raw)
        texts[rel] = text
        enum_values.update(_parse_enums(text))

    for rel, text in texts.items():
        transitions: dict[str, list[Row]] = {}
        descs: dict[str, list[tuple[str | None, dict[str, str]]]] = {}

        for m in ARRAY_RE.finditer(text):
            kind, name = m.group(1), m.group(2)
            end = _match_brace(text, m.end() - 1)
            body = text[m.end():end - 1]
            if kind == "SM_Transition_t":
                rows = []
                for _, inner in _parse_elements(body):
                    f = _fields(inner, ["from_state", "event", "to_state",
                                        "_reserved", "guard", "action"])
                    if "from_state" in f and "to_state" in f:
                        rows.append(Row(
                            from_state=f["from_state"],
                            event=f.get("event", "?"),
                            to_state=f["to_state"],
                            guard=_opt(f.get("guard")),
                            action=_opt(f.get("action")),
                        ))
                transitions[name] = rows
            else:
                descs[name] = [
                    (designator,
                     _fields(inner, ["on_entry", "on_execute", "on_exit",
                                     "timeout_ms", "min_dwell_ms"]))
                    for designator, inner in _parse_elements(body)
                ]

        for cm in CONFIG_RE.finditer(text):
            cfg_name = cm.group(1)
            end = _match_brace(text, cm.end() - 1)
            f = _fields(text[cm.end():end - 1],
                        ["states", "transitions", "transition_count",
                         "initial_state"])
            rows = transitions.get(f.get("transitions", ""), [])
            if not rows:
                continue

            # State universe: enum names used by this machine's rows
            row_states = {r.from_state for r in rows} | \
                         {r.to_state for r in rows}
            value_to_name = {enum_values[n]: n for n in sorted(row_states)
                             if n in enum_values}

            states: dict[str, StateInfo] = {n: StateInfo(name=n)
                                            for n in sorted(row_states)}
            for i, (designator, df) in enumerate(
                    descs.get(f.get("states", ""), [])):
                name = designator or value_to_name.get(i, f"#{i}")
                info = states.setdefault(name, StateInfo(name=name))
                info.on_entry = _opt(df.get("on_entry"))
                info.on_execute = _opt(df.get("on_execute"))
                info.on_exit = _opt(df.get("on_exit"))
                info.timeout_ms = _int_or_zero(df.get("timeout_ms", "0"))
                info.min_dwell_ms = _int_or_zero(df.get("min_dwell_ms", "0"))

            machine = Machine(
                name=cfg_name, file=rel,
                initial_state=f.get("initial_state", "?"),
                rows=rows, states=states,
            )
            _validate(machine)
            machines.append(machine)

    # Collapse identical definitions (test files re-declare the same local
    # config per test case) into one machine with a multiplicity count.
    unique: dict[tuple, Machine] = {}
    for m in machines:
        sig = m.signature()
        if sig in unique:
            unique[sig].multiplicity += 1
        else:
            unique[sig] = m

    return sorted(unique.values(), key=lambda m: (m.file, m.name))


def _validate(m: Machine) -> None:
    # V1: reachability from initial_state via transition rows
    reached = {m.initial_state}
    frontier = [m.initial_state]
    while frontier:
        cur = frontier.pop()
        for r in m.rows:
            if r.from_state == cur and r.to_state not in reached:
                reached.add(r.to_state)
                frontier.append(r.to_state)
    for name in sorted(m.states):
        if name not in reached:
            m.findings.append((
                "WARN", "V1-unreachable",
                f"state `{name}` is unreachable from initial state "
                f"`{m.initial_state}`"))

    # V2: timeout configured but no timeout-event route from that state
    for name, info in sorted(m.states.items()):
        if info.timeout_ms > 0:
            routed = any(r.from_state == name and r.event in TIMEOUT_NAMES
                         for r in m.rows)
            if not routed:
                m.findings.append((
                    "WARN", "V2-timeout-unrouted",
                    f"state `{name}` sets timeout_ms={info.timeout_ms} but "
                    f"no transition consumes SM_EVT_TIMEOUT from it -- the "
                    f"timeout event is posted and silently discarded"))

    # V3: dwell longer than timeout
    for name, info in sorted(m.states.items()):
        if 0 < info.timeout_ms < info.min_dwell_ms:
            m.findings.append((
                "INFO", "V3-dwell-gt-timeout",
                f"state `{name}`: min_dwell_ms={info.min_dwell_ms} > "
                f"timeout_ms={info.timeout_ms}; effective timeout "
                f"transition time is max() of the two"))

    # V4: terminal states
    sources = {r.from_state for r in m.rows}
    for name in sorted(m.states):
        if name not in sources:
            m.findings.append((
                "INFO", "V4-terminal",
                f"state `{name}` has no outgoing transitions (terminal)"))

    # V5: fully-guarded (from,event) groups -- event droppable at runtime
    groups: dict[tuple[str, str], list[Row]] = {}
    for r in m.rows:
        groups.setdefault((r.from_state, r.event), []).append(r)
    for (frm, evt), rows in sorted(groups.items()):
        if all(r.guard for r in rows):
            m.findings.append((
                "INFO", "V5-all-guarded",
                f"every transition for ({frm}, {evt}) is guarded -- the "
                f"event is discarded when all guards return false"))
        # V6: an unguarded row shadows every later row for the same pair
        # (the engine returns the first row whose guard passes, in order)
        for i, r in enumerate(rows):
            if r.guard is None and i < len(rows) - 1:
                m.findings.append((
                    "WARN", "V6-shadowed-row",
                    f"({frm}, {evt}): row {i} is unguarded, so "
                    f"{len(rows) - 1 - i} later row(s) can never be taken"))
                break
        # V7: duplicate exact rows
        seen: set[tuple] = set()
        for r in rows:
            key = (r.to_state, r.guard, r.action)
            if key in seen:
                m.findings.append((
                    "WARN", "V7-duplicate-row",
                    f"({frm}, {evt}): duplicate row -> {r.to_state}"))
            seen.add(key)


def _short_event(event: str) -> str:
    return "TIMEOUT" if event in TIMEOUT_NAMES else event


def render_machines(machines: list[Machine]) -> str:
    lines: list[str] = []
    app = [m for m in machines if not m.is_test_fixture]
    fixtures = [m for m in machines if m.is_test_fixture]
    app_warn = sum(1 for m in app
                   for sev, _, _ in m.findings if sev == "WARN")
    app_info = sum(1 for m in app
                   for sev, _, _ in m.findings if sev == "INFO")
    lines.append("# State Machines")
    lines.append("")
    lines.append("Machine-level graph extracted from SM_Config_t / "
                 "SM_Transition_t / SM_StateDesc_t tables by `graphify`. "
                 "Regenerate with `python3 -m graphify.watch`.")
    lines.append("")
    lines.append(f"**{len(app)} application machines** -- validator "
                 f"findings: **{app_warn} WARN**, {app_info} INFO. "
                 f"({len(fixtures)} test fixtures listed separately below; "
                 f"fixtures are deliberately partial machines, so their "
                 f"findings are expected and excluded from the headline.)")
    lines.append("")
    lines.append("# Application machines")
    lines.append("")
    for m in app:
        _render_machine(m, lines)
    lines.append("# Test fixtures")
    lines.append("")
    lines.append("Minimal machines declared by test suites. Unreachable / "
                 "terminal states here are usually intentional scaffolding.")
    lines.append("")
    for m in fixtures:
        _render_machine(m, lines)

    return "\n".join(lines) + "\n"


def _render_machine(m: Machine, lines: list[str]) -> None:
    mult = f" (defined x{m.multiplicity})" if m.multiplicity > 1 else ""
    lines.append(f"## `{m.name}` -- `{m.file}`{mult}")
    lines.append("")
    lines.append(f"Initial state: `{m.initial_state}` | "
                 f"states: {len(m.states)} | "
                 f"transitions: {len(m.rows)}")
    lines.append("")
    lines.append("```mermaid")
    lines.append("stateDiagram-v2")
    lines.append(f"    [*] --> {m.initial_state}")
    for r in m.rows:
        label = _short_event(r.event)
        if r.guard:
            label += f" [{r.guard}]"
        if r.action:
            label += f" / {r.action}"
        lines.append(f"    {r.from_state} --> {r.to_state} : {label}")
    lines.append("```")
    lines.append("")
    timed = [s for s in m.states.values()
             if s.timeout_ms or s.min_dwell_ms]
    if timed:
        lines.append("| State | timeout_ms | min_dwell_ms |")
        lines.append("|---|---:|---:|")
        for s in sorted(timed, key=lambda s: s.name):
            lines.append(f"| `{s.name}` | {s.timeout_ms} "
                         f"| {s.min_dwell_ms} |")
        lines.append("")
    if m.findings:
        lines.append("Validator findings:")
        lines.append("")
        for sev, check, msg in m.findings:
            lines.append(f"- **{sev}** ({check}): {msg}")
    else:
        lines.append("Validator findings: none.")
    lines.append("")
