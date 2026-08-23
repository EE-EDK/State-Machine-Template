"""Unit tests for the graphify extractor on small synthetic C fixtures.

Every test pins one extraction behaviour a downstream report depends on.
A checker nobody has tested is a checker nobody should trust.
"""

from __future__ import annotations

import tempfile
import textwrap
import unittest
from pathlib import Path

from graphify.analyze import build_graph
from graphify.cparse import gate_lines, split_args, strip_noise


def _repo(files: dict[str, str]) -> Path:
    root = Path(tempfile.mkdtemp(prefix="graphify-test-"))
    for rel, text in files.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(textwrap.dedent(text), encoding="utf-8")
    return root


HEADER = """
    #ifndef T_H
    #define T_H
    #include <stdint.h>
    #ifndef SM_FEATURE_X
    #define SM_FEATURE_X (1U)
    #endif
    #ifndef SM_QUEUE_SIZE
    #define SM_QUEUE_SIZE (8U)
    #endif
    typedef struct SM_Context SM_Context_t;
    typedef SM_Context_t *SM_Handle_t;
    typedef void (*SM_StateCallback_t)(SM_Handle_t sm);
    typedef struct {
        volatile uint8_t head;
        uint8_t nMin;
    } SM_Queue_t;
    struct SM_Context {
        volatile uint16_t current_state;   /* comment; with semicolon */
        uint16_t state_dis;
        SM_Queue_t q;
    #if SM_FEATURE_X
        uint32_t gated_field;
    #endif
    };
    #if SM_FEATURE_ASSERT
    #define SM_REQUIRE(id_, expr_) ((expr_) ? ((void)0) : SM_Platform_Assert("m", (id_)))
    #else
    #define SM_REQUIRE(id_, expr_) ((void)0)
    #endif
    #define SM_DIS_VERIFY(f_, d_, t_, id_) SM_REQUIRE((id_), (t_)(f_) == (t_)(~(t_)(d_)))
    /** ISR-SAFE: may be called from interrupts. */
    bool SM_PostEvent(SM_Handle_t sm, uint16_t event, uint32_t data);
    /** NOT ISR-safe. */
    void SM_Process(SM_Handle_t sm);
    void SM_Platform_Assert(const char *module, int id);
    void SM_Platform_EnterCritical(void);
    void SM_Platform_ExitCritical(void);
    #endif
"""

ENGINE = """
    #include "t.h"
    SM_DEFINE_MODULE("sm_engine");
    static void helper(SM_Handle_t sm)
    {
        sm->q.nMin = 1U;
    }
    bool SM_PostEvent(SM_Handle_t sm, uint16_t event, uint32_t data)
    {
        SM_REQUIRE(500, sm != NULL);
        SM_Platform_EnterCritical();
        sm->q.head++;
        helper(sm);
        SM_Platform_ExitCritical();
        return true;
    }
    void SM_Process(SM_Handle_t sm)
    {
        const char *s = "SM_PostEvent(x)";   /* string must not make an edge */
        SM_DIS_VERIFY(sm->current_state, sm->state_dis, uint16_t, 201);
        sm->current_state = 3U;
        if (sm->q.head == 0U) { helper(sm); }
        SM_PostEvent(sm, 1U, 0U);
    }
    #if SM_FEATURE_X
    void SM_Gated(SM_Handle_t sm)
    {
        (void)sm;
    }
    #endif
"""

WEAK = """
    #include "t.h"
    SM_WEAK void SM_Platform_Assert(const char *module, int id) { (void)module; (void)id; }
    SM_WEAK void SM_Platform_EnterCritical(void) { }
    SM_WEAK void SM_Platform_ExitCritical(void) { }
"""

TEST = """
    #include "t.h"
    void SM_Platform_Assert(const char *module, int id) { (void)module; (void)id; }
    void SM_Platform_EnterCritical(void) { }
    void SM_Platform_ExitCritical(void) { }
    static void helper(void) { }
    void test_one(void)
    {
        helper();
        SM_PostEvent(NULL, 0U, 0U);
    }
"""


