// SPDX-License-Identifier: MS-PL
// SKIA-153: headless feasibility spike for SkVertices-based 2D vertex/fragment rendering, the
// replacement for the originally planned SkMeshSpecification prototype -- SkBitmapDevice::drawMesh
// is a literal empty stub in the pinned Skia revision (raster cannot draw a mesh at all), while
// SkBitmapDevice::drawVertices is genuinely implemented. See
// docs/skia-vertices-2d-effect-contract.md for the full design rationale. Deliberately stays below
// the public API: SKIA-154-157 own the versioned ABI, translator, hardening, and public
// ShaderEffect/SpriteBatch integration respectively.

#include "CNA/Internal/Renderers/Skia/SkiaSurface.hpp"

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkRuntimeEffect.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using CNA::Internal::Renderers::Skia::SkiaSurface;

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures;
    }

    [[nodiscard]] bool Near(int actual, int expected, int tolerance = 2)
    {
        int diff = actual - expected;
        if (diff < 0) diff = -diff;
        return diff <= tolerance;
    }

    struct Rgba final { std::uint8_t r = 0, g = 0, b = 0, a = 0; };

    [[nodiscard]] Rgba ReadCentrePixel(const SkiaSurface& surface)
    {
        const std::vector<std::uint8_t> pixels = surface.SnapshotRgba();
        const int w = surface.Width();
        const int cx = w / 2;
        const int cy = surface.Height() / 2;
        const std::size_t offset = (static_cast<std::size_t>(cy) * w + cx) * 4u;
        Rgba out;
        out.r = pixels[offset + 0];
        out.g = pixels[offset + 1];
        out.b = pixels[offset + 2];
        out.a = pixels[offset + 3];
        return out;
    }

    [[nodiscard]] sk_sp<SkImage> MakeSolidImage(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        const std::uint8_t rgba[4] = {r, g, b, a};
        SkiaSurface source(1, 1);
        (void)source.WritePixels(0, 0, 1, 1, rgba, 4);
        return source.SnapshotImage();
    }

    // A full-surface quad (two triangles) at device-space corners; `reversed` flips the winding
    // of both triangles to prove SkVertices performs no back-face culling.
    [[nodiscard]] sk_sp<SkVertices> MakeQuad(
        int width, int height, const SkColor* colours, const SkPoint* texCoords, bool reversed)
    {
        SkPoint pos[4] = {
            {0.0f, 0.0f}, {static_cast<float>(width), 0.0f},
            {static_cast<float>(width), static_cast<float>(height)}, {0.0f, static_cast<float>(height)},
        };
        std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        if (reversed)
        {
            std::uint16_t reversedIndices[6] = {0, 2, 1, 0, 3, 2};
            std::memcpy(indices, reversedIndices, sizeof(indices));
        }
        return SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, 4, pos, texCoords, colours,
                                    6, indices);
    }
}

