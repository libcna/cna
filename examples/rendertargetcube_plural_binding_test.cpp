// SPDX-License-Identifier: MS-PL
// REMED-GFX-096: backend-neutral public regression for the plural cube-face handoff.
//
// The singular calls initialize all six faces. The operation under test then binds
// exactly one RenderTargetBinding selecting NegativeZ through SetRenderTargets,
// draws a distinctive marker, unbinds, and samples all faces through the ordinary
// TextureCube consumer path. Only NegativeZ may change.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kCubeSize = 32;
    constexpr float kInvSqrt2 = 0.7071067811865475f;

    const std::array<CubeMapFace, 6> kFaces = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };

    const std::array<const char*, 6> kFaceNames = {
        "+X", "-X", "+Y", "-Y", "+Z", "-Z",
    };

    const std::array<Color, 6> kInitialColors = {
        Color(160,  20,  30, 255),
        Color( 30, 170,  40, 255),
        Color( 40,  50, 180, 255),
        Color(190, 180,  30, 255),
        Color(180,  40, 170, 255),
        Color( 35, 175, 185, 255),
    };

    const Color kNegativeZMarker(245, 95, 25, 255);

    // At each probe point the identity-view quad has incident direction +X.
    // These normals reflect +X into +X, -X, +Y, -Y, +Z, and -Z.
    const std::array<Vector3, 6> kFaceNormals = {
        Vector3(0.0f,       1.0f,        0.0f),
        Vector3(1.0f,       0.0f,        0.0f),
        Vector3(kInvSqrt2, -kInvSqrt2,   0.0f),
        Vector3(kInvSqrt2,  kInvSqrt2,   0.0f),
        Vector3(kInvSqrt2,  0.0f,       -kInvSqrt2),
        Vector3(kInvSqrt2,  0.0f,        kInvSqrt2),
    };

    bool Matches(const Color& got, const Color& expected, int tolerance = 16)
    {
        return std::abs(got.getRProperty() - expected.getRProperty()) <= tolerance
            && std::abs(got.getGProperty() - expected.getGProperty()) <= tolerance
            && std::abs(got.getBProperty() - expected.getBProperty()) <= tolerance
            && std::abs(got.getAProperty() - expected.getAProperty()) <= tolerance;
    }
}

class RenderTargetCubePluralBindingTest final : public Game
{
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> white_;
    bool done_ = false;
    int result_ = 1;

    void DrawFace(RenderTargetCube& cube, CubeMapFace face, const Color& color,
                  bool plural)
    {
        auto& device = getGraphicsDeviceProperty();
        if (plural)
        {
            device.SetRenderTargets({
                RenderTargetBinding(static_cast<Texture*>(&cube), face)
            });
        }
        else
        {
            device.SetRenderTarget(&cube, face);
        }

        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                            &noDepth, &noCull);
        spriteBatch_->Draw(*white_, Rectangle(0, 0, kCubeSize, kCubeSize),
                           Rectangle(0, 0, 1, 1), color);
        spriteBatch_->End();
    }

    std::array<Color, 6> SampleFaces(RenderTargetCube& cube)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets({});

        const int width = device.getViewportProperty().getWidthProperty();
        const int height = device.getViewportProperty().getHeightProperty();
        device.Clear(Color(7, 11, 13, 255));

        EnvironmentMapEffect effect(device);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setFresnelFactorProperty(0.0f);
        effect.setTextureProperty(white_.get());
        effect.setEnvironmentMapProperty(&cube);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        // GFX-094 state-hygiene requirement: establish all observer state after
        // the producer SpriteBatch calls and effect Apply.
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        struct Probe { int x; int y; };
        std::array<Probe, 6> probes{};
        for (int i = 0; i < 6; ++i)
        {
            const int column = i % 3;
            const int row = i / 3;
            const int x0 = column * width / 3;
            const int x1 = (column + 1) * width / 3;
            const int y0 = row * height / 2;
            const int y1 = (row + 1) * height / 2;
            device.setViewportProperty(Viewport(x0, y0, x1 - x0, y1 - y0));

            const Vector3 normal = kFaceNormals[i];
            const VertexPositionNormalTexture quad[6] = {
                {Vector3(-1,  1, 0), normal, Vector2(0, 1)},
                {Vector3(-1, -1, 0), normal, Vector2(0, 0)},
                {Vector3( 1, -1, 0), normal, Vector2(1, 0)},
                {Vector3(-1,  1, 0), normal, Vector2(0, 1)},
                {Vector3( 1, -1, 0), normal, Vector2(1, 0)},
                {Vector3( 1,  1, 0), normal, Vector2(1, 1)},
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            probes[i] = {x0 + 3 * (x1 - x0) / 4, y0 + (y1 - y0) / 2};
        }

        device.setViewportProperty(Viewport(0, 0, width, height));
        std::vector<Color> pixels(static_cast<std::size_t>(width) * height,
                                  Color(0, 0, 0, 0));
        const Rectangle full(0, 0, width, height);
        device.GetBackBufferData(&full, pixels.data(), 0,
                                 static_cast<int>(pixels.size()));

        std::array<Color, 6> result = {
            Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0),
            Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0),
        };
        for (int i = 0; i < 6; ++i)
            result[i] = pixels[static_cast<std::size_t>(probes[i].y) * width
                               + probes[i].x];
        return result;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTargetCube cube(device, kCubeSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);

        for (int i = 0; i < 6; ++i)
            DrawFace(cube, kFaces[i], kInitialColors[i], false);

        // The minimal GFX-096 reproducer: one cube-face binding through the
        // plural API. No other render target participates.
        DrawFace(cube, CubeMapFace::NegativeZ, kNegativeZMarker, true);

        const auto sampled = SampleFaces(cube);
        bool pass = true;
        for (int i = 0; i < 6; ++i)
        {
            const Color& expected = i == 5 ? kNegativeZMarker : kInitialColors[i];
            const bool facePass = Matches(sampled[i], expected);
            pass = pass && facePass;
            std::printf("[%s] face %s got=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                        facePass ? "PASS" : "FAIL", kFaceNames[i],
                        sampled[i].getRProperty(), sampled[i].getGProperty(),
                        sampled[i].getBProperty(), sampled[i].getAProperty(),
                        expected.getRProperty(), expected.getGProperty(),
                        expected.getBProperty(), expected.getAProperty());
        }

        std::printf("[%s] plural one-binding NegativeZ changes only layer 5\n",
                    pass ? "PASS" : "FAIL");
        result_ = pass ? 0 : 1;
        Exit();
    }

public:
    int Result() const { return result_; }
};

int main()
{
    RenderTargetCubePluralBindingTest game;
    game.Run();
    return game.Result();
}