class CparseTests(unittest.TestCase):
    def test_strip_noise_is_length_preserving(self):
        src = 'a /* x\ny */ b "s;t" c // tail\nd'
        out = strip_noise(src)
        self.assertEqual(len(out), len(src))
        self.assertEqual(out.count("\n"), src.count("\n"))
        self.assertNotIn("s;t", out)

    def test_gate_lines_tracks_else_and_endif(self):
        text = "#if A\nx\n#else\ny\n#endif\nz\n#ifndef T_H\nw\n#endif\n"
        g = gate_lines(text)
        self.assertEqual(g[2], ("A",))
        self.assertEqual(g[4], ("!(A)",))
        self.assertEqual(g[6], ())
        self.assertEqual(g[8], ())   # include guard is noise

    def test_split_args_respects_nesting(self):
        self.assertEqual(split_args("a, (b, c), {d, e}, f"),
                         ["a", "(b, c)", "{d, e}", "f"])


class AnalyzeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = _repo({
            "include/t.h": HEADER,
            "src/core/sm_engine.c": ENGINE,
            "src/platform/weak.c": WEAK,
            "tests/test_x.c": TEST,
        })
        cls.g = build_graph(cls.root)

    def fn(self, key):
        return self.g.functions[key]

    def test_static_calls_resolve_in_file(self):
        proc = self.fn("SM_Process@src/core/sm_engine.c")
        self.assertIn("helper@src/core/sm_engine.c", proc.calls)
        self.assertNotIn("helper@tests/test_x.c", proc.calls)
        t = self.fn("test_one@tests/test_x.c")
        self.assertIn("helper@tests/test_x.c", t.calls)

    def test_link_unit_resolution_prefers_same_unit_then_lib(self):
        post = self.fn("SM_PostEvent@src/core/sm_engine.c")
        self.assertIn("SM_Platform_EnterCritical@src/platform/weak.c", post.calls)
        t = self.fn("test_one@tests/test_x.c")
        self.assertIn("SM_PostEvent@src/core/sm_engine.c", t.calls)

    def test_strings_do_not_create_edges(self):
        proc = self.fn("SM_Process@src/core/sm_engine.c")
        # SM_PostEvent IS called for real once; the string literal must not
        # add a second resolution target or a macro expansion.
        self.assertEqual(
            sorted(k for k in proc.calls if k.startswith("SM_PostEvent")),
            ["SM_PostEvent@src/core/sm_engine.c"])

    def test_macro_variants_and_expansion(self):
        m = self.g.macros["SM_REQUIRE"]
        self.assertEqual(len(m.variants), 2)
        self.assertEqual(m.variants[0].gate, ("SM_FEATURE_ASSERT",))
        self.assertIn("SM_Platform_Assert", m.variants[0].calls)
        self.assertTrue(m.variants[1].noop)
        self.assertIn("SM_REQUIRE", self.g.macros["SM_DIS_VERIFY"].calls)
        self.assertIn("SM_DIS_VERIFY",
                      self.fn("SM_Process@src/core/sm_engine.c").expands)

    def test_config_override_pattern(self):
        self.assertEqual(self.g.configs["SM_QUEUE_SIZE"].default, "8")
        self.assertIn("SM_FEATURE_X", self.g.configs)

    def test_struct_fields_gates_and_volatile(self):
        ctx = self.g.types["SM_Context"]
        self.assertEqual(list(ctx.fields), ["current_state", "state_dis",
                                            "q", "gated_field"])
        self.assertTrue(ctx.fields["current_state"].volatile)
        self.assertEqual(ctx.fields["gated_field"].gate, ("SM_FEATURE_X",))
        self.assertTrue(self.g.types["SM_Queue_t"].fields["head"].volatile)

    def test_alias_chain_resolves_handle_to_struct(self):
        from graphify.analyze import resolve_struct
        self.assertEqual(resolve_struct(self.g, "SM_Handle_t"), "SM_Context")

    def test_field_reads_writes_and_traversal(self):
        proc = self.fn("SM_Process@src/core/sm_engine.c")
        self.assertIn("SM_Context.current_state", proc.writes)
        self.assertIn("SM_Context.state_dis", proc.reads)
        self.assertIn("SM_Queue_t.head", proc.reads)
        self.assertIn("SM_Context.q", proc.traverses)
        post = self.fn("SM_PostEvent@src/core/sm_engine.c")
        self.assertIn("SM_Queue_t.head", post.writes)

    def test_volatile_write_outside_critsec_detected(self):
        proc = self.fn("SM_Process@src/core/sm_engine.c")
        self.assertIn("SM_Context.current_state", proc.unprotected_volatile)
        post = self.fn("SM_PostEvent@src/core/sm_engine.c")
        self.assertEqual(post.unprotected_volatile, set())
        self.assertEqual((post.crit_enter, post.crit_exit), (1, 1))
        self.assertIn("helper", post.in_critsec_calls)

    def test_isr_contract_from_header_doc(self):
        self.assertEqual(self.fn("SM_PostEvent@src/core/sm_engine.c").isr, "safe")
        self.assertEqual(self.fn("SM_Process@src/core/sm_engine.c").isr, "unsafe")

    def test_assertions_extracted_with_module(self):
        ids = {(a.module, a.id, a.macro) for a in self.g.assertions}
        self.assertIn(("sm_engine", 500, "SM_REQUIRE"), ids)
        self.assertIn(("sm_engine", 201, "SM_DIS_VERIFY"), ids)

    def test_feature_gate_on_function(self):
        self.assertEqual(self.fn("SM_Gated@src/core/sm_engine.c").gate,
                         ("SM_FEATURE_X",))

    def test_decl_implements_and_overrides(self):
        d = self.g.decls["SM_Platform_Assert"]
        self.assertIn("SM_Platform_Assert@src/platform/weak.c", d.implementations)
        t = self.fn("SM_Platform_Assert@tests/test_x.c")
        self.assertEqual(t.overrides, "SM_Platform_Assert@src/platform/weak.c")
        self.assertTrue(self.fn("SM_Platform_Assert@src/platform/weak.c").weak)


