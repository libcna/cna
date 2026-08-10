// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-197 / historical Task 952: functional Bgfx RenderTargetCube depth/stencil coverage.
//
// The recorded test's PRODUCER was already correct on the current renderer: direct GetData returned
// the far red quad for DepthFormat::None and the near green quad for Depth24Stencil8. Its observer
// nevertheless reported the backbuffer clear colour because Matrix::CreateLookAt(eye z=5) was
// paired with an Identity projection, putting the whole sampling quad outside the clip volume.
// After giving the observer a valid orthographic projection, the draw landed but was almost black:
// the fixture had also left EnvironmentMapEffect's lighting and Fresnel defaults active. This file
// keeps the corrected observer and proves the actual depth/stencil result directly as well.
//
// No constructor-only assertion can pass this test. Every depth format is rendered with overlapping
// near/far geometry; Depth24Stencil8 additionally exercises stencil stamp/gate and aspect-specific
// clears. Both requested single-sample and supported-MSAA routes cover all six faces, same-frame
// sampling before any public readback, direct GetData, A/B/A face cycles, native attachment sample
// compatibility, disposal/recreation/handle reuse, and explicit device teardown with a live cube.

#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifndef CNA_RENDERER_BGFX
#error "REMED-GFX-197's focused Task 952 regression is Bgfx-only."
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Bgfx::BgfxRenderTargetCubeRenderer;

namespace
{
    constexpr int kCubeSize = 24;
    constexpr int kBandWidth = kCubeSize / 3;
    constexpr int kConsumerSize = 8;
    constexpr int kMsaaRequest = 4;
    constexpr int kSampleTolerance = 14;

    const Color kBlack(0, 0, 0, 255);
    const Color kMarker(255, 255, 255, 255);
    const Color kSentinel(7, 11, 19, 197);

    const std::array<CubeMapFace, 6> kFaces = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    const std::array<const char*, 6> kFaceNames = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    const std::array<int, 6> kProduceOrder = {0, 2, 4, 1, 5, 3};
    const std::array<int, 6> kReadOrder = {4, 1, 5, 0, 3, 2};

    struct FaceAim
    {
        float x;
        float y;
        float z;
    };

    // Pinned by the working GFX-138/GFX-181 EnvironmentMapEffect cube observer.
    const std::array<FaceAim, 6> kFaceAims = {{
        { 1.0f,  0.0f, 1.0f}, {-1.0f,  0.0f, 1.0f},
        { 0.0f,  1.0f, 1.0f}, { 0.0f, -1.0f, 1.0f},
        { 0.0f,  0.0f, 1.0f}, { 1.0f,  0.0f, 0.0f},
    }};

    const std::array<Color, 6> kNearColours = {
        Color(255,   0,   0, 255), Color(  0, 255,   0, 255),
        Color(  0,   0, 255, 255), Color(  0, 255, 255, 255),
        Color(255,   0, 255, 255), Color(255, 255,   0, 255),
    };
    const std::array<Color, 6> kFarColours = {
        Color(  0, 255, 255, 255), Color(255,   0, 255, 255),
        Color(255, 255,   0, 255), Color(255,   0,   0, 255),
        Color(  0, 255,   0, 255), Color(  0,   0, 255, 255),
    };

    [[nodiscard]] bool Exact(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty()
            && a.getAProperty() == b.getAProperty();
    }

    [[nodiscard]] bool Near(const Color& a, const Color& b)
    {
        const auto delta = [](int x, int y) { return std::abs(x - y); };
        return delta(a.getRProperty(), b.getRProperty()) <= kSampleTolerance
            && delta(a.getGProperty(), b.getGProperty()) <= kSampleTolerance
            && delta(a.getBProperty(), b.getBProperty()) <= kSampleTolerance
            && delta(a.getAProperty(), b.getAProperty()) <= kSampleTolerance;
    }

