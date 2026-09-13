#!/usr/bin/env python3
"""Generate a deterministic C++ shader-package header from declared sources."""

from __future__ import annotations

import argparse
import ctypes
import glob
import hashlib
import json
import os
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SKIP = 77
STAGES = {"vertex": 0, "fragment": 1, "compute": 2}
FORMATS = {"text", "spirv"}
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
NAMESPACE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*$")
SPIRV_MAGIC = 0x07230203
SHADERC_TARGET_ENV_VULKAN = 0
SHADERC_ENV_VERSION_VULKAN_1_0 = 1 << 22
SHADERC_SPIRV_VERSION_1_0 = 0x010000
SHADERC_OPTIMIZATION_PERFORMANCE = 2


class ToolchainUnavailable(RuntimeError):
    """Raised when a declared compiled payload cannot be produced offline."""


@dataclass(frozen=True)
class Payload:
    symbol: str
    source_path: Path
    source_name: str
    source: bytes
    language: str
    source_language: str
    stage: str
    output_format: str
    entry_point: str


@dataclass(frozen=True)
class Toolchain:
    path: Path
    sha256: str
    spirv_version: int
    spirv_revision: int


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_manifest(path: Path) -> tuple[dict[str, Any], bytes, list[Payload]]:
    raw = path.read_bytes()
    try:
        manifest = json.loads(raw)
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid JSON in {path}: {error}") from error
    if manifest.get("schema") != 1:
        raise ValueError("manifest 'schema' must be 1")
    namespace = manifest.get("namespace")
    package = manifest.get("package")
    preprocessor_guard = manifest.get("preprocessor_guard")
    if not isinstance(namespace, str) or not NAMESPACE.fullmatch(namespace):
        raise ValueError("manifest 'namespace' is not a valid C++ namespace")
    if not isinstance(package, str) or not IDENTIFIER.fullmatch(package):
        raise ValueError("manifest 'package' is not a valid identifier")
    if (preprocessor_guard is not None and
            (not isinstance(preprocessor_guard, str) or
             not IDENTIFIER.fullmatch(preprocessor_guard))):
        raise ValueError("manifest 'preprocessor_guard' is not a valid identifier")
    declarations = manifest.get("payloads")
    if not isinstance(declarations, list) or not declarations:
        raise ValueError("manifest 'payloads' must be a non-empty array")

    base = path.parent.resolve()
    seen_symbols: set[str] = set()
    payloads: list[Payload] = []
    required = {"symbol", "source", "language", "stage", "format", "entry_point"}
    for index, declaration in enumerate(declarations):
        if not isinstance(declaration, dict):
            raise ValueError(f"payload {index} must be an object")
        missing = required - declaration.keys()
        if missing:
            raise ValueError(f"payload {index} is missing {', '.join(sorted(missing))}")
        symbol = declaration["symbol"]
        stage = declaration["stage"]
        output_format = declaration["format"]
        entry_point = declaration["entry_point"]
        language = declaration["language"]
        source_language = declaration.get("source_language", language)
        if not isinstance(symbol, str) or not IDENTIFIER.fullmatch(symbol):
            raise ValueError(f"payload {index} has an invalid symbol")
        if symbol in seen_symbols:
            raise ValueError(f"duplicate payload symbol '{symbol}'")
        seen_symbols.add(symbol)
        if stage not in STAGES:
            raise ValueError(f"payload '{symbol}' has unsupported stage '{stage}'")
        if output_format not in FORMATS:
            raise ValueError(f"payload '{symbol}' has unsupported format '{output_format}'")
        if output_format == "spirv" and language != "spirv":
            raise ValueError(f"payload '{symbol}' must declare language 'spirv'")
        if output_format == "spirv" and source_language != "vulkan-glsl":
            raise ValueError(f"payload '{symbol}' must compile declared 'vulkan-glsl' source")
        if output_format == "text" and language not in {"glsl", "glsl-es", "vulkan-glsl"}:
            raise ValueError(f"payload '{symbol}' has an unsupported text language")
        if not isinstance(entry_point, str) or not IDENTIFIER.fullmatch(entry_point):
            raise ValueError(f"payload '{symbol}' has an invalid entry point")
        source_name = declaration["source"]
        if not isinstance(source_name, str) or not source_name:
            raise ValueError(f"payload '{symbol}' has an invalid source path")
        source_path = (base / source_name).resolve()
        if not source_path.is_relative_to(base):
            raise ValueError(f"payload '{symbol}' source escapes the manifest directory")
        source = source_path.read_bytes()
        if not source:
            raise ValueError(f"payload '{symbol}' source is empty")
        try:
            source.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ValueError(f"payload '{symbol}' source is not UTF-8") from error
        payloads.append(Payload(symbol, source_path, source_name, source, language,
                                source_language, stage, output_format, entry_point))
    return manifest, raw, payloads