if __name__ == "__main__":
    unittest.main()


class LinkChecksTests(unittest.TestCase):
    """G15 (library vs app compile-time config) and V6/V7 (row shadowing)."""

    def test_abi_check_flags_count_and_layout_divergence(self):
        from graphify.link import abi_check
        root = _repo({
            "CMakeLists.txt": """
                add_library(sm_framework STATIC src/core/e.c)
                target_compile_definitions(sm_framework PRIVATE
                    SM_STATE_COUNT=4U
                    SM_EVENT_COUNT=8U
                )
            """,
            "include/t.h": HEADER.replace("SM_QUEUE_SIZE", "SM_EVENT_QUEUE_SIZE"),
            "src/core/e.c": ENGINE,
            "examples/big_example.c": """
                #define SM_STATE_COUNT (6U)
                #define SM_EVENT_COUNT (2U)
                #define SM_EVENT_QUEUE_SIZE (16U)
                #include "t.h"
                static const int rows[] = { SM_EVT_TIMEOUT };
                int main(void) { return rows[0]; }
            """,
            "examples/ok_example.c": """
                #define SM_STATE_COUNT (4U)
                #define SM_EVENT_COUNT (8U)
                #include "t.h"
                int main(void) { return 0; }
            """,
        })
        g = build_graph(root)
        issues = abi_check(root, g)
        msgs = {(i.severity, i.file, i.message.split(":")[0]) for i in issues}
        self.assertIn(("ERROR", "examples/big_example.c", "declares 6 states but the library was built with SM_STATE_COUNT=4"), msgs)
        self.assertTrue(any(i.file == "examples/big_example.c" and "route here is dead" in i.message and i.severity == "ERROR" for i in issues))
        self.assertTrue(any(i.file == "examples/big_example.c" and "SM_EVENT_QUEUE_SIZE=16" in i.message for i in issues))
        self.assertFalse(any(i.file == "examples/ok_example.c" for i in issues))

    def test_v6_v7_row_shadowing(self):
        from graphify.machines import extract_machines
        root = _repo({
            "examples/m.c": """
                enum { S0 = 0, S1, S2 };
                enum { E0 = 0 };
                static const SM_StateDesc_t st[3] = { {0}, {0}, {0} };
                static const SM_Transition_t rows[] = {
                    { S0, E0, S1, 0, NULL, NULL },
                    { S0, E0, S2, 0, g, NULL },
                    { S1, E0, S2, 0, NULL, NULL },
                    { S1, E0, S2, 0, NULL, NULL },
                };
                static const SM_Config_t cfg = { st, rows, 4, S0 };
            """,
        })
        ms = extract_machines(root, ["examples/m.c"])
        self.assertEqual(len(ms), 1)
        checks = {c for _, c, _ in ms[0].findings}
        self.assertIn("V6-shadowed-row", checks)
        self.assertIn("V7-duplicate-row", checks)