    [[nodiscard]] std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," +
               std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," +
               std::to_string(c.getAProperty()) + ")";
    }

    [[nodiscard]] const char* DepthName(DepthFormat depth)
    {
        switch (depth)
        {
        case DepthFormat::None: return "None";
        case DepthFormat::Depth16: return "Depth16";
        case DepthFormat::Depth24: return "Depth24";
        case DepthFormat::Depth24Stencil8: return "Depth24Stencil8";
        }
        return "unknown";
    }

    [[nodiscard]] bgfx::TextureFormat::Enum NativeDepthFormat(DepthFormat depth)
    {
        switch (depth)
        {
        case DepthFormat::Depth16: return bgfx::TextureFormat::D16;
        case DepthFormat::Depth24: return bgfx::TextureFormat::D24;
        case DepthFormat::Depth24Stencil8: return bgfx::TextureFormat::D24S8;
        case DepthFormat::None: break;
        }
        return bgfx::TextureFormat::Unknown;
    }

    [[nodiscard]] int ExpectedAppliedSamples(DepthFormat depth, int requested)
    {
        if (requested < 2) return 0;
        const bgfx::Caps* caps = bgfx::getCaps();
        constexpr uint32_t required = BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA;
        if ((caps->formats[bgfx::TextureFormat::RGBA8] & required) == 0) return 0;
        if (depth != DepthFormat::None &&
            (caps->formats[NativeDepthFormat(depth)] & required) == 0) return 0;
        const uint32_t ceiling = std::min<uint32_t>(caps->limits.maxMsaa,
                                                     static_cast<uint32_t>(requested));
        if (ceiling >= 4 && requested >= 4) return 4;
        if (ceiling >= 2) return 2;
        return 0;
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

class BgfxRenderTargetCubeDepthFormatTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<BasicEffect> basicEffect_;
    std::unique_ptr<RenderTargetCube> heldForTeardown_;
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

    [[nodiscard]] DepthStencilState DepthTesting() const
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(true);
        state.setDepthBufferWriteEnableProperty(true);
        state.setDepthBufferFunctionProperty(CompareFunction::LessEqual);
        state.setStencilEnableProperty(false);
        return state;
    }

    [[nodiscard]] DepthStencilState StencilStamp() const
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(CompareFunction::Always);
        state.setStencilPassProperty(StencilOperation::Replace);
        state.setStencilFailProperty(StencilOperation::Keep);
        state.setStencilDepthBufferFailProperty(StencilOperation::Replace);
        state.setReferenceStencilProperty(1);
        return state;
    }

    [[nodiscard]] DepthStencilState StencilGate() const
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(CompareFunction::Equal);
        state.setStencilPassProperty(StencilOperation::Keep);
        state.setStencilFailProperty(StencilOperation::Keep);
        state.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        state.setReferenceStencilProperty(1);
        return state;
    }

    void PrepareDraw(GraphicsDevice& device, const DepthStencilState& state)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(state);
        basicEffect_->setWorldProperty(Matrix::getIdentityProperty());
        basicEffect_->setViewProperty(Matrix::getIdentityProperty());
        basicEffect_->setProjectionProperty(Matrix::getIdentityProperty());
        basicEffect_->VertexColorEnabled = true;
        basicEffect_->Apply();
    }

    void DrawBand(GraphicsDevice& device, int band, float z, const Color& colour)
    {
        const float x0 = band < 0
            ? -1.0f
            : -1.0f + static_cast<float>(band) * (2.0f / 3.0f);
        const float x1 = band < 0
            ? 1.0f
            : -1.0f + static_cast<float>(band + 1) * (2.0f / 3.0f);
        const VertexPositionColor vertices[6] = {
            {Vector3(x0,  1.0f, z), colour}, {Vector3(x0, -1.0f, z), colour},
            {Vector3(x1, -1.0f, z), colour}, {Vector3(x0,  1.0f, z), colour},
            {Vector3(x1, -1.0f, z), colour}, {Vector3(x1,  1.0f, z), colour},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    [[nodiscard]] std::array<Color, 3> ReadBands(const RenderTargetCube& cube, int face,
                                                  const std::string& label)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kCubeSize) * kCubeSize, kSentinel);
        bool returned = false;
        std::string exception;
        try
        {
            cube.GetData(kFaces[static_cast<std::size_t>(face)], pixels.data(),
                         static_cast<int>(pixels.size()));
            returned = true;
        }
        catch (const std::exception& e)
        {
            exception = e.what();
        }
        Check(returned, label + ": direct GetData returns" +
                        (exception.empty() ? std::string() : " (" + exception + ")"));

        std::array<Color, 3> result = {kSentinel, kSentinel, kSentinel};
        for (int band = 0; band < 3; ++band)
        {
            bool uniform = true;
            bool first = true;
            for (int y = kCubeSize / 2 - 2; y < kCubeSize / 2 + 2; ++y)
                for (int x = band * kBandWidth + 1;
                     x < (band + 1) * kBandWidth - 1; ++x)
                {
                    const Color& pixel = pixels[static_cast<std::size_t>(y) * kCubeSize + x];
                    if (first)
                    {
                        result[static_cast<std::size_t>(band)] = pixel;
                        first = false;
                    }
                    else if (!Exact(pixel, result[static_cast<std::size_t>(band)]))
                    {
                        uniform = false;
                    }
                }
            Check(uniform, label + ": band " + std::to_string(band) +
                           " is uniform at " + ColorText(result[static_cast<std::size_t>(band)]));
        }
        return result;
    }

    void ExpectBands(const std::array<Color, 3>& got,
                     const Color& b0, const Color& b1, const Color& b2,
                     const std::string& label)
    {
        const bool exact = Exact(got[0], b0) && Exact(got[1], b1) && Exact(got[2], b2);
        Check(exact, label + " got " + ColorText(got[0]) + "/" + ColorText(got[1]) +
                     "/" + ColorText(got[2]) + " expected " + ColorText(b0) + "/" +
                     ColorText(b1) + "/" + ColorText(b2));
    }

    void DrawEnvironmentFace(GraphicsDevice& device, RenderTargetCube& cube,
                             Texture2D& white, int face)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        SamplerState point = PointClampSampler();
        device.getSamplerStatesProperty()[0] = point;
        device.getSamplerStatesProperty()[1] = point;

        EnvironmentMapEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 4.0f / 3.0f), Vector3::Zero, Vector3::Up));
        // The historical Identity projection clipped the quad at view-space z=-4/3 (and at -5 in
        // the original observer). This is the load-bearing Task 952 fixture correction.
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

        const FaceAim aim = kFaceAims[static_cast<std::size_t>(face)];
        const float length = std::sqrt(aim.x * aim.x + aim.y * aim.y + aim.z * aim.z);
        const Vector3 normal(aim.x / length, aim.y / length, aim.z / length);
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

    void CheckNativeAttachments(BgfxRenderTargetCubeRenderer& renderer, DepthFormat depth,
                                int applied, const std::string& label)
    {
        Check(bgfx::isValid(renderer.cubeTex), label + ": public cube colour handle is valid");
        Check((depth == DepthFormat::None) == !bgfx::isValid(renderer.depthTex),
              label + ": depth attachment presence matches the requested public format");
        const uint64_t producerSamples =
            renderer.msaaProducerCreationFlags_ & BGFX_TEXTURE_RT_MSAA_MASK;
        const uint64_t depthSamples =
            renderer.depthCreationFlags_ & BGFX_TEXTURE_RT_MSAA_MASK;
        const uint64_t directColourSamples =
            renderer.creationFlags_ & BGFX_TEXTURE_RT_MSAA_MASK;
        if (applied > 0)
        {
            Check(producerSamples != 0, label + ": colour producer carries applied MSAA bits");
            if (depth != DepthFormat::None)
                Check(depthSamples == producerSamples,
                      label + ": colour and depth attachment sample fields are identical");
        }
        else
        {
            const bool compatible = depth == DepthFormat::None
                ? depthSamples == 0
                : depthSamples == directColourSamples;
            Check(producerSamples == 0 && compatible,
                  label + ": direct colour and requested depth use the same single-sample field");
        }
    }

    void RunDepthMatrix(GraphicsDevice& device, DepthFormat depth, int requestedSamples)
    {
        const std::string label = std::string("depth=") + DepthName(depth) +
            " requested=" + std::to_string(requestedSamples) + "x";
        std::printf("[SEQUENCE] construct RenderTargetCube(device,%d,false,Color,%s,%d,"
                    "PreserveContents); bind/Clear/draw/unbind/sample/GetData\n",
                    kCubeSize, DepthName(depth), requestedSamples);
        auto cube = std::make_unique<RenderTargetCube>(
            device, kCubeSize, false, SurfaceFormat::Color, depth, requestedSamples,
            RenderTargetUsage::PreserveContents);
        const int applied = cube->getMultiSampleCountProperty();
        const int expectedApplied = ExpectedAppliedSamples(depth, requestedSamples);
        std::printf("[INFO] %s applied=%dx usage=PreserveContents surface=Color\n",
                    label.c_str(), applied);
        Check(cube->getFormatProperty() == SurfaceFormat::Color,
              label + ": public SurfaceFormat remains Color");
        Check(cube->getDepthStencilFormatProperty() == depth,
              label + ": requested public DepthFormat is preserved");
        Check(cube->getRenderTargetUsageProperty() == RenderTargetUsage::PreserveContents,
              label + ": requested RenderTargetUsage is preserved");
        Check(applied == expectedApplied,
              label + ": applied MSAA follows active-renderer capability policy (expected " +
              std::to_string(expectedApplied) + "x, got " + std::to_string(applied) + "x)");
        if (requestedSamples > 0)
            Check(applied > 0, label + ": the supported MSAA route genuinely engages");

        auto* renderer = dynamic_cast<BgfxRenderTargetCubeRenderer*>(
            cube->GetRenderTargetCubeRenderer());
        Check(renderer != nullptr, label + ": concrete Bgfx cube diagnostics are available");

        const bool depthEnabled = depth != DepthFormat::None;
        for (int face : kProduceOrder)
        {
            std::printf("[STAGE] %s face=%s bind -> Clear(Target|Depth|Stencil) -> "
                        "near draw -> far draw -> unbind\n",
                        label.c_str(), kFaceNames[static_cast<std::size_t>(face)]);
            device.SetRenderTarget(cube.get(), kFaces[static_cast<std::size_t>(face)]);
            device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                         kBlack, 1.0f, 0);
            PrepareDraw(device, depthEnabled ? DepthTesting() : DepthStencilState::None);
            DrawBand(device, -1, 0.20f, kNearColours[static_cast<std::size_t>(face)]);
            DrawBand(device, -1, 0.80f, kFarColours[static_cast<std::size_t>(face)]);
            device.SetRenderTargets({});
        }

        // A/B/A: face 0 was produced first, face 1 later, then face 0 receives a partial update.
        device.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);
        PrepareDraw(device, DepthStencilState::None);
        DrawBand(device, 0, 0.50f, kMarker);
        device.SetRenderTargets({});

        if (renderer != nullptr)
        {
            CheckNativeAttachments(*renderer, depth, applied, label);
            bool allFaceFbos = true;
            for (const bgfx::FrameBufferHandle handle : renderer->faceFbos_)
                allFaceFbos = allFaceFbos && bgfx::isValid(handle);
            Check(allFaceFbos, label + ": all six face framebuffers were constructed and retained");
        }

        // Queue every EnvironmentMapEffect consumer before the first public read. Sampling cannot
        // pass because an earlier readback accidentally finalized or primed the cube.
        Texture2D white(device, 1, 1, false, SurfaceFormat::Color);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        std::array<std::unique_ptr<RenderTarget2D>, 6> consumers;
        for (int face : kReadOrder)
        {
            consumers[static_cast<std::size_t>(face)] = std::make_unique<RenderTarget2D>(
                device, kConsumerSize, kConsumerSize, false, SurfaceFormat::Color,
                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(consumers[static_cast<std::size_t>(face)].get());
            device.Clear(kSentinel);
            DrawEnvironmentFace(device, *cube, white, face);
        }
        device.SetRenderTargets({});

        const Color baseExpected = depthEnabled ? kNearColours[0] : kFarColours[0];
        for (int face : kReadOrder)
        {
            Color sampled = kSentinel;
            const Rectangle center(kConsumerSize / 2, kConsumerSize / 2, 1, 1);
            bool returned = false;
            try
            {
                consumers[static_cast<std::size_t>(face)]->GetData(
                    0, &center, &sampled, 0, 1);
                returned = true;
            }
            catch (...) {}
            const Color expected = depthEnabled
                ? kNearColours[static_cast<std::size_t>(face)]
                : kFarColours[static_cast<std::size_t>(face)];
            Check(returned && Near(sampled, expected),
                  label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                  ": sampling returns rendered depth result (got " + ColorText(sampled) +
                  ", expected " + ColorText(expected) + ")");
        }

        for (int face : kReadOrder)
        {
            const Color expected = depthEnabled
                ? kNearColours[static_cast<std::size_t>(face)]
                : kFarColours[static_cast<std::size_t>(face)];
            const auto bands = ReadBands(*cube, face,
                label + " face " + kFaceNames[static_cast<std::size_t>(face)]);
            if (face == 0)
                ExpectBands(bands, kMarker, baseExpected, baseExpected,
                            label + ": A/B/A partial face update preserves A outside the marker");
            else
                ExpectBands(bands, expected, expected, expected,
                            label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                            ": direct GetData proves face isolation and real depth output");
        }
    }

    void RunClearAndStencil(GraphicsDevice& device, int requestedSamples)
    {
        const std::string label = "Depth24Stencil8 clear/stencil requested=" +
            std::to_string(requestedSamples) + "x";
        RenderTargetCube cube(device, kCubeSize, false, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8, requestedSamples,
                              RenderTargetUsage::PreserveContents);
        const int applied = cube.getMultiSampleCountProperty();
        Check(applied == ExpectedAppliedSamples(DepthFormat::Depth24Stencil8, requestedSamples),
              label + ": exact applied count is retained (" + std::to_string(applied) + "x)");

        // Depth-only Clear must reset depth while retaining the colour in the untouched band.
        device.SetRenderTarget(&cube, CubeMapFace::PositiveZ);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     kBlack, 1.0f, 0);
        PrepareDraw(device, DepthTesting());
        DrawBand(device, 0, 0.20f, kNearColours[0]);
        DrawBand(device, 1, 0.20f, kNearColours[0]);
        device.Clear(ClearOptions::DepthBuffer, kBlack, 1.0f, 0);
        PrepareDraw(device, DepthTesting());
        DrawBand(device, 0, 0.80f, kFarColours[0]);
        device.SetRenderTargets({});
        ExpectBands(ReadBands(cube, 4, label + " depth-only Clear"),
                    kFarColours[0], kNearColours[0], kBlack,
                    label + ": depth-only Clear accepts the far probe and preserves colour");

        // Stencil testing without a clear: gate lands exactly in the two stamped bands.
        device.SetRenderTarget(&cube, CubeMapFace::PositiveY);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     kBlack, 1.0f, 0);
        PrepareDraw(device, StencilStamp());
        DrawBand(device, 0, 0.50f, kNearColours[3]);
        DrawBand(device, 1, 0.50f, kNearColours[3]);
        PrepareDraw(device, StencilGate());
        DrawBand(device, -1, 0.50f, kFarColours[3]);
        device.SetRenderTargets({});
        ExpectBands(ReadBands(cube, 2, label + " stencil gate"),
                    kFarColours[3], kFarColours[3], kBlack,
                    label + ": stencil testing changes output only where ReferenceStencil=1");

        // Stencil-only Clear must remove the gate but preserve the stamp's colour.
        device.SetRenderTarget(&cube, CubeMapFace::NegativeY);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     kBlack, 1.0f, 0);
        PrepareDraw(device, StencilStamp());
        DrawBand(device, 0, 0.50f, kNearColours[3]);
        DrawBand(device, 1, 0.50f, kNearColours[3]);
        device.Clear(ClearOptions::Stencil, kBlack, 1.0f, 0);
        PrepareDraw(device, StencilGate());
        DrawBand(device, -1, 0.50f, kFarColours[3]);
        device.SetRenderTargets({});
        ExpectBands(ReadBands(cube, 3, label + " stencil-only Clear"),
                    kNearColours[3], kNearColours[3], kBlack,
                    label + ": stencil-only Clear rejects the gate and preserves colour");
    }

    void RenderOneLifecycleFace(GraphicsDevice& device, RenderTargetCube& cube,
                                const Color& colour)
    {
        device.SetRenderTarget(&cube, CubeMapFace::PositiveX);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     kBlack, 1.0f, 0);
        PrepareDraw(device, DepthTesting());
        DrawBand(device, -1, 0.20f, colour);
        DrawBand(device, -1, 0.80f, Color(3, 5, 7, 255));
        device.SetRenderTargets({});
    }

    void RunLifecycle(GraphicsDevice& device)
    {
        std::set<uint16_t> cubeIds;
        std::set<uint16_t> depthIds;
        std::set<uint16_t> producerIds;
        std::set<uint16_t> framebufferIds;
        bool cubeReuse = false;
        bool depthReuse = false;
        bool producerReuse = false;
        bool framebufferReuse = false;

        for (int round = 0; round < 16; ++round)
        {
            auto cube = std::make_unique<RenderTargetCube>(
                device, kCubeSize, false, SurfaceFormat::Color,
                DepthFormat::Depth24Stencil8, kMsaaRequest,
                RenderTargetUsage::PreserveContents);
            auto* renderer = dynamic_cast<BgfxRenderTargetCubeRenderer*>(
                cube->GetRenderTargetCubeRenderer());
            Check(renderer != nullptr, "lifecycle round " + std::to_string(round) +
                  ": concrete renderer exists");
            const Color colour = kNearColours[static_cast<std::size_t>(round % 6)];
            RenderOneLifecycleFace(device, *cube, colour);
            const auto bands = ReadBands(*cube, 0,
                "lifecycle round " + std::to_string(round));
            ExpectBands(bands, colour, colour, colour,
                        "lifecycle round " + std::to_string(round) +
                        ": recreated target has only its own depth-tested output");

            if (renderer != nullptr)
            {
                cubeReuse = !cubeIds.insert(renderer->cubeTex.idx).second || cubeReuse;
                depthReuse = !depthIds.insert(renderer->depthTex.idx).second || depthReuse;
                producerReuse = !producerIds.insert(renderer->msaaFaceColorTex_[0].idx).second ||
                                producerReuse;
                framebufferReuse = !framebufferIds.insert(renderer->faceFbos_[0].idx).second ||
                                   framebufferReuse;
            }
            cube->Dispose();
            Check(cube->getIsDisposedProperty() && cube->GetRenderTargetCubeRenderer() == nullptr,
                  "lifecycle round " + std::to_string(round) +
                  ": explicit disposal clears the public renderer alias");
        }
        Check(cubeReuse && depthReuse && producerReuse && framebufferReuse,
              "native cube/depth/MSAA-producer/framebuffer handle IDs are all reclaimed and reused");

        heldForTeardown_ = std::make_unique<RenderTargetCube>(
            device, kCubeSize, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, kMsaaRequest,
            RenderTargetUsage::PreserveContents);
        RenderOneLifecycleFace(device, *heldForTeardown_, kNearColours[4]);
        const auto heldBands = ReadBands(*heldForTeardown_, 0, "device teardown survivor");
        ExpectBands(heldBands, kNearColours[4], kNearColours[4], kNearColours[4],
                    "device teardown survivor is exact before explicit device disposal");
        Check(!heldForTeardown_->getIsDisposedProperty(),
              "live depth-backed MSAA cube remains registered for GraphicsDevice teardown");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        basicEffect_ = std::make_unique<BasicEffect>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        GraphicsDevice& device = getGraphicsDeviceProperty();

        std::printf("[INFO] REMED-GFX-197 requested renderer=OpenGL, active renderer=%s\n",
                    bgfx::getRendererName(bgfx::getRendererType()));
        Check(bgfx::getRendererType() == bgfx::RendererType::OpenGL,
              "Task 952 regression runs on the requested Bgfx OpenGL renderer without fallback");

        try
        {
            for (const int requested : {0, kMsaaRequest})
                for (const DepthFormat depth : {DepthFormat::None, DepthFormat::Depth16,
                                                DepthFormat::Depth24,
                                                DepthFormat::Depth24Stencil8})
                    RunDepthMatrix(device, depth, requested);
            RunClearAndStencil(device, 0);
            RunClearAndStencil(device, kMsaaRequest);
            RunLifecycle(device);
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("unexpected Task 952 exception: ") + e.what());
            try { device.SetRenderTargets({}); } catch (...) {}
        }
        catch (...)
        {
            Check(false, "unexpected non-std Task 952 exception");
            try { device.SetRenderTargets({}); } catch (...) {}
        }

        std::printf("[INFO] REMED-GFX-197 Task 952 matrix: %d/%d checks passed\n",
                    passed_, total_);
        std::fflush(stdout);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    BgfxRenderTargetCubeDepthFormatTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
        manager_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        manager_->setPreferMultiSamplingProperty(true);
        manager_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int Result() const { return result_; }

    void DisposeGraphicsDeviceForTeardown()
    {
        getGraphicsDeviceProperty().Dispose();
    }

    [[nodiscard]] bool TeardownDisposedHeldCube() const
    {
        return heldForTeardown_ != nullptr && heldForTeardown_->getIsDisposedProperty();
    }
};

int main()
{
    BgfxRenderTargetCubeDepthFormatTest game;
    game.Run();
    int result = game.Result();
    std::printf("[STEP] explicit GraphicsDevice teardown while the final depth-backed MSAA cube lives\n");
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
