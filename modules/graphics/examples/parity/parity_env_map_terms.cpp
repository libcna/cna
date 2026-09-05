// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-176 (harness: WEBGPU-207): `EnvironmentMapEffect`'s remaining terms --
// `EnvironmentMapSpecular`, three lights against one, `EyePosition`, a non-identity world transform,
// and the Fresnel gradient across a curved surface.
//
// WHY EVERY CELL USES A CUBE WHOSE SIX FACES ARE THE SAME COLOUR. `parity_compressed_cube` records
// the reason at length: which cube FACE a reflection lands on depends on the pixel-centre
// convention, EasyGL and WebGPU disagree about it (`WEBGPU-187`), and a fixture that samples
// different faces on the two renderers cannot be compared frame to frame. A uniform cube removes
// that variable completely -- every direction returns the same texel, so the reflection direction
// stops mattering and every remaining quantity here is exact arithmetic that both renderers must
// reproduce identically. That is what lets this fixture assert BYTE equality where the compressed
// cube one could only assert an internal A/B.
//
// It also does not weaken the two direction-sensitive rows. `EyePosition` and the world transform
// reach the pixel through the FRESNEL weight as well as through `reflect()`:
//
//     eyeVector   = normalize(EyePosition - mul(Position, World))
//     worldNormal = normalize(mul(Normal, WorldInverseTranspose))
//     fresnel     = pow(max(1 - |dot(eyeVector, worldNormal)|, 0), FresnelFactor) * EnvironmentMapAmount
//     color.rgb   = lerp(litColor, envColor, fresnel)
//
// so moving the eye, or rotating the world, changes the blend weight by exact maths that no
// sampling convention touches. Rows 2 and 3 measure exactly that, and they measure it against a
// twin cell that differs in nothing else.
//
// HOW THE EYE MOVES WITHOUT THE QUAD MOVING. `EyePosition` is not a settable property: XNA derives
// it from the view, `Invert(View).Translation`, so the only way to move the eye is to change a
// matrix that also transforms the geometry. Each cell therefore sets
//
//     View       = CreateTranslation(-eye)
//     World      = identity, or a rotation about Y
//     Projection = Invert(World * View)
//
// which makes `World * View * Projection` exactly the identity for every cell. The quad lands on the
// same pixels whatever the eye and the world transform are, so two cells differ in the shaded
// VALUE alone and never in which pixels they cover -- without that, a "difference" between two
// cells could just be the quad having moved.
//
// The layout, four columns by four rows:
//
//   row 0  the env/lit lerp:   amount 0 | amount 1 | amount 0.5 | EnvironmentMapSpecular
//   row 1  lighting:           one light | three lights | (both again, as the specular baseline)
//   row 2  EyePosition:        eye near | eye far | eye off-axis | (control: eye near repeated)
//   row 3  world + Fresnel:    identity | rotated | flat normals | fanned normals
//
// The amount-0.5 cell is the one with real teeth: it must be the exact MEAN of the amount-0 and
// amount-1 cells, measured from the frame rather than from a constant in this file. A renderer that
// applied the environment map as an ADD, or that clamped the weight, or that swapped the lerp
// endpoints, misses that mean while still producing a plausible-looking picture.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 4;
    constexpr int kRows = 4;
    constexpr int kCell = 32;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;
    constexpr int kCubeSize = 4;

    const Color kClearColor(9, 13, 17, 255);

    /// The environment. One colour on all six faces, so the reflection DIRECTION cannot change the
    /// sampled value and the cube-face convention stops being a variable. Three distinct channels,
    /// none saturated, so a wrong blend weight has nowhere to hide.
    const Color kEnvColor(60, 200, 120, 255);
    /// The same colour at half alpha, for the EnvironmentMapSpecular column: FNA scales the specular
    /// add by `envmap.a`, so an implementation that ignored the cube's alpha lands on twice the
    /// expected addition.
    const Color kEnvColorHalfAlpha(60, 200, 120, 128);

    /// The surface texture. White, so `tex * Diffuse` is the light result alone and the lit half of
    /// the lerp is something this file can compute.
    const Color kSurfaceTexel(255, 255, 255, 255);

    const Vector3 kDiffuseColor(0.8f, 0.4f, 0.2f);
    const Vector3 kSpecularAdd(0.5f, 0.0f, 0.25f);

    struct Vertex { float x, y, z; float nx, ny, nz; float u, v; };
    constexpr int kStride = 32;

    [[nodiscard]] VertexDeclaration EnvMapDeclaration()
    {
        return VertexDeclaration(kStride, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    }
}

