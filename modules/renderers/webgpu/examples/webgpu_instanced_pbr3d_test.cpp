// SPDX-License-Identifier: MS-PL
// plans/plan_street_webgpu.md STREETW-0001: an instanced PbrEffect draw is rendered by the PBR
// family, not by the position-only instanced3d one.
//
// Before this task `WebGPURenderer::DrawInstancedPrimitivesEx` built an `InstancedDrawCommand` for
// every stock draw that carried a per-instance stream, whatever effect was applied. instanced3d.wgsl
// declares position (plus an optional COLOR0) and writes `u.diffuseColor` -- no texture, no normal,
// no light -- so every instanced PbrEffect prop rendered as a flat silhouette in the effect's
// diffuse colour while the renderer reported success. cna-street's trees, parked cars, benches and
// street furniture are all instanced PbrEffect draws, and all of them came out white.
//
// The draws below go through the ordinary XNA surface -- `PbrEffect`, `SetVertexBuffers`,
// `DrawInstancedPrimitives` -- because that is the route a game reaches, and the one that was
// broken. Every check is a colour a position-only shader cannot produce:
//
// Check A -- three instances of a small quad, translated to three distinct screen positions, with
//   a RED base-colour texture, AmbientLightColor white and every directional light off. Each
//   instance's own centre reads red. instanced3d.wgsl would have painted all three in the effect's
//   DiffuseColor (white here), so red at the right places proves both the family routing and that
//   the per-instance matrices reached the PBR vertex stage.
// Check B -- a region away from all three quads keeps the clear colour: the draw painted three
//   small quads, not the screen.
// Check C -- DiffuseColor is deliberately WHITE in check A, so "red" cannot come from the flat
//   colour path by accident; this check re-draws the same three instances with the texture
//   swapped for a BLUE one and reads blue, which only a sampling shader can do.
// Check D -- one instance rotated 180 degrees about Y, lit by a single directional light and no
//   ambient. The rotation turns the quad's normal away from the light, so the instance renders
//   black while the same geometry unrotated renders lit. This is the check that fails if the
//   instance transform reaches the POSITION but not the normal matrix -- the shape that would
//   leave every rotated prop in the street lit as though it still faced its authored direction.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    /// The per-instance world matrix, as the four Float32x4 columns every renderer's instanced
    /// path reads. The same declaration `CNA::Graphics::InstancedRendererEXT` builds.
    const VertexDeclaration& InstanceDeclaration()
    {
        static const VertexDeclaration declaration{
            VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 3),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 4)};
        return declaration;
    }

    Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool IsMostly(const Color& c, int r, int g, int b, int tolerance = 40)
    {
        const auto near = [tolerance](int value, int target) {
            return value >= target - tolerance && value <= target + tolerance;
        };
        return near(c.getRProperty(), r) && near(c.getGProperty(), g) && near(c.getBProperty(), b);
    }

    bool IsNearBlack(const Color& c, int tolerance = 24)
    {
        return c.getRProperty() <= tolerance && c.getGProperty() <= tolerance &&
               c.getBProperty() <= tolerance;
    }
}

