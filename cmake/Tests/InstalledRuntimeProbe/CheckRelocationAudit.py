#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Exercise the relocation gate's path refusals without building an SDK."""

import importlib.util
import contextlib
import io
import json
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

    def test_static_mixed_requires_the_runtime_form_diagnostic(self):
        diagnostic = ('CMake Error: The INTERFACE_CNA_RUNTIME_FORM property of "CNA::CApiStatic" does\n'
                      'not agree with the value of CNA_RUNTIME_FORM already determined\n'
                      'for "InstalledStaticMixed".')
        checker.check_runtime_form_conflict(1, diagnostic)
        checker.check_runtime_form_conflict(1, diagnostic.replace("CNA::CApiStatic", "CNA::Math"))
        for code, output in ((0, diagnostic), (1, "CMake Error: missing package"),
                             (1, "undefined reference to CNA_RUNTIME_FORM"),
                             (1, diagnostic.replace("CNA_RUNTIME_FORM", "OTHER_PROPERTY"))):
            with self.subTest(code=code, output=output), self.assertRaisesRegex(
                    RuntimeError, "did not refuse"):
                checker.check_runtime_form_conflict(code, output)

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

    def test_native_consumer_uses_the_producer_compilers(self):
        args = checker.consumer_toolchain_args(
            {"CMAKE_C_COMPILER": "/usr/bin/cc", "CMAKE_CXX_COMPILER": "/usr/bin/c++",
             "CMAKE_BUILD_TYPE": "Release"}, self.moved)
        self.assertEqual(args, ["-DCMAKE_C_COMPILER=/usr/bin/cc", "-DCMAKE_CXX_COMPILER=/usr/bin/c++",
                                "-DCMAKE_BUILD_TYPE=Release"])

    def test_cross_consumer_uses_the_toolchain_and_moved_find_root(self):
        args = checker.consumer_toolchain_args(
            {"CMAKE_TOOLCHAIN_FILE": "/emsdk/Emscripten.cmake", "CMAKE_CROSSCOMPILING_EMULATOR": "/emsdk/node",
             "CMAKE_C_COMPILER": "/emsdk/emcc", "CMAKE_BUILD_TYPE": "Release"}, self.moved)
        self.assertEqual(args, ["-DCMAKE_TOOLCHAIN_FILE=/emsdk/Emscripten.cmake",
                                "-DCMAKE_CROSSCOMPILING_EMULATOR=/emsdk/node",
                                "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_FIND_ROOT_PATH={self.moved}",
                                f"-DPROBE_FIND_ROOT={self.moved}"])

    def test_cross_dependency_prefixes_are_explicit_and_keep_the_moved_sdk_first(self):
        # A cross SDK can require an installed third-party libcurl; it still must not use CNA sources.
        args = checker.consumer_toolchain_args(
            {"CMAKE_TOOLCHAIN_FILE": "/toolchains/mingw.cmake"}, self.moved,
            [Path("/installed/curl")])
        roots = f"{self.moved};/installed/curl"
        self.assertIn(f"-DCMAKE_FIND_ROOT_PATH={roots}", args)
        self.assertIn(f"-DPROBE_FIND_ROOT={roots}", args)

    def test_emscripten_runs_node_tests_and_a_private_browser_frame(self):
        # Mock every build/install/browser process; only the driver's routing is under test.
        with tempfile.TemporaryDirectory(prefix="sdk-audit-") as directory:
            build = Path(directory) / "producer"
            build.mkdir()
            (build / "CMakeCache.txt").write_text(
                "CNA_INSTALL_CXX:BOOL=ON\nCNA_BUILD_C_API:BOOL=ON\nCNA_C_API_BUILD_STATIC:BOOL=ON\n"
                "CNA_SHARED_LIBRARY:BOOL=OFF\nCNA_GRAPHICS_RENDERER:STRING=WEBGPU\n"
                "CMAKE_GENERATOR:INTERNAL=Ninja\nEMSCRIPTEN:INTERNAL=1\n"
                "CMAKE_TOOLCHAIN_FILE:FILEPATH=/emsdk/Emscripten.cmake\n"
                "CMAKE_CROSSCOMPILING_EMULATOR:FILEPATH=/emsdk/node\n"
                "CMAKE_C_COMPILER:FILEPATH=/emsdk/emcc\n")
            # modules/c-api/CMakeLists.txt publishes the static archive only on supported targets.
            install = build / "modules/c-api/cmake_install.cmake"
            install.parent.mkdir(parents=True)
            install.write_text('file(INSTALL FILES "CNACStaticTargets.cmake")\n')
            work = Path(directory) / "work"
            calls = []

            def fake_run(command, **kwargs):
                calls.append((command, kwargs["env"]))
                if "--install" in command:
                    Path(command[command.index("--prefix") + 1]).mkdir(parents=True, exist_ok=True)
                if any(argument.startswith("-DPROBE_MIXED_TARGET=") for argument in command):
                    kwargs["stdout"].write('INTERFACE_CNA_RUNTIME_FORM property of "CNA::CApiStatic" '
                                           'does not agree with the value of CNA_RUNTIME_FORM already '
                                           'determined for "InstalledStaticMixed"')
                    return SimpleNamespace(returncode=1)
                return SimpleNamespace(returncode=0)

            with patch.object(sys, "argv", [str(checker.__file__), "--build-dir", str(build),
                                            "--work-dir", str(work), "--keep-artifacts"]), \
                    patch.dict(checker.os.environ, {"TMPDIR": "/a/long/lane/tmp"}), \
                    patch.object(checker.subprocess, "run", fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                checker.main()
            configures = [command for command, _ in calls if "-S" in command]
            self.assertTrue(configures)
            for command in configures:
                self.assertIn("-DCMAKE_TOOLCHAIN_FILE=/emsdk/Emscripten.cmake", command)
                self.assertIn(f"-DCMAKE_FIND_ROOT_PATH={work / 'relocated'}", command)
                self.assertIn(f"-DPROBE_FIND_ROOT={work / 'relocated'}", command)
                self.assertFalse(any(argument.startswith("-DCMAKE_C_COMPILER=") for argument in command))
                self.assertNotIn("-DPROBE_CASE=CApiOnly", command)
            # MTH-PLAT-004 D2: the pure-C case of the static archive, in a C-only project.
            static = [command for command in configures if "-DPROBE_CASE=CApiStaticWasm" in command]
            self.assertEqual(len(static), 1)
            self.assertIn("-DPROBE_C_ONLY=ON", static[0])
            private = [(command, env) for command, env in calls
                       if command[0].endswith("run_gpu_tests_private.sh")]
            self.assertEqual([command[-1] for command, _ in private],
                             ["InstalledBrowserFrame.html", "InstalledCApiBrowser.html"],
                             "only the browser pages may use the private runner")
            self.assertEqual(private[1][0][-2], str(work / "CApiStaticWasm"))
            for command, env in private:
                self.assertEqual(command[1:3], ["--exec", "/emsdk/node"])
                self.assertNotIn("TMPDIR", env)
                self.assertEqual(env["CNA_BROWSER_RENDERER"], "WEBGPU")
            tests = [command for command, _ in calls if "--test-dir" in command]
            self.assertTrue(tests and all(command[0] == "ctest" for command in tests))
            self.assertIn(str(work / "CApiStaticWasm"), [command[2] for command in tests])
            audit = json.loads((work / "audit.json").read_text())
            self.assertTrue(audit["passed"])
            self.assertTrue(audit["browser"].startswith("passed"))
            self.assertTrue(audit["c_api_package"].startswith("passed"))

    def test_static_archive_is_built_before_install(self):
        # Model a clean producer whose combined archive does not exist yet.
        # Stop at CNACApi installation; no compiler, display or SDK is needed.
        with tempfile.TemporaryDirectory(prefix="sdk-audit-") as directory:
            build = Path(directory)
            (build / "CMakeCache.txt").write_text(
                "CNA_INSTALL_CXX:BOOL=ON\nCNA_BUILD_C_API:BOOL=ON\n"
                "CNA_C_API_BUILD_STATIC:BOOL=ON\nCMAKE_GENERATOR:INTERNAL=Ninja\n")
            install = build / "modules/c-api/cmake_install.cmake"
            install.parent.mkdir(parents=True)
            install.write_text('file(INSTALL FILES "CNACStaticTargets.cmake")\n')
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

    def test_prepare_only_windows_never_builds_an_unpublished_archive_or_runs_tests(self):
        # Oracle: modules/c-api/CMakeLists.txt gates static publication on target UNIX, not host POSIX.
        # --prepare-only must preserve all runtime work as pending, even when every subprocess returns 0.
        with tempfile.TemporaryDirectory(prefix="sdk-audit-") as directory:
            build = Path(directory) / "producer"
            build.mkdir()
            (build / "CMakeCache.txt").write_text(
                "CNA_INSTALL_CXX:BOOL=ON\nCNA_BUILD_C_API:BOOL=ON\nCNA_C_API_BUILD_STATIC:BOOL=ON\n"
                "CNA_GRAPHICS_RENDERER:STRING=DIRECTX9\nCNA_DIRECTX9_COMPILED_EFFECTS:BOOL=ON\n"
                "CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_TOOLCHAIN_FILE:FILEPATH=/toolchains/mingw.cmake\n")
            install = build / "modules/c-api/cmake_install.cmake"
            install.parent.mkdir(parents=True)
            install.write_text('file(INSTALL FILES "CNACApiTargets.cmake")\n')
            work = Path(directory) / "work"
            calls = []

            def fake_run(command, **kwargs):
                calls.append(command)
                if "--install" in command:
                    Path(command[command.index("--prefix") + 1]).mkdir(parents=True, exist_ok=True)
                return SimpleNamespace(returncode=0)

            with patch.object(sys, "argv", [str(checker.__file__), "--build-dir", str(build),
                                            "--work-dir", str(work), "--compiled-effects",
                                            "--prepare-only", "--keep-artifacts"]), \
                    patch.object(checker.subprocess, "run", fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                checker.main()
            self.assertFalse(any("cna_c_api_static" in command for command in calls))
            self.assertFalse(any("--test-dir" in command for command in calls))
            self.assertFalse(any(command[0].endswith("run_gpu_tests_private.sh") for command in calls))
            configures = [command for command in calls if "-S" in command]
            self.assertTrue(configures)
            self.assertTrue(all("-DPROBE_EXPECT_COMPILED_EFFECTS=ON" in command for command in configures))
            if sys.platform == "linux":
                launcher = Path(checker.__file__).resolve().parents[2] / "scripts/run-wine-dxvk9.sh"
                self.assertTrue(all(f"-DPROBE_COMPILED_LAUNCHER={launcher}" in command
                                    for command in configures))
            self.assertTrue(all(f"-DPROBE_FIND_ROOT={work / 'relocated'}" in command for command in configures))
            audit = json.loads((work / "audit.json").read_text())
            self.assertTrue(audit["prepared"])
            self.assertFalse(audit["passed"])
            self.assertEqual(audit["compiled_effects"]["renderer"], "DIRECTX9")
            self.assertTrue(audit["compiled_effects"]["expected_capability"])
            self.assertIn("runtime", audit["runtime_tests_pending"])
            self.assertIn("CppOnly", audit["runtime_tests_pending"])


if __name__ == "__main__":
    unittest.main()
