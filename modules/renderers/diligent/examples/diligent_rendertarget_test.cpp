// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-20/DILIGENT-21: real-device proof that RenderTarget2D on the Diligent
// renderer renders off-screen and reads back, through the public XNA API only.
//
// Check A -- a Clear() while a render target is bound lands in the target, read back via
//   RenderTarget2D::GetData.
// Check B -- ...and the back buffer was NOT touched by it. This is what separates a real
//   off-screen target from a renderer that quietly kept drawing to the back buffer: check A alone
//   passes in both worlds.
// Check C -- a SpriteBatch quad drawn into the target covers only its own destination rectangle
//   inside the target (partial coverage, so a full-target overwrite fails).
// Check D -- after unbinding, the target's colour is intact and can be sampled as a texture: it is
//   drawn to the back buffer and read back from there.
// Check E -- the target has its own depth buffer: a near quad drawn BEFORE a far one still wins
//   inside the target, which a target without depth storage cannot do.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

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
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

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
    constexpr int kTargetSize = 32;
    constexpr int kChecks = 5;

    bool ColorNear(Color a, Color b, int tolerance = 12)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    Color ReadBackBufferPixel(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    Color ReadTargetPixel(RenderTarget2D& target, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        target.GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }
}

class DiligentRenderTargetTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D whiteTexture_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

    void DrawFullTargetQuad(GraphicsDevice& device, float z, const Color& color)
    {
        const VertexPositionColor vertices[6] = {
            {Vector3(-1.0f, 1.0f, z), color},  {Vector3(-1.0f, -1.0f, z), color},
            {Vector3(1.0f, -1.0f, z), color},  {Vector3(-1.0f, 1.0f, z), color},
            {Vector3(1.0f, -1.0f, z), color},  {Vector3(1.0f, 1.0f, z), color},
        };
        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = new SpriteBatch(getGraphicsDeviceProperty());
        whiteTexture_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                    std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        RenderTarget2D target(device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8);

        device.Clear(Color::Black);
        device.SetRenderTargets({RenderTargetBinding(&target)});
        device.Clear(Color::Red);
        device.SetRenderTargets({});

        Check(ColorNear(ReadTargetPixel(target, kTargetSize / 2, kTargetSize / 2), Color::Red),
              "Clear() while a render target is bound lands in the target");
        Check(ColorNear(ReadBackBufferPixel(device, kSize / 2, kSize / 2), Color::Black),
              "...and the back buffer was left untouched by it");

        device.SetRenderTargets({RenderTargetBinding(&target)});
        device.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(whiteTexture_, Rectangle(0, 0, kTargetSize / 2, kTargetSize),
                           Color::Blue);
        spriteBatch_->End();
        device.SetRenderTargets({});

        const bool insideIsBlue =
            ColorNear(ReadTargetPixel(target, kTargetSize / 4, kTargetSize / 2), Color::Blue);
        const bool outsideIsBlack = ColorNear(
            ReadTargetPixel(target, kTargetSize - kTargetSize / 4, kTargetSize / 2), Color::Black);
        Check(insideIsBlue && outsideIsBlack,
              "SpriteBatch into the target covers only its destination rectangle");

        device.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(target, Rectangle(0, 0, kSize, kSize), Color::White);
        spriteBatch_->End();
        Check(ColorNear(ReadBackBufferPixel(device, kSize / 4, kSize / 2), Color::Blue),
              "the unbound target is sampleable as a texture on the back buffer");

        // SpriteBatch leaves its own RasterizerState behind (XNA semantics: it sets
        // CullCounterClockwise on Begin and does not restore), so the 3D checks below have to
        // re-establish CullNone or the quads are culled.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.SetRenderTargets({RenderTargetBinding(&target)});
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        DrawFullTargetQuad(device, 0.1f, Color::Lime);
        DrawFullTargetQuad(device, 0.9f, Color::Red);
        device.SetRenderTargets({});
        device.setDepthStencilStateProperty(DepthStencilState::None);

        const Color depthResult = ReadTargetPixel(target, kTargetSize / 2, kTargetSize / 2);
        Check(ColorNear(depthResult, Color::Lime),
              "the target has real depth storage: the near quad drawn first still wins");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentRenderTargetTest()
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
        DiligentRenderTargetTest game;
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
