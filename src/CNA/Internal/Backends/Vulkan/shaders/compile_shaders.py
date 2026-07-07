#!/usr/bin/env python3
"""Compile GLSL shaders to SPIR-V and emit a C++ header with embedded byte arrays.

Uses libshaderc.so.1 (already installed as libshaderc1 package) via ctypes.
Run from the shaders/ directory or pass --output <path>.

Output: spirv_shaders.hpp  (next to this script unless --output given)
"""

import ctypes
import os
import struct
import sys
import textwrap
from pathlib import Path

# ---------------------------------------------------------------------------
# Load libshaderc
# ---------------------------------------------------------------------------
_lib_candidates = [
    "libshaderc.so.1",
    "/usr/lib/x86_64-linux-gnu/libshaderc.so.1",
    "/usr/lib/x86_64-linux-gnu/libshaderc_shared.so.1",
]
_shaderc = None
for _candidate in _lib_candidates:
    try:
        _shaderc = ctypes.CDLL(_candidate)
        break
    except OSError:
        pass
if _shaderc is None:
    raise RuntimeError("libshaderc.so.1 not found — install libshaderc1 or libshaderc-dev")

# shaderc C API prototypes
_shaderc.shaderc_compiler_initialize.restype = ctypes.c_void_p
_shaderc.shaderc_compiler_initialize.argtypes = []

_shaderc.shaderc_compiler_release.restype = None
_shaderc.shaderc_compiler_release.argtypes = [ctypes.c_void_p]

_shaderc.shaderc_compile_options_initialize.restype = ctypes.c_void_p
_shaderc.shaderc_compile_options_initialize.argtypes = []

_shaderc.shaderc_compile_options_release.restype = None
_shaderc.shaderc_compile_options_release.argtypes = [ctypes.c_void_p]

_shaderc.shaderc_compile_options_set_optimization_level.restype = None
_shaderc.shaderc_compile_options_set_optimization_level.argtypes = [ctypes.c_void_p, ctypes.c_int]

_shaderc.shaderc_compile_into_spv.restype = ctypes.c_void_p
_shaderc.shaderc_compile_into_spv.argtypes = [
    ctypes.c_void_p,   # compiler
    ctypes.c_char_p,   # source_text
    ctypes.c_size_t,   # source_text_size
    ctypes.c_int,      # shader_kind
    ctypes.c_char_p,   # input_file_name
    ctypes.c_char_p,   # entry_point_name
    ctypes.c_void_p,   # options
]

_shaderc.shaderc_result_get_compilation_status.restype = ctypes.c_int
_shaderc.shaderc_result_get_compilation_status.argtypes = [ctypes.c_void_p]

_shaderc.shaderc_result_get_error_message.restype = ctypes.c_char_p
_shaderc.shaderc_result_get_error_message.argtypes = [ctypes.c_void_p]

_shaderc.shaderc_result_get_length.restype = ctypes.c_size_t
_shaderc.shaderc_result_get_length.argtypes = [ctypes.c_void_p]

_shaderc.shaderc_result_get_bytes.restype = ctypes.POINTER(ctypes.c_char)
_shaderc.shaderc_result_get_bytes.argtypes = [ctypes.c_void_p]

_shaderc.shaderc_result_release.restype = None
_shaderc.shaderc_result_release.argtypes = [ctypes.c_void_p]

# shaderc_shader_kind enum values
VERTEX_SHADER   = 0
FRAGMENT_SHADER = 1

# shaderc_optimization_level
OPT_PERFORMANCE = 2


def compile_glsl(source: str, kind: int, filename: str) -> bytes:
    compiler = _shaderc.shaderc_compiler_initialize()
    options  = _shaderc.shaderc_compile_options_initialize()
    _shaderc.shaderc_compile_options_set_optimization_level(options, OPT_PERFORMANCE)

    src_bytes = source.encode("utf-8")
    result = _shaderc.shaderc_compile_into_spv(
        compiler,
        src_bytes, len(src_bytes),
        kind,
        filename.encode("utf-8"),
        b"main",
        options,
    )

    status = _shaderc.shaderc_result_get_compilation_status(result)
    if status != 0:
        err = _shaderc.shaderc_result_get_error_message(result).decode("utf-8")
        _shaderc.shaderc_result_release(result)
        _shaderc.shaderc_compile_options_release(options)
        _shaderc.shaderc_compiler_release(compiler)
        raise RuntimeError(f"Shader compilation failed ({filename}):\n{err}")

    length = _shaderc.shaderc_result_get_length(result)
    raw    = _shaderc.shaderc_result_get_bytes(result)
    spv    = bytes(raw[:length])

    _shaderc.shaderc_result_release(result)
    _shaderc.shaderc_compile_options_release(options)
    _shaderc.shaderc_compiler_release(compiler)
    return spv


def spv_to_cpp_array(name: str, spv: bytes) -> str:
    # SPIR-V is an array of uint32_t words
    assert len(spv) % 4 == 0, "SPIR-V size not a multiple of 4"
    words = struct.unpack(f"<{len(spv)//4}I", spv)
    hex_words = [f"0x{w:08x}u" for w in words]
    rows = [", ".join(hex_words[i:i+8]) for i in range(0, len(hex_words), 8)]
    body = ",\n    ".join(rows)
    return (
        f"static constexpr uint32_t {name}[] = {{\n"
        f"    {body}\n"
        f"}};\n"
        f"static constexpr size_t {name}_size = sizeof({name});\n"
    )


