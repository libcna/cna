// SPDX-License-Identifier: MS-PL
// REMED-GFX-096: Vulkan cube-face bindings in genuine plural MRT passes.
//
// Public rendering checks use the GFX-095 two-output fragment shader, so clears
// cannot masquerade as correct output-slot routing. Read-only Vulkan diagnostics
// prove the exact selected per-face views, ordered MSAA sources/resolves, slot-0
// depth ownership, and render-pass/framebuffer identity.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp"
#include "vulkan_mrt_msaa_test_spv.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::Vulkan::VulkanGraphicsBackend;
using CNA::Internal::Backends::Vulkan::VulkanMRTProxy;
using CNA::Internal::Backends::Vulkan::VulkanRenderTargetBackend;
using CNA::Internal::Backends::Vulkan::VulkanRenderTargetCubeBackend;

namespace
{
    constexpr int kSize = 32;
    constexpr float kInvSqrt2 = 0.7071067811865475f;
    const Color kClear(10, 20, 30, 40);
    const Color kSource(200, 100, 50, 220);
    const Color kOutput1(100, 50, 200, 220);

    const std::array<Vector3, 6> kFaceNormals = {
        Vector3(0.0f,       1.0f,        0.0f),
        Vector3(1.0f,       0.0f,        0.0f),
        Vector3(kInvSqrt2, -kInvSqrt2,   0.0f),
        Vector3(kInvSqrt2,  kInvSqrt2,   0.0f),
        Vector3(kInvSqrt2,  0.0f,       -kInvSqrt2),
        Vector3(kInvSqrt2,  0.0f,        kInvSqrt2),
    };

    bool Near(int got, int want, int tolerance = 5)
    {
        return std::abs(got - want) <= tolerance;
    }

    bool Matches(const Color& got, const Color& want, int tolerance = 5)
    {
        return Near(got.getRProperty(), want.getRProperty(), tolerance)
            && Near(got.getGProperty(), want.getGProperty(), tolerance)
            && Near(got.getBProperty(), want.getBProperty(), tolerance)
            && Near(got.getAProperty(), want.getAProperty(), tolerance);
    }

    bool MatchesRgb(const Color& got, const Color& want, int tolerance = 5)
    {
        return Near(got.getRProperty(), want.getRProperty(), tolerance)
            && Near(got.getGProperty(), want.getGProperty(), tolerance)
            && Near(got.getBProperty(), want.getBProperty(), tolerance);
    }

    std::string Describe(const Color& value)
    {
        return "(" + std::to_string(value.getRProperty()) + ","
            + std::to_string(value.getGProperty()) + ","
            + std::to_string(value.getBProperty()) + ","
            + std::to_string(value.getAProperty()) + ")";
    }

    Color ResolveOneSample(const Color& clear, const Color& source, int samples)
    {
        auto channel = [samples](int destination, int value) {
            return static_cast<int>(std::lround(
                (static_cast<double>(value)
                 + (samples - 1) * static_cast<double>(destination)) / samples));
        };
        return Color(channel(clear.getRProperty(), source.getRProperty()),
                     channel(clear.getGProperty(), source.getGProperty()),
                     channel(clear.getBProperty(), source.getBProperty()),
                     channel(clear.getAProperty(), source.getAProperty()));
    }
}

class VulkanCubeMrtBindingTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D white_;
    bool done_ = false;
    int pass_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok) ++pass_;
    }

    VulkanGraphicsBackend& Backend()
    {
        return static_cast<VulkanGraphicsBackend&>(
            getGraphicsDeviceProperty().GetBackend());
    }

    static VulkanRenderTargetBackend& BackendOf(RenderTarget2D& target)
    {
        return static_cast<VulkanRenderTargetBackend&>(
            *target.GetRenderTargetBackend());
    }

    static VulkanRenderTargetCubeBackend& BackendOf(RenderTargetCube& target)
    {
        return static_cast<VulkanRenderTargetCubeBackend&>(
            *target.GetRenderTargetCubeBackend());
    }

    std::unique_ptr<RenderTarget2D> Make2D(
        int samples, DepthFormat depth = DepthFormat::None, int size = kSize)
    {
        return std::make_unique<RenderTarget2D>(
            getGraphicsDeviceProperty(), size, size, false, SurfaceFormat::Color,
            depth, samples, RenderTargetUsage::PreserveContents);
    }

    std::unique_ptr<RenderTargetCube> MakeCube(
        int samples, DepthFormat depth = DepthFormat::None, int size = kSize)
    {
        auto result = std::make_unique<RenderTargetCube>(
            getGraphicsDeviceProperty(), size, false, SurfaceFormat::Color,
            depth, samples, RenderTargetUsage::PreserveContents);
        // A cube sampling descriptor spans all six layers. Initialize every layer
        // before any observer samples the cube so validation never has to reason
        // about unrelated VK_IMAGE_LAYOUT_UNDEFINED faces.
        auto& device = getGraphicsDeviceProperty();
        for (int face = 0; face < 6; ++face)
        {
            device.SetRenderTarget(
                result.get(), static_cast<CubeMapFace>(face));
            device.Clear(Color(3, 5, 7, 255));
        }
        device.SetRenderTargets({});
        return result;
    }

    static RenderTargetBinding CubeBinding(
        RenderTargetCube& cube, CubeMapFace face)
    {
        return RenderTargetBinding(static_cast<Texture*>(&cube), face);
    }

    std::unique_ptr<ShaderEffect> MakeEffect()
    {
        std::string vert(reinterpret_cast<const char*>(kGfx095VertSpv),
                         kGfx095VertSpvSize);
        std::string frag(reinterpret_cast<const char*>(kGfx095Mrt2FragSpv),
                         kGfx095Mrt2FragSpvSize);
        return std::make_unique<ShaderEffect>(
            getGraphicsDeviceProperty(), vert, frag);
    }

    void QueueMrtDraw(const std::vector<RenderTargetBinding>& bindings,
                      ShaderEffect& effect, const BlendState& blend)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets(bindings);
        device.Clear(kClear);
        effect.SetUniformVec4(
            "uColor",
            kSource.getRProperty() / 255.0f,
            kSource.getGProperty() / 255.0f,
            kSource.getBProperty() / 255.0f,
            kSource.getAProperty() / 255.0f);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, blend, &point,
                    &noDepth, &noCull, &effect);
        batch.Draw(white_, Rectangle(0, 0, kSize, kSize),
                   Rectangle(0, 0, 1, 1), Color::White);
        batch.End();
        device.SetRenderTargets({});
    }

    Color ReadCenter(RenderTarget2D& target)
    {
        Color result(0, 0, 0, 0);
        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &center, &result, 0, 1);
        return result;
    }

    Color SampleFace(RenderTargetCube& cube, CubeMapFace face)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets({});
        const int width = device.getViewportProperty().getWidthProperty();
        const int height = device.getViewportProperty().getHeightProperty();
        // Vulkan defers both producer and observer passes until this readback.
        // Keep their clear value identical so the later backbuffer observer does
        // not change the pending MRT pass's partial-sample baseline.
        device.Clear(kClear);

        EnvironmentMapEffect effect(device);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setFresnelFactorProperty(0.0f);
        effect.setTextureProperty(&white_);
        effect.setEnvironmentMapProperty(&cube);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        // Establish observer state after the producer SpriteBatch and effect Apply.
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setViewportProperty(Viewport(0, 0, width, height));
        device.setScissorRectangleProperty(Rectangle(0, 0, width, height));

        const Vector3 normal = kFaceNormals[static_cast<int>(face)];
        const VertexPositionNormalTexture quad[6] = {
            {Vector3(-1,  1, 0), normal, Vector2(0, 1)},
            {Vector3(-1, -1, 0), normal, Vector2(0, 0)},
            {Vector3( 1, -1, 0), normal, Vector2(1, 0)},
            {Vector3(-1,  1, 0), normal, Vector2(0, 1)},
            {Vector3( 1, -1, 0), normal, Vector2(1, 0)},
            {Vector3( 1,  1, 0), normal, Vector2(1, 1)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        Color result(0, 0, 0, 0);
        // The identity-view quad's incident vector is +X at this established
        // GFX-094 probe point; the center has a degenerate incident direction.
        const Rectangle probe(3 * width / 4, height / 2, 1, 1);
        device.GetBackBufferData(&probe, &result, 0, 1);
        return result;
    }

    void RunNonMsaaCases(ShaderEffect& effect)
    {
        auto& device = getGraphicsDeviceProperty();
        auto cube = MakeCube(0);
        auto rt = Make2D(0);
        const auto bindings = std::vector<RenderTargetBinding>{
            CubeBinding(*cube, CubeMapFace::PositiveY),
            RenderTargetBinding(rt.get()),
        };
        device.SetRenderTargets(bindings);
        const VulkanMRTProxy* proxy = Backend().GetCurrentMRTProxyEXT();
        const bool structure = proxy
            && proxy->GetColorSampleCountEXT() == VK_SAMPLE_COUNT_1_BIT
            && proxy->GetFramebufferAttachmentCountEXT() == 2
            && proxy->GetResolveAttachmentCountEXT() == 0
            && proxy->GetColorAttachmentViewEXT(0)
                == BackendOf(*cube).GetFaceResolveViewEXT(2)
            && proxy->GetColorAttachmentViewEXT(1)
                == BackendOf(*rt).GetResolveColorViewEXT();
        const auto current = device.GetRenderTargets();
        Check(structure && current.size() == 2
              && current[0].getRenderTargetProperty() == cube.get()
              && current[0].getCubeMapFaceProperty() == CubeMapFace::PositiveY
              && current[1].getRenderTargetProperty() == rt.get(),
              "1x cube + RenderTarget2D keeps ordered selected-face framebuffer views");
        device.SetRenderTargets({});

        QueueMrtDraw(bindings, effect, BlendState::Opaque);
        const Color cubePixel = SampleFace(*cube, CubeMapFace::PositiveY);
        const Color rtPixel = ReadCenter(*rt);
        Check(MatchesRgb(cubePixel, kSource) && Matches(rtPixel, kOutput1),
              "1x cube + RenderTarget2D receives distinct location-0/location-1 outputs: "
              + Describe(cubePixel) + " / " + Describe(rtPixel));

        auto cubeA = MakeCube(0);
        auto cubeB = MakeCube(0);
        const auto twoCubes = std::vector<RenderTargetBinding>{
            CubeBinding(*cubeA, CubeMapFace::PositiveY),
            CubeBinding(*cubeB, CubeMapFace::NegativeZ),
        };
        device.SetRenderTargets(twoCubes);
        proxy = Backend().GetCurrentMRTProxyEXT();
        const bool cubeStructure = proxy
            && proxy->GetColorAttachmentViewEXT(0)
                == BackendOf(*cubeA).GetFaceResolveViewEXT(2)
            && proxy->GetColorAttachmentViewEXT(1)
                == BackendOf(*cubeB).GetFaceResolveViewEXT(5);
        device.SetRenderTargets({});
        QueueMrtDraw(twoCubes, effect, BlendState::Opaque);
        const Color aPixel = SampleFace(*cubeA, CubeMapFace::PositiveY);
        const Color bPixel = SampleFace(*cubeB, CubeMapFace::NegativeZ);
        Check(cubeStructure && MatchesRgb(aPixel, kSource)
              && MatchesRgb(bPixel, kOutput1),
              "two cube objects preserve ordered +Y/-Z views and distinct outputs: "
              + Describe(aPixel) + " / " + Describe(bPixel));

        auto oneCube = MakeCube(0);
        const auto twoFaces = std::vector<RenderTargetBinding>{
            CubeBinding(*oneCube, CubeMapFace::PositiveX),
            CubeBinding(*oneCube, CubeMapFace::NegativeX),
        };
        device.SetRenderTargets(twoFaces);
        proxy = Backend().GetCurrentMRTProxyEXT();
        const bool faceStructure = proxy
            && proxy->GetColorAttachmentViewEXT(0)
                == BackendOf(*oneCube).GetFaceResolveViewEXT(0)
            && proxy->GetColorAttachmentViewEXT(1)
                == BackendOf(*oneCube).GetFaceResolveViewEXT(1);
        device.SetRenderTargets({});
        QueueMrtDraw(twoFaces, effect, BlendState::Opaque);
        const Color positiveX = SampleFace(*oneCube, CubeMapFace::PositiveX);
        const Color negativeX = SampleFace(*oneCube, CubeMapFace::NegativeX);
        Check(faceStructure && MatchesRgb(positiveX, kSource)
              && MatchesRgb(negativeX, kOutput1),
              "same cube different 1x faces are distinct valid MRT subresources: "
              + Describe(positiveX) + " / " + Describe(negativeX));

        BlendState faceMasks = BlendState::Opaque;
        faceMasks.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        faceMasks.setColorWriteChannels1Property(ColorWriteChannels::Blue);
        QueueMrtDraw(twoFaces, effect, faceMasks);
        const Color maskedPositiveX =
            SampleFace(*oneCube, CubeMapFace::PositiveX);
        const Color maskedNegativeX =
            SampleFace(*oneCube, CubeMapFace::NegativeX);
        Check(MatchesRgb(maskedPositiveX, Color(200, 20, 30, 255))
              && MatchesRgb(maskedNegativeX, Color(10, 20, 200, 255)),
              "ColorWriteChannels0/1 stay aligned with same-cube +X/-X subresources: "
              + Describe(maskedPositiveX) + " / "
              + Describe(maskedNegativeX));

        bool duplicateRejected = false;
        try {
            device.SetRenderTargets({
                CubeBinding(*oneCube, CubeMapFace::PositiveX),
                CubeBinding(*oneCube, CubeMapFace::PositiveX),
            });
        } catch (const std::runtime_error&) {
            duplicateRejected = true;
        }
        device.SetRenderTargets({});
        Check(duplicateRejected,
              "duplicate identical cube-face subresource is rejected deterministically");

        // Render-pass compatibility is format/sample/depth based; framebuffer identity
        // includes the actual selected per-face image view.
        device.SetRenderTargets({
            CubeBinding(*cube, CubeMapFace::PositiveX),
            RenderTargetBinding(rt.get()),
        });
        proxy = Backend().GetCurrentMRTProxyEXT();
        const VkRenderPass renderPassX = proxy ? proxy->GetRenderPass() : VK_NULL_HANDLE;
        const VkFramebuffer framebufferX =
            proxy ? proxy->GetFramebuffer() : VK_NULL_HANDLE;
        device.SetRenderTargets({});
        device.SetRenderTargets({
            CubeBinding(*cube, CubeMapFace::NegativeX),
            RenderTargetBinding(rt.get()),
        });
        proxy = Backend().GetCurrentMRTProxyEXT();
        const bool identities = proxy
            && proxy->GetRenderPass() == renderPassX
            && proxy->GetFramebuffer() != framebufferX
            && proxy->GetColorAttachmentViewEXT(0)
                == BackendOf(*cube).GetFaceResolveViewEXT(1);
        device.SetRenderTargets({});
        Check(identities,
              "render pass is face-independent while framebuffer identity follows face view");
    }

    void RunMsaaCases(ShaderEffect& effect)
    {
        auto& device = getGraphicsDeviceProperty();
        auto cube = MakeCube(8);
        auto rt = Make2D(8);
        const int applied = static_cast<int>(
            BackendOf(*cube).GetColorSampleCountEXT());
        const auto bindings = std::vector<RenderTargetBinding>{
            CubeBinding(*cube, CubeMapFace::NegativeZ),
            RenderTargetBinding(rt.get()),
        };
        device.SetRenderTargets(bindings);
        const VulkanMRTProxy* proxy = Backend().GetCurrentMRTProxyEXT();
        const bool structure = applied > 1 && proxy
            && static_cast<int>(proxy->GetColorSampleCountEXT()) == applied
            && proxy->GetFramebufferAttachmentCountEXT() == 4
            && proxy->GetResolveAttachmentCountEXT() == 2
            && proxy->GetColorAttachmentViewEXT(0)
                == BackendOf(*cube).GetMsaaColorViewEXT()
            && proxy->GetColorAttachmentViewEXT(1)
                == BackendOf(*rt).GetMsaaColorViewEXT()
            && proxy->GetResolveAttachmentViewEXT(0)
                == BackendOf(*cube).GetFaceResolveViewEXT(5)
            && proxy->GetResolveAttachmentViewEXT(1)
                == BackendOf(*rt).GetResolveColorViewEXT();
        device.SetRenderTargets({});
        Check(structure,
              "MSAA cube + 2D wires [sourceCube,source2D,resolveFace-5,resolve2D]");

        BlendState oneSample = BlendState::Opaque;
        oneSample.setMultiSampleMaskProperty(1);
        QueueMrtDraw(bindings, effect, oneSample);
        const Color cubePartial = SampleFace(*cube, CubeMapFace::NegativeZ);
        const Color rtPartial = ReadCenter(*rt);
        Check(MatchesRgb(
                  cubePartial, ResolveOneSample(kClear, kSource, applied), 6)
              && Matches(
                  rtPartial, ResolveOneSample(kClear, kOutput1, applied), 6),
              "cube MRT MultiSampleMask=1 proves genuine " + std::to_string(applied)
              + "x sources and selected-face resolve: "
              + Describe(cubePartial) + " / " + Describe(rtPartial));

        auto cubeA = MakeCube(8);
        auto cubeB = MakeCube(8);
        const auto twoCubes = std::vector<RenderTargetBinding>{
            CubeBinding(*cubeA, CubeMapFace::PositiveY),
            CubeBinding(*cubeB, CubeMapFace::NegativeZ),
        };
        device.SetRenderTargets(twoCubes);
        proxy = Backend().GetCurrentMRTProxyEXT();
        const bool twoCubeStructure = proxy
            && proxy->GetColorAttachmentViewEXT(0)
                == BackendOf(*cubeA).GetMsaaColorViewEXT()
            && proxy->GetColorAttachmentViewEXT(1)
                == BackendOf(*cubeB).GetMsaaColorViewEXT()
            && proxy->GetResolveAttachmentViewEXT(0)
                == BackendOf(*cubeA).GetFaceResolveViewEXT(2)
            && proxy->GetResolveAttachmentViewEXT(1)
                == BackendOf(*cubeB).GetFaceResolveViewEXT(5);
        device.SetRenderTargets({});
        QueueMrtDraw(twoCubes, effect, BlendState::Opaque);
        const Color aPixel = SampleFace(*cubeA, CubeMapFace::PositiveY);
        const Color bPixel = SampleFace(*cubeB, CubeMapFace::NegativeZ);
        Check(twoCubeStructure && MatchesRgb(aPixel, kSource)
              && MatchesRgb(bPixel, kOutput1),
              "two MSAA cubes use independent sources and exact +Y/-Z resolves: "
              + Describe(aPixel) + " / " + Describe(bPixel));

        bool sharedSourceRejected = false;
        try {
            device.SetRenderTargets({
                CubeBinding(*cubeA, CubeMapFace::PositiveX),
                CubeBinding(*cubeA, CubeMapFace::NegativeX),
            });
        } catch (const std::runtime_error&) {
            sharedSourceRejected = true;
        }
        device.SetRenderTargets({});
        Check(sharedSourceRejected,
              "same-cube different-face MSAA is explicitly rejected (shared transient source)");

        auto depthCube = MakeCube(8, DepthFormat::Depth24Stencil8);
        auto depthPeer = Make2D(8, DepthFormat::Depth24Stencil8);
        device.SetRenderTargets({
            CubeBinding(*depthCube, CubeMapFace::PositiveZ),
            RenderTargetBinding(depthPeer.get()),
        });
        proxy = Backend().GetCurrentMRTProxyEXT();
        const bool depthOwner = proxy
            && proxy->GetDepthFormat() == BackendOf(*depthCube).GetDepthFormatEXT()
            && proxy->GetDepthSampleCountEXT()
                == BackendOf(*depthCube).GetColorSampleCountEXT()
            && proxy->GetFramebufferAttachmentViewEXT(
                   proxy->GetFramebufferAttachmentCountEXT() - 1)
                == BackendOf(*depthCube).GetDepthViewEXT();
        device.SetRenderTargets({});
        Check(depthOwner,
              "slot-0 cube owns matching shared depth attachment and sample count");
    }

    void RunCompatibilityCases()
    {
        auto& device = getGraphicsDeviceProperty();
        auto cube = MakeCube(8);
        auto plain = Make2D(0);
        bool sampleRejected = false;
        try {
            device.SetRenderTargets({
                CubeBinding(*cube, CubeMapFace::PositiveX),
                RenderTargetBinding(plain.get()),
            });
        } catch (const std::runtime_error&) {
            sampleRejected = true;
        }
        device.SetRenderTargets({});
        Check(sampleRejected,
              "cube MRT mismatched applied sample counts are rejected deterministically");

        auto small = Make2D(8, DepthFormat::None, kSize / 2);
        bool extentRejected = false;
        try {
            device.SetRenderTargets({
                CubeBinding(*cube, CubeMapFace::PositiveX),
                RenderTargetBinding(small.get()),
            });
        } catch (const std::runtime_error&) {
            extentRejected = true;
        }
        device.SetRenderTargets({});
        Check(extentRejected,
              "cube MRT mismatched face/2D dimensions are rejected deterministically");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        getGraphicsDeviceProperty().RecreateBackendForMultiSampleCount(8);
        // Game::Initialize invokes LoadContent before returning. Create this fixture
        // texture only after the explicit backend recreation so it cannot retain the
        // just-destroyed pre-MSAA Vulkan backend.
        white_ = Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 1, 1,
            std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto effect = MakeEffect();
        Check(effect->IsEffectValid(),
              "GFX-095 genuine two-output SPIR-V effect is valid");
        if (effect->IsEffectValid())
        {
            RunNonMsaaCases(*effect);
            RunMsaaCases(*effect);
            RunCompatibilityCases();
        }

        std::printf("=== %d/%d PASS ===\n", pass_, total_);
        result_ = pass_ == total_ ? 0 : 1;
        Exit();
    }

public:
    VulkanCubeMrtBindingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(96);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int Result() const { return result_; }
};

int main()
{
    VulkanCubeMrtBindingTest game;
    game.Run();
    return game.Result();
}