def find_shaderc(explicit: str | None) -> Path:
    requested = explicit or os.environ.get("CNA_SHADERC_LIBRARY")
    if requested:
        path = Path(requested).expanduser().resolve()
        if not path.is_file():
            raise ToolchainUnavailable(f"shaderc library does not exist: {path}")
        return path

    candidates = [
        "/usr/lib/x86_64-linux-gnu/libshaderc.so.1",
        "/usr/lib/aarch64-linux-gnu/libshaderc.so.1",
        "/usr/local/lib/libshaderc.so.1",
    ]
    candidates.extend(sorted(glob.glob("/usr/lib/*/libshaderc.so.1")))
    for candidate in candidates:
        path = Path(candidate)
        if path.is_file():
            return path.resolve()
    raise ToolchainUnavailable(
        "libshaderc.so.1 was not found; install shaderc or set CNA_SHADERC_LIBRARY")


def configure_shaderc(library: ctypes.CDLL) -> None:
    library.shaderc_compiler_initialize.argtypes = []
    library.shaderc_compiler_initialize.restype = ctypes.c_void_p
    library.shaderc_compiler_release.argtypes = [ctypes.c_void_p]
    library.shaderc_compile_options_initialize.argtypes = []
    library.shaderc_compile_options_initialize.restype = ctypes.c_void_p
    library.shaderc_compile_options_release.argtypes = [ctypes.c_void_p]
    library.shaderc_compile_options_set_optimization_level.argtypes = [ctypes.c_void_p, ctypes.c_int]
    library.shaderc_compile_options_set_target_env.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                                               ctypes.c_uint]
    library.shaderc_compile_options_set_target_spirv.argtypes = [ctypes.c_void_p, ctypes.c_int]
    library.shaderc_compile_into_spv.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
                                                 ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p,
                                                 ctypes.c_void_p]
    library.shaderc_compile_into_spv.restype = ctypes.c_void_p
    library.shaderc_result_get_compilation_status.argtypes = [ctypes.c_void_p]
    library.shaderc_result_get_compilation_status.restype = ctypes.c_int
    library.shaderc_result_get_error_message.argtypes = [ctypes.c_void_p]
    library.shaderc_result_get_error_message.restype = ctypes.c_char_p
    library.shaderc_result_get_length.argtypes = [ctypes.c_void_p]
    library.shaderc_result_get_length.restype = ctypes.c_size_t
    library.shaderc_result_get_bytes.argtypes = [ctypes.c_void_p]
    library.shaderc_result_get_bytes.restype = ctypes.c_void_p
    library.shaderc_result_release.argtypes = [ctypes.c_void_p]
    library.shaderc_get_spv_version.argtypes = [ctypes.POINTER(ctypes.c_uint),
                                                ctypes.POINTER(ctypes.c_uint)]


