#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Exercise real probes and linker-wrapped error paths using synthetic local payloads."""

import argparse
import os
from pathlib import Path
import resource
import subprocess
import tempfile
import unittest


OPTIONS = None


def no_core_dump():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def crc32c(data):
    value = 0xffffffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x82f63b78 if value & 1 else 0)
    return value ^ 0xffffffff


class ProbeTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="cna probe controls ")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.payload = self.root / "input.payload"
        self.data = bytes(range(256)) * 32 + b"end"
        self.payload.write_bytes(self.data)

    def run_probe(self, binary, *arguments, fault=None):
        environment = os.environ.copy()
        environment.pop("CNA_CNBF_PROBE_FAULT", None)
        if fault:
            environment["CNA_CNBF_PROBE_FAULT"] = fault
        return subprocess.run([binary, *map(str, arguments)], capture_output=True, text=True,
                              env=environment, timeout=30, preexec_fn=no_core_dump)

    def compression(self):
        if not OPTIONS.compression:
            self.skipTest("Zstandard/zlib probe dependencies are not enabled")

    def test_inputs_must_exist_be_nonempty_and_regular(self):
        empty = self.root / "empty"
        empty.touch()
        binaries = [OPTIONS.mmap]
        if OPTIONS.compression:
            binaries.append(OPTIONS.compression)
        for binary in binaries:
            with self.subTest(binary=binary, case="usage"):
                result = self.run_probe(binary)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            for path in (self.root / "missing", empty, self.root):
                with self.subTest(binary=binary, path=path):
                    result = self.run_probe(binary, path)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertTrue(result.stderr)

    def test_real_compression_roundtrips(self):
        self.compression()
        result = self.run_probe(OPTIONS.compression, self.payload)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for label in ("zstd-1", "zstd-3", "zstd-9", "zstd-19", "zlib-6"):
            self.assertIn(label, result.stdout)
        self.assertNotIn("mismatch", result.stderr)

    def test_codec_errors_lengths_and_mismatches_fail(self):
        self.compression()
        for codec in ("zstd", "zlib"):
            for suffix in ("compress", "timed-compress", "decompress", "timed-decompress",
                           "length", "mismatch", "timed-output", "timed-mismatch"):
                with self.subTest(codec=codec, suffix=suffix):
                    result = self.run_probe(OPTIONS.compression_faults, "--codec=" + codec,
                                            self.payload, fault=codec + "-" + suffix)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertTrue(result.stderr)

    def test_mmap_io_errors_are_graceful_failures(self):
        for fault in ("open", "read", "short-read", "mmap"):
            with self.subTest(fault=fault):
                result = self.run_probe(OPTIONS.mmap_faults, self.payload, fault=fault)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertTrue(result.stderr)
        interrupted = self.run_probe(OPTIONS.mmap_faults, self.payload, fault="eintr")
        self.assertEqual(interrupted.returncode, 0, interrupted.stdout + interrupted.stderr)

    def test_mapped_scan_checks_the_last_byte(self):
        result = self.run_probe(OPTIONS.mmap_faults, self.payload, fault="mapped-tail")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("CRC", result.stderr)
        self.assertEqual(self.payload.read_bytes(), self.data, "MAP_PRIVATE changed the input")

    def test_crc_known_answer_boundaries_and_forced_table(self):
        self.assertEqual(crc32c(b"123456789"), 0xe3069283)
        for data in (b"123456789", b"x", b"abcdefg", b"abcdefgh", self.data):
            with self.subTest(size=len(data)):
                self.payload.write_bytes(data)
                expected = f"{crc32c(data):08X}"
                for option in ([], ["--crc=table"]):
                    result = self.run_probe(OPTIONS.mmap, *option, self.payload)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("CRC32C=" + expected, result.stdout)
                    if option:
                        self.assertIn("hardware CRC disabled", result.stdout)

    def test_one_good_input_does_not_hide_a_bad_one(self):
        binaries = [OPTIONS.mmap]
        if OPTIONS.compression:
            binaries.append(OPTIONS.compression)
        for binary in binaries:
            with self.subTest(binary=binary):
                result = self.run_probe(binary, self.payload, self.root / "missing")
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_output_write_failure_is_not_a_successful_measurement(self):
        binaries = [OPTIONS.mmap]
        if OPTIONS.compression:
            binaries.append(OPTIONS.compression)
        for binary in binaries:
            with self.subTest(binary=binary):
                read_fd, write_fd = os.pipe()
                os.close(read_fd)
                try:
                    result = subprocess.run([binary, str(self.payload)], stdout=write_fd,
                        stderr=subprocess.PIPE, text=True, timeout=30, restore_signals=False,
                        preexec_fn=no_core_dump)
                finally:
                    os.close(write_fd)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("output", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mmap", required=True)
    parser.add_argument("--mmap-faults", required=True)
    parser.add_argument("--compression")
    parser.add_argument("--compression-faults")
    OPTIONS, unittest_arguments = parser.parse_known_args()
    unittest.main(argv=[__file__, *unittest_arguments])