class LvalueMacroTests(unittest.TestCase):
    """Writes performed through a function-like macro's lvalue argument.

    Since v4.1 every DIS-protected field on a live machine is assigned via
    SM_DIS_ASSIGN, so an extractor that only understands `x = y` loses the
    write edge for exactly the fields whose single-writer property matters
    most. This regression-tests the modelling of that.
    """

    def test_dis_assign_records_writes_for_both_lvalue_args(self):
        root = _repo({
            "include/t.h": HEADER + """
                #define SM_DIS_ASSIGN(f_, d_, t_, v_) \
                    do { (f_) = (v_); SM_DIS_UPDATE((f_), (d_), t_); } while (0)
                #define SM_DIS_UPDATE(f_, d_, t_) ((d_) = (t_)(~(t_)(f_)))
            """,
            "src/core/w.c": """
                #include "t.h"
                void writer(SM_Handle_t sm, uint16_t next)
                {
                    SM_DIS_ASSIGN(sm->current_state, sm->state_dis,
                                  uint16_t, next);
                }
                void reader(SM_Handle_t sm)
                {
                    uint16_t s = sm->current_state;
                    (void)s;
                }
            """,
        })
        g = build_graph(root)
        w = g.functions["writer@src/core/w.c"]
        self.assertIn("SM_Context.current_state", w.writes)
        self.assertIn("SM_Context.state_dis", w.writes)
        # the value argument stays a read, not a write
        r = g.functions["reader@src/core/w.c"]
        self.assertIn("SM_Context.current_state", r.reads)
        self.assertNotIn("SM_Context.current_state", r.writes)

    def test_plain_read_of_a_macro_arg_is_not_a_write(self):
        root = _repo({
            "include/t.h": HEADER + """
                #define SM_DIS_VERIFY(f_, d_, t_, id_) SM_REQUIRE((id_), 1)
            """,
            "src/core/v.c": """
                #include "t.h"
                void verify(SM_Handle_t sm)
                {
                    SM_DIS_VERIFY(sm->current_state, sm->state_dis,
                                  uint16_t, 201);
                }
            """,
        })
        g = build_graph(root)
        v = g.functions["verify@src/core/v.c"]
        self.assertEqual(v.writes, set())
        self.assertIn("SM_Context.current_state", v.reads)