class WebGpuInstancedPbr3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

    /// A small quad facing the camera at the origin: normal (0,0,-1), tangent (1,0,0,1) as glTF
    /// spells it, and a full UV square so a 1x1 base-colour texture covers it whole.
    static std::vector<VertexPositionNormalTangentTexture> Quad()
    {
        const Vector3 normal(0.0f, 0.0f, -1.0f);
        const Vector4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
        return {
            {Vector3(-0.1f, -0.1f, 0.5f), normal, tangent, Vector2(0.0f, 1.0f)},
            {Vector3( 0.1f, -0.1f, 0.5f), normal, tangent, Vector2(1.0f, 1.0f)},
            {Vector3( 0.1f,  0.1f, 0.5f), normal, tangent, Vector2(1.0f, 0.0f)},
            {Vector3(-0.1f,  0.1f, 0.5f), normal, tangent, Vector2(0.0f, 0.0f)},
        };
    }

    static Texture2D SolidTexture(GraphicsDevice& device, const Color& colour)
    {
        Texture2D texture(device, 1, 1);
        texture.SetData(&colour, 1);
        return texture;
    }

    /// Draws `instances` copies of the quad in one instanced call and leaves the device's
    /// bindings as it found them.
    void DrawInstances(GraphicsDevice& device, PbrEffect& effect, VertexBuffer& geometry,
                       IndexBuffer& indices, VertexBuffer& instanceStream, int instances)
    {
        std::vector<VertexBufferBinding> bindings;
        bindings.emplace_back(&geometry, 0, 0);
        bindings.emplace_back(&instanceStream, 0, 1);
        device.SetVertexBuffers(bindings);
        device.setIndicesProperty(&indices);
        effect.Apply();
        device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, instances);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        GraphicsDevice& device = getGraphicsDeviceProperty();

        const std::vector<VertexPositionNormalTangentTexture> quad = Quad();
        VertexBuffer geometry(device, VertexPositionNormalTangentTexture::getVertexDeclarationStatic(),
                              static_cast<int>(quad.size()), BufferUsage::WriteOnly);
        geometry.SetData(quad.data(), static_cast<int>(quad.size()));

        // Wound clockwise as displayed -- an ordinary XNA front face, so the device's default
        // CullCounterClockwise keeps it.
        const std::uint16_t indexData[6] = {0, 2, 1, 0, 3, 2};
        IndexBuffer indices(device, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
        indices.SetData(indexData, 6);

        const Matrix instanceMatrices[3] = {
            Matrix::CreateTranslation(-0.5f, 0.0f, 0.0f),
            Matrix::CreateTranslation(0.0f, 0.0f, 0.0f),
            Matrix::CreateTranslation(0.5f, 0.0f, 0.0f),
        };
        VertexBuffer instanceStream(device, InstanceDeclaration(), 3, BufferUsage::WriteOnly);
        instanceStream.SetDataRaw(instanceMatrices, 3, static_cast<int>(sizeof(Matrix)));

        Texture2D red = SolidTexture(device, Color(255, 0, 0, 255));
        Texture2D blue = SolidTexture(device, Color(0, 0, 255, 255));

        PbrEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        // White, so a flat-colour shader would paint white and the red below could not be an
        // accident of the DiffuseColor path.
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setLightingEnabledProperty(true);
        effect.getDirectionalLight0Property().setEnabledProperty(false);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);

        // Check A/B -- the base-colour texture and the three instance positions.
        effect.setTextureProperty(&red);
        device.Clear(Color(0, 255, 0, 255));
        DrawInstances(device, effect, geometry, indices, instanceStream, 3);

        // The quad is symmetric, so each instance's NDC centre (tx, 0) maps to pixel
        // ((tx+1)/2)*64: -0.5 -> 16, 0.0 -> 32, +0.5 -> 48.
        const Color left = ReadPixel(device, 16, 32);
        const Color middle = ReadPixel(device, 32, 32);
        const Color right = ReadPixel(device, 48, 32);
        const Color away = ReadPixel(device, 60, 60);

        check(IsMostly(left, 255, 0, 0) && IsMostly(middle, 255, 0, 0) && IsMostly(right, 255, 0, 0),
              "all three instances sample the PBR base-colour texture at their own screen "
              "positions -- the PBR family owns the draw, not instanced3d's flat DiffuseColor");
        check(IsMostly(away, 0, 255, 0),
              "a region away from all three quads keeps the clear colour");

        // Check C -- the same three instances with a different texture.
        effect.setTextureProperty(&blue);
        device.Clear(Color(0, 255, 0, 255));
        DrawInstances(device, effect, geometry, indices, instanceStream, 3);
        const Color blueMiddle = ReadPixel(device, 32, 32);
        check(IsMostly(blueMiddle, 0, 0, 255),
              "swapping the base-colour texture changes the instanced result -- the shader "
              "samples it rather than carrying a constant");

        // Check D -- the per-instance transform reaches the NORMAL, not only the position. One
        // instance is rotated 180 degrees about Y, which turns its face away from the only light.
        effect.setTextureProperty(&red);
        effect.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        DirectionalLight& light = effect.getDirectionalLight0Property();
        light.setEnabledProperty(true);
        // Travelling +Z: it strikes the unrotated quad's -Z normal head-on.
        light.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
        light.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        light.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));

        // The turn is about Y through the origin, which also sends the quad's own z = 0.5 to
        // -0.5 -- outside the depth range, where it would be clipped rather than drawn unlit. The
        // translation puts it back at z = 0.5, so the only thing that differs between the two
        // instances is which way the face points.
        const Matrix facingPair[2] = {
            Matrix::CreateTranslation(-0.5f, 0.0f, 0.0f),
            Matrix::CreateRotationY(MathHelper::Pi) * Matrix::CreateTranslation(0.5f, 0.0f, 1.0f),
        };
        VertexBuffer facingStream(device, InstanceDeclaration(), 2, BufferUsage::WriteOnly);
        facingStream.SetDataRaw(facingPair, 2, static_cast<int>(sizeof(Matrix)));

        // Culling off for this leg only: a 180-degree turn reverses the winding as displayed, so
        // under the default CullCounterClockwise the rotated instance would simply be absent and
        // "not lit" would be satisfied by geometry that was never rasterized.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 255, 0, 255));
        DrawInstances(device, effect, geometry, indices, facingStream, 2);
        const Color lit = ReadPixel(device, 16, 32);
        const Color turnedAway = ReadPixel(device, 48, 32);
        check(!IsNearBlack(lit) && lit.getRProperty() > lit.getBProperty(),
              "the instance facing the light is lit through its base-colour texture");
        check(IsNearBlack(turnedAway),
              "the instance rotated away from the light is unlit -- the per-instance matrix "
              "reaches the normal matrix, not only the position");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuInstancedPbr3DTest()
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
    WebGpuInstancedPbr3DTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
