"""Self-test of tools/check_modules.py on tiny temporary source trees (stdlib unittest)."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import check_modules as cm  # noqa: E402

MODULES = {
    "modules": {
        "foundation": {"paths": ["core/include/tanara/foundation/**", "core/src/foundation/**"], "deps": [],
                       "namespace": "tanara", "enforceNamespace": False},
        "library": {"paths": ["core/include/tanara/library/**", "core/src/library/**"], "deps": ["foundation"],
                    "namespace": "tanara::library", "enforceNamespace": False},
        "processing": {"paths": ["core/include/tanara/processing/**", "core/src/processing/**"],
                       "deps": ["foundation", "library"], "namespace": "tanara::processing",
                       "enforceNamespace": False},
        "ui": {"paths": ["gui/**"], "deps": ["foundation", "library", "processing"], "namespace": None,
               "enforceNamespace": False},
        "watcher-exe": {"paths": ["watcher/**"], "deps": ["foundation"], "namespace": None,
                        "enforceNamespace": False},
    },
    "legacy": [["old/Thing*", "library"]],
    "forbiddenIncludes": {},
}

BASE = {
    "core/include/tanara/foundation/F.h": "namespace tanara { int f(); }\n",
    "core/include/tanara/library/L.h": '#include "tanara/foundation/F.h"\nnamespace tanara::library { int l(); }\n',
    "core/include/tanara/library/detail/D.h": "namespace tanara::library { int d(); }\n",
    "core/include/tanara/processing/P.h": '#include "tanara/library/L.h"\nnamespace tanara::processing { int p(); }\n',
}


class Fixture:
    def __init__(self, files, allow="", modules=None):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        for rel, text in {**BASE, **files}.items():
            p = self.root / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text, encoding="utf-8")
        (self.root / "tools").mkdir(exist_ok=True)
        (self.root / "tools" / "modules.json").write_text(json.dumps(modules or MODULES), encoding="utf-8")
        (self.root / "tools" / "module-boundaries.allow").write_text(allow, encoding="utf-8")

    def run(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = cm.main(["--root", str(self.root)])
        return rc, out.getvalue()


class CheckModulesTest(unittest.TestCase):
    def check(self, fx, rc_expected, *needles):
        try:
            rc, out = fx.run()
        finally:
            fx.dir.cleanup()
        self.assertEqual(rc, rc_expected, out)
        for n in needles:
            self.assertIn(n, out)
        return out

    def test_clean_tree_ok(self):
        self.check(Fixture({}), 0, "0 violations")

    def test_allowed_edge_ok(self):
        self.check(Fixture({"core/src/processing/P.cpp": '#include "tanara/library/L.h"\n#include "tanara/foundation/F.h"\n'}), 0)

    def test_forbidden_edge_fails(self):
        out = self.check(Fixture({"core/src/library/L.cpp": '#include "tanara/processing/P.h"\n'}), 1,
                         "core/src/library/L.cpp:1: module-dep", "not in allowlist")
        self.assertIn("library may not depend on processing", out)

    def test_allowlisted_violation_ok(self):
        fx = Fixture({"core/src/library/L.cpp": '#include "tanara/processing/P.h"\n'},
                     allow="core/src/library/L.cpp -> tanara/processing/P.h   # K1 -> P4\n")
        self.check(fx, 0, "1 tolerated")

    def test_stale_allowlist_line_fails(self):
        fx = Fixture({}, allow="core/src/library/L.cpp -> tanara/processing/P.h   # K1 -> P4\n")
        self.check(fx, 1, "stale-allowlist")

    def test_malformed_allowlist_line_fails(self):
        self.check(Fixture({}, allow="this is not a rule\n"), 1, "stale-allowlist")

    def test_detail_from_other_module_fails(self):
        self.check(Fixture({"core/src/processing/P.cpp": '#include "tanara/library/detail/D.h"\n'}), 1,
                   "P.cpp:1: detail")

    def test_detail_from_own_module_ok(self):
        self.check(Fixture({"core/src/library/L.cpp": '#include "tanara/library/detail/D.h"\n'}), 0)

    def test_missing_namespace_fails_when_enforced(self):
        mods = json.loads(json.dumps(MODULES))
        mods["modules"]["library"]["enforceNamespace"] = True
        self.check(Fixture({"core/src/library/NoNs.cpp": "int x;\n"}, modules=mods), 1,
                   "NoNs.cpp:1: namespace")

    def test_namespace_nested_form_accepted(self):
        mods = json.loads(json.dumps(MODULES))
        mods["modules"]["library"]["enforceNamespace"] = True
        fx = Fixture({"core/src/library/Nested.cpp": "namespace tanara {\nnamespace library {\nint x;\n}\n}\n"},
                     modules=mods)
        self.check(fx, 0)

    def test_namespace_not_enforced_by_default(self):
        self.check(Fixture({"core/src/library/NoNs.cpp": "int x;\n"}), 0)

    def test_platform_macro_outside_platform_dir_fails(self):
        self.check(Fixture({"core/src/library/W.cpp": "#ifdef Q_OS_WIN\nint w;\n#endif\n"}), 1,
                   "W.cpp:1: platform")

    def test_platform_macro_in_platform_dir_ok(self):
        self.check(Fixture({"core/src/library/platform/windows/W.cpp": "#ifdef Q_OS_WIN\nint w;\n#endif\n"}), 0)

    def test_platform_macro_in_comment_ignored(self):
        self.check(Fixture({"core/src/library/C.cpp": "// Q_OS_WIN is not used here\n/* _WIN32 */\n"}), 0)

    def test_watcher_exe_must_not_include_gui_src(self):
        files = {"gui/src/Tray.h": "int t;\n",
                 "watcher/main.cpp": '#include "Tray.h"\n'}
        self.check(Fixture(files), 1, "watcher/main.cpp:1: watcher-gui")

    def test_gui_must_not_include_core_src(self):
        files = {"core/src/library/Priv.h": "int p;\n",
                 "gui/a.cpp": '#include "../core/src/library/Priv.h"\n'}
        self.check(Fixture(files), 1, "private-header")

    def test_legacy_map_decides_module(self):
        files = {"core/include/tanara/old/Thing.h": "int t;\n",
                 "core/src/processing/P.cpp": '#include "tanara/old/Thing.h"\n',
                 "core/src/foundation/F.cpp": '#include "tanara/old/Thing.h"\n'}
        self.check(Fixture(files), 1, "core/src/foundation/F.cpp:1: module-dep")

    def test_unresolved_tanara_include_fails(self):
        self.check(Fixture({"core/src/library/L.cpp": '#include "tanara/library/Missing.h"\n'}), 1,
                   "unresolved-include")

    def test_forbidden_include_rule(self):
        mods = json.loads(json.dumps(MODULES))
        mods["forbiddenIncludes"] = {"ui": ["tanara/library/L.h"]}
        self.check(Fixture({"gui/a.cpp": '#include "tanara/library/L.h"\n'}, modules=mods), 1,
                   "forbidden-include")

    def test_report_and_dot_run(self):
        fx = Fixture({"core/src/processing/P.cpp": '#include "tanara/library/L.h"\n'})
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            cm.main(["--root", str(fx.root), "--report"])
            cm.main(["--root", str(fx.root), "--dot"])
        fx.dir.cleanup()
        self.assertIn("module include matrix", out.getvalue())
        self.assertIn('"processing" -> "library"', out.getvalue())

    def test_write_allowlist_then_green(self):
        fx = Fixture({"core/src/library/L.cpp": '#include "tanara/processing/P.h"\n'})
        with contextlib.redirect_stdout(io.StringIO()):
            cm.main(["--root", str(fx.root), "--write-allowlist"])
        rc, _ = fx.run()
        fx.dir.cleanup()
        self.assertEqual(rc, 0)


if __name__ == "__main__":
    unittest.main()
