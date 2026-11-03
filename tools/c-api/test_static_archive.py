#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Link-closure regression tests; no compiler, build tree or temporary files needed."""
import shlex
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import generate_static_archive as archive


class StaticClosureTests(unittest.TestCase):
    build = Path("/build")
    module = Path("/build/CNA/modules/c-api")
    runtime_dir = Path("/build/CNA")
    runtime = Path("/build/CNA/libcna.so")

    def tokens(self, module, build, target="cna_c_api"):
        self.assertEqual(build, self.build)
        if target == "cna_shared":
            self.assertEqual(module, self.runtime_dir)
            return ("c++ generated/stub.o modules/core/libcore.a libsharp.a "
                    "modules/core/libcore.a -lSDL3 -lcurl -o libcna.so".split(), self.runtime_dir)
        self.assertEqual(module, self.module)
        return ("c++ CnaCApi.o /build/CNA/libcna.so /build/CNA/libsharp.a "
                "-lSDL3 -o libcna_c_api.so".split(), self.module)

    @patch.object(archive, "link_line_tokens")
    def test_expands_runtime_and_deduplicates_archives(self, tokens):
        tokens.side_effect = self.tokens
        objects, archives, external = archive.read_static_closure(
            self.module, self.build, self.runtime, self.runtime_dir)
        self.assertEqual(objects, [str(self.module / "CnaCApi.o"), "/build/CNA/generated/stub.o"])
        self.assertEqual(archives, ["/build/CNA/libsharp.a", "/build/CNA/modules/core/libcore.a"])
        self.assertEqual(external, ["-lSDL3", "-lcurl"])

    @patch.object(archive, "link_line_tokens")
    def test_adapter_without_archives_can_expand_runtime(self, tokens):
        tokens.side_effect = lambda module, build, target="cna_c_api": (
            self.tokens(module, build, target) if target == "cna_shared" else
            ("c++ CnaCApi.o /build/CNA/libcna.so -o libcna_c_api.so".split(), self.module))
        _, archives, external = archive.read_static_closure(
            self.module, self.build, self.runtime, self.runtime_dir)
        self.assertEqual(len(archives), 2)
        self.assertNotIn(str(self.runtime), external)

    @patch.object(archive, "link_line_tokens")
    def test_static_runtime_configuration_unchanged(self, tokens):
        tokens.return_value = ("c++ api.o core.a core.a -lm -o libcna_c_api.so".split(), self.module)
        self.assertEqual(archive.read_static_closure(self.module, self.build),
                         ([str(self.module / "api.o")], [str(self.module / "core.a")], ["-lm"]))

    @patch.object(archive, "link_line_tokens")
    def test_missing_runtime_edge_is_refused(self, tokens):
        tokens.return_value = ("c++ api.o core.a -o libcna_c_api.so".split(), self.module)
        with self.assertRaisesRegex(SystemExit, "does not contain the shared runtime"):
            archive.read_static_closure(self.module, self.build, self.runtime, self.runtime_dir)

    def test_partial_runtime_configuration_is_refused(self):
        for library, directory in [(self.runtime, None), (None, self.runtime_dir)]:
            with self.subTest(library=library, directory=directory):
                with self.assertRaisesRegex(SystemExit, "supplied together"):
                    archive.read_static_closure(self.module, self.build, library, directory)

    @patch.object(archive, "link_line_tokens")
    def test_no_objects_or_no_static_archives_remains_an_error(self, tokens):
        for line in ["c++ core.a -o libcna_c_api.so", "c++ api.o -o libcna_c_api.so"]:
            tokens.return_value = (line.split(), self.module)
            with self.subTest(line=line):
                with self.assertRaisesRegex(SystemExit, "no objects or no archives"):
                    archive.read_static_closure(self.module, self.build)


