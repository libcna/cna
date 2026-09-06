// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-058 -- does a SpriteBatch honour the DepthStencilState its Begin()
// installs?
//
// `SpriteBatch::Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState)`
// takes the state, and `SpriteBatch::Begin` really does apply it to the device (a null argument
// resolving to `DepthStencilState::None`, as FNA does). What this file measures is whether the
// renderer below then uses it. Stencil-masking sprites -- write a stencil pattern with geometry,
// then let a SpriteBatch fill only the marked region -- is an ordinary XNA idiom, and it is the
// case a hardcoded "sprites never test anything" quietly turns into a full-screen fill.
//
// Renderer-agnostic and registered on Vulkan AND EasyGL, because the question is comparative: this
// began as a candidate finding from VULKAN-096's read of the 2D pipeline, and it was the EasyGL run
// that turned the candidate into a defect. Measured before the fix: leg A passed on both, leg B
// gave the masked left half on EasyGL and the WHOLE target on Vulkan.
//
//   A  The control, and the leg that makes B mean anything: the stencil pattern is really in the
//      buffer, proved by a 3D draw that tests against it. If A fails, B is measuring the stencil
//      WRITE and not the sprite path.
//   B  The same pattern, consumed by a SpriteBatch. Only the marked half may be painted.
//   C  Depth. A sprite's quad is emitted at z = 0, so `CompareFunction::Less` must reject it
//      exactly where geometry has already written 0 and accept it where the buffer still holds the
//      cleared 1 -- which also proves the test is a real comparison rather than an on/off switch.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
constexpr int kN = 8;
const Color kClear(0, 0, 0, 255);
const Color kMarked(0, 255, 0, 255);
const Color kSprite(255, 0, 0, 255);
const Color kBlocker(0, 0, 255, 255);
}  // namespace

class SpriteBatchDepthStencilStateTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> white_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    static bool Is(const Color& got, const Color& want)
    {
        return got.getRProperty() == want.getRProperty() &&
               got.getGProperty() == want.getGProperty() &&
               got.getBProperty() == want.getBProperty();
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    /// A screen-space quad spanning [x0, x1] horizontally and the whole target vertically.
    void DrawQuad(float x0, float x1, const Color& c)
    {
        auto& dev = getGraphicsDeviceProperty();
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        const VertexPositionColor t[6] = {
            { Vector3(x0,  1.f, 0.f), c }, { Vector3(x1,  1.f, 0.f), c },
            { Vector3(x0, -1.f, 0.f), c }, { Vector3(x1,  1.f, 0.f), c },
            { Vector3(x1, -1.f, 0.f), c }, { Vector3(x0, -1.f, 0.f), c } };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, t, 0, 2);
    }

    void DrawFullTargetSprite(const DepthStencilState& state)
    {
        auto& dev = getGraphicsDeviceProperty();
        SamplerState point = SamplerState::PointClamp;
        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &state, nullptr);
        sb.Draw(*white_, Rectangle(0, 0, kN, kN), Rectangle(0, 0, 2, 2), kSprite);
        sb.End();
    }

    static DepthStencilState StencilWrite()
    {
        DepthStencilState s;
        s.setDepthBufferEnableProperty(false);
        s.setDepthBufferWriteEnableProperty(false);
        s.setStencilEnableProperty(true);
        s.setStencilFunctionProperty(CompareFunction::Always);
        s.setStencilPassProperty(StencilOperation::Replace);
        s.setReferenceStencilProperty(1);
        return s;
    }

    static DepthStencilState StencilTest()
    {
        DepthStencilState s;
        s.setDepthBufferEnableProperty(false);
        s.setDepthBufferWriteEnableProperty(false);
        s.setStencilEnableProperty(true);
        s.setStencilFunctionProperty(CompareFunction::Equal);
        s.setStencilPassProperty(StencilOperation::Keep);
        s.setReferenceStencilProperty(1);
        return s;
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        white_ = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<std::uint8_t, 16> px{ 255, 255, 255, 255, 255, 255, 255, 255,
                                               255, 255, 255, 255, 255, 255, 255, 255 };
        white_->SetDataRGBA(px.data(), 4);

        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color,
                          DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        // One texel inside each half, never on the seam.
        const std::size_t kLeft  = 1;
        const std::size_t kRight = static_cast<std::size_t>(kN - 2);

        const auto beginPass = [&] {
            dev.SetRenderTarget(&rt);
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                      kClear, 1.0f, 0);
            dev.setBlendStateProperty(BlendState::Opaque);
        };
        const auto endPass = [&] {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            rt.GetData(p.data(), 0, kN * kN);
        };

        // A. The control. The stencil pattern must really be in the buffer, or leg B is measuring
        //    the write rather than the sprite.
        beginPass();
        dev.setDepthStencilStateProperty(StencilWrite());
        DrawQuad(-1.f, 0.f, kClear);                       // stencil := 1 on the left half
        dev.setDepthStencilStateProperty(StencilTest());
        DrawQuad(-1.f, 1.f, kMarked);                      // painted only where stencil == 1
        endPass();
        check(Is(p[kLeft], kMarked) && Is(p[kRight], kClear),
              "A control: a stencil-tested 3D draw paints only the marked half, left=" +
                  Text(p[kLeft]) + " right=" + Text(p[kRight]) + " (want " + Text(kMarked) +
                  " and " + Text(kClear) + ")");

        // B. The same stencil, consumed by a SpriteBatch. Before VULKAN-058 this painted both
        //    halves on Vulkan, because the 2D pipeline hardcoded no stencil at all.
        beginPass();
        dev.setDepthStencilStateProperty(StencilWrite());
        DrawQuad(-1.f, 0.f, kClear);                       // stencil := 1 on the left half
        DrawFullTargetSprite(StencilTest());
        endPass();
        check(Is(p[kLeft], kSprite) && Is(p[kRight], kClear),
              "B SpriteBatch honours Begin()'s stencil state: left=" + Text(p[kLeft]) +
                  " right=" + Text(p[kRight]) + " (want " + Text(kSprite) + " and " +
                  Text(kClear) + "; both halves painted means the state was discarded)");

        // C. Depth, and a real comparison rather than an on/off switch: a sprite's quad is at
        //    z = 0, so Less rejects it where geometry wrote 0 and accepts it where the buffer
        //    still holds the cleared 1.
        DepthStencilState depthWrite;
        depthWrite.setDepthBufferEnableProperty(true);
        depthWrite.setDepthBufferWriteEnableProperty(true);
        depthWrite.setDepthBufferFunctionProperty(CompareFunction::LessEqual);
        DepthStencilState depthTestLess;
        depthTestLess.setDepthBufferEnableProperty(true);
        depthTestLess.setDepthBufferWriteEnableProperty(false);
        depthTestLess.setDepthBufferFunctionProperty(CompareFunction::Less);

        beginPass();
        dev.setDepthStencilStateProperty(depthWrite);
        DrawQuad(-1.f, 0.f, kBlocker);                     // depth := 0 on the left half
        DrawFullTargetSprite(depthTestLess);
        endPass();
        check(Is(p[kLeft], kBlocker) && Is(p[kRight], kSprite),
              "C SpriteBatch honours Begin()'s depth comparison: left=" + Text(p[kLeft]) +
                  " right=" + Text(p[kRight]) + " (want " + Text(kBlocker) + " and " +
                  Text(kSprite) + ")");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        white_.reset();
        Exit();
    }

public:
    SpriteBatchDepthStencilStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SpriteBatchDepthStencilStateTest game;
    game.Run();
    return game.getResult();
}
