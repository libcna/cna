// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-039: focused single-sample RenderTarget2D validation.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <array>
#include <cmath>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWindowSize = 64;
    constexpr int kTargetSize = 16;

    [[nodiscard]] bool Matches(const Color& actual, const Color& expected)
    {
        return actual.getRProperty() == expected.getRProperty() &&
            actual.getGProperty() == expected.getGProperty() &&
            actual.getBProperty() == expected.getBProperty() &&
            actual.getAProperty() == expected.getAProperty();
    }

    [[nodiscard]] Color ReadPixel(
        RenderTarget2D& target, const int x = kTargetSize / 2,
        const int y = kTargetSize / 2, const int level = 0)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle rectangle(x, y, 1, 1);
        target.GetData(level, &rectangle, &pixel, 0, 1);
        return pixel;
    }
}

class RlglRenderTargetTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglRenderTargetTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWindowSize);
        graphics_->setPreferredBackBufferHeightProperty(kWindowSize);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::Rlgl::RlglRenderer&>(
            device.GetRenderer());
        namespace Bridge = CNA::Internal::Renderers::Rlgl::Bridge;
        namespace Rlgl = CNA::Internal::Renderers::Rlgl;

        Check(device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Color) &&
                  !device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4),
              "RenderTarget2D format query advertises only the implemented Color baseline");

        RenderTarget2D noDepth(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D depth16(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::Depth16, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D depth24(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D depthStencil(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);

        const auto noneSnapshot = Rlgl::GetRenderTargetResourceSnapshotForTesting(
            *noDepth.GetRenderTargetRenderer());
        const auto depth16Snapshot = Rlgl::GetRenderTargetResourceSnapshotForTesting(
            *depth16.GetRenderTargetRenderer());
        const auto depth24Snapshot = Rlgl::GetRenderTargetResourceSnapshotForTesting(
            *depth24.GetRenderTargetRenderer());
        const auto stencilSnapshot = Rlgl::GetRenderTargetResourceSnapshotForTesting(
            *depthStencil.GetRenderTargetRenderer());
        Check(noneSnapshot.framebuffer != 0 && noneSnapshot.colorTexture != 0 &&
                  noneSnapshot.depthStencilRenderbuffer == 0 &&
                  depth16Snapshot.depthStencilRenderbuffer != 0 &&
                  depth24Snapshot.depthStencilRenderbuffer != 0 &&
                  stencilSnapshot.depthStencilRenderbuffer != 0 &&
                  depth16.GetRenderTargetRenderer()->DepthBufferBitsEXT() == 16 &&
                  depth24.GetRenderTargetRenderer()->DepthBufferBitsEXT() == 24 &&
                  depthStencil.GetRenderTargetRenderer()->HasRealStencilBuffer(true),
              "rlgl FBOs own exact None/Depth16/Depth24/Depth24Stencil8 attachments");

        bool rejectedMsaa = false;
        try
        {
            RenderTarget2D pendingMsaa(
                device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
            (void)pendingMsaa;
        }
        catch (const System::NotSupportedException&)
        {
            rejectedMsaa = true;
        }
        Check(rejectedMsaa,
              "multisampled RenderTarget2D is explicitly rejected until RLGL-041");

        device.SetRenderTarget(&noDepth);
        device.Clear(Color::Black);
        RasterizerState scissored;
        scissored.setCullModeProperty(CullMode::None);
        scissored.setScissorTestEnableProperty(true);
        device.setRasterizerStateProperty(scissored);
        device.setScissorRectangleProperty(Rectangle(0, 0, kTargetSize, kTargetSize / 2));
        renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        device.setScissorRectangleProperty(
            Rectangle(0, kTargetSize / 2, kTargetSize, kTargetSize / 2));
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        scissored.setScissorTestEnableProperty(false);
        device.setRasterizerStateProperty(scissored);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(Matches(ReadPixel(noDepth, 2, 2), Color::Red) &&
                  Matches(ReadPixel(noDepth, 2, kTargetSize - 3), Color::Blue),
              "RenderTarget2D readback maps top-left rectangles and row order exactly once");

        device.Clear(Color::Black);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        BasicEffect texturedEffect(device);
        texturedEffect.setWorldProperty(Matrix::getIdentityProperty());
        texturedEffect.setViewProperty(Matrix::getIdentityProperty());
        texturedEffect.setProjectionProperty(Matrix::getIdentityProperty());
        texturedEffect.setTextureEnabledProperty(true);
        texturedEffect.setTextureProperty(&noDepth);
        const std::array<VertexPositionTexture, 6> texturedQuad{
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)),
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f))};
        texturedEffect.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, texturedQuad.data(), 0, 2);
        const bool stockTop = ExpectPixel(
            "stock BasicEffect samples the target's logical top row",
            Rectangle(4, 4, 1, 1), Color::Red);
        const bool stockBottom = ExpectPixel(
            "stock BasicEffect samples the target's logical bottom row",
            Rectangle(4, kWindowSize - 5, 1, 1), Color::Blue);
        Check(stockTop && stockBottom,
              "stock shader applies RenderTarget2D orientation per texture slot");

        const std::array<Color, 4> uploaded{
            Color::Red, Color::Green, Color::Blue, Color::Yellow};
        RenderTarget2D uploadedTarget(
            device, 2, 2, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        uploadedTarget.SetData(uploaded.data(), static_cast<int>(uploaded.size()));
        std::array<Color, 4> uploadedReadback{};
        uploadedTarget.GetData(uploadedReadback.data(), 0, 4);
        bool uploadRoundTrip = true;
        for (std::size_t index = 0; index < uploaded.size(); ++index)
            uploadRoundTrip = uploadRoundTrip && Matches(uploadedReadback[index], uploaded[index]);
        Check(uploadRoundTrip,
              "RenderTarget2D CPU upload/readback preserves top-row-first Color order");

        Texture2D white(device, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        white.GetRenderer().BindGL(0);

        const auto depthAttachmentRejects = [&](RenderTarget2D& target)
        {
            device.SetRenderTarget(&target);
            device.Clear(Color::Black, 0.0f);
            DepthStencilState depthState;
            depthState.setDepthBufferEnableProperty(true);
            depthState.setDepthBufferWriteEnableProperty(true);
            depthState.setDepthBufferFunctionProperty(CompareFunction::LessEqual);
            device.setDepthStencilStateProperty(depthState);
            white.GetRenderer().BindGL(0);
            Bridge::DrawBoundTextureSampleForTesting(
                0.5f, 0.5f, kTargetSize, kTargetSize);
            const bool rejected = Matches(ReadPixel(target), Color::Black);
            depthState.setDepthBufferFunctionProperty(CompareFunction::Always);
            device.setDepthStencilStateProperty(depthState);
            white.GetRenderer().BindGL(0);
            Bridge::DrawBoundTextureSampleForTesting(
                0.5f, 0.5f, kTargetSize, kTargetSize);
            const bool accepted = Matches(ReadPixel(target), Color::White);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            return rejected && accepted;
        };
        Check(depthAttachmentRejects(depth16) && depthAttachmentRejects(depth24),
              "Depth16 and Depth24 attachments reject and accept real fragments");

        RasterizerState biased = RasterizerState::CullNone;
        biased.setDepthBiasProperty(0.0001f);
        device.setRasterizerStateProperty(biased);
        device.SetRenderTarget(&depth16);
        const float depth16Bias = Bridge::GetPipelineSnapshotForTesting().polygonOffsetUnits;
        device.SetRenderTarget(&depth24);
        const float depth24Bias = Bridge::GetPipelineSnapshotForTesting().polygonOffsetUnits;
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(std::fabs(depth16Bias - 6.5535f) < 0.02f &&
                  std::fabs(depth24Bias - 1677.7215f) < 0.1f,
              "target switching rescales normalized depth bias to attachment precision");
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        device.SetRenderTarget(&depthStencil);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(Color::Black, 1.0f);
        DepthStencilState stencilWriter;
        stencilWriter.setDepthBufferEnableProperty(false);
        stencilWriter.setDepthBufferWriteEnableProperty(false);
        stencilWriter.setStencilEnableProperty(true);
        stencilWriter.setStencilFunctionProperty(CompareFunction::Always);
        stencilWriter.setStencilPassProperty(StencilOperation::Replace);
        stencilWriter.setStencilMaskProperty(0xFF);
        stencilWriter.setStencilWriteMaskProperty(0xFF);
        stencilWriter.setReferenceStencilProperty(3);
        device.setDepthStencilStateProperty(stencilWriter);
        white.GetRenderer().BindGL(0);
        Bridge::DrawBoundTextureSampleForTesting(
            0.5f, 0.5f, kTargetSize, kTargetSize);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        DepthStencilState stencilReader = stencilWriter;
        stencilReader.setStencilFunctionProperty(CompareFunction::Equal);
        stencilReader.setStencilPassProperty(StencilOperation::Keep);
        stencilReader.setStencilWriteMaskProperty(0);
        stencilReader.setReferenceStencilProperty(4);
        device.setDepthStencilStateProperty(stencilReader);
        white.GetRenderer().BindGL(0);
        Bridge::DrawBoundTextureSampleForTesting(
            0.5f, 0.5f, kTargetSize, kTargetSize);
        const bool stencilRejected = Matches(ReadPixel(depthStencil), Color::Black);
        device.setReferenceStencilProperty(3);
        white.GetRenderer().BindGL(0);
        Bridge::DrawBoundTextureSampleForTesting(
            0.5f, 0.5f, kTargetSize, kTargetSize);
        const bool stencilAccepted = Matches(ReadPixel(depthStencil), Color::White);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(stencilRejected && stencilAccepted,
              "Depth24Stencil8 attachment preserves stencil writes across color clears");

        RenderTarget2D mipTarget(
            device, kTargetSize, kTargetSize, true, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&mipTarget);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(Color::Magenta);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Rectangle mipRectangle(1, 1, 1, 1);
        Color mipPixel(0, 0, 0, 0);
        mipTarget.GetData(1, &mipRectangle, &mipPixel, 0, 1);
        Check(mipTarget.getLevelCountProperty() == 5 && Matches(mipPixel, Color::Magenta),
              "mipMap RenderTarget2D regenerates and reads a non-zero mip level");

        RenderTarget2D preserved(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&preserved);
        device.Clear(Color::Green);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.SetRenderTarget(&preserved);
        const bool preservedColor = Matches(ReadPixel(preserved), Color::Green);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        RenderTarget2D discarded(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&discarded);
        device.Clear(Color::Red);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.SetRenderTarget(&discarded);
        const bool discardedToBlack = Matches(ReadPixel(discarded), Color::Black);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(preservedColor && discardedToBlack,
              "PreserveContents survives switches while DiscardContents clears on bind");

        device.setDepthStencilStateProperty(DepthStencilState::Default);
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    return CNA::Examples::RunPixelTest<RlglRenderTargetTest>();
}
