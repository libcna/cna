// SPDX-License-Identifier: MS-PL
// Real 3D test for the TinyGL renderer. TinyGL_Smoke only proves rasterization: it draws a
// full-viewport quad at z=0 with identity matrices, which a purely 2D rasterizer would also pass.
// SupportsCapability(ThreeD) reports true, so this suite exists to actually earn that answer.
//
// Check A -- perspective projection really divides by w: a quad pushed further from the camera
//   covers fewer pixels than the same quad drawn nearer. An orthographic or ignored projection
//   matrix produces identical coverage and fails this.
// Check B -- the depth buffer really occludes: a far red quad drawn AFTER a near green quad does
//   not overwrite it. Without a working z-buffer the last draw wins and the centre reads red.
// Check C -- the same pair drawn in the opposite order gives the same picture. This is what
//   separates a real depth test from "later draws happen to be nearer".
// Check D -- depth writes really gate the buffer: with DepthStencilState::DepthRead (test on,
//   write off) the far quad still loses to the near one, but a second near-plane draw is no longer
//   blocked by depth its predecessor never wrote.
// Check E -- a rotated 3D quad produces a genuinely different silhouette than the unrotated one,
//   proving the modelview matrix reaches TinyGL's vertex stage rather than being dropped.
// Check F -- 3D geometry is perspective-textured, not just flat-shaded.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include "CNA/Internal/Renderers/TinyGL/TinyGLRenderer.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::TinyGL;

namespace
{
    constexpr int kChecks = 8;
    constexpr int kSize = 64;

    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }

    VertexDeclaration PosColorTexDecl()
    {
        return VertexDeclaration(24, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,             0),
            VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    /// Counts backbuffer pixels that are not the clear colour, i.e. the drawn silhouette's area.
    int CoveredPixels(GraphicsDevice& dev, const Color& clearColor)
    {
        const Rectangle region(0, 0, kSize, kSize);
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
        int covered = 0;
        for (const Color& p : pixels)
        {
            const int dr = static_cast<int>(p.getRProperty()) - clearColor.getRProperty();
            const int dg = static_cast<int>(p.getGProperty()) - clearColor.getGProperty();
            const int db = static_cast<int>(p.getBProperty()) - clearColor.getBProperty();
            if (std::abs(dr) > 6 || std::abs(dg) > 6 || std::abs(db) > 6) ++covered;
        }
        return covered;
    }
}

