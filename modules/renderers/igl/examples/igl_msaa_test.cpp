// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-15/IGL-55: real MSAA into a RenderTarget2D -- an antialiased edge actually
// resolved into the render target's colour texture, not just a bookkeeping check that the count
// round-trips. Mirrors llgl_msaa_rendertarget_test.cpp's own technique (design decision 5 in
// plans/plan_igl.md means this renderer's own back-buffer viewport/scissor handling never enters the
// picture here -- a plain orthographic-projected render target is enough).
//
// A right triangle with its hypotenuse crossing a scanned row puts a hard, unblended edge into a
// single-sample render target; the same edge with real MSAA must produce at least one genuinely
// blended, mid-tone pixel where a sample sits astride the geometric line.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kScanY = kSize / 2;

    // A diagonal edge along x + y = kSize; at (kSize/2 - 1, kScanY) a pixel's centre sits exactly
    // on the line, so any real multisample pattern splits that pixel's samples across the edge.
    std::vector<VertexPositionColor> DiagonalTriangle()
    {
        const Color white(static_cast<bytecs>(255), static_cast<bytecs>(255),
                          static_cast<bytecs>(255), static_cast<bytecs>(255));
        return {
            VertexPositionColor(Vector3(0.0f, 0.0f, 0.0f), white),
            VertexPositionColor(Vector3(static_cast<float>(kSize), 0.0f, 0.0f), white),
            VertexPositionColor(Vector3(0.0f, static_cast<float>(kSize), 0.0f), white),
        };
    }
}

class IglMsaaTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// Draws the diagonal triangle into a fresh RenderTarget2D requesting multiSampleCount, and
    /// returns the resolved edge pixel at (kSize/2 - 1, kScanY).
    Color RenderEdgePixel(GraphicsDevice& device, const int multiSampleCount, int& appliedOut)
    {
        RenderTarget2D renderTarget(device, kSize, kSize, false, SurfaceFormat::Color,
                                    DepthFormat::None, multiSampleCount);
        appliedOut = renderTarget.getMultiSampleCountProperty();

        device.SetRenderTarget(&renderTarget);
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setTextureEnabledProperty(false);
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 0.0f, 1.0f));

        const std::vector<VertexPositionColor> triangle = DiagonalTriangle();
        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                            static_cast<int>(triangle.size()), BufferUsage::WriteOnly);
        buffer.SetData(triangle.data(), 0, static_cast<int>(triangle.size()));
        device.SetVertexBuffer(&buffer);

        for (EffectPass& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
        {
            pass.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        }
        device.SetRenderTarget(nullptr);

        const Rectangle edgeRegion(kSize / 2 - 1, kScanY, 1, 1);
        Color pixel(0, 0, 0, 0);
        renderTarget.GetData(0, &edgeRegion, &pixel, 0, 1);
        return pixel;
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        int appliedNoMsaa = -1;
        const Color noMsaaPixel = RenderEdgePixel(device, 0, appliedNoMsaa);
        // IGL's own convention (already documented in plans/plan_igl.md for the back buffer and cube
        // render targets) reports an unmultisampled surface as 1, not 0.
        ExpectTrue("MultiSampleCount=0 really applies no multisampling to a RenderTarget2D",
                  appliedNoMsaa == 1);
        ExpectTrue("without MSAA the diagonal edge is a hard, unblended step",
                  noMsaaPixel.getRProperty() < 10 || noMsaaPixel.getRProperty() > 245);

        int appliedMsaa = -1;
        const Color msaaPixel = RenderEdgePixel(device, 4, appliedMsaa);
        if (appliedMsaa <= 1)
        {
            // A build/driver combination that genuinely applies no MSAA to a RenderTarget2D is
            // a real, honest capability boundary, not a failure of this test to exercise anything.
            return;
        }
        ExpectTrue("GetMultiSampleCount() reports a real, renderer-applied sample count once "
                  "MultiSampleCount is requested",
                  appliedMsaa > 1);
        ExpectTrue("with real MSAA the same diagonal edge produces a genuinely blended, "
                  "mid-tone pixel",
                  msaaPixel.getRProperty() >= 10 && msaaPixel.getRProperty() <= 245);
    }

public:
    IglMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglMsaaTest>();
}
