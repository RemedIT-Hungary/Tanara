#!/usr/bin/env python3
"""Module-boundary guard for Tanara (cleanup phase 2, decision D1).

Walks the .h/.cpp files of core/, gui/, cli/ and watcher/ and judges every
#include against the target module map in tools/modules.json (plus the
transitional `legacy` folder map, so the guard works on today's folders too).

Rules (id in the output):
  module-dep         the include direction is not listed in the module's `deps`
  detail             a detail/ header is included from another module
  private-header     code outside core/ includes a core/src/ header
  watcher-gui        watcher/** includes gui/src/** (gui/common is allowed)
  forbidden-include  an include listed in `forbiddenIncludes` for the module
  platform           Q_OS_*, _WIN32, __APPLE__, __linux__ outside */platform/<os>/
  namespace          (only where enforceNamespace is true) missing namespace tanara::<m>
  unmapped           a core file that belongs to no module
  unresolved-include a "tanara/..." include that does not resolve to a file

Output: one line per error, `file:line: <rule>: <explanation>`.
Exit 1 if (a) there is a violation that is not in tools/module-boundaries.allow,
or (b) there is a STALE allowlist line (the violation is gone, the line is not).

RATCHET RULE: the allowlist may only shrink. Every slice deletes the lines of the
violations it removes (stale lines are errors). A NEW line may be added only with
the orchestrator's permission, justified in the slice report.

Allowlist line:  <file> -> <include or token>   # <cycle/reason> -> P<n>
  token is the include path as written (module-dep, detail, forbidden-include, ...),
  `platform:<macro>` or `namespace:<ns>`.

Usage:
  python3 tools/check_modules.py [--root DIR] [--report] [--dot] [--write-allowlist]
  --report           module-pair matrix (distinct include edges); '!' marks a tolerated
                     violating pair, '!!' a pair with an untolerated violation
  --dot              Graphviz edge list (violating edges in red)
  --write-allowlist  one-off: (re)write the allowlist from the current tree (keeps
                     the trailing `# ...` tags of lines that already exist)
Also: `cmake --build build --target check_modules`; ctest runs it as `module_boundaries`.
"""
import argparse
import fnmatch
import json
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

SRC_EXT = {".h", ".hpp", ".cpp", ".cc", ".mm"}
SCAN_DIRS = ["core", "demo", "gui", "cli", "watcher"]
SKIP_DIR_RE = re.compile(r"^(build.*|\.git|node_modules|third_party|\.cache)$")
INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', re.M)
PLATFORM_RE = re.compile(r"\b(Q_OS_\w+|_WIN32|__APPLE__|__linux__)\b")
PLATFORM_DIR_RE = re.compile(r"(^|/)platform/[^/]+/")
CORE_INC = "core/include/"
CORE_SRC = "core/src/"
HEADER_NOTE = (
    "# Tolerated module-boundary violations (today's debt). RATCHET: lines may only be\n"
    "# DELETED (each slice removes the lines of the violations it fixes; a stale line is\n"
    "# an error). A NEW line may be added ONLY with the orchestrator's permission.\n"
    "# Format: <file> -> <include or token>   # <cycle/reason> -> P<slice>\n"
)


def glob_match(pattern, path):
    return fnmatch.fnmatchcase(path, pattern.replace("**", "*"))


class Tree:
    def __init__(self, root, modules_path=None, allow_path=None):
        self.root = Path(root).resolve()
        mp = Path(modules_path) if modules_path else self.root / "tools" / "modules.json"
        cfg = json.loads(mp.read_text(encoding="utf-8"))
        self.modules = cfg["modules"]
        self.legacy = cfg.get("legacy", [])
        self.forbidden = cfg.get("forbiddenIncludes", {})
        self.allow_path = Path(allow_path) if allow_path else self.root / "tools" / "module-boundaries.allow"
        self.files = self._scan()
        self.fileset = set(self.files)

    def _scan(self):
        out = []
        for d in SCAN_DIRS:
            base = self.root / d
            if not base.is_dir():
                continue
            for dp, dns, fns in os.walk(base):
                dns[:] = sorted(x for x in dns if not SKIP_DIR_RE.match(x))
                for fn in sorted(fns):
                    if os.path.splitext(fn)[1] in SRC_EXT:
                        out.append(Path(dp, fn).relative_to(self.root).as_posix())
        return out

    # ---- module of a file -------------------------------------------------
    def module_of(self, path):
        for prefix in (CORE_INC + "tanara/", CORE_SRC):
            if path.startswith(prefix):
                key = path[len(prefix):]
                for pat, mod in self.legacy:
                    if glob_match(pat, key):
                        return mod
                break
        for name, m in self.modules.items():
            if any(glob_match(p, path) for p in m["paths"]):
                return name
        return None

    # ---- include resolution -----------------------------------------------
    def resolve(self, includer, inc, delim):
        if inc.startswith("tanara/"):
            cand = CORE_INC + inc
            return cand if cand in self.fileset else None
        if delim != '"':
            return None
        rel = os.path.normpath(os.path.join(os.path.dirname(includer), inc)).replace(os.sep, "/")
        if rel in self.fileset:
            return rel
        # include path set by CMake (e.g. gui/src on the include path): match by path suffix
        suffix = "/" + inc
        hits = [f for f in self.files if f.endswith(suffix) or f == inc]
        if not hits:
            return None
        top = includer.split("/")[0]
        hits.sort(key=lambda f: (f.split("/")[0] != top, len(f), f))
        return hits[0]


