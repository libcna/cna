// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-194: does a SpriteBatch leave its own SamplerState behind in
// GraphicsDevice.SamplerStates[0], so that the next 3D draw samples with it?
//
// What XNA actually does
// ----------------------
// FNA's SpriteBatch.PrepRenderState() begins with
//
//     GraphicsDevice.SamplerStates[0] = samplerState;
//
// and PrepRenderState runs from Begin() for SpriteSortMode.Immediate and from FlushBatch()
// otherwise. So the assignment is a real, observable side effect on the device -- not an internal
// detail -- and it outlives End().
//
// That reading was confirmed on the shipped XNA 4.0 runtime rather than inferred from FNA
// (spikes/xna-spritebatch-sampler0-spike/). The measurement:
//
//     before Begin: SamplerStates[0] = PointClamp
//     after  Begin: SamplerStates[0] = PointClamp     <- NOT published yet, for a Deferred batch
//     after  End  : SamplerStates[0] = PointWrap      <- published at the flush
//     3D draw after the batch, sampled at u = 1.25 -> red, i.e. the batch's Wrap
//
// The "after Begin" row is the one that cannot be guessed: a plausible implementation publishes in
// Begin(), and XNA does not. This file asserts the measured timing, both halves.
//
// How the behaviour half is measured
// ----------------------------------
// A 2x1 texture, texel 0 red and texel 1 green, sampled with POINT filtering at a constant u
// across the whole quad:
//
//     u = 1.25 -> Wrap gives frac(1.25) = 0.25 -> texel 0 -> RED
//              -> Clamp gives 1.0            -> texel 1 -> GREEN
//
// u = 1.5 would have proved nothing: Wrap(1.5) = 0.5 and Clamp(1.5) = 1.0 both land in texel 1.
// Two in-range control legs (u = 0.25 -> red, u = 0.75 -> green) pin the geometry, the texture
// orientation and the point filter, so a wrong answer at u = 1.25 cannot be blamed on those. A
// further negative-control leg re-assigns PointClamp by hand and re-draws at u = 1.25: it must
// come back GREEN. Without it the wrap leg could pass on a renderer that ignores the address mode
// entirely and always wraps.
//
// Renderer-neutral on purpose -- the change under test is in the shared SpriteBatch/GraphicsDevice
// layer, so every family that registers this file answers for itself.
//
// Exit code 0 = every leg PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kN = 64;
} // namespace