/// WEBGPU-176: EnvironmentMapSpecular, light count, EyePosition, world transform, Fresnel gradient.
class EnvMapTermsParityFixture : public CNA::Parity::ParityFixture
{
public:
    EnvMapTermsParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        const auto makeCube = [&device](const Color& texel) {
            TextureCube cube(device, kCubeSize, false, SurfaceFormat::Color);
            const std::array<CubeMapFace, 6> faces{
                CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
                CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ};
            std::array<Color, kCubeSize * kCubeSize> texels;
            texels.fill(texel);
            for (CubeMapFace face : faces)
                cube.SetData(face, texels.data(), static_cast<int>(texels.size()));
            return cube;
        };
        TextureCube opaqueCube = makeCube(kEnvColor);
        TextureCube halfAlphaCube = makeCube(kEnvColorHalfAlpha);

        Texture2D surface(device, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> surfaceTexels{kSurfaceTexel, kSurfaceTexel, kSurfaceTexel,
                                                 kSurfaceTexel};
        surface.SetData(surfaceTexels.data(), static_cast<int>(surfaceTexels.size()));

        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        pointClamp.setAddressUProperty(TextureAddressMode::Clamp);
        pointClamp.setAddressVProperty(TextureAddressMode::Clamp);
        device.getSamplerStatesProperty()[0] = pointClamp;
        device.getSamplerStatesProperty()[1] = pointClamp;

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        struct Cell
        {
            const char* label;
            float amount;
            bool useFresnel;      ///< FresnelFactor > 0 selects FNA's fresnel vertex-shader variant.
            bool halfAlphaCube;
            bool specular;
            int lightCount;       ///< 1 or 3.
            Vector3 eye;
            bool rotateWorld;
            bool fannedNormals;   ///< Splay the four vertex normals, so the Fresnel weight varies.
        };
        const Vector3 kEyeNear(0.0f, 0.0f, 2.0f);
        const Vector3 kEyeFar(0.0f, 0.0f, 8.0f);
        const Vector3 kEyeOffAxis(1.5f, 0.9f, 2.0f);

        const std::array<Cell, kColumns * kRows> cells{{
            // Row 0 -- the env/lit lerp, and the specular add.
            {"amount 0: the lit surface alone", 0.0f, false, false, false, 1, kEyeNear, false, false},
            {"amount 1: the environment alone", 1.0f, false, false, false, 1, kEyeNear, false, false},
            {"amount 0.5: the exact mean of the two", 0.5f, false, false, false, 1, kEyeNear, false,
             false},
            {"EnvironmentMapSpecular scaled by the cube's alpha", 1.0f, false, true, true, 1,
             kEyeNear, false, false},
            // Row 1 -- light count, and the specular column's own baseline.
            {"one directional light", 0.0f, false, false, false, 1, kEyeNear, false, false},
            {"three directional lights", 0.0f, false, false, false, 3, kEyeNear, false, false},
            {"the specular baseline: same cube, specular off", 1.0f, false, true, false, 1, kEyeNear,
             false, false},
            {"three lights under a full environment", 1.0f, false, false, false, 3, kEyeNear, false,
             false},
            // Row 2 -- EyePosition, through the Fresnel weight.
            {"Fresnel, eye near", 1.0f, true, false, false, 1, kEyeNear, false, false},
            {"Fresnel, eye far", 1.0f, true, false, false, 1, kEyeFar, false, false},
            {"Fresnel, eye off axis", 1.0f, true, false, false, 1, kEyeOffAxis, false, false},
            {"Fresnel, eye near again (the repeat control)", 1.0f, true, false, false, 1, kEyeNear,
             false, false},
            // Row 3 -- the world transform, and the Fresnel gradient.
            {"Fresnel, identity world", 1.0f, true, false, false, 1, kEyeOffAxis, false, false},
            {"Fresnel, rotated world", 1.0f, true, false, false, 1, kEyeOffAxis, true, false},
            {"Fresnel, flat normals: no gradient", 1.0f, true, false, false, 1, kEyeOffAxis, false,
             false},
            {"Fresnel, fanned normals: a gradient", 1.0f, true, false, false, 1, kEyeOffAxis, false,
             true},
        }};

        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            const Cell& cell = cells[index];
            const int column = static_cast<int>(index) % kColumns;
            const int row = static_cast<int>(index) / kColumns;

            EnvironmentMapEffect effect(device);
            // See the header: the projection is whatever makes World * View * Projection the
            // identity, so the eye and the world transform change the SHADING and never the pixels.
            const Matrix world = cell.rotateWorld ? Matrix::CreateRotationY(0.7f)
                                                  : Matrix::getIdentityProperty();
            const Matrix view = Matrix::CreateTranslation(-cell.eye);
            effect.setWorldProperty(world);
            effect.setViewProperty(view);
            effect.setProjectionProperty(Matrix::Invert(world * view));
            effect.setTextureProperty(&surface);
            effect.setEnvironmentMapProperty(cell.halfAlphaCube ? &halfAlphaCube : &opaqueCube);
            effect.setEnvironmentMapAmountProperty(cell.amount);
            effect.setFresnelFactorProperty(cell.useFresnel ? 1.0f : 0.0f);
            effect.setEnvironmentMapSpecularProperty(cell.specular ? kSpecularAdd : Vector3::Zero);
            effect.setDiffuseColorProperty(kDiffuseColor);
            effect.setEmissiveColorProperty(Vector3::Zero);
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setAlphaProperty(1.0f);

            // One light straight down the +Z axis at half brightness; the other two are enabled only
            // for the three-light cells and add a quarter each on their own channels, so "three
            // lights" is not merely "brighter" but brighter in a way one light cannot be.
            effect.getDirectionalLight0Property().setEnabledProperty(true);
            effect.getDirectionalLight0Property().setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            effect.getDirectionalLight0Property().setDiffuseColorProperty(
                Vector3(0.5f, 0.5f, 0.5f));
            const bool three = cell.lightCount == 3;
            effect.getDirectionalLight1Property().setEnabledProperty(three);
            effect.getDirectionalLight1Property().setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            effect.getDirectionalLight1Property().setDiffuseColorProperty(
                three ? Vector3(0.25f, 0.0f, 0.0f) : Vector3::Zero);
            effect.getDirectionalLight2Property().setEnabledProperty(three);
            effect.getDirectionalLight2Property().setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            effect.getDirectionalLight2Property().setDiffuseColorProperty(
                three ? Vector3(0.0f, 0.0f, 0.25f) : Vector3::Zero);

            const auto corners = grid.QuadCorners(column, row);
            // Normals: straight at the camera, or splayed towards the four corners so the Fresnel
            // weight -- which depends on dot(eyeVector, normal) -- varies across the quad.
            const float fan = cell.fannedNormals ? 0.9f : 0.0f;
            const auto normalAt = [fan](int corner) {
                static const float kX[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
                static const float kY[4] = { 1.0f, -1.0f, -1.0f, 1.0f};
                return Vector3(kX[corner] * fan, kY[corner] * fan, 1.0f);
            };
            // Triangle STRIP order TL, BL, TR, BR.
            const std::array<int, 4> order{0, 1, 3, 2};
            std::array<Vertex, 4> verts{};
            for (int i = 0; i < 4; ++i)
            {
                const int corner = order[static_cast<std::size_t>(i)];
                const Vector3 n = normalAt(corner);
                verts[static_cast<std::size_t>(i)] = Vertex{
                    corners[static_cast<std::size_t>(corner)].X,
                    corners[static_cast<std::size_t>(corner)].Y, 0.0f,
                    n.X, n.Y, n.Z,
                    (corner == 2 || corner == 3) ? 1.0f : 0.0f,
                    (corner == 1 || corner == 2) ? 1.0f : 0.0f};
            }
            VertexBuffer vb(device, EnvMapDeclaration(), static_cast<int>(verts.size()),
                            BufferUsage::None);
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), kStride);
            device.SetVertexBuffer(&vb);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        const auto at = [&grid](int column, int row) { return grid.Interior(column, row); };
        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            const Color got = Average(at(static_cast<int>(index) % kColumns,
                                         static_cast<int>(index) / kColumns));
            std::printf("[info] %-48s -> (%d,%d,%d)\n", cells[index].label,
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
        }

