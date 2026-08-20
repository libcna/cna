// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-13: real proof for the OpenGL4 renderer's DrawPrimitivesEx/
// DrawIndexedPrimitivesEx stride dispatch -- textured3d (VertexPositionTexture, stride 20),
// colored_textured3d (VertexPositionColorTexture, stride 24), and lit_textured3d
// (VertexPositionNormalTexture, stride 32, BasicEffect.EnableDefaultLighting()) -- verified via
// real pixel readback, not just "didn't throw" (unlike opengl4_3d_test.cpp's own colored3d-only
// scope note, this renderer now genuinely samples textures and applies lighting).
//
// Check A -- textured3d: a solid-orange texture sampled through BasicEffect.TextureEnabled is
//   visible at its exact color (proves real sampling, not a texture-ignored/diffuse-only path).
// Check B -- colored_textured3d: the same texture combined with VertexColorEnabled's per-vertex
//   tint multiplies correctly (proves the vertex-color*texture*diffuse chain, not just one term).
// Check C -- lit_textured3d: BasicEffect.EnableDefaultLighting() produces a visibly different
//   (non-degenerate) pixel than the same geometry with lighting left off -- proves the lighting
//   branch is real dead code has no effect otherwise, not a compile-only no-op.
// Check D -- DrawIndexedPrimitivesEx: an indexed textured3d quad (4 vertices/6 indices) samples
//   correctly too, not just the non-indexed DrawPrimitivesEx path.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

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
    // Solid orange, 2x2 so it's a real (non-degenerate) texture, not a 1x1 edge case.
    const Color kOrange(200, 100, 50, 255);

    std::vector<std::uint8_t> SolidTexturePixels(const Color& c)
    {
        std::vector<std::uint8_t> pixels(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            pixels[i * 4 + 0] = static_cast<std::uint8_t>(c.getRProperty());
            pixels[i * 4 + 1] = static_cast<std::uint8_t>(c.getGProperty());
            pixels[i * 4 + 2] = static_cast<std::uint8_t>(c.getBProperty());
            pixels[i * 4 + 3] = static_cast<std::uint8_t>(c.getAProperty());
        }
        return pixels;
    }
}

