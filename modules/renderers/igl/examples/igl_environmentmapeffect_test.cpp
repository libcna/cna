// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-35/IGL-55: EnvironmentMapEffect pixel conformance -- the first TextureCube
// exercise for this renderer family.
//
// EnvironmentMapAmount=1 makes the shader's `mix(baseColor, envColor, amount)` collapse to plain
// `envColor` (see IglShaderLibrary.cpp) -- but only once FresnelFactor is explicitly zeroed:
// real XNA/FNA's EnvironmentMapEffect defaults FresnelFactor to 1.0 (enabled), and this test's
// quad faces the camera directly, which is exactly the viewing angle where the Fresnel term
// (`pow(1 - abs(dot(eye, normal)), fresnelFactor)`) evaluates to zero and would silently zero out
// the whole env-map contribution -- physically correct (Fresnel reflectance is lowest head-on),
// but not what this test wants to exercise. With Fresnel off, whatever the cubemap returns for the
// reflected view direction is the whole story -- no need to derive the exact reflection vector for
// a solid-colour cube, which returns the same colour regardless of which face direction actually
// got sampled. A quad facing the camera directly is enough to prove real TextureCube sampling and
// the full env-map replacement path both work.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kCubeSize = 4;

    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& device, const Color& colour)
    {
        auto cube = std::make_unique<TextureCube>(device, kCubeSize, false, SurfaceFormat::Color);
        const std::vector<Color> pixels(static_cast<std::size_t>(kCubeSize) * kCubeSize, colour);
        const std::array<CubeMapFace, 6> faces = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
            CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (const CubeMapFace face : faces)
            cube->SetData(face, pixels.data(), static_cast<int>(pixels.size()));
        return cube;
    }
}

class IglEnvironmentMapEffectTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        std::unique_ptr<TextureCube> cube =
            MakeSolidCube(device, Color(static_cast<bytecs>(0), static_cast<bytecs>(200),
                                        static_cast<bytecs>(200), static_cast<bytecs>(255)));

        EnvironmentMapEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setEnvironmentMapProperty(cube.get());
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setFresnelFactorProperty(0.0f);

        // A quad facing the camera, well within the [0, 1] clip-z range this renderer's own test
        // family uses (see igl_fog_test.cpp).
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const std::vector<VertexPositionNormalTexture> vertices = {
            VertexPositionNormalTexture(Vector3(-1.0f, -1.0f, 0.5f), normal, Vector2(0.0f, 1.0f)),
            VertexPositionNormalTexture(Vector3(1.0f, -1.0f, 0.5f), normal, Vector2(1.0f, 1.0f)),
            VertexPositionNormalTexture(Vector3(1.0f, 1.0f, 0.5f), normal, Vector2(1.0f, 0.0f)),
            VertexPositionNormalTexture(Vector3(-1.0f, 1.0f, 0.5f), normal, Vector2(0.0f, 0.0f)),
        };
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        VertexBuffer vertexBuffer(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
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

        ExpectPixel("EnvironmentMapAmount=1 replaces the surface colour with the cube sample",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(200),
                          static_cast<bytecs>(200), static_cast<bytecs>(255)),
                    /*tolerance=*/10);
    }

public:
    IglEnvironmentMapEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglEnvironmentMapEffectTest>();
}