def strip_comments(text):
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    return re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)


def ns_regexes(ns):
    parts = ns.split("::")
    flat = r"\bnamespace\s+" + r"\s*::\s*".join(re.escape(p) for p in parts) + r"\b"
    nested = r"\bnamespace\s+" + r"\s*\{\s*namespace\s+".join(re.escape(p) for p in parts) + r"\b"
    return [re.compile(flat), re.compile(nested)]


def lineno(text, pos):
    return text.count("\n", 0, pos) + 1


def check(tree):
    """Returns a list of violations: dicts(file, line, rule, key, msg, src, dst)."""
    vios = []
    for f in tree.files:
        text = Path(tree.root, f).read_text(encoding="utf-8", errors="replace")
        src = tree.module_of(f)
        if src is None:
            if f.startswith("core/"):
                vios.append(dict(file=f, line=1, rule="unmapped", key="unmapped", src=None, dst=None,
                                 msg="file belongs to no module (modules.json: paths/legacy)"))
            continue
        deps = set(tree.modules[src]["deps"])
        seen = set()
        for m in INCLUDE_RE.finditer(text):
            delim, inc = m.group(1), m.group(2)
            ln = lineno(text, m.start())
            dst_path = tree.resolve(f, inc, delim)
            if dst_path is None:
                if inc.startswith("tanara/"):
                    vios.append(dict(file=f, line=ln, rule="unresolved-include", key=inc, src=src, dst=None,
                                     msg=f"{inc} does not resolve to a file under core/include"))
                continue
            dst = tree.module_of(dst_path)
            if dst is None:
                continue
            if (inc, dst_path) in seen:
                continue
            seen.add((inc, dst_path))

            def add(rule, msg):
                vios.append(dict(file=f, line=ln, rule=rule, key=inc, src=src, dst=dst, msg=msg))

            if dst_path.startswith(CORE_SRC) and not f.startswith("core/"):
                add("private-header", f"{inc} is a private core/src header, not includable from {src}")
            if "/detail/" in dst_path and dst != src:
                add("detail", f"{inc} is a detail/ header of {dst}, only {dst} may include it")
            if src == "watcher-exe" and dst_path.startswith("gui/src/"):
                add("watcher-gui", f"watcher/** must not include gui/src/** ({inc}); only gui/common is shared")
            elif dst != src and dst not in deps:
                add("module-dep", f"{src} may not depend on {dst} ({inc}); allowed: {', '.join(sorted(deps)) or 'nothing'}")
            for forb in tree.forbidden.get(src, []):
                if inc == forb or dst_path.endswith("/" + forb):
                    add("forbidden-include", f"{src} may not include {forb}")
        # platform rule (comments stripped, line numbers kept)
        if not PLATFORM_DIR_RE.search(f):
            clean = strip_comments(text)
            done = set()
            for m in PLATFORM_RE.finditer(clean):
                tok = m.group(1)
                if tok in done:
                    continue
                done.add(tok)
                vios.append(dict(file=f, line=lineno(clean, m.start()), rule="platform", key=f"platform:{tok}",
                                 src=src, dst=None,
                                 msg=f"{tok} only allowed under */platform/<os>/"))
        # namespace rule
        mod = tree.modules[src]
        if mod.get("enforceNamespace") and mod.get("namespace"):
            if not any(r.search(strip_comments(text)) for r in ns_regexes(mod["namespace"])):
                vios.append(dict(file=f, line=1, rule="namespace", key=f"namespace:{mod['namespace']}",
                                 src=src, dst=None, msg=f"missing namespace {mod['namespace']}"))
    return vios


ALLOW_RE = re.compile(r"^\s*(\S+)\s*->\s*(\S+)\s*(?:#\s*(.*))?$")


