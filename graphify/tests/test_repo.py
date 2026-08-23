"""Invariant tests against the real repository.

These pin facts the architecture docs assert (single state writer set,
ISR contracts, round-trip models, no call cycles) so a future change that
breaks one is caught by `ctest`, not by a reader of GRAPH_REPORT.md.
"""

from __future__ import annotations

import unittest
from pathlib import Path

from graphify.metrics import (articulation_points, betweenness, bowtie,
                              label_propagation, layers, modularity,
                              pagerank, scc, undirected)
from graphify.render import short
from graphify.watch import build_analysis

ROOT = Path(__file__).resolve().parents[2]


class RepoInvariants(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.a = build_analysis(ROOT)
        cls.g = cls.a.g

    def test_engine_core_calls(self):
        proc = self.g.functions["SM_Process@src/core/sm_engine.c"]
        names = {short(k) for k in proc.calls}
        self.assertTrue({"sm_event_dequeue", "sm_execute_transition",
                         "sm_resolve_transition", "SM_TimeEvt_Tick_"} <= names)

    def test_current_state_writers_are_exactly_three(self):
        writers = {short(k) for k, f in self.g.functions.items()
                   if f.unit == "lib" and "SM_Context.current_state" in f.writes}
        self.assertEqual(writers, {"SM_Init", "SM_Reset",
                                   "sm_execute_transition"})

    def test_isr_contracts_documented(self):
        isr = {f.name: f.isr for f in self.g.functions.values()
               if f.unit == "lib" and f.isr}
        self.assertEqual(isr.get("SM_PostEvent"), "safe")
        self.assertEqual(isr.get("SM_Process"), "unsafe")
        self.assertEqual(isr.get("SM_TimeEvt_Arm"), "safe")

    def test_hal_override_edges(self):
        t = self.g.functions["SM_Platform_GetTimeMs@tests/test_platform.c"]
        self.assertEqual(t.overrides,
                         "SM_Platform_GetTimeMs@src/platform/sm_platform_weak.c")
        # tests resolve to their own platform, not the weak default
        caller = next(f for f in self.g.functions.values()
                      if f.file == "tests/test_time_events.c"
                      and "SM_Platform_GetTimeMs@tests/test_platform.c" in f.calls)
        self.assertIsNotNone(caller)

    def test_models_roundtrip_match(self):
        for mc in self.a.model_checks:
            self.assertTrue(mc.ok, f"{mc.model}: {mc.diffs}")
        self.assertEqual(len(self.a.model_checks), 3)

    def test_no_call_cycles(self):
        self.assertEqual(self.a.sccs, [])

    def test_assertion_ids_unique_per_library_module(self):
        seen = set()
        for asr in self.g.assertions:
            if not asr.file.startswith("src/") or \
                    asr.macro == "SM_BOUNDED_LOOP_END":
                continue
            self.assertNotIn((asr.module, asr.id), seen, asr)
            seen.add((asr.module, asr.id))

    def test_callback_bindings_reach_engine(self):
        app = [b for b in self.a.bindings
               if b.machine.startswith("examples/")]
        self.assertTrue(app)
        for b in app:
            self.assertTrue(b.indirect_from, b)

    def test_every_example_machine_extracted(self):
        files = {m.file for m in self.a.machines if not m.is_test_fixture}
        for ex in ("basic", "blinky", "error_recovery", "multi_fsm",
                   "sensor_pipeline", "simulation"):
            self.assertIn(f"examples/{ex}_example.c", files)

    def test_report_renders_all_sections(self):
        from graphify.render import render_report, render_wiki, graph_json
        rep = render_report(self.a)
        for h in ("## Summary", "## Validator findings", "## Topology",
                  "## God nodes", "## Interface layer", "## Feature gates",
                  "## State access matrix", "## Critical sections",
                  "## Assertion map", "## Macro expansion map",
                  "## Machine ↔ code bindings", "## Test inventory",
                  "## Documentation cross-reference", "## Models ↔ examples",
                  "## Host tooling", "## Structural communities"):
            self.assertIn(h, rep, h)
        self.assertIn("## Core engine", render_wiki(self.a))
        data = graph_json(self.a)
        ids = {n["id"] for n in data["nodes"]}
        for e in data["edges"]:
            self.assertIn(e["src"], ids)
            self.assertIn(e["dst"], ids)


class MetricsTests(unittest.TestCase):
    def test_algorithms_on_toy_graph(self):
        adj = {"a": {"b"}, "b": {"c"}, "c": {"a"}, "d": {"a"}, "e": set()}
        comps = scc(adj)
        self.assertEqual(comps[0], ["a", "b", "c"])
        bc = betweenness(adj)
        self.assertGreater(bc["a"], bc["e"])
        pr = pagerank(adj)
        self.assertAlmostEqual(sum(pr.values()), 1.0, places=6)
        u = undirected(adj)
        self.assertIn("a", articulation_points(u))
        lp = label_propagation(u)
        self.assertEqual(len(lp), 5)
        self.assertLessEqual(modularity(u, lp), 1.0)
        ly = layers(adj)
        self.assertEqual(ly["e"], 0)
        self.assertEqual(ly["d"], ly["a"] + 1)
        tie = bowtie(adj, {"a"})
        self.assertIn("d", tie["in"])
        self.assertEqual(tie["disconnected"], {"e"})


if __name__ == "__main__":
    unittest.main()
