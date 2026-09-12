// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-040: focused multiple-render-target validation.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWindowSize = 64;
    constexpr int kTargetSize = 16;
    constexpr int kColorAttachment0 = 0x8CE0;

    [[nodiscard]] bool Matches(
        const Color& actual, const Color& expected, const int tolerance = 1)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance &&
            std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance &&
            std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance &&
            std::abs(actual.getAProperty() - expected.getAProperty()) <= tolerance;
    }

    [[nodiscard]] Color ReadPixel(RenderTarget2D& target, const int level = 0)
    {
        const int width = std::max(1, target.getWidthProperty() >> level);
        const int height = std::max(1, target.getHeightProperty() >> level);
        Color pixel(0, 0, 0, 0);
        const Rectangle rectangle(width / 2, height / 2, 1, 1);
        target.GetData(level, &rectangle, &pixel, 0, 1);
        return pixel;
    }

    [[nodiscard]] std::vector<RenderTargetBinding> Bindings(
        const std::initializer_list<RenderTarget2D*> targets)
    {
        std::vector<RenderTargetBinding> result;
        result.reserve(targets.size());
        for (RenderTarget2D* const target : targets) result.emplace_back(target);
        return result;
    }

    void DrawFullScreen(
        GraphicsDevice& device, const Color& color, const float depth = 0.0f)
    {
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        const std::array<VertexPositionColor, 6> vertices{
            VertexPositionColor(Vector3(-1.0f,  1.0f, depth), color),
            VertexPositionColor(Vector3(-1.0f, -1.0f, depth), color),
            VertexPositionColor(Vector3( 1.0f, -1.0f, depth), color),
            VertexPositionColor(Vector3(-1.0f,  1.0f, depth), color),
            VertexPositionColor(Vector3( 1.0f, -1.0f, depth), color),
            VertexPositionColor(Vector3( 1.0f,  1.0f, depth), color)};
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, vertices.data(), 0, 2);
    }
}

class RlglMrtTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglMrtTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWindowSize);
        graphics_->setPreferredBackBufferHeightProperty(kWindowSize);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
    }

