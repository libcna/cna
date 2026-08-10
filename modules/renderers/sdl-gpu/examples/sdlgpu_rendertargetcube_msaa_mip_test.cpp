// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-188 -- RenderTargetCube(mipMap=true, multiSampleCount>1) owns the complete
// single-sample cube mip chain on SDL_GPU. The six multisample attachments remain one-level 2D
// render resources; each pass resolves into level 0 of its selected cube face, and that face's
// lower levels are generated before a later pass can sample them.
//
// The local FNA contract is not inferred from CNA's old state. RenderTargetCube passes mipMap to
// its TextureCube base independently of preferredMultiSampleCount, so LevelCount remains the full
// Texture.CalculateMipLevels(size,size) chain. FNA3D_GenColorRenderbuffer creates a separate
// one-level multisample resource, and FNA3D_ResolveTarget performs mip generation only after the
// render target has been resolved/unbound. Thus every declared face/level belongs to the resolved
// cube, while the MSAA attachment owns level 0 only.
//
// Coverage:
//   * 1x1, narrow 2x2, NPOT 13x13 and ordinary 16x16 cubes;
//   * non-MSAA and device-supported requested-4x MSAA configurations;
//   * all six faces and every level, including level 0, level 1, intermediate and final;
//   * whole-level and rectangular GetData with destination offsets, spare capacity and suffixes;
//   * repeated same-face rendering and A/B/A face cycles;
//   * same-frame EnvironmentMapEffect minification followed by direct lower-level GetData;
//   * mipmapped 1x1 and non-mipmapped one-level targets;
//   * disposal before deferred work flushes and a live target across device teardown.

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "common/PixelTestGame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef CNA_RENDERER_SDL_GPU
#error "REMED-GFX-188's native/public cube-mip regression is SDL_GPU-only."
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer;
using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderTargetCubeRenderer;

namespace
{
    constexpr int kBackbuffer = 64;
    constexpr int kGuard = 5;
    constexpr int kSuffix = 9;
    constexpr int kReadSpare = 3;
    constexpr int kTolerance = 10;
    const Color kSentinel(7, 11, 19, 197);

    const std::array<CubeMapFace, 6> kFaces = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };

    const std::array<const char*, 6> kFaceNames = {
        "+X", "-X", "+Y", "-Y", "+Z", "-Z",
    };

    const std::array<Color, 6> kSolidFaceColors = {
        Color(229, 31, 47, 255), Color(37, 211, 59, 255),
        Color(43, 67, 223, 255), Color(233, 197, 29, 255),
        Color(211, 41, 193, 255), Color(31, 203, 211, 255),
    };

    const std::array<Color, 6> kCheckerLow = {
        Color(160, 20, 20, 255), Color(20, 160, 20, 255),
        Color(20, 20, 160, 255), Color(160, 160, 20, 255),
        Color(160, 20, 160, 255), Color(20, 160, 160, 255),
    };

    const std::array<Color, 6> kCheckerHigh = {
        Color(240, 60, 60, 255), Color(60, 240, 60, 255),
        Color(60, 60, 240, 255), Color(240, 240, 60, 255),
        Color(240, 60, 240, 255), Color(60, 240, 240, 255),
    };

    const std::array<Color, 6> kCheckerAverage = {
        Color(200, 40, 40, 255), Color(40, 200, 40, 255),
        Color(40, 40, 200, 255), Color(200, 200, 40, 255),
        Color(200, 40, 200, 255), Color(40, 200, 200, 255),
    };

    struct FaceAim
    {
        float nx;
        float ny;
        float nz;
    };

    // EnvironmentMapEffect's reflection-vector convention, already pinned by REMED-GFX-173.
    const std::array<FaceAim, 6> kFaceAims = {{
        { 1.0f,  0.0f, 1.0f}, // +X
        {-1.0f,  0.0f, 1.0f}, // -X
        { 0.0f,  1.0f, 1.0f}, // +Y
        { 0.0f, -1.0f, 1.0f}, // -Y
        { 0.0f,  0.0f, 1.0f}, // +Z
        { 1.0f,  0.0f, 0.0f}, // -Z
    }};

    [[nodiscard]] int LevelDimension(int size, int level)
    {
        return std::max(1, size >> level);
    }

    [[nodiscard]] int ChainLength(int size)
    {
        int levels = 1;
        while (size > 1)
        {
            size = std::max(1, size / 2);
            ++levels;
        }
        return levels;
    }

    [[nodiscard]] bool Exact(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty()
            && a.getAProperty() == b.getAProperty();
    }

    [[nodiscard]] bool Near(const Color& a, const Color& b, int tolerance = kTolerance)
    {
        const auto delta = [](int x, int y) { return std::abs(x - y); };
        return delta(a.getRProperty(), b.getRProperty()) <= tolerance
            && delta(a.getGProperty(), b.getGProperty()) <= tolerance
            && delta(a.getBProperty(), b.getBProperty()) <= tolerance
            && delta(a.getAProperty(), b.getAProperty()) <= tolerance;
    }

    [[nodiscard]] std::string ColorText(const Color& value)
    {
        return "(" + std::to_string(value.getRProperty()) + "," +
               std::to_string(value.getGProperty()) + "," +
               std::to_string(value.getBProperty()) + "," +
               std::to_string(value.getAProperty()) + ")";
    }

    [[nodiscard]] SamplerState PointClampSampler()
    {
        SamplerState sampler;
        sampler.setFilterProperty(TextureFilter::Point);
        sampler.setAddressUProperty(TextureAddressMode::Clamp);
        sampler.setAddressVProperty(TextureAddressMode::Clamp);
        sampler.setAddressWProperty(TextureAddressMode::Clamp);
        return sampler;
    }
}

