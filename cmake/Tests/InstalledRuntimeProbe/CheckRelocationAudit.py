#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Exercise the relocation gate's path refusals without building an SDK."""

import importlib.util
import contextlib
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("sdk_check", sys.argv.pop(1))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class RelocationAudit(unittest.TestCase):
    source = Path("/producer").resolve()
    moved = source / "build-consumer/moved"
    consumer = source / "build-consumer/consumer"
    forbidden = {source}
    allowed = (moved, consumer)

    def test_moved_prefix_inside_source_is_allowed(self):
        checker.check_path(self.moved / "include", self.forbidden, self.allowed)

    def test_source_header_is_refused(self):
        with self.assertRaisesRegex(RuntimeError, "source/build path leaked"):
            checker.check_path(self.source / "modules/math/include", self.forbidden, self.allowed)

    def test_similar_system_prefix_is_allowed(self):
        checker.check_path(Path(str(self.source) + "-system/lib"), self.forbidden, self.allowed)

    def test_cmake_and_pkgconfig_producer_prefixes_are_refused(self):
        for text in ('set(prefix "/producer")', 'prefix=/producer\n',
                     'set(libs "/producer/lib/libcna.a")'):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                checker.check_metadata(text, self.forbidden)

    def test_relocatable_metadata_is_allowed(self):
        checker.check_metadata('prefix=${pcfiledir}/../..\n'
                               'set(system "/producer-system/lib")', self.forbidden)

    def test_link_search_and_direct_library_leaks_are_refused(self):
        for fragment in ("-L/producer/build", "-L /producer/build", "/producer/build/libcna.a",
                         "-Wl,--push-state,--no-as-needed,/producer/build/libcna.so,--pop-state",
                         "-Wl,-rpath,/producer/build", "-Wl,-rpath=/producer/build",
                         "-Wl,--library-path=/producer/build", "../../build/libcna.a"):
            with self.subTest(fragment=fragment), self.assertRaises(RuntimeError):
                checker.check_link(fragment, self.forbidden, self.allowed, self.consumer)

    def test_moved_and_system_link_inputs_are_allowed(self):
        checker.check_link(f'-L"{self.moved}/lib" "{self.moved}/lib/libcna.so" '
                           '-Wl,-rpath,\'$ORIGIN/../lib\' -lpthread /usr/lib/libm.so',
                           self.forbidden, self.allowed, self.consumer)

    def test_origin_runtime_lookup_is_allowed(self):
        checker.check_dynamic("(RUNPATH) Library runpath: [$ORIGIN/../lib]",
                              self.moved / "bin/app", self.forbidden, self.allowed)

    def test_origin_escape_is_refused(self):
        with self.assertRaises(RuntimeError):
            checker.check_dynamic("(RUNPATH) Library runpath: [${ORIGIN}/../../..]",
                                  self.moved / "bin/app", self.forbidden, self.allowed)

    def test_cwd_runtime_lookup_is_refused(self):
        for path in ("", "lib", ":$ORIGIN", "$ORIGIN:"):
            with self.subTest(path=path), self.assertRaisesRegex(RuntimeError, "cwd-dependent"):
                checker.check_dynamic(f"(RPATH) Library rpath: [{path}]",
                                      self.moved / "bin/app", self.forbidden, self.allowed)

    def test_absolute_needed_build_library_is_refused(self):
        with self.assertRaises(RuntimeError):
            checker.check_dynamic("(NEEDED) Shared library: [/producer/build/libcna.so]",
                                  self.moved / "bin/app", self.forbidden, self.allowed)

    def test_static_archive_is_built_before_install(self):
        # Model a clean producer whose combined archive does not exist yet.
        # Stop at CNACApi installation; no compiler, display or SDK is needed.
        with tempfile.TemporaryDirectory(prefix="sdk-audit-") as directory:
            build = Path(directory)
            (build / "CMakeCache.txt").write_text(
                "CNA_INSTALL_CXX:BOOL=ON\nCNA_BUILD_C_API:BOOL=ON\n"
                "CNA_C_API_BUILD_STATIC:BOOL=ON\nCMAKE_GENERATOR:INTERNAL=Ninja\n")
            built = False
            installed_after_build = False

            def fake_run(command, **kwargs):
                nonlocal built, installed_after_build
                if "--target" in command and "cna_c_api_static" in command:
                    built = True
                if "--component" in command and "CNACApi" in command:
                    installed_after_build = built
                    return SimpleNamespace(returncode=1)
                return SimpleNamespace(returncode=0)

            with patch.object(sys, "argv", [str(checker.__file__), "--build-dir", str(build)]), \
                    patch.object(checker.subprocess, "run", fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(RuntimeError, "install-CNACApi failed"):
                    checker.main()
            self.assertTrue(installed_after_build, "clean install attempted before the static archive exists")


if __name__ == "__main__":
    unittest.main()
