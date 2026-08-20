#!/usr/bin/env python3
"""Compile GLSL shaders to SPIR-V and emit a C++ header with embedded byte arrays.

Uses libshaderc.so.1 (already installed as libshaderc1 package) via ctypes.
Run from the shaders/ directory or pass --output <path>.

Output: spirv_shaders.hpp  (next to this script unless --output given)

Note: these shaders follow SDL_gpu's SPIR-V resource-binding convention for graphics
pipelines (SDL_gpu.h, SDL_CreateGPUShader doc comment), NOT the plain Vulkan convention
this project's VulkanRenderer shaders use -- vertex-stage textures/storage are set 0,
vertex-stage uniform buffers are set 1, fragment-stage textures/storage are set 2, and
fragment-stage uniform buffers are set 3. Do not copy a VulkanRenderer shader's
compiled SPIR-V here without re-checking its set numbers against this convention.
"""

import ctypes
import struct
import sys
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
        ("sprite2d.vert.glsl", VERTEX_SHADER,   "kSprite2dVertSpv"),
        ("sprite2d.frag.glsl", FRAGMENT_SHADER, "kSprite2dFragSpv"),
        ("colored3d.vert.glsl", VERTEX_SHADER,   "kColored3dVertSpv"),
        ("colored3d.frag.glsl", FRAGMENT_SHADER, "kColored3dFragSpv"),
        ("textured3d.vert.glsl", VERTEX_SHADER,   "kTextured3dVertSpv"),
        ("textured3d.frag.glsl", FRAGMENT_SHADER, "kTextured3dFragSpv"),
        ("colored_textured3d.vert.glsl", VERTEX_SHADER, "kColoredTextured3dVertSpv"),
        # colored_textured3d shares textured3d's fragment shader (functionally identical).
        ("lit_textured3d.vert.glsl", VERTEX_SHADER,   "kLitTextured3dVertSpv"),
        ("lit_textured3d.frag.glsl", FRAGMENT_SHADER, "kLitTextured3dFragSpv"),
        ("alpha_test3d.vert.glsl", VERTEX_SHADER,   "kAlphaTest3dVertSpv"),
        ("alpha_test_colored3d.vert.glsl", VERTEX_SHADER, "kAlphaTestColored3dVertSpv"),
        ("alpha_test3d.frag.glsl", FRAGMENT_SHADER, "kAlphaTest3dFragSpv"),
        ("dual_texture3d.vert.glsl", VERTEX_SHADER,   "kDualTexture3dVertSpv"),
        ("dual_texture_colored3d.vert.glsl", VERTEX_SHADER, "kDualTextureColored3dVertSpv"),
        ("dual_texture3d.frag.glsl", FRAGMENT_SHADER, "kDualTexture3dFragSpv"),
        ("env_map3d.vert.glsl", VERTEX_SHADER,   "kEnvMap3dVertSpv"),
        ("env_map3d.frag.glsl", FRAGMENT_SHADER, "kEnvMap3dFragSpv"),
        ("skinned3d.vert.glsl", VERTEX_SHADER,   "kSkinned3dVertSpv"),
        # skinned3d's fragment stage reuses lit_textured3d.frag.glsl unchanged (byte-identical
        # varying interface and UBO layout) -- no separate skinned3d.frag.glsl needed.
        ("skinned_colored3d.vert.glsl", VERTEX_SHADER,   "kSkinnedColored3dVertSpv"),
        ("skinned_colored3d.frag.glsl", FRAGMENT_SHADER, "kSkinnedColored3dFragSpv"),
        ("pbr3d.vert.glsl", VERTEX_SHADER,   "kPbr3dVertSpv"),
        # plans/plan_gltf.md GLTF-462/GLTF-465: stride 60's colour-carrying twin of the line above.
        ("pbr3d.vert.glsl", VERTEX_SHADER,   "kPbr3dColorVertSpv", "CNA_PBR_VERTEX_COLOR"),
        ("pbr3d.frag.glsl", FRAGMENT_SHADER, "kPbr3dFragSpv"),
        ("pbr_skinned3d.vert.glsl", VERTEX_SHADER, "kPbrSkinned3dVertSpv"),
        # plans/plan_gltf.md GLTF-463: stride 80's colour-carrying twin.
        ("pbr_skinned3d.vert.glsl", VERTEX_SHADER, "kPbrSkinned3dColorVertSpv", "CNA_PBR_VERTEX_COLOR"),
        # pbr_skinned3d's fragment stage reuses pbr3d.frag.glsl unchanged (byte-identical varying
        # interface and UBO layout) -- no separate pbr_skinned3d.frag.glsl needed.
    ]

    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[1] == "--output" else \
                  script_dir / "spirv_shaders.hpp"

    parts = [
        "// AUTO-GENERATED by compile_shaders.py — do not edit by hand.\n"
        "// Re-run compile_shaders.py to regenerate after shader changes.\n"
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n\n"
        "namespace CNA::Internal::Renderers::SdlGpu::Shaders {\n\n"
    ]

    for filename, kind, cname, *defines in shaders:
        glsl_path = script_dir / filename
        if not glsl_path.exists():
            print(f"ERROR: {glsl_path} not found", file=sys.stderr)
            sys.exit(1)
        source = glsl_path.read_text()
        if defines:
            # plans/plan_gltf.md GLTF-465: variant defines are inserted after GLSL's mandatory #version
            # line, the same way the Vulkan renderer's own compile_shaders.py does it.
            version_end = source.index("\n", source.index("#version")) + 1
            source = (source[:version_end]
                      + "".join(f"#define {name} 1\n" for name in defines)
                      + source[version_end:])
        print(f"Compiling {filename} ...", end=" ", flush=True)
        spv = compile_glsl(source, kind, filename)
        print(f"OK ({len(spv)} bytes, {len(spv)//4} words)")
        parts.append(spv_to_cpp_array(cname, spv) + "\n")

    parts.append("} // namespace CNA::Internal::Renderers::SdlGpu::Shaders\n")

    output_path.write_text("".join(parts))
    print(f"Written: {output_path}")


if __name__ == "__main__":
    main()
