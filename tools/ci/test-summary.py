#!/usr/bin/env python3
"""Publish failed CTest tests to the GitHub job summary and as annotations.

Usage: test-summary.py [path/to/LastTest.log]

Job logs and artifacts need a signed-in user with access; the job summary and the
annotations are public. So when tests failed, this script:
  - appends a "Failed tests" Markdown section to $GITHUB_STEP_SUMMARY (when set), with
    each failed test's own output in a collapsible block (capped);
  - prints one "::error" annotation per failed test (at most MAX_ANNOTATIONS);
  - always prints one "::notice" annotation with the totals line.
Nothing else is printed to stdout.
"""
import html
import os
import re
import sys

# CTest header of a test section, e.g. "12/108 Testing: test_foo"
TEST_RE = re.compile(r"^\d+/\d+ Testing: (\S+)")
# Qt Test failure and location lines, e.g. "FAIL!  : TestFoo::bar() ..." and "   Loc: [file(12)]"
FAIL_RE = re.compile(r"^FAIL!\s*:")
LOC_RE = re.compile(r"^\s*Loc:\s*")
END_OF_OUTPUT = "<end of output>"

HEAD_LINES = 40
TAIL_LINES = 120
MAX_SUMMARY_BYTES = 900 * 1024  # GitHub's limit per step is 1 MiB
MAX_ANNOTATIONS = 10


class TestResult:
    def __init__(self, name):
        self.name = name
        self.output = []   # lines between "Output:" and "<end of output>"
        self.status = None  # e.g. "Test Passed.", "Test Failed."

    @property
    def failed(self):
        return self.status is not None and self.status != "Test Passed."


def parse(lines):
    """Return the TestResult list of a LastTest.log, in log order."""
    results = []
    cur = None
    state = None  # None | "header" | "output" | "after"
    for raw in lines:
        line = raw.rstrip("\r\n")
        m = TEST_RE.match(line)
        if m:
            cur = TestResult(m.group(1))
            results.append(cur)
            state = "header"
            continue
        if cur is None:
            continue
        if state == "header":
            if line == "Output:":
                state = "output-start"
            continue
        if state == "output-start":
            # The separator line right after "Output:".
            state = "output"
            if set(line) == {"-"}:
                continue
        if state == "output":
            if line == END_OF_OUTPUT or line.startswith("Test time ="):
                state = "after"
                if line == END_OF_OUTPUT:
                    continue
            else:
                cur.output.append(line)
                continue
        if state == "after" and line.startswith("Test ") and not line.startswith("Test time"):
            cur.status = line.strip()
            state = None
    return results


def cap_lines(lines, head=HEAD_LINES, tail=TAIL_LINES):
    if len(lines) <= head + tail:
        return list(lines)
    skipped = len(lines) - head - tail
    return lines[:head] + [f"... ({skipped} lines omitted) ..."] + lines[-tail:]


def first_failure(lines):
    """The first "FAIL!" line (with the following "Loc:" line, if any), or ""."""
    for i, line in enumerate(lines):
        if FAIL_RE.match(line):
            text = " ".join(line.split())
            if i + 1 < len(lines) and LOC_RE.match(lines[i + 1]):
                text += " " + " ".join(lines[i + 1].split())
            return text
    return ""


def totals_line(results):
    """The CTest-style totals line, e.g. "98% tests passed, 2 tests failed out of 105"."""
    done = [r for r in results if r.status is not None]
    total = len(done)
    failed = sum(1 for r in done if r.failed)
    pct = 100 if total == 0 else int((total - failed) * 100 / total)
    return f"{pct}% tests passed, {failed} tests failed out of {total}"


def fence_for(lines):
    longest = 0
    for line in lines:
        for run in re.findall(r"`+", line):
            longest = max(longest, len(run))
    return "`" * max(3, longest + 1)


def render(results, max_bytes=MAX_SUMMARY_BYTES):
    """Markdown for the job summary; "" when no test failed."""
    failed = [r for r in results if r.failed]
    if not failed:
        return ""
    head = [f"### Failed tests ({len(failed)})", "", totals_line(results), ""]
    names = [f"- `{r.name}` ({r.status.rstrip('.')})" for r in failed]
    blocks = []
    for r in failed:
        body = cap_lines(r.output) or ["(no output)"]
        fence = fence_for(body)
        blocks += [f"<details><summary>{html.escape(r.name)}</summary>", "", fence + "text"]
        blocks += body
        blocks += [fence, "", "</details>", ""]
    full = "\n".join(head + names + [""] + blocks) + "\n"
    if len(full.encode("utf-8")) <= max_bytes:
        return full
    note = "_Test output omitted: the summary would exceed the size limit. See LastTest.log._"
    return "\n".join(head + names + ["", note]) + "\n"


def escape_data(text):
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


# Lines of a failed test's output carried in its annotation (the job summary is not visible to
# signed-out viewers, the annotations are): the tail of the output, within GitHub's message limit.
ANNOTATION_TAIL_LINES = 30
ANNOTATION_MAX_CHARS = 3500


def annotation_tail(output):
    """The last ANNOTATION_TAIL_LINES lines of a test's output, cut to ANNOTATION_MAX_CHARS."""
    tail = [l.rstrip() for l in output[-ANNOTATION_TAIL_LINES:]]
    text = "\n".join(tail)
    if len(text) > ANNOTATION_MAX_CHARS:
        text = "…" + text[-ANNOTATION_MAX_CHARS:]
    return text


def annotations(results):
    lines = []
    for r in [r for r in results if r.failed][:MAX_ANNOTATIONS]:
        msg = r.name
        detail = first_failure(r.output)
        if detail:
            msg += " — " + detail
        tail = annotation_tail(r.output)
        if tail:
            msg += "\n--- output (tail) ---\n" + tail
        lines.append(f"::error title=Test failed::{escape_data(msg)}")
    lines.append(f"::notice title=CTest::{escape_data(totals_line(results))}")
    return lines


def main():
    # Test output may be Hungarian; a cp1252 console (Windows runner) would raise UnicodeEncodeError.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "Testing", "Temporary", "LastTest.log")
    if not os.path.exists(path):
        print(f"test-summary: {path} not found", file=sys.stderr)
        return 1
    with open(path, encoding="utf-8", errors="replace") as f:
        results = parse(f)
    for line in annotations(results):
        print(line)
    text = render(results)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary and text:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