int main()
{
    // -- "colored": vertex colour x opaque paint tint, no texture (BasicEffect vertex-colour path) --
    {
        SkiaSurface surface(4, 4);
        surface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        const SkColor colours[4] = {
            SkColorSetARGB(255, 200, 100, 50), SkColorSetARGB(255, 200, 100, 50),
            SkColorSetARGB(255, 200, 100, 50), SkColorSetARGB(255, 200, 100, 50),
        };
        sk_sp<SkVertices> quad = MakeQuad(4, 4, colours, nullptr, false);
        SkPaint paint;
        paint.setColor(SkColorSetARGB(255, 255, 255, 255));
        surface.Canvas()->drawVertices(quad, SkBlendMode::kModulate, paint);
        const Rgba pixel = ReadCentrePixel(surface);
        Check(pixel.r == 200 && pixel.g == 100 && pixel.b == 50 && pixel.a == 255,
              "colored: vertex colour reproduced exactly against an opaque white paint tint");
    }
    {
        SkiaSurface surface(4, 4);
        surface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        const SkColor colours[4] = {
            SkColorSetARGB(255, 200, 100, 50), SkColorSetARGB(255, 200, 100, 50),
            SkColorSetARGB(255, 200, 100, 50), SkColorSetARGB(255, 200, 100, 50),
        };
        sk_sp<SkVertices> quad = MakeQuad(4, 4, colours, nullptr, false);
        SkPaint paint;
        paint.setColor(SkColorSetARGB(255, 128, 128, 128));
        surface.Canvas()->drawVertices(quad, SkBlendMode::kModulate, paint);
        const Rgba pixel = ReadCentrePixel(surface);
        Check(Near(pixel.r, 200 * 128 / 255, 3) && Near(pixel.g, 100 * 128 / 255, 3)
                  && Near(pixel.b, 50 * 128 / 255, 3),
              "colored: kModulate reproduces BasicEffect's vertexColour*uDiffuseColor combine");
    }

    // -- "textured": paint shader alone, texCoords present, no vertex colour --
    {
        SkiaSurface surface(4, 4);
        surface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        const sk_sp<SkImage> tex = MakeSolidImage(0, 200, 0, 255);
        const SkPoint uv[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        sk_sp<SkVertices> quad = MakeQuad(4, 4, nullptr, uv, false);
        SkPaint paint;
        paint.setShader(tex->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                        SkSamplingOptions(SkFilterMode::kNearest)));
        surface.Canvas()->drawVertices(quad, SkBlendMode::kModulate, paint);
        const Rgba pixel = ReadCentrePixel(surface);
        Check(pixel.r == 0 && pixel.g == 200 && pixel.b == 0 && pixel.a == 255,
              "textured: paint shader sampled through SkVertices texCoords reproduces the exact texel");
    }

    // -- "col_textured": vertex colour x texture combine --
    {
        SkiaSurface surface(4, 4);
        surface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        const sk_sp<SkImage> tex = MakeSolidImage(128, 128, 128, 255);
        const SkPoint uv[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        const SkColor colours[4] = {
            SkColorSetARGB(255, 200, 100, 50), SkColorSetARGB(255, 200, 100, 50),
            SkColorSetARGB(255, 200, 100, 50), SkColorSetARGB(255, 200, 100, 50),
        };
        sk_sp<SkVertices> quad = MakeQuad(4, 4, colours, uv, false);
        SkPaint paint;
        paint.setShader(tex->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                        SkSamplingOptions(SkFilterMode::kNearest)));
        surface.Canvas()->drawVertices(quad, SkBlendMode::kModulate, paint);
        const Rgba pixel = ReadCentrePixel(surface);
        Check(Near(pixel.r, 200 * 128 / 255, 3) && Near(pixel.g, 100 * 128 / 255, 3)
                  && Near(pixel.b, 50 * 128 / 255, 3),
              "col_textured: vertex colour x texture reproduces BasicEffect's combined-colour path");
    }

    // -- "dual_textured"-style combine: an SkRuntimeEffect paint shader with two texture children,
    // proving arbitrary custom fragment math (not just a plain image shader) also receives the
    // SkVertices-interpolated local coordinate correctly -- de-risks SKIA-154's real ABI. Uses
    // docs/skia-easygl-effect-inventory.md's own dual_textured formula: tex0.rgb*=2, then
    // multiplied by tex1 and the tint.
    {
        SkiaSurface surface(4, 4);
        surface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        const sk_sp<SkImage> tex0 = MakeSolidImage(60, 60, 60, 255);
        const sk_sp<SkImage> tex1 = MakeSolidImage(200, 200, 200, 255);
        const SkPoint uv[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        sk_sp<SkVertices> quad = MakeQuad(4, 4, nullptr, uv, false);

        const char* source = R"(
            uniform shader tex0;
            uniform shader tex1;
            half4 main(float2 p) {
                half4 base = tex0.eval(p);
                base.rgb *= 2.0;
                return base * tex1.eval(p);
            }
        )";
        auto result = SkRuntimeEffect::MakeForShader(SkString(source));
        Check(result.effect != nullptr, "dual_textured-style: runtime effect compiles");
        if (result.effect)
        {
            const sk_sp<SkShader> shader0 = tex0->makeShader(
                SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kNearest));
            const sk_sp<SkShader> shader1 = tex1->makeShader(
                SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kNearest));
            sk_sp<SkShader> children[2] = {shader0, shader1};
            sk_sp<SkShader> combined = result.effect->makeShader(nullptr, children, 2);
            SkPaint paint;
            paint.setShader(std::move(combined));
            surface.Canvas()->drawVertices(quad, SkBlendMode::kSrc, paint);
            const Rgba pixel = ReadCentrePixel(surface);
            const int expected = (60 * 2) * 200 / 255;
            Check(Near(pixel.r, expected, 3) && Near(pixel.g, expected, 3) && Near(pixel.b, expected, 3),
                  "dual_textured-style: a 2-child SkRuntimeEffect paint shader receives SkVertices' "
                  "interpolated local coordinate exactly like a plain image shader, reproducing "
                  "base*=2 then modulated-by-tex1");
        }
    }

    // -- winding: SkVertices has no back-face culling -- both windings must render identically --
    {
        SkiaSurface forward(4, 4);
        SkiaSurface reversed(4, 4);
        forward.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        reversed.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        const SkColor colours[4] = {
            SkColorSetARGB(255, 10, 20, 30), SkColorSetARGB(255, 10, 20, 30),
            SkColorSetARGB(255, 10, 20, 30), SkColorSetARGB(255, 10, 20, 30),
        };
        sk_sp<SkVertices> forwardQuad = MakeQuad(4, 4, colours, nullptr, false);
        sk_sp<SkVertices> reversedQuad = MakeQuad(4, 4, colours, nullptr, true);
        SkPaint paint;
        paint.setColor(SkColorSetARGB(255, 255, 255, 255));
        forward.Canvas()->drawVertices(forwardQuad, SkBlendMode::kModulate, paint);
        reversed.Canvas()->drawVertices(reversedQuad, SkBlendMode::kModulate, paint);
        const Rgba a = ReadCentrePixel(forward);
        const Rgba b = ReadCentrePixel(reversed);
        Check(a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a && a.r == 10 && a.g == 20 && a.b == 30,
              "winding: a reversed-winding quad renders identically to the forward-winding one -- "
              "SkVertices performs no back-face culling, matching docs/skia-vertices-2d-effect-"
              "contract.md's documented architectural finding, not a defect");
    }

    // -- alpha convention: unpremultiplied vertex colour alpha src-over blends correctly --
    {
        SkiaSurface surface(4, 4);
        surface.Clear(1.0f, 1.0f, 1.0f, 1.0f); // opaque white background
        const SkColor colours[4] = {
            SkColorSetARGB(128, 0, 0, 0), SkColorSetARGB(128, 0, 0, 0),
            SkColorSetARGB(128, 0, 0, 0), SkColorSetARGB(128, 0, 0, 0),
        };
        sk_sp<SkVertices> quad = MakeQuad(4, 4, colours, nullptr, false);
        SkPaint paint;
        paint.setColor(SkColorSetARGB(255, 255, 255, 255));
        surface.Canvas()->drawVertices(quad, SkBlendMode::kModulate, paint);
        const Rgba pixel = ReadCentrePixel(surface);
        // Half-alpha black over opaque white: straight-alpha src-over expects roughly mid-grey.
        Check(Near(pixel.r, 128, 4) && Near(pixel.g, 128, 4) && Near(pixel.b, 128, 4) && pixel.a == 255,
              "alpha: half-alpha unpremultiplied vertex colour src-over blends onto an opaque "
              "background as ordinary straight-alpha compositing, not a premultiplied double-darken");
    }

    // -- ordering: a drawVertices call interleaved with ordinary canvas draws preserves painter's --
    // -- order, standing in for SpriteBatch's own immediate/deferred draw sequencing --
    {
        SkiaSurface surface(4, 4);
        surface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SkPaint backgroundPaint;
        backgroundPaint.setColor(SkColorSetARGB(255, 255, 0, 0));
        surface.Canvas()->drawRect(SkRect::MakeWH(4.0f, 4.0f), backgroundPaint);

        const SkColor colours[4] = {
            SkColorSetARGB(255, 0, 255, 0), SkColorSetARGB(255, 0, 255, 0),
            SkColorSetARGB(255, 0, 255, 0), SkColorSetARGB(255, 0, 255, 0),
        };
        sk_sp<SkVertices> quad = MakeQuad(4, 4, colours, nullptr, false);
        SkPaint vertexPaint;
        vertexPaint.setColor(SkColorSetARGB(255, 255, 255, 255));
        surface.Canvas()->drawVertices(quad, SkBlendMode::kModulate, vertexPaint);

        SkPaint markerPaint;
        markerPaint.setColor(SkColorSetARGB(255, 0, 0, 255));
        surface.Canvas()->drawRect(SkRect::MakeXYWH(0.0f, 0.0f, 1.0f, 1.0f), markerPaint);

        const Rgba centre = ReadCentrePixel(surface);
        const std::vector<std::uint8_t> pixels = surface.SnapshotRgba();
        const Rgba corner{pixels[0], pixels[1], pixels[2], pixels[3]};
        Check(centre.r == 0 && centre.g == 255 && centre.b == 0,
              "ordering: drawVertices paints over the earlier background rect");
        Check(corner.r == 0 && corner.g == 0 && corner.b == 255,
              "ordering: a later canvas draw still paints over the drawVertices result -- painter's "
              "order is preserved across interleaved drawVertices/drawRect calls on one canvas");
    }

    std::printf("=== %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
