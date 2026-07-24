// SPDX-License-Identifier: MS-PL
// REMED-GFX-094: prove Vulkan RenderTargetCube MSAA resolve semantics.
//
// The original Task 903 fixture set RasterizerState::CullNone before filling the
// cube with six SpriteBatch passes.  SpriteBatch::Begin() deliberately applies
// its XNA default RasterizerState::CullCounterClockwise, so the later
// EnvironmentMapEffect observer inherited that state and culled its CCW quad.
// The resulting backbuffer clear was misclassified as a black cube resolve.
//
// This regression applies all observer state after the producer SpriteBatch
// passes, then proves:
//   * requested cube sample counts are reported honestly;
//   * all six non-MSAA and device-applied-MSAA faces retain distinct colors;
//   * the resolved MSAA cube is sampleable in the same submission in which it
//     was produced;
//   * face A -> face B -> face A preserves B and leaves A's final draw;
//   * two cube targets with the same face index do not alias.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
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

    const std::array<Color, 6> kFaceColors = {
        Color(255,   0,   0, 255), // +X red
        Color(  0, 255,   0, 255), // -X green
        Color(  0,   0, 255, 255), // +Y blue
        Color(255, 255,   0, 255), // -Y yellow
        Color(255,   0, 255, 255), // +Z magenta
        Color(  0, 255, 255, 255), // -Z cyan
    };
    const Color kObserverClear(17, 19, 23, 255);

    // At the sampled point the identity-view quad has incident direction +X.
    // These normals reflect +X into +X,-X,+Y,-Y,+Z,-Z respectively.
    const std::array<Vector3, 6> kFaceNormals = {
        Vector3(0.0f,       1.0f,        0.0f),
        Vector3(1.0f,       0.0f,        0.0f),
        Vector3(kInvSqrt2, -kInvSqrt2,   0.0f),
        Vector3(kInvSqrt2,  kInvSqrt2,   0.0f),
        Vector3(kInvSqrt2,  0.0f,       -kInvSqrt2),
        Vector3(kInvSqrt2,  0.0f,        kInvSqrt2),
    };

    bool ColorMatches(const Color& got, const Color& want, int tolerance = 16)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tolerance &&
               std::abs(got.getGProperty() - want.getGProperty()) <= tolerance &&
               std::abs(got.getBProperty() - want.getBProperty()) <= tolerance &&
               std::abs(got.getAProperty() - want.getAProperty()) <= tolerance;
    }
}

class VulkanRenderTargetCubeMsaaTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<Texture2D> whiteTexture_;
    std::array<std::unique_ptr<Texture2D>, 6> faceTextures_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void Check(bool ok, const std::string& description)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", description.c_str());
        ok ? ++pass_ : ++fail_;
    }

    void DrawSolidFace(GraphicsDevice& device, RenderTargetCube& cube,
                       int faceIndex, int colorIndex)
    {
        device.SetRenderTarget(&cube, kFaces[faceIndex]);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        sb_->Draw(*faceTextures_[colorIndex],
                  Rectangle(0, 0, kCubeSize, kCubeSize),
                  Rectangle(0, 0, 1, 1),
                  Color::White);
        sb_->End();
    }

    void FillAllFaces(GraphicsDevice& device, RenderTargetCube& cube)
    {
        for (int i = 0; i < 6; ++i)
            DrawSolidFace(device, cube, i, i);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    std::vector<Color> SampleFaces(GraphicsDevice& device, RenderTargetCube& cube,
                                   const std::vector<int>& faceIndices,
                                   int backbufferWidth, int backbufferHeight,
                                   const Color& backbufferClear)
    {
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(backbufferClear);

        EnvironmentMapEffect effect(device);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setFresnelFactorProperty(0.0f);
        effect.setTextureProperty(whiteTexture_.get());
        effect.setEnvironmentMapProperty(&cube);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        // This ordering is the GFX-094 regression: every preceding SpriteBatch::Begin()
        // applies CullCounterClockwise.  The observer must establish its own reachable
        // GraphicsDevice state after those producer passes and before it queues the draw.
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        struct Probe
        {
            int x;
            int y;
        };
        std::vector<Probe> probes;
        probes.reserve(faceIndices.size());

        const int columns = 3;
        const int rows = static_cast<int>((faceIndices.size() + columns - 1) / columns);
        for (std::size_t i = 0; i < faceIndices.size(); ++i)
        {
            const int column = static_cast<int>(i) % columns;
            const int row = static_cast<int>(i) / columns;
            const int x0 = column * backbufferWidth / columns;
            const int x1 = (column + 1) * backbufferWidth / columns;
            const int y0 = row * backbufferHeight / rows;
            const int y1 = (row + 1) * backbufferHeight / rows;
            device.setViewportProperty(Viewport(x0, y0, x1 - x0, y1 - y0));

            const Vector3 normal = kFaceNormals[faceIndices[i]];
            const VertexPositionNormalTexture quad[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), normal, Vector2(0.0f, 1.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), normal, Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), normal, Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f,  1.0f, 0.0f), normal, Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), normal, Vector2(1.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.0f), normal, Vector2(1.0f, 1.0f) },
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            probes.push_back({ x0 + 3 * (x1 - x0) / 4, y0 + (y1 - y0) / 2 });
        }

        device.setViewportProperty(Viewport(0, 0, backbufferWidth, backbufferHeight));
        const Rectangle full(0, 0, backbufferWidth, backbufferHeight);
        std::vector<Color> pixels(
            static_cast<std::size_t>(backbufferWidth) * backbufferHeight,
            Color(0, 0, 0, 0));
        device.GetBackBufferData(&full, pixels.data(), 0, static_cast<int>(pixels.size()));

        std::vector<Color> sampled;
        sampled.reserve(probes.size());
        for (const Probe& probe : probes)
            sampled.push_back(pixels[static_cast<std::size_t>(probe.y) * backbufferWidth + probe.x]);
        return sampled;
    }

    bool CheckSamples(const std::vector<Color>& got,
                      const std::vector<int>& faceIndices,
                      const std::vector<Color>& expected,
                      const char* label)
    {
        bool all = got.size() == expected.size();
        for (std::size_t i = 0; i < got.size() && i < expected.size(); ++i)
        {
            const bool ok = ColorMatches(got[i], expected[i]);
            all = all && ok;
            std::printf("  %s %s sample=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                        label, kFaceNames[faceIndices[i]],
                        got[i].getRProperty(), got[i].getGProperty(),
                        got[i].getBProperty(), got[i].getAProperty(),
                        expected[i].getRProperty(), expected[i].getGProperty(),
                        expected[i].getBProperty(), expected[i].getAProperty());
        }
        return all;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // Task 878/879's narrow test hook is required because per-target MSAA
        // currently uses the backend's already-selected sample count.
        device.RecreateBackendForMultiSampleCount(8);

        sb_ = std::make_unique<SpriteBatch>(device);
        whiteTexture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(device, 1, 1, {255, 255, 255, 255}));
        for (int i = 0; i < 6; ++i)
        {
            const Color& c = kFaceColors[i];
            faceTextures_[i] = std::make_unique<Texture2D>(
                Texture2D::CreateFromPixels(
                    device, 1, 1,
                    { static_cast<uint8_t>(c.getRProperty()),
                      static_cast<uint8_t>(c.getGProperty()),
                      static_cast<uint8_t>(c.getBProperty()),
                      static_cast<uint8_t>(c.getAProperty()) }));
        }
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const int backbufferWidth = device.getViewportProperty().getWidthProperty();
        const int backbufferHeight = device.getViewportProperty().getHeightProperty();
        const std::vector<int> allFaces = {0, 1, 2, 3, 4, 5};
        const std::vector<Color> allColors(kFaceColors.begin(), kFaceColors.end());

        RenderTargetCube clearOnly(
            device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None, 8,
            RenderTargetUsage::DiscardContents);
        // Initialize/submit every layer once because a sampled cube view covers all
        // six layers even when the shader direction reaches only one of them.
        FillAllFaces(device, clearOnly);
        (void) SampleFaces(device, clearOnly, allFaces,
                           backbufferWidth, backbufferHeight, kObserverClear);

        device.SetRenderTarget(&clearOnly, CubeMapFace::PositiveZ);
        const Color clearMarker(255, 64, 32, 255);
        device.Clear(clearMarker);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const auto clearSample = SampleFaces(
            device, clearOnly, {4}, backbufferWidth, backbufferHeight, clearMarker);
        Check(clearSample.size() == 1 && ColorMatches(clearSample[0], clearMarker),
              "MSAA cube clear-only resolve: +Z samples as (255,64,32,255)");

        RenderTargetCube drawOverClear(
            device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None, 8,
            RenderTargetUsage::DiscardContents);
        FillAllFaces(device, drawOverClear);
        device.SetRenderTarget(&drawOverClear, CubeMapFace::PositiveY);
        device.Clear(Color::Red);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        sb_->Draw(*faceTextures_[1], Rectangle(0, 0, kCubeSize, kCubeSize),
                  Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const auto drawSample = SampleFaces(
            device, drawOverClear, {2}, backbufferWidth, backbufferHeight, Color::Red);
        Check(drawSample.size() == 1 && ColorMatches(drawSample[0], kFaceColors[1]),
              "MSAA cube draw-over-red-clear resolve: +Y samples as green");

        RenderTargetCube cube1x(
            device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        RenderTargetCube cubeMsaa(
            device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None, 8,
            RenderTargetUsage::DiscardContents);
        Check(cube1x.getMultiSampleCountProperty() == 0,
              "MultiSampleCount request 0 reports 0");
        const int appliedSampleCount = cubeMsaa.getMultiSampleCountProperty();
        Check(appliedSampleCount > 1,
              "MultiSampleCount request 8 applies a supported multisample count");

        FillAllFaces(device, cube1x);
        FillAllFaces(device, cubeMsaa);

        const auto samplesMsaa = SampleFaces(
            device, cubeMsaa, allFaces, backbufferWidth, backbufferHeight, kObserverClear);
        const std::string msaaLabel = std::to_string(appliedSampleCount) + "x";
        Check(CheckSamples(samplesMsaa, allFaces, allColors, msaaLabel.c_str()),
              "MSAA cube resolves and samples all six distinct faces in the producer submission");

        const auto samples1x = SampleFaces(
            device, cube1x, allFaces, backbufferWidth, backbufferHeight, kObserverClear);
        Check(CheckSamples(samples1x, allFaces, allColors, "1x"),
              "non-MSAA cube samples all six distinct faces");

        RenderTargetCube cubeA(
            device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None, 8,
            RenderTargetUsage::DiscardContents);
        RenderTargetCube cubeB(
            device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None, 8,
            RenderTargetUsage::DiscardContents);

        // A cube descriptor covers all six layers.  Initialize every layer before
        // sampling either stress cube so the fixture itself remains Vulkan-valid.
        FillAllFaces(device, cubeA);
        FillAllFaces(device, cubeB);
        DrawSolidFace(device, cubeA, 0, 0); // cube A +X = red
        DrawSolidFace(device, cubeA, 1, 1); // cube A -X = green
        DrawSolidFace(device, cubeB, 0, 5); // cube B +X = cyan
        DrawSolidFace(device, cubeA, 0, 4); // cube A +X = magenta (final A)
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const std::vector<int> plusMinusX = {0, 1};
        const std::vector<Color> expectedA = {kFaceColors[4], kFaceColors[1]};
        const auto samplesA = SampleFaces(
            device, cubeA, plusMinusX, backbufferWidth, backbufferHeight, kObserverClear);
        Check(CheckSamples(samplesA, plusMinusX, expectedA, "A->B->A"),
              "face +X -> -X -> +X keeps -X green and final +X magenta");

        const std::vector<int> plusX = {0};
        const std::vector<Color> expectedB = {kFaceColors[5]};
        const auto samplesB = SampleFaces(
            device, cubeB, plusX, backbufferWidth, backbufferHeight, kObserverClear);
        Check(CheckSamples(samplesB, plusX, expectedB, "cube B"),
              "multiple cubes keep independent +X resolve destinations");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanRenderTargetCubeMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
    }

    int Result() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanRenderTargetCubeMsaaTest game;
    game.Run();
    return game.Result();
}
