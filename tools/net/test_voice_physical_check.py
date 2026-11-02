#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Exercise the physical-voice wrapper with isolated processes and generated WAVs only."""

import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest


SCRIPT = Path(__file__).with_name("voice_physical_check.sh")
FAKE = r'''
import math, os, signal, struct, sys, time, wave
from pathlib import Path

tool = Path(sys.argv[0]).name
case = os.environ['VOICE_TEST_CASE']
kind = os.environ.get('CNA_VOICE_PHYSICAL', '')
if tool == 'pactl':
    if case == 'pactl': sys.exit(13)
    print('Mute: yes' if case == 'muted' else 'Mute: no') if sys.argv[1] == 'get-sink-mute' else print('fake-device')
elif tool == 'pw-play':
    sys.exit(17 if case == 'tone' else 0)
elif tool == 'sleep':
    duration = sys.argv[1]
    names = []
    if duration == '4':
        names = ['sink-during.wav'] + ([] if case == 'muted' else ['room-during.wav'])
    elif duration == '0.3': names = ['guide.wav']
    for _ in range(200):
        work = list(Path(os.environ['VOICE_TEST_OUT']).glob('work.*'))
        if not names or (work and all((work[0] / (name + '.ready')).exists() for name in names)): break
        time.sleep(.005)
    time.sleep(.002)
elif tool == 'pw-record':
    path = Path(sys.argv[-1])
    signal.signal(signal.SIGINT, lambda *_: sys.exit(130))
    samples = [int(12000 * math.sin(2 * math.pi * 433 * i / 8000)) for i in range(8000)]
    if path.name == 'guide.wav':
        samples = [sample if (i // 400) % 2 else 0 for i, sample in enumerate(samples)]
    if case == 'silent': samples = [0] * len(samples)
    if case == 'empty': samples = []
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(8000)
        wav.writeframes(struct.pack('<' + 'h' * len(samples), *samples))
    Path(str(path) + '.ready').touch()
    if case == 'recorder' and path.name == 'sink-during.wav': sys.exit(23)
    if case == 'room-recorder' and path.name == 'room-during.wav': sys.exit(24)
    if case == 'guide-recorder' and path.name == 'guide.wav': sys.exit(25)
    while True: time.sleep(.1)
elif tool == 'python3':
    code = sys.stdin.read()
    is_share = 'spectrum =' in code
    is_guide = 'bursts =' in code
    if case == 'share' and is_share: sys.exit(26)
    if case == 'guide-analysis' and is_guide: sys.exit(27)
    if (is_share or is_guide) and os.environ.get('VOICE_TEST_REAL_METRICS') != '1':
        print('0.9999 rms=8485.3' if is_share else 'guide sounds at the output device: 6 bursts, peak rms 8485, quiet rms 0')
    else:
        # The shell heredoc receives the same argv it gets from a real python3 process.
        sys.argv = sys.argv[1:]
        exec(compile(code, '<voice probe heredoc>', 'exec'))
elif tool == 'cna_net_two_process_harness':
    role = next(a.split('=', 1)[1] for a in sys.argv if a.startswith('--role='))
    if role == 'voice-physical-host':
        if case != 'missing-port': print('PORT=31337', flush=True)
        sys.exit(31 if case == kind + '-host' else 0)
    elif role == 'voice-physical-client':
        if case != 'missing-join': print('JOINED', flush=True)
        print('CAPTURE decoded-seconds=3.0' if kind == 'capture' else 'PLAYBACK talking=1', flush=True)
        sys.exit(32 if case == kind + '-client' else 0)
    else:
        print('GUIDE_SOUNDS played', flush=True)
        sys.exit(33 if case == 'guide' else 0)
else:
    raise SystemExit('unexpected fake tool ' + tool)
'''


class VoicePhysicalCheckTests(unittest.TestCase):
    def run_case(self, case, *, real_metrics=False):
        with tempfile.TemporaryDirectory(prefix="cna voice wrapper ") as directory:
            root = Path(directory)
            bin_dir = root / "bin"
            build = root / "build"
            out = root / "evidence"
            bin_dir.mkdir(); build.mkdir()
            driver = bin_dir / "fake-driver"
            driver.write_text("#!" + sys.executable + "\n" + textwrap.dedent(FAKE))
            driver.chmod(0o755)
            for tool in ("pactl", "pw-play", "pw-record", "python3", "sleep"):
                (bin_dir / tool).symlink_to(driver)
            (build / "cna_net_two_process_harness").symlink_to(driver)
            environment = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ["PATH"],
                               VOICE_TEST_CASE=case, VOICE_TEST_OUT=str(out),
                               VOICE_TEST_REAL_METRICS="1" if real_metrics else "0")
            result = subprocess.run(["bash", str(SCRIPT), str(build), str(out)],
                                    env=environment, capture_output=True, text=True, timeout=15)
            evidence = out / "voice-physical.txt"
            report = evidence.read_text() if evidence.exists() else ""
            self.assertFalse(list(out.glob("work.*")), "temporary recordings were not removed")
            return result, report

    def test_successful_processes_and_analyses(self):
        result, report = self.run_case("ok")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("capture exit=0", report)
        self.assertIn("playback exit=0", report)
        self.assertIn("guide sounds at the output device", report)

    def test_muted_speaker_keeps_its_existing_capture_policy(self):
        result, report = self.run_case("muted")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("heard by the microphone", report)

    def test_child_failures_cannot_be_hidden_by_later_success(self):
        for case in ("capture-client", "capture-host", "playback-client", "playback-host",
                     "tone", "recorder", "room-recorder", "guide", "guide-recorder"):
            with self.subTest(case=case):
                result, report = self.run_case(case)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("guide sounds at the output device", report,
                              "a child failure should retain later phase evidence")

    def test_analysis_and_setup_failures_propagate(self):
        for case in ("share", "guide-analysis", "pactl", "missing-port", "missing-join"):
            with self.subTest(case=case):
                result, _ = self.run_case(case)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "real WAV metrics require NumPy")
    def test_real_generated_wav_metrics(self):
        result, report = self.run_case("ok", real_metrics=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        share = re.search(r"output device: ([0-9.]+) rms=", report)
        self.assertIsNotNone(share)
        self.assertGreaterEqual(float(share[1]), .99)
        self.assertLessEqual(float(share[1]), 1.0)
        bursts = re.search(r"output device: ([0-9]+) bursts", report)
        self.assertIsNotNone(bursts)
        self.assertGreater(int(bursts[1]), 0)
        self.assertNotIn("nan", report.lower())

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "real WAV metrics require NumPy")
    def test_empty_or_silent_wav_is_an_analysis_failure(self):
        for case in ("empty", "silent"):
            with self.subTest(case=case):
                result, _ = self.run_case(case, real_metrics=True)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
