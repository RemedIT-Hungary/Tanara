"""Self-test of tools/ci/test-summary.py on synthetic CTest LastTest.log files (stdlib unittest)."""
import contextlib
import importlib.util
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

_PATH = Path(__file__).resolve().parent.parent / "ci" / "test-summary.py"
_spec = importlib.util.spec_from_file_location("test_summary", _PATH)
ts = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ts)

SEP = "-" * 58


def section(index, total, name, output, passed):
    lines = [
        f"{index}/{total} Testing: {name}",
        f"{index}/{total} Test: {name}",
        f'Command: "/build/tests/{name}"',
        "Directory: /build/tests",
        f'"{name}" start time: Oct 19 22:38 CEST',
        "Output:",
        SEP,
        *output,
        "<end of output>",
        "Test time =   0.10 sec",
        SEP,
        "Test Passed." if passed else "Test Failed.",
        f'"{name}" end time: Oct 19 22:38 CEST',
        f'"{name}" time elapsed: 00:00:00',
        SEP,
        "",
    ]
    return lines


def make_log(tests):
    lines = ["Start testing: Oct 19 22:38 CEST", SEP]
    for i, (name, output, passed) in enumerate(tests, 1):
        lines += section(i, len(tests), name, output, passed)
    lines.append("End testing: Oct 19 22:47 CEST")
    return [line + "\n" for line in lines]


LONG_OUTPUT = [f"QDEBUG : TestLong::run() line {i}" for i in range(1, 301)] + [
    "FAIL!  : TestLong::run() 'ok' returned FALSE. ()",
    "   Loc: [/src/tests/test_long.cpp(42)]",
    "Totals: 0 passed, 1 failed, 0 skipped",
]
SHORT_OUTPUT = [
    "********* Start testing of TestShort *********",
    "PASS   : TestShort::initTestCase()",
    "FAIL!  : TestShort::compare() Compared values are not the same",
    "   Actual   (a): 1",
    "   Expected (b): 2",
    "   Loc: [/src/tests/test_short.cpp(17)]",
    "Totals: 1 passed, 1 failed, 0 skipped",
]
PASS_OUTPUT = ["PASS   : TestOk::run()", "Totals: 1 passed, 0 failed, 0 skipped"]

MIXED = [
    ("test_ok_a", PASS_OUTPUT, True),
    ("test_long", LONG_OUTPUT, False),
    ("test_ok_b", ["-" * 70, "Ran 3 tests in 0.01s", "", "OK"], True),
    ("test_short", SHORT_OUTPUT, False),
    ("test_ok_c", PASS_OUTPUT, True),
]


class ParseTest(unittest.TestCase):
    def test_parse_statuses_and_output(self):
        results = ts.parse(make_log(MIXED))
        self.assertEqual([r.name for r in results], [t[0] for t in MIXED])
        self.assertEqual([r.failed for r in results], [False, True, False, True, False])
        self.assertEqual(results[1].output, LONG_OUTPUT)
        # A dashed line inside the output (unittest) is kept, not mistaken for the separator.
        self.assertEqual(results[2].output[0], "-" * 70)

    def test_totals_line(self):
        self.assertEqual(ts.totals_line(ts.parse(make_log(MIXED))), "60% tests passed, 2 tests failed out of 5")


class RenderTest(unittest.TestCase):
    def setUp(self):
        self.results = ts.parse(make_log(MIXED))
        self.md = ts.render(self.results)

    def test_failed_section_with_two_details(self):
        self.assertIn("### Failed tests (2)", self.md)
        self.assertEqual(self.md.count("<details>"), 2)
        self.assertIn("<details><summary>test_long</summary>", self.md)
        self.assertIn("<details><summary>test_short</summary>", self.md)
        self.assertNotIn("test_ok_a", self.md)
        self.assertIn("   Expected (b): 2", self.md)

    def test_long_output_capped(self):
        block = self.md.split("<summary>test_long</summary>")[1].split("</details>")[0]
        self.assertIn("line 1\n", block)
        self.assertIn("line 40\n", block)
        self.assertNotIn("line 41\n", block)
        # 303 lines: 40 head + 120 tail -> 143 omitted; the tail starts at line 184.
        self.assertIn("... (143 lines omitted) ...", block)
        self.assertNotIn("line 183\n", block)
        self.assertIn("line 184\n", block)
        self.assertIn("Totals: 0 passed, 1 failed, 0 skipped", block)

    def test_size_cap_falls_back_to_names(self):
        md = ts.render(self.results, max_bytes=500)
        self.assertIn("### Failed tests (2)", md)
        self.assertIn("- `test_long` (Test Failed)", md)
        self.assertIn("- `test_short` (Test Failed)", md)
        self.assertNotIn("<details>", md)
        self.assertIn("size limit", md)

    def test_all_green_has_no_section(self):
        results = ts.parse(make_log([t for t in MIXED if t[2]]))
        self.assertEqual(ts.render(results), "")


class AnnotationTest(unittest.TestCase):
    def test_two_errors_and_a_notice(self):
        lines = ts.annotations(ts.parse(make_log(MIXED)))
        errors = [line for line in lines if line.startswith("::error ")]
        self.assertEqual(len(errors), 2)
        self.assertTrue(errors[0].startswith("::error title=Test failed::test_long — FAIL! : TestLong::run() "
                                             "'ok' returned FALSE. () Loc: [/src/tests/test_long.cpp(42)]"))
        # The tail of the output travels in the annotation (newlines escaped) for signed-out viewers.
        self.assertIn("%0A--- output (tail) ---%0A", errors[0])
        self.assertTrue(errors[0].endswith("Totals: 0 passed, 1 failed, 0 skipped"))
        self.assertTrue(errors[1].startswith("::error title=Test failed::test_short — FAIL! : "
                                             "TestShort::compare()"))
        self.assertEqual(lines[-1], "::notice title=CTest::60%25 tests passed, 2 tests failed out of 5")

    def test_errors_capped(self):
        tests = [(f"test_{i}", ["boom"], False) for i in range(15)]
        lines = ts.annotations(ts.parse(make_log(tests)))
        self.assertEqual(sum(line.startswith("::error ") for line in lines), ts.MAX_ANNOTATIONS)
        self.assertTrue(lines[0].startswith("::error title=Test failed::test_0"))


class MainTest(unittest.TestCase):
    def run_main(self, tests):
        with tempfile.TemporaryDirectory() as tmp:
            log = os.path.join(tmp, "LastTest.log")
            summary = os.path.join(tmp, "summary.md")
            with open(log, "w", encoding="utf-8") as f:
                f.writelines(make_log(tests))
            out = io.StringIO()
            with mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": summary}), \
                    mock.patch.object(sys, "argv", ["test-summary.py", log]), \
                    contextlib.redirect_stdout(out):
                self.assertEqual(ts.main(), 0)
            text = open(summary, encoding="utf-8").read() if os.path.exists(summary) else ""
            return out.getvalue(), text

    def test_main_writes_summary_and_annotations(self):
        stdout, summary = self.run_main(MIXED)
        self.assertEqual(len(stdout.splitlines()), 3)
        self.assertIn("### Failed tests (2)", summary)

    def test_main_all_green_writes_no_summary(self):
        stdout, summary = self.run_main([t for t in MIXED if t[2]])
        self.assertEqual(stdout, "::notice title=CTest::100%25 tests passed, 0 tests failed out of 3\n")
        self.assertEqual(summary, "")


if __name__ == "__main__":
    unittest.main()
