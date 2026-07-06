// SPDX-License-Identifier: MS-PL
// Task 387: verify DualTextureEffect's second texture (`Texture2`, slot 1) null behavior on
// Bgfx -- REAL BUG FOUND AND FIXED HERE. See
// examples/easygl_dualtextureeffect_null_texture2_test.cpp for the full derivation.
//
// Bgfx's `texColor3DSampler2_` (slot 1) had NO fallback at all: every one of Task 379's 7
// fixed call sites used `texColor3DSampler_` (slot 0); this second-slot site was explicitly
// left unfixed and flagged in Task 379's own writeup for whoever next touched
// DualTextureEffect. A null `Texture2` draw silently left whatever texture the *previous*
// draw's slot 1 had bound. Fixed by adding an else-branch mirroring slot 0's own Task 379
// fix exactly (`bgfx::setTexture(1, texColor3DSampler2_, defaultWhiteTexture3D_, ...)`).
//
// Per Task 364's finding (tracked as Task 884, not fixed there or here): Bgfx's default
// RasterizerState cull state is the only one of the 3 backends that actually matches FNA's
// real CullCounterClockwiseFace default, so it silently culls the standard NDC quad winding
// used throughout this pixel-test family unless RasterizerState::CullNone is set
// explicitly -- worked around here identically to prior Bgfx tests.
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

static constexpr int kSize = 64;

class BgfxDualTextureNullTexture2Test : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            ++fail_;
        }
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, Texture2D* tex2,
                      const VertexPositionTexture (&quad)[6])
    {
        DualTextureEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setTexture2Property(tex2);
        fx.Apply();

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            // See Task 364/884 finding: Bgfx's default cull state culls this quad's winding,
            // unlike EasyGL/Vulkan's own defaults.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        const Color kDistinctivePrev(20, 200, 20, 255); // "previous draw" texture2
        const Color kTex(80, 40, 120, 255);             // non-saturated Texture (slot 0)

        Texture2D tex(dev, 1, 1);      tex.SetData(&kTex, 1);
        Texture2D texPrev(dev, 1, 1);  texPrev.SetData(&kDistinctivePrev, 1);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionTexture quad[6] = {
            { tl, uv0 }, { bl, uv1 }, { br, uv2 },
            { tl, uv0 }, { br, uv2 }, { tr, uv3 },
        };

        // First draw: Texture + real, distinctive texture2 -- establishes "previous draw" state.
        renderWith(dev, tex, &texPrev, quad);

        // Second draw: Texture=kTex, Texture2=null -- the actual behavior under test.
        const Color got = renderWith(dev, tex, nullptr, quad);

        check(colourMatch(got, Color(160, 80, 240, 255)),
              "Texture2=null falls back to white (not the previous draw's texture)",
              got, Color(160, 80, 240, 255));
        check(!colourMatch(got, kDistinctivePrev),
              "Texture2=null: pixel != previous draw's texture (proves no stale-state leak)",
              got, kDistinctivePrev);

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxDualTextureNullTexture2Test()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxDualTextureNullTexture2Test game;
    game.Run();
    return game.getResult();
}
