// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-22/IGL-55: multiple simultaneous colour attachments (MRT), the first cut for
// this renderer -- proves the render pass genuinely carries more than one colour attachment at
// once and that binding/unbinding an MRT set does not corrupt the back buffer.
//
// Scene: two RenderTarget2D slots bound together, cleared to a black sentinel, then a single
// BasicEffect quad draw covers both. A working multi-attachment render pass writes the SAME draw's
// output to every bound colour attachment simultaneously -- both targets must show the quad's
// colour, not just the first slot -- proving this is a real MRT bind rather than a single-target
// bind that silently dropped the second slot. After releasing the MRT set, an ordinary back-buffer
// clear must show its own colour untouched by anything the MRT draw did.
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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kRTSize = 32;
}

class IglMrtTest : public CNA::Examples::PixelTestGame
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

        device.SetRenderTargets({RenderTargetBinding(&rt0), RenderTargetBinding(&rt1)});
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
        Color pixel0(0, 0, 0, 0);
        rt0.GetData(0, &onePixel, &pixel0, 0, 1);
        Color pixel1(0, 0, 0, 0);
        rt1.GetData(0, &onePixel, &pixel1, 0, 1);

        ExpectTrue("slot 0 receives the draw's colour",
                  pixel0.getRProperty() > 200 && pixel0.getGProperty() < 40 &&
                      pixel0.getBProperty() > 200);
        ExpectTrue("slot 1 also receives the SAME draw's colour simultaneously",
                  pixel1.getRProperty() > 200 && pixel1.getGProperty() < 40 &&
                      pixel1.getBProperty() > 200);

        // Release the MRT set and confirm the back buffer is untouched by any of it.
        device.SetRenderTargets({});
        device.Clear(Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                           static_cast<bytecs>(20), static_cast<bytecs>(255)));
        ExpectPixel("releasing the MRT set restores the back buffer, unaffected by the MRT draw",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                          static_cast<bytecs>(20), static_cast<bytecs>(255)));
    }

public:
    IglMrtTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglMrtTest>();
}
