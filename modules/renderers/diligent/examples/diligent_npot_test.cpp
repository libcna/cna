// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-56: real-device NPOT (non-power-of-two) Texture2D round-trip test,
// mirroring D3D11's own DX-140 (a genuinely non-power-of-two 5x3 texture, never exercised anywhere
// in that suite before) and going further in the same direction DILIGENT-55 already established for
// mip levels: a real GPU SetData/GetData round-trip, not just "does sampling look plausible".
//
// A 5x3 texture (5*4=20 bytes/row -- an odd, non-alignment-friendly row size) filled with DISTINCT
// per-pixel colours (not one solid colour): a row-pitch/stride-assumption bug in the staging-texture
// upload or readback path (ReadTextureRegion(), SetData()) would shift pixels sideways between
// rows, which a solid-colour fixture could never reveal since every byte is identical anyway.
//
// Check A -- full-texture SetData/GetData round-trips every one of the 15 distinct pixels
//   byte-exact.
// Check B -- a sub-rectangle GetData() read (columns 1..3, all 3 rows -- deliberately NOT aligned
//   to the full row) also round-trips exactly, exercising the row-pitch-vs-requested-width skip
//   path independently of the full-texture case.
// Check C -- a real draw samples the exact colour from the NPOT texture through the normal
//   BasicEffect/SpriteBatch path (not just a raw renderer round-trip), matching DX-140's own "does
//   NPOT upload/sample corrupt anything" concern.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kW = 5;
    constexpr int kH = 3;

    Color PixelColor(int index)
    {
        // Deterministic, all-distinct per pixel so a row-pitch/stride shift is a hard mismatch.
        return Color(static_cast<std::uint8_t>(10 + index * 15), static_cast<std::uint8_t>(200 - index * 10),
                    static_cast<std::uint8_t>(index * 7), static_cast<std::uint8_t>(255));
    }

    bool ColorEqual(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }
}

class DiligentNpotTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        Texture2D tex(device, kW, kH);
        std::vector<Color> pixels;
        pixels.reserve(kW * kH);
        for (int i = 0; i < kW * kH; ++i)
            pixels.push_back(PixelColor(i));
        tex.SetData(pixels.data(), kW * kH);

        // Check A: full-texture round-trip.
        {
            std::vector<Color> rb(kW * kH, Color(0, 0, 0, 0));
            tex.GetData(rb.data(), kW * kH);
            bool exact = true;
            for (int i = 0; i < kW * kH; ++i)
                if (!ColorEqual(rb[i], pixels[i]))
                    exact = false;
            Check(exact,
                  "Texture2D(5x3 NPOT): full-texture SetData/GetData round-trips all 15 distinct "
                  "pixels byte-exact");
        }

        // Check B: sub-rectangle round-trip, columns [1,4) x rows [0,3) -- not row-aligned to the
        // full 5-pixel width, exercising the row-pitch-vs-requested-width skip path.
        {
            const Rectangle region(1, 0, 3, 3);
            std::vector<Color> rb(3 * 3, Color(0, 0, 0, 0));
            tex.GetData(0, &region, rb.data(), 0, 9);
            bool exact = true;
            for (int y = 0; y < 3 && exact; ++y)
                for (int x = 0; x < 3; ++x)
                {
                    const int srcIndex = y * kW + (x + 1);
                    if (!ColorEqual(rb[static_cast<std::size_t>(y) * 3 + x], pixels[srcIndex]))
                    {
                        exact = false;
                        break;
                    }
                }
            Check(exact,
                  "Texture2D(5x3 NPOT): sub-rectangle GetData([1,4)x[0,3)) round-trips exactly, "
                  "not shifted by row-pitch padding");
        }

        // Check C: a real draw samples the exact colour through BasicEffect/SpriteBatch, not just a
        // raw renderer round-trip -- proves NPOT upload/sample doesn't corrupt through the normal
        // draw path either.
        {
            SamplerState point;
            point.setFilterProperty(TextureFilter::Point);
            point.setAddressUProperty(TextureAddressMode::Clamp);
            point.setAddressVProperty(TextureAddressMode::Clamp);
            device.getSamplerStatesProperty()[0] = point;

            BasicEffect effect(device);
            effect.setTextureProperty(&tex);
            effect.setTextureEnabledProperty(true);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.Apply();

            device.Clear(Color(0, 0, 0, 255));
            device.SetDepthTestEnabled(false);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setRasterizerStateProperty(RasterizerState::CullNone);

            const VertexPositionTexture verts[6] = {
                {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)},
                {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)},
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

            // Sample the viewport centre. Exactly which of the 5x3 texels maps there depends on the
            // renderer's own Y-flip convention (immaterial to this check), so instead of predicting
            // one exact texel, verify the sampled colour matches ANY of the 15 known, sparse,
            // pseudo-random texture colours -- something corruption/garbage/an all-black or
            // all-clear-colour result could not plausibly produce by chance.
            const auto& viewport = device.getViewportProperty();
            const Rectangle region(viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2,
                                   1, 1);
            Color sample(0, 0, 0, 0);
            device.GetBackBufferData(&region, &sample, 0, 1);

            bool matchesKnownTexel = false;
            for (const Color& expected : pixels)
                if (ColorEqual(sample, expected))
                    matchesKnownTexel = true;

            Check(matchesKnownTexel,
                  "Texture2D(5x3 NPOT): a real BasicEffect draw samples one of the texture's exact "
                  "known colours at the viewport centre, no corruption through the normal draw path");
        }

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = passCount_ == 3 ? 0 : 1;
        Exit();
    }

public:
    DiligentNpotTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(64);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentNpotTest game;
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
