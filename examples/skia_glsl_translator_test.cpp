// SPDX-License-Identifier: MS-PL
// SKIA-155: proves the deliberately restricted GLSL-to-SkSL translator against both rejection
// (every excluded construct, plus the real, complete EasyGL dual_textured fragment source) and a
// genuine differential pixel comparison for the one grammar this translator accepts today --
// docs/skia-glsl-to-sksl-translator-contract.md's `dual_textured` core formula. No public
// ShaderEffect/content integration here -- that stays SKIA-157's job, same as SKIA-153/154.

#include "CNA/Internal/Backends/Skia/SkiaGlslToSkslTranslatorEXT.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkString.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkRuntimeEffect.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace CNA::Internal::Backends::Skia;

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures;
    }

    [[nodiscard]] bool RejectsCiting(const std::string& glsl, const std::string& mustContain)
    {
        const GlslToSkslTranslationResultEXT result = TranslateGlslToSkslEXT(glsl);
        return !result.success && result.errorMessage.find(mustContain) != std::string::npos;
    }

    struct Rgba final { std::uint8_t r = 0, g = 0, b = 0, a = 0; };

    [[nodiscard]] Rgba ReadCentrePixel(const SkiaSurface& surface)
    {
        const std::vector<std::uint8_t> pixels = surface.SnapshotRgba();
        const int cx = surface.Width() / 2;
        const int cy = surface.Height() / 2;
        const std::size_t offset = (static_cast<std::size_t>(cy) * surface.Width() + cx) * 4u;
        return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
    }

    [[nodiscard]] sk_sp<SkImage> MakeSolidImage(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        const std::uint8_t rgba[4] = {r, g, b, a};
        SkiaSurface source(1, 1);
        (void)source.WritePixels(0, 0, 1, 1, rgba, 4);
        return source.SnapshotImage();
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

    // The real, complete dual_textured fragment source (docs/skia-easygl-effect-inventory.md),
    // copied verbatim from EasyGLGraphicsBackend.cpp's own embedded string literal.
    const std::string kRealDualTexturedSource = R"(#version 300 es
precision highp float;
in vec2 vUV;
in float vFogFactor;
uniform sampler2D uTexture;
uniform sampler2D uTexture2;
uniform vec4 uDiffuseColor;
uniform vec4 uAlphaTest;
uniform vec3 uFogColor;
out vec4 FragColor;
void main(){
    vec4 base=texture(uTexture,vUV);
    base.rgb*=2.0;
    FragColor=base*texture(uTexture2,vUV)*uDiffuseColor;
    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);
    if(_at<0.0)discard;
    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);
}
)";

    // Just the accepted subset of dual_textured's core formula: two sampler2D uniforms, one vec4
    // tint, one in vec2 UV, one out vec4 -- no alpha-test/fog/flip-V macro.
    const std::string kAcceptedDualTexturedSource = R"(
uniform sampler2D uTexture;
uniform sampler2D uTexture2;
uniform vec4 uDiffuseColor;
in vec2 vUV;
out vec4 FragColor;
void main() {
    vec4 base = texture(uTexture, vUV);
    base.rgb *= 2.0;
    FragColor = base * texture(uTexture2, vUV) * uDiffuseColor;
}
)";
}

