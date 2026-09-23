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
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SKIP = 77
STAGES = {"vertex": 0, "fragment": 1, "compute": 2}
FORMATS = {"text", "spirv", "wgsl"}
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
NAMESPACE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*$")
SPIRV_MAGIC = 0x07230203
SHADERC_TARGET_ENV_VULKAN = 0
SHADERC_ENV_VERSION_VULKAN_1_0 = 1 << 22
SHADERC_SPIRV_VERSION_1_0 = 0x010000
SHADERC_OPTIMIZATION_ZERO = 0
SHADERC_OPTIMIZATION_PERFORMANCE = 2
# plans/plan_vulkan_modern_graphics.md VMG-0012: "performance" (the default, and what every package
# before this one uses) drops OpName/OpMemberName, and Vulkan's ComputeShader::setUniform binds a
# scalar by its push-constant member NAME -- so a package that uses named scalar uniforms says
# "zero", which keeps the names. Recorded in the header like every other compiler option.
OPTIMIZATION_LEVELS = {"performance": SHADERC_OPTIMIZATION_PERFORMANCE, "zero": SHADERC_OPTIMIZATION_ZERO}


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
    optimization = manifest.get("optimization", "performance")
    if optimization not in OPTIMIZATION_LEVELS:
        raise ValueError("manifest 'optimization' must be one of: " + ", ".join(sorted(OPTIMIZATION_LEVELS)))
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
        if output_format == "wgsl" and language != "wgsl":
            raise ValueError(f"payload '{symbol}' must declare language 'wgsl'")
        if output_format == "wgsl" and source_language != "vulkan-glsl":
            raise ValueError(f"payload '{symbol}' must translate declared 'vulkan-glsl' source")
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
    def __init__(self, path: Path, optimization: str = "performance"):
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
            self.options, OPTIMIZATION_LEVELS[optimization])
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


# ---- WGSL payloads (plans/plan_webgpu_modern_graphics.md WMG-0005) --------------------------------
#
# A "wgsl" payload is derived from the SAME Vulkan GLSL source as the package's SPIR-V payload, so
# the Vulkan and WebGPU programs cannot drift apart: the source is rewritten into the WebGPU binding
# contract (below), compiled by shaderc exactly like a SPIR-V payload, and translated to WGSL by
# naga. Every rule here is a mechanical rewrite of a declaration, never of shader logic.
#
# The WebGPU binding contract the rewrite targets (docs/webgpu-renderer.md, "Modern shader payloads"):
#   * the push-constant block becomes `@group(3) @binding(0) var<uniform>` -- WebGPU has no push
#     constants (wgpu-native's immediate data is a native-only extension, and browsers have none);
#   * every combined `sampler*` at (set S, binding B) becomes a texture at (S, B) and a sampler at
#     (S, B + 32) -- WGSL has no combined image sampler;
#   * a std140 block holding only arrays of 4- or 8-byte elements becomes a std430 read-only storage
#     block -- WGSL's uniform address space cannot express std140's 16-byte stride for those, and
#     naga would otherwise write an `array<f32, N>` the WebGPU validator rejects;
#   * `writeonly buffer` becomes `buffer` -- WGSL storage buffers are read or read_write.
# Clip space needs no rule: naga's SPIR-V frontend negates the vertex position's y (Vulkan's clip
# space is y-down, WebGPU's y-up), which makes every generated program produce the framebuffer image
# its Vulkan twin produces.
WEBGPU_TRANSFORM = "cna-webgpu-glsl/1"
WEBGPU_SCALAR_BLOCK = (3, 0)
WEBGPU_SAMPLER_BINDING_OFFSET = 32
_COMBINED_SAMPLERS = {
    "sampler2D": ("texture2D", "sampler"),
    "samplerCube": ("textureCube", "sampler"),
    "sampler3D": ("texture3D", "sampler"),
    "sampler2DArray": ("texture2DArray", "sampler"),
    "sampler2DShadow": ("texture2D", "samplerShadow"),
    "isampler2D": ("itexture2D", "sampler"),
    "usampler2D": ("utexture2D", "sampler"),
}
_SAMPLER_DECL = re.compile(
    r"layout\s*\(\s*set\s*=\s*(\d+)\s*,\s*binding\s*=\s*(\d+)\s*\)\s*uniform\s+"
    r"(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+(\w+)\s*;")
