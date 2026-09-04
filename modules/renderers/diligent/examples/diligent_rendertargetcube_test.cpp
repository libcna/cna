// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-22: real-device proof that RenderTargetCube on the Diligent renderer
// renders into individual faces and can be sampled back, through the public XNA API only.
//
// Check A -- rendering a distinct clear colour into face 0 (+X) and reading it back via
//   RenderTargetCube::GetData shows that colour.
// Check B -- the same for face 2 (+Y), a DIFFERENT colour than face 0's. A and B together prove
//   per-face storage rather than one shared colour: a renderer that aliased every face to one
//   image would pass either check alone but not both with different colours.
// Check C -- SpriteBatch drawn into one face covers only its destination rectangle inside that
//   face, exactly like the RenderTarget2D case, proving ordinary 2D drawing works through the
//   per-face render-target view too.
// Check D -- EnvironmentMapEffect sampling this cube (instead of a plain TextureCube, as in
//   Diligent_DualTextureEnvMap) picks up face 0's rendered colour via reflection. This is the real
//   point of a cube render target: the whole reason to make one is to sample it back as an
//   environment map, and this renderer's EnvironmentMapEffect dispatch (DILIGENT-34) accepts a
//   RenderTargetCube through the same DiligentSampledTexture interface a plain TextureCube uses.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

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
    constexpr int kFaceSize = 16;
    constexpr int kChecks = 4;

    bool ColorNear(Color a, Color b, int tolerance = 20)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    Color ReadFacePixel(RenderTargetCube& cube, CubeMapFace face, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        cube.GetData(face, 0, &region, &pixel, 0, 1);
        return pixel;
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class DiligentRenderTargetCubeTest : public Game
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
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);

        RenderTargetCube cube(device, kFaceSize, false, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8);

        device.SetRenderTarget(&cube, CubeMapFace::PositiveX);
        device.Clear(Color::Red);
        device.SetRenderTarget(nullptr);

        device.SetRenderTarget(&cube, CubeMapFace::PositiveY);
        device.Clear(Color::Lime);
        device.SetRenderTarget(nullptr);

        Check(ColorNear(ReadFacePixel(cube, CubeMapFace::PositiveX, kFaceSize / 2, kFaceSize / 2),
                        Color::Red),
              "face +X was cleared red and reads back red");
        Check(ColorNear(ReadFacePixel(cube, CubeMapFace::PositiveY, kFaceSize / 2, kFaceSize / 2),
                        Color::Lime),
              "face +Y was cleared a DIFFERENT colour (green) and reads back green, not red");

        device.SetRenderTarget(&cube, CubeMapFace::PositiveZ);
        device.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(whiteTexture_, Rectangle(0, 0, kFaceSize / 2, kFaceSize), Color::Blue);
        spriteBatch_->End();
        device.SetRenderTarget(nullptr);

        const bool insideIsBlue = ColorNear(
            ReadFacePixel(cube, CubeMapFace::PositiveZ, kFaceSize / 4, kFaceSize / 2), Color::Blue);
        const bool outsideIsBlack = ColorNear(
            ReadFacePixel(cube, CubeMapFace::PositiveZ, kFaceSize - kFaceSize / 4, kFaceSize / 2),
            Color::Black);
        Check(insideIsBlue && outsideIsBlack,
              "SpriteBatch into face +Z covers only its destination rectangle");

        // Check D deliberately does not try to predict which XNA CubeMapFace a given reflection
        // direction samples in this renderer's HLSL -- that mapping is real but untested here, and
        // getting it wrong would make this check assert a coincidence instead of the thing it is
        // meant to prove. Every face is set to the SAME colour instead, so the check is genuinely
        // "does sampling this render target return rendered content at all", independent of which
        // face direction resolves to.
        for (int face = 0; face < 6; ++face)
        {
            device.SetRenderTarget(&cube, static_cast<CubeMapFace>(face));
            device.Clear(Color::Red);
        }
        device.SetRenderTarget(nullptr);

        // SpriteBatch leaves its own RasterizerState behind (XNA semantics: it sets
        // CullCounterClockwise on Begin and does not restore -- see
        // diligent_rendertarget_test.cpp's identical note), so the 3D draw below has to
        // re-establish CullNone or the quad is culled and never reaches the render target at all.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture vertices[6] = {
            {Vector3(-1.0f, 1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(-1.0f, -1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(1.0f, -1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(-1.0f, 1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(1.0f, -1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(1.0f, 1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
        };

        EnvironmentMapEffect envMap(device);
        envMap.setWorldProperty(Matrix::getIdentityProperty());
        envMap.setViewProperty(Matrix::getIdentityProperty());
        envMap.setProjectionProperty(Matrix::getIdentityProperty());
        envMap.setTextureProperty(&whiteTexture_);
        envMap.setEnvironmentMapProperty(&cube);
        envMap.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        envMap.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        envMap.setFresnelFactorProperty(0.0f);
        envMap.setEnvironmentMapAmountProperty(1.0f);

        device.Clear(Color::Black);
        envMap.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        const Color reflected = ReadCenter(device);
        std::printf("    (EnvironmentMapEffect sampling the render target -> R=%d G=%d B=%d)\n",
                    reflected.getRProperty(), reflected.getGProperty(), reflected.getBProperty());
        Check(ColorNear(reflected, Color::Red),
              "EnvironmentMapEffect reflects the render target's own rendered content");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentRenderTargetCubeTest()
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
        DiligentRenderTargetCubeTest game;
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
