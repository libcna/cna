// SPDX-License-Identifier: MS-PL
// cna-killer KF-5: a multisampled target is resolved with glBlitFramebuffer, which obeys the
// scissor test. Resolving while a game's scissor rectangle was active copied only that rectangle,
// so everything outside it kept whatever the single-sample texture held before -- and with a
// multisampled back buffer GetBackBufferData, which resolves first, read zeros there.
//
// Each case clears to blue (XNA's Clear ignores the scissor), draws a red quad under a small
// scissor rectangle, resolves, and reads a pixel outside the rectangle (blue) and one inside (red):
// a multisampled RenderTarget2D, a multisampled RenderTargetCube face, and the back buffer.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MsaaResolveIgnoresScissorTest final : public CNA::Examples::PixelTestGame
{
public:
    MsaaResolveIgnoresScissorTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setPreferMultiSamplingProperty(true);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        BasicEffect effect(device);
        effect.setVertexColorEnabledProperty(true);
        const std::vector<VertexPositionColor> quad{
            {Vector3(-1, 1, 0), Color::Red}, {Vector3(-1, -1, 0), Color::Red},
            {Vector3(1, -1, 0), Color::Red}, {Vector3(-1, 1, 0), Color::Red},
            {Vector3(1, -1, 0), Color::Red}, {Vector3(1, 1, 0), Color::Red}};
        RasterizerState scissored;
        scissored.setCullModeProperty(CullMode::None);
        scissored.setScissorTestEnableProperty(true);

        // Clear, then a red quad clipped to (4,4 4x4); the scissor test stays enabled afterwards,
        // as it would in a game that resolves the target next.
        const auto drawScissored = [&] {
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.Clear(Color::Blue);
            device.setRasterizerStateProperty(scissored);
            device.setScissorRectangleProperty(Rectangle(4, 4, 4, 4));
            effect.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2,
                                      VertexPositionColor::getVertexDeclarationStatic());
        };
        const auto report = [&](const char* what, const std::vector<Color>& pixels, int width) {
            const Color outside = pixels[0];
            const Color inside = pixels[static_cast<std::size_t>(5 * width + 5)];
            const bool pass = outside == Color::Blue && inside == Color::Red;
            std::printf("[%s] %s resolved under a scissor keeps the whole target: outside (%d,%d,%d), "
                        "inside (%d,%d,%d)\n",
                        pass ? "PASS" : "FAIL", what, outside.getRProperty(), outside.getGProperty(),
                        outside.getBProperty(), inside.getRProperty(), inside.getGProperty(),
                        inside.getBProperty());
            if (!pass) { MarkFailedEXT(); }
        };

        {
            RenderTarget2D target(device, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 4,
                                  RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            drawScissored();
            device.SetRenderTarget(nullptr);
            std::vector<Color> pixels(256);
            target.GetData(pixels.data(), 256);
            report("a multisampled RenderTarget2D", pixels, 16);
        }
        {
            RenderTargetCube cube(device, 16, false, SurfaceFormat::Color, DepthFormat::None, 4,
                                  RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&cube, CubeMapFace::PositiveX);
            drawScissored();
            device.SetRenderTarget(nullptr);
            std::vector<Color> pixels(256);
            cube.GetData(CubeMapFace::PositiveX, pixels.data(), 256);
            report("a multisampled RenderTargetCube face", pixels, 16);
        }
        if (device.getPresentationParametersProperty().getMultiSampleCountProperty() > 1)
        {
            drawScissored();
            std::vector<Color> pixels(16 * 16);
            const Rectangle corner(0, 0, 16, 16);
            device.GetBackBufferData(&corner, pixels.data(), 0, 256);
            report("the multisampled back buffer", pixels, 16);
        }
        else
        {
            std::printf("[SKIP] the back buffer is not multisampled on this device\n");
        }
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    return CNA::Examples::RunPixelTest<MsaaResolveIgnoresScissorTest>();
}