_PUSH_CONSTANT = re.compile(r"layout\s*\(\s*push_constant\s*\)\s*uniform")
_WRITEONLY_BUFFER = re.compile(r"\bwriteonly(\s+buffer\b)")
_STD140_BLOCK = re.compile(
    r"layout\s*\(\s*(set\s*=\s*\d+\s*,\s*binding\s*=\s*\d+)\s*,\s*std140\s*\)\s*uniform\s+(\w+)\s*\{([^}]*)\}")
_NARROW_ARRAY_MEMBER = re.compile(
    r"^\s*(?:(?:highp|mediump|lowp)\s+)?(float|int|uint|bool|vec2|ivec2|uvec2)\s+\w+\s*\[\s*\d+\s*\]\s*$")


def webgpu_transform(source: str, name: str) -> str:
    """Rewrites Vulkan GLSL into the WebGPU binding contract described above."""
    lines = source.split("\n")
    if not lines or not lines[0].startswith("#version"):
        raise ValueError(f"{name}: a wgsl payload's Vulkan GLSL must start with #version")
    body = "\n".join(lines[1:])
    body = _PUSH_CONSTANT.sub(
        f"layout(set = {WEBGPU_SCALAR_BLOCK[0]}, binding = {WEBGPU_SCALAR_BLOCK[1]}, std140) uniform",
        body)
    body = _WRITEONLY_BUFFER.sub(r"\1", body)

    def narrow_block(match: re.Match) -> str:
        members = [m for m in match.group(3).split(";") if m.strip()]
        narrow = [bool(_NARROW_ARRAY_MEMBER.match(m)) for m in members]
        if not any(narrow):
            return match.group(0)
        if not all(narrow):
            raise ValueError(
                f"{name}: std140 block '{match.group(2)}' mixes a 4/8-byte-element array with other "
                "members; WGSL's uniform address space cannot express its 16-byte array stride")
        return (f"layout({match.group(1)}, std430) readonly buffer {match.group(2)} "
                f"{{{match.group(3)}}}")

    body = _STD140_BLOCK.sub(narrow_block, body)

    defines: list[str] = []

    def split_sampler(match: re.Match) -> str:
        group, binding, kind, var = match.group(1), int(match.group(2)), match.group(3), match.group(4)
        if kind not in _COMBINED_SAMPLERS:
            return match.group(0)
        texture, sampler = _COMBINED_SAMPLERS[kind]
        defines.append(f"#define {var} {kind}({var}_cnaTexture, {var}_cnaSampler)")
        return (f"layout(set = {group}, binding = {binding}) uniform {texture} {var}_cnaTexture;\n"
                f"layout(set = {group}, binding = {binding + WEBGPU_SAMPLER_BINDING_OFFSET}) "
                f"uniform {sampler} {var}_cnaSampler;")

    body = _SAMPLER_DECL.sub(split_sampler, body)
    text = lines[0] + "\n" + body
    if defines:
        # The macros must follow the declarations they name and precede every use, so they go
        # directly after the last rewritten declaration.
        at = text.rfind("_cnaSampler;")
        at = text.find("\n", at) + 1
        text = text[:at] + "\n".join(defines) + "\n" + text[at:]
    return text


def find_naga(explicit: str | None) -> Path:
    requested = explicit or os.environ.get("CNA_NAGA")
    if requested:
        path = Path(requested).expanduser().resolve()
        if not path.is_file():
            raise ToolchainUnavailable(f"naga does not exist: {path}")
        return path
    found = shutil.which("naga")
    if found:
        return Path(found).resolve()
    raise ToolchainUnavailable("naga (naga-cli) was not found; put it on PATH or set CNA_NAGA")


@dataclass(frozen=True)
class NagaToolchain:
    path: Path
    sha256: str
    version: str


