// SPDX-License-Identifier: MS-PL
// REMED-GFX-097: canonical SDL_GPU render-pass/pipeline compatibility reproducer.
//
// A depthless RenderTargetCube face is a valid public SpriteBatch destination. The selected face
// must receive the opaque draw, every other face must remain unchanged, and the SDL_GPU Vulkan
// route must not report VUID-vkCmdDraw-renderPass-02684. CTest makes that exact VUID fatal; the
// checks below independently prove target/face identity and pixel correctness.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "common/PixelTestGame.hpp"

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
    constexpr int kCubeSize = 16;
    constexpr CubeMapFace kSelectedFace = CubeMapFace::NegativeZ;
    const Color kMarker(211, 73, 29, 255);
    const Color kFinalMarker(37, 191, 227, 255);
    const std::array<CubeMapFace, 6> kFaces = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    const std::array<Color, 6> kInitialColors = {
        Color(11, 21, 31, 255), Color(41, 51, 61, 255),
        Color(71, 81, 91, 255), Color(101, 111, 121, 255),
        Color(131, 141, 151, 255), Color(161, 171, 181, 255),
    };
    const std::array<Color, 6> kFaceDrawColors = {
        Color(231, 31, 41, 255), Color(41, 221, 51, 255),
        Color(51, 61, 211, 255), Color(221, 211, 51, 255),
        Color(211, 51, 201, 255), Color(45, 205, 215, 255),
    };

    bool Matches(const Color& got, const Color& expected, int tolerance = 8)
    {
        return std::abs(got.getRProperty() - expected.getRProperty()) <= tolerance
            && std::abs(got.getGProperty() - expected.getGProperty()) <= tolerance
            && std::abs(got.getBProperty() - expected.getBProperty()) <= tolerance
            && std::abs(got.getAProperty() - expected.getAProperty()) <= tolerance;
    }
}

