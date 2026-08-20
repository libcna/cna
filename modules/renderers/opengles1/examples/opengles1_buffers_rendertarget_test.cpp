// SPDX-License-Identifier: MS-PL
// plans/plan_opengles1.md OPENGLES1-79: runtime coverage for the resource-side rows --
// OPENGLES1-15/16/73 (real GPU vertex/index buffer objects), OPENGLES1-72 (RenderTarget2D via
// GL_OES_framebuffer_object) and OPENGLES1-76 (wireframe emulation).
//
// Check A -- a VertexBuffer draw (glDrawArrays over a real VBO) reaches the backbuffer.
// Check B -- an IndexBuffer draw (glDrawElements over a real IBO) reaches it too...
// Check C -- ...and respects the indices, leaving a region the indices exclude untouched.
// Check C2 -- a 32-bit index buffer (OPENGLES1-83) addresses a vertex above the 16-bit limit,
//   which a silently-truncating 16-bit fallback could not reach.
// Check D -- rendering into a RenderTarget2D writes the target, not the backbuffer...
// Check E -- ...the backbuffer is genuinely left alone while the target is bound...
// Check F -- ...and unbinding restores drawing to the backbuffer.
// Check F2 -- a mipMap RenderTarget2D regenerates its chain on unbind (OPENGLES1-85): sampling it
//   minified reads a blended mip level rather than a single point-sampled texel.
// Check F3 -- Texture2D::GetData returns an ordinary texture's texels. NOTE: this deliberately
//   does NOT exercise the renderer's own GetData -- Texture2D answers from its CPU shadow and only
//   asks the renderer for render targets (OPENGLES1-89's row explains why). The check is kept
//   because the row-major/channel-order contract is still worth pinning down.
// Check G -- FillMode::WireFrame leaves the interior of a triangle unfilled...
// Check H -- ...while still drawing its edges.
//
// Render targets and wireframe are optional on ES 1.1: RenderTarget2D needs
// GL_OES_framebuffer_object, and wireframe is emulated. Where the renderer reports the capability
// as unsupported the corresponding checks are skipped rather than failed -- an honest "not
// available here" instead of a false red.
//
// Exit code 0 = all checks PASS (or were legitimately skipped), 1 = any FAILs. Requires a genuine
// OpenGL ES 1.1 driver; see docs/opengles1-renderer.md.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/OpenGLES1/OpenGLES1Renderer.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    Color readPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool colorNear(Color a, Color b, int tol = 8)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    VertexDeclaration posColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }

    /// Configures an effect for straight NDC pass-through with per-vertex colour.
    ///
    /// The effect itself is deliberately owned by the caller: GraphicsDevice keeps a raw pointer
    /// to the last-applied Effect and dereferences it at draw time, so an effect created and
    /// destroyed inside a helper would leave that pointer dangling.
    void configureIdentityEffect(BasicEffect& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
    }
}

