// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-50: real-device proof that GraphicsDevice.ReferenceStencil is used by
// the Diligent renderer independently of the currently-assigned DepthStencilState's own
// ReferenceStencil field, mirroring Task 319's cross-renderer test
// (examples/easygl_graphicsdevice_reference_stencil_test.cpp).
//
// FNA's GraphicsDevice.ReferenceStencil is a real, independent device property
// (FNA3D_Get/SetReferenceStencil), analogous to GraphicsDevice.BlendFactor -- it can be changed
// WITHOUT reassigning the whole DepthStencilState, and that change should immediately affect
// subsequent stencil compares.
//
// Method: stamp stencil=0x05, then assign a DepthStencilState with StencilFunction=Equal and a
// baked-in ReferenceStencil=0x05 (so, taken at face value, a compare would PASS). Then call
// GraphicsDevice.setReferenceStencilProperty(0x99) DIRECTLY -- NOT via a new DepthStencilState --
// which should override the ACTIVE reference used by the next draw's stencil compare. Draw a GREEN
// quad using the SAME (unchanged) DepthStencilState object: if the override genuinely took effect,
// the compare becomes 0x99 vs buffer 0x05 (Equal, false) -> REJECTED -> stays BACKGROUND. If
// setReferenceStencilProperty has no real effect, the compare still uses the state's own baked-in
// 0x05 vs buffer 0x05 -> PASSES -> incorrectly shows GREEN.
//
// Unlike EasyGL/Bgfx (Task 872's still-open, universal "no renderer connection at all" gap), this
// renderer's DiligentRenderer::SetReferenceStencil() writes the same referenceStencil_ member
// every draw path re-reads into IDeviceContext::SetStencilRef() before each draw (see
// DiligentRenderer.cpp), so this test is expected to genuinely PASS here, not just document
// a known-broken gap the way the EasyGL version of this test does.
//
// Exit code 0 = PASS (correct override behavior), 1 = FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    const Color kBackground(20, 20, 20, 255);
    const Color kGreen(0, 255, 0, 255);

    void DrawQuad(GraphicsDevice& device, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            {Vector3(-1.0f, 1.0f, 0.5f), color},
            {Vector3(-1.0f, -1.0f, 0.5f), color},
            {Vector3(1.0f, -1.0f, 0.5f), color},
            {Vector3(-1.0f, 1.0f, 0.5f), color},
            {Vector3(1.0f, -1.0f, 0.5f), color},
            {Vector3(1.0f, 1.0f, 0.5f), color},
        };
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class DiligentReferenceStencilTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    bool done_ = false;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBackground, 1.0f, 0);
        device.setBlendStateProperty(BlendState::Opaque);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;
        effect.Apply();

        // Stamp stencil=0x05.
        DepthStencilState stamp;
        stamp.setDepthBufferEnableProperty(false);
        stamp.setStencilEnableProperty(true);
        stamp.setStencilFunctionProperty(CompareFunction::Always);
        stamp.setStencilPassProperty(StencilOperation::Replace);
        stamp.setReferenceStencilProperty(0x05);
        device.setDepthStencilStateProperty(stamp);
        DrawQuad(device, kBackground);

        // A compare state with a baked-in ReferenceStencil=0x05 (would PASS at face value).
        DepthStencilState compare;
        compare.setDepthBufferEnableProperty(false);
        compare.setStencilEnableProperty(true);
        compare.setStencilFunctionProperty(CompareFunction::Equal);
        compare.setReferenceStencilProperty(0x05);
        compare.setStencilPassProperty(StencilOperation::Keep);
        compare.setStencilFailProperty(StencilOperation::Keep);
        device.setDepthStencilStateProperty(compare);

        // Override the ACTIVE reference directly -- should make the next draw's compare use 0x99,
        // not the state object's own 0x05.
        device.setReferenceStencilProperty(0x99);

        DrawQuad(device, kGreen);

        Rectangle region(viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);

        const bool isGreen = pixel.getGProperty() >= 200 && pixel.getRProperty() <= 60 &&
                             pixel.getBProperty() <= 60;
        const bool ok = !isGreen;

        std::printf("[%s] centre=(%d,%d,%d), expected BACKGROUND (override reference 0x99 must "
                    "reject)\n",
                    ok ? "PASS" : "FAIL", pixel.getRProperty(), pixel.getGProperty(),
                    pixel.getBProperty());

        result_ = ok ? 0 : 1;
        Exit();
    }

public:
    DiligentReferenceStencilTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
        graphicsDeviceManager_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentReferenceStencilTest game;
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
