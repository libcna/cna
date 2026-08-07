// SPDX-License-Identifier: MS-PL
// SKIA-154: below-the-API spike proving the versioned SkVertices mesh/effect ABI --
// CNA_SKIA_SKSL_MESH_V1 marker discrimination, bounded uniform/texture reflection diagnostics,
// texture-child lifetime, and the basic source-keyed compilation cache with clone isolation. See
// docs/skia-vertices-2d-effect-contract.md's "SKIA-154: the mesh/effect ABI itself" section for the
// full design. No public ShaderEffect/SpriteBatch wiring here -- that stays SKIA-157's job.

#include "CNA/Internal/Backends/Skia/SkiaMeshEffectBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkShader.h"
#include "include/core/SkVertices.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace CNA::Internal::Backends::Skia;
using CNA::Internal::Backends::ITextureBackend;
using CNA::Internal::Graphics::ImageData;

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures;
    }

    const std::string kMarker(kSkiaSkslMeshEffectMarkerEXT);

    struct Rgba final { std::uint8_t r = 0, g = 0, b = 0, a = 0; };

    [[nodiscard]] Rgba ReadCentrePixel(const SkiaSurface& surface)
    {
        const std::vector<std::uint8_t> pixels = surface.SnapshotRgba();
        const int cx = surface.Width() / 2;
        const int cy = surface.Height() / 2;
        const std::size_t offset = (static_cast<std::size_t>(cy) * surface.Width() + cx) * 4u;
        return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
    }

    [[nodiscard]] std::shared_ptr<ITextureBackend> MakeSolidTexture(
        std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        ImageData data;
        data.width = 1;
        data.height = 1;
        data.pixels = {r, g, b, a};
        return std::make_shared<SkiaTextureBackend>(data);
    }

    [[nodiscard]] sk_sp<SkVertices> MakeQuad(int width, int height)
    {
        SkPoint pos[4] = {
            {0.0f, 0.0f}, {static_cast<float>(width), 0.0f},
            {static_cast<float>(width), static_cast<float>(height)}, {0.0f, static_cast<float>(height)},
        };
        SkPoint uv[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        return SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, 4, pos, uv, nullptr, 6, indices);
    }

    [[nodiscard]] Rgba DrawShader(const sk_sp<SkShader>& shader)
    {
        SkiaSurface surface(4, 4);
        surface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        sk_sp<SkVertices> quad = MakeQuad(4, 4);
        SkPaint paint;
        paint.setShader(shader);
        surface.Canvas()->drawVertices(quad, SkBlendMode::kSrc, paint);
        return ReadCentrePixel(surface);
    }
}

