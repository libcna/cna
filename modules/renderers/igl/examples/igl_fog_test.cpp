// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-38/IGL-55: BasicEffect fog pixel conformance -- FNA's fogVector view-space
// formulation (see modules/graphics/src/Xna/BasicEffect.cpp's REMED-GFX-010 comment).
//
// With World=View=identity, fogVector reduces so that dot(objectPosition, fogVector) equals
// saturate((z + fogStart) / (fogStart - fogEnd)); with fogStart=0 and fogEnd=-1 that reduces
// further to plain saturate(z), so the object-space z value doubles as the fog factor directly.
// Object-space z also doubles as clip-space z here (Projection is identity too), and this renderer
// clips to a [0, 1] range (matching igl_3d_test.cpp's own positive-z depths) rather than OpenGL's
// traditional [-1, 1] -- so z is kept inside (0, 1) here for the same reason igl_3d_test.cpp never
// used a negative depth. Scene: three side-by-side red quads at z=0.05, z=0.5 and z=0.95, white fog
// colour -- expected fog factors 0.05, 0.5 and 0.95, so the quads shade from nearly pure red
// through a half red/white blend to nearly pure white.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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
    constexpr int kSize = 90;
}

class IglFogTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// A screen-third-wide quad at a given object-space depth, all in the same on-screen X range so
    /// varying z (with no projection applied) never moves where it lands on screen.
    static std::vector<VertexPositionColor> Quad(const float left, const float right, const float z)
    {
        const Color red(static_cast<bytecs>(255), static_cast<bytecs>(0), static_cast<bytecs>(0),
                        static_cast<bytecs>(255));
        return {
            VertexPositionColor(Vector3(left, -1.0f, z), red),
            VertexPositionColor(Vector3(right, -1.0f, z), red),
            VertexPositionColor(Vector3(right, 1.0f, z), red),
            VertexPositionColor(Vector3(left, 1.0f, z), red),
        };
    }

    void DrawQuad(GraphicsDevice& device, BasicEffect& effect, const float left, const float right,
                 const float z)
    {
        std::vector<VertexPositionColor> vertices = Quad(left, right, z);
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

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setFogEnabledProperty(true);
        effect.setFogColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setFogStartProperty(0.0f);
        effect.setFogEndProperty(-1.0f);

        DrawQuad(device, effect, -1.0f, -1.0f / 3.0f, 0.05f);       // factor 0.05: nearly pure red
        DrawQuad(device, effect, -1.0f / 3.0f, 1.0f / 3.0f, 0.5f);  // factor 0.5: red/white blend
        DrawQuad(device, effect, 1.0f / 3.0f, 1.0f, 0.95f);          // factor 0.95: nearly pure white

        ExpectPixel("z=0.05 is nearly unfogged", Rectangle(kSize / 6, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(255), static_cast<bytecs>(13),
                          static_cast<bytecs>(13), static_cast<bytecs>(255)),
                    /*tolerance=*/15);
        ExpectPixel("the midpoint blends red and the white fog colour",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(255), static_cast<bytecs>(127),
                          static_cast<bytecs>(127), static_cast<bytecs>(255)),
                    /*tolerance=*/15);
        ExpectPixel("z=0.95 is nearly fully fogged to the fog colour",
                    Rectangle(5 * kSize / 6, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(255), static_cast<bytecs>(242),
                          static_cast<bytecs>(242), static_cast<bytecs>(255)),
                    /*tolerance=*/15);
    }

public:
    IglFogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglFogTest>();
}
