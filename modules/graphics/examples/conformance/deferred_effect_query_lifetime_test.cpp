// SPDX-License-Identifier: MS-PL
// An issued draw is consumed when the public call returns: disposing the custom Effect or the
// OcclusionQuery it used before the renderer flushes must neither crash nor change the draw.
// Moved from the retired VULKAN suite (REMED-GFX-075, scenes S4/S5/S8); the texture, render-target,
// cube and SpriteBatch source legs of that file already live in deferred_source_lifetime_test.
//
// XNA 4.0 submits each draw to Direct3D 9 at the call, so a game may dispose an effect right after
// SpriteBatch.End() or an OcclusionQuery right after End(); a renderer that defers its draws must
// behave the same. A renderer that DROPS the draw instead of keeping it fails here just as loudly
// as one that crashes, because every leg reads the pixels back.
//
//   E1 control: a portable tint ShaderEffect stays alive; the sprite is white * tint.
//   E2 the ShaderEffect is destroyed after SpriteBatch.End(), before the backbuffer readback.
//   E3 the same with a RenderTarget2D destination, destroyed before RenderTarget2D.GetData.
//   E4 the ShaderEffect AND the sampled texture are both destroyed before the readback.
//   Q1 an OcclusionQuery is destroyed after End(), before the frame is presented: the queried
//      draw is still in the backbuffer, Present succeeds, and the next frame renders normally.
//
// E-legs need the CNA ShaderEffect extension with a usable portable package (CNA_CNAEXT); Q1 needs
// OcclusionQuery. Each missing capability is reported as [SKIP]; when nothing could run the
// program exits 77.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = no leg could run on this renderer.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/GraphicsCapability.hpp"

#include "common/PixelTestGame.hpp"
#include "common/PortableTintShaderPackage.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kW = 96;
    constexpr int kH = 72;
    /// The tint the custom effect multiplies a white texel by: (204, 102, 51).
    constexpr float kTint[4] = {0.8f, 0.4f, 0.2f, 1.0f};
    const Color kTinted(204, 102, 51, 255);
    const Color kGreen(0, 255, 0, 255);

    bool Near(const Color& got, const Color& want, int tolerance = 3)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tolerance &&
               std::abs(got.getGProperty() - want.getGProperty()) <= tolerance &&
               std::abs(got.getBProperty() - want.getBProperty()) <= tolerance;
    }

    std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }
}

class DeferredEffectQueryLifetimeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> white_;
    int frame_ = 0;
    int result_ = 0;
    int ranLegs_ = 0;
    bool queryLegRunning_ = false;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        if (!ok) result_ = 1;
        ++ranLegs_;
    }

    Color ReadBackBuffer(GraphicsDevice& dev, int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        dev.GetBackBufferData(&region, &px, 0, 1);
        return px;
    }