        // --- Row 0: the lerp is a LERP -------------------------------------------------------
        const Color lit = Average(at(0, 0));
        const Color env = Average(at(1, 0));
        const Color half = Average(at(2, 0));
        ExpectDistinct("amount 0 and amount 1 are different pictures, so the mean below is a real "
                       "claim rather than two copies of one colour",
                       at(0, 0), at(1, 0), 40);
        const Color expectedHalf(
            static_cast<SharpRuntime::bytecs>((lit.getRProperty() + env.getRProperty()) / 2),
            static_cast<SharpRuntime::bytecs>((lit.getGProperty() + env.getGProperty()) / 2),
            static_cast<SharpRuntime::bytecs>((lit.getBProperty() + env.getBProperty()) / 2), 255);
        ExpectAverage("amount 0.5 is the exact mean of amount 0 and amount 1 -- the environment is "
                      "LERPED in, not added, and the weight is not clamped or inverted",
                      at(2, 0), expectedHalf, 3);
        (void)half;

        // --- Row 0/1: EnvironmentMapSpecular adds `EnvironmentMapSpecular * envmap.a` ---------
        // The baseline at (2,1) is the same draw with the specular colour zeroed, so the difference
        // between the two cells is the specular term alone. envmap.a is the cube's 128/255 (Alpha
        // is 1), so the expected addition is kSpecularAdd * 128/255 -> (64, 0, 32).
        const Color specularOff = Average(at(2, 1));
        const Color specularOn = Average(at(3, 0));
        const int addR = specularOn.getRProperty() - specularOff.getRProperty();
        const int addG = specularOn.getGProperty() - specularOff.getGProperty();
        const int addB = specularOn.getBProperty() - specularOff.getBProperty();
        const int expectedAddR = static_cast<int>(kSpecularAdd.X * 128.0f / 255.0f * 255.0f + 0.5f);
        const int expectedAddB = static_cast<int>(kSpecularAdd.Z * 128.0f / 255.0f * 255.0f + 0.5f);
        std::printf("[info] specular add measured (%d,%d,%d), expected (%d,0,%d)\n",
                    addR, addG, addB, expectedAddR, expectedAddB);
        Require(std::abs(addR - expectedAddR) <= 3 && std::abs(addG) <= 3
                    && std::abs(addB - expectedAddB) <= 3,
                "EnvironmentMapSpecular adds exactly `EnvironmentMapSpecular * envmap.a` -- scaled "
                "by the cube's ALPHA, on the two channels it names and on neither other one");

