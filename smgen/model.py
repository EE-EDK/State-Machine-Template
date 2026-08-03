"""Schema v1 parsing: TOML -> Machine dataclasses.

Everything rejected here is a *schema* error (malformed model), raised
as ModelError with a path for context. Semantic checks on well-formed
models (reachability, dead rows, ...) live in validate.py.

Design notes (decision references are docs_dev/phase_b_model_plan.md):
- D15: `timeout` requires `goto` -- an unrouted timeout cannot be
  expressed. `timeout` also rejects guards: a guard that fails would
  silently re-wedge the state, which is the exact defect class D15
  exists to kill.
- Declaration order is semantic: state and event order defines the
  generated enum values, and row order within an event group defines the
  engine's guard-fallthrough search order. The parser preserves both.
- The canonical hash (D16) hashes parsed semantics, not file bytes, so
  comments and formatting never change the hash while any semantic edit
  does.
"""

from __future__ import annotations

import hashlib
import json
import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from . import SCHEMA_VERSION

MAX_STATES = 255
MAX_EVENTS = 65534
MAX_MS = 2**31 - 1

_C_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

_TOP_KEYS = {"schema", "machine", "prefix", "initial", "events", "states"}
_STATE_KEYS = {"entry", "execute", "exit", "timeout", "min_dwell_ms",
               "on", "allow_drop", "parent"}
_TIMEOUT_KEYS = {"after_ms", "goto", "action"}
_ROW_KEYS = {"goto", "if", "action"}


class ModelError(ValueError):
    """Malformed model. Message includes a `path:` context prefix."""

    def __init__(self, path: str, message: str):
        super().__init__(f"{path}: {message}")
        self.path = path


@dataclass(frozen=True)
class Row:
    """One transition: state x event -> goto, with optional guard/action.

    Order within a (state, event) group is the engine's search order.
    """
    event: str
    goto: str
    guard: str | None = None
    action: str | None = None


@dataclass
class StateModel:
    name: str
    entry: str | None = None
    execute: str | None = None
    exit: str | None = None
    timeout_after_ms: int = 0
    timeout_goto: str | None = None
    timeout_action: str | None = None
    min_dwell_ms: int = 0
    rows: list[Row] = field(default_factory=list)
    allow_drop: frozenset[str] = frozenset()


@dataclass
class Machine:
    name: str
    prefix: str
    initial: str
    events: list[str]
    states: dict[str, StateModel]   # declaration order preserved
    source: str = ""

    # ------------------------------------------------------------------
    def canonical(self) -> dict:
        """Semantic content in a deterministic structure (order-preserving
        lists for everything where order is meaningful)."""
        return {
            "schema": SCHEMA_VERSION,
            "machine": self.name,
            "prefix": self.prefix,
            "initial": self.initial,
            "events": list(self.events),
            "states": [
                [s.name, {
                    "entry": s.entry,
                    "execute": s.execute,
                    "exit": s.exit,
                    "timeout_after_ms": s.timeout_after_ms,
                    "timeout_goto": s.timeout_goto,
                    "timeout_action": s.timeout_action,
                    "min_dwell_ms": s.min_dwell_ms,
                    "rows": [[r.event, r.goto, r.guard, r.action]
                             for r in s.rows],
                    "allow_drop": sorted(s.allow_drop),
                }]
                for s in self.states.values()
            ],
        }

    def hash(self) -> str:
        """64-bit model hash (D16): sha256 of canonical JSON, first 16
        hex digits."""
        blob = json.dumps(self.canonical(), sort_keys=True,
                          separators=(",", ":")).encode("utf-8")
        return hashlib.sha256(blob).hexdigest()[:16]


# ----------------------------------------------------------------------
# Parsing helpers
# ----------------------------------------------------------------------

def _require(cond: bool, path: str, msg: str) -> None:
    if not cond:
        raise ModelError(path, msg)


def _ident(value: object, path: str) -> str:
    _require(isinstance(value, str), path, f"expected string, got {value!r}")
    _require(bool(_C_IDENT.match(value)), path,
             f"{value!r} is not a valid C identifier")
    return value  # type: ignore[return-value]


def _ms(value: object, path: str, minimum: int) -> int:
    _require(isinstance(value, int) and not isinstance(value, bool), path,
             f"expected integer milliseconds, got {value!r}")
    _require(minimum <= value <= MAX_MS, path,  # type: ignore[operator]
             f"must be in [{minimum}, {MAX_MS}] (engine limit), "
             f"got {value!r}")
    return value  # type: ignore[return-value]


def _check_keys(table: dict, allowed: set[str], path: str) -> None:
    unknown = sorted(set(table) - allowed)
    _require(not unknown, path, f"unknown key(s): {', '.join(unknown)}")


def _parse_row(spec: object, event: str, states: set[str],
               path: str) -> Row:
    if isinstance(spec, str):
        _require(spec in states, path, f"goto references undeclared "
                 f"state {spec!r}")
        return Row(event=event, goto=spec)
    _require(isinstance(spec, dict), path,
             f"expected state name, table, or array of tables, "
             f"got {spec!r}")
    assert isinstance(spec, dict)
    _check_keys(spec, _ROW_KEYS, path)
    _require("goto" in spec, path, "transition requires `goto`")
    goto = spec["goto"]
    _require(isinstance(goto, str) and goto in states, path,
             f"goto references undeclared state {goto!r}")
    guard = _ident(spec["if"], f"{path}.if") if "if" in spec else None
    action = (_ident(spec["action"], f"{path}.action")
              if "action" in spec else None)
    return Row(event=event, goto=goto, guard=guard, action=action)


