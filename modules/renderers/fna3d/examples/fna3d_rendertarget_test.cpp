// SPDX-License-Identifier: MS-PL
// plans/plan_fna3d.md FNA3D-10: render-target oracle for the FNA3D renderer. FNA3D models a render
// target as a destination texture plus, when multisampled, a separate colour renderbuffer that
// must be resolved into it, with the depth/stencil renderbuffer handed to SetRenderTargets as a
// third object -- so these checks exercise binding, drawing, resolving and reading back, not just
// allocation.
//
// Check A -- a render target reads back the colour it was cleared to, and the backbuffer is
//   untouched by that clear.
// Check B -- geometry drawn into a bound target lands in the target, at the right place.
// Check C -- unbinding restores the backbuffer as the draw destination.
// Check D -- the rendered target can then be sampled as a texture by a SpriteBatch draw.
// Check E -- a target created with DepthFormat::None honestly reports no depth buffer, and one
//   created with Depth24Stencil8 reports both depth and stencil.
// Check F -- a multisampled target reports the device-clamped sample count it really allocated
//   and still resolves to a readable texture.
// Check G -- RenderTargetUsage::PreserveContents keeps previously rendered colour across a
//   re-bind, and DiscardContents is not required to.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetSize = 32;

    Color ReadTarget(RenderTarget2D& target, int x, int y)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        target.GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }

    bool ColorNear(const Color& got, const Color& want, int tolerance = 2)
    {
        const auto close = [tolerance](int a, int b) {
            return (a > b ? a - b : b - a) <= tolerance;
        };
        return close(got.getRProperty(), want.getRProperty()) &&
               close(got.getGProperty(), want.getGProperty()) &&
               close(got.getBProperty(), want.getBProperty());
    }
}

class Fna3dRenderTargetTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<BasicEffect> basic_;

    void DrawColoredQuad(float x0, float y0, float x1, float y1, const Color& color)
    {
        auto& device = getGraphicsDeviceProperty();
        const VertexPositionColor vertices[6] = {
            { Vector3(x0, y1, 0.5f), color }, { Vector3(x0, y0, 0.5f), color },
            { Vector3(x1, y0, 0.5f), color }, { Vector3(x0, y1, 0.5f), color },
            { Vector3(x1, y0, 0.5f), color }, { Vector3(x1, y1, 0.5f), color },
        };
        basic_->VertexColorEnabled = true;
        basic_->setTextureEnabledProperty(false);
        basic_->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        basic_ = std::make_unique<BasicEffect>(device);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        const int backbufferWidth = device.getViewportProperty().getWidthProperty();
        const int backbufferHeight = device.getViewportProperty().getHeightProperty();
        const Rectangle backbufferCentre(backbufferWidth / 2, backbufferHeight / 2, 1, 1);

        device.Clear(Color(0, 0, 255, 255));

        // Check A -- clear a bound target, then read it back.
        RenderTarget2D target(device, kTargetSize, kTargetSize);
        device.SetRenderTarget(&target);
        device.Clear(Color(255, 128, 0, 255));
        device.SetRenderTarget(nullptr);
        Check(ColorNear(ReadTarget(target, 4, 4), Color(255, 128, 0, 255)),
              "a render target reads back the colour it was cleared to");
        ExpectPixel("clearing the target left the backbuffer untouched", backbufferCentre,
                    Color(0, 0, 255, 255));

        // Check B -- geometry into the target, at a known place. The quad covers the left half of
        // the target in NDC, which is the left half in texels.
        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 255));
        DrawColoredQuad(-1.0f, -1.0f, 0.0f, 1.0f, Color(0, 255, 0, 255));
        device.SetRenderTarget(nullptr);
        Check(ColorNear(ReadTarget(target, 4, 16), Color(0, 255, 0, 255)),
              "geometry drawn into a bound target lands in its left half");
        Check(ColorNear(ReadTarget(target, 28, 16), Color(0, 0, 0, 255)),
              "the right half of the target keeps its cleared colour");

        // Check C -- the backbuffer is the destination again.
        device.Clear(Color(0, 0, 0, 255));
        DrawColoredQuad(-1.0f, -1.0f, 1.0f, 1.0f, Color(255, 0, 255, 255));
        ExpectPixel("unbinding restores the backbuffer as the draw destination", backbufferCentre,
                    Color(255, 0, 255, 255), 2);

        // Check D -- the rendered target is sampleable.
        device.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch batch(device);
            batch.Begin();
            batch.Draw(target, Rectangle(0, 0, 64, 64), Rectangle(0, 0, kTargetSize, kTargetSize),
                       Color::White);
            batch.End();
        }
        ExpectPixel("the rendered target samples as a texture (left half)", Rectangle(16, 32, 1, 1),
                    Color(0, 255, 0, 255), 2);
        ExpectPixel("the rendered target samples as a texture (right half)",
                    Rectangle(48, 32, 1, 1), Color(0, 0, 0, 255), 2);

        // Check E -- honest depth/stencil reporting.
        RenderTarget2D noDepth(device, 16, 16, false, SurfaceFormat::Color, DepthFormat::None);
        Check(noDepth.getDepthStencilFormatProperty() == DepthFormat::None,
              "a DepthFormat::None target reports no depth/stencil format");
        RenderTarget2D withStencil(device, 16, 16, false, SurfaceFormat::Color,
                                   DepthFormat::Depth24Stencil8);
        Check(withStencil.getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8,
              "a Depth24Stencil8 target reports that format back");

        // Check F -- MSAA clamping and resolve.
        RenderTarget2D multisampled(device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                                    DepthFormat::Depth24, 4, RenderTargetUsage::DiscardContents);
        const int appliedSamples = multisampled.getMultiSampleCountProperty();
        Check(appliedSamples == 0 || appliedSamples >= 2,
              "a multisampled target reports either no MSAA or a real sample count");
        std::printf("[info] requested 4x MSAA, device applied %dx\n", appliedSamples);
        device.SetRenderTarget(&multisampled);
        device.Clear(Color(10, 200, 30, 255));
        device.SetRenderTarget(nullptr);
        Check(ColorNear(ReadTarget(multisampled, 8, 8), Color(10, 200, 30, 255)),
              "a multisampled target resolves to a readable texture");

        // Check G -- PreserveContents survives a re-bind.
        RenderTarget2D preserved(device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                                 DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&preserved);
        device.Clear(Color(200, 20, 20, 255));
        device.SetRenderTarget(nullptr);
        device.SetRenderTarget(&preserved);
        device.SetRenderTarget(nullptr);
        Check(ColorNear(ReadTarget(preserved, 8, 8), Color(200, 20, 20, 255)),
              "RenderTargetUsage::PreserveContents keeps colour across a re-bind");
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3dRenderTargetTest>();
}
