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
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
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
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kCubeSize = 32;
    constexpr float kInvSqrt2 = 0.7071067811865475f;
#if defined(CNA_GFX096_FORCE_CUBE_DEPTH)
    constexpr DepthFormat kCubeDepthFormat = DepthFormat::Depth24Stencil8;
#else
    constexpr DepthFormat kCubeDepthFormat = DepthFormat::None;
#endif

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

    const std::array<Color, 12> kDrawColors = {
        kInitialColors[0], kInitialColors[1], kInitialColors[2],
        kInitialColors[3], kInitialColors[4], kInitialColors[5],
        Color(245,  95,  25, 255), // minimal reproducer -Z marker
        Color(220,  35,  45, 255), // A -> B -> A first +X
        Color( 35, 220,  55, 255), // A -> B -> A -X
        Color( 45,  65, 230, 255), // A -> B -> A final +X
        Color(235, 155,  35, 255), // multiple-cube Cube A +X
        Color(125,  45, 225, 255), // multiple-cube Cube B +X
    };

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

    Color ExpectedStoredColor(const Color& linear)
    {
#if defined(CNA_GFX096_EXPECT_SRGB_ENCODED)
        auto encode = [](int channel) {
            const float value = static_cast<float>(channel) / 255.0f;
            const float encoded = value <= 0.0031308f
                ? 12.92f * value
                : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
            return static_cast<int>(std::lround(encoded * 255.0f));
        };
        return Color(encode(linear.getRProperty()),
                     encode(linear.getGProperty()),
                     encode(linear.getBProperty()),
                     static_cast<int>(linear.getAProperty()));
#else
        return linear;
#endif
    }
}

class RenderTargetCubePluralBindingTest final : public Game
{
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> white_;
    std::array<std::unique_ptr<Texture2D>, kDrawColors.size()> solidTextures_;
    bool done_ = false;
    int pass_ = 0;
    int total_ = 0;
    int result_ = 1;
#if defined(CNA_GFX096_STAGED_SINGLE_FACE)
    int stagedFrame_ = 0;
    std::unique_ptr<RenderTargetCube> stagedSingular_;
    std::unique_ptr<RenderTargetCube> stagedPlural_;
    bool stagedBindingState_ = false;
#endif

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        ++total_;
        if (ok) ++pass_;
    }

    void DrawFace(RenderTargetCube& cube, CubeMapFace face, int colorIndex,
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
        spriteBatch_->Draw(*solidTextures_[colorIndex],
                           Rectangle(0, 0, kCubeSize, kCubeSize),
                           Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
    }

    std::array<Color, 6> SampleFaces(RenderTargetCube& cube)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets({});

#if defined(CNA_GFX096_CUBE_GETDATA)
        // SDL_GPU and WebGPU expose exact per-face RenderTargetCube readback.
        // Use that stronger subresource oracle (SDL_GPU intentionally has no
        // swapchain ReadBackbuffer implementation).
        std::array<Color, 6> result = {
            Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0),
            Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0),
        };
        const Rectangle centre(kCubeSize / 2, kCubeSize / 2, 1, 1);
        for (int i = 0; i < 6; ++i)
            cube.GetData(kFaces[i], 0, &centre, &result[i], 0, 1);
        return result;