def _parse_state(name: str, table: object, events: list[str],
                 states: set[str]) -> StateModel:
    path = f"states.{name}"
    _require(isinstance(table, dict), path, "state must be a table")
    assert isinstance(table, dict)
    _check_keys(table, _STATE_KEYS, path)
    _require("parent" not in table, f"{path}.parent",
             "hierarchical states are reserved for a future schema "
             "version -- the engine's HSM semantics are not yet "
             "generator-supported (see phase_b_model_plan.md Non-Goals)")

    s = StateModel(name=name)
    for role in ("entry", "execute", "exit"):
        if role in table:
            setattr(s, role, _ident(table[role], f"{path}.{role}"))

    if "min_dwell_ms" in table:
        s.min_dwell_ms = _ms(table["min_dwell_ms"],
                             f"{path}.min_dwell_ms", 0)

    if "timeout" in table:
        tpath = f"{path}.timeout"
        t = table["timeout"]
        _require(isinstance(t, dict), tpath, "timeout must be a table")
        assert isinstance(t, dict)
        _require("if" not in t and "guard" not in t, tpath,
                 "timeout transitions cannot be guarded -- a failing "
                 "guard would silently discard the timeout (D15)")
        _check_keys(t, _TIMEOUT_KEYS, tpath)
        _require("after_ms" in t, tpath, "timeout requires `after_ms`")
        _require("goto" in t, tpath,
                 "timeout requires `goto` -- unrouted timeouts are "
                 "unrepresentable by design (D15)")
        s.timeout_after_ms = _ms(t["after_ms"], f"{tpath}.after_ms", 1)
        goto = t["goto"]
        _require(isinstance(goto, str) and goto in states, f"{tpath}.goto",
                 f"goto references undeclared state {goto!r}")
        s.timeout_goto = goto
        if "action" in t:
            s.timeout_action = _ident(t["action"], f"{tpath}.action")

    if "on" in table:
        on = table["on"]
        _require(isinstance(on, dict), f"{path}.on", "`on` must be a table")
        assert isinstance(on, dict)
        for event, spec in on.items():
            epath = f"{path}.on.{event}"
            _require(event in events, epath,
                     f"undeclared event {event!r}")
            if isinstance(spec, list):
                _require(len(spec) > 0, epath,
                         "empty transition list")
                for i, item in enumerate(spec):
                    s.rows.append(_parse_row(item, event, states,
                                             f"{epath}[{i}]"))
            else:
                s.rows.append(_parse_row(spec, event, states, epath))

    if "allow_drop" in table:
        ad = table["allow_drop"]
        apath = f"{path}.allow_drop"
        _require(isinstance(ad, list) and
                 all(isinstance(e, str) for e in ad), apath,
                 "allow_drop must be a list of event names")
        assert isinstance(ad, list)
        routed = {r.event for r in s.rows}
        for e in ad:
            _require(e in events, apath, f"undeclared event {e!r}")
            _require(e in routed, apath,
                     f"allow_drop for {e!r} but this state has no "
                     f"transitions on it")
        s.allow_drop = frozenset(ad)

    return s


# ----------------------------------------------------------------------
# Entry points
# ----------------------------------------------------------------------

def parse_machine(data: dict, source: str = "") -> Machine:
    """Parse an already-loaded TOML document into a Machine."""
    _check_keys(data, _TOP_KEYS, "(top level)")

    _require("schema" in data, "schema", "missing (must be 1)")
    _require(data["schema"] == SCHEMA_VERSION, "schema",
             f"unsupported schema version {data['schema']!r} "
             f"(this smgen supports {SCHEMA_VERSION})")

    _require("machine" in data, "machine", "missing")
    name = _ident(data["machine"], "machine")
    prefix = (_ident(data["prefix"], "prefix")
              if "prefix" in data else name.upper())

    _require("events" in data, "events", "missing")
    events = data["events"]
    _require(isinstance(events, list) and len(events) > 0, "events",
             "must be a non-empty array of event names")
    assert isinstance(events, list)
    events = [_ident(e, f"events[{i}]") for i, e in enumerate(events)]
    _require(len(set(events)) == len(events), "events",
             "duplicate event names")
    _require(len(events) <= MAX_EVENTS, "events",
             f"engine supports at most {MAX_EVENTS} events")

    _require("states" in data, "states", "missing")
    states_tbl = data["states"]
    _require(isinstance(states_tbl, dict) and len(states_tbl) > 0,
             "states", "must be a non-empty table of states")
    assert isinstance(states_tbl, dict)
    state_names = [_ident(n, f"states.{n}") for n in states_tbl]
    _require(len(states_tbl) <= MAX_STATES, "states",
             f"engine supports at most {MAX_STATES} states")
    name_set = set(state_names)

    _require("initial" in data, "initial", "missing")
    initial = data["initial"]
    _require(isinstance(initial, str) and initial in name_set, "initial",
             f"references undeclared state {initial!r}")

    states = {
        n: _parse_state(n, states_tbl[n], events, name_set)
        for n in state_names
    }

    return Machine(name=name, prefix=prefix, initial=initial,
                   events=events, states=states, source=source)


def load_machine(path: Path | str) -> Machine:
    """Load and parse a machine model from a TOML file."""
    p = Path(path)
    try:
        with p.open("rb") as f:
            data = tomllib.load(f)
    except tomllib.TOMLDecodeError as e:
        raise ModelError(str(p), f"TOML parse error: {e}") from e
    return parse_machine(data, source=str(p))
