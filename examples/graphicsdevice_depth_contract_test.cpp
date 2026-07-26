// SPDX-License-Identifier: MS-PL
// REMED-GFX-089: backend-neutral public GraphicsDevice depth contract.
//
// Uses only normal XNA/CNA objects and draw calls: DepthStencilState, GraphicsDevice,
// BasicEffect, DrawUserPrimitives, and backbuffer readback. The canonical state is
// DepthStencilState::Default:
//   DepthBufferEnable=true
//   DepthBufferWriteEnable=true
//   DepthBufferFunction=CompareFunction::LessEqual
//
// A: Default, near RED z=0.2 then far GREEN z=0.8 -> RED (far rejected).
// B: DepthRead, same order -> GREEN (near did not write depth, so far also passes clear depth=1).
// C: None, same order -> GREEN (depth disabled; painter's order).
// A2: Default again -> RED (A->B->C->A state restoration).
// REMED-GFX-030 closes Software's former fixed-LessEqual/write-on-pass boundary, so all backends
// registered for this shared public contract now assert all four outcomes without a skip.
//
// Identity World/View/Projection means clip-space W=1 and post-divide depths remain 0.2/0.8;
// the default Viewport MinDepth/MaxDepth=0/1 maps them unchanged. Opaque blending, all colour
// channels, CullNone, flat vertex colours, no texture/fog/lighting.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kClear(10, 10, 10, 255);
    const Color kNear(255, 0, 0, 255);
    const Color kFar(0, 255, 0, 255);

    void DrawFullQuad(GraphicsDevice& device, float z, const Color& color)
    {
        const VertexPositionColor vertices[6] = {
            {Vector3(-1.0f,  1.0f, z), color},
            {Vector3(-1.0f, -1.0f, z), color},
            {Vector3( 1.0f, -1.0f, z), color},
            {Vector3(-1.0f,  1.0f, z), color},
            {Vector3( 1.0f, -1.0f, z), color},
            {Vector3( 1.0f,  1.0f, z), color},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    bool IsExact(const Color& value, const Color& expected)
    {
        return value.getRProperty() == expected.getRProperty() &&
               value.getGProperty() == expected.getGProperty() &&
               value.getBProperty() == expected.getBProperty() &&
               value.getAProperty() == expected.getAProperty();
    }
}

class GraphicsDeviceDepthContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    bool done_ = false;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        GraphicsDevice& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;
        effect.Apply();

        const Rectangle probe(kSize / 2, kSize / 2, 1, 1);
        auto render = [&](const DepthStencilState& state) {
            device.setDepthStencilStateProperty(state);
            device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
            DrawFullQuad(device, 0.2f, kNear);
            DrawFullQuad(device, 0.8f, kFar);
            Color pixel(0, 0, 0, 0);
            device.GetBackBufferData(&probe, &pixel, 0, 1);
            return pixel;
        };

        const Color defaultA = render(DepthStencilState::Default);
        const Color depthRead = render(DepthStencilState::DepthRead);
        const Color none = render(DepthStencilState::None);
        const Color defaultA2 = render(DepthStencilState::Default);

        int passed = 0;
        constexpr int expected = 4;
        auto check = [&](bool ok, const char* label) {
            std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
            if (ok) ++passed;
        };
        check(IsExact(defaultA, kNear),
              "A Default: near red writes depth and rejects farther green");
        check(IsExact(depthRead, kFar),
              "B DepthRead: near does not write depth, so farther green passes clear depth");
        check(IsExact(none, kFar),
              "C None: depth test disabled, so farther green wins by painter's order");
        check(IsExact(defaultA2, kNear),
              "A2 Default: complete state restoration reproduces the first near-red result");

        const Viewport& viewport = device.getViewportProperty();
        std::printf(
            "[GFX089] target=backbuffer %dx%d depth=Depth24Stencil8 samples=0 "
            "viewport=%d,%d %dx%d %.1f..%.1f clear=(10,10,10,255)/1.0 "
            "zNear=0.2 zFar=0.8 pixels A=%d/%d B=%d/%d C=%d/%d A2=%d/%d\n",
            kSize, kSize,
            viewport.getXProperty(), viewport.getYProperty(),
            viewport.getWidthProperty(), viewport.getHeightProperty(),
            viewport.getMinDepthProperty(), viewport.getMaxDepthProperty(),
            defaultA.getRProperty(), defaultA.getGProperty(),
            depthRead.getRProperty(), depthRead.getGProperty(),
            none.getRProperty(), none.getGProperty(),
            defaultA2.getRProperty(), defaultA2.getGProperty());
        std::printf("=== %d/%d PASS ===\n", passed, expected);

        result_ = passed == expected ? 0 : 1;
        Exit();
    }

public:
    GraphicsDeviceDepthContractTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
        graphicsDeviceManager_->setPreferredDepthStencilFormatProperty(
            DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    GraphicsDeviceDepthContractTest game;
    game.Run();
    return game.getResult();
}
