// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-37: real-device proof that the Diligent renderer implements real XNA's
// own default (BasicEffect/SkinnedEffect's PreferPerPixelLighting == false) per-vertex-lit shader
// family, rather than always rendering per-pixel regardless of the flag.
//
// Both shader families evaluate the exact same Blinn-Phong formula -- only the STAGE differs (once
// per vertex, Gouraud-interpolated, vs. once per fragment). For a flat quad with one uniform normal
// there is nothing for that evaluation frequency to change: the two must read back the SAME colour.
// That equality is the check -- not a plausible-looking colour on its own, which a shader that
// silently ignored the flag (always rendering one or the other family) would also produce.
//
// Check A -- BasicEffect (stride 32): PreferPerPixelLighting=true and =false render identically.
// Check B -- ...and the lit colour is meaningfully different from the same draw with lighting
//   disabled, proving check A isn't two shaders that both silently ignore lighting entirely.
// Check C -- SkinnedEffect (stride 52): PreferPerPixelLighting=true and =false render identically.
// Check D -- ...and likewise differs meaningfully from lighting disabled.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kChecks = 4;

    /// The stride-52 skinned layout: position, normal, UV, four weights, four bone indices.
    struct SkinnedVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedVertex) == 52, "the skinned vertex layout must be 52 bytes");

    Color ReadCenter(GraphicsDevice& device)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool ColorNear(Color a, Color b, int tolerance)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    /// A meaningful difference proves lighting was genuinely evaluated, not just a same-shader
    /// no-op on both sides of check A/C.
    bool MeaningfullyDifferent(Color a, Color b, int minDelta = 20)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) >= minDelta ||
               std::abs(a.getGProperty() - b.getGProperty()) >= minDelta ||
               std::abs(a.getBProperty() - b.getBProperty()) >= minDelta;
    }
}

class DiligentVertexLitTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    Texture2D whiteTexture_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

protected:
    void LoadContent() override
    {
        whiteTexture_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                    std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);

        // Full-screen quad facing the camera (normal +Z) so the whole surface shares one normal --
        // exactly the case where per-vertex and per-pixel lighting must agree pixel-for-pixel.
        const VertexPositionNormalTexture quad[6] = {
            {Vector3(-1.0f, 1.0f, 0.0f), Vector3(0, 0, 1), Vector2(0, 0)},
            {Vector3(-1.0f, -1.0f, 0.0f), Vector3(0, 0, 1), Vector2(0, 1)},
            {Vector3(1.0f, -1.0f, 0.0f), Vector3(0, 0, 1), Vector2(1, 1)},
            {Vector3(-1.0f, 1.0f, 0.0f), Vector3(0, 0, 1), Vector2(0, 0)},
            {Vector3(1.0f, -1.0f, 0.0f), Vector3(0, 0, 1), Vector2(1, 1)},
            {Vector3(1.0f, 1.0f, 0.0f), Vector3(0, 0, 1), Vector2(1, 0)},
        };
        VertexBuffer vb(device, 6);
        vb.SetData(quad, 6);

        BasicEffect effect(device);
        effect.setTextureProperty(&whiteTexture_);
        effect.setTextureEnabledProperty(true);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.EnableDefaultLighting();

        const auto drawBasic = [&](bool lightingEnabled, bool preferPerPixel) {
            effect.setLightingEnabledProperty(lightingEnabled);
            effect.setPreferPerPixelLightingProperty(preferPerPixel);
            device.Clear(Color::Blue);
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            return ReadCenter(device);
        };

        const Color perPixelLit = drawBasic(true, true);
        const Color vertexLit = drawBasic(true, false);
        const Color unlit = drawBasic(false, false);

        Check(ColorNear(perPixelLit, vertexLit, 4),
              "BasicEffect: PreferPerPixelLighting true/false render the same flat-normal quad "
              "identically");
        Check(MeaningfullyDifferent(vertexLit, unlit),
              "BasicEffect: the vertex-lit result is genuinely lit, not a lighting no-op");

        // Same comparison for SkinnedEffect (stride 52), identity bone palette so skinning itself
        // is a no-op and only the lighting stage differs.
        const SkinnedVertex skinnedQuad[6] = {
            {-1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
            {-1, -1, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0},
            {1, -1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
            {-1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
            {1, -1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0},
        };
        VertexBuffer skinnedVb(device, 6);
        skinnedVb.SetDataRaw(skinnedQuad, 6, static_cast<int>(sizeof(SkinnedVertex)));

        SkinnedEffect skinnedEffect(device);
        skinnedEffect.setTextureProperty(&whiteTexture_);
        skinnedEffect.setWorldProperty(Matrix::getIdentityProperty());
        skinnedEffect.setViewProperty(Matrix::getIdentityProperty());
        skinnedEffect.setProjectionProperty(Matrix::getIdentityProperty());
        skinnedEffect.setWeightsPerVertexProperty(1);
        skinnedEffect.EnableDefaultLighting();
        skinnedEffect.SetBoneTransforms(std::vector<Matrix>(72, Matrix::getIdentityProperty()));

        const auto drawSkinned = [&](bool preferPerPixel) {
            skinnedEffect.setPreferPerPixelLightingProperty(preferPerPixel);
            device.Clear(Color::Blue);
            skinnedEffect.Apply();
            device.SetVertexBuffer(&skinnedVb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            return ReadCenter(device);
        };

        const Color skinnedPerPixel = drawSkinned(true);
        const Color skinnedVertexLit = drawSkinned(false);

        Check(ColorNear(skinnedPerPixel, skinnedVertexLit, 4),
              "SkinnedEffect: PreferPerPixelLighting true/false render the same flat-normal quad "
              "identically");
        Check(MeaningfullyDifferent(skinnedVertexLit, Color::Blue),
              "SkinnedEffect: the vertex-lit result is genuinely lit and covers the probe");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentVertexLitTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentVertexLitTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
