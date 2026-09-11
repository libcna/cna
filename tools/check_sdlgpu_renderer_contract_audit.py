#!/usr/bin/env python3
"""Generate and validate the EasyGL/SDL_GPU renderer-contract hook inventory."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shlex
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INTERFACE_HEADER = ROOT / "modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
SDL_HEADER = ROOT / "modules/renderers/sdl-gpu/include/CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
EASYGL_HEADER = ROOT / "modules/renderers/easygl/include/CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
MANIFEST = ROOT / "plans/sdlgpu_renderer_contract_audit.csv"

INTERFACE_IMPLEMENTATIONS = {
    "IVertexBufferRenderer": (
        (("buffer", "EasyGLVertexBufferRenderer"),),
        (("buffer", "SdlGpuVertexBufferRenderer"),),
    ),
    "IIndexBufferRenderer": (
        (("buffer", "EasyGLIndexBufferRenderer"),),
        (("buffer", "SdlGpuIndexBufferRenderer"),),
    ),
    "IGpuTimerRenderer": (
        (("timer", "EasyGLGpuTimerRenderer"),),
        (),
    ),
    "IOcclusionQueryRenderer": (
        (("query", "EasyGLOcclusionQueryRenderer"),),
        (),
    ),
    "IStorageBufferRenderer": (
        (("storage", "EasyGLStorageBufferRenderer"),),
        (),
    ),
    "IComputeShaderRenderer": (
        (("compute", "EasyGLComputeShaderRenderer"),),
        (),
    ),
    "ITextureCubeRenderer": (
        (
            ("TextureCube", "EasyGLTextureCubeRenderer"),
            ("RenderTargetCube", "EasyGLRenderTargetCubeRenderer"),
        ),
        (
            ("TextureCube", "SdlGpuTextureCubeRenderer"),
            ("RenderTargetCube", "SdlGpuRenderTargetCubeRenderer"),
        ),
    ),
    "ITexture3DRenderer": (
        (("Texture3D", "EasyGLTexture3DRenderer"),),
        (("Texture3D", "SdlGpuTexture3DRenderer"),),
    ),
    "ITexture2DArrayRenderer": (
        (),
        (),
    ),
    "IStorageTexture2DRenderer": (
        (),
        (),
    ),
    "ITextureRenderer": (
        (
            ("Texture2D", "EasyGLTextureRenderer"),
            ("RenderTarget2D", "EasyGLRenderTargetRenderer"),
        ),
        (
            ("Texture2D", "SdlGpuTextureRenderer"),
            ("RenderTarget2D", "SdlGpuRenderTargetRenderer"),
        ),
    ),
    "IRenderTargetRenderer": (
        (("RenderTarget2D", "EasyGLRenderTargetRenderer"),),
        (("RenderTarget2D", "SdlGpuRenderTargetRenderer"),),
    ),
    "IRenderTargetCubeRenderer": (
        (("RenderTargetCube", "EasyGLRenderTargetCubeRenderer"),),
        (("RenderTargetCube", "SdlGpuRenderTargetCubeRenderer"),),
    ),
    "IEffectRenderer": (
        (("ShaderEffect", "EasyGLEffectRenderer"),),
        (("ShaderEffect", "SdlGpuEffectRenderer"),),
    ),
    "ISpriteBatchRenderer": (
        (("SpriteBatch", "EasyGLSpriteBatchRenderer"),),
        (("SpriteBatch", "SdlGpuSpriteBatchRenderer"),),
    ),
    "IRendererThreadContextLease": (
        (("lease", "EasyGLThreadContextLease"),),
        (),
    ),
    "IGraphicsRenderer": (
        (("renderer", "EasyGLRenderer"),),
        (("renderer", "SdlGpuRenderer"),),
    ),
}

OUT_INTERFACES = {
    "IGpuTimerRenderer",
    "IStorageBufferRenderer",
    "ITexture2DArrayRenderer",
    "IStorageTexture2DRenderer",
    "IComputeShaderRenderer",
    "IEffectRenderer",
}

OUT_GRAPHICS_METHODS = {
    "GetShaderDialectEXT",
    "CreateEffectRenderer",
    "CreateComputeShader",
    "CreateStorageBuffer",
    "CreateStorageBufferEXT",
    "CreateTexture2DArrayEXT",
    "CreateStorageTexture2DEXT",
    "DispatchCompute",
    "MemoryBarrierEXT",
    "ExecutesShaderEffectSourceEXT",
    "SupportsShadowSamplingEXT",
    "SupportsImageBasedLightingEXT",
    "SupportsComputeShadersEXT",
    "SupportsIndirectDrawEXT",
    "SupportsBaseInstanceDrawingEXT",
    "SupportsComputeImageBindingEXT",
    "SupportsTexture3DSamplingEXT",
    "SupportsShaderLanguageEXT",
    "GetSurfaceFormatUsageSupportEXT",
    "GetDisplayColorSpaceEXT",
    "SetDisplayColorSpaceEXT",
    "GetMaxVertexShaderStorageBlocksEXT",
    "GetMaxStorageBufferBytesEXT",
    "GetMaxUniformBufferBytesEXT",
    "GetMaxComputeStorageBufferBindingsEXT",
    "GetMaxTextureArrayLayersEXT",
    "GetMaxSampledTexturesPerShaderStageEXT",
    "GetMaxStorageImagesPerShaderStageEXT",
    "GetMaxVertexInputBindingsEXT",
    "GetMaxVertexInputAttributesEXT",
    "GetMaxColorAttachmentsEXT",
    "GetMinStorageBufferOffsetAlignmentEXT",
    "GetMinUniformBufferOffsetAlignmentEXT",
    "GetTimestampPeriodPicosecondsEXT",
    "BindStorageBufferForDrawEXT",
    "SupportsGpuTimerEXT",
    "CreateGpuTimerEXT",
    "GetMaxComputeWorkGroupCountEXT",
    "GetMaxComputeWorkGroupSizeEXT",
    "GetMaxComputeWorkGroupInvocationsEXT",
    "DrawPrimitivesIndirectEXT",
    "DrawIndexedPrimitivesIndirectEXT",
    "SetContextRecoveryEnabled",
    "SetUnsupported3DGraphicsCallBehavior",
    "GetUnsupported3DGraphicsCallBehavior",
    "SupportsCapability",
    "GetAdditionalLimitationsTextEXT",
    "SupportsHalfFloatTextureLinearFilteringEXT",
    "SetStringMarkerEXT",
    "DebugSimulateContextLoss",
    "DebugRestoreContext",
}

OUT_SPRITE_METHODS = {"DrawMeshEXT"}

OUT_INTERFACE_METHODS = {
    "IOcclusionQueryRenderer": {"PixelCountIsPreciseEXT"},
    "ITextureCubeRenderer": {"BindGL", "GetSizeEXT", "GetSurfaceFormatEXT"},
    "ITexture3DRenderer": {"BindGL", "GetDimensionsEXT", "GetSurfaceFormatEXT"},
    "ITextureRenderer": {"BindGL"},
    "IRenderTargetRenderer": {"GetColorGLHandle"},
    "IRenderTargetCubeRenderer": {"GetGLHandle"},
}

OUT_METHOD_EVIDENCE = {
    ("IOcclusionQueryRenderer", "PixelCountIsPreciseEXT"):
        "out: CNAEXT precision metadata; the classic query limit is SDLGPU-80",
    ("ITextureCubeRenderer", "BindGL"):
        "out: EasyGL-native binding hook; SDL_GPU binds sampled texture descriptors",
    ("ITextureCubeRenderer", "GetSizeEXT"):
        "out: unused renderer helper; public TextureCube owns its size",
    ("ITextureCubeRenderer", "GetSurfaceFormatEXT"):
        "out: unused renderer metadata; public TextureCube owns its SurfaceFormat",
    ("ITexture3DRenderer", "BindGL"):
        "out: EasyGL-native binding hook; SDL_GPU binds sampled texture descriptors",
    ("ITexture3DRenderer", "GetDimensionsEXT"):
        "out: unused renderer helper; public Texture3D owns its dimensions",
    ("ITexture3DRenderer", "GetSurfaceFormatEXT"):
        "out: unused renderer metadata; public Texture3D owns its SurfaceFormat",
    ("ITextureRenderer", "BindGL"):
        "out: EasyGL-native binding hook; SDL_GPU binds sampled texture descriptors",
    ("IRenderTargetRenderer", "GetColorGLHandle"):
        "out: GL-native MRT plumbing; SDL_GPU carries native target state directly",
    ("IRenderTargetCubeRenderer", "GetGLHandle"):
        "out: GL-native framebuffer plumbing; SDL_GPU carries native target state directly",
}

OUT_GRAPHICS_EVIDENCE = {
    "SupportsHalfFloatTextureLinearFilteringEXT": "out: plans/plan_modern.md MOD-123",
    "SetUnsupported3DGraphicsCallBehavior": "out: CNAEXT 2D-renderer policy",
    "GetUnsupported3DGraphicsCallBehavior": "out: CNAEXT 2D-renderer policy",
    "SupportsCapability": "out: CNA capability diagnostics; audited separately for truthfulness",
    "GetAdditionalLimitationsTextEXT": "out: CNA capability diagnostics",
}

INTERFACE_EVIDENCE = {
    "IVertexBufferRenderer": "SDLGPU-59/78",
    "IIndexBufferRenderer": "SDLGPU-78",
    "IGpuTimerRenderer": "out: plans/plan_modern.md",
    "IOcclusionQueryRenderer": "SDLGPU-80",
    "IStorageBufferRenderer": "out: plans/plan_modern.md",
    "ITexture2DArrayRenderer": "out: plans/plan_modern.md MOD-2226",
    "IStorageTexture2DRenderer": "out: plans/plan_modern.md MOD-2227",
    "IComputeShaderRenderer": "out: plans/plan_modern.md",
    "ITextureCubeRenderer": "SDLGPU-70/73/74/81",
    "ITexture3DRenderer": "SDLGPU-71/79/81",
    "ITextureRenderer": "SDLGPU-69/72/74/81",
    "IRenderTargetRenderer": "SDLGPU-72/74/81",
    "IRenderTargetCubeRenderer": "SDLGPU-73/74/81",
    "IEffectRenderer": "out: existing CNAEXT ShaderEffect",
    "ISpriteBatchRenderer": "SDLGPU-64/76/79/81",
    "IRendererThreadContextLease": "default null lease is intentional for non-GL renderers",
}

GRAPHICS_EVIDENCE_GROUPS = (
    (
        "SDLGPU-56/68/81/83/85",
        {
            "~IGraphicsRenderer", "Present", "OnSurfaceChanged", "GetViewportSize",
            "AcquireThreadContextLeaseEXT", "OnSurfaceInvalidated",
            "GetDefaultViewportRect", "SetVirtualResolution", "SetPresentationMode",
            "SetSwapInterval", "GetSwapIntervalEXT", "ApplyMultiSampleCount",
            "GetAppliedMultiSampleCountEXT",
            "GetMultiSampleCount", "TransformWindowToLogical", "TransformLogicalToWindow",
            "CanBeginDrawEXT",
        },
    ),
    (
        "SDLGPU-68/85/132",
        {
            "UpdatePresentationFormatEXT", "GetAppliedBackBufferFormatEXT",
            "GetAppliedDepthStencilFormatEXT",
        },
    ),
    (
        "SDLGPU-57/69/70/71/72/73/74",
        {
            "GetMaxTextureSizeForProfileEXT", "GetMaxCubeSizeForProfileEXT",
            "GetMaxVolumeExtentForProfileEXT", "GetMaxRenderTargetsForProfileEXT",
            "ClassifySurfaceFormatEXT", "ClassifyTextureCubeFormatEXT",
            "ClassifyTexture3DFormatEXT", "ClassifyRenderTargetFormatEXT",
            "ClassifyRenderTargetCubeFormatEXT", "ClassifyColorTransferFormatEXT",
            "IsCompressedTransferFormatEXT", "IsCompressedCubeTransferFormatEXT",
            "LoadsCompressedContentNativelyEXT",
            "SupportsCapability", "GetAdditionalLimitationsTextEXT", "GetMaxVertexStreams",
            "GetMaxTextureDimension",
        },
    ),
    (
        "SDLGPU-57/87/132",
        {"SupportsDepthStencil", "SupportsDepthBuffer", "SupportsStencilBuffer"},
    ),
    (
        "SDLGPU-68/69/70/71/72/73/74/80",
        {
            "CreateTexture", "CreateSpriteBatch", "ReadBackbuffer", "CreateOcclusionQuery",
            "CreateTexture3D", "CreateTextureCube", "CreateRenderTarget2D",
            "CreateRenderTarget2DEXT", "SetRenderTarget2D", "CreateRenderTargetCube",
            "CreateRenderTargetCubeEXT", "SetRenderTargetCubeFace", "SetRenderTargets",
        },
    ),
    (
        "SDLGPU-58/65/66/67",
        {
            "Clear", "ClearColorAndDepth", "ClearDepth", "ClearStencil",
            "ClearDepthAndStencil", "ClearColorAndStencil", "ClearColorDepthAndStencil",
            "SetDepthTestEnabled", "SetBlendEnabled", "SetDepthWriteEnabled",
            "ApplyBlendState", "ApplyDepthStencilState", "ApplyRasterizerState",
            "ApplySamplerState", "ApplySamplerMipState", "ApplySamplerAddressW",
            "SetBlendFactor", "SetReferenceStencil", "SetScissorRect", "SetViewport",
        },
    ),
    (
        "SDLGPU-59/60/66/78/79",
        {
            "CreateVertexBuffer", "CreateIndexBuffer16", "CreateIndexBuffer32",
            "DrawColoredPrimitives", "DrawIndexedColoredPrimitives", "DrawPrimitivesEx",
            "DrawIndexedPrimitivesEx", "DrawInstancedPrimitivesEx", "Ensure3DSupported",
        },
    ),
    (
        "SDLGPU-79",
        {"CreateCompiledEffect", "SupportsCompiledEffects"},
    ),
)

GRAPHICS_EVIDENCE = {
    name: evidence
    for evidence, names in GRAPHICS_EVIDENCE_GROUPS
    for name in names
}

# Every entry is a reviewed, semantically active default rather than an omitted implementation.
# The exact signature makes interface drift fail closed instead of silently broadening an exception.
ALLOWED_CLASSIC_SDL_INHERITED: set[tuple[str, str, str]] = {
    (
        "ITextureCubeRenderer",
        "SetCompressedDataEXT :: bool (int, int, int, int, int, int, const void *, int)",
        "RenderTargetCube",
    ),
    (
        "ITextureCubeRenderer",
        "ShareCpuPixels :: void (int, std::shared_ptr<std::vector<uint8_t>>)",
        "TextureCube",
    ),
    (
        "ITextureCubeRenderer",
        "ShareCpuPixels :: void (int, std::shared_ptr<std::vector<uint8_t>>)",
        "RenderTargetCube",
    ),
    (
        "ITextureRenderer",
        "HasDefinedMipLevel :: bool (int) const noexcept",
        "Texture2D",
    ),
    (
        "ITextureRenderer",
        "ShareCpuPixels :: void (std::shared_ptr<std::vector<uint8_t>>)",
        "Texture2D",
    ),
    (
        "ITextureRenderer",
        "ShareCpuPixels :: void (std::shared_ptr<std::vector<uint8_t>>)",
        "RenderTarget2D",
    ),
    (
        "ISpriteBatchRenderer",
        "Draw :: void (const ITextureRenderer &, float, float, float, float, "
        "const Rectangle &, const Color &, float, const Vector2 &, SpriteEffects, float)",
        "SpriteBatch",
    ),
    (
        "ISpriteBatchRenderer",
        "SetSamplerMipState :: void (int, float)",
        "SpriteBatch",
    ),
    (
        "ISpriteBatchRenderer",
        "SetSamplerAddressW :: void (int)",
        "SpriteBatch",
    ),
    (
        "ISpriteBatchRenderer",
        "SetSamplerAddressModeWEXT :: void (int)",
        "SpriteBatch",
    ),
    (
        "IGraphicsRenderer",
        "AcquireThreadContextLeaseEXT :: std::unique_ptr<IRendererThreadContextLease> "
        "(RendererThreadContextLeaseRelease)",
        "renderer",
    ),
    (
        "IGraphicsRenderer",
        "OnSurfaceInvalidated :: void (CNA::Platform::WindowId)",
        "renderer",
    ),
    (
        "IGraphicsRenderer",
        "GetMaxTextureSizeForProfileEXT :: int (int) const",
        "renderer",
    ),
    (
        "IGraphicsRenderer",
        "GetMaxCubeSizeForProfileEXT :: int (int) const",
        "renderer",
    ),
    (
        "IGraphicsRenderer",
        "GetMaxVolumeExtentForProfileEXT :: int (int) const",
        "renderer",
    ),
    (
        "IGraphicsRenderer",
        "GetMaxRenderTargetsForProfileEXT :: int (int) const",
        "renderer",
    ),
    (
        "IGraphicsRenderer",
        "SetRenderTargetCubeFace :: void (IRenderTargetCubeRenderer *, int)",
        "renderer",
    ),
    (
        "IGraphicsRenderer",
        "Ensure3DSupported :: void (const char *) const",
        "renderer",
    ),
    (
        "IGraphicsRenderer",
        "GetMaxTextureDimension :: int () const",
        "renderer",
    ),
    (
        "IGraphicsRenderer",
        "CanBeginDrawEXT :: bool () const",
        "renderer",
    ),
}

ALLOWED_CLASSIC_SDL_UNAVAILABLE = {
    ("IRendererThreadContextLease", "~ :: void ()"),
}

ALLOWED_CLASSIC_SDL_LIMITATIONS = {
    ("IOcclusionQueryRenderer", "~ :: void ()"),
    ("IOcclusionQueryRenderer", "Begin :: void ()"),
    ("IOcclusionQueryRenderer", "End :: void ()"),
    ("IOcclusionQueryRenderer", "IsComplete :: bool () const"),
    ("IOcclusionQueryRenderer", "PixelCount :: int () const"),
}


@dataclass(frozen=True)
class Method:
    interface: str
    name: str
    type_name: str
    line: int
    base_contract: str

    @property
    def key(self) -> str:
        name = "~" if self.name.startswith("~") else self.name
        return f"{name} :: {self.type_name}"

    @property
    def match_key(self) -> str:
        type_name = self.type_name
        for qualified, canonical in (
            ("std::uint8_t", "uint8_t"),
            ("std::uint16_t", "uint16_t"),
            ("std::uint32_t", "uint32_t"),
            ("std::uint64_t", "uint64_t"),
            ("std::int8_t", "int8_t"),
            ("std::int16_t", "int16_t"),
            ("std::int32_t", "int32_t"),
            ("std::int64_t", "int64_t"),
        ):
            type_name = type_name.replace(qualified, canonical)
        parameter_start = type_name.find("(")
        if parameter_start >= 0:
            # C++ override identity is name + parameters/cv-ref qualifiers. Covariant returns and
            # a stricter noexcept specification do not stop a method from overriding its base.
            type_name = type_name[parameter_start:]
        type_name = re.sub(r"\s+noexcept(?:\([^)]*\))?$", "", type_name)
        name = "~" if self.name.startswith("~") else self.name
        return f"{name} :: {type_name}"


def compile_entry(build_dir: Path, source_suffix: str) -> dict:
    database = build_dir / "compile_commands.json"
    entries = json.loads(database.read_text(encoding="utf-8"))
    matches = [entry for entry in entries if entry["file"].endswith(source_suffix)]
    if len(matches) != 1:
        raise RuntimeError(f"expected one compile command ending in {source_suffix}, got {len(matches)}")
    return matches[0]


def ast_command(entry: dict, filter_name: str) -> list[str]:
    args = list(entry.get("arguments") or shlex.split(entry["command"]))
    source = entry["file"]
    kept: list[str] = []
    i = 1
    while i < len(args):
        arg = args[i]
        if arg == "-o":
            i += 2
            continue
        if arg == "-c" or arg == source:
            i += 1
            continue
        kept.append(arg)
        i += 1
    clang = shutil.which("clang++")
    if clang is None:
        raise RuntimeError("clang++ is required for the renderer-contract AST audit")
    return [
        clang,
        *kept,
        "-Xclang", "-ast-dump=json",
        "-Xclang", f"-ast-dump-filter={filter_name}",
        "-fsyntax-only",
        source,
    ]


def decode_json_stream(data: str) -> list[dict]:
    decoder = json.JSONDecoder()
    result: list[dict] = []
    offset = 0
    while offset < len(data):
        while offset < len(data) and data[offset].isspace():
            offset += 1
        if offset == len(data):
            break
        value, offset = decoder.raw_decode(data, offset)
        result.append(value)
    return result


def ast_records(
    entry: dict,
    filter_name: str,
    wanted: set[str],
    exact_sources: set[Path] | None = None,
) -> dict[str, dict]:
    process = subprocess.run(
        ast_command(entry, filter_name),
        cwd=entry["directory"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr or f"clang AST dump failed with {process.returncode}")
    records: dict[str, dict] = {}
    discovered: set[str] = set()
    normalized_sources = {path.resolve() for path in exact_sources or set()}
    source_bytes = {path: path.read_bytes() for path in normalized_sources}

    def declared_in_exact_source(node: dict) -> bool:
        location = node.get("loc", {})
        filename = location.get("file")
        if filename is not None:
            return Path(filename).resolve() in normalized_sources
        # Clang's JSON stream omits `file` when it is unchanged from the surrounding declaration.
        # Match the declaration token at its byte offset against the small reviewed source set.
        offset = location.get("offset")
        length = location.get("tokLen")
        name = node.get("name", "")
        if offset is None or length is None:
            return False
        token = name.encode("utf-8")
        return any(data[offset:offset + length] == token for data in source_bytes.values())

    def visit(node: dict) -> None:
        name = node.get("name")
        if node.get("kind") == "CXXRecordDecl" and node.get("completeDefinition"):
            methods = [
                child for child in node.get("inner", [])
                if child.get("kind") in {"CXXMethodDecl", "CXXDestructorDecl"}
            ]
            owns_contract_hook = any(
                method.get("virtual") or any(
                    child.get("kind") == "OverrideAttr"
                    for child in method.get("inner", []))
                for method in methods
            )
            if owns_contract_hook and normalized_sources and declared_in_exact_source(node):
                discovered.add(name)
            if name in wanted:
                if name in records:
                    raise RuntimeError(f"duplicate complete AST record for {name}")
                records[name] = node
        for child in node.get("inner", []):
            visit(child)

    for record in decode_json_stream(process.stdout):
        visit(record)
    missing = wanted - set(records)
    if missing:
        raise RuntimeError(f"AST dump omitted records: {sorted(missing)}")
    if normalized_sources and discovered != wanted:
        raise RuntimeError(
            "renderer-contract class inventory drift: "
            f"missing={sorted(wanted - discovered)}, extra={sorted(discovered - wanted)}")
    return records


def node_source(node: dict, source: str) -> str:
    begin = node.get("range", {}).get("begin", {}).get("offset")
    end = node.get("range", {}).get("end", {}).get("offset")
    length = node.get("range", {}).get("end", {}).get("tokLen", 1)
    if begin is None or end is None:
        return ""
    return source[begin:end + length]


def classify_default(node: dict, source: str) -> str:
    if node.get("pure"):
        return "required"
    if node.get("explicitlyDefaulted") == "default":
        return "defaulted-destructor"
    text = node_source(node, source)
    if "throw " in text:
        return "default-throws"
    if re.search(r"return\s+nullptr\s*;", text):
        return "default-null"
    if re.search(r"return\s+false\s*;", text):
        return "default-false"
    if re.search(r"return\s+true\s*;", text):
        return "default-true"
    if re.search(r"return\s+(?:0|0\.0f?|\{\})\s*;", text):
        return "default-zero"
    body = text[text.find("{") + 1:text.rfind("}")] if "{" in text else text
    body = re.sub(r"\(void\)\s*[A-Za-z_][A-Za-z0-9_]*\s*;", "", body)
    if not body.strip():
        return "default-no-op"
    if "return " in body or re.search(r"\b[A-Za-z_][A-Za-z0-9_]*\s*\(", body):
        return "default-delegates-or-value"
    return "default-implementation"


def record_methods(record: dict, interface: str | None, source: str) -> list[Method]:
    methods: list[Method] = []
    for node in record.get("inner", []):
        if node.get("kind") not in {"CXXMethodDecl", "CXXDestructorDecl"}:
            continue
        if interface is not None and not node.get("virtual"):
            continue
        if interface is None and not any(
                child.get("kind") == "OverrideAttr" for child in node.get("inner", [])):
            continue
        name = node.get("name", "")
        type_name = node.get("type", {}).get("qualType", "")
        if not name or not type_name:
            continue
        methods.append(Method(
            interface=interface or record["name"],
            name=name,
            type_name=type_name,
            line=node.get("loc", {}).get("line", 0),
            base_contract=classify_default(node, source) if interface is not None else "",
        ))
    return methods


def scope_for(interface: str, name: str) -> str:
    if interface in OUT_INTERFACES:
        return "out"
    if interface == "IGraphicsRenderer" and name in OUT_GRAPHICS_METHODS:
        return "out"
    if interface == "ISpriteBatchRenderer" and name in OUT_SPRITE_METHODS:
        return "out"
    if name in OUT_INTERFACE_METHODS.get(interface, set()):
        return "out"
    return "classic-xna-observable"


def evidence_for(interface: str, name: str, scope: str) -> str:
    if (interface, name) in OUT_METHOD_EVIDENCE:
        return OUT_METHOD_EVIDENCE[(interface, name)]
    if interface == "IGraphicsRenderer":
        if scope == "out":
            return OUT_GRAPHICS_EVIDENCE.get(
                name, "out: CNAEXT/EasyGL context/modern graphics")
        if name not in GRAPHICS_EVIDENCE:
            raise RuntimeError(f"no reviewed evidence owner for IGraphicsRenderer::{name}")
        return GRAPHICS_EVIDENCE[name]
    if interface == "ISpriteBatchRenderer" and scope == "out":
        return "out: modern CNAEXT mesh submission"
    return INTERFACE_EVIDENCE[interface]


def implementation_status(
    method: Method,
    implementations: tuple[tuple[str, str], ...],
    derived_methods: dict[str, set[str]],
) -> str:
    if not implementations:
        return "unavailable"
    status = []
    for label, class_name in implementations:
        kind = "override" if method.match_key in derived_methods[class_name] else f"inherited:{method.base_contract}"
        status.append(f"{label}={kind}")
    return ";".join(status)


def expected_rows(sdl_build: Path, easygl_build: Path) -> list[dict[str, str]]:
    sdl_entry = compile_entry(sdl_build, "modules/renderers/sdl-gpu/src/SdlGpuRenderer.cpp")
    easygl_entry = compile_entry(easygl_build, "modules/renderers/easygl/src/EasyGLRenderer.cpp")
    interface_names = set(INTERFACE_IMPLEMENTATIONS)
    sdl_class_names = {
        class_name
        for _, implementations in INTERFACE_IMPLEMENTATIONS.values()
        for _, class_name in implementations
        if class_name.startswith("SdlGpu")
    }
    easygl_class_names = {
        class_name
        for implementations, _ in INTERFACE_IMPLEMENTATIONS.values()
        for _, class_name in implementations
        if class_name.startswith("EasyGL")
    }
    interface_records = ast_records(
        sdl_entry, "CNA::Internal::Renderers::I", interface_names, {INTERFACE_HEADER})
    sdl_records = ast_records(
        sdl_entry, "CNA::Internal::Renderers::SdlGpu::SdlGpu", sdl_class_names, {SDL_HEADER})
    easygl_records = ast_records(
        easygl_entry, "EasyGL", easygl_class_names,
        {EASYGL_HEADER, Path(easygl_entry["file"])})

    interface_source = INTERFACE_HEADER.read_text(encoding="utf-8")
    sdl_source = SDL_HEADER.read_text(encoding="utf-8")
    easygl_source = EASYGL_HEADER.read_text(encoding="utf-8")
    sdl_methods = {
        name: {method.match_key for method in record_methods(record, None, sdl_source)}
        for name, record in sdl_records.items()
    }
    easygl_methods = {
        name: {method.match_key for method in record_methods(record, None, easygl_source)}
        for name, record in easygl_records.items()
    }

    rows: list[dict[str, str]] = []
    for interface in INTERFACE_IMPLEMENTATIONS:
        easygl_implementations, sdl_implementations = INTERFACE_IMPLEMENTATIONS[interface]
        for method in record_methods(interface_records[interface], interface, interface_source):
            scope = scope_for(interface, method.name)
            easygl_status = implementation_status(method, easygl_implementations, easygl_methods)
            sdl_status = implementation_status(method, sdl_implementations, sdl_methods)
            state = "out" if scope == "out" else "="
            identity = (interface, method.key)
            if scope != "out" and identity in ALLOWED_CLASSIC_SDL_LIMITATIONS:
                state = "⛔"
            elif scope != "out":
                if (not sdl_implementations
                        and identity not in ALLOWED_CLASSIC_SDL_UNAVAILABLE):
                    state = "~"
                for label, _ in sdl_implementations:
                    token = f"{label}=inherited:"
                    if token in sdl_status and (interface, method.key, label) not in ALLOWED_CLASSIC_SDL_INHERITED:
                        state = "~"
            rows.append({
                "interface": interface,
                "hook": method.key,
                "line": str(method.line),
                "base_contract": method.base_contract,
                "scope": scope,
                "easygl": easygl_status,
                "sdl_gpu": sdl_status,
                "state": state,
                "evidence": evidence_for(interface, method.name, scope),
            })
    return rows


FIELDS = (
    "interface", "hook", "line", "base_contract", "scope",
    "easygl", "sdl_gpu", "state", "evidence",
)


def write_manifest(rows: list[dict[str, str]]) -> None:
    with MANIFEST.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def validate(rows: list[dict[str, str]]) -> None:
    with MANIFEST.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        actual = list(reader)
        fieldnames = tuple(reader.fieldnames or ())
    errors: list[str] = []
    if fieldnames != FIELDS:
        errors.append(f"manifest header is {fieldnames!r}, expected {FIELDS!r}")
    if actual != rows:
        errors.append("manifest differs from the current Clang AST inventory; run with --write")
    identities = [(row["interface"], row["hook"]) for row in rows]
    if len(identities) != len(set(identities)):
        errors.append("duplicate interface/hook identities in AST inventory")
    observed_inherited = {
        (row["interface"], row["hook"], part.split("=", 1)[0])
        for row in rows
        if row["scope"] != "out"
        for part in row["sdl_gpu"].split(";")
        if "=inherited:" in part
    }
    stale_allowances = ALLOWED_CLASSIC_SDL_INHERITED - observed_inherited
    if stale_allowances:
        errors.append(f"stale classic inherited-default allowances: {sorted(stale_allowances)}")
    observed_unavailable = {
        (row["interface"], row["hook"])
        for row in rows
        if row["scope"] != "out" and row["sdl_gpu"] == "unavailable" and row["state"] == "="
    }
    stale_unavailable = ALLOWED_CLASSIC_SDL_UNAVAILABLE - observed_unavailable
    if stale_unavailable:
        errors.append(f"stale classic unavailable allowances: {sorted(stale_unavailable)}")
    observed_limitations = {
        (row["interface"], row["hook"])
        for row in rows
        if row["state"] == "⛔"
    }
    stale_limitations = ALLOWED_CLASSIC_SDL_LIMITATIONS - observed_limitations
    if stale_limitations:
        errors.append(f"stale classic limitation allowances: {sorted(stale_limitations)}")
    uncertain = [f"{row['interface']}::{row['hook']}" for row in rows if row["state"] == "~"]
    if uncertain:
        errors.append(f"unreviewed classic inherited defaults: {uncertain}")
    bad = [f"{row['interface']}::{row['hook']}" for row in rows if row["state"] not in {"=", "out", "⛔"}]
    if bad:
        errors.append(f"invalid/unresolved states: {bad}")
    if errors:
        raise SystemExit("\n".join(errors))
    counts: dict[str, int] = {}
    for row in rows:
        counts[row["state"]] = counts.get(row["state"], 0) + 1
    print(f"classified {len(rows)} renderer-contract hooks")
    for state in ("=", "out", "⛔"):
        print(f"{state}: {counts.get(state, 0)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdl-build", type=Path, default=ROOT / "cmake-build-sdlgpu")
    parser.add_argument("--easygl-build", type=Path, default=ROOT / "cmake-build-debug")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    rows = expected_rows(args.sdl_build.resolve(), args.easygl_build.resolve())
    if args.write:
        write_manifest(rows)
    validate(rows)


if __name__ == "__main__":
    main()
