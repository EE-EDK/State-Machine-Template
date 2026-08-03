"""Schema-level tests: parsing, rejection cases, hash determinism."""

import tomllib
import unittest

from smgen.model import ModelError, parse_machine

MINIMAL = """
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on = { GO = "B" }
[states.B]
"""


def parse(toml_text: str):
    return parse_machine(tomllib.loads(toml_text))


class TestParseGood(unittest.TestCase):
    def test_minimal_parses(self):
        m = parse(MINIMAL)
        self.assertEqual(m.name, "m")
        self.assertEqual(m.prefix, "M")           # default = upper(name)
        self.assertEqual(m.initial, "A")
        self.assertEqual(list(m.states), ["A", "B"])
        self.assertEqual(m.states["A"].rows[0].goto, "B")

    def test_declaration_order_preserved(self):
        m = parse("""
schema = 1
machine = "m"
initial = "Z"
events = ["E2", "E1"]
[states.Z]
on = { E1 = "A" }
[states.A]
on = { E2 = "Z" }
""")
        self.assertEqual(m.events, ["E2", "E1"])
        self.assertEqual(list(m.states), ["Z", "A"])

    def test_row_forms(self):
        m = parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on.GO = [
  { if = "g1", goto = "B", action = "a1" },
  { goto = "B" },
]
[states.B]
""")
        rows = m.states["A"].rows
        self.assertEqual(rows[0].guard, "g1")
        self.assertEqual(rows[0].action, "a1")
        self.assertIsNone(rows[1].guard)

    def test_timeout_full(self):
        m = parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
timeout = { after_ms = 100, goto = "B", action = "on_late" }
on = { GO = "B" }
[states.B]
""")
        a = m.states["A"]
        self.assertEqual(a.timeout_after_ms, 100)
        self.assertEqual(a.timeout_goto, "B")
        self.assertEqual(a.timeout_action, "on_late")


class TestParseRejections(unittest.TestCase):
    def assert_rejected(self, toml_text: str, fragment: str):
        with self.assertRaises(ModelError) as ctx:
            parse(toml_text)
        self.assertIn(fragment, str(ctx.exception))

    def test_missing_schema(self):
        self.assert_rejected(MINIMAL.replace("schema = 1\n", ""),
                             "schema")

    def test_wrong_schema_version(self):
        self.assert_rejected(MINIMAL.replace("schema = 1", "schema = 2"),
                             "unsupported schema version")

    def test_unknown_top_key(self):
        self.assert_rejected(MINIMAL + "\nbogus = 1\n", "unknown key")

    def test_initial_undeclared(self):
        self.assert_rejected(MINIMAL.replace('initial = "A"',
                                             'initial = "X"'),
                             "undeclared state")

    def test_goto_undeclared_state(self):
        self.assert_rejected(MINIMAL.replace('on = { GO = "B" }',
                                             'on = { GO = "X" }'),
                             "undeclared state")

    def test_on_undeclared_event(self):
        self.assert_rejected(MINIMAL.replace('on = { GO = "B" }',
                                             'on = { NOPE = "B" }'),
                             "undeclared event")

    def test_duplicate_events(self):
        self.assert_rejected(MINIMAL.replace('events = ["GO"]',
                                             'events = ["GO", "GO"]'),
                             "duplicate event")

    def test_empty_events(self):
        self.assert_rejected(MINIMAL.replace('events = ["GO"]',
                                             'events = []'),
                             "non-empty")

    def test_bad_identifier(self):
        self.assert_rejected(MINIMAL.replace('events = ["GO"]',
                                             'events = ["9GO"]'),
                             "not a valid C identifier")

    def test_timeout_requires_goto(self):
        self.assert_rejected("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
timeout = { after_ms = 100 }
on = { GO = "B" }
[states.B]
""", "unrepresentable by design")

    def test_timeout_rejects_guard(self):
        self.assert_rejected("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
timeout = { after_ms = 100, goto = "B", if = "g" }
on = { GO = "B" }
[states.B]
""", "cannot be guarded")

    def test_timeout_after_ms_range(self):
        self.assert_rejected("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
timeout = { after_ms = 0, goto = "B" }
on = { GO = "B" }
[states.B]
""", "engine limit")

    def test_parent_rejected(self):
        self.assert_rejected("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
parent = "B"
on = { GO = "B" }
[states.B]
""", "reserved")

    def test_row_missing_goto(self):
        self.assert_rejected("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on.GO = { action = "a" }
[states.B]
""", "requires `goto`")

    def test_empty_row_list(self):
        self.assert_rejected("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on.GO = []
[states.B]
""", "empty transition list")

    def test_allow_drop_without_route(self):
        self.assert_rejected("""
schema = 1
machine = "m"
initial = "A"
events = ["GO", "OTHER"]
[states.A]
allow_drop = ["OTHER"]
on = { GO = "B" }
[states.B]
""", "no \ntransitions on it".replace("\n", ""))


class TestHash(unittest.TestCase):
    def test_formatting_and_comments_do_not_change_hash(self):
        a = parse(MINIMAL)
        b = parse("""
# a comment
schema = 1

machine    =    "m"
initial = "A"
events = [ "GO" ]

[states.A]      # trailing comment
on = { GO = "B" }

[states.B]
""")
        self.assertEqual(a.hash(), b.hash())

    def test_semantic_change_changes_hash(self):
        a = parse(MINIMAL)
        b = parse(MINIMAL.replace('on = { GO = "B" }',
                                  'on = { GO = "A" }'))
        self.assertNotEqual(a.hash(), b.hash())

    def test_state_order_is_semantic(self):
        # Enum values follow declaration order, so order affects the hash
        a = parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.A]
on = { GO = "B" }
[states.B]
on = { GO = "A" }
""")
        b = parse("""
schema = 1
machine = "m"
initial = "A"
events = ["GO"]
[states.B]
on = { GO = "A" }
[states.A]
on = { GO = "B" }
""")
        self.assertNotEqual(a.hash(), b.hash())


if __name__ == "__main__":
    unittest.main()
