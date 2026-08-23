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
        # SM_Init is a macro over SM_Init_ since v4.1 (build-dimension check)
        self.assertEqual(writers, {"SM_Init_", "SM_Reset",
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


class DisAtomicityRepoTests(unittest.TestCase):
    """The v4.1 DIS fix, pinned as a repository invariant.

    The 2026-08-22 review found the field and its shadow written as two
    separately observable stores, so an ISR calling a documented ISR-safe
    reader between them asserted on data that was never corrupt. v4.1 routed
    every live-machine write through SM_DIS_ASSIGN. That fix was, until W2b,
    unproven by test -- and it cannot be proven by a runtime race harness,
    because a hook on the critical-section boundary has no seam to fire in
    between two adjacent non-critical stores (brief F-C).

    G16 proves it structurally instead. The detector was demonstrated against
    the known-bad revision 9427166~1, where it reports 6 ERRORs including the
    three live-machine sites v4.1 fixed. These tests keep it that way.
    """

    @classmethod
    def setUpClass(cls):
        cls.g = build_analysis(ROOT).g

    def _lib_sites(self):
        for fn in self.g.functions.values():
            if fn.unit != "lib":
                continue
            for site in fn.dis_sites:
                yield fn, site

    def test_every_live_machine_dis_write_is_atomic(self):
        torn = [f"{fn.name}:{site[3]}"
                for fn, site in self._lib_sites()
                if not site[4] and not fn.dis_exempt]
        self.assertEqual(torn, [],
                         "torn DIS pair(s) in the library -- the field and its "
                         "shadow are separately observable")

    def test_the_three_live_writers_use_dis_assign(self):
        by_fn = {}
        for fn, site in self._lib_sites():
            by_fn.setdefault(fn.name, set()).add(site[0])
        for name in ("sm_execute_transition", "SM_Reset", "SM_Error_Report"):
            self.assertIn(name, by_fn, f"{name} no longer writes a DIS pair")
            self.assertEqual(by_fn[name], {"SM_DIS_ASSIGN"},
                             f"{name} must write its DIS pair indivisibly")

    def test_only_construction_is_exempt(self):
        exempt = {fn.name for fn, _ in self._lib_sites() if fn.dis_exempt}
        self.assertEqual(exempt, {"SM_Init_"},
                         "only instance construction may write a DIS pair "
                         "non-atomically; anything else needs a real reason")

    def test_exemptions_carry_a_stated_reason(self):
        for fn, _ in self._lib_sites():
            if fn.dis_exempt:
                self.assertGreater(len(fn.dis_exempt), 20,
                                   f"{fn.name}: exemption without a rationale")


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
