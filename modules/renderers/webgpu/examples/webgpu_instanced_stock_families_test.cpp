// SPDX-License-Identifier: MS-PL
// plans/plan_street_webgpu.md STREETW-0004: an instanced stock draw is rendered by its OWN
// family, not by the position-only instanced3d one -- for every stock effect, not just PbrEffect.
//
// `STREETW-0001` fixed PbrEffect because cna-street needed it. The same defect covered every other
// stock family: `WebGPURenderer::DrawInstancedPrimitivesEx` built an `InstancedDrawCommand` for any
// stock draw carrying a per-instance stream, whatever effect was applied, and instanced3d.wgsl
// writes `u.diffuseColor` with no texture, no normal and no light. A textured BasicEffect, an
// AlphaTestEffect, a DualTextureEffect and an EnvironmentMapEffect all rendered as flat silhouettes
// while the renderer reported success.
//
// Each leg below draws THREE instances of a small quad through the ordinary XNA surface --
// `SetVertexBuffers`, `Effect.Apply`, `DrawInstancedPrimitives` -- with a red texture and a white
// DiffuseColor, and reads the three instance centres plus one pixel away from them. Red at three
// distinct screen positions is a result the position-only program cannot produce: it proves the
// family routing (a texture was sampled) and the per-instance transform (three positions) in the
// same reading, and the white DiffuseColor rules out the flat-colour path as the source of it.
//
// The quad is symmetric, so each instance's NDC centre (tx, 0) maps to pixel ((tx+1)/2)*64:
// -0.5 -> 16, 0.0 -> 32, +0.5 -> 48.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const std::string& label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount;
    }

    /// The per-instance world matrix, as the four Float32x4 columns every renderer's instanced
    /// path reads -- the declaration `CNA::Graphics::InstancedRendererEXT` builds.
    const VertexDeclaration& InstanceDeclaration()
    {
        static const VertexDeclaration declaration{
            VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 3),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 4)};
        return declaration;
    }

    /// A second texture coordinate beside the first, for DualTextureEffect.
    const VertexDeclaration& DualTextureDeclaration()
    {
        static const VertexDeclaration declaration{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1)};
        return declaration;
    }

    struct DualTextureVertex
    {
        Vector3 position;
        Vector2 uv0;
        Vector2 uv1;
    };

    /// QueueSkinnedDraw's stride-52 record (VertexPositionNormalTextureSkinned).
    struct SkinnedVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedVertex) == 52, "skinned vertex must be 52 bytes");

    /// The stride-52 record's own declaration, in the order that layout expects.
    const VertexDeclaration& SkinnedDeclaration()
    {
        static const VertexDeclaration declaration{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0)};
        return declaration;
    }

    /// QueueSkinnedPbrDraw's stride-68 record (VertexPositionNormalTangentTextureSkinned).
    struct SkinnedPbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrVertex) == 68, "skinned PBR vertex must be 68 bytes");

    /// The stride-68 record's own declaration.
    const VertexDeclaration& SkinnedPbrDeclaration()
    {
        static const VertexDeclaration declaration{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
            VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(64, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0)};
        return declaration;
    }

    Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool IsMostly(const Color& c, int r, int g, int b, int tolerance = 48)
    {
        const auto near = [tolerance](int value, int target) {
            return value >= target - tolerance && value <= target + tolerance;
        };
        return near(c.getRProperty(), r) && near(c.getGProperty(), g) && near(c.getBProperty(), b);
    }

    Texture2D SolidTexture(GraphicsDevice& device, const Color& colour)
    {
        Texture2D texture(device, 1, 1);
        texture.SetData(&colour, 1);
        return texture;
    }

    /// Wound clockwise as displayed -- an ordinary XNA front face, so the device's default
    /// CullCounterClockwise keeps it.
    const std::uint16_t kIndices[6] = {0, 2, 1, 0, 3, 2};

    const Matrix kInstances[3] = {
        Matrix::CreateTranslation(-0.5f, 0.0f, 0.0f),
        Matrix::CreateTranslation(0.0f, 0.0f, 0.0f),
        Matrix::CreateTranslation(0.5f, 0.0f, 0.0f),
    };
}

class WebGpuInstancedStockFamiliesTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

    /// Draws three instances of @p geometry in one call and reads the four probes.
    void DrawAndCheck(GraphicsDevice& device, Effect& effect, VertexBuffer& geometry,
                      IndexBuffer& indices, VertexBuffer& instanceStream, const char* family)
    {
        std::vector<VertexBufferBinding> bindings;
        bindings.emplace_back(&geometry, 0, 0);
        bindings.emplace_back(&instanceStream, 0, 1);
        device.Clear(Color(0, 255, 0, 255));
        device.SetVertexBuffers(bindings);
        device.setIndicesProperty(&indices);
        effect.Apply();
        device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);

        const Color left = ReadPixel(device, 16, 32);
        const Color middle = ReadPixel(device, 32, 32);
        const Color right = ReadPixel(device, 48, 32);
        const Color away = ReadPixel(device, 60, 60);
        check(IsMostly(left, 255, 0, 0) && IsMostly(middle, 255, 0, 0) && IsMostly(right, 255, 0, 0),
              std::string(family) + ": all three instances sample the family's own texture at "
              "their own screen positions -- not instanced3d's flat DiffuseColor");
        check(IsMostly(away, 0, 255, 0),
              std::string(family) + ": a region away from all three quads keeps the clear colour");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        GraphicsDevice& device = getGraphicsDeviceProperty();
        const Matrix identity = Matrix::getIdentityProperty();

        IndexBuffer indices(device, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
        indices.SetData(kIndices, 6);
        VertexBuffer instanceStream(device, InstanceDeclaration(), 3, BufferUsage::WriteOnly);
        instanceStream.SetDataRaw(kInstances, 3, static_cast<int>(sizeof(Matrix)));
        Texture2D red = SolidTexture(device, Color(255, 0, 0, 255));

        // ---- Textured: BasicEffect with a texture and no lighting. -------------------------
        {
            const VertexPositionTexture quad[4] = {
                {Vector3(-0.1f, -0.1f, 0.5f), Vector2(0.0f, 1.0f)},
                {Vector3( 0.1f, -0.1f, 0.5f), Vector2(1.0f, 1.0f)},
                {Vector3( 0.1f,  0.1f, 0.5f), Vector2(1.0f, 0.0f)},
                {Vector3(-0.1f,  0.1f, 0.5f), Vector2(0.0f, 0.0f)},
            };
            VertexBuffer geometry(device, VertexPositionTexture::getVertexDeclarationStatic(), 4,
                                  BufferUsage::WriteOnly);
            geometry.SetData(quad, 4);

            BasicEffect effect(device);
            effect.setWorldProperty(identity);
            effect.setViewProperty(identity);
            effect.setProjectionProperty(identity);
            effect.setLightingEnabledProperty(false);
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(&red);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            DrawAndCheck(device, effect, geometry, indices, instanceStream, "Textured3D");
        }

        // ---- LitTextured: the same buffer with normals, and lighting on. -------------------
        {
            const Vector3 normal(0.0f, 0.0f, -1.0f);  // faces the camera at the origin
            const VertexPositionNormalTexture quad[4] = {
                {Vector3(-0.1f, -0.1f, 0.5f), normal, Vector2(0.0f, 1.0f)},
                {Vector3( 0.1f, -0.1f, 0.5f), normal, Vector2(1.0f, 1.0f)},
                {Vector3( 0.1f,  0.1f, 0.5f), normal, Vector2(1.0f, 0.0f)},
                {Vector3(-0.1f,  0.1f, 0.5f), normal, Vector2(0.0f, 0.0f)},
            };
            VertexBuffer geometry(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                                  4, BufferUsage::WriteOnly);
            geometry.SetData(quad, 4);

            BasicEffect effect(device);
            effect.setWorldProperty(identity);
            effect.setViewProperty(identity);
            effect.setProjectionProperty(identity);
            effect.setLightingEnabledProperty(true);
            // Ambient white and every directional light off, so the reading is the texture itself
            // rather than a lighting result that would need its own tolerance.
            effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.getDirectionalLight0Property().setEnabledProperty(false);
            effect.getDirectionalLight1Property().setEnabledProperty(false);
            effect.getDirectionalLight2Property().setEnabledProperty(false);
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(&red);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            DrawAndCheck(device, effect, geometry, indices, instanceStream, "LitTextured3D");
        }

        // ---- AlphaTest: an opaque texel passes the test and is written unchanged. ----------
        {
            const VertexPositionTexture quad[4] = {
                {Vector3(-0.1f, -0.1f, 0.5f), Vector2(0.0f, 1.0f)},
                {Vector3( 0.1f, -0.1f, 0.5f), Vector2(1.0f, 1.0f)},
                {Vector3( 0.1f,  0.1f, 0.5f), Vector2(1.0f, 0.0f)},
                {Vector3(-0.1f,  0.1f, 0.5f), Vector2(0.0f, 0.0f)},
            };
            VertexBuffer geometry(device, VertexPositionTexture::getVertexDeclarationStatic(), 4,
                                  BufferUsage::WriteOnly);
            geometry.SetData(quad, 4);

            AlphaTestEffect effect(device);
            effect.setWorldProperty(identity);
            effect.setViewProperty(identity);
            effect.setProjectionProperty(identity);
            effect.setTextureProperty(&red);
            effect.setReferenceAlphaProperty(128);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            DrawAndCheck(device, effect, geometry, indices, instanceStream, "AlphaTest3D");
        }

        // ---- DualTexture: red over white, so the modulated result is still red. ------------
        {
            const DualTextureVertex quad[4] = {
                {Vector3(-0.1f, -0.1f, 0.5f), Vector2(0.0f, 1.0f), Vector2(0.0f, 1.0f)},
                {Vector3( 0.1f, -0.1f, 0.5f), Vector2(1.0f, 1.0f), Vector2(1.0f, 1.0f)},
                {Vector3( 0.1f,  0.1f, 0.5f), Vector2(1.0f, 0.0f), Vector2(1.0f, 0.0f)},
                {Vector3(-0.1f,  0.1f, 0.5f), Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f)},
            };
            VertexBuffer geometry(device, DualTextureDeclaration(), 4, BufferUsage::WriteOnly);
            geometry.SetDataRaw(quad, 4, static_cast<int>(sizeof(DualTextureVertex)));
            Texture2D white = SolidTexture(device, Color(255, 255, 255, 255));

            DualTextureEffect effect(device);
            effect.setWorldProperty(identity);
            effect.setViewProperty(identity);
            effect.setProjectionProperty(identity);
            effect.setTextureProperty(&red);
            effect.setTexture2Property(&white);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            DrawAndCheck(device, effect, geometry, indices, instanceStream, "DualTexture3D");
        }

        // ---- EnvMap: EnvironmentMapAmount 0, so the reading is the base texture alone. -----
        {
            const Vector3 normal(0.0f, 0.0f, -1.0f);
            const VertexPositionNormalTexture quad[4] = {
                {Vector3(-0.1f, -0.1f, 0.5f), normal, Vector2(0.0f, 1.0f)},
                {Vector3( 0.1f, -0.1f, 0.5f), normal, Vector2(1.0f, 1.0f)},
                {Vector3( 0.1f,  0.1f, 0.5f), normal, Vector2(1.0f, 0.0f)},
                {Vector3(-0.1f,  0.1f, 0.5f), normal, Vector2(0.0f, 0.0f)},
            };
            VertexBuffer geometry(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                                  4, BufferUsage::WriteOnly);
            geometry.SetData(quad, 4);

            TextureCube environment(device, 1, false, SurfaceFormat::Color);
            const Color blue(0, 0, 255, 255);
            for (int face = 0; face < 6; ++face)
                environment.SetData(static_cast<CubeMapFace>(face), &blue, 1);

            EnvironmentMapEffect effect(device);
            effect.setWorldProperty(identity);
            effect.setViewProperty(identity);
            effect.setProjectionProperty(identity);
            effect.setTextureProperty(&red);
            effect.setEnvironmentMapProperty(&environment);
            // Nothing of the cube in the result, so the leg reads the base texture and the
            // instance positions rather than a reflection whose value would need its own oracle.
            effect.setEnvironmentMapAmountProperty(0.0f);
            effect.setFresnelFactorProperty(0.0f);
            effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.getDirectionalLight0Property().setEnabledProperty(false);
            effect.getDirectionalLight1Property().setEnabledProperty(false);
            effect.getDirectionalLight2Property().setEnabledProperty(false);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            DrawAndCheck(device, effect, geometry, indices, instanceStream, "EnvMap3D");
        }

        // ---- Skinned: one bone palette, three instances. -----------------------------------
        // plans/plan_street_webgpu.md STREETW-0005. The bone is the identity, so what the leg
        // measures is the family and the per-instance matrix rather than the skin itself -- and a
        // single palette serving every instance is exactly what an instanced skinned draw means.
        {
            const SkinnedVertex quad[4] = {
                {-0.1f, -0.1f, 0.5f,  0, 0, -1,  0.0f, 1.0f,  1, 0, 0, 0,  0, 0, 0, 0},
                { 0.1f, -0.1f, 0.5f,  0, 0, -1,  1.0f, 1.0f,  1, 0, 0, 0,  0, 0, 0, 0},
                { 0.1f,  0.1f, 0.5f,  0, 0, -1,  1.0f, 0.0f,  1, 0, 0, 0,  0, 0, 0, 0},
                {-0.1f,  0.1f, 0.5f,  0, 0, -1,  0.0f, 0.0f,  1, 0, 0, 0,  0, 0, 0, 0},
            };
            VertexBuffer geometry(device, SkinnedDeclaration(), 4, BufferUsage::WriteOnly);
            geometry.SetDataRaw(quad, 4, static_cast<int>(sizeof(SkinnedVertex)));

            SkinnedEffect effect(device);
            effect.setWorldProperty(identity);
            effect.setViewProperty(identity);
            effect.setProjectionProperty(identity);
            effect.setTextureProperty(&red);
            const std::vector<Matrix> bones = {identity};
            effect.SetBoneTransforms(bones);
            effect.setWeightsPerVertexProperty(1);
            effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.DirectionalLight0.setEnabledProperty(false);
            effect.DirectionalLight1.setEnabledProperty(false);
            effect.DirectionalLight2.setEnabledProperty(false);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            DrawAndCheck(device, effect, geometry, indices, instanceStream, "Skinned3D");
        }

        // ---- SkinnedPbr: the other half of the skinned gap. --------------------------------
        {
            const SkinnedPbrVertex quad[4] = {
                {-0.1f, -0.1f, 0.5f,  0, 0, -1,  1, 0, 0, 1,  0.0f, 1.0f,  1, 0, 0, 0,  0, 0, 0, 0},
                { 0.1f, -0.1f, 0.5f,  0, 0, -1,  1, 0, 0, 1,  1.0f, 1.0f,  1, 0, 0, 0,  0, 0, 0, 0},
                { 0.1f,  0.1f, 0.5f,  0, 0, -1,  1, 0, 0, 1,  1.0f, 0.0f,  1, 0, 0, 0,  0, 0, 0, 0},
                {-0.1f,  0.1f, 0.5f,  0, 0, -1,  1, 0, 0, 1,  0.0f, 0.0f,  1, 0, 0, 0,  0, 0, 0, 0},
            };
            VertexBuffer geometry(device, SkinnedPbrDeclaration(), 4, BufferUsage::WriteOnly);
            geometry.SetDataRaw(quad, 4, static_cast<int>(sizeof(SkinnedPbrVertex)));

            SkinnedPbrEffect effect(device);
            effect.setWorldProperty(identity);
            effect.setViewProperty(identity);
            effect.setProjectionProperty(identity);
            effect.setTextureProperty(&red);
            const std::vector<Matrix> bones = {identity};
            effect.SetBoneTransforms(bones);
            effect.setWeightsPerVertexProperty(1);
            effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.DirectionalLight0.setEnabledProperty(false);
            effect.DirectionalLight1.setEnabledProperty(false);
            effect.DirectionalLight2.setEnabledProperty(false);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            DrawAndCheck(device, effect, geometry, indices, instanceStream, "SkinnedPbr3D");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuInstancedStockFamiliesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // plans/plan_gpu_test_isolation.md GTI-0003: GetBackBufferData is HiDef-only.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    WebGpuInstancedStockFamiliesTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
