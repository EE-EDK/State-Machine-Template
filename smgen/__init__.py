"""smgen -- state machine model compiler (Phase B).

The model is the source of truth: a machine is defined once in a TOML
file (schema v1, see docs_dev/phase_b_model_plan.md) and smgen generates
the C tables the engine consumes. Validation runs before any C exists;
structural defects fail generation instead of shipping.

Phase status:
  B1 (this): schema + parser + validator + `validate`/`hash` CLI.
  B2: C emitters (`generate`).
  B3: drift + round-trip gate (`check`).
  B4: per-state transition index emission + runtime hash API.

Zero third-party dependencies by design (D13): TOML parsing uses stdlib
`tomllib` (Python >= 3.11). The internal model (smgen.model.Machine) is
format-agnostic; emitters and validators depend only on it.

CLI:
    python3 -m smgen validate [--strict] model.toml [more.toml ...]
    python3 -m smgen hash model.toml
"""

__version__ = "0.1.0"

SCHEMA_VERSION = 1
