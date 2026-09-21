// SPDX-License-Identifier: MS-PL
// Task 387: verify DualTextureEffect's second texture (`Texture2`, slot 1) null behavior on
// Vulkan. See examples/easygl_dualtextureeffect_null_texture2_test.cpp for the full
// derivation.
//
// plans/plan_vulkan_parity.md VKPAR-0004 corrected the expected value and strengthened the probe.
// The white fallback this file was written to confirm is not XNA's rule: an unbound
// DualTextureEffect slot samples opaque black -- tools/xna-oracle/reference/null-texture/
// dualtexture_texture{,2}_null.png, centre (0,0,0,255) -- so the product goes to black whichever
// slot is missing. GSC-0004 corrected DirectX11, DirectX12 and EasyGL the same way.
//
// Because the expected pixel is now black, the second draw clears to a WITNESS colour instead of
// black: otherwise "sampled opaque black" and "drew nothing at all" are the same readback. The
// witness check below is what makes the assertion mean something.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    bool colourMatch(Color got, Color want, int tol = 20)
    {
        return closeTo(got.getRProperty(), want.getRProperty(), tol)
            && closeTo(got.getGProperty(), want.getGProperty(), tol)
            && closeTo(got.getBProperty(), want.getBProperty(), tol);
    }
}

class VulkanDualTextureNullTexture2Test : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            result_ = 1;
        }
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding (mirrors the Task 364/884 fix on Bgfx, retired 2026-09-17): this quad's
        // winding is culled under FNA's real default RasterizerState.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color kBlack(0, 0, 0, 255);
        // VKPAR-0004: the second draw's clear. Any colour the draw cannot produce will do;
        // this is the one StockEffectNullTextureTest uses for the same reason.
        const Color kWitness(7, 199, 53, 255);
        const Color kDistinctivePrev(20, 200, 20, 255); // "previous draw" texture2
        const Color kTex(80, 40, 120, 255);             // non-saturated Texture (slot 0)

        Texture2D tex(dev, 1, 1);      tex.SetData(&kTex, 1);
        Texture2D texPrev(dev, 1, 1);  texPrev.SetData(&kDistinctivePrev, 1);

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        // First draw: Texture + real, distinctive texture2 -- establishes "previous draw" state.
        dev.Clear(kBlack);
        {
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&tex);
            fx.setTexture2Property(&texPrev);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        }

        // Second draw: Texture=kTex, Texture2=null -- the actual behavior under test.
        dev.Clear(kWitness);
        {
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&tex);
            fx.setTexture2Property(nullptr);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        }

        Color got = readCenter(dev);
        check(colourMatch(got, kBlack),
              "Texture2=null samples XNA's opaque black (not the previous draw's texture)",
              got, kBlack);
        check(!colourMatch(got, kWitness),
              "Texture2=null: pixel != the clear colour (proves the quad was drawn at all)",
              got, kWitness);
        check(!colourMatch(got, kDistinctivePrev),
              "Texture2=null: pixel != previous draw's texture (proves no stale-state leak)",
              got, kDistinctivePrev);

        Exit();
    }

public:
    VulkanDualTextureNullTexture2Test()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // VKPAR-0004: GetBackBufferData is a HiDef operation. Without this these three tests
        // aborted before asserting anything -- "GetBackBufferData is not supported by the
        // Reach graphics profile" -- on the baseline as well as here, so their registrations
        // had been contributing nothing. The EasyGL siblings have always set it.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanDualTextureNullTexture2Test game;
    game.Run();
    return game.getResult();
}
