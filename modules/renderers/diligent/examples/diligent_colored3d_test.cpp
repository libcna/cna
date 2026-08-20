// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-16: real-GPU pixel proof for the Diligent renderer's 3D draw path,
// through the public BasicEffect / DrawUserPrimitives API rather than the renderer interface.
//
// Check A -- non-indexed DrawUserPrimitives renders a vertex-coloured quad.
// Check B -- indexed DrawUserIndexedPrimitives renders one too (a separate code path: it binds an
//   index buffer and issues DrawIndexed).
// Checks C/D -- a real depth test, proven in both draw orders. Drawing the near quad last and
//   still seeing it only proves the draws happened in order; drawing it FIRST and still seeing it
//   is what distinguishes a working depth buffer from a painter's algorithm.
// Check E -- a textured quad samples its texture (stride-20 vertex layout and the textured shader
//   variant, as opposed to check A's stride-16 vertex-colour one).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device (a skip, see the CTest
// registration in cmake/Tests/DiligentTests.cmake).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kChecks = 6;

    bool ColorNear(Color a, Color b, int tolerance = 12)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    BasicEffect& SharedEffect(GraphicsDevice& device)
    {
        static BasicEffect* effect = nullptr;
        if (effect == nullptr)
            effect = new BasicEffect(device);
        effect->setWorldProperty(Matrix::getIdentityProperty());
        effect->setViewProperty(Matrix::getIdentityProperty());
        effect->setProjectionProperty(Matrix::getIdentityProperty());
        return *effect;
    }

    /// Full-screen quad in clip space at depth @p z (0 = near, 1 = far), single solid colour.
    void DrawFullScreenQuad(GraphicsDevice& device, float z, const Color& color)
    {
        const VertexPositionColor vertices[6] = {
            {Vector3(-1.0f, 1.0f, z), color},  {Vector3(-1.0f, -1.0f, z), color},
            {Vector3(1.0f, -1.0f, z), color},  {Vector3(-1.0f, 1.0f, z), color},
            {Vector3(1.0f, -1.0f, z), color},  {Vector3(1.0f, 1.0f, z), color},
        };
        BasicEffect& effect = SharedEffect(device);
        effect.VertexColorEnabled = true;
        effect.setTextureEnabledProperty(false);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }
}

class DiligentColored3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    Texture2D blueTexture_;
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
        blueTexture_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                   std::vector<std::uint8_t>{0, 0, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        device.Clear(Color::Black);
        DrawFullScreenQuad(device, 0.5f, Color::Red);
        Check(ColorNear(ReadCenter(device), Color::Red),
              "DrawUserPrimitives renders a vertex-coloured quad");

        device.Clear(Color::Black);
        {
            const VertexPositionColor vertices[4] = {
                {Vector3(-1.0f, 1.0f, 0.5f), Color::Blue},
                {Vector3(1.0f, 1.0f, 0.5f), Color::Blue},
                {Vector3(-1.0f, -1.0f, 0.5f), Color::Blue},
                {Vector3(1.0f, -1.0f, 0.5f), Color::Blue},
            };
            const std::uint16_t indices[6] = {0, 1, 2, 1, 3, 2};
            BasicEffect& effect = SharedEffect(device);
            effect.VertexColorEnabled = true;
            effect.setTextureEnabledProperty(false);
            effect.Apply();
            device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 4, indices,
                                             0, 2);
        }
        Check(ColorNear(ReadCenter(device), Color::Blue),
              "DrawUserIndexedPrimitives renders an indexed quad");

        device.setDepthStencilStateProperty(DepthStencilState::Default);

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        DrawFullScreenQuad(device, 0.9f, Color::Red);
        DrawFullScreenQuad(device, 0.1f, Color::Lime);
        Check(ColorNear(ReadCenter(device), Color::Lime),
              "depth test: near quad drawn after the far one wins");

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        DrawFullScreenQuad(device, 0.1f, Color::Lime);
        DrawFullScreenQuad(device, 0.9f, Color::Red);
        Check(ColorNear(ReadCenter(device), Color::Lime),
              "depth test: near quad drawn BEFORE the far one still wins");

        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(Color::Black);
        {
            const VertexPositionTexture vertices[6] = {
                {Vector3(-1.0f, 1.0f, 0.5f), Vector2(0.0f, 0.0f)},
                {Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f)},
                {Vector3(1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f)},
                {Vector3(-1.0f, 1.0f, 0.5f), Vector2(0.0f, 0.0f)},
                {Vector3(1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f)},
                {Vector3(1.0f, 1.0f, 0.5f), Vector2(1.0f, 0.0f)},
            };
            BasicEffect& effect = SharedEffect(device);
            effect.VertexColorEnabled = false;
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(&blueTexture_);
            effect.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        }
        Check(ColorNear(ReadCenter(device), Color::Blue),
              "textured quad samples its texture (stride-20 layout, textured shader variant)");

        // DILIGENT-46: SetStringMarkerEXT is debug-only (no rendering effect) -- the only thing to
        // prove is that inserting a label around a draw doesn't disturb it. Bracket the same
        // vertex-coloured draw as check A with markers and confirm it still renders correctly.
        device.Clear(Color::Black);
        device.SetStringMarkerEXT("diligent_colored3d_test: before quad");
        DrawFullScreenQuad(device, 0.5f, Color::Red);
        device.SetStringMarkerEXT("diligent_colored3d_test: after quad");
        device.SetStringMarkerEXT(""); // empty marker: documented no-op, must not throw either
        Check(ColorNear(ReadCenter(device), Color::Red),
              "SetStringMarkerEXT around a draw does not disturb it");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentColored3DTest()
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
        DiligentColored3DTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        // Only a genuine "there is no device here" failure is a skip. Any other exception is a
        // real defect and must fail: an over-broad catch turned a renderer bug into a green skip
        // once already while this test was being written.
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