class ShadercCompiler:
    def __init__(self, path: Path):
        try:
            self.library = ctypes.CDLL(str(path))
        except OSError as error:
            raise ToolchainUnavailable(f"could not load shaderc library {path}: {error}") from error
        configure_shaderc(self.library)
        self.compiler = self.library.shaderc_compiler_initialize()
        self.options = self.library.shaderc_compile_options_initialize()
        if not self.compiler or not self.options:
            self.close()
            raise RuntimeError("shaderc failed to allocate a compiler or options object")
        self.library.shaderc_compile_options_set_optimization_level(
            self.options, SHADERC_OPTIMIZATION_PERFORMANCE)
        self.library.shaderc_compile_options_set_target_env(
            self.options, SHADERC_TARGET_ENV_VULKAN, SHADERC_ENV_VERSION_VULKAN_1_0)
        self.library.shaderc_compile_options_set_target_spirv(
            self.options, SHADERC_SPIRV_VERSION_1_0)

    def close(self) -> None:
        if getattr(self, "options", None):
            self.library.shaderc_compile_options_release(self.options)
            self.options = None
        if getattr(self, "compiler", None):
            self.library.shaderc_compiler_release(self.compiler)
            self.compiler = None

    def compile(self, payload: Payload) -> bytes:
        result = self.library.shaderc_compile_into_spv(
            self.compiler, payload.source, len(payload.source), STAGES[payload.stage],
            payload.source_name.encode("utf-8"), payload.entry_point.encode("utf-8"), self.options)
        if not result:
            raise RuntimeError(f"shaderc returned no result for {payload.source_name}")
        try:
            status = self.library.shaderc_result_get_compilation_status(result)
            if status != 0:
                message = self.library.shaderc_result_get_error_message(result)
                detail = message.decode("utf-8", errors="replace") if message else "unknown error"
                raise RuntimeError(f"shaderc failed for {payload.source_name}:\n{detail}")
            length = self.library.shaderc_result_get_length(result)
            address = self.library.shaderc_result_get_bytes(result)
            output = ctypes.string_at(address, length)
        finally:
            self.library.shaderc_result_release(result)
        if len(output) % 4 != 0 or struct.unpack_from("<I", output)[0] != SPIRV_MAGIC:
            raise RuntimeError(f"shaderc produced invalid SPIR-V for {payload.source_name}")
        return output

    def toolchain(self, path: Path) -> Toolchain:
        version = ctypes.c_uint()
        revision = ctypes.c_uint()
        self.library.shaderc_get_spv_version(ctypes.byref(version), ctypes.byref(revision))
        return Toolchain(path, sha256(path.read_bytes()), version.value, revision.value)


def raw_string(text: str) -> str:
    delimiter = "CNA_SHADER"
    if f"){delimiter}\"" in text:
        raise ValueError("shader source collides with the generated raw-string delimiter")
    return f'R"{delimiter}({text}){delimiter}"'


def spirv_array(symbol: str, output: bytes) -> list[str]:
    words = struct.unpack(f"<{len(output) // 4}I", output)
    lines = [f"inline constexpr std::uint32_t {symbol}[] = {{"]
    for start in range(0, len(words), 8):
        row = ", ".join(f"0x{word:08x}u" for word in words[start:start + 8])
        lines.append(f"    {row},")
    lines.extend(["};", f"inline constexpr std::size_t {symbol}ByteSize = sizeof({symbol});"])
    return lines