class SpriteBatchSampler0PublicationTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    Texture2D                              strip_;   ///< 2x1: texel 0 red, texel 1 green
    Texture2D                              white_;   ///< 1x1, for the sprite the batch draws
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    /// Describes SamplerStates[0] by the three fields this test can distinguish.
    static std::string Describe(const SamplerState& s)
    {
        auto axis = [](TextureAddressMode m) {
            switch (m)
            {
                case TextureAddressMode::Wrap:   return "Wrap";
                case TextureAddressMode::Clamp:  return "Clamp";
                case TextureAddressMode::Mirror: return "Mirror";
                default:                         return "?";
            }
        };
        const char* filter = (s.getFilterProperty() == TextureFilter::Point) ? "Point" : "non-Point";
        return std::string(filter) + "/" + axis(s.getAddressUProperty()) + "," +
               axis(s.getAddressVProperty());
    }

    static bool IsPoint(const SamplerState& s, TextureAddressMode uv)
    {
        return s.getFilterProperty() == TextureFilter::Point &&
               s.getAddressUProperty() == uv && s.getAddressVProperty() == uv;
    }

    /// A full-viewport quad whose u is the SAME at every corner, so the whole quad resolves to one
    /// texel and the centre pixel answers for the address mode on its own.
    Color DrawAtU(GraphicsDevice& dev, float u)
    {
        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(u, 0.5f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(u, 0.5f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(u, 0.5f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(u, 0.5f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(u, 0.5f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(u, 0.5f) },
        };
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect fx(dev);
        fx.setLightingEnabledProperty(false);
        fx.setVertexColorEnabledProperty(false);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&strip_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const Rectangle reg(kN / 2, kN / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    static bool IsRed(const Color& c)
    {
        return c.getRProperty() >= 180 && c.getGProperty() <= 80;
    }
    static bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 180 && c.getRProperty() <= 80;
    }

    /// Runs one SpriteBatch with `sampler`, and reports SamplerStates[0] right after Begin() and
    /// again after End().
    void RunBatch(GraphicsDevice& dev, SpriteSortMode mode, const SamplerState& sampler,
                  SamplerState& afterBegin, SamplerState& afterEnd)
    {
        sb_->Begin(mode, BlendState::Opaque, &sampler, nullptr, nullptr);
        afterBegin = dev.getSamplerStatesProperty()[0];
        sb_->Draw(white_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();
        afterEnd = dev.getSamplerStatesProperty()[0];
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        const std::vector<std::uint8_t> strip = {
            255, 0, 0, 255,     // texel 0 -- red
            0, 255, 0, 255,     // texel 1 -- green
        };
        strip_ = Texture2D::CreateFromPixels(dev, 2, 1, strip);

        const std::vector<std::uint8_t> one = { 255, 255, 255, 255 };
        white_ = Texture2D::CreateFromPixels(dev, 1, 1, one);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // ---- the two control legs: in range, where Wrap and Clamp must agree ----------------
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        const Color c025 = DrawAtU(dev, 0.25f);
        check(IsRed(c025),
              "control u=0.25 samples texel 0: " + Text(c025) +
                  " (want red; this leg pins the geometry, the texture orientation and the point "
                  "filter, not the address mode)");

        const Color c075 = DrawAtU(dev, 0.75f);
        check(IsGreen(c075),
              "control u=0.75 samples texel 1: " + Text(c075) + " (want green)");

        // ---- negative control: Clamp really is distinguishable at u = 1.25 -------------------
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        const Color clamped = DrawAtU(dev, 1.25f);
        check(IsGreen(clamped),
              "negative control: with SamplerStates[0] = PointClamp, u=1.25 gives " +
                  Text(clamped) +
                  " (want green; a renderer that ignored the address mode and always wrapped would "
                  "give red here and make the wrap leg below meaningless)");

        // ---- the timing half, Deferred ------------------------------------------------------
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        const SamplerState before = dev.getSamplerStatesProperty()[0];
        check(IsPoint(before, TextureAddressMode::Clamp),
              "harness: SamplerStates[0] before Begin() is what the caller assigned -- " +
                  Describe(before));

        SamplerState afterBegin, afterEnd;
        RunBatch(dev, SpriteSortMode::Deferred, SamplerState::PointWrap, afterBegin, afterEnd);

        check(IsPoint(afterBegin, TextureAddressMode::Clamp),
              "Deferred: Begin() does NOT publish the batch sampler -- SamplerStates[0] is still " +
                  Describe(afterBegin) +
                  " (XNA publishes from PrepRenderState, which for Deferred runs at the flush; "
                  "measured on the shipped runtime)");
        check(IsPoint(afterEnd, TextureAddressMode::Wrap),
              "Deferred: the flush publishes it -- SamplerStates[0] after End() is " +
                  Describe(afterEnd) + " (want Point/Wrap,Wrap)");

        // ---- the behaviour half: the next 3D draw inherits it -------------------------------
        const Color inherited = DrawAtU(dev, 1.25f);
        check(IsRed(inherited),
              "a 3D draw after the batch samples with the BATCH's address mode: u=1.25 gives " +
                  Text(inherited) +
                  " (want red, i.e. Wrap; green would mean the batch's SamplerState never reached "
                  "SamplerStates[0])");

        // ---- Immediate publishes in Begin(), because that is where its PrepRenderState runs ---
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        SamplerState immAfterBegin, immAfterEnd;
        RunBatch(dev, SpriteSortMode::Immediate, SamplerState::PointWrap, immAfterBegin,
                 immAfterEnd);
        check(IsPoint(immAfterBegin, TextureAddressMode::Wrap),
              "Immediate: Begin() DOES publish it -- SamplerStates[0] right after Begin() is " +
                  Describe(immAfterBegin) +
                  " (Immediate never reaches a FlushBatch, so PrepRenderState runs in Begin)");
        check(IsPoint(immAfterEnd, TextureAddressMode::Wrap),
              "Immediate: and it is still there after End() -- " + Describe(immAfterEnd));

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    SpriteBatchSampler0PublicationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kN);
        gdm_->setPreferredBackBufferHeightProperty(kN);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SpriteBatchSampler0PublicationTest game;
    game.Run();
    return game.getResult();
}