def naga_toolchain(path: Path) -> NagaToolchain:
    result = subprocess.run([str(path), "--version"], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise ToolchainUnavailable(f"{path} --version failed: {result.stderr.strip()}")
    return NagaToolchain(path, sha256(path.read_bytes()), result.stdout.strip())


# SPIR-V block layouts, compared between the shaderc module and naga's re-emission of the WGSL. The
# WGSL is correct exactly when every uniform/storage block has the same member offsets, array
# strides and matrix strides in both -- which is what makes the WebGPU renderer's byte uploads mean
# what the Vulkan renderer's mean.
_OP_CONSTANT, _OP_TYPE_INT, _OP_TYPE_FLOAT, _OP_TYPE_VECTOR, _OP_TYPE_MATRIX = 43, 21, 22, 23, 24
_OP_TYPE_ARRAY, _OP_TYPE_RUNTIME_ARRAY, _OP_TYPE_STRUCT, _OP_TYPE_POINTER = 28, 29, 30, 32
_OP_VARIABLE, _OP_DECORATE, _OP_MEMBER_DECORATE, _OP_TYPE_BOOL = 59, 71, 72, 20
_DEC_ARRAY_STRIDE, _DEC_MATRIX_STRIDE, _DEC_BINDING, _DEC_SET, _DEC_OFFSET = 6, 7, 33, 34, 35
_SC_UNIFORM, _SC_STORAGE_BUFFER = 2, 12


def spirv_block_layouts(binary: bytes) -> dict[tuple[int, int], Any]:
    words = struct.unpack(f"<{len(binary) // 4}I", binary)
    types: dict[int, tuple] = {}
    constants: dict[int, int] = {}
    decorations: dict[int, dict[int, int]] = {}
    member_decorations: dict[tuple[int, int], dict[int, int]] = {}
    variables: list[tuple[int, int, int]] = []
    pointers: dict[int, int] = {}
    at = 5
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 0xFFFF
        operands = words[at + 1:at + count]
        if count == 0:
            raise ValueError("malformed SPIR-V")
        if opcode == _OP_TYPE_INT:
            types[operands[0]] = ("int", operands[1], operands[2])
        elif opcode == _OP_TYPE_FLOAT:
            types[operands[0]] = ("float", operands[1])
        elif opcode == _OP_TYPE_BOOL:
            types[operands[0]] = ("bool",)
        elif opcode == _OP_TYPE_VECTOR:
            types[operands[0]] = ("vec", operands[1], operands[2])
        elif opcode == _OP_TYPE_MATRIX:
            types[operands[0]] = ("mat", operands[1], operands[2])
        elif opcode == _OP_TYPE_ARRAY:
            types[operands[0]] = ("array", operands[1], operands[2])
        elif opcode == _OP_TYPE_RUNTIME_ARRAY:
            types[operands[0]] = ("rtarray", operands[1])
        elif opcode == _OP_TYPE_STRUCT:
            types[operands[0]] = ("struct",) + tuple(operands[1:])
        elif opcode == _OP_TYPE_POINTER:
            pointers[operands[0]] = operands[2]
        elif opcode == _OP_CONSTANT:
            constants[operands[1]] = operands[2]
        elif opcode == _OP_VARIABLE:
            variables.append((operands[0], operands[1], operands[2]))
        elif opcode == _OP_DECORATE and len(operands) >= 3:
            decorations.setdefault(operands[0], {})[operands[1]] = operands[2]
        elif opcode == _OP_MEMBER_DECORATE and len(operands) >= 4:
            member_decorations.setdefault((operands[0], operands[1]), {})[operands[2]] = operands[3]
        at += count

    def describe(type_id: int) -> Any:
        t = types[type_id]
        if t[0] in ("int", "float", "bool"):
            return t
        if t[0] == "vec":
            return ("vec", describe(t[1]), t[2])
        if t[0] == "mat":
            return ("mat", describe(t[1]), t[2])
        if t[0] == "array":
            return ("array", describe(t[1]), constants.get(t[2]),
                    decorations.get(type_id, {}).get(_DEC_ARRAY_STRIDE))
        if t[0] == "rtarray":
            return ("rtarray", describe(t[1]), decorations.get(type_id, {}).get(_DEC_ARRAY_STRIDE))
        members = []
        for index, member in enumerate(t[1:]):
            d = member_decorations.get((type_id, index), {})
            members.append((d.get(_DEC_OFFSET), d.get(_DEC_MATRIX_STRIDE), describe(member)))
        return ("struct", tuple(members))

    blocks: dict[tuple[int, int], Any] = {}
    for pointer_type, variable, storage_class in variables:
        if storage_class not in (_SC_UNIFORM, _SC_STORAGE_BUFFER):
            continue
        d = decorations.get(variable, {})
        key = (d.get(_DEC_SET, 0), d.get(_DEC_BINDING, 0))
        layout = describe(pointers[pointer_type])
        # naga wraps a block whose type it cannot decorate in place; compare the payload.
        while (layout[0] == "struct" and len(layout[1]) == 1 and layout[1][0][0] == 0
               and layout[1][0][2][0] == "struct"):
            layout = layout[1][0][2]
        blocks[key] = layout
    return blocks


def translate_to_wgsl(compiler: ShadercCompiler, naga: NagaToolchain, payload: Payload) -> str:
    source = webgpu_transform(payload.source.decode("utf-8"), payload.source_name)
    transformed = Payload(payload.symbol, payload.source_path, payload.source_name,
                          source.encode("utf-8"), "spirv", "vulkan-glsl", payload.stage, "spirv",
                          payload.entry_point)
    spirv = compiler.compile(transformed)
    with tempfile.TemporaryDirectory(prefix="cna-wgsl-") as scratch:
        spv_path = Path(scratch) / "in.spv"
        wgsl_path = Path(scratch) / "out.wgsl"
        back_path = Path(scratch) / "back.spv"
        spv_path.write_bytes(spirv)
        result = subprocess.run([str(naga.path), str(spv_path), str(wgsl_path)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"naga failed for {payload.source_name}:\n{result.stderr.strip()}")
        wgsl = wgsl_path.read_text(encoding="utf-8")
        result = subprocess.run([str(naga.path), str(wgsl_path), str(back_path)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(
                f"naga cannot re-read its own WGSL for {payload.source_name}:\n{result.stderr.strip()}")
        expected = spirv_block_layouts(spirv)
        actual = spirv_block_layouts(back_path.read_bytes())
    for key, layout in expected.items():
        if actual.get(key) != layout:
            raise RuntimeError(
                f"{payload.source_name}: the WGSL block at group {key[0]} binding {key[1]} does not "
                f"have the byte layout of its Vulkan GLSL source\n  expected {layout}\n"
                f"  actual   {actual.get(key)}")
    header = (f"// {payload.source_name} -> {WEBGPU_TRANSFORM} -> shaderc -> naga. "
              "Generated; edit the Vulkan GLSL source.\n")
    return header + wgsl


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
             library_path: Path | None, naga_path: Path | None = None) -> str:
    compiled: dict[str, bytes] = {}
    translated: dict[str, str] = {}
    toolchain: Toolchain | None = None
    naga: NagaToolchain | None = None
    compiler: ShadercCompiler | None = None
    if any(payload.output_format == "spirv" for payload in payloads):
        assert library_path is not None
        compiler = ShadercCompiler(library_path, manifest.get("optimization", "performance"))
        try:
            toolchain = compiler.toolchain(library_path)
            for payload in payloads:
                if payload.output_format == "spirv":
                    compiled[payload.symbol] = compiler.compile(payload)
        finally:
            compiler.close()
    if any(payload.output_format == "wgsl" for payload in payloads):
        assert library_path is not None and naga_path is not None
        naga = naga_toolchain(naga_path)
        # Always unoptimized: WGSL keeps every block member's name, which the WebGPU renderer's
        # name-based scalar uniforms (ComputeShader::setUniform) need, and it stays readable.
        compiler = ShadercCompiler(library_path, "zero")
        try:
            if toolchain is None:
                toolchain = compiler.toolchain(library_path)
            for payload in payloads:
                if payload.output_format == "wgsl":
                    translated[payload.symbol] = translate_to_wgsl(compiler, naga, payload)
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
            f'inline constexpr std::string_view kCompilerOptimization = "{manifest.get("optimization", "performance")}";',
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
    if naga:
        lines.extend([
            'inline constexpr std::string_view kWgslTranslator = "naga-cli";',
            f'inline constexpr std::string_view kWgslTranslatorVersion = "{naga.version}";',
            f'inline constexpr std::string_view kWgslTranslatorSha256 = "{naga.sha256}";',
            f'inline constexpr std::string_view kWgslSourceTransform = "{WEBGPU_TRANSFORM}";',
        ])
    lines.append("")

    for payload in payloads:
        if payload.output_format == "text":
            lines.append(f"inline constexpr std::string_view {payload.symbol} =")
            lines.append(f"    {raw_string(payload.source.decode('utf-8'))};")
        elif payload.output_format == "wgsl":
            lines.append(f"inline constexpr std::string_view {payload.symbol} =")
            lines.append(f"    {raw_string(translated[payload.symbol])};")
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
    parser.add_argument("--naga", help="path to the naga (naga-cli) executable for wgsl payloads")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest, raw, payloads = load_manifest(args.manifest.resolve())
        library_path = (find_shaderc(args.shaderc_library)
                        if any(payload.output_format in ("spirv", "wgsl") for payload in payloads)
                        else None)
        naga_path = (find_naga(args.naga)
                     if any(payload.output_format == "wgsl" for payload in payloads) else None)
        generated = generate(manifest, raw, payloads, library_path, naga_path)
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