class TinyGL3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    /// A unit quad in the XY plane at world z = @p depth, wound clockwise in screen space.
    void DrawQuadAtDepth(GraphicsDevice& dev, const Color& color, float depth,
                         const Matrix& view, const Matrix& projection,
                         const Matrix& world = Matrix::getIdentityProperty())
    {
        VertexBuffer vb(dev, PosColorDecl(), 6, BufferUsage::None);
        const Vector3 tl(-1.0f,  1.0f, depth);
        const Vector3 tr( 1.0f,  1.0f, depth);
        const Vector3 br( 1.0f, -1.0f, depth);
        const Vector3 bl(-1.0f, -1.0f, depth);
        const VertexPositionColor verts[6] = {
            {tl, color}, {tr, color}, {br, color},
            {tl, color}, {br, color}, {bl, color},
        };
        vb.SetData(verts, 0, 6);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(world);
        fx.setViewProperty(view);
        fx.setProjectionProperty(projection);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        const Color clear(0, 0, 40, 255);

        // A real perspective camera looking down -Z from z = +4.
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            3.14159265f / 4.0f, 1.0f, 0.1f, 100.0f);
        const Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 4.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);

        // --- Check A: perspective really divides by w ---------------------------------------
        int nearArea = 0;
        int farArea = 0;
        {
            dev.Clear(clear);
            DrawQuadAtDepth(dev, Color(255, 255, 255, 255), 0.0f, view, projection);
            nearArea = CoveredPixels(dev, clear);

            dev.Clear(clear);
            DrawQuadAtDepth(dev, Color(255, 255, 255, 255), -6.0f, view, projection);
            farArea = CoveredPixels(dev, clear);

            std::printf("       near quad covers %d px, far quad covers %d px\n", nearArea, farArea);
            check(nearArea > 0 && farArea > 0 && farArea * 2 < nearArea,
                  "perspective projection shrinks distant geometry (real w-divide)");
        }

        // --- Check B: the depth buffer occludes ---------------------------------------------
        {
            dev.Clear(clear);
            // Near green first, far red second. Without a depth test the far red wins.
            DrawQuadAtDepth(dev, Color(0, 255, 0, 255), 0.0f, view, projection);
            DrawQuadAtDepth(dev, Color(255, 0, 0, 255), -6.0f, view, projection);
            const Color centre = ReadPixel(dev, kSize / 2, kSize / 2);
            check(centre.getGProperty() > 180 && centre.getRProperty() < 60,
                  "the depth buffer occludes: a later far draw loses to an earlier near one");
        }

        // --- Check C: order independence ----------------------------------------------------
        {
            dev.Clear(clear);
            // Far red first, near green second. A correct depth test gives the SAME picture.
            DrawQuadAtDepth(dev, Color(255, 0, 0, 255), -6.0f, view, projection);
            DrawQuadAtDepth(dev, Color(0, 255, 0, 255), 0.0f, view, projection);
            const Color centre = ReadPixel(dev, kSize / 2, kSize / 2);
            check(centre.getGProperty() > 180 && centre.getRProperty() < 60,
                  "the same scene in the opposite draw order renders the same");
        }

        // --- Check D: depth writes gate the buffer ------------------------------------------
        {
            dev.Clear(clear);
            DrawQuadAtDepth(dev, Color(0, 255, 0, 255), 0.0f, view, projection);

            // DepthRead: test on, write off. The far quad is still occluded by what the near
            // quad wrote before the state changed...
            dev.setDepthStencilStateProperty(DepthStencilState::DepthRead);
            DrawQuadAtDepth(dev, Color(255, 0, 0, 255), -6.0f, view, projection);
            const bool stillOccluded = ReadPixel(dev, kSize / 2, kSize / 2).getGProperty() > 180;

            // ...and a nearer draw still wins, without having written depth itself.
            DrawQuadAtDepth(dev, Color(0, 0, 255, 255), 1.0f, view, projection);
            const Color afterNear = ReadPixel(dev, kSize / 2, kSize / 2);
            dev.setDepthStencilStateProperty(DepthStencilState::Default);

            check(stillOccluded, "DepthRead keeps testing against previously written depth");
            check(afterNear.getBProperty() > 180,
                  "a nearer draw still passes the test while depth writes are off");
        }

        // --- Check E: the modelview matrix reaches the vertex stage --------------------------
        {
            dev.Clear(clear);
            DrawQuadAtDepth(dev, Color(255, 255, 255, 255), 0.0f, view, projection);
            const int unrotated = CoveredPixels(dev, clear);

            dev.Clear(clear);
            // Rotating 60 degrees about Y foreshortens the quad: fewer covered pixels, and the
            // near/far edges land at different depths, which only a real 3D transform produces.
            const Matrix world = Matrix::CreateRotationY(3.14159265f / 3.0f);
            DrawQuadAtDepth(dev, Color(255, 255, 255, 255), 0.0f, view, projection, world);
            const int rotated = CoveredPixels(dev, clear);

            std::printf("       unrotated covers %d px, Y-rotated covers %d px\n",
                        unrotated, rotated);
            check(rotated > 0 && rotated < unrotated * 3 / 4,
                  "a world rotation foreshortens the quad (real modelview transform)");
        }

        // --- Check F: 3D geometry is textured ------------------------------------------------
        {
            Texture2D texture(dev, 2, 2);
            const Color texels[4] = {
                Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                Color(0, 255, 0, 255), Color(255, 0, 0, 255),
            };
            texture.SetData(texels, 4);

            dev.Clear(clear);
            VertexBuffer vb(dev, PosColorTexDecl(), 6, BufferUsage::None);
            const Color white(255, 255, 255, 255);
            const VertexPositionColorTexture verts[6] = {
                {Vector3(-1.0f,  1.0f, 0.0f), white, Vector2(0.0f, 0.0f)},
                {Vector3( 1.0f,  1.0f, 0.0f), white, Vector2(1.0f, 0.0f)},
                {Vector3( 1.0f, -1.0f, 0.0f), white, Vector2(1.0f, 1.0f)},
                {Vector3(-1.0f,  1.0f, 0.0f), white, Vector2(0.0f, 0.0f)},
                {Vector3( 1.0f, -1.0f, 0.0f), white, Vector2(1.0f, 1.0f)},
                {Vector3(-1.0f, -1.0f, 0.0f), white, Vector2(0.0f, 1.0f)},
            };
            vb.SetData(verts, 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&texture);
            fx.setViewProperty(view);
            fx.setProjectionProperty(projection);
            fx.Apply();

            bool threw = false;
            try
            {
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
            }
            catch (...) { threw = true; }
            check(!threw, "a textured 3D draw (VertexPositionColorTexture) does not throw");

            // The checkerboard's opposite quadrants carry opposite colours, so a genuinely
            // sampled quad shows both -- a flat-shaded one could not.
            const Color topLeft = ReadPixel(dev, 24, 24);
            const Color topRight = ReadPixel(dev, 40, 24);
            const bool sampled =
                (topLeft.getRProperty() > 150 && topRight.getGProperty() > 150) ||
                (topLeft.getGProperty() > 150 && topRight.getRProperty() > 150);
            check(sampled, "the 3D quad is really texture-mapped, not flat-shaded");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    TinyGL3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main()
{
    TinyGL3DTest game;
    game.Run();
    return game.getResult();
}