def generate(manifest: dict[str, Any], manifest_raw: bytes, payloads: list[Payload],
             library_path: Path | None) -> str:
    compiled: dict[str, bytes] = {}
    toolchain: Toolchain | None = None
    compiler: ShadercCompiler | None = None
    if any(payload.output_format == "spirv" for payload in payloads):
        assert library_path is not None
        compiler = ShadercCompiler(library_path)
        try:
            toolchain = compiler.toolchain(library_path)
            for payload in payloads:
                if payload.output_format == "spirv":
                    compiled[payload.symbol] = compiler.compile(payload)
        finally:
            compiler.close()

    guard = manifest.get("preprocessor_guard")
    lines = [
        "// SPDX-License-Identifier: MS-PL",
        "// Generated by tools/shader_package/generate_shader_package.py. Do not edit.",
        "#pragma once",
        "",
    ]
    if guard:
        lines.extend([f"#ifdef {guard}", ""])
    lines.extend([
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        f"namespace {manifest['namespace']} {{",
        "",
        "struct PayloadProvenance",
        "{",
        "    std::string_view symbol;",
        "    std::string_view source;",
        "    std::string_view sourceSha256;",
        "    std::string_view language;",
        "    std::string_view sourceLanguage;",
        "    std::string_view stage;",
        "    std::string_view format;",
        "    std::string_view entryPoint;",
        "};",
        "",
        f'inline constexpr std::string_view kPackageName = "{manifest["package"]}";',
        f'inline constexpr std::string_view kManifestSha256 = "{sha256(manifest_raw)}";',
    ])
    if toolchain:
        lines.extend([
            'inline constexpr std::string_view kCompiler = "shaderc shared library";',
            f'inline constexpr std::string_view kCompilerSoname = "{toolchain.path.name}";',
            f'inline constexpr std::string_view kCompilerSha256 = "{toolchain.sha256}";',
            f"inline constexpr std::uint32_t kCompilerSpirVVersion = 0x{toolchain.spirv_version:08x}u;",
            f"inline constexpr std::uint32_t kCompilerSpirVRevision = {toolchain.spirv_revision}u;",
            'inline constexpr std::string_view kCompilerTarget = "Vulkan 1.0 / SPIR-V 1.0";',
            'inline constexpr std::string_view kCompilerOptimization = "performance";',
        ])
    else:
        lines.extend([
            'inline constexpr std::string_view kCompiler = "none";',
            'inline constexpr std::string_view kCompilerSoname = "";',
            'inline constexpr std::string_view kCompilerSha256 = "";',
            "inline constexpr std::uint32_t kCompilerSpirVVersion = 0u;",
            "inline constexpr std::uint32_t kCompilerSpirVRevision = 0u;",
            'inline constexpr std::string_view kCompilerTarget = "";',
            'inline constexpr std::string_view kCompilerOptimization = "";',
        ])
    lines.append("")

    for payload in payloads:
        if payload.output_format == "text":
            lines.append(f"inline constexpr std::string_view {payload.symbol} =")
            lines.append(f"    {raw_string(payload.source.decode('utf-8'))};")
        else:
            lines.extend(spirv_array(payload.symbol, compiled[payload.symbol]))
        lines.append("")

    lines.append(f"inline constexpr std::array<PayloadProvenance, {len(payloads)}> kPayloads = {{{{")
    for payload in payloads:
        lines.append(
            "    {"
            f'"{payload.symbol}", "{payload.source_name}", "{sha256(payload.source)}", '
            f'"{payload.language}", "{payload.source_language}", "{payload.stage}", '
            f'"{payload.output_format}", "{payload.entry_point}"'
            "},")
    lines.extend(["}};", "", f"}} // namespace {manifest['namespace']}", ""])
    if guard:
        lines.extend([f"#endif // {guard}", ""])
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="shader package JSON manifest")
    parser.add_argument("--output", required=True, type=Path, help="generated C++ header")
    parser.add_argument("--check", action="store_true", help="compare without writing")
    parser.add_argument("--shaderc-library", help="path to libshaderc.so.1")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest, raw, payloads = load_manifest(args.manifest.resolve())
        library_path = (find_shaderc(args.shaderc_library)
                        if any(payload.output_format == "spirv" for payload in payloads) else None)
        generated = generate(manifest, raw, payloads, library_path)
        output = args.output.resolve()
        if args.check:
            if not output.is_file():
                print(f"shader package is missing: {output}", file=sys.stderr)
                return 1
            tracked = output.read_text(encoding="utf-8")
            if tracked != generated:
                print(f"shader package is stale: {output}", file=sys.stderr)
                print("regenerate with the compiler fingerprint shown in the new header; "
                      "a different fingerprint is an explicit toolchain-version change",
                      file=sys.stderr)
                return 1
            print(f"shader package is reproducible: {output}")
            return 0
        output.parent.mkdir(parents=True, exist_ok=True)
        if not output.is_file() or output.read_text(encoding="utf-8") != generated:
            output.write_text(generated, encoding="utf-8")
            print(f"generated {output}")
        else:
            print(f"unchanged {output}")
        return 0
    except ToolchainUnavailable as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return SKIP
    except (OSError, ValueError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
