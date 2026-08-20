// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-31/DILIGENT-32: real-device proof for AlphaTestEffect's per-pixel
// discard and for BasicEffect's fog, through the public XNA API only.
//
// Check A -- AlphaTestEffect with CompareFunction::Greater and a reference above the surface's
//   alpha discards the pixel: the cleared background shows through where the quad is.
// Check B -- the same effect with a reference below the surface's alpha keeps it. A and B together
//   are what prove a real per-pixel test rather than "nothing was ever drawn" or "nothing was ever
//   discarded"; either check alone passes in one of those degenerate worlds.
// Check C -- BasicEffect fog at a depth well inside the fog range pulls the surface colour toward
//   FogColor...
// Check D -- ...while the same geometry with fog disabled keeps its original colour, so C cannot
//   pass by accident on a renderer that simply renders something dark.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

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
    constexpr int kChecks = 4;

    bool ColorNear(Color a, Color b, int tolerance = 14)
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
}

class DiligentAlphaTestFogTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    Texture2D opaqueWhite_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

    /// A full-screen quad at clip-space depth @p z, carrying @p color as its vertex colour.
    static void DrawQuad(GraphicsDevice& device, float z, const Color& color)
    {
        const VertexPositionColorTexture vertices[6] = {
            {Vector3(-1.0f, 1.0f, z), color, Vector2(0.0f, 0.0f)},
            {Vector3(-1.0f, -1.0f, z), color, Vector2(0.0f, 1.0f)},
            {Vector3(1.0f, -1.0f, z), color, Vector2(1.0f, 1.0f)},
            {Vector3(-1.0f, 1.0f, z), color, Vector2(0.0f, 0.0f)},
            {Vector3(1.0f, -1.0f, z), color, Vector2(1.0f, 1.0f)},
            {Vector3(1.0f, 1.0f, z), color, Vector2(1.0f, 0.0f)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

protected:
    void LoadContent() override
    {
        opaqueWhite_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                   std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);

        AlphaTestEffect alphaTest(device);
        alphaTest.setWorldProperty(Matrix::getIdentityProperty());
        alphaTest.setViewProperty(Matrix::getIdentityProperty());
        alphaTest.setProjectionProperty(Matrix::getIdentityProperty());
        alphaTest.setTextureProperty(&opaqueWhite_);
        alphaTest.setVertexColorEnabledProperty(true);
        alphaTest.setAlphaFunctionProperty(CompareFunction::Greater);

        // The surface's own alpha is 0.5 (128/255): a reference above it must discard, one below
        // it must keep, and both use the very same geometry and effect object.
        const Color halfAlphaBlue(0, 0, 255, 128);

        device.Clear(Color::Red);
        alphaTest.setReferenceAlphaProperty(200);
        alphaTest.Apply();
        DrawQuad(device, 0.5f, halfAlphaBlue);
        Check(ColorNear(ReadCenter(device), Color::Red),
              "AlphaTestEffect discards where alpha fails the reference (background shows through)");

        device.Clear(Color::Red);
        alphaTest.setReferenceAlphaProperty(50);
        alphaTest.Apply();
        DrawQuad(device, 0.5f, halfAlphaBlue);
        Check(ColorNear(ReadCenter(device), Color::Blue),
              "the same effect keeps the pixel when the alpha passes the reference");

        // Fog: FNA's fog vector is built from World*View, so what matters is the surface's
        // view-space depth. The quad below sits 4 units in front of the camera, past FogEnd = 3,
        // so it is fully fogged. A real orthographic projection is needed here (unlike the
        // clip-space quads above): with an identity projection a quad pushed back 4 units simply
        // falls outside the clip volume and renders nothing at all.
        BasicEffect fogEffect(device);
        fogEffect.setWorldProperty(Matrix::getIdentityProperty());
        fogEffect.setViewProperty(Matrix::getIdentityProperty());
        fogEffect.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f));
        fogEffect.VertexColorEnabled = true;
        fogEffect.setTextureEnabledProperty(false);
        fogEffect.setFogEnabledProperty(true);
        fogEffect.setFogColorProperty(Vector3(1.0f, 1.0f, 0.0f));
        fogEffect.setFogStartProperty(1.0f);
        fogEffect.setFogEndProperty(3.0f);

        device.Clear(Color::Black);
        fogEffect.Apply();
        DrawQuad(device, -4.0f, Color::Blue);
        const Color fogged = ReadCenter(device);
        std::printf("    (fully fogged blue -> R=%d G=%d B=%d)\n", fogged.getRProperty(),
                    fogged.getGProperty(), fogged.getBProperty());
        Check(ColorNear(fogged, Color::Yellow, 20),
              "fog pulls a fully-fogged surface to FogColor");

        device.Clear(Color::Black);
        fogEffect.setFogEnabledProperty(false);
        fogEffect.Apply();
        DrawQuad(device, -4.0f, Color::Blue);
        Check(ColorNear(ReadCenter(device), Color::Blue),
              "the same geometry with fog disabled keeps its own colour");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentAlphaTestFogTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentAlphaTestFogTest game;
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