class DisAtomicityTests(unittest.TestCase):
    """G16: both stores of a DIS pair must fall inside one critical section.

    The detector is demonstrated against the repository's own known-bad
    revision in test_repo.py's docstring and in CLAUDE.md; these fixtures pin
    the three shapes it has to tell apart.
    """

    HDR = """
        #ifndef D_H
        #define D_H
        #include <stdint.h>
        typedef struct SM_Context SM_Context_t;
        typedef SM_Context_t *SM_Handle_t;
        struct SM_Context {
            volatile uint16_t current_state;
            uint16_t state_dis;
        };
        #define SM_DIS_UPDATE(f_, d_, t_)  ((d_) = (t_)(~(t_)(f_)))
        #define SM_DIS_ASSIGN(f_, d_, t_, v_) do { \\
            SM_Platform_EnterCritical(); (f_) = (v_); \\
            SM_DIS_UPDATE((f_), (d_), t_); \\
            SM_Platform_ExitCritical(); } while (0)
        void SM_Platform_EnterCritical(void);
        void SM_Platform_ExitCritical(void);
        #endif
    """

    def _graph(self, engine: str):
        root = _repo({"include/d.h": self.HDR,
                      "src/core/sm_engine.c": engine})
        return build_graph(root)

    def _sites(self, g, name):
        for fn in g.functions.values():
            if fn.name == name:
                return fn
        self.fail(f"function {name} not extracted")

    def test_bare_update_outside_critsec_is_torn(self):
        g = self._graph("""
            #include "d.h"
            void torn(SM_Handle_t sm)
            {
                sm->current_state = 3U;
                SM_DIS_UPDATE(sm->current_state, sm->state_dis, uint16_t);
            }
        """)
        fn = self._sites(g, "torn")
        self.assertEqual(len(fn.dis_sites), 1)
        kind, fexpr, dexpr, _line, protected = fn.dis_sites[0]
        self.assertEqual(kind, "SM_DIS_UPDATE")
        self.assertEqual(fexpr, "sm->current_state")
        self.assertEqual(dexpr, "sm->state_dis")
        self.assertFalse(protected, "a bare update outside a critsec is torn")
        self.assertEqual(fn.dis_exempt, "")

    def test_update_inside_critsec_is_atomic(self):
        g = self._graph("""
            #include "d.h"
            void guarded(SM_Handle_t sm)
            {
                SM_Platform_EnterCritical();
                sm->current_state = 3U;
                SM_DIS_UPDATE(sm->current_state, sm->state_dis, uint16_t);
                SM_Platform_ExitCritical();
            }
        """)
        fn = self._sites(g, "guarded")
        self.assertEqual(len(fn.dis_sites), 1)
        self.assertTrue(fn.dis_sites[0][4],
                        "an update inside a critical section is indivisible")

    def test_dis_assign_is_atomic_anywhere(self):
        g = self._graph("""
            #include "d.h"
            void assigned(SM_Handle_t sm)
            {
                SM_DIS_ASSIGN(sm->current_state, sm->state_dis, uint16_t, 3U);
            }
        """)
        fn = self._sites(g, "assigned")
        self.assertEqual(len(fn.dis_sites), 1)
        self.assertEqual(fn.dis_sites[0][0], "SM_DIS_ASSIGN")
        self.assertTrue(fn.dis_sites[0][4],
                        "SM_DIS_ASSIGN takes the critical section itself")

    def test_exemption_marker_is_read_whole(self):
        g = self._graph("""
            #include "d.h"
            void constructing(SM_Handle_t sm)
            {
                /* DIS-ATOMIC-EXEMPT: instance under construction,
                 * not yet observable by any reader. */
                sm->current_state = 0U;
                SM_DIS_UPDATE(sm->current_state, sm->state_dis, uint16_t);
            }
        """)
        fn = self._sites(g, "constructing")
        self.assertFalse(fn.dis_sites[0][4], "still recorded as non-atomic")
        # The rationale must survive the line break -- a one-line cap used to
        # truncate it mid-sentence, which reads as a checker bug in the report.
        self.assertIn("instance under construction", fn.dis_exempt)
        self.assertIn("not yet observable", fn.dis_exempt)
        self.assertNotIn("*", fn.dis_exempt)

    def test_exemption_does_not_leak_to_other_functions(self):
        g = self._graph("""
            #include "d.h"
            void excused(SM_Handle_t sm)
            {
                /* DIS-ATOMIC-EXEMPT: under construction. */
                sm->current_state = 0U;
                SM_DIS_UPDATE(sm->current_state, sm->state_dis, uint16_t);
            }
            void not_excused(SM_Handle_t sm)
            {
                sm->current_state = 1U;
                SM_DIS_UPDATE(sm->current_state, sm->state_dis, uint16_t);
            }
        """)
        self.assertNotEqual(self._sites(g, "excused").dis_exempt, "")
        self.assertEqual(self._sites(g, "not_excused").dis_exempt, "",
                         "an exemption is scoped to the body that states it")
