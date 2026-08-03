"""Semantic validation (V1..V10) on a parsed Machine.

Schema-level malformations never reach here (model.py rejects them).
These checks catch well-formed models that describe broken machines.

Severities:
  ERROR -- generation must not proceed
  WARN  -- generated, but reported; --strict promotes to ERROR
  INFO  -- observation only

V2 (timeout without route) has no check here: the schema makes it
unrepresentable (D15). graphify's extractor keeps V2 for hand-written C.
"""

from __future__ import annotations

from dataclasses import dataclass

from .model import MAX_EVENTS, MAX_STATES, Machine, Row

ERROR = "ERROR"
WARN = "WARN"
INFO = "INFO"


@dataclass(frozen=True)
class Finding:
    severity: str
    check: str
    message: str

    def __str__(self) -> str:
        return f"{self.severity} ({self.check}): {self.message}"


def _timeout_row(s) -> Row | None:
    if s.timeout_goto is None:
        return None
    return Row(event="(timeout)", goto=s.timeout_goto,
               action=s.timeout_action)


def validate(m: Machine) -> list[Finding]:
    findings: list[Finding] = []
    add = findings.append

    # ------------------------------------------------------------------
    # V1: reachability from initial (transitions + timeout routes)
    reached = {m.initial}
    frontier = [m.initial]
    while frontier:
        cur = frontier.pop()
        s = m.states[cur]
        targets = [r.goto for r in s.rows]
        if s.timeout_goto is not None:
            targets.append(s.timeout_goto)
        for t in targets:
            if t not in reached:
                reached.add(t)
                frontier.append(t)
    for name in m.states:
        if name not in reached:
            add(Finding(ERROR, "V1-unreachable",
                        f"state `{name}` is unreachable from initial "
                        f"state `{m.initial}`"))

    # ------------------------------------------------------------------
    # V3: dwell longer than timeout
    for s in m.states.values():
        if 0 < s.timeout_after_ms < s.min_dwell_ms:
            add(Finding(WARN, "V3-dwell-gt-timeout",
                        f"state `{s.name}`: min_dwell_ms="
                        f"{s.min_dwell_ms} > timeout after_ms="
                        f"{s.timeout_after_ms}; the timeout transition "
                        f"cannot run before the dwell elapses "
                        f"(effective timeout = max of the two)"))

    # ------------------------------------------------------------------
    # V4: terminal states (no outgoing transitions, no timeout route)
    for s in m.states.values():
        if not s.rows and s.timeout_goto is None:
            add(Finding(INFO, "V4-terminal",
                        f"state `{s.name}` has no outgoing transitions "
                        f"(terminal)"))

    # ------------------------------------------------------------------
    # Per-(state, event) groups, preserving row order
    for s in m.states.values():
        groups: dict[str, list[Row]] = {}
        for r in s.rows:
            groups.setdefault(r.event, []).append(r)

        for event, rows in groups.items():
            # V5: all guarded and not explicitly allowed to drop
            if all(r.guard for r in rows) and event not in s.allow_drop:
                add(Finding(WARN, "V5-all-guarded",
                            f"({s.name}, {event}): every transition is "
                            f"guarded -- the event is silently discarded "
                            f"when all guards fail. Add an unguarded "
                            f"fallthrough, or declare intent with "
                            f"allow_drop = [\"{event}\"]"))

            # V6: rows after an unguarded row can never be evaluated
            for i, r in enumerate(rows):
                if r.guard is None and i < len(rows) - 1:
                    add(Finding(ERROR, "V6-unreachable-row",
                                f"({s.name}, {event}): row {i} is "
                                f"unguarded, so row(s) {i + 1}.."
                                f"{len(rows) - 1} can never be evaluated "
                                f"(engine takes the first passing row in "
                                f"order)"))
                    break

            # V7: duplicate exact rows
            seen: set[Row] = set()
            for r in rows:
                if r in seen:
                    add(Finding(ERROR, "V7-duplicate-row",
                                f"({s.name}, {event}): duplicate "
                                f"transition row (goto={r.goto}, "
                                f"guard={r.guard}, action={r.action})"))
                seen.add(r)

    # ------------------------------------------------------------------
    # V8: declared events never used
    used = {r.event for s in m.states.values() for r in s.rows}
    for e in m.events:
        if e not in used:
            add(Finding(WARN, "V8-unused-event",
                        f"event `{e}` is declared but no state routes it"))

    # ------------------------------------------------------------------
    # V9: one callback name in incompatible roles (signatures differ)
    guards: set[str] = set()
    actions: set[str] = set()
    statecbs: set[str] = set()
    for s in m.states.values():
        for r in s.rows:
            if r.guard:
                guards.add(r.guard)
            if r.action:
                actions.add(r.action)
        if s.timeout_action:
            actions.add(s.timeout_action)
        for cb in (s.entry, s.execute, s.exit):
            if cb:
                statecbs.add(cb)
    for name in sorted((guards & actions) | (guards & statecbs)
                       | (actions & statecbs)):
        roles = [role for role, group in
                 (("guard", guards), ("action", actions),
                  ("state-callback", statecbs)) if name in group]
        add(Finding(ERROR, "V9-role-conflict",
                    f"callback `{name}` is used as {' and '.join(roles)} "
                    f"-- these have incompatible C signatures"))

    # ------------------------------------------------------------------
    # V10: engine limits (belt-and-suspenders; schema also enforces)
    if len(m.states) > MAX_STATES:
        add(Finding(ERROR, "V10-limits",
                    f"{len(m.states)} states exceeds engine limit "
                    f"{MAX_STATES}"))
    if len(m.events) > MAX_EVENTS:
        add(Finding(ERROR, "V10-limits",
                    f"{len(m.events)} events exceeds engine limit "
                    f"{MAX_EVENTS}"))

    return findings


def worst_severity(findings: list[Finding], strict: bool = False) -> str:
    """ERROR / WARN / INFO / '' -- with --strict, WARN counts as ERROR."""
    sevs = {f.severity for f in findings}
    if ERROR in sevs or (strict and WARN in sevs):
        return ERROR
    if WARN in sevs:
        return WARN
    if INFO in sevs:
        return INFO
    return ""
