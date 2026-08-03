"""smgen CLI.

B1 commands:
    validate [--strict] MODEL [MODEL ...]   parse + validate, exit 1 on
                                            ERROR (or WARN with --strict)
    hash MODEL                              print the canonical model hash

Exit codes: 0 ok, 1 validation failure, 2 malformed model / usage.
(`generate` and `check` arrive in B2/B3 -- see phase_b_model_plan.md.)
"""

from __future__ import annotations

import argparse
import sys

from .model import ModelError, load_machine
from .validate import ERROR, validate, worst_severity


def _cmd_validate(args: argparse.Namespace) -> int:
    failed = False
    for path in args.models:
        try:
            machine = load_machine(path)
        except ModelError as e:
            print(f"{path}: SCHEMA ERROR -- {e}")
            failed = True
            continue
        findings = validate(machine)
        verdict = worst_severity(findings, strict=args.strict)
        label = verdict if verdict else "OK"
        print(f"{path}: {label} -- machine `{machine.name}`, "
              f"{len(machine.states)} states, "
              f"{sum(len(s.rows) for s in machine.states.values())} "
              f"transitions, hash {machine.hash()}")
        for f in findings:
            print(f"  {f}")
        if verdict == ERROR:
            failed = True
    return 1 if failed else 0


def _cmd_hash(args: argparse.Namespace) -> int:
    print(load_machine(args.model).hash())
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="smgen",
        description="State machine model compiler (Phase B1: "
                    "validate/hash only)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_val = sub.add_parser("validate",
                           help="parse + validate model file(s)")
    p_val.add_argument("models", nargs="+", metavar="MODEL")
    p_val.add_argument("--strict", action="store_true",
                       help="treat WARN findings as errors (CI mode)")
    p_val.set_defaults(func=_cmd_validate)

    p_hash = sub.add_parser("hash",
                            help="print the canonical model hash (D16)")
    p_hash.add_argument("model", metavar="MODEL")
    p_hash.set_defaults(func=_cmd_hash)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except ModelError as e:
        print(f"SCHEMA ERROR -- {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
