// SPDX-License-Identifier: MS-PL
//
// plan_diligent.md DILIGENT-33/DILIGENT-34: real-device proof for DualTextureEffect's two-layer
// modulate and for EnvironmentMapEffect's cube-map reflection, through the public XNA API only.
//
// Check A -- DualTextureEffect with a mid-grey second layer leaves the first layer's colour
//   essentially unchanged (XNA doubles the first layer before the modulate, so 2 * c * 0.5 = c).
// Check B -- the same first layer with a BLUE second layer loses its red channel entirely. A and B
//   together prove both samplers are genuinely read: a backend sampling only the first layer
//   passes A and fails B, one sampling only the second fails A.
// Check C -- EnvironmentMapEffect with EnvironmentMapAmount = 1 and a solid cube map takes the
//   cube's colour, not the diffuse texture's.
// Check D -- the same geometry with EnvironmentMapAmount = 0 shows the lit diffuse colour instead,
//   so C cannot pass on a backend that always samples the cube.
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
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
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
    constexpr int kChecks = 4;

    bool ColorNear(Color a, Color b, int tolerance = 20)
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

    Texture2D SolidTexture(GraphicsDevice& device, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        return Texture2D::CreateFromPixels(device, 1, 1, std::vector<std::uint8_t>{r, g, b, 255});
    }
}

class DiligentDualTextureEnvMapTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    Texture2D redTexture_;
    Texture2D greyTexture_;
    Texture2D blueTexture_;
    Texture2D whiteTexture_;
    std::unique_ptr<TextureCube> greenCube_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

    /// Full-screen quad with texture coordinates, for the dual-texture (stride 20) path.
    static void DrawTexturedQuad(GraphicsDevice& device)
    {
        const VertexPositionTexture vertices[6] = {
            {Vector3(-1.0f, 1.0f, 0.5f), Vector2(0.5f, 0.5f)},
            {Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.5f, 0.5f)},
            {Vector3(1.0f, -1.0f, 0.5f), Vector2(0.5f, 0.5f)},
            {Vector3(-1.0f, 1.0f, 0.5f), Vector2(0.5f, 0.5f)},
            {Vector3(1.0f, -1.0f, 0.5f), Vector2(0.5f, 0.5f)},
            {Vector3(1.0f, 1.0f, 0.5f), Vector2(0.5f, 0.5f)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    /// Full-screen quad with a normal pointing straight at the camera, for the env-map (stride 32)
    /// path: the reflection vector is then the -Z face of the cube.
    static void DrawLitQuad(GraphicsDevice& device)
    {
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture vertices[6] = {
            {Vector3(-1.0f, 1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(-1.0f, -1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(1.0f, -1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(-1.0f, 1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(1.0f, -1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
            {Vector3(1.0f, 1.0f, 0.5f), normal, Vector2(0.5f, 0.5f)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        redTexture_ = SolidTexture(device, 255, 0, 0);
        greyTexture_ = SolidTexture(device, 128, 128, 128);
        blueTexture_ = SolidTexture(device, 0, 0, 255);
        whiteTexture_ = SolidTexture(device, 255, 255, 255);

        greenCube_ = std::make_unique<TextureCube>(device, 1, false, SurfaceFormat::Color);
        const std::vector<Color> face{Color(0, 255, 0, 255)};
        for (int i = 0; i < 6; ++i)
            greenCube_->SetData(static_cast<CubeMapFace>(i), face.data(), 0, 1);
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

        DualTextureEffect dualTexture(device);
        dualTexture.setWorldProperty(Matrix::getIdentityProperty());
        dualTexture.setViewProperty(Matrix::getIdentityProperty());
        dualTexture.setProjectionProperty(Matrix::getIdentityProperty());
        dualTexture.setVertexColorEnabledProperty(false);
        dualTexture.setTextureProperty(&redTexture_);

        device.Clear(Color::Black);
        dualTexture.setTexture2Property(&greyTexture_);
        dualTexture.Apply();
        DrawTexturedQuad(device);
        const Color modulatedByGrey = ReadCenter(device);
        std::printf("    (red x 2 x mid-grey -> R=%d G=%d B=%d)\n", modulatedByGrey.getRProperty(),
                    modulatedByGrey.getGProperty(), modulatedByGrey.getBProperty());
        Check(ColorNear(modulatedByGrey, Color::Red, 24),
              "DualTextureEffect: a mid-grey second layer leaves the doubled first layer as-is");

        device.Clear(Color::Black);
        dualTexture.setTexture2Property(&blueTexture_);
        dualTexture.Apply();
        DrawTexturedQuad(device);
        Check(ColorNear(ReadCenter(device), Color::Black),
              "DualTextureEffect: a blue second layer zeroes the red first layer (both sampled)");

        EnvironmentMapEffect envMap(device);
        envMap.setWorldProperty(Matrix::getIdentityProperty());
        envMap.setViewProperty(Matrix::getIdentityProperty());
        envMap.setProjectionProperty(Matrix::getIdentityProperty());
        envMap.setTextureProperty(&redTexture_);
        envMap.setEnvironmentMapProperty(greenCube_.get());
        envMap.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        envMap.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        envMap.setFresnelFactorProperty(0.0f);

        device.Clear(Color::Black);
        envMap.setEnvironmentMapAmountProperty(1.0f);
        envMap.Apply();
        DrawLitQuad(device);
        const Color reflected = ReadCenter(device);
        std::printf("    (full env-map reflection -> R=%d G=%d B=%d)\n", reflected.getRProperty(),
                    reflected.getGProperty(), reflected.getBProperty());
        Check(ColorNear(reflected, Color::Lime, 24),
              "EnvironmentMapEffect: amount 1 takes the cube map's colour");

        device.Clear(Color::Black);
        envMap.setEnvironmentMapAmountProperty(0.0f);
        envMap.Apply();
        DrawLitQuad(device);
        const Color unreflected = ReadCenter(device);
        std::printf("    (no reflection -> R=%d G=%d B=%d)\n", unreflected.getRProperty(),
                    unreflected.getGProperty(), unreflected.getBProperty());
        Check(unreflected.getRProperty() > 128 && unreflected.getGProperty() < 96,
              "EnvironmentMapEffect: amount 0 shows the lit diffuse texture instead");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentDualTextureEnvMapTest()
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
        DiligentDualTextureEnvMapTest game;
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