def main():
    script_dir = Path(__file__).parent

    shaders = [
        ("sprite2d.vert.glsl",          VERTEX_SHADER,   "kSprite2dVertSpv"),
        ("sprite2d.frag.glsl",          FRAGMENT_SHADER, "kSprite2dFragSpv"),
        ("colored3d.vert.glsl",         VERTEX_SHADER,   "kColored3dVertSpv"),
        ("colored3d.frag.glsl",         FRAGMENT_SHADER, "kColored3dFragSpv"),
        # Legacy no-GpuDrawParams DrawColoredPrimitives()/DrawIndexedColoredPrimitives() path
        # (Task 899: colored3d.vert.glsl above now needs a fog UBO binding that
        # pipelineLayout3D_'s zero-descriptor-set layout doesn't provide).
        ("colored3d_legacy.vert.glsl",  VERTEX_SHADER,   "kColored3dLegacyVertSpv"),
        # Textured 3D pipeline — stride 20 (VertexPositionTexture)
        ("textured3d.vert.glsl",         VERTEX_SHADER,   "kTextured3dVertSpv"),
        ("textured3d.frag.glsl",         FRAGMENT_SHADER, "kTextured3dFragSpv"),
        # Colored+Textured 3D pipeline — stride 24 (VertexPositionColorTexture)
        ("colored_textured3d.vert.glsl", VERTEX_SHADER,   "kColoredTextured3dVertSpv"),
        ("colored_textured3d.frag.glsl", FRAGMENT_SHADER, "kColoredTextured3dFragSpv"),
        # Lit+Textured 3D pipeline — stride 32 (VertexPositionNormalTexture)
        ("lit_textured3d.vert.glsl",     VERTEX_SHADER,   "kLitTextured3dVertSpv"),
        ("lit_textured3d.frag.glsl",     FRAGMENT_SHADER, "kLitTextured3dFragSpv"),
        # AlphaTestEffect pipeline — single VS handles stride 20/32 via attribute remapping
        ("alpha_test3d.vert.glsl",       VERTEX_SHADER,   "kAlphaTest3dVertSpv"),
        ("alpha_test3d.frag.glsl",       FRAGMENT_SHADER, "kAlphaTest3dFragSpv"),
        # AlphaTestEffect stride-24 (VertexPositionColorTexture) variant with VertexColorEnabled
        # support (Task 887); shares alpha_test3d's FS.
        ("alpha_test_colored3d.vert.glsl", VERTEX_SHADER, "kAlphaTestColored3dVertSpv"),
        # DualTextureEffect pipeline — dedicated VS (Task 899: previously reused textured3d's,
        # but that now has its own fog UBO binding conflicting with dual_texture3d's 2-sampler
        # descriptor set layout); FS samples two texture units
        ("dual_texture3d.vert.glsl",     VERTEX_SHADER,   "kDualTexture3dVertSpv"),
        ("dual_texture3d.frag.glsl",     FRAGMENT_SHADER, "kDualTexture3dFragSpv"),
        # EnvironmentMapEffect pipeline — stride 32, world matrix in PC, UBO for FS params
        ("env_map3d.vert.glsl",          VERTEX_SHADER,   "kEnvMap3dVertSpv"),
        ("env_map3d.frag.glsl",          FRAGMENT_SHADER, "kEnvMap3dFragSpv"),
        # SkinnedEffect pipeline — stride 52, bone palette in dynamic UBO (binding=1)
        ("skinned3d.vert.glsl",          VERTEX_SHADER,   "kSkinned3dVertSpv"),
        ("skinned3d.frag.glsl",          FRAGMENT_SHADER, "kSkinned3dFragSpv"),
        # Instanced 3D pipeline — binding=0 per-vertex (pos only), binding=1 per-instance mat4.
        # Dedicated FS (Task 899: previously reused colored3d's, but that now has a 2nd
        # descriptor binding for fog, incompatible with Instanced3D's unmodified 1-binding layout).
        ("instanced3d.vert.glsl",        VERTEX_SHADER,   "kInstanced3dVertSpv"),
        ("instanced3d.frag.glsl",        FRAGMENT_SHADER, "kInstanced3dFragSpv"),
    ]

    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[1] == "--output" else \
                  script_dir / "spirv_shaders.hpp"

    parts = [
        "// AUTO-GENERATED by compile_shaders.py — do not edit by hand.\n"
        "// Re-run compile_shaders.py to regenerate after shader changes.\n"
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n\n"
        "namespace CNA::Internal::Backends::Vulkan::Shaders {\n\n"
    ]

    for filename, kind, cname in shaders:
        glsl_path = script_dir / filename
        if not glsl_path.exists():
            print(f"ERROR: {glsl_path} not found", file=sys.stderr)
            sys.exit(1)
        source = glsl_path.read_text()
        print(f"Compiling {filename} ...", end=" ", flush=True)
        spv = compile_glsl(source, kind, filename)
        print(f"OK ({len(spv)} bytes, {len(spv)//4} words)")
        parts.append(spv_to_cpp_array(cname, spv) + "\n")

    parts.append("} // namespace CNA::Internal::Backends::Vulkan::Shaders\n")

    output_path.write_text("".join(parts))
    print(f"Written: {output_path}")


if __name__ == "__main__":
    main()