#if defined(CNA_CNAEXT)
    /// A portable tint effect, or null with a printed reason when this renderer cannot run one.
    std::unique_ptr<ShaderEffect> MakeTintEffect(GraphicsDevice& dev, std::string& why)
    {
        if (!dev.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
        {
            why = "GraphicsCapability::CustomEffects is false";
            return nullptr;
        }
        if (!dev.ExecutesShaderEffectSourceEXT())
        {
            why = "the renderer accepts ShaderEffect source but does not execute it";
            return nullptr;
        }
        const auto package = CNA::Examples::PortableTint::CreatePackage();
        if (!package.selectFor(dev).isUsable())
        {
            why = "no variant of the portable tint package is usable here";
            return nullptr;
        }
        auto effect = std::make_unique<ShaderEffect>(dev, package);
        if (!effect->IsEffectValid())
        {
            why = "the portable tint package did not compile";
            return nullptr;
        }
        return effect;
    }

    /// Queues one tinted sprite into whatever target is bound; the effect is only borrowed.
    void QueueTintedSprite(GraphicsDevice& dev, ShaderEffect& effect, Texture2D& texture,
                           const Rectangle& destination)
    {
        effect.SetUniformVec4("uColor", kTint[0], kTint[1], kTint[2], kTint[3]);
        effect.Apply();
        SpriteBatch batch(dev);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState none = DepthStencilState::None;
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &none, nullptr, &effect);
        batch.Draw(texture, destination, Rectangle(0, 0, 1, 1), Color::White);
        batch.End();
    }

    void RunEffectLegs(GraphicsDevice& dev)
    {
        std::string why;
        auto probe = MakeTintEffect(dev, why);
        if (!probe)
        {
            std::printf("[SKIP] E1-E4 custom ShaderEffect lifetime: %s\n", why.c_str());
            return;
        }
        const Rectangle sprite(20, 15, 50, 40);

        // E1 control.
        dev.Clear(Color::Black);
        QueueTintedSprite(dev, *probe, *white_, sprite);
        const Color control = ReadBackBuffer(dev, 40, 30);
        Check(Near(control, kTinted),
              "E1 control: a live tint ShaderEffect draws white * tint = " + Show(control) +
                  ", expected " + Show(kTinted));
        probe.reset();

        // E2 effect destroyed before the backbuffer readback.
        {
            auto effect = MakeTintEffect(dev, why);
            dev.Clear(Color::Black);
            QueueTintedSprite(dev, *effect, *white_, sprite);
            effect.reset();
            const Color got = ReadBackBuffer(dev, 40, 30);
            const Color outside = ReadBackBuffer(dev, 5, 5);
            Check(Near(got, kTinted) && Near(outside, Color::Black),
                  "E2 effect destroyed after End(), before the backbuffer flush: still tinted " +
                      Show(got) + ", outside " + Show(outside));
        }

        // E3 render-target destination, effect destroyed before RenderTarget2D.GetData.
        {
            RenderTarget2D target(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            auto effect = MakeTintEffect(dev, why);
            dev.SetRenderTarget(&target);
            dev.Clear(Color::Black);
            QueueTintedSprite(dev, *effect, *white_, sprite);
            effect.reset();
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<Color> pixels(static_cast<std::size_t>(kW * kH), Color(0, 0, 0, 0));
            target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
            const Color got = pixels[static_cast<std::size_t>(30 * kW + 40)];
            Check(Near(got, kTinted),
                  "E3 effect destroyed before RenderTarget2D.GetData: target still tinted " +
                      Show(got));
        }

        // E4 effect AND texture destroyed before the flush.
        {
            auto effect = MakeTintEffect(dev, why);
            auto texture = std::make_unique<Texture2D>(dev, 1, 1);
            const Color white(255, 255, 255, 255);
            texture->SetData(&white, 1);
            dev.Clear(Color::Black);
            QueueTintedSprite(dev, *effect, *texture, sprite);
            effect.reset();
            texture.reset();
            const Color got = ReadBackBuffer(dev, 40, 30);
            Check(Near(got, kTinted),
                  "E4 effect and texture both destroyed before the flush: still tinted " +
                      Show(got));
        }
    }
#endif

    void DrawGreenQuad(GraphicsDevice& dev, BasicEffect& effect)
    {
        const VertexPositionColor quad[6] = {
            {Vector3(-1.f, 1.f, 0.5f), kGreen}, {Vector3(-1.f, -1.f, 0.5f), kGreen},
            {Vector3(1.f, -1.f, 0.5f), kGreen}, {Vector3(-1.f, 1.f, 0.5f), kGreen},
            {Vector3(1.f, -1.f, 0.5f), kGreen}, {Vector3(1.f, 1.f, 0.5f), kGreen},
        };
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }

    /// Q1, first frame: returns true when the leg is running and needs the next frame.
    bool StartQueryLeg(GraphicsDevice& dev)
    {
        if (!dev.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery))
        {
            std::printf("[SKIP] Q1 OcclusionQuery lifetime: GraphicsCapability::OcclusionQuery "
                        "is false\n");
            return false;
        }
        BasicEffect effect(dev);
        effect.setVertexColorEnabledProperty(true);
        auto query = std::make_unique<OcclusionQuery>(dev);
        dev.Clear(Color::Black);
        query->Begin();
        DrawGreenQuad(dev, effect);
        query->End();
        query.reset();
        const Color got = ReadBackBuffer(dev, kW / 2, kH / 2);
        Check(Near(got, kGreen),
              "Q1 OcclusionQuery destroyed after End(): the queried draw is still in the "
              "backbuffer " + Show(got));
        return true;  // Game::EndDraw presents this frame with the query already gone
    }

    void FinishQueryLeg(GraphicsDevice& dev)
    {
        BasicEffect effect(dev);
        effect.setVertexColorEnabledProperty(true);
        dev.Clear(Color(0, 0, 255, 255));
        DrawGreenQuad(dev, effect);
        const Color got = ReadBackBuffer(dev, kW / 2, kH / 2);
        Check(Near(got, kGreen),
              "Q1 the frame after presenting the dead query renders normally " + Show(got));
    }

protected:
    void LoadContent() override
    {
        white_ = std::make_unique<Texture2D>(getGraphicsDeviceProperty(), 1, 1);
        const Color white(255, 255, 255, 255);
        white_->SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        ++frame_;
        if (frame_ == 1)
        {
#if defined(CNA_CNAEXT)
            RunEffectLegs(dev);
#else
            std::printf("[SKIP] E1-E4 custom ShaderEffect lifetime: built without CNA_CNAEXT\n");
#endif
            queryLegRunning_ = StartQueryLeg(dev);
            if (!queryLegRunning_)
                Exit();
            return;
        }
        if (frame_ == 2 && queryLegRunning_)
            FinishQueryLeg(dev);
        Exit();
    }

public:
    DeferredEffectQueryLifetimeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kW);
        gdm_->setPreferredBackBufferHeightProperty(kH);
    }

    int getResult() const
    {
        if (result_ != 0) return 1;
        return ranLegs_ == 0 ? CNA::Examples::kSkipExitCode : 0;
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("deferred_effect_query_lifetime"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    DeferredEffectQueryLifetimeTest game;
    game.Run();
    return game.getResult();
}
