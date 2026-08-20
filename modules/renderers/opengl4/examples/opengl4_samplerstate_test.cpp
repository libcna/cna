// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-26: real dynamic SamplerState for OpenGL4's direct 3D draws
// (DrawPrimitivesEx/DrawIndexedPrimitivesEx). A real, now-fixed bug: BindProgramForStride used to
// unconditionally call ApplySamplerState(slot, 0, 1, 1, 1) (Linear + hardcoded Clamp) for every
// bound texture unit AFTER GraphicsDevice::applySamplerStatesToRenderer() had already applied the
// REAL per-slot GraphicsDevice.SamplerStates[slot] value moments earlier -- silently overwriting
// it on every single direct 3D draw, always Clamp, regardless of what SamplerState a game
// actually assigned. Real XNA's own SamplerState default is Linear+Wrap (not Clamp), so the old
// hardcoded value was not just non-dynamic but the wrong default too. Fixed by simply removing
// the redundant/incorrect override calls -- the real per-slot state was already being applied
// correctly by the caller all along; this renderer just needed to stop clobbering it.
//
// Check A ports easygl_sampler_state_effect_test.cpp's own already-cross-renderer-verified proof
// verbatim: a 2-texel red|green pattern texture, UV spanning u=[0,2] (so u=1.25 either wraps to
// the pattern's own left/red texel, or clamps to its right/green edge depending on AddressU),
// sampled with SamplerState::PointWrap explicitly assigned to slot 0 -- must read RED.
// Check B -- the SAME scene with the SamplerState left at its untouched default -- must ALSO read
//   RED, since real XNA's own default AddressU is Wrap, not Clamp (decisive proof the renderer's
//   own default was wrong before this fix, not just that an explicit assignment now works).
// Check C -- SamplerState::PointClamp explicitly assigned -- must read GREEN (the clamped edge),
//   proving Clamp still works correctly when a game genuinely wants it (the fix didn't just
//   flip the bug from always-Clamp to always-Wrap).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class OpenGL4SamplerStateTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D patternTex_; // 2x1: red | green

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Full-screen quad (NDC -1..1), U spans 0..2 (destination x-fraction * 2 = raw u).
        const VertexPositionTexture verts[6] = {
            {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 1.0f)},
            {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f)},
            {Vector3(1.0f, -1.0f, 0.0f), Vector2(2.0f, 0.0f)},
            {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 1.0f)},
            {Vector3(1.0f, -1.0f, 0.0f), Vector2(2.0f, 0.0f)},
            {Vector3(1.0f, 1.0f, 0.0f), Vector2(2.0f, 1.0f)},
        };
        // Sample at destination x-fraction 0.625 (raw u = 1.25).
        const int sampleX = static_cast<int>(kSize * 0.625f);
        const Rectangle sampleReg(sampleX, kSize / 2, 1, 1);

        const auto drawWithSampler = [&](const SamplerState* explicitSampler) {
            dev.Clear(Color(0, 0, 255, 255)); // blue background (neither expected result)
            if (explicitSampler) dev.getSamplerStatesProperty()[0] = *explicitSampler;

            BasicEffect fx(dev);
            fx.setTextureProperty(&patternTex_);
            fx.setTextureEnabledProperty(true);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

            Color px(0, 0, 0, 0);
            dev.GetBackBufferData(&sampleReg, &px, 0, 1);
            return px;
        };

        const auto isRed = [](Color c) { return c.getRProperty() >= 200 && c.getGProperty() <= 50; };
        const auto isGreen = [](Color c) { return c.getGProperty() >= 200 && c.getRProperty() <= 50; };

        const Color withPointWrap = drawWithSampler(&SamplerState::PointWrap);
        Check(isRed(withPointWrap),
              "Check A: SamplerState::PointWrap explicitly assigned -> RED (u=1.25 wraps to the left/red texel)");

        const Color withDefault = drawWithSampler(nullptr);
        Check(isRed(withDefault),
              "Check B: untouched default SamplerState -> ALSO RED (real XNA default AddressU is Wrap, not Clamp)");

        const Color withPointClamp = drawWithSampler(&SamplerState::PointClamp);
        Check(isGreen(withPointClamp),
              "Check C: SamplerState::PointClamp explicitly assigned -> GREEN (u=1.25 clamps to the right/green edge)");
    }

public:
    OpenGL4SamplerStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    void Initialize() override
    {
        PixelTestGame::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        const std::vector<std::uint8_t> pattern = {
            255, 0, 0, 255,   // texel 0: red
            0, 255, 0, 255,   // texel 1: green
        };
        patternTex_ = Texture2D::CreateFromPixels(dev, 2, 1, pattern);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4SamplerStateTest>();
}