/** @brief Runs REMED-GFX-188's resolved-cube mip allocation and finalization matrix. */
class SdlGpuRenderTargetCubeMsaaMipTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTargetCube> heldForTeardown_;
    SdlGpuRenderer* renderer_ = nullptr;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (condition) ++passed_;
    }

    void Step(const std::string& label) const
    {
        std::printf("[STEP] %s\n", label.c_str());
        std::fflush(stdout);
    }

    void ResetDrawState(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    [[nodiscard]] SdlGpuRenderTargetCubeRenderer* Native(RenderTargetCube& cube)
    {
        return dynamic_cast<SdlGpuRenderTargetCubeRenderer*>(cube.GetRenderTargetCubeRenderer());
    }

    [[nodiscard]] std::unique_ptr<RenderTargetCube> MakeCube(
        GraphicsDevice& device, int size, bool mipMap, int requestedSamples,
        RenderTargetUsage usage, const std::string& label)
    {
        Step(label + ": RenderTargetCube(size=" + std::to_string(size) +
             ",mipMap=" + (mipMap ? "true" : "false") +
             ",requestedMSAA=" + std::to_string(requestedSamples) + ")");
        auto cube = std::make_unique<RenderTargetCube>(
            device, size, mipMap, SurfaceFormat::Color, DepthFormat::None,
            requestedSamples, usage);
        SdlGpuRenderTargetCubeRenderer* native = Native(*cube);
        Check(native != nullptr, label + ": concrete SDL_GPU cube renderer is reachable");

        const int expectedLevels = mipMap ? ChainLength(size) : 1;
        Check(cube->getLevelCountProperty() == expectedLevels,
              label + ": public LevelCount is " + std::to_string(expectedLevels) +
              " (got " + std::to_string(cube->getLevelCountProperty()) + ")");
        if (native != nullptr)
        {
            Check(native->LevelCountEXT() == expectedLevels,
                  label + ": resolved native cube owns " + std::to_string(expectedLevels) +
                  " levels (got " + std::to_string(native->LevelCountEXT()) + ")");
            Check(native->LevelCountEXT() == cube->getLevelCountProperty(),
                  label + ": public and native cube mip counts agree");
            Check(native->WantsMipMap() == mipMap,
                  label + ": native finalization retains the caller's mipMap request");
        }

        const int applied = cube->getMultiSampleCountProperty();
        if (requestedSamples == 0)
            Check(applied == 0, label + ": requested 1x path applies no multisampling");
        else
            Check(applied > 1, label + ": requested 4x path engages supported MSAA (applied " +
                                  std::to_string(applied) + "x)");

        if (native != nullptr)
        {
            bool attachmentShape = true;
            bool attachmentLevels = true;
            for (int face = 0; face < 6; ++face)
            {
                const bool exists = native->MsaaTexture(face) != nullptr;
                if (applied > 1 ? !exists : exists) attachmentShape = false;
                const int levels = native->MsaaLevelCountEXT(face);
                if (levels != (applied > 1 ? 1 : 0)) attachmentLevels = false;
            }
            Check(attachmentShape,
                  label + (applied > 1
                      ? ": all six one-face MSAA attachments exist separately"
                      : ": no MSAA attachment exists on the single-sample path"));
            Check(attachmentLevels,
                  label + (applied > 1
                      ? ": every MSAA face attachment owns level zero only"
                      : ": absent MSAA attachments report no native levels"));
        }
        return cube;
    }

    void RenderSolidFaces(GraphicsDevice& device, RenderTargetCube& cube,
                          const std::array<Color, 6>& colors, const std::string& label)
    {
        ResetDrawState(device);
        for (int face = 0; face < 6; ++face)
        {
            Step(label + ": render face " + kFaceNames[static_cast<std::size_t>(face)] +
                 " level 0 as " + ColorText(colors[static_cast<std::size_t>(face)]));
            device.SetRenderTarget(&cube, kFaces[static_cast<std::size_t>(face)]);
            device.Clear(colors[static_cast<std::size_t>(face)]);
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void ReadRegion(RenderTargetCube& cube, int face, int level, const Rectangle* rectangle,
                    const Color& expected, const std::string& label)
    {
        const int edge = LevelDimension(cube.getSizeProperty(), level);
        const int width = rectangle != nullptr ? rectangle->Width : edge;
        const int height = rectangle != nullptr ? rectangle->Height : edge;
        const int requested = width * height;
        const int capacity = requested + kReadSpare;
        std::vector<Color> destination(
            static_cast<std::size_t>(kGuard + capacity + kSuffix), kSentinel);

        Step(label + ": GetData(face=" + kFaceNames[static_cast<std::size_t>(face)] +
             ",level=" + std::to_string(level) +
             (rectangle == nullptr ? ",full" : ",rectangle") +
             ",startIndex=" + std::to_string(kGuard) +
             ",elementCount=" + std::to_string(capacity) + ")");
        bool returned = false;
        std::string exception;
        try
        {
            cube.GetData(kFaces[static_cast<std::size_t>(face)], level, rectangle,
                         destination.data(), kGuard, capacity);
            returned = true;
        }
        catch (const std::exception& e)
        {
            exception = e.what();
        }
        Check(returned, label + ": valid face/level read returns" +
                            (exception.empty() ? std::string() : " (exception: " + exception + ")"));

        int wrong = 0;
        std::string firstWrong;
        if (returned)
        {
            for (int i = 0; i < requested; ++i)
            {
                const Color& got = destination[static_cast<std::size_t>(kGuard + i)];
                if (!Near(got, expected))
                {
                    if (firstWrong.empty())
                        firstWrong = "index " + std::to_string(i) + " got " + ColorText(got) +
                                     " expected " + ColorText(expected);
                    ++wrong;
                }
            }
        }
        else
        {
            wrong = requested;
        }
        if (!firstWrong.empty()) std::printf("        %s\n", firstWrong.c_str());
        Check(wrong == 0, label + ": all " + std::to_string(requested) +
                          " requested texels contain the resolved/generated face color");

        bool prefix = true;
        for (int i = 0; i < kGuard; ++i)
            if (!Exact(destination[static_cast<std::size_t>(i)], kSentinel)) prefix = false;
        Check(prefix, label + ": destination prefix before startIndex is untouched");

        bool suffix = true;
        for (int i = requested; i < capacity + kSuffix; ++i)
            if (!Exact(destination[static_cast<std::size_t>(kGuard + i)], kSentinel)) suffix = false;
        Check(suffix, label + ": spare capacity and protected suffix are untouched");
    }

    void ReadEveryFaceAndLevel(RenderTargetCube& cube,
                               const std::array<Color, 6>& expected,
                               const std::string& label)
    {
        const int levels = cube.getLevelCountProperty();
        for (int face = 0; face < 6; ++face)
        {
            for (int level = 0; level < levels; ++level)
            {
                ReadRegion(cube, face, level, nullptr,
                           expected[static_cast<std::size_t>(face)],
                           label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                           " full level " + std::to_string(level) +
                           (level == levels - 1 ? " (final)" : ""));
            }

            std::set<int> probes = {0, levels - 1};
            if (levels > 1) probes.insert(1);
            if (levels > 3) probes.insert(levels / 2);
            for (const int level : probes)
            {
                const int edge = LevelDimension(cube.getSizeProperty(), level);
                const int x = edge >= 3 ? 1 : 0;
                const int y = edge >= 4 ? 1 : 0;
                const int width = std::max(1, edge - x - (edge >= 5 ? 1 : 0));
                const int height = std::max(1, edge - y - (edge >= 6 ? 1 : 0));
                const Rectangle rectangle(x, y, width, height);
                ReadRegion(cube, face, level, &rectangle,
                           expected[static_cast<std::size_t>(face)],
                           label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                           " rectangular level " + std::to_string(level));
            }
        }
    }

    void RunShapeMatrix(GraphicsDevice& device, int size, int requestedSamples,
                        const std::string& shape)
    {
        const std::string label = shape + " requestedMSAA=" + std::to_string(requestedSamples);
        auto cube = MakeCube(device, size, true, requestedSamples,
                             RenderTargetUsage::DiscardContents, label);
        RenderSolidFaces(device, *cube, kSolidFaceColors, label);
        ReadEveryFaceAndLevel(*cube, kSolidFaceColors, label);
        cube->Dispose();
        Check(cube->getIsDisposedProperty(), label + ": cube disposes after complete face/level reads");
    }

    void RunOneLevelControls(GraphicsDevice& device)
    {
        for (const int samples : {0, 4})
        {
            const std::string label = "one-level 16x16 mipMap=false requestedMSAA=" +
                                      std::to_string(samples);
            auto cube = MakeCube(device, 16, false, samples,
                                 RenderTargetUsage::DiscardContents, label);
            RenderSolidFaces(device, *cube, kSolidFaceColors, label);
            for (int face = 0; face < 6; ++face)
                ReadRegion(*cube, face, 0, nullptr, kSolidFaceColors[static_cast<std::size_t>(face)],
                           label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                           " only level");
        }
    }

    void RunAbaCycles(GraphicsDevice& device, int requestedSamples)
    {
        const std::string label = "A/B/A cycle requestedMSAA=" + std::to_string(requestedSamples);
        auto cube = MakeCube(device, 16, true, requestedSamples,
                             RenderTargetUsage::PreserveContents, label);
        const Color firstA(231, 43, 19, 255);
        const Color faceB(29, 197, 83, 255);
        const Color finalA(47, 71, 229, 255);

        ResetDrawState(device);
        Step(label + ": render +X=A1, then -Z=B, then +X=A2 without an intervening read/present");
        device.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);
        device.Clear(firstA);
        device.SetRenderTarget(cube.get(), CubeMapFace::NegativeZ);
        device.Clear(faceB);
        device.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);
        device.Clear(finalA);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const int levels = cube->getLevelCountProperty();
        for (int level = 0; level < levels; ++level)
        {
            ReadRegion(*cube, 0, level, nullptr, finalA,
                       label + " final A full level " + std::to_string(level));
            ReadRegion(*cube, 5, level, nullptr, faceB,
                       label + " preserved B full level " + std::to_string(level));
        }
    }

    void RunRepeatedFace(GraphicsDevice& device, int requestedSamples)
    {
        const std::string label = "repeated same-face cycle requestedMSAA=" +
                                  std::to_string(requestedSamples);
        auto cube = MakeCube(device, 16, true, requestedSamples,
                             RenderTargetUsage::PreserveContents, label);
        const Color first(217, 53, 101, 255);
        const Color second(61, 173, 239, 255);

        ResetDrawState(device);
        Step(label + ": render +Y twice in consecutive bind cycles");
        device.SetRenderTarget(cube.get(), CubeMapFace::PositiveY);
        device.Clear(first);
        device.SetRenderTarget(cube.get(), CubeMapFace::PositiveY);
        device.Clear(second);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        for (int level = 0; level < cube->getLevelCountProperty(); ++level)
            ReadRegion(*cube, 2, level, nullptr, second,
                       label + " final +Y full level " + std::to_string(level));
    }

    void DrawTexturedQuad(GraphicsDevice& device, Texture2D& texture)
    {
        ResetDrawState(device);
        device.getSamplerStatesProperty()[0] = PointClampSampler();
        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAlphaProperty(1.0f);
        effect.setFogEnabledProperty(false);
        effect.Apply();

        const VertexPositionTexture vertices[6] = {
            {Vector3(-1.f,  1.f, 0.f), Vector2(0.f, 0.f)},
            {Vector3( 1.f, -1.f, 0.f), Vector2(1.f, 1.f)},
            {Vector3(-1.f, -1.f, 0.f), Vector2(0.f, 1.f)},
            {Vector3(-1.f,  1.f, 0.f), Vector2(0.f, 0.f)},
            {Vector3( 1.f,  1.f, 0.f), Vector2(1.f, 0.f)},
            {Vector3( 1.f, -1.f, 0.f), Vector2(1.f, 1.f)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    void DrawEnvironmentFace(GraphicsDevice& device, RenderTargetCube& cube,
                             Texture2D& white, const FaceAim& aim)
    {
        ResetDrawState(device);
        device.getSamplerStatesProperty()[0] = PointClampSampler();
        device.getSamplerStatesProperty()[1] = PointClampSampler();

        EnvironmentMapEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 4.0f / 3.0f), Vector3::Zero, Vector3::Up));
        effect.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f));
        effect.setTextureProperty(&white);
        effect.setEnvironmentMapProperty(&cube);
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setFresnelFactorProperty(0.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3::Zero);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAlphaProperty(1.0f);
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.DirectionalLight0.setEnabledProperty(false);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
        effect.setFogEnabledProperty(false);
        effect.Apply();

        const float length = std::sqrt(aim.nx * aim.nx + aim.ny * aim.ny + aim.nz * aim.nz);
        const Vector3 normal(aim.nx / length, aim.ny / length, aim.nz / length);
        const VertexPositionNormalTexture vertices[6] = {
            {Vector3(-1.f,  1.f, 0.f), normal, Vector2(0.f, 0.f)},
            {Vector3( 1.f, -1.f, 0.f), normal, Vector2(1.f, 1.f)},
            {Vector3(-1.f, -1.f, 0.f), normal, Vector2(0.f, 1.f)},
            {Vector3(-1.f,  1.f, 0.f), normal, Vector2(0.f, 0.f)},
            {Vector3( 1.f,  1.f, 0.f), normal, Vector2(1.f, 0.f)},
            {Vector3( 1.f, -1.f, 0.f), normal, Vector2(1.f, 1.f)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    void RunSampling(GraphicsDevice& device, int requestedSamples)
    {
        constexpr int kCubeSize = 32;
        constexpr int kSampleSize = 8;
        const std::string label = "same-frame minified sampling requestedMSAA=" +
                                  std::to_string(requestedSamples);
        auto cube = MakeCube(device, kCubeSize, true, requestedSamples,
                             RenderTargetUsage::DiscardContents, label);

        std::array<std::unique_ptr<Texture2D>, 6> checkerTextures;
        for (int face = 0; face < 6; ++face)
        {
            checkerTextures[static_cast<std::size_t>(face)] =
                std::make_unique<Texture2D>(device, kCubeSize, kCubeSize, false,
                                            SurfaceFormat::Color);
            std::vector<Color> pixels(
                static_cast<std::size_t>(kCubeSize) * kCubeSize,
                kCheckerLow[static_cast<std::size_t>(face)]);
            for (int y = 0; y < kCubeSize; ++y)
                for (int x = 0; x < kCubeSize; ++x)
                    pixels[static_cast<std::size_t>(y * kCubeSize + x)] =
                        ((x ^ y) & 1) == 0
                            ? kCheckerLow[static_cast<std::size_t>(face)]
                            : kCheckerHigh[static_cast<std::size_t>(face)];
            checkerTextures[static_cast<std::size_t>(face)]->SetData(
                pixels.data(), static_cast<int>(pixels.size()));

            Step(label + ": render checkerboard into face " +
                 kFaceNames[static_cast<std::size_t>(face)]);
            device.SetRenderTarget(cube.get(), kFaces[static_cast<std::size_t>(face)]);
            device.Clear(kSentinel);
            DrawTexturedQuad(device, *checkerTextures[static_cast<std::size_t>(face)]);
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Texture2D white(device, 1, 1, false, SurfaceFormat::Color);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        std::array<std::unique_ptr<RenderTarget2D>, 6> samples;
        for (int face = 0; face < 6; ++face)
        {
            samples[static_cast<std::size_t>(face)] = std::make_unique<RenderTarget2D>(
                device, kSampleSize, kSampleSize, false, SurfaceFormat::Color,
                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            Step(label + ": sample face " + kFaceNames[static_cast<std::size_t>(face)] +
                 " into 8x8 after its queued resolve, before any public read/present");
            device.SetRenderTarget(samples[static_cast<std::size_t>(face)].get());
            device.Clear(kSentinel);
            DrawEnvironmentFace(device, *cube, white, kFaceAims[static_cast<std::size_t>(face)]);
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        for (int face = 0; face < 6; ++face)
        {
            Color sampled = kSentinel;
            const Rectangle center(kSampleSize / 2, kSampleSize / 2, 1, 1);
            Step(label + ": read sampled center for face " +
                 kFaceNames[static_cast<std::size_t>(face)]);
            bool returned = false;
            std::string exception;
            try
            {
                samples[static_cast<std::size_t>(face)]->GetData(
                    0, &center, &sampled, 0, 1);
                returned = true;
            }
            catch (const std::exception& e)
            {
                exception = e.what();
            }
            Check(returned, label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                            ": sampled read returns" +
                            (exception.empty() ? std::string() : " (exception: " + exception + ")"));
            Check(returned && Near(sampled, kCheckerAverage[static_cast<std::size_t>(face)], 14),
                  label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                  ": minification samples the generated lower chain (got " + ColorText(sampled) +
                  ", expected near " +
                  ColorText(kCheckerAverage[static_cast<std::size_t>(face)]) + ")");

            const int finalLevel = cube->getLevelCountProperty() - 1;
            ReadRegion(*cube, face, 1, nullptr,
                       kCheckerAverage[static_cast<std::size_t>(face)],
                       label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                       " direct level 1");
            ReadRegion(*cube, face, finalLevel, nullptr,
                       kCheckerAverage[static_cast<std::size_t>(face)],
                       label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                       " direct final mip");
        }
    }

    void RunDeferredDisposal(GraphicsDevice& device)
    {
        const std::string label = "deferred disposal";
        auto cube = MakeCube(device, 13, true, 4,
                             RenderTargetUsage::DiscardContents, label);
        RenderSolidFaces(device, *cube, kSolidFaceColors, label);
        Step(label + ": Dispose before any GetData forces the queued face producers to flush");
        cube->Dispose();
        Check(cube->getIsDisposedProperty(), label + ": cube is disposed immediately");

        std::vector<Color> destination(17, kSentinel);
        bool threw = false;
        try
        {
            cube->GetData(CubeMapFace::PositiveX, destination.data(), 0, 1);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        Check(threw, label + ": public GetData after disposal rejects the request");
        Check(std::all_of(destination.begin(), destination.end(),
                          [](const Color& value) { return Exact(value, kSentinel); }),
              label + ": rejected post-disposal read leaves the destination untouched");

        RenderTarget2D flush(device, 1, 1, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&flush);
        device.Clear(Color(83, 149, 211, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color pixel = kSentinel;
        bool flushed = false;
        try
        {
            flush.GetData(&pixel, 0, 1);
            flushed = true;
        }
        catch (const std::exception& e)
        {
            std::printf("        deferred flush exception: %s\n", e.what());
        }
        Check(flushed, label + ": later GPU work flushes and retires the disposed cube safely");
    }

    void PrepareDeviceTeardown(GraphicsDevice& device)
    {
        const std::string label = "device teardown survivor";
        heldForTeardown_ = MakeCube(device, 13, true, 4,
                                    RenderTargetUsage::DiscardContents, label);
        RenderSolidFaces(device, *heldForTeardown_, kSolidFaceColors, label);
        ReadRegion(*heldForTeardown_, 4, heldForTeardown_->getLevelCountProperty() - 1,
                   nullptr, kSolidFaceColors[4], label + " final mip before teardown");
        Check(!heldForTeardown_->getIsDisposedProperty(),
              label + ": live cube remains registered for GraphicsDevice teardown");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        renderer_ = dynamic_cast<SdlGpuRenderer*>(&device.GetRenderer());
        Check(renderer_ != nullptr, "SDL_GPU renderer is reachable from GraphicsDevice");

        try
        {
            // Put real MSAA first so a pre-fix run records the public/native count mismatch before
            // it reaches the separate one-level generation assertion at the end of the matrix.
            for (const int samples : {4, 0})
            {
                RunShapeMatrix(device, 16, samples, "ordinary 16x16 mipmapped cube");
                RunShapeMatrix(device, 13, samples, "NPOT 13x13 mipmapped cube");
                RunShapeMatrix(device, 2, samples, "narrow 2x2 mipmapped cube");
                RunShapeMatrix(device, 1, samples, "1x1 mipmapped cube");
            }
            RunOneLevelControls(device);
            RunRepeatedFace(device, 0);
            RunRepeatedFace(device, 4);
            RunAbaCycles(device, 0);
            RunAbaCycles(device, 4);
            RunSampling(device, 0);
            RunSampling(device, 4);
            RunDeferredDisposal(device);
            PrepareDeviceTeardown(device);
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("unexpected exception in REMED-GFX-188 matrix: ") + e.what());
            try { device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); }
            catch (...) {}
        }
        Exit();
    }

    void EndRun() override
    {
        if (renderer_ != nullptr)
            Check(renderer_->IsDebugModeEnabledEXT(), "SDL_GPU native debug mode is enabled");
        std::printf("[INFO] REMED-GFX-188 SDL_GPU cube mip matrix: %d/%d checks passed\n",
                    passed_, total_);
        std::fflush(stdout);
        result_ = passed_ == total_ ? 0 : 1;
    }

public:
    /** @brief Creates the serial SDL_GPU cube-mip regression game. */
    SdlGpuRenderTargetCubeMsaaMipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackbuffer);
        gdm_->setPreferredBackBufferHeightProperty(kBackbuffer);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        gdm_->setPreferMultiSamplingProperty(true);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    /** @brief Returns zero only when every allocation, content and lifecycle check passed. */
    [[nodiscard]] int Result() const { return result_; }

    /** @brief Disposes the game-owned device while the retained cube is deliberately still live. */
    void DisposeGraphicsDeviceForTeardown()
    {
        getGraphicsDeviceProperty().Dispose();
    }

    /** @brief Returns whether device teardown disposed the deliberately retained cube. */
    [[nodiscard]] bool TeardownDisposedHeldCube() const
    {
        return heldForTeardown_ != nullptr && heldForTeardown_->getIsDisposedProperty();
    }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuRenderTargetCubeMsaaMipTest game;
    game.Run();
    int result = game.Result();
    std::printf("[STEP] explicit GraphicsDevice teardown while the final mipmapped MSAA cube lives\n");
    std::fflush(stdout);
    try
    {
        game.DisposeGraphicsDeviceForTeardown();
        const bool disposed = game.TeardownDisposedHeldCube();
        std::printf("[%s] device teardown disposed the retained cube before renderer destruction\n",
                    disposed ? "PASS" : "FAIL");
        if (!disposed) result = 1;
        game.Dispose();
    }
    catch (const std::exception& e)
    {
        std::printf("[FAIL] explicit device teardown threw: %s\n", e.what());
        result = 1;
    }
    std::fflush(stdout);
    return result;
}