#else
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
#endif
    }

    bool FacesMatch(const std::array<Color, 6>& got,
                    const std::array<Color, 6>& expected,
                    const char* label)
    {
        bool pass = true;
        for (int i = 0; i < 6; ++i)
        {
            const Color storedExpected = ExpectedStoredColor(expected[i]);
            const bool facePass = Matches(got[i], storedExpected);
            pass = pass && facePass;
            std::printf("  [%s] %s face %s got=(%d,%d,%d,%d) "
                        "expected=(%d,%d,%d,%d)\n",
                        facePass ? "PASS" : "FAIL", label, kFaceNames[i],
                        got[i].getRProperty(), got[i].getGProperty(),
                        got[i].getBProperty(), got[i].getAProperty(),
                        storedExpected.getRProperty(),
                        storedExpected.getGProperty(),
                        storedExpected.getBProperty(),
                        storedExpected.getAProperty());
        }
        return pass;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
        for (std::size_t i = 0; i < solidTextures_.size(); ++i)
        {
            const Color& color = kDrawColors[i];
            solidTextures_[i] = std::make_unique<Texture2D>(
                Texture2D::CreateFromPixels(
                    device, 1, 1,
                    std::vector<std::uint8_t>{
                        static_cast<std::uint8_t>(color.getRProperty()),
                        static_cast<std::uint8_t>(color.getGProperty()),
                        static_cast<std::uint8_t>(color.getBProperty()),
                        static_cast<std::uint8_t>(color.getAProperty()),
                    }));
        }
#if defined(CNA_GFX096_STAGED_SINGLE_FACE)
        stagedSingular_ = std::make_unique<RenderTargetCube>(
            device, kCubeSize, false, SurfaceFormat::Color, kCubeDepthFormat, 0,
            RenderTargetUsage::PreserveContents);
        stagedPlural_ = std::make_unique<RenderTargetCube>(
            device, kCubeSize, false, SurfaceFormat::Color, kCubeDepthFormat, 0,
            RenderTargetUsage::PreserveContents);
#endif
    }

    void Draw(const GameTime&) override
    {
#if defined(CNA_GFX096_STAGED_SINGLE_FACE)
        // Bgfx submits one frame/view transition at a time for cube targets. Its
        // cross-backend contract check therefore stages the singular producer,
        // plural producer, and observers across frames. Vulkan retains the full
        // same-frame all-six/A->B->A battery below.
        auto& stagedDevice = getGraphicsDeviceProperty();
        if (stagedFrame_ == 0)
        {
            DrawFace(*stagedSingular_, CubeMapFace::NegativeZ, 6, false);
            stagedDevice.SetRenderTargets({});
            ++stagedFrame_;
            return;
        }
        if (stagedFrame_ == 1)
        {
            DrawFace(*stagedPlural_, CubeMapFace::NegativeZ, 6, true);
            const auto current = stagedDevice.GetRenderTargets();
            stagedBindingState_ = current.size() == 1
                && current[0].getRenderTargetProperty() == stagedPlural_.get()
                && current[0].getCubeMapFaceProperty() == CubeMapFace::NegativeZ;
            stagedDevice.SetRenderTargets({});
            ++stagedFrame_;
            return;
        }
        if (!done_)
        {
            done_ = true;
            const auto singular = SampleFaces(*stagedSingular_);
            const auto plural = SampleFaces(*stagedPlural_);
            Check(stagedBindingState_,
                  "plural binding state preserves cube object and NegativeZ face");
            Check(Matches(singular[5], ExpectedStoredColor(kDrawColors[6])),
                  "singular NegativeZ producer reaches layer 5");
            Check(Matches(plural[5], ExpectedStoredColor(kDrawColors[6])),
                  "plural NegativeZ producer reaches layer 5");
            Check(Matches(plural[5], singular[5]),
                  "singular and plural one-face results are pixel-equivalent");

            RenderTarget2D peer(stagedDevice, kCubeSize, kCubeSize, false,
                                SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
            bool mixedRejected = false;
            try
            {
                stagedDevice.SetRenderTargets({
                    RenderTargetBinding(
                        static_cast<Texture*>(stagedPlural_.get()),
                        CubeMapFace::NegativeZ),
                    RenderTargetBinding(&peer),
                });
            }
            catch (const std::runtime_error&)
            {
                mixedRejected = true;
            }
            Check(mixedRejected,
                  "unsupported cube + 2D MRT is rejected explicitly");

            bool twoFacesRejected = false;
            try
            {
                stagedDevice.SetRenderTargets({
                    RenderTargetBinding(
                        static_cast<Texture*>(stagedPlural_.get()),
                        CubeMapFace::PositiveX),
                    RenderTargetBinding(
                        static_cast<Texture*>(stagedPlural_.get()),
                        CubeMapFace::NegativeX),
                });
            }
            catch (const std::runtime_error&)
            {
                twoFacesRejected = true;
            }
            Check(twoFacesRejected,
                  "unsupported two-face cube MRT is rejected explicitly");
            stagedDevice.SetRenderTargets({});

            std::printf("=== %d/%d PASS ===\n", pass_, total_);
            result_ = pass_ == total_ ? 0 : 1;
            Exit();
        }
        return;
#else
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTargetCube cube(device, kCubeSize, false, SurfaceFormat::Color,
                              kCubeDepthFormat, 0,
                              RenderTargetUsage::PreserveContents);

        // Minimal source-proven reproducer and one-binding public-state check.
        for (int i = 0; i < 6; ++i)
            DrawFace(cube, kFaces[i], i, false);

        device.setViewportProperty(Viewport(3, 4, 7, 8));
        device.setScissorRectangleProperty(Rectangle(2, 3, 5, 6));
        DrawFace(cube, CubeMapFace::NegativeZ, 6, true);
        const auto current = device.GetRenderTargets();
        Check(current.size() == 1
              && current[0].getRenderTargetProperty() == &cube
              && current[0].getCubeMapFaceProperty() == CubeMapFace::NegativeZ,
              "GetRenderTargets preserves cube object and selected NegativeZ face");
        Check(device.getViewportProperty().getXProperty() == 0
              && device.getViewportProperty().getYProperty() == 0
              && device.getViewportProperty().getWidthProperty() == kCubeSize
              && device.getViewportProperty().getHeightProperty() == kCubeSize
              && device.getScissorRectangleProperty() == Rectangle(
                  0, 0, kCubeSize, kCubeSize),
              "plural cube binding resets viewport and scissor to the face extent");

        const auto sampled = SampleFaces(cube);
        auto minimalExpected = kInitialColors;
        minimalExpected[5] = kDrawColors[6];
        Check(FacesMatch(sampled, minimalExpected, "minimal"),
              "plural one-binding NegativeZ changes only layer 5");

        // Exact all-six layer oracle: singular and plural must independently produce
        // +X/-X/+Y/-Y/+Z/-Z == layers 0/1/2/3/4/5.
        RenderTargetCube singular(device, kCubeSize, false, SurfaceFormat::Color,
                                  kCubeDepthFormat, 0,
                                  RenderTargetUsage::PreserveContents);
        RenderTargetCube plural(device, kCubeSize, false, SurfaceFormat::Color,
                                kCubeDepthFormat, 0,
                                RenderTargetUsage::PreserveContents);
        for (int i = 0; i < 6; ++i)
        {
            DrawFace(singular, kFaces[i], i, false);
            DrawFace(plural, kFaces[i], i, true);
        }
        const auto singularFaces = SampleFaces(singular);
        const auto pluralFaces = SampleFaces(plural);
        Check(FacesMatch(singularFaces, kInitialColors, "singular oracle"),
              "singular path maps all six faces exactly");
        Check(FacesMatch(pluralFaces, kInitialColors, "plural"),
              "plural path maps all six faces exactly");
        bool pathsEqual = true;
        for (int i = 0; i < 6; ++i)
            pathsEqual = pathsEqual && Matches(pluralFaces[i], singularFaces[i]);
        Check(pathsEqual, "singular and plural cube-face rendering are pixel-equivalent");

        // Same-object A -> B -> A stresses framebuffer/view identity.
        RenderTargetCube aba(device, kCubeSize, false, SurfaceFormat::Color,
                             kCubeDepthFormat, 0,
                             RenderTargetUsage::PreserveContents);
        for (int i = 0; i < 6; ++i)
            DrawFace(aba, kFaces[i], i, false);
        DrawFace(aba, CubeMapFace::PositiveX, 7, true);
        DrawFace(aba, CubeMapFace::NegativeX, 8, true);
        DrawFace(aba, CubeMapFace::PositiveX, 9, true);
        const auto abaFaces = SampleFaces(aba);
        Check(Matches(abaFaces[0], ExpectedStoredColor(kDrawColors[9]))
                  && Matches(abaFaces[1], ExpectedStoredColor(kDrawColors[8])),
              "plural +X -> -X -> +X keeps -X and the final +X result");

        // Object and subresource identities must both participate.
        RenderTargetCube cubeA(device, kCubeSize, false, SurfaceFormat::Color,
                               kCubeDepthFormat, 0,
                               RenderTargetUsage::PreserveContents);
        RenderTargetCube cubeB(device, kCubeSize, false, SurfaceFormat::Color,
                               kCubeDepthFormat, 0,
                               RenderTargetUsage::PreserveContents);
        for (int i = 0; i < 6; ++i)
        {
            DrawFace(cubeA, kFaces[i], i, false);
            DrawFace(cubeB, kFaces[i], i, false);
        }
        DrawFace(cubeA, CubeMapFace::PositiveX, 10, true);
        DrawFace(cubeB, CubeMapFace::PositiveX, 11, true);
        DrawFace(cubeA, CubeMapFace::NegativeZ, 6, true);
        const auto cubeAFaces = SampleFaces(cubeA);
        const auto cubeBFaces = SampleFaces(cubeB);
        Check(Matches(cubeAFaces[0], ExpectedStoredColor(kDrawColors[10]))
                  && Matches(cubeAFaces[5], ExpectedStoredColor(kDrawColors[6]))
                  && Matches(cubeBFaces[0], ExpectedStoredColor(kDrawColors[11])),
              "multiple cube objects preserve both object and face identity");

        // The singular overload now delegates to the normalized plural path. Exercise
        // DiscardContents without a draw so the implicit clear itself is observable.
        RenderTargetCube discardSingular(
            device, kCubeSize, false, SurfaceFormat::Color, kCubeDepthFormat, 0,
            RenderTargetUsage::DiscardContents);
        RenderTargetCube discardPlural(
            device, kCubeSize, false, SurfaceFormat::Color, kCubeDepthFormat, 0,
            RenderTargetUsage::DiscardContents);
        for (int i = 0; i < 6; ++i)
        {
            DrawFace(discardSingular, kFaces[i], i, false);
            DrawFace(discardPlural, kFaces[i], i, false);
        }
        device.SetRenderTarget(&discardSingular, CubeMapFace::PositiveY);
        device.SetRenderTargets({});
        device.SetRenderTargets({
            RenderTargetBinding(
                static_cast<Texture*>(&discardPlural), CubeMapFace::PositiveY)
        });
        device.SetRenderTargets({});
        const auto discardSingularFaces = SampleFaces(discardSingular);
        const auto discardPluralFaces = SampleFaces(discardPlural);
        std::printf("  [INFO] DiscardContents +Y singular=(%d,%d,%d,%d) "
                    "plural=(%d,%d,%d,%d)\n",
                    discardSingularFaces[2].getRProperty(),
                    discardSingularFaces[2].getGProperty(),
                    discardSingularFaces[2].getBProperty(),
                    discardSingularFaces[2].getAProperty(),
                    discardPluralFaces[2].getRProperty(),
                    discardPluralFaces[2].getGProperty(),
                    discardPluralFaces[2].getBProperty(),
                    discardPluralFaces[2].getAProperty());
        Check(Matches(discardSingularFaces[2], discardPluralFaces[2]),
              "singular and plural DiscardContents treat the selected face identically");

        RenderTarget2D rt2D(device, kCubeSize, kCubeSize, false,
                            SurfaceFormat::Color, DepthFormat::None, 0,
                            RenderTargetUsage::PreserveContents);
        device.SetRenderTargets({RenderTargetBinding(&rt2D)});
        const auto one2D = device.GetRenderTargets();
        Check(one2D.size() == 1
              && one2D[0].getRenderTargetProperty() == &rt2D,
              "ordinary one-binding RenderTarget2D remains intact");
        device.SetRenderTargets({});
        Check(device.GetRenderTargets().empty()
              && device.getViewportProperty().getWidthProperty() != kCubeSize,
              "zero plural bindings restore the backbuffer and clear public target state");

        bool nullRejected = false;
        try {
            device.SetRenderTargets({RenderTargetBinding()});
        } catch (const std::invalid_argument&) {
            nullRejected = true;
        }
        Check(nullRejected, "default/null RenderTargetBinding is rejected deterministically");

        bool plainTextureRejected = false;
        try {
            device.SetRenderTargets({RenderTargetBinding(white_.get())});
        } catch (const std::invalid_argument&) {
            plainTextureRejected = true;
        }
        Check(plainTextureRejected,
              "plain Texture2D binding is rejected instead of treated as a render target");

        bool sliceRejected = false;
        try {
            device.SetRenderTargets({RenderTargetBinding(&rt2D, 1)});
        } catch (const std::exception&) {
            sliceRejected = true;
        }
        Check(sliceRejected,
              "unsupported RenderTarget2D array slice is rejected deterministically");

        device.SetRenderTargets({});
        std::printf("=== %d/%d PASS ===\n", pass_, total_);
        result_ = pass_ == total_ ? 0 : 1;
        Exit();
#endif
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
