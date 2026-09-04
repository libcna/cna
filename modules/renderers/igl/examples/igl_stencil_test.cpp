// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-39/IGL-55: DepthStencilState's stencil test, wired into the real 3D pipeline.
//
// Scene: clear colour+depth+stencil to black/0. Draw a blue quad covering the LEFT half, with
// StencilFunction::Always/StencilPass::Replace/ReferenceStencil=5 -- stamps stencil=5 across the
// left half only. Then draw a full-screen green quad with StencilFunction::Equal,
// ReferenceStencil=5 -- its fragments only pass the stencil test where the stamp already wrote 5.
//
// A WORKING stencil test therefore leaves the left half green (stamped, then re-covered) and the
// right half BLACK (stencil=0 there never matches reference 5, so the green draw is discarded and
// the clear colour survives). If the stencil test were silently bypassed -- every fragment always
// passing regardless of the buffer -- the right half would incorrectly turn green too: that
// negative case is what actually proves the gate works, not just that a quad renders.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
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
}

class IglStencilTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    static std::vector<VertexPositionColor> Quad(const float left, const float right,
                                                 const Color& colour)
    {
        return {
            VertexPositionColor(Vector3(left, -1.0f, 0.05f), colour),
            VertexPositionColor(Vector3(right, -1.0f, 0.05f), colour),
            VertexPositionColor(Vector3(right, 1.0f, 0.05f), colour),
            VertexPositionColor(Vector3(left, 1.0f, 0.05f), colour),
        };
    }

    void DrawQuad(GraphicsDevice& device, BasicEffect& effect, const float left, const float right,
                 const Color& colour, const DepthStencilState& state)
    {
        device.setDepthStencilStateProperty(state);

        std::vector<VertexPositionColor> vertices = Quad(left, right, colour);
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
    }

    static DepthStencilState StampState()
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(CompareFunction::Always);
        state.setStencilPassProperty(StencilOperation::Replace);
        state.setReferenceStencilProperty(5);
        return state;
    }

    static DepthStencilState TestState()
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(CompareFunction::Equal);
        state.setStencilPassProperty(StencilOperation::Keep);
        state.setReferenceStencilProperty(5);
        return state;
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                          static_cast<bytecs>(255)),
                    1.0f, 0);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());

        // Stamp stencil=5 across the left half only.
        DrawQuad(device, effect, -1.0f, 0.0f,
                Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(255),
                      static_cast<bytecs>(255)),
                StampState());

        // Full-screen green, gated on stencil==5.
        DrawQuad(device, effect, -1.0f, 1.0f,
                Color(static_cast<bytecs>(0), static_cast<bytecs>(255), static_cast<bytecs>(0),
                      static_cast<bytecs>(255)),
                TestState());

        ExpectPixel("the stamped left half passes the stencil test and shows green",
                    Rectangle(kSize / 4, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));
        ExpectPixel("the unstamped right half fails the stencil test and stays the clear colour",
                    Rectangle(3 * kSize / 4, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(0),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));
    }

public:
    IglStencilTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglStencilTest>();
}