protected:
    void RunTest() override
    {
        namespace Bridge = CNA::Internal::Renderers::Rlgl::Bridge;
        namespace Rlgl = CNA::Internal::Renderers::Rlgl;
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<Rlgl::RlglRenderer&>(device.GetRenderer());

        Check(device.getGraphicsProfileProperty() == GraphicsProfile::HiDef,
              "GraphicsDeviceManager forwards the requested HiDef profile");
        Check(renderer.GetMaxRenderTargetsForProfileEXT(0) == 1 &&
                  renderer.GetMaxRenderTargetsForProfileEXT(1) == 4,
              "HiDef exposes four native MRT slots while Reach retains XNA's one-slot ceiling");
        Check(device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets),
              "the live HiDef device advertises its validated MRT implementation");

        RenderTarget2D slot0(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D slot1(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::Depth16, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D slot2(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D slot3(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        const std::array<RenderTarget2D*, 4> ordered{&slot0, &slot1, &slot2, &slot3};
        std::array<Rlgl::RenderTargetResourceSnapshot, 4> resources{};
        for (std::size_t index = 0; index < ordered.size(); ++index)
        {
            resources[index] = Rlgl::GetRenderTargetResourceSnapshotForTesting(
                *ordered[index]->GetRenderTargetRenderer());
        }

        bool everyCountMapped = true;
        unsigned int previousFramebuffer = 0;
        for (int count = 2; count <= 4; ++count)
        {
            std::vector<RenderTargetBinding> bindings;
            for (int slot = 0; slot < count; ++slot)
                bindings.emplace_back(ordered[slot]);
            device.SetRenderTargets(bindings);
            const Bridge::MrtFramebufferSnapshot native =
                Bridge::GetBoundMrtFramebufferSnapshotForTesting();
            everyCountMapped = everyCountMapped && native.complete &&
                native.framebuffer != 0 && native.framebuffer != previousFramebuffer &&
                native.depthObject == resources[0].depthStencilRenderbuffer &&
                native.stencilObject == resources[0].depthStencilRenderbuffer;
            for (int slot = 0; slot < 4; ++slot)
            {
                everyCountMapped = everyCountMapped &&
                    native.colorObjects[slot] ==
                        (slot < count ? resources[slot].colorTexture : 0u) &&
                    native.drawBuffers[slot] ==
                        (slot < count ? kColorAttachment0 + slot : 0);
            }
            everyCountMapped = everyCountMapped &&
                native.readBuffer == kColorAttachment0;
            previousFramebuffer = native.framebuffer;
        }
        Check(everyCountMapped,
              "transactional rlgl FBOs map ordered 2/3/4-target sets and slot-zero depth/stencil");

        BlendState masks;
        masks.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        masks.setColorWriteChannels1Property(ColorWriteChannels::Green);
        masks.setColorWriteChannels2Property(ColorWriteChannels::Blue);
        masks.setColorWriteChannels3Property(ColorWriteChannels::Alpha);
        device.setBlendStateProperty(masks);
        const Color clearColor(29, 83, 151, 211);
        device.Clear(clearColor);
        const Bridge::PipelineSnapshot masksAfterClear =
            Bridge::GetPipelineSnapshotForTesting();
        const unsigned int activeAfterRead =
            Bridge::GetBoundMrtFramebufferSnapshotForTesting().framebuffer;
        bool allCleared = true;
        for (RenderTarget2D* const target : ordered)
            allCleared = allCleared && Matches(ReadPixel(*target), clearColor);
        Check(allCleared && activeAfterRead == previousFramebuffer &&
                  masksAfterClear.colorWriteMasks == std::array<int, 4>{1, 2, 4, 8},
              "Clear ignores all indexed write masks, restores them, and active reads preserve MRT");

        device.SetRenderTargets({});
        device.SetRenderTargets(Bindings({&slot0, &slot1, &slot2, &slot3}));
        bool preserveSurvived = true;
        for (RenderTarget2D* const target : ordered)
            preserveSurvived = preserveSurvived && Matches(ReadPixel(*target), clearColor);
        device.SetRenderTargets({});
        Check(preserveSurvived,
              "all four PreserveContents attachments survive MRT replacement and rebind");

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetRenderTargets(Bindings({&slot1, &slot0}));
        device.Clear(Color::Black);
        DrawFullScreen(device, Color::Green);
        device.SetRenderTargets({});
        Check(Matches(ReadPixel(slot1), Color::Green),
              "stock output location zero follows the first target after an ordered slot swap");

        Texture2D white(device, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        DepthStencilState depthLessEqual;
        depthLessEqual.setDepthBufferEnableProperty(true);
        depthLessEqual.setDepthBufferWriteEnableProperty(true);
        depthLessEqual.setDepthBufferFunctionProperty(CompareFunction::LessEqual);

        device.SetRenderTargets(Bindings({&slot0, &slot2}));
        device.Clear(Color::Black, 0.0f);
        device.setDepthStencilStateProperty(depthLessEqual);
        white.GetRenderer().BindGL(0);
        Bridge::DrawBoundTextureSampleForTesting(
            0.5f, 0.5f, kTargetSize, kTargetSize);
        const bool slotZeroDepthRejected = Matches(ReadPixel(slot0), Color::Black);
        device.SetRenderTargets({});

        RenderTarget2D noDepthFirst(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTargets(Bindings({&noDepthFirst, &slot1}));
        device.Clear(Color::Black, 0.0f);
        device.setDepthStencilStateProperty(depthLessEqual);
        white.GetRenderer().BindGL(0);
        Bridge::DrawBoundTextureSampleForTesting(
            0.5f, 0.5f, kTargetSize, kTargetSize);
        const bool laterDepthIgnored = Matches(ReadPixel(noDepthFirst), Color::White);
        device.SetRenderTargets({});
        Check(slotZeroDepthRejected && laterDepthIgnored,
              "only MRT slot zero supplies depth; a later target's depth buffer is never borrowed");

        RenderTarget2D hdr(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::HdrBlendable,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.SetRenderTargets(Bindings({&slot2, &hdr}));
        const auto mixedNative = Bridge::GetBoundMrtFramebufferSnapshotForTesting();
        const auto hdrResource = Rlgl::GetRenderTargetResourceSnapshotForTesting(
            *hdr.GetRenderTargetRenderer());
        const Color mixedClear(64, 128, 192, 255);
        device.Clear(mixedClear);
        device.SetRenderTargets({});
        PackedVector::HalfVector4 hdrPixel;
        const Rectangle center(kTargetSize / 2, kTargetSize / 2, 1, 1);
        hdr.GetData(0, &center, &hdrPixel, 0, 1);
        const Vector4 hdrVector = hdrPixel.ToVector4();
        Check(mixedNative.complete &&
                  mixedNative.colorObjects[0] == resources[2].colorTexture &&
                  mixedNative.colorObjects[1] == hdrResource.colorTexture &&
                  Matches(ReadPixel(slot2), mixedClear) &&
                  std::fabs(hdrVector.X - 64.0f / 255.0f) < 0.002f &&
                  std::fabs(hdrVector.Y - 128.0f / 255.0f) < 0.002f &&
                  std::fabs(hdrVector.Z - 192.0f / 255.0f) < 0.002f &&
                  std::fabs(hdrVector.W - 1.0f) < 0.002f,
              "mixed Color/HdrBlendable attachments keep exact format-native storage and clear");

        RenderTarget2D msaa0(
            device, kTargetSize, kTargetSize, true, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::PreserveContents);
        RenderTarget2D msaa1(
            device, kTargetSize, kTargetSize, true, SurfaceFormat::Color,
            DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
        const auto msaaResource0 = Rlgl::GetRenderTargetResourceSnapshotForTesting(
            *msaa0.GetRenderTargetRenderer());
        const auto msaaResource1 = Rlgl::GetRenderTargetResourceSnapshotForTesting(
            *msaa1.GetRenderTargetRenderer());
        device.SetRenderTargets(Bindings({&msaa0, &msaa1}));
        const auto msaaNative = Bridge::GetBoundMrtFramebufferSnapshotForTesting();
        const Color msaaClear(17, 101, 233, 197);
        device.Clear(msaaClear, 1.0f);
        device.SetRenderTargets({});
        Check(msaa0.getMultiSampleCountProperty() > 1 &&
                  msaa0.getMultiSampleCountProperty() ==
                      msaa1.getMultiSampleCountProperty() &&
                  msaaNative.colorObjects[0] ==
                      msaaResource0.multisampleColorRenderbuffer &&
                  msaaNative.colorObjects[1] ==
                      msaaResource1.multisampleColorRenderbuffer &&
                  msaaNative.depthObject == msaaResource0.depthStencilRenderbuffer &&
                  Matches(ReadPixel(msaa0), msaaClear) &&
                  Matches(ReadPixel(msaa1), msaaClear) &&
                  Matches(ReadPixel(msaa0, 1), msaaClear) &&
                  Matches(ReadPixel(msaa1, 1), msaaClear),
              "MRT MSAA uses real slot renderbuffers, resolves each target, then generates each mip chain");

        RenderTarget2D discard0(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        RenderTarget2D discard1(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        device.SetRenderTargets(Bindings({&discard0, &discard1}));
        device.Clear(Color::Red);
        device.SetRenderTargets({});
        device.SetRenderTargets(Bindings({&discard0, &discard1}));
        const bool discardCleared = Matches(ReadPixel(discard0), Color::Black) &&
            Matches(ReadPixel(discard1), Color::Black);
        device.SetRenderTargets({});
        Check(discardCleared,
              "first-slot DiscardContents policy clears the complete MRT set on every bind");

        device.SetRenderTargets(Bindings({&slot0, &slot1}));
        const unsigned int validFramebuffer =
            Bridge::GetBoundMrtFramebufferSnapshotForTesting().framebuffer;
        RenderTarget2D wrongSize(
            device, kTargetSize / 2, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D wrongSamples(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
        bool sizeRejected = false;
        bool samplesRejected = false;
        bool duplicateRejected = false;
        bool countRejected = false;
        try { device.SetRenderTargets(Bindings({&slot0, &wrongSize})); }
        catch (const std::exception&) { sizeRejected = true; }
        try { device.SetRenderTargets(Bindings({&slot0, &wrongSamples})); }
        catch (const std::exception&) { samplesRejected = true; }
        try { device.SetRenderTargets(Bindings({&slot0, &slot0})); }
        catch (const std::exception&) { duplicateRejected = true; }
        RenderTarget2D fifth(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        try
        {
            device.SetRenderTargets(Bindings(
                {&slot0, &slot1, &slot2, &slot3, &fifth}));
        }
        catch (const std::exception&) { countRejected = true; }
        const auto stillBound = device.GetRenderTargets();
        const bool transactionPreserved = stillBound.size() == 2 &&
            stillBound[0].getRenderTargetProperty() == &slot0 &&
            stillBound[1].getRenderTargetProperty() == &slot1 &&
            Bridge::GetBoundMrtFramebufferSnapshotForTesting().framebuffer == validFramebuffer;
        device.SetRenderTargets({});
        Check(sizeRejected && samplesRejected && duplicateRejected && countRejected &&
                  transactionPreserved,
              "dimension/sample/duplicate/count failures preserve the prior public and native MRT set");

        device.SetRenderTarget(&slot0);
        device.Clear(Color::Blue, 0.0f);
        device.setDepthStencilStateProperty(depthLessEqual);
        white.GetRenderer().BindGL(0);
        Bridge::DrawBoundTextureSampleForTesting(
            0.5f, 0.5f, kTargetSize, kTargetSize);
        const bool depthStillOwned = Matches(ReadPixel(slot0), Color::Blue);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(depthStillOwned,
              "destroying borrowed MRT FBOs leaves the target-owned depth attachment alive");

        device.setDepthStencilStateProperty(DepthStencilState::Default);
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    return CNA::Examples::RunPixelTest<RlglMrtTest>();
}