int main()
{
    // -- marker discrimination: only the exact mesh marker compiles --
    {
        SkiaMeshEffectCacheEXT cache;
        SkiaMeshEffectBackend backend;
        const bool ok = backend.CompileProgram(
            "CNA_SKIA_SKSL_V1", "half4 main(float2 p) { return half4(1); }", cache);
        Check(!ok && !backend.IsValid(), "the sprite ABI's own marker is rejected by the mesh ABI");
        Check(backend.GetCompileError().find("CNA_SKIA_SKSL_MESH_V1") != std::string::npos,
              "the rejection names the exact expected marker");
    }
    {
        SkiaMeshEffectCacheEXT cache;
        SkiaMeshEffectBackend backend;
        const bool ok = backend.CompileProgram(
            "totally untagged glsl", "half4 main(float2 p) { return half4(1); }", cache);
        Check(!ok && !backend.IsValid(), "untagged/arbitrary source is rejected");
    }

    // -- a colored-only mesh effect (zero declared children) compiles and renders -- proving the
    // mesh ABI has no mandatory reserved primary texture, unlike the sprite ABI's cnaTexture0 --
    {
        SkiaMeshEffectCacheEXT cache;
        SkiaMeshEffectBackend backend;
        const std::string source =
            "half4 main(float2 p) { return half4(0.5, 0.25, 0.75, 1.0); }";
        Check(backend.CompileProgram(kMarker, source, cache),
              "a mesh effect with zero declared children compiles");
        backend.ValidateMeshBindingsEXT(); // must not throw -- nothing to bind
        const sk_sp<SkShader> shader = backend.MakeMeshShaderEXT();
        Check(shader != nullptr, "a colored-only mesh effect builds a real shader");
        const Rgba pixel = DrawShader(shader);
        Check(pixel.r == 128 && pixel.g == 64 && pixel.b == 191,
              "the colored-only mesh effect renders its exact declared colour");
    }

    // -- reflected uniform diagnostics: exact name/type match required --
    {
        SkiaMeshEffectCacheEXT cache;
        SkiaMeshEffectBackend backend;
        const std::string source =
            "uniform float3 uColour;\n"
            "half4 main(float2 p) { return half4(uColour, 1.0); }";
        Check(backend.CompileProgram(kMarker, source, cache), "a uniform-only mesh effect compiles");

        bool threwWrongType = false;
        try { backend.SetUniformFloat("uColour", 1.0f); }
        catch (const std::invalid_argument&) { threwWrongType = true; }
        Check(threwWrongType, "SetUniformFloat on a float3 uniform throws a type-mismatch diagnostic");

        bool threwUnknownName = false;
        try { backend.SetUniformVec3("nonexistent", 1.0f, 2.0f, 3.0f); }
        catch (const std::invalid_argument&) { threwUnknownName = true; }
        Check(threwUnknownName, "SetUniformVec3 on an undeclared name throws a not-found diagnostic");

        backend.SetUniformVec3("uColour", 0.2f, 0.6f, 1.0f);
        const Rgba pixel = DrawShader(backend.MakeMeshShaderEXT());
        Check(pixel.r == 51 && pixel.g == 153 && pixel.b == 255,
              "a correctly reflected uniform renders its exact set value");
    }

    // -- texture child binding, validation, and weak lifetime tracking --
    {
        SkiaMeshEffectCacheEXT cache;
        SkiaMeshEffectBackend backend;
        const std::string source =
            "uniform shader cnaTexture0;\n"
            "half4 main(float2 p) { return cnaTexture0.eval(p); }";
        Check(backend.CompileProgram(kMarker, source, cache), "a single-texture-child mesh effect compiles");

        bool threwUnbound = false;
        try { backend.ValidateMeshBindingsEXT(); }
        catch (const std::runtime_error&) { threwUnbound = true; }
        Check(threwUnbound, "ValidateMeshBindingsEXT throws before cnaTexture0 is bound");

        std::shared_ptr<ITextureBackend> texture = MakeSolidTexture(10, 20, 30, 255);
        backend.BindTexture(0, texture.get());
        bool okAfterBind = true;
        try { backend.ValidateMeshBindingsEXT(); } catch (...) { okAfterBind = false; }
        Check(okAfterBind, "ValidateMeshBindingsEXT passes once cnaTexture0 is bound");

        const Rgba pixel = DrawShader(backend.MakeMeshShaderEXT());
        Check(pixel.r == 10 && pixel.g == 20 && pixel.b == 30,
              "the bound texture child samples its exact colour");

        texture.reset();
        bool threwExpired = false;
        try { backend.ValidateMeshBindingsEXT(); }
        catch (const std::runtime_error&) { threwExpired = true; }
        Check(threwExpired,
              "ValidateMeshBindingsEXT throws again once the bound texture's owner is disposed -- "
              "weak, not owning, lifetime tracking, matching SkiaEffectBackend::BindTexture's contract");
    }

    // -- compilation cache reuse and clone isolation --
    {
        SkiaMeshEffectCacheEXT cache;
        const std::string source =
            "uniform float uValue;\n"
            "half4 main(float2 p) { return half4(uValue, 0.0, 0.0, 1.0); }";

        SkiaMeshEffectBackend backendA;
        SkiaMeshEffectBackend backendB;
        Check(backendA.CompileProgram(kMarker, source, cache), "backend A compiles the shared source");
        Check(backendB.CompileProgram(kMarker, source, cache), "backend B compiles the identical source");
        Check(cache.SizeEXT() == 1,
              "the cache holds exactly one compiled program for two identical-source compiles -- "
              "the second compile was a cache hit, not a second SkSL compilation");

        backendA.SetUniformFloat("uValue", 0.2f);
        backendB.SetUniformFloat("uValue", 0.8f);
        const Rgba pixelA = DrawShader(backendA.MakeMeshShaderEXT());
        const Rgba pixelB = DrawShader(backendB.MakeMeshShaderEXT());
        Check(pixelA.r != pixelB.r && pixelA.r == static_cast<int>(0.2f * 255.0f + 0.5f)
                  && pixelB.r == static_cast<int>(0.8f * 255.0f + 0.5f),
              "two backend instances sharing one cached compiled program keep fully independent "
              "uniform state -- a cache hit shares the immutable program, never mutable state");
    }

    std::printf("=== %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