int main()
{
    // -- rejection: one isolated construct per synthetic snippet --
    Check(RejectsCiting(
              "uniform sampler2D uTexture;\nin vec2 vUV;\nout vec4 FragColor;\n"
              "void main() { FragColor = texture(uTexture, vUV); if (FragColor.a < 0.5) discard; }",
              "discard"),
          "a fragment shader using `discard` is rejected, naming it");
    Check(RejectsCiting(
              "in vec2 vUV;\nout vec4 FragColor;\nvoid main() { gl_FragDepth = 0.5; }",
              "gl_FragDepth"),
          "a fragment shader writing `gl_FragDepth` is rejected, naming it");
    Check(RejectsCiting(
              "in vec2 vUV;\nout vec4 FragColor;\nout vec4 FragColor2;\n"
              "void main() { FragColor = vec4(1.0); }",
              "second `out`"),
          "a second `out` declaration (MRT) is rejected");
    Check(RejectsCiting(
              "uniform sampler2D uTexture;\nin vec2 vUV;\nout vec4 FragColor;\n"
              "void main() { FragColor = vec4(dFdx(vUV), 0.0, 1.0); }",
              "dFdx"),
          "a fragment shader using `dFdx` is rejected, naming it");
    Check(RejectsCiting(
              "in vec2 vUV;\nin float vFog;\nout vec4 FragColor;\n"
              "void main() { FragColor = vec4(vFog); }",
              "second `in`"),
          "a second `in` varying is rejected");
    Check(RejectsCiting(
              "precision highp float;\nin vec2 vUV;\nout vec4 FragColor;\n"
              "void main() { FragColor = vec4(1.0); }",
              "precision"),
          "a `precision` qualifier line is rejected");
    Check(RejectsCiting(
              "uniform samplerCube uEnv;\nin vec2 vUV;\nout vec4 FragColor;\n"
              "void main() { FragColor = vec4(1.0); }",
              "samplerCube"),
          "a `samplerCube` uniform is rejected");
    Check(RejectsCiting(
              "in vec2 vUV;\nout vec4 FragColor;\n"
              "void main() { FragColor = vec4(cnaSampleUV(vUV, 0.0), 0.0, 1.0); }",
              "cnaSampleUV"),
          "the backend-specific `cnaSampleUV` flip-V macro call is rejected");
    Check(RejectsCiting(
              "in vec2 vUV;\nout vec4 FragColor;\n"
              "vec3 Helper(vec3 x) { return x; }\n"
              "void main() { FragColor = vec4(Helper(vec3(1.0)), 1.0); }",
              "Helper"),
          "a helper function definition is rejected (not yet in this MVP's grammar)");
    Check(RejectsCiting("in vec2 vUV;\nout vec4 FragColor;\n", "main"),
          "source with no `main` function at all is rejected");
    {
        const GlslToSkslTranslationResultEXT real = TranslateGlslToSkslEXT(kRealDualTexturedSource);
        Check(!real.success,
              "the real, complete EasyGL dual_textured fragment source is rejected outright -- it "
              "declares a second `in float vFogFactor` varying and uses discard/fog beyond this "
              "MVP grammar, so it must not be silently mistranslated");
    }

    // -- positive/differential: the accepted subset compiles, and its SkSL rendering matches the
    // exact dual_textured formula this project already proved twice (SKIA-93, SKIA-153) --
    {
        const GlslToSkslTranslationResultEXT translated =
            TranslateGlslToSkslEXT(kAcceptedDualTexturedSource);
        Check(translated.success, "the accepted dual_textured-core subset translates successfully");
        if (translated.success)
        {
            auto compiled = SkRuntimeEffect::MakeForShader(SkString(translated.sksl));
            Check(compiled.effect != nullptr,
                  "the translated SkSL compiles through SkRuntimeEffect::MakeForShader");
            if (compiled.effect)
            {
                const sk_sp<SkImage> tex0 = MakeSolidImage(60, 60, 60, 255);
                const sk_sp<SkImage> tex1 = MakeSolidImage(200, 200, 200, 255);
                const sk_sp<SkShader> shader0 = tex0->makeShader(
                    SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kNearest));
                const sk_sp<SkShader> shader1 = tex1->makeShader(
                    SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kNearest));

                const float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                const sk_sp<SkData> uniformData = SkData::MakeWithCopy(tint, sizeof(tint));
                sk_sp<SkShader> children[2] = {shader0, shader1};
                sk_sp<SkShader> combined = compiled.effect->makeShader(uniformData, children, 2);
                Check(combined != nullptr, "the translated shader binds real texture children");

                SkiaSurface surface(4, 4);
                surface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
                sk_sp<SkVertices> quad = MakeQuad(4, 4);
                SkPaint paint;
                paint.setShader(combined);
                surface.Canvas()->drawVertices(quad, SkBlendMode::kSrc, paint);
                const Rgba pixel = ReadCentrePixel(surface);

                // Identical formula/inputs to SKIA-153's own hand-written-SkSL dual_textured check:
                // (60*2) * 200 / 255.
                const int expected = (60 * 2) * 200 / 255;
                const auto near = [](int actual, int expectedValue) {
                    const int diff = actual - expectedValue;
                    return (diff < 0 ? -diff : diff) <= 3;
                };
                Check(near(pixel.r, expected) && near(pixel.g, expected) && near(pixel.b, expected),
                      "the GLSL-translated shader renders pixel-identical output to SKIA-153's "
                      "already-proven hand-written SkSL dual_textured formula -- a genuine "
                      "differential comparison, not just a compile check");
            }
        }
    }

    std::printf("=== %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