def read_allow(path):
    entries = []  # (file, key, tag, lineno)
    if not path.is_file():
        return entries
    for i, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        m = ALLOW_RE.match(raw)
        if not m:
            entries.append((None, raw, None, i))
            continue
        entries.append((m.group(1), m.group(2), (m.group(3) or "").strip(), i))
    return entries


def evaluate(tree, vios):
    allow = read_allow(tree.allow_path)
    allowed = {(f, k) for f, k, _, _ in allow if f}
    untolerated = [v for v in vios if (v["file"], v["key"]) not in allowed]
    present = {(v["file"], v["key"]) for v in vios}
    stale = [e for e in allow if e[0] is None or (e[0], e[1]) not in present]
    return allow, untolerated, stale


def matrix(tree, vios):
    """distinct (file, include) edges between modules, only for resolved includes."""
    edges = defaultdict(set)
    for f in tree.files:
        src = tree.module_of(f)
        if not src:
            continue
        text = Path(tree.root, f).read_text(encoding="utf-8", errors="replace")
        for m in INCLUDE_RE.finditer(text):
            dp = tree.resolve(f, m.group(2), m.group(1))
            dst = tree.module_of(dp) if dp else None
            if dst and dst != src:
                edges[(src, dst)].add((f, m.group(2)))
    return edges


def print_report(tree, vios, untolerated):
    edges = matrix(tree, vios)
    bad = {(v["src"], v["dst"]) for v in vios if v["rule"] in ("module-dep", "watcher-gui", "detail", "private-header")}
    badnew = {(v["src"], v["dst"]) for v in untolerated}
    names = list(tree.modules)
    w = max(len(n) for n in names) + 1
    print("module include matrix (rows include columns; distinct file+include edges; ! = tolerated violating pair, !! = untolerated)")
    print(" " * w + " ".join(n[:6].rjust(6) for n in names))
    for s in names:
        cells = []
        for d in names:
            n = len(edges.get((s, d), ()))
            if s == d or n == 0:
                cells.append(".".rjust(6))
            else:
                flag = "!!" if (s, d) in badnew else ("!" if (s, d) in bad else "")
                cells.append((str(n) + flag).rjust(6))
        print(s.ljust(w) + " ".join(cells))
    counts = defaultdict(int)
    for v in vios:
        counts[v["rule"]] += 1
    print("violations by rule: " + (", ".join(f"{k}={n}" for k, n in sorted(counts.items())) or "none"))


def print_dot(tree, vios):
    edges = matrix(tree, vios)
    bad = {(v["src"], v["dst"]) for v in vios if v["rule"] in ("module-dep", "watcher-gui")}
    print("digraph modules {\n  rankdir=BT; node [shape=box];")
    for (s, d), es in sorted(edges.items()):
        color = "red" if (s, d) in bad else "black"
        print(f'  "{s}" -> "{d}" [label="{len(es)}", color={color}];')
    print("}")


def write_allow(tree, vios):
    old = {(f, k): t for f, k, t, _ in read_allow(tree.allow_path) if f}
    lines = sorted({(v["file"], v["key"]) for v in vios})
    out = [HEADER_NOTE]
    for f, k in lines:
        tag = old.get((f, k)) or "egyéb -> P?"
        out.append(f"{f} -> {k}   # {tag}\n")
    tree.allow_path.write_text("".join(out), encoding="utf-8")
    print(f"wrote {tree.allow_path} ({len(lines)} lines)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.add_argument("--modules")
    ap.add_argument("--allowlist")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--dot", action="store_true")
    ap.add_argument("--write-allowlist", action="store_true")
    a = ap.parse_args(argv)
    tree = Tree(a.root, a.modules, a.allowlist)
    vios = check(tree)
    if a.write_allowlist:
        write_allow(tree, vios)
        return 0
    allow, untolerated, stale = evaluate(tree, vios)
    if a.dot:
        print_dot(tree, vios)
        return 0
    if a.report:
        print_report(tree, vios, untolerated)
    for v in untolerated:
        print(f"{v['file']}:{v['line']}: {v['rule']}: {v['msg']}  [not in allowlist]")
    for f, k, _, ln in stale:
        what = f"{f} -> {k}" if f else f"malformed line: {k.strip()}"
        print(f"{tree.allow_path.name}:{ln}: stale-allowlist: {what} no longer occurs; delete the line")
    tolerated = len(vios) - len(untolerated)
    print(f"check_modules: {len(tree.files)} files, {len(vios)} violations "
          f"({tolerated} tolerated, {len(untolerated)} new), {len(stale)} stale allowlist lines")
    return 1 if (untolerated or stale) else 0


if __name__ == "__main__":
    sys.exit(main())
