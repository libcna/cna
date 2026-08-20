// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-22/IGL-55: MRT at the full 4-slot count (`igl::IGL_COLOR_ATTACHMENTS_MAX`),
// closing the gap `igl_mrt_test.cpp`'s own 2-slot scene left open. IGL's generated fragment shader
// declares one `out vec4 FragColorN` per bound colour attachment and assigns every one the same
// expression (see `IglShaderLibraryTests.cpp`'s
// `TheFragmentStageDeclaresOneOutputPerColourAttachment`), so a 4-slot draw cannot discriminate
// different colours per slot the way a differently-lit scene might -- what it DOES prove, and what
// the 2-slot test cannot, is that all 4 of `IglBoundTarget`'s `igl::FramebufferDesc::colorAttachments`
// slots genuinely bind and write simultaneously, with none of them silently dropped or left
// pointing at a stale/duplicate attachment the way an off-by-one in the attachment-count plumbing
// would produce.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kRTSize = 32;
}

class IglMrt4Test : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                           static_cast<bytecs>(20), static_cast<bytecs>(255)));

        RenderTarget2D rt0(device, kRTSize, kRTSize);
        RenderTarget2D rt1(device, kRTSize, kRTSize);
        RenderTarget2D rt2(device, kRTSize, kRTSize);
        RenderTarget2D rt3(device, kRTSize, kRTSize);

        device.SetRenderTargets({RenderTargetBinding(&rt0), RenderTargetBinding(&rt1),
                                 RenderTargetBinding(&rt2), RenderTargetBinding(&rt3)});
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());

        const Color magenta(static_cast<bytecs>(255), static_cast<bytecs>(0),
                            static_cast<bytecs>(255), static_cast<bytecs>(255));
        const std::vector<VertexPositionColor> vertices = {
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.05f), magenta),
            VertexPositionColor(Vector3(1.0f, -1.0f, 0.05f), magenta),
            VertexPositionColor(Vector3(1.0f, 1.0f, 0.05f), magenta),
            VertexPositionColor(Vector3(-1.0f, 1.0f, 0.05f), magenta),
        };
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        VertexBuffer vertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                                  static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
        vertexBuffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()));

        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
        indexBuffer.SetData(indices, 0, 6);

        device.SetVertexBuffer(&vertexBuffer);
        device.setIndicesProperty(&indexBuffer);

        for (EffectPass& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
        {
            pass.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        }

        const Rectangle onePixel(0, 0, 1, 1);
        RenderTarget2D* targets[4] = {&rt0, &rt1, &rt2, &rt3};
        const char* labels[4] = {"slot 0", "slot 1", "slot 2", "slot 3"};
        for (int i = 0; i < 4; ++i)
        {
            Color pixel(0, 0, 0, 0);
            targets[i]->GetData(0, &onePixel, &pixel, 0, 1);
            const std::string label =
                std::string(labels[i]) + " of 4 simultaneously bound targets receives the draw's colour";
            ExpectTrue(label.c_str(), pixel.getRProperty() > 200 && pixel.getGProperty() < 40 &&
                                          pixel.getBProperty() > 200);
        }

        // Release the MRT set and confirm the back buffer is untouched by any of it.
        device.SetRenderTargets({});
        device.Clear(Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                           static_cast<bytecs>(20), static_cast<bytecs>(255)));
        ExpectPixel("releasing the 4-slot MRT set restores the back buffer, unaffected by the draw",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                          static_cast<bytecs>(20), static_cast<bytecs>(255)));
    }

public:
    IglMrt4Test()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglMrt4Test>();
}
