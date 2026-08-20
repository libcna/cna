// SPDX-License-Identifier: MS-PL
// plans/plan_fna3d.md FNA3D-12: graphics-state oracle for the FNA3D renderer. Every state this renderer
// applies goes into one of FNA3D's four state structures, whose fields are the XNA 4.0 fields, so
// these checks are about the mapping being real and complete rather than about FNA3D's own
// rasterization.
//
// Check A -- BlendState::AlphaBlend really blends against what is already in the buffer.
// Check B -- BlendState::Additive sums instead.
// Check C -- a BlendState with a restricted ColorWriteChannels mask leaves the masked channels
//   of the destination alone (the write state carried alongside the six blend ordinals).
// Check D -- the BlendFactor blend modes consume GraphicsDevice.BlendFactor.
// Check E -- the scissor rectangle clips, and disabling the scissor test un-clips.
// Check F -- a non-default DepthStencilState comparison really changes which fragment survives.
// Check G -- the stencil buffer masks a second draw (write-then-test through ReferenceStencil).
// Check H -- a viewport sub-rectangle confines rendering to that rectangle.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class Fna3dStateTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<BasicEffect> basic_;

    void DrawQuad(const Color& color)
    {
        auto& device = getGraphicsDeviceProperty();
        const VertexPositionColor vertices[6] = {
            { Vector3(-1.0f, 1.0f, 0.5f), color },  { Vector3(-1.0f, -1.0f, 0.5f), color },
            { Vector3(1.0f, -1.0f, 0.5f), color },  { Vector3(-1.0f, 1.0f, 0.5f), color },
            { Vector3(1.0f, -1.0f, 0.5f), color },  { Vector3(1.0f, 1.0f, 0.5f), color },
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

        const int width = device.getViewportProperty().getWidthProperty();
        const int height = device.getViewportProperty().getHeightProperty();
        const Rectangle centre(width / 2, height / 2, 1, 1);

        // Check A -- alpha blending. XNA's BlendState.AlphaBlend is PREMULTIPLIED
        // (One / InverseSourceAlpha), so a half-alpha grey over blue gives
        // src.rgb + dst.rgb * (1 - 0.5) = (128,128,128) + (0,0,128) = (128,128,255).
        device.setBlendStateProperty(BlendState::AlphaBlend);
        device.Clear(Color(0, 0, 255, 255));
        DrawQuad(Color(128, 128, 128, 128));
        ExpectPixel("AlphaBlend blends the premultiplied source over the destination", centre,
                    Color(128, 128, 255, 255), 4);

        // Check B -- additive blending. Red added onto blue is magenta.
        device.setBlendStateProperty(BlendState::Additive);
        device.Clear(Color(0, 0, 255, 255));
        DrawQuad(Color(255, 0, 0, 255));
        ExpectPixel("Additive sums source and destination", centre, Color(255, 0, 255, 255), 2);

        // Check C -- ColorWriteChannels masks the output merger. Writing red-only over a blue
        // buffer must leave the blue channel exactly as it was.
        BlendState redOnly;
        redOnly.setColorSourceBlendProperty(Blend::One);
        redOnly.setColorDestinationBlendProperty(Blend::Zero);
        redOnly.setAlphaSourceBlendProperty(Blend::One);
        redOnly.setAlphaDestinationBlendProperty(Blend::Zero);
        redOnly.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        device.setBlendStateProperty(redOnly);
        device.Clear(Color(0, 0, 255, 255));
        DrawQuad(Color(255, 255, 255, 255));
        ExpectPixel("ColorWriteChannels::Red writes red and preserves green/blue", centre,
                    Color(255, 0, 255, 255), 2);

        // Check D -- the BlendFactor blend modes consume GraphicsDevice.BlendFactor.
        BlendState byFactor;
        byFactor.setColorSourceBlendProperty(Blend::BlendFactor);
        byFactor.setColorDestinationBlendProperty(Blend::Zero);
        byFactor.setAlphaSourceBlendProperty(Blend::One);
        byFactor.setAlphaDestinationBlendProperty(Blend::Zero);
        device.setBlendStateProperty(byFactor);
        device.setBlendFactorProperty(Color(128, 128, 128, 255));
        device.Clear(Color(0, 0, 0, 255));
        DrawQuad(Color(255, 255, 255, 255));
        ExpectPixel("Blend::BlendFactor scales the source by GraphicsDevice.BlendFactor", centre,
                    Color(128, 128, 128, 255), 4);
        device.setBlendFactorProperty(Color(255, 255, 255, 255));
        device.setBlendStateProperty(BlendState::Opaque);

        // Check E -- the scissor rectangle.
        RasterizerState scissored;
        scissored.setCullModeProperty(CullMode::None);
        scissored.setScissorTestEnableProperty(true);
        device.setScissorRectangleProperty(Rectangle(0, 0, width / 2, height));
        device.setRasterizerStateProperty(scissored);
        device.Clear(Color(0, 0, 0, 255));
        DrawQuad(Color(255, 255, 0, 255));
        ExpectPixel("scissor keeps the left half", Rectangle(width / 4, height / 2, 1, 1),
                    Color(255, 255, 0, 255), 2);
        ExpectPixel("scissor clips the right half", Rectangle(3 * width / 4, height / 2, 1, 1),
                    Color(0, 0, 0, 255));

        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 0, 0, 255));
        DrawQuad(Color(255, 255, 0, 255));
        ExpectPixel("disabling the scissor test un-clips the right half",
                    Rectangle(3 * width / 4, height / 2, 1, 1), Color(255, 255, 0, 255), 2);

        // Check F -- a non-default depth comparison.
        DepthStencilState greater;
        greater.setDepthBufferEnableProperty(true);
        greater.setDepthBufferWriteEnableProperty(true);
        greater.setDepthBufferFunctionProperty(CompareFunction::Greater);
        device.setDepthStencilStateProperty(greater);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 0.0f,
                     0);
        DrawQuad(Color(0, 255, 0, 255));
        ExpectPixel("CompareFunction::Greater passes against a zero-cleared depth buffer", centre,
                    Color(0, 255, 0, 255), 2);

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f,
                     0);
        DrawQuad(Color(0, 255, 0, 255));
        ExpectPixel("CompareFunction::Greater rejects against a one-cleared depth buffer", centre,
                    Color(0, 0, 0, 255));
        device.setDepthStencilStateProperty(DepthStencilState::None);

        // Check G -- stencil write then stencil test.
        DepthStencilState stencilWrite;
        stencilWrite.setDepthBufferEnableProperty(false);
        stencilWrite.setStencilEnableProperty(true);
        stencilWrite.setStencilFunctionProperty(CompareFunction::Always);
        stencilWrite.setStencilPassProperty(StencilOperation::Replace);
        stencilWrite.setReferenceStencilProperty(1);

        DepthStencilState stencilTest;
        stencilTest.setDepthBufferEnableProperty(false);
        stencilTest.setStencilEnableProperty(true);
        stencilTest.setStencilFunctionProperty(CompareFunction::Equal);
        stencilTest.setStencilPassProperty(StencilOperation::Keep);
        stencilTest.setReferenceStencilProperty(1);

        device.Clear(ClearOptions::Target | ClearOptions::Stencil, Color(0, 0, 0, 255), 1.0f, 0);
        // Stamp stencil=1 over the left half only, with colour writes off.
        BlendState noColor;
        noColor.setColorWriteChannelsProperty(ColorWriteChannels::None);
        device.setBlendStateProperty(noColor);
        device.setDepthStencilStateProperty(stencilWrite);
        RasterizerState leftHalf;
        leftHalf.setCullModeProperty(CullMode::None);
        leftHalf.setScissorTestEnableProperty(true);
        device.setScissorRectangleProperty(Rectangle(0, 0, width / 2, height));
        device.setRasterizerStateProperty(leftHalf);
        DrawQuad(Color::White);

        // Now draw everywhere, but only where stencil == 1.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(stencilTest);
        DrawQuad(Color(255, 0, 0, 255));
        ExpectPixel("the stencil test keeps the stamped half",
                    Rectangle(width / 4, height / 2, 1, 1), Color(255, 0, 0, 255), 2);
        ExpectPixel("the stencil test rejects the unstamped half",
                    Rectangle(3 * width / 4, height / 2, 1, 1), Color(0, 0, 0, 255));
        device.setDepthStencilStateProperty(DepthStencilState::None);

        // Check H -- a viewport sub-rectangle confines rendering.
        const Viewport fullViewport = device.getViewportProperty();
        device.Clear(Color(0, 0, 0, 255));
        device.setViewportProperty(Viewport(0, 0, width / 2, height / 2));
        DrawQuad(Color(0, 255, 255, 255));
        device.setViewportProperty(fullViewport);
        ExpectPixel("a viewport sub-rectangle renders inside itself",
                    Rectangle(width / 4, height / 4, 1, 1), Color(0, 255, 255, 255), 2);
        ExpectPixel("a viewport sub-rectangle renders nothing outside itself",
                    Rectangle(3 * width / 4, 3 * height / 4, 1, 1), Color(0, 0, 0, 255));
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3dStateTest>();
}