class SdlGpuDepthlessCubeSpriteBatchCompatibilityTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> sprites_;
    std::unique_ptr<Texture2D> marker_;
    std::unique_ptr<Texture2D> finalMarker_;
    std::unique_ptr<Texture2D> white_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        ++total_;
        if (ok)
            ++passed_;
    }

    std::unique_ptr<Texture2D> MakeTexture(const Color& color)
    {
        auto& device = getGraphicsDeviceProperty();
        return std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1,
            std::vector<std::uint8_t>{
                static_cast<std::uint8_t>(color.getRProperty()),
                static_cast<std::uint8_t>(color.getGProperty()),
                static_cast<std::uint8_t>(color.getBProperty()),
                static_cast<std::uint8_t>(color.getAProperty()),
            }));
    }

    void DrawCubeFace(RenderTargetCube& cube, CubeMapFace face, Texture2D& texture,
                      bool plural, const BlendState& blendState,
                      DepthStencilState depthState,
                      RasterizerState rasterizerState = RasterizerState::CullNone)
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
        device.setViewportProperty(Viewport(0, 0, kCubeSize, kCubeSize));
        device.setScissorRectangleProperty(Rectangle(0, 0, kCubeSize, kCubeSize));

        SamplerState point = SamplerState::PointClamp;
        sprites_->Begin(SpriteSortMode::Deferred, blendState, &point,
                        &depthState, &rasterizerState);
        sprites_->Draw(texture, Rectangle(0, 0, kCubeSize, kCubeSize),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void Draw2D(RenderTarget2D& target, Texture2D& texture)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(&target);
        device.Clear(Color(3, 5, 7, 255));
        device.setViewportProperty(Viewport(0, 0, kCubeSize, kCubeSize));
        device.setScissorRectangleProperty(Rectangle(0, 0, kCubeSize, kCubeSize));
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                        &noDepth, &noCull);
        sprites_->Draw(texture, Rectangle(0, 0, kCubeSize, kCubeSize),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    Color ReadCube(RenderTargetCube& cube, CubeMapFace face, int x = kCubeSize / 2,
                   int y = kCubeSize / 2)
    {
        Color got(0, 0, 0, 0);
        const Rectangle pixel(x, y, 1, 1);
        cube.GetData(face, 0, &pixel, &got, 0, 1);
        return got;
    }

    Color Read2D(RenderTarget2D& target)
    {
        Color got(0, 0, 0, 0);
        const Rectangle pixel(kCubeSize / 2, kCubeSize / 2, 1, 1);
        target.GetData(0, &pixel, &got, 0, 1);
        return got;
    }

    void ClearCube(RenderTargetCube& cube)
    {
        auto& device = getGraphicsDeviceProperty();
        for (std::size_t i = 0; i < kFaces.size(); ++i)
        {
            device.SetRenderTarget(&cube, kFaces[i]);
            device.Clear(kInitialColors[i]);
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sprites_ = std::make_unique<SpriteBatch>(device);
        marker_ = MakeTexture(kMarker);
        finalMarker_ = MakeTexture(kFinalMarker);
        white_ = MakeTexture(Color::White);
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer&>(
            device.GetRenderer());
        Check(renderer.IsDebugModeEnabledEXT(),
              "SDL_GPU debug mode requested for the validation route");
        Check(renderer.GetSpritePipelineCacheSizeEXT() == 0,
              "SpriteBatch pipeline cache begins empty");

        RenderTargetCube cubeA(device, kCubeSize, false, SurfaceFormat::Color,
                               DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        Check(cubeA.getDepthStencilFormatProperty() == DepthFormat::None,
              "public cube target is genuinely depthless");
        ClearCube(cubeA);

        DepthStencilState noDepth = DepthStencilState::None;
        DrawCubeFace(cubeA, kSelectedFace, *marker_, false, BlendState::Opaque, noDepth);

        bool facesCorrect = true;
        for (std::size_t i = 0; i < kFaces.size(); ++i)
        {
            const Color got = ReadCube(cubeA, kFaces[i]);
            const Color& expected = kFaces[i] == kSelectedFace ? kMarker : kInitialColors[i];
            const bool ok = Matches(got, expected);
            facesCorrect = facesCorrect && ok;
            std::printf("  [%s] face %zu got=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                        ok ? "PASS" : "FAIL", i,
                        got.getRProperty(), got.getGProperty(),
                        got.getBProperty(), got.getAProperty(),
                        expected.getRProperty(), expected.getGProperty(),
                        expected.getBProperty(), expected.getAProperty());
        }
        Check(facesCorrect, "only NegativeZ receives the opaque SpriteBatch result");
        Check(renderer.GetSpritePipelineCacheSizeEXT() == 1,
              "fresh depthless cube creates exactly one compatible SpriteBatch pipeline");

        // Depthless RenderTarget2D is the same attachment compatibility class as a depthless
        // single-sample cube face: it must be clean and reuse the pipeline.
        RenderTarget2D depthless2D(device, kCubeSize, kCubeSize, false, SurfaceFormat::Color,
                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        Draw2D(depthless2D, *marker_);
        Check(Matches(Read2D(depthless2D), kMarker),
              "depthless RenderTarget2D SpriteBatch control is pixel-correct");
        Check(renderer.GetSpritePipelineCacheSizeEXT() == 1,
              "depthless RT2D reuses the depthless cube compatibility pipeline");

        // A(depthless) -> B(depth-backed) -> A(depthless), with a flush/readback at each step so
        // the deferred renderer truly executes the transition in that order.
        RenderTargetCube cubeDepth(device, kCubeSize, false, SurfaceFormat::Color,
                                   DepthFormat::Depth24Stencil8, 0,
                                   RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&cubeDepth, kSelectedFace);
        device.Clear(Color::Black, 1.0f);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        DrawCubeFace(cubeDepth, kSelectedFace, *marker_, false, BlendState::Opaque, noDepth);
        const Color depthBackedResult = ReadCube(cubeDepth, kSelectedFace);
        Check(Matches(depthBackedResult, kMarker),
              "depth-backed cube with DepthStencilState.None is pixel-correct");
        Check(renderer.GetSpritePipelineCacheSizeEXT() == 2,
              "depth-backed pass creates one distinct compatibility pipeline");

        DrawCubeFace(cubeA, kSelectedFace, *finalMarker_, false, BlendState::Opaque, noDepth);
        const Color finalA = ReadCube(cubeA, kSelectedFace);
        Check(Matches(finalA, kFinalMarker) && Matches(ReadCube(cubeDepth, kSelectedFace), kMarker),
              "A depthless -> B depth-backed -> A depthless keeps both results exact");
        Check(renderer.GetSpritePipelineCacheSizeEXT() == 2,
              "A -> B -> A reuses both compatibility entries without growth");

        RenderTargetCube cubeB(device, kCubeSize, false, SurfaceFormat::Color,
                               DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        DrawCubeFace(cubeB, CubeMapFace::PositiveX, *marker_, false,
                     BlendState::Opaque, noDepth);
        Check(Matches(ReadCube(cubeB, CubeMapFace::PositiveX), kMarker),
              "second depthless cube object renders correctly");
        Check(renderer.GetSpritePipelineCacheSizeEXT() == 2,
              "cube object and face identity do not fragment the pipeline cache");

        // Backbuffer transition. Its color format may or may not be distinct on a given SDL_GPU
        // driver, so the first use may add at most one entry; a repeat and return to cube must not.
        const std::size_t beforeBackbuffer = renderer.GetSpritePipelineCacheSizeEXT();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(Color::Black);
        SamplerState point = SamplerState::PointClamp;
        RasterizerState noCull = RasterizerState::CullNone;
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                        &noDepth, &noCull);
        sprites_->Draw(*marker_, Rectangle(0, 0, kCubeSize, kCubeSize),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.Present();
        const std::size_t afterBackbuffer = renderer.GetSpritePipelineCacheSizeEXT();
        Check(afterBackbuffer >= beforeBackbuffer && afterBackbuffer <= beforeBackbuffer + 1,
              "first backbuffer use adds at most one genuine compatibility entry");
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                        &noDepth, &noCull);
        sprites_->Draw(*marker_, Rectangle(0, 0, kCubeSize, kCubeSize),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.Present();
        DrawCubeFace(cubeA, CubeMapFace::PositiveX, *marker_, false,
                     BlendState::Opaque, noDepth);
        ReadCube(cubeA, CubeMapFace::PositiveX);
        Check(renderer.GetSpritePipelineCacheSizeEXT() == afterBackbuffer,
              "backbuffer reuse and return to depthless cube do not grow the cache");

        // Singular/plural parity on genuinely depthless targets.
        RenderTargetCube singular(device, kCubeSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        RenderTargetCube plural(device, kCubeSize, false, SurfaceFormat::Color,
                                DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        DrawCubeFace(singular, kSelectedFace, *marker_, false, BlendState::Opaque, noDepth);
        DrawCubeFace(plural, kSelectedFace, *marker_, true, BlendState::Opaque, noDepth);
        Check(Matches(ReadCube(singular, kSelectedFace), kMarker)
                  && Matches(ReadCube(plural, kSelectedFace), kMarker),
              "singular and plural depthless cube bindings are pixel-equivalent");

        // Every face uses the same pipeline compatibility class while retaining its own layer.
        RenderTargetCube allFaces(device, kCubeSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        std::array<std::unique_ptr<Texture2D>, 6> faceTextures;
        for (std::size_t i = 0; i < kFaces.size(); ++i)
        {
            faceTextures[i] = MakeTexture(kFaceDrawColors[i]);
            DrawCubeFace(allFaces, kFaces[i], *faceTextures[i], true,
                         BlendState::Opaque, noDepth);
        }
        bool allFacesCorrect = true;
        for (std::size_t i = 0; i < kFaces.size(); ++i)
            allFacesCorrect = allFacesCorrect
                && Matches(ReadCube(allFaces, kFaces[i]), kFaceDrawColors[i]);
        Check(allFacesCorrect, "all six depthless cube faces are exact and validation-compatible");

        // Depth-backed + active depth state remains valid; depth state and attachment metadata are
        // independent pipeline dimensions.
        DepthStencilState defaultDepth = DepthStencilState::Default;
        device.SetRenderTarget(&cubeDepth, CubeMapFace::PositiveY);
        device.Clear(Color::Black, 1.0f);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        DrawCubeFace(cubeDepth, CubeMapFace::PositiveY, *marker_, false,
                     BlendState::Opaque, defaultDepth);
        Check(Matches(ReadCube(cubeDepth, CubeMapFace::PositiveY), kMarker),
              "depth-backed cube with DepthStencilState.Default remains correct");

        RenderTargetCube depthlessWithDepthState(
            device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        DrawCubeFace(depthlessWithDepthState, CubeMapFace::NegativeY, *marker_, false,
                     BlendState::Opaque, defaultDepth);
        Check(Matches(ReadCube(depthlessWithDepthState, CubeMapFace::NegativeY), kMarker),
              "depth-testing state on a depthless pass follows established SDL_GPU behavior");

        // MSAA source -> selected-face resolve retains the same depthless target metadata.
        RenderTargetCube msaa(device, kCubeSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        DrawCubeFace(msaa, CubeMapFace::PositiveZ, *marker_, false,
                     BlendState::Opaque, noDepth);
        Check(msaa.getMultiSampleCountProperty() > 1
                  && Matches(ReadCube(msaa, CubeMapFace::PositiveZ), kMarker),
              "depthless MSAA cube SpriteBatch resolves the selected face correctly");

        // Ordinary AlphaBlend sanity.
        // AlphaBlend is premultiplied-alpha blending (One, InverseSourceAlpha). Use an explicitly
        // premultiplied source and a non-black destination so the expected value discriminates
        // real blending from either an opaque copy or a non-premultiplied fixture mistake.
        const Color alphaSource(100, 50, 25, 128);
        const Color alphaBackground(20, 40, 60, 255);
        auto alphaTexture = MakeTexture(alphaSource);
        RenderTargetCube alphaCube(device, kCubeSize, false, SurfaceFormat::Color,
                                   DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&alphaCube, CubeMapFace::NegativeX);
        device.Clear(alphaBackground);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        DrawCubeFace(alphaCube, CubeMapFace::NegativeX, *alphaTexture, false,
                     BlendState::AlphaBlend, noDepth);
        const Color alphaResult = ReadCube(alphaCube, CubeMapFace::NegativeX);
        std::printf("[INFO] AlphaBlend result=(%d,%d,%d,%d)\n",
                    alphaResult.getRProperty(), alphaResult.getGProperty(),
                    alphaResult.getBProperty(), alphaResult.getAProperty());
        Check(Matches(alphaResult, Color(110, 70, 55, 255)),
              "depthless cube AlphaBlend remains pixel-correct");

        // Custom scissor and custom viewport, both through SpriteBatch.
        RenderTargetCube clipped(device, kCubeSize, false, SurfaceFormat::Color,
                                 DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        const Color clipBackground(9, 19, 29, 255);
        device.SetRenderTarget(&clipped, CubeMapFace::PositiveX);
        device.Clear(clipBackground);
        device.setViewportProperty(Viewport(0, 0, kCubeSize, kCubeSize));
        device.setScissorRectangleProperty(Rectangle(kCubeSize / 2, 0,
                                                     kCubeSize / 2, kCubeSize));
        RasterizerState scissor = RasterizerState::CullNone;
        scissor.setScissorTestEnableProperty(true);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                        &noDepth, &scissor);
        sprites_->Draw(*marker_, Rectangle(0, 0, kCubeSize, kCubeSize),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(Matches(ReadCube(clipped, CubeMapFace::PositiveX, 3, 8), clipBackground)
                  && Matches(ReadCube(clipped, CubeMapFace::PositiveX, 12, 8), kMarker),
              "depthless cube SpriteBatch honors a custom scissor");

        device.SetRenderTarget(&clipped, CubeMapFace::NegativeX);
        device.Clear(clipBackground);
        device.setViewportProperty(Viewport(kCubeSize / 2, 0, kCubeSize / 2,
                                            kCubeSize));
        device.setScissorRectangleProperty(Rectangle(0, 0, kCubeSize, kCubeSize));
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                        &noDepth, &noCull);
        sprites_->Draw(*marker_, Rectangle(0, 0, kCubeSize / 2, kCubeSize),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(Matches(ReadCube(clipped, CubeMapFace::NegativeX, 3, 8), clipBackground)
                  && Matches(ReadCube(clipped, CubeMapFace::NegativeX, 12, 8), kMarker),
              "depthless cube SpriteBatch honors a custom viewport");

        // Producer -> cube consumer in the same command buffer. Queue all six producer faces before
        // the consumer so the cubemap is uniformly the new marker color: this makes the observer
        // independent of cube filtering at face seams while still requiring the queued SpriteBatch
        // writes to become visible to the later EnvironmentMap draw without Present/readback.
        RenderTargetCube producer(device, kCubeSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        RenderTargetCube consumer(device, kCubeSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        for (CubeMapFace face : kFaces)
            DrawCubeFace(producer, face, *marker_, false, BlendState::Opaque, noDepth);
        device.SetRenderTarget(&consumer, CubeMapFace::PositiveX);
        device.Clear(Color::Black);
        EnvironmentMapEffect effect(device);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setFresnelFactorProperty(0.0f);
        effect.setTextureProperty(white_.get());
        effect.setEnvironmentMapProperty(&producer);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        const Vector3 observerNormal(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            {Vector3(-1,  1, 0), observerNormal, Vector2(0, 1)},
            {Vector3(-1, -1, 0), observerNormal, Vector2(0, 0)},
            {Vector3( 1, -1, 0), observerNormal, Vector2(1, 0)},
            {Vector3(-1,  1, 0), observerNormal, Vector2(0, 1)},
            {Vector3( 1, -1, 0), observerNormal, Vector2(1, 0)},
            {Vector3( 1,  1, 0), observerNormal, Vector2(1, 1)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Color consumerResult = ReadCube(consumer, CubeMapFace::PositiveX);
        std::printf("[INFO] producer-consumer result=(%d,%d,%d,%d)\n",
                    consumerResult.getRProperty(), consumerResult.getGProperty(),
                    consumerResult.getBProperty(), consumerResult.getAProperty());
        Check(Matches(consumerResult, kMarker, 16),
              "same-submission producer -> cube consumer observes the new face contents");

        std::printf("[INFO] final SpriteBatch pipeline-cache cardinality: %zu\n",
                    renderer.GetSpritePipelineCacheSizeEXT());

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    SdlGpuDepthlessCubeSpriteBatchCompatibilityTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int Result() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuDepthlessCubeSpriteBatchCompatibilityTest game;
    game.Run();
    return game.Result();
}
