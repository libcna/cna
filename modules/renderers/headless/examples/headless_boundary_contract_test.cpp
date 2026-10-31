// SPDX-License-Identifier: MS-PL
// Focused renderer-boundary coverage for the audit paths that public GraphicsDevice validation
// would otherwise mask: raw primitive ordinals, foreign renderer resources and LastBatch fields.

#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Headless;

namespace
{
    class ForeignTexture final : public ITextureRenderer
    {
    public:
        [[nodiscard]] int GetWidth() const override { return 4; }
        [[nodiscard]] int GetHeight() const override { return 3; }
    };

    int g_passCount = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++g_passCount;
    }

    bool ThrowsValidation(const auto& action)
    {
        try
        {
            action();
            return false;
        }
        catch (const HeadlessValidationException&)
        {
            return true;
        }
    }
}

int main()
{
    HeadlessRenderer renderer(64, 64);
    renderer.SetMode(HeadlessMode::Validation);

    ImageData image{4, 3, std::vector<std::uint8_t>(48, 0xFF), 1, 0};
    auto texture = renderer.CreateTexture(image);
    auto spriteBase = renderer.CreateSpriteBatch();
    auto& sprites = dynamic_cast<HeadlessSpriteBatchRenderer&>(*spriteBase);

    const Rectangle destination(7, 9, 11, 13);
    const Rectangle source(1, 2, 3, 1);
    const Color color(10, 20, 30, 40);
    const Vector2 origin(2.5f, 1.5f);
    sprites.Begin();
    sprites.Draw(*texture, destination, source, color, 0.75f, origin,
                 SpriteEffects::FlipHorizontally, 0.625f);
    sprites.End();
    const auto& batch = sprites.LastBatch();
    const bool fullRecord = batch.size() == 1 && batch[0].texture == texture.get() &&
        batch[0].destinationRectangle == destination && batch[0].sourceRectangle == source &&
        batch[0].color == color && std::abs(batch[0].rotation - 0.75f) < 0.00001f &&
        batch[0].origin == origin && batch[0].effects == SpriteEffects::FlipHorizontally &&
        std::abs(batch[0].layerDepth - 0.625f) < 0.00001f;
    Check(fullRecord, "LastBatch preserves every field of the full SpriteBatch draw overload");

    sprites.Begin();
    const bool clearedOnBegin = sprites.LastBatch().empty();
    sprites.End();
    Check(clearedOnBegin, "LastBatch is cleared by the next Begin call");

    auto vertices = renderer.CreateVertexBuffer(3);
    const float vertexData[9] = {};
    vertices->SetData(vertexData, 3, sizeof(float) * 3);
    const Matrix identity = Matrix::getIdentityProperty();
    const auto invalidPrimitive = static_cast<PrimitiveType>(999);

    Check(ThrowsValidation([&] {
              renderer.DrawColoredPrimitives(
                  *vertices, identity, identity, identity, invalidPrimitive, 1);
          }),
          "Validation mode rejects a raw invalid PrimitiveType ordinal at the renderer boundary");

    renderer.SetMode(HeadlessMode::Fast);
    bool invalidPrimitiveFastThrew = false;
    try
    {
        renderer.DrawColoredPrimitives(
            *vertices, identity, identity, identity, invalidPrimitive, 1);
    }
    catch (...)
    {
        invalidPrimitiveFastThrew = true;
    }
    Check(!invalidPrimitiveFastThrew,
          "Fast mode keeps the documented no-validation behavior for an invalid primitive ordinal");

    ForeignTexture foreignTexture;
    GpuDrawParams params{};
    params.textureEnabled = true;
    params.texture0 = &foreignTexture;
    renderer.SetMode(HeadlessMode::Validation);
    Check(ThrowsValidation([&] {
              renderer.DrawPrimitivesEx(
                  *vertices, identity, identity, identity, PrimitiveType::TriangleList, 1, params);
          }),
          "Validation mode rejects a texture resource owned by another renderer");

    sprites.Begin();
    const bool spriteForeignThrew = ThrowsValidation([&] { sprites.Draw(foreignTexture, 0.0f, 0.0f); });
    sprites.End();
    Check(spriteForeignThrew,
          "SpriteBatch validation rejects a texture resource owned by another renderer");

    renderer.SetMode(HeadlessMode::Fast);
    bool foreignFastThrew = false;
    try
    {
        renderer.DrawPrimitivesEx(
            *vertices, identity, identity, identity, PrimitiveType::TriangleList, 1, params);
    }
    catch (...)
    {
        foreignFastThrew = true;
    }
    Check(!foreignFastThrew,
          "Fast mode skips foreign-resource validation while keeping the draw structurally safe");

    std::printf("=== %d/%d PASS ===\n", g_passCount, 7);
    return g_passCount == 7 ? 0 : 1;
}