        // At weight 1 the lerp keeps NONE of the lit half, so the light count cannot reach the
        // pixel -- the three-light cell at (3,1) must equal the one-light environment at (1,0).
        ExpectSameRegion("at amount 1 the environment fully replaces the lit colour, so three "
                         "lights change nothing -- the lerp weight is a weight, not a bias",
                         at(1, 0), at(3, 1), 2);
        // And the cube's ALPHA does not tint the lerp: FNA multiplies the sampled alpha into
        // `envmap.a` only, so the half-alpha cube's RGB half must match the opaque cube's.
        ExpectSameRegion("the cube's alpha scales the specular add and nothing else -- the lerped "
                         "environment colour is the same from the half-alpha cube",
                         at(1, 0), at(2, 1), 2);

        // --- Row 1: three lights are not one light -------------------------------------------
        ExpectDistinct("three directional lights differ from one -- the extra two reach the shader",
                       at(0, 1), at(1, 1), 20);
        ExpectBrighter("three lights are brighter than one, since the extra two only add",
                       at(1, 1), at(0, 1), 10);

        // --- Row 2: EyePosition reaches the Fresnel weight ------------------------------------
        ExpectSameRegion("the same EyePosition twice gives the same pixel -- the control that makes "
                         "the two differences below mean something",
                         at(0, 2), at(3, 2), 2);
        ExpectDistinct("moving the eye along the view axis changes the Fresnel weight",
                       at(0, 2), at(1, 2), 6);
        ExpectDistinct("moving the eye off axis changes it again, differently",
                       at(1, 2), at(2, 2), 6);

        // --- Row 3: the world transform, and the gradient -------------------------------------
        ExpectDistinct("a non-identity world transform changes the reflection -- the normal it "
                       "rotates is the one the Fresnel weight is measured against",
                       at(0, 3), at(1, 3), 6);
        // The gradient is measured as a difference between the two HALVES of one cell, so the claim
        // is about variation ACROSS the surface rather than about any absolute value -- and the
        // flat-normal cell is the control that the halves of a uniform cell agree.
        const auto leftHalf = [](const Rectangle& r) {
            return Rectangle(r.X, r.Y, r.Width / 2, r.Height);
        };
        const auto rightHalf = [](const Rectangle& r) {
            return Rectangle(r.X + r.Width - r.Width / 2, r.Y, r.Width / 2, r.Height);
        };
        ExpectFlat("flat normals give a flat Fresnel weight across the quad", at(2, 3), 6);
        ExpectSameRegion("and its two halves agree -- the control for the gradient check below",
                         leftHalf(at(2, 3)), rightHalf(at(2, 3)), 3);
        ExpectDistinct("fanned normals give a Fresnel GRADIENT across the quad -- the weight is "
                       "evaluated per vertex and interpolated, not taken once for the surface",
                       leftHalf(at(3, 3)), rightHalf(at(3, 3)), 6);
    }
};

CNA_PARITY_FIXTURE_MAIN(EnvMapTermsParityFixture)
