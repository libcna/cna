// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-35/IGL-55: EnvironmentMapEffect's Fresnel edge-weighting, closing the gap
// `igl_environmentmapeffect_test.cpp` left open (that test explicitly zeroed FresnelFactor to
// isolate plain env-map replacement; this test turns Fresnel back on -- real XNA/FNA's own
// default, FresnelFactor=1 -- and proves the view-angle-dependent blend it produces is correct).
// Mirrors `bgfx_environmentmapeffect_fresnel_test.cpp`/`easygl_environmentmapeffect_fresnel_test.cpp`'s
// own derivation (Task 396), reusing the identical scene and expected pixel values -- the formula
// (`IglShaderLibrary.cpp`: `amount = uSpecularColor.w; if (Fresnel) amount *=
// pow(max(1-abs(dot(eye,normal)),0), uAmbientColor.w); color = mix(base, envColor, amount)`)
// and the FresnelFactor/EnvironmentMapAmount uniform packing (`ambientColor.w`/`specularColor.w`)
// are shared cross-renderer conventions, but IGL's own generated GLSL implementing them had never
// been run before this test.
//
// FNA's real EnvironmentMapEffect.fx: color = texture * pin.Diffuse (pin.Diffuse = EmissiveColor
// with no active lights, since this test never calls EnableDefaultLighting or sets a light);
// envColor = cube sample; final = lerp(color, envColor, fresnelWeightedAmount).
//   baseColor = texture(200,100,50) * EmissiveColor(0.5,0.5,0.5) = (100,50,25)
//
// (a) Grazing/coplanar camera (View=Identity): eyeVector has no Z component while the quad's
//     normal is pure Z, so viewAngle = dot(eye,normal) ~= 0 everywhere on the quad, and
//     pow(max(1-0,0),F) = 1 -- the Fresnel-weighted formula reduces to the flat
//     EnvironmentMapAmount (1.0) here, same as `igl_environmentmapeffect_test.cpp`'s own
//     Fresnel-disabled case. NOT discriminating (both formulas agree), included as a sanity
//     check this degenerate case still behaves sensibly. Expect the flat cube colour (128,128,128).
// (b) Head-on perspective camera (eye at (0,0,3) looking at the origin, quad normal (0,0,1)):
//     eyeVector is parallel to the normal at screen centre, so viewAngle ~= 1, and
//     pow(max(1-1,0),F) = pow(0,F) = 0 for F>0 -- Fresnel fully SUPPRESSES the cube map at
//     normal incidence (physically correct: reflectance is lowest head-on). Discriminating: a
//     renderer that ignored view angle entirely (the flat-Amount bug Task 396 found and fixed
//     across every other family) would still show the grey cube colour here instead of baseColor.
//     Expect baseColor (100,50,25).
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
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

    bool CloseTo(const Color& got, const Color& want, const int tolerance)
    {
        const auto close = [tolerance](const int a, const int b) {
            return std::abs(a - b) <= tolerance;
        };
        return close(got.getRProperty(), want.getRProperty()) &&
               close(got.getGProperty(), want.getGProperty()) &&
               close(got.getBProperty(), want.getBProperty());
    }
}

class IglEnvironmentMapEffectFresnelTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<TextureCube> cube_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<VertexBuffer> vertexBuffer_;
    std::unique_ptr<IndexBuffer> indexBuffer_;

    // A quad facing the camera along +Z. `z` differs between the two cases: EyePosition is
    // `Invert(View).Translation` (EnvironmentMapEffect.cpp), which is the world origin for an
    // identity View -- the "grazing" case needs the quad genuinely coplanar with that origin
    // (z=0) so eyeVector has no Z component, matching the derivation in this file's header
    // comment; the perspective case places its quad at its natural world position (z=0) in front
    // of the eye at (0,0,3) instead.
    void BuildQuad(GraphicsDevice& device, const float z)
    {
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const std::vector<VertexPositionNormalTexture> vertices = {
            VertexPositionNormalTexture(Vector3(-1.0f, -1.0f, z), normal, Vector2(0.0f, 1.0f)),
            VertexPositionNormalTexture(Vector3(1.0f, -1.0f, z), normal, Vector2(1.0f, 1.0f)),
            VertexPositionNormalTexture(Vector3(1.0f, 1.0f, z), normal, Vector2(1.0f, 0.0f)),
            VertexPositionNormalTexture(Vector3(-1.0f, 1.0f, z), normal, Vector2(0.0f, 0.0f)),
        };
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        vertexBuffer_ = std::make_unique<VertexBuffer>(
            device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
            static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
        vertexBuffer_->SetData(vertices.data(), 0, static_cast<int>(vertices.size()));

        indexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits, 6,
                                                      BufferUsage::WriteOnly);
        indexBuffer_->SetData(indices, 0, 6);
    }

    Color RenderWith(GraphicsDevice& device, const Matrix& view, const Matrix& projection,
                     const float z)
    {
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        BuildQuad(device, z);

        EnvironmentMapEffect effect(device);
        effect.setTextureProperty(texture_.get());
        effect.setEnvironmentMapProperty(cube_.get());
        effect.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(view);
        effect.setProjectionProperty(projection);
        // FresnelFactor left at its real XNA default (1.0, enabled) -- the whole point of this
        // test, unlike igl_environmentmapeffect_test.cpp's own explicit setFresnelFactorProperty(0).

        device.SetVertexBuffer(vertexBuffer_.get());
        device.setIndicesProperty(indexBuffer_.get());

        for (EffectPass& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
        {
            pass.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        }

        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        return pixel;
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        cube_ = MakeSolidCube(device, Color(static_cast<bytecs>(128), static_cast<bytecs>(128),
                                            static_cast<bytecs>(128), static_cast<bytecs>(255)));
        texture_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color texel(static_cast<bytecs>(200), static_cast<bytecs>(100),
                          static_cast<bytecs>(50), static_cast<bytecs>(255));
        texture_->SetData(&texel, 1);

        const Color grazing =
            RenderWith(device, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), 0.0f);

        const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                 Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);
        const Color headOn = RenderWith(device, view, projection, 0.0f);

        const Color grazingExpected(static_cast<bytecs>(128), static_cast<bytecs>(128),
                                    static_cast<bytecs>(128), static_cast<bytecs>(255));
        const Color headOnExpected(static_cast<bytecs>(100), static_cast<bytecs>(50),
                                   static_cast<bytecs>(25), static_cast<bytecs>(255));

        ExpectTrue("(a) grazing camera: Fresnel reduces to the flat cube colour",
                  CloseTo(grazing, grazingExpected, 20));
        ExpectTrue("(b) head-on camera: Fresnel suppresses the env map, showing baseColor",
                  CloseTo(headOn, headOnExpected, 20));
    }

public:
    IglEnvironmentMapEffectFresnelTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglEnvironmentMapEffectFresnelTest>();
}