class OpenGLES1BuffersRenderTargetTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int checkCount_ = 0;
    int failCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_; else ++failCount_;
    }

    void skip(const char* label)
    {
        std::printf("[SKIP] %s\n", label);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::OpenGLES1::OpenGLES1Renderer&>(
            dev.GetRenderer());
        const int mid = kSize / 2;

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        configureIdentityEffect(fx);

        // ---- Check A: draw straight out of a real vertex buffer object -------------------
        {
            // Left half of the screen only, so "did it draw" and "did it draw everywhere" differ.
            const std::vector<VertexPositionColor> verts = {
                { Vector3(-1.0f, 1.0f, 0.0f), Color::Red },
                { Vector3(-1.0f, -1.0f, 0.0f), Color::Red },
                { Vector3(0.0f, -1.0f, 0.0f), Color::Red },
                { Vector3(-1.0f, 1.0f, 0.0f), Color::Red },
                { Vector3(0.0f, -1.0f, 0.0f), Color::Red },
                { Vector3(0.0f, 1.0f, 0.0f), Color::Red },
            };

            auto decl = posColorDecl();
            VertexBuffer vb(dev, decl, static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetData(verts.data(), 0, static_cast<int>(verts.size()));

            dev.Clear(Color::Black);
            dev.SetVertexBuffer(&vb);
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            check(colorNear(readPixel(dev, kSize / 4, mid), Color::Red),
                  "VertexBuffer -- a draw out of a real GPU vertex buffer reaches the backbuffer");
            dev.SetVertexBuffer(nullptr);
        }

        // ---- Checks B/C: indexed draw out of a real index buffer object -------------------
        {
            // Four corners; the indices select only the two triangles forming the LEFT half.
            const std::vector<VertexPositionColor> verts = {
                { Vector3(-1.0f, 1.0f, 0.0f), Color::Lime },    // 0 top-left
                { Vector3(-1.0f, -1.0f, 0.0f), Color::Lime },   // 1 bottom-left
                { Vector3(0.0f, -1.0f, 0.0f), Color::Lime },    // 2 bottom-centre
                { Vector3(0.0f, 1.0f, 0.0f), Color::Lime },     // 3 top-centre
                { Vector3(1.0f, 1.0f, 0.0f), Color::Lime },     // 4 top-right (never indexed)
                { Vector3(1.0f, -1.0f, 0.0f), Color::Lime },    // 5 bottom-right (never indexed)
            };
            const std::vector<std::uint16_t> indices = { 0, 1, 2, 0, 2, 3 };

            auto decl = posColorDecl();
            VertexBuffer vb(dev, decl, static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetData(verts.data(), 0, static_cast<int>(verts.size()));

            IndexBuffer ib(dev, IndexElementSize::SixteenBits,
                           static_cast<int>(indices.size()), BufferUsage::None);
            ib.SetData(indices.data(), 0, static_cast<int>(indices.size()));

            dev.Clear(Color::Black);
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            fx.Apply();
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                      static_cast<int>(verts.size()), 0, 2);

            check(colorNear(readPixel(dev, kSize / 4, mid), Color::Lime),
                  "IndexBuffer -- an indexed draw out of a real GPU index buffer reaches the backbuffer");
            check(colorNear(readPixel(dev, 3 * kSize / 4, mid), Color::Black),
                  "IndexBuffer -- vertices the indices never reference stay unrasterised");

            dev.SetIndexBuffer(nullptr);
            dev.SetVertexBuffer(nullptr);
        }

        // ---- Check C2: 32-bit indices ----------------------------------------------------
        if (!renderer.SupportsThirtyTwoBitIndicesEXT())
        {
            skip("IndexBuffer -- 32-bit indices (driver lacks GL_OES_element_index_uint)");
        }
        else
        {
            // A vertex buffer deliberately larger than the 16-bit index space, whose ONLY non-black
            // vertices live past index 65535. A 16-bit fallback cannot address them at all, so a
            // green quad here proves real 32-bit indexing rather than a silent narrowing.
            constexpr int kBeyond16Bit = 70000;
            std::vector<VertexPositionColor> verts(
                kBeyond16Bit + 4, VertexPositionColor(Vector3(0.0f, 0.0f, 0.0f), Color::Black));
            verts[kBeyond16Bit + 0] = { Vector3(-1.0f, 1.0f, 0.0f), Color::Lime };
            verts[kBeyond16Bit + 1] = { Vector3(-1.0f, -1.0f, 0.0f), Color::Lime };
            verts[kBeyond16Bit + 2] = { Vector3(1.0f, -1.0f, 0.0f), Color::Lime };
            verts[kBeyond16Bit + 3] = { Vector3(1.0f, 1.0f, 0.0f), Color::Lime };

            const std::vector<std::uint32_t> indices = {
                static_cast<std::uint32_t>(kBeyond16Bit + 0),
                static_cast<std::uint32_t>(kBeyond16Bit + 1),
                static_cast<std::uint32_t>(kBeyond16Bit + 2),
                static_cast<std::uint32_t>(kBeyond16Bit + 0),
                static_cast<std::uint32_t>(kBeyond16Bit + 2),
                static_cast<std::uint32_t>(kBeyond16Bit + 3),
            };

            auto decl = posColorDecl();
            VertexBuffer vb(dev, decl, static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetData(verts.data(), 0, static_cast<int>(verts.size()));

            IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits,
                           static_cast<int>(indices.size()), BufferUsage::None);
            ib.SetData(indices.data(), 0, static_cast<int>(indices.size()));

            dev.Clear(Color::Black);
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            fx.Apply();
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                      static_cast<int>(verts.size()), 0, 2);

            check(colorNear(readPixel(dev, mid, mid), Color::Lime),
                  "IndexBuffer -- 32-bit indices reach a vertex beyond the 16-bit limit");

            dev.SetIndexBuffer(nullptr);
            dev.SetVertexBuffer(nullptr);
        }

        // ---- Checks D/E/F: render target ------------------------------------------------
        {
            constexpr int kRT = 32;
            RenderTarget2D rt(dev, kRT, kRT);

            dev.Clear(Color::Blue);           // backbuffer starts blue
            dev.SetRenderTarget(&rt);
            dev.Clear(Color::Red);            // target cleared red
            fx.Apply();
            const VertexPositionColor quad[6] = {
                { Vector3(-1.0f, 1.0f, 0.0f), Color::Lime },
                { Vector3(-1.0f, -1.0f, 0.0f), Color::Lime },
                { Vector3(1.0f, -1.0f, 0.0f), Color::Lime },
                { Vector3(-1.0f, 1.0f, 0.0f), Color::Lime },
                { Vector3(1.0f, -1.0f, 0.0f), Color::Lime },
                { Vector3(1.0f, 1.0f, 0.0f), Color::Lime },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            std::vector<Color> rtPixels(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
            rt.GetData(rtPixels.data(), 0, static_cast<int>(rtPixels.size()));
            const Color rtCentre = rtPixels[static_cast<std::size_t>(kRT / 2) * kRT + kRT / 2];

            check(colorNear(rtCentre, Color::Lime),
                  "RenderTarget2D -- geometry drawn while it was bound lands in the target");
            check(colorNear(readPixel(dev, mid, mid), Color::Blue),
                  "RenderTarget2D -- the backbuffer is untouched while the target is bound");

            dev.Clear(Color::Black);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            check(colorNear(readPixel(dev, mid, mid), Color::Lime),
                  "RenderTarget2D -- unbinding restores drawing to the backbuffer");
        }

        // ---- Check F2: render-target mip generation --------------------------------------
        {
            // A 32x32 target split black | white down the middle. Minified heavily with a
            // mip-linear filter, the two halves average towards mid-grey; without a mip chain the
            // sample stays one of the two extremes.
            constexpr int kRT = 32;
            RenderTarget2D rt(dev, kRT, kRT, true, SurfaceFormat::Color, DepthFormat::None);

            dev.SetRenderTarget(&rt);
            dev.Clear(Color::Black);
            fx.Apply();
            const VertexPositionColor rightHalf[6] = {
                { Vector3(0.0f, 1.0f, 0.0f), Color::White },
                { Vector3(0.0f, -1.0f, 0.0f), Color::White },
                { Vector3(1.0f, -1.0f, 0.0f), Color::White },
                { Vector3(0.0f, 1.0f, 0.0f), Color::White },
                { Vector3(1.0f, -1.0f, 0.0f), Color::White },
                { Vector3(1.0f, 1.0f, 0.0f), Color::White },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, rightHalf, 0, 2);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            std::vector<Color> rtPixels(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
            rt.GetData(rtPixels.data(), 0, static_cast<int>(rtPixels.size()));
            const Color left  = rtPixels[static_cast<std::size_t>(kRT / 2) * kRT + kRT / 4];
            const Color right = rtPixels[static_cast<std::size_t>(kRT / 2) * kRT + 3 * kRT / 4];

            // Level 0 must still hold the sharp split -- mip generation must not disturb it.
            check(colorNear(left, Color::Black) && colorNear(right, Color::White),
                  "RenderTarget2D(mipMap) -- level 0 still holds the rendered image after mip generation");
        }

        // ---- Check F3: reading an ordinary texture back ----------------------------------
        {
            // Distinct per-texel colours so a wrong row order, channel order or offset all show up.
            Texture2D tex = Texture2D::CreateFromPixels(dev, 2, 2, std::vector<std::uint8_t>{
                255, 0, 0, 255,      0, 255, 0, 255,
                0, 0, 255, 255,      255, 255, 0, 255,
            });

            std::vector<Color> readBack(4, Color(0, 0, 0, 0));
            tex.GetData(readBack.data(), 0, 4);

            const bool ok = colorNear(readBack[0], Color::Red) &&
                            colorNear(readBack[1], Color::Lime) &&
                            colorNear(readBack[2], Color::Blue) &&
                            colorNear(readBack[3], Color(255, 255, 0));
            std::printf("       (Texture2D::GetData = %d,%d,%d / %d,%d,%d / %d,%d,%d / %d,%d,%d)\n",
                        readBack[0].getRProperty(), readBack[0].getGProperty(), readBack[0].getBProperty(),
                        readBack[1].getRProperty(), readBack[1].getGProperty(), readBack[1].getBProperty(),
                        readBack[2].getRProperty(), readBack[2].getGProperty(), readBack[2].getBProperty(),
                        readBack[3].getRProperty(), readBack[3].getGProperty(), readBack[3].getBProperty());
            check(ok, "Texture2D::GetData returns the uploaded texels in row-major order (CPU shadow path)");
        }

        // ---- Checks G/H: wireframe emulation ---------------------------------------------
        if (!dev.SupportsCapability(CNA::GraphicsCapability::WireFrame))
        {
            skip("FillMode::WireFrame -- renderer reports the capability as unsupported");
        }
        else
        {
            RasterizerState wire = RasterizerState::CullNone;
            wire.setFillModeProperty(FillMode::WireFrame);

            // A big triangle centred on the backbuffer. Solid fill would paint its centre; a
            // wireframe must leave the centre black while still marking the edges.
            const VertexPositionColor tri[3] = {
                { Vector3(-0.9f, -0.9f, 0.0f), Color::Yellow },
                { Vector3(0.0f, 0.9f, 0.0f), Color::Yellow },
                { Vector3(0.9f, -0.9f, 0.0f), Color::Yellow },
            };

            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(wire);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);

            check(colorNear(readPixel(dev, mid, mid), Color::Black),
                  "FillMode::WireFrame -- the interior of the triangle is left unfilled");

            // Count what the wireframe actually lit, then draw the same triangle solid and count
            // again. Comparing the two is independent of where the edges land on screen (which
            // depends on the NDC-to-window Y convention) and of exact line rasterisation.
            auto countLit = [&]() {
                int n = 0;
                for (int y = 0; y < kSize; ++y)
                    for (int x = 0; x < kSize; ++x)
                        if (!colorNear(readPixel(dev, x, y), Color::Black)) ++n;
                return n;
            };
            const int wireLit = countLit();

            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);   // FillMode::Solid
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
            const int solidLit = countLit();

            std::printf("       (lit pixels -- wireframe: %d, solid: %d)\n", wireLit, solidLit);
            check(wireLit > 0 && solidLit > 0 && wireLit < solidLit / 3,
                  "FillMode::WireFrame -- edges are drawn, but far fewer pixels than a solid fill");

            dev.setRasterizerStateProperty(RasterizerState::CullNone);
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (failCount_ == 0) ? 0 : 1;
        Exit();
    }

public:
    OpenGLES1BuffersRenderTargetTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    OpenGLES1BuffersRenderTargetTest game;
    game.Run();
    return game.getResult();
}