class EmscriptenContractTests(unittest.TestCase):
    """MTH-PLAT-004 D2: the contract a C program linking the merged wasm archive needs."""

    build = Path("/build")
    # The never-built contract program's line in the WEBGPU Release tree, shortened.
    line = ("/emsdk/upstream/emscripten/emcc -O3 -DNDEBUG -lembind -fexceptions "
            "-sDISABLE_EXCEPTION_CATCHING=0 --use-port=emdawnwebgpu -lidbfs.js "
            "--pre-js /src/modules/storage/src/WebStoragePre.js -sSTACK_SIZE=1048576 "
            "modules/c-api/CMakeFiles/contract.dir/wasm/module_entry.c.o "
            "-o modules/c-api/contract.js modules/c-api/libcna_c_api.a "
            "modules/core/libcna_core.a -lm modules/core/libcna_core.a")

    def contract(self, line):
        return archive.emscripten_link_contract(shlex.split(line), self.build)

    def test_keeps_closure_options_and_names_the_files_to_ship(self):
        options, libraries, archives = self.contract(self.line)
        self.assertEqual(options, [
            "-lembind", "-fexceptions", "-sDISABLE_EXCEPTION_CATCHING=0",
            "--use-port=emdawnwebgpu", "-lidbfs.js",
            ("--pre-js", Path("/src/modules/storage/src/WebStoragePre.js")),
            "-sSTACK_SIZE=1048576"])
        self.assertEqual(libraries, ["-lm"])
        self.assertEqual(archives, ["/build/modules/c-api/libcna_c_api.a",
                                    "/build/modules/core/libcna_core.a"])

    def test_relative_file_and_quoted_option(self):
        options, _, _ = self.contract(
            "em++ \"-sEXPORTED_RUNTIME_METHODS=['ccall','FS']\" --js-library lib/x.js "
            "--js-library lib/x.js -o out.js")
        self.assertEqual(options, ["-sEXPORTED_RUNTIME_METHODS=['ccall','FS']",
                                   ("--js-library", Path("/build/lib/x.js"))])

    def test_producer_paths_are_refused(self):
        for option in ("-sEXPORTED_FUNCTIONS=@/build/exports.json", "-sEXPORTED_FUNCTIONS=@x",
                       "-L/usr/lib", "--use-port=/src/port.py", "-Wl,-rpath,/build"):
            with self.subTest(option=option):
                with self.assertRaisesRegex(SystemExit, "names a path"):
                    self.contract(f"emcc {option} -o out.js")

    def test_only_merged_archives_and_toolchain_libraries_follow_the_objects(self):
        for library in ("-Wl,--whole-archive", "/usr/lib/libfoo.so", "-l/x/y"):
            with self.subTest(library=library):
                with self.assertRaisesRegex(SystemExit, "unexpected link library"):
                    self.contract(f"emcc a.o -o out.js core.a {library}")
        for stray in ("stray.js", "/build/stray.js"):
            with self.subTest(stray=stray):
                with self.assertRaisesRegex(SystemExit, "unexpected link input"):
                    self.contract(f"emcc {stray} -o out.js")

    def test_one_install_directory_cannot_hold_two_files_of_one_name(self):
        with self.assertRaisesRegex(SystemExit, "both named pre.js"):
            self.contract("emcc --pre-js /a/pre.js --post-js /b/pre.js -o out.js")

    def write(self, asyncify):
        class Sink:
            text = ""

            def write_text(self, text, encoding):
                Sink.text = text
        options, libraries, _ = self.contract(self.line)
        archive.write_emscripten_targets(Sink(), "libcna_c_api_static.a", options, libraries,
                                         "../share/cna/emscripten", asyncify)
        return Sink.text

    def test_targets_file_exports_the_contract_and_the_cxx_runtime(self):
        text = self.write(["-sASYNCIFY=1", "-sASYNCIFY_STACK_SIZE=65536"])
        self.assertIn('"${_cna_package_lib_dir}/../share/cna/emscripten" ABSOLUTE)', text)
        self.assertIn(
            'INTERFACE_LINK_OPTIONS "-sDEFAULT_TO_CXX=1;-lembind;-fexceptions;'
            '-sDISABLE_EXCEPTION_CATCHING=0;--use-port=emdawnwebgpu;-lidbfs.js;'
            'SHELL:--pre-js \\"${_cna_static_js_dir}/WebStoragePre.js\\";-sSTACK_SIZE=1048576"',
            text)
        self.assertIn('INTERFACE_LINK_LIBRARIES "-lm"', text)
        self.assertIn('add_library(CNA::CApiStaticAsyncify INTERFACE IMPORTED)', text)
        self.assertIn('INTERFACE_LINK_OPTIONS "-sASYNCIFY=1;-sASYNCIFY_STACK_SIZE=65536"', text)
        self.assertNotIn("/src/", text)
        self.assertNotIn("/build/", text)

    def test_no_asyncify_target_without_its_options(self):
        self.assertNotIn("CApiStaticAsyncify INTERFACE IMPORTED", self.write([]))

    def test_cmake_specials_are_escaped_or_refused(self):
        self.assertEqual(archive.cmake_quote('-s"X"=$Y'), '-s\\"X\\"=\\$Y')
        with self.assertRaisesRegex(SystemExit, "contains ';'"):
            archive.cmake_quote("-sA;B")

    def test_a_targets_file_without_the_contract_is_refused_before_any_work(self):
        base = ["generate_static_archive.py", "--build-dir", "/nonexistent/build",
                "--output", "/nonexistent/build/libcna_c_api_static.a",
                "--merge-from", "cna_c_api_wasm", "--archiver", "llvm-ar",
                "--targets-file", "/nonexistent/build/CNACStaticTargets.cmake"]
        complete = ["--link-contract", "/nonexistent/contract.json", "--stage-dir", "/nonexistent/stage",
                    "--install-libdir", "lib", "--install-js-dir", "share/cna/emscripten"]
        for argv in (base, base + complete[:2], base + complete[:-1] + ["/abs/share"]):
            with self.subTest(argv=argv[len(base):]):
                with patch.object(sys, "argv", argv), patch("sys.stderr"), \
                        patch.object(archive, "merge_archives") as merge:
                    with self.assertRaises(SystemExit) as raised:
                        archive.main()
                    self.assertEqual(raised.exception.code, 2)
                    merge.assert_not_called()


    def test_the_launcher_records_the_argv_and_satisfies_the_build(self):
        command = ["/emsdk/em++", "-O3", "-lembind", "--pre-js", "/src/pre.js", "x.o",
                   "-o", "modules/c-api/contract.js", "libcna.a"]
        written = {}
        touched = []
        with patch.object(sys, "argv", ["generate_static_archive.py", "--record-link",
                                        "/build/contract.json", *command]), \
                patch.object(Path, "mkdir"), patch.object(archive.os, "getcwd", return_value="/build"), \
                patch.object(Path, "write_text", lambda path, text, encoding: written.update(
                    {str(path): text})), \
                patch.object(Path, "touch", lambda path: touched.append(str(path))):
            self.assertEqual(archive.main(), 0)
        self.assertEqual(touched, ["modules/c-api/contract.js"])
        with patch.object(Path, "is_file", return_value=True), \
                patch.object(Path, "read_text", lambda path, encoding: written[str(path)]):
            self.assertEqual(archive.recorded_link(Path("/build/contract.json")),
                             (command, Path("/build")))

    def test_a_record_without_an_output_is_refused(self):
        with self.assertRaisesRegex(SystemExit, "names no output"):
            archive.record_link(Path("/nonexistent/contract.json"), ["/emsdk/emcc", "a.o"])


if __name__ == "__main__":
    unittest.main()