class OpenGL4Textured3DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // plans/plan_opengl4.md GL4-16: RasterizerState.CullMode now really applies, and
        // GraphicsDevice's own default RasterizerState (CullCounterClockwiseFace, XNA's real
        // default) would otherwise cull this file's own quad winding -- irrelevant to what this
        // test actually checks (texturing/lighting, not winding), so opt out explicitly. Same
        // idiom already established by e.g. bgfx_basiceffect_texture_enabled_test.cpp for the
        // identical reason.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 10.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 20.0f);

        Texture2D orangeTex = Texture2D::CreateFromPixels(dev, 2, 2, SolidTexturePixels(kOrange));

        // Check A: textured3d -- the sampled texture color must come through exactly (no tint).
        {
            dev.Clear(Color::Black);

            const VertexPositionTexture verts[6] = {
                {Vector3(-0.6f, 0.6f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3(-0.6f, -0.6f, 0.0f), Vector2(0.0f, 1.0f)},
                {Vector3(0.6f, -0.6f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(-0.6f, 0.6f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3(0.6f, -0.6f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(0.6f, 0.6f, 0.0f), Vector2(1.0f, 0.0f)},
            };
            VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 0, 6);

            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&orangeTex);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(view);
            fx.setProjectionProperty(projection);
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            ExpectPixel("textured3d: solid-orange texture samples exactly",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), kOrange);
        }

        // Check B: colored_textured3d -- vertex-color tint multiplies the sampled texture.
        {
            dev.Clear(Color::Black);

            const Color tint(128, 128, 128, 255); // 50% grey
            const VertexPositionColorTexture verts[6] = {
                {Vector3(-0.6f, 0.6f, 0.0f), tint, Vector2(0.0f, 0.0f)},
                {Vector3(-0.6f, -0.6f, 0.0f), tint, Vector2(0.0f, 1.0f)},
                {Vector3(0.6f, -0.6f, 0.0f), tint, Vector2(1.0f, 1.0f)},
                {Vector3(-0.6f, 0.6f, 0.0f), tint, Vector2(0.0f, 0.0f)},
                {Vector3(0.6f, -0.6f, 0.0f), tint, Vector2(1.0f, 1.0f)},
                {Vector3(0.6f, 0.6f, 0.0f), tint, Vector2(1.0f, 0.0f)},
            };
            VertexBuffer vb(dev, VertexPositionColorTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&orangeTex);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(view);
            fx.setProjectionProperty(projection);
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            const Color expected(
                static_cast<int>(kOrange.getRProperty()) * 128 / 255,
                static_cast<int>(kOrange.getGProperty()) * 128 / 255,
                static_cast<int>(kOrange.getBProperty()) * 128 / 255,
                255);
            ExpectPixel("colored_textured3d: vertex-color tint multiplies the sampled texture",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), expected, /*tolerance=*/6);
        }

        // Check C: lit_textured3d -- lit vs unlit must differ (proves the lighting branch is
        // real, not dead code).
        Color litPixel(0, 0, 0, 0);
        Color unlitPixel(0, 0, 0, 0);
        {
            const VertexPositionNormalTexture verts[6] = {
                {Vector3(-0.6f, 0.6f, 0.0f), Vector3(0, 0, 1), Vector2(0.0f, 0.0f)},
                {Vector3(-0.6f, -0.6f, 0.0f), Vector3(0, 0, 1), Vector2(0.0f, 1.0f)},
                {Vector3(0.6f, -0.6f, 0.0f), Vector3(0, 0, 1), Vector2(1.0f, 1.0f)},
                {Vector3(-0.6f, 0.6f, 0.0f), Vector3(0, 0, 1), Vector2(0.0f, 0.0f)},
                {Vector3(0.6f, -0.6f, 0.0f), Vector3(0, 0, 1), Vector2(1.0f, 1.0f)},
                {Vector3(0.6f, 0.6f, 0.0f), Vector3(0, 0, 1), Vector2(1.0f, 0.0f)},
            };
            VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 0, 6);

            dev.Clear(Color::Black);
            BasicEffect litFx(dev);
            litFx.setTextureEnabledProperty(true);
            litFx.setTextureProperty(&orangeTex);
            litFx.EnableDefaultLighting();
            litFx.setWorldProperty(Matrix::getIdentityProperty());
            litFx.setViewProperty(view);
            litFx.setProjectionProperty(projection);
            litFx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            const Rectangle centerPixel(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&centerPixel, &litPixel, 0, 1);

            dev.Clear(Color::Black);
            BasicEffect unlitFx(dev);
            unlitFx.setTextureEnabledProperty(true);
            unlitFx.setTextureProperty(&orangeTex);
            unlitFx.setLightingEnabledProperty(false);
            unlitFx.setWorldProperty(Matrix::getIdentityProperty());
            unlitFx.setViewProperty(view);
            unlitFx.setProjectionProperty(projection);
            unlitFx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.GetBackBufferData(&centerPixel, &unlitPixel, 0, 1);
        }
        {
            std::printf("    (unlit=(%d,%d,%d) lit=(%d,%d,%d))\n",
                        unlitPixel.getRProperty(), unlitPixel.getGProperty(), unlitPixel.getBProperty(),
                        litPixel.getRProperty(), litPixel.getGProperty(), litPixel.getBProperty());
            Check(unlitPixel.getRProperty() == kOrange.getRProperty() &&
                  unlitPixel.getGProperty() == kOrange.getGProperty() &&
                  unlitPixel.getBProperty() == kOrange.getBProperty(),
                  "lit_textured3d: unlit render matches the plain texture exactly");
            Check(litPixel.getRProperty() != unlitPixel.getRProperty() ||
                  litPixel.getGProperty() != unlitPixel.getGProperty() ||
                  litPixel.getBProperty() != unlitPixel.getBProperty(),
                  "lit_textured3d: EnableDefaultLighting() produces a different pixel than unlit");
        }

        // Check D: DrawIndexedPrimitivesEx -- indexed textured3d quad (4 vertices/6 indices).
        {
            dev.Clear(Color::Black);

            const VertexPositionTexture verts[4] = {
                {Vector3(-0.6f, 0.6f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3(-0.6f, -0.6f, 0.0f), Vector2(0.0f, 1.0f)},
                {Vector3(0.6f, -0.6f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(0.6f, 0.6f, 0.0f), Vector2(1.0f, 0.0f)},
            };
            VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 4, BufferUsage::None);
            vb.SetData(verts, 0, 4);

            const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            ib.SetData(indices, 0, 6);

            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&orangeTex);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(view);
            fx.setProjectionProperty(projection);
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.setIndicesProperty(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.setIndicesProperty(nullptr);
            dev.SetVertexBuffer(nullptr);

            ExpectPixel("DrawIndexedPrimitivesEx: indexed textured3d quad samples correctly",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), kOrange);
        }
    }

public:
    OpenGL4Textured3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4Textured3DTest>();
}
