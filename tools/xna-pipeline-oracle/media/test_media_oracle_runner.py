#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Exercise the real shell wrapper with isolated synthetic compiler/Wine commands."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class MediaOracleRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cna-media-runner-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.script = self.root / "tools/xna-pipeline-oracle/media/run-media-oracle.sh"
        self.script.parent.mkdir(parents=True)
        shutil.copyfile(Path(__file__).with_name("run-media-oracle.sh"), self.script)
        (self.script.parent / "MediaContentOracle.cs").write_text("// synthetic input\n")
        fixtures = self.root / "tests/assets/xna40/media"
        fixtures.mkdir(parents=True)
        (fixtures / "fixture").write_bytes(b"synthetic")
        refs = self.root / "refs"
        refs.mkdir()
        for name in ("", ".Graphics", ".Video", ".Content.Pipeline",
                     ".Content.Pipeline.AudioImporters", ".Content.Pipeline.VideoImporters"):
            (refs / ("Microsoft.Xna.Framework" + name + ".dll")).write_bytes(b"synthetic")
        native = self.root / "XnaNative.dll"
        native.write_bytes(b"synthetic")
        helper = self.root / "XnaMediaHelper_1.dll"
        helper.write_bytes(b"synthetic")
        commands = self.root / "commands"
        commands.mkdir()
        self.command(commands / "mcs", """
import os, sys
from pathlib import Path
status = int(os.environ.get('CNA_FAKE_COMPILER_STATUS', '0'))
if status: sys.exit(status)
for argument in sys.argv[1:]:
    if argument.startswith('-out:'): Path(argument[5:]).write_bytes(b'synthetic executable')
""")
        self.command(commands / "wine", """
import json, os, signal, sys, time
from pathlib import Path
if sys.argv[1] == 'winepath':
    status = int(os.environ.get('CNA_FAKE_WINEPATH_STATUS', '0'))
    if status: sys.exit(status)
    print(sys.argv[-1])
    sys.exit(0)
if os.environ.get('CNA_FAKE_NO_REPORT') != '1':
    report = Path(sys.argv[2]) / 'partial.json'
    report.write_bytes((json.dumps({'synthetic': True, 'skip': sys.argv[4:]}) + '\\r\\n').encode())
if os.environ.get('CNA_FAKE_HANG') == '1': time.sleep(30)
if os.environ.get('CNA_FAKE_SIGNAL') == '1': os.kill(os.getpid(), signal.SIGTERM)
sys.exit(int(os.environ.get('CNA_FAKE_DRIVER_STATUS', '0')))
""")
        self.out = self.root / "reports"
        self.env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                        CNA_XNA40_REFERENCES=str(refs), CNA_XNA40_WINEPREFIX=str(self.root / "prefix"),
                        CNA_XNA40_NATIVE=str(native), CNA_XNA40_MEDIA_HELPER=str(helper))

    @staticmethod
    def command(path, source):
        path.write_text("#!/usr/bin/env python3\n" + source)
        path.chmod(0o755)

    def run_wrapper(self, **changes):
        return subprocess.run(["bash", str(self.script), str(self.out), "skip-this-case"],
                              env=dict(self.env, **changes), text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)

    def assert_report(self):
        data = (self.out / "partial.json").read_bytes()
        self.assertNotIn(b"\r", data)
        self.assertEqual(json.loads(data), {"synthetic": True, "skip": ["skip-this-case"]})

    def test_success_publishes_normalized_reports_and_preserves_skip_arguments(self):
        result = self.run_wrapper()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assert_report()

    def test_driver_failure_preserves_its_status_and_partial_reports(self):
        result = self.run_wrapper(CNA_FAKE_DRIVER_STATUS="42")
        self.assertEqual(result.returncode, 42, result.stdout)
        self.assert_report()

    def test_driver_failure_without_reports_still_fails(self):
        result = self.run_wrapper(CNA_FAKE_DRIVER_STATUS="42", CNA_FAKE_NO_REPORT="1")
        self.assertEqual(result.returncode, 42, result.stdout)
        self.assertFalse(list(self.out.glob("*.json")))

    def test_timeout_preserves_status_124_and_partial_reports(self):
        result = self.run_wrapper(CNA_FAKE_HANG="1", CNA_MEDIA_ORACLE_TIMEOUT="2")
        self.assertEqual(result.returncode, 124, result.stdout)
        self.assert_report()

    def test_signal_failure_preserves_status_and_partial_reports(self):
        result = self.run_wrapper(CNA_FAKE_SIGNAL="1")
        self.assertEqual(result.returncode, 143, result.stdout)
        self.assert_report()

    def test_compiler_failure_is_not_hidden(self):
        result = self.run_wrapper(CNA_FAKE_COMPILER_STATUS="17")
        self.assertEqual(result.returncode, 17, result.stdout)

    def test_path_translation_failure_is_not_hidden(self):
        result = self.run_wrapper(CNA_FAKE_WINEPATH_STATUS="23")
        self.assertEqual(result.returncode, 23, result.stdout)


if __name__ == "__main__":
    unittest.main()
