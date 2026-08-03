"""Validator tests: every V-check fires on a crafted model and stays
quiet on the pilot models under models/."""

import tomllib
import unittest
from pathlib import Path

from smgen.model import load_machine, parse_machine
from smgen.validate import ERROR, WARN, validate, worst_severity

REPO = Path(__file__).resolve().parents[2]


def parse(toml_text: str):
    return parse_machine(tomllib.loads(toml_text))


def checks(findings):
    return {f.check for f in findings}


class TestVChecks(unittest.TestCase):
    def test_v1_unreachable_is_error(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on = { GO = "A" }
[states.ORPHAN]
on = { GO = "A" }
"""))
        self.assertIn("V1-unreachable", checks(f))
        self.assertEqual(worst_severity(f), ERROR)

    def test_v1_timeout_route_counts_as_reachable(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
timeout = { after_ms = 10, goto = "FAULT" }
on = { GO = "A" }
[states.FAULT]
on = { GO = "A" }
"""))
        self.assertNotIn("V1-unreachable", checks(f))

    def test_v3_dwell_gt_timeout_warns(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
min_dwell_ms = 500
timeout = { after_ms = 100, goto = "B" }
on = { GO = "B" }
[states.B]
on = { GO = "A" }
"""))
        self.assertIn("V3-dwell-gt-timeout", checks(f))

    def test_v4_terminal_is_info(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on = { GO = "HALT" }
[states.HALT]
"""))
        self.assertIn("V4-terminal", checks(f))
        self.assertEqual(worst_severity(f), "INFO")

    def test_v5_all_guarded_warns(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on.GO = { goto = "B", if = "g" }
[states.B]
on = { GO = "A" }
"""))
        self.assertIn("V5-all-guarded", checks(f))

    def test_v5_silenced_by_allow_drop(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
allow_drop = ["GO"]
on.GO = { goto = "B", if = "g" }
[states.B]
on = { GO = "A" }
"""))
        self.assertNotIn("V5-all-guarded", checks(f))

    def test_v6_row_after_unguarded_is_error(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on.GO = [
  { goto = "B" },
  { goto = "A", if = "g" },
]
[states.B]
on = { GO = "A" }
"""))
        self.assertIn("V6-unreachable-row", checks(f))
        self.assertEqual(worst_severity(f), ERROR)

    def test_v6_guarded_then_unguarded_is_fine(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on.GO = [
  { goto = "B", if = "g" },
  { goto = "A" },
]
[states.B]
on = { GO = "A" }
"""))
        self.assertNotIn("V6-unreachable-row", checks(f))

    def test_v7_duplicate_row_is_error(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on.GO = [
  { goto = "B", if = "g" },
  { goto = "B", if = "g" },
]
[states.B]
on = { GO = "A" }
"""))
        self.assertIn("V7-duplicate-row", checks(f))

    def test_v8_unused_event_warns(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO", "NEVER"]
[states.A]
on = { GO = "A" }
"""))
        self.assertIn("V8-unused-event", checks(f))

    def test_v9_role_conflict_is_error(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
entry = "shared_fn"
on.GO = { goto = "B", if = "shared_fn" }
[states.B]
on = { GO = "A" }
"""))
        self.assertIn("V9-role-conflict", checks(f))
        self.assertEqual(worst_severity(f), ERROR)

    def test_strict_promotes_warn(self):
        f = validate(parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO", "NEVER"]
[states.A]
on = { GO = "A" }
"""))
        self.assertEqual(worst_severity(f), WARN)
        self.assertEqual(worst_severity(f, strict=True), ERROR)


class TestPilotModels(unittest.TestCase):
    """B1 exit criterion: the three pilot models validate clean."""

    def _assert_clean(self, name: str):
        m = load_machine(REPO / "models" / name)
        findings = validate(m)
        self.assertEqual(
            [], findings,
            f"{name} should have zero findings, got: "
            f"{[str(x) for x in findings]}")

    def test_blinky_clean(self):
        self._assert_clean("blinky.toml")

    def test_basic_clean(self):
        self._assert_clean("basic.toml")

    def test_sensor_pipeline_clean(self):
        self._assert_clean("sensor_pipeline.toml")

    def test_hashes_stable(self):
        # Regenerating the hash from disk twice must agree (determinism)
        for name in ("blinky.toml", "basic.toml", "sensor_pipeline.toml"):
            p = REPO / "models" / name
            self.assertEqual(load_machine(p).hash(),
                             load_machine(p).hash())


if __name__ == "__main__":
    unittest.main()
