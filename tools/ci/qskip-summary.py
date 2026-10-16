#!/usr/bin/env python3
"""Summarize QSKIP lines from a CTest LastTest.log.

Usage: qskip-summary.py [path/to/LastTest.log]

Prints a Markdown table (test, skipped cases, reason) to stdout and, when the
GITHUB_STEP_SUMMARY environment variable is set, appends it there as well.
CTest reports a test with QSKIPs as passed, so without this the skips are invisible.
"""
import os
import re
import sys
from collections import OrderedDict

# CTest header of a test section, e.g. "12/108 Testing: test_foo"
TEST_RE = re.compile(r"^\d+/\d+ Testing: (\S+)")
# Qt Test skip line, e.g. "SKIP   : TestFoo::bar() ffmpeg not available"
SKIP_RE = re.compile(r"^SKIP\s*:\s*(\S+?)\(\)\s*(.*)$")


def parse(path):
    skips = OrderedDict()  # test -> list of (case, reason)
    current = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\r\n")
            m = TEST_RE.match(line)
            if m:
                current = m.group(1)
                continue
            m = SKIP_RE.match(line)
            if m and current:
                skips.setdefault(current, []).append((m.group(1), m.group(2).strip()))
    return skips


def render(skips):
    total = sum(len(v) for v in skips.values())
    out = ["### QSKIP summary", ""]
    if not skips:
        out.append("No skipped test cases.")
        return "\n".join(out) + "\n"
    out.append(f"{total} skipped test case(s) in {len(skips)} test(s).")
    out.append("")
    out.append("| Test | Skipped cases | Reason |")
    out.append("|---|---|---|")
    for test, items in skips.items():
        reasons = OrderedDict()
        for case, reason in items:
            reasons.setdefault(reason or "(no reason)", []).append(case)
        for reason, cases in reasons.items():
            shown = ", ".join(c.split("::")[-1] for c in cases[:3])
            if len(cases) > 3:
                shown += f", ... (+{len(cases) - 3})"
            out.append(f"| `{test}` | {len(cases)}: {shown} | {reason.replace('|', '/')} |")
    return "\n".join(out) + "\n"


def main():
    # The QSKIP reasons are Hungarian; a cp1252 console (Windows runner) would raise UnicodeEncodeError.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "Testing", "Temporary", "LastTest.log")
    if not os.path.exists(path):
        print(f"qskip-summary: {path} not found", file=sys.stderr)
        return 1
    text = render(parse(path))
    sys.stdout.write(text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
