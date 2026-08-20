// SPDX-License-Identifier: MS-PL
// plans/plan_opengles1.md OPENGLES1-79: runtime coverage for the remaining feature rows --
// OPENGLES1-71 (DualTextureEffect via two GL_COMBINE stages), OPENGLES1-74 (EnvironmentMapEffect
// via GL_OES_texture_cube_map reflection-map texgen) and OPENGLES1-11 (context loss/restore).
//
// Check A -- DualTextureEffect reproduces its (tex0 * 2) * tex1 formula rather than either
//   texture on its own: a mid-grey base doubled against a white overlay lands near white, which
//   neither a plain modulate nor a single-texture fallback would produce...
// Check B -- ...and darkening the second texture darkens the result, proving the second stage is
//   genuinely sampled instead of ignored.
// Check C -- DualTextureEffect samples the SECOND texture-coordinate set for the second unit,
//   not a copy of the first (OPENGLES1-81). Checks A/B use one shared UV set and therefore cannot
//   tell a correct two-set implementation from one that ignores the second set entirely.
// Check D -- EnvironmentMapEffect renders using its cube map.
// Check C2 -- RenderTargetCube renders into an individual face and reads it back (OPENGLES1-84),
//   with the other faces left untouched so a target that ignores the face index is detectable.
// Check D -- a simulated context loss followed by a restore leaves the device drawing correctly.
// Check E -- ...and a texture uploaded before the loss still samples correctly afterwards, i.e.
//   the renderer really re-uploaded it rather than leaving a dead GL name bound...
// Check F -- ...as does an indexed draw out of vertex/index buffers filled before the loss
//   (OPENGLES1-80), which would otherwise render nothing from dead buffer names.
//
// Check F2 -- SetContextRecoveryEnabled(false) genuinely gives recovery up (OPENGLES1-91): a
//   texture uploaded afterwards no longer survives a context loss, proving the CPU copy was
//   really released rather than merely promised to be.
//
// Both multitexturing paths are optional on ES 1.1 (dual texture needs GL_MAX_TEXTURE_UNITS >= 2,
// environment mapping needs GL_OES_texture_cube_map). When the driver lacks them the renderer
// falls back to the plain colored path by design, so those checks are skipped rather than failed.
//
// Exit code 0 = all checks PASS (or were legitimately skipped), 1 = any FAILs. Requires a genuine
// OpenGL ES 1.1 driver; see docs/opengles1-renderer.md.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
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

    Texture2D makeSolid(GraphicsDevice& dev, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        return Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{r, g, b, 255});
    }

    /// Screen-filling quad carrying both a texture coordinate and a +Z normal, which is what the
    /// dual-texture (stride 24) and environment-map (stride 32) paths respectively expect.
    void drawTexturedQuad(GraphicsDevice& dev)
    {
        const Color white = Color::White;
        const VertexPositionColorTexture quad[6] = {
            { Vector3(-1.0f, 1.0f, 0.0f), white, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), white, Vector2(0.0f, 1.0f) },
            { Vector3(1.0f, -1.0f, 0.0f), white, Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f, 1.0f, 0.0f), white, Vector2(0.0f, 0.0f) },
            { Vector3(1.0f, -1.0f, 0.0f), white, Vector2(1.0f, 1.0f) },
            { Vector3(1.0f, 1.0f, 0.0f), white, Vector2(1.0f, 0.0f) },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }

    /// Position(12) + TexCoord0(8) + TexCoord1(8) -- the layout DualTextureEffect actually uses.
    struct VertexPositionDualTexture
    {
        Vector3 Position;
        Vector2 TextureCoordinate0;
        Vector2 TextureCoordinate1;
    };
    static_assert(sizeof(VertexPositionDualTexture) == 28, "dual-UV vertex must stay 28 bytes");

    VertexDeclaration dualTextureDeclaration()
    {
        return VertexDeclaration(28, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        });
    }

    void drawNormalQuad(GraphicsDevice& dev)
    {
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f, 1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f, 1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3(1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
            { Vector3(1.0f, 1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }
}

class OpenGLES1MultitextureContextLossTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D greyTex_;
    Texture2D whiteTex_;
    Texture2D darkTex_;
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

    void skip(const char* label) { std::printf("[SKIP] %s\n", label); }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = new SpriteBatch(dev);
        greyTex_  = makeSolid(dev, 128, 128, 128);
        whiteTex_ = makeSolid(dev, 255, 255, 255);
        darkTex_  = makeSolid(dev, 64, 64, 64);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::OpenGLES1::OpenGLES1Renderer&>(dev.GetRenderer());
        const int mid = kSize / 2;

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        // ---- Checks A/B: DualTextureEffect ----------------------------------------------
        if (!renderer.SupportsSecondTextureUnit())
        {
            skip("DualTextureEffect -- driver exposes fewer than two texture units");
            skip("DualTextureEffect -- second stage sampling (same reason)");
        }
        else
        {
            // (grey * 2) * white = ~255. A single-texture fallback would leave ~128, and a plain
            // (non-doubling) modulate would too, so this value distinguishes all three.
            dev.Clear(Color::Black);
            DualTextureEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&greyTex_);
            fx.setTexture2Property(&whiteTex_);
            fx.Apply();
            drawTexturedQuad(dev);

            const Color both = readPixel(dev, mid, mid);
            std::printf("       (dual texture: grey x2 over white = %d,%d,%d)\n",
                        both.getRProperty(), both.getGProperty(), both.getBProperty());
            check(both.getRProperty() > 200,
                  "DualTextureEffect -- the doubled base times a white overlay approaches white");

            dev.Clear(Color::Black);
            DualTextureEffect fx2(dev);
            fx2.setWorldProperty(Matrix::getIdentityProperty());
            fx2.setViewProperty(Matrix::getIdentityProperty());
            fx2.setProjectionProperty(Matrix::getIdentityProperty());
            fx2.setTextureProperty(&greyTex_);
            fx2.setTexture2Property(&darkTex_);
            fx2.Apply();
            drawTexturedQuad(dev);

            const Color darker = readPixel(dev, mid, mid);
            std::printf("       (dual texture: grey x2 over dark  = %d,%d,%d)\n",
                        darker.getRProperty(), darker.getGProperty(), darker.getBProperty());
            check(darker.getRProperty() + 40 < both.getRProperty(),
                  "DualTextureEffect -- darkening the second texture darkens the result");
        }

        // ---- Check C: the second UV set actually reaches the second texture unit ---------
        if (!renderer.SupportsSecondTextureUnit())
        {
            skip("DualTextureEffect -- second UV set (fewer than two texture units)");
        }
        else
        {
            // Unit 0 samples a 1x1 white texture, so UV0 cannot affect the result. Unit 1 samples a
            // 2x1 red|blue texture through UV1 only. Feeding UV1 = 0.75 (right texel, blue) while
            // UV0 stays 0 means a renderer that wrongly points unit 1 at UV0 samples the LEFT texel
            // and comes out red -- the two outcomes are unambiguous.
            Texture2D splitTex = Texture2D::CreateFromPixels(dev, 2, 1, std::vector<std::uint8_t>{
                255, 0, 0, 255,
                0, 0, 255, 255,
            });

            auto decl = dualTextureDeclaration();
            const VertexPositionDualTexture quad[6] = {
                { Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f), Vector2(0.75f, 0.5f) },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f), Vector2(0.75f, 0.5f) },
                { Vector3(1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f), Vector2(0.75f, 0.5f) },
                { Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f), Vector2(0.75f, 0.5f) },
                { Vector3(1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f), Vector2(0.75f, 0.5f) },
                { Vector3(1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f), Vector2(0.75f, 0.5f) },
            };

            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            vb.SetDataRaw(quad, 6, 28);   // no typed SetData overload for a dual-UV vertex

            dev.Clear(Color::Black);
            DualTextureEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex_);
            fx.setTexture2Property(&splitTex);
            dev.SetVertexBuffer(&vb);
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            const Color got = readPixel(dev, mid, mid);
            std::printf("       (dual texture, UV1=0.75 selects the blue texel = %d,%d,%d)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            check(got.getBProperty() > got.getRProperty(),
                  "DualTextureEffect -- unit 1 samples its own UV set, not a copy of UV0");
        }

        // ---- Check D: EnvironmentMapEffect -----------------------------------------------
        {
            std::unique_ptr<TextureCube> cube;
            try
            {
                cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
            }
            catch (...)
            {
                cube.reset();
            }

            if (!cube || !renderer.SupportsTextureCubeMap())
            {
                skip("EnvironmentMapEffect -- driver lacks GL_OES_texture_cube_map");
            }
            else
            {
                const Color face(0, 0, 255, 255);
                for (int f = 0; f < 6; ++f)
                    cube->SetData(static_cast<CubeMapFace>(f), &face, 1);

                dev.Clear(Color::Black);
                EnvironmentMapEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(&whiteTex_);
                fx.setEnvironmentMapProperty(cube.get());
                fx.setEnvironmentMapAmountProperty(1.0f);
                fx.Apply();
                drawNormalQuad(dev);

                const Color got = readPixel(dev, mid, mid);
                std::printf("       (environment map pixel = %d,%d,%d)\n",
                            got.getRProperty(), got.getGProperty(), got.getBProperty());
                check(!colorNear(got, Color::Black),
                      "EnvironmentMapEffect -- the cube-mapped surface is rendered");
            }
        }

        // ---- Check C2: RenderTargetCube --------------------------------------------------
        {
            constexpr int kFaceSize = 16;
            auto cubeTarget = renderer.CreateRenderTargetCube(kFaceSize, /*depthFormat=*/0);
            if (!cubeTarget)
            {
                skip("RenderTargetCube -- driver lacks framebuffer objects or cube maps");
            }
            else
            {
                // Render red into face 3 only. Every other face must stay at its initial contents,
                // which is what catches a target that ignores the face index.
                constexpr int kTargetFace = 3;
                cubeTarget->BindAsRenderTargetFace(kTargetFace);
                dev.Clear(Color::Red);
                cubeTarget->UnbindAsRenderTarget();

                std::vector<std::uint8_t> facePixels(
                    static_cast<std::size_t>(kFaceSize) * kFaceSize * 4, 0);
                cubeTarget->GetData(kTargetFace, 0, 0, 0, kFaceSize, kFaceSize,
                                    facePixels.data(), static_cast<int>(facePixels.size()));
                const Color drawn(facePixels[0], facePixels[1], facePixels[2], facePixels[3]);

                std::vector<std::uint8_t> otherPixels(
                    static_cast<std::size_t>(kFaceSize) * kFaceSize * 4, 0);
                cubeTarget->GetData(/*face=*/0, 0, 0, 0, kFaceSize, kFaceSize,
                                    otherPixels.data(), static_cast<int>(otherPixels.size()));
                const Color untouched(otherPixels[0], otherPixels[1], otherPixels[2], otherPixels[3]);

                std::printf("       (cube face %d = %d,%d,%d ; face 0 = %d,%d,%d)\n",
                            kTargetFace, drawn.getRProperty(), drawn.getGProperty(), drawn.getBProperty(),
                            untouched.getRProperty(), untouched.getGProperty(), untouched.getBProperty());
                check(colorNear(drawn, Color::Red) && !colorNear(untouched, Color::Red),
                      "RenderTargetCube -- the clear lands in the selected face and no other");
            }
        }

        // ---- Checks D/E: context loss and restore ----------------------------------------
        {
            // Draw the texture once BEFORE the loss so its GL name is live, then lose the context.
            dev.Clear(Color::Black);
            spriteBatch_->Begin();
            spriteBatch_->Draw(greyTex_, Rectangle(0, 0, kSize, kSize), Color::White);
            spriteBatch_->End();

            renderer.DebugSimulateContextLoss();
            renderer.DebugRestoreContext();

            dev.Clear(Color::Red);
            check(colorNear(readPixel(dev, mid, mid), Color::Red),
                  "context loss -- the device clears correctly again after a restore");

            dev.Clear(Color::Black);
            spriteBatch_->Begin();
            spriteBatch_->Draw(greyTex_, Rectangle(0, 0, kSize, kSize), Color::White);
            spriteBatch_->End();

            const Color afterLoss = readPixel(dev, mid, mid);
            std::printf("       (pre-loss texture after restore = %d,%d,%d)\n",
                        afterLoss.getRProperty(), afterLoss.getGProperty(), afterLoss.getBProperty());
            check(colorNear(afterLoss, Color(128, 128, 128), 24),
                  "context loss -- a texture uploaded before the loss still samples after restore");
        }

        // ---- Check F: buffers survive the loss too (OPENGLES1-80) -------------------------
        {
            VertexDeclaration decl(16, {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
            const std::vector<VertexPositionColor> verts = {
                { Vector3(-1.0f, 1.0f, 0.0f), Color::Lime },
                { Vector3(-1.0f, -1.0f, 0.0f), Color::Lime },
                { Vector3(1.0f, -1.0f, 0.0f), Color::Lime },
                { Vector3(1.0f, 1.0f, 0.0f), Color::Lime },
            };
            const std::vector<std::uint16_t> indices = { 0, 1, 2, 0, 2, 3 };

            // Fill both buffers, THEN lose the context -- nothing is re-uploaded afterwards, so
            // the draw can only work if the renderer rebuilt them itself.
            VertexBuffer vb(dev, decl, static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetData(verts.data(), 0, static_cast<int>(verts.size()));
            IndexBuffer ib(dev, IndexElementSize::SixteenBits,
                           static_cast<int>(indices.size()), BufferUsage::None);
            ib.SetData(indices.data(), 0, static_cast<int>(indices.size()));

            renderer.DebugSimulateContextLoss();
            renderer.DebugRestoreContext();

            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            dev.setBlendStateProperty(BlendState::Opaque);

            dev.Clear(Color::Black);
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                      static_cast<int>(verts.size()), 0, 2);

            const Color got = readPixel(dev, mid, mid);
            std::printf("       (pre-loss buffers after restore = %d,%d,%d)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            check(colorNear(got, Color::Lime),
                  "context loss -- vertex/index buffers filled before the loss still draw after restore");

            dev.SetIndexBuffer(nullptr);
            dev.SetVertexBuffer(nullptr);
        }

        // ---- Check F2: context recovery can be switched off ------------------------------
        {
            // With recovery disabled the renderer must decline to keep a CPU copy, so the same
            // loss/restore that check E proves survivable must now NOT restore the texture.
            dev.SetContextRecoveryEnabled(false);

            Texture2D volatileTex = makeSolid(dev, 128, 128, 128);
            dev.Clear(Color::Black);
            spriteBatch_->Begin();
            spriteBatch_->Draw(volatileTex, Rectangle(0, 0, kSize, kSize), Color::White);
            spriteBatch_->End();
            const Color beforeLoss = readPixel(dev, mid, mid);

            renderer.DebugSimulateContextLoss();
            renderer.DebugRestoreContext();

            dev.Clear(Color::Black);
            spriteBatch_->Begin();
            spriteBatch_->Draw(volatileTex, Rectangle(0, 0, kSize, kSize), Color::White);
            spriteBatch_->End();
            const Color afterLoss = readPixel(dev, mid, mid);

            std::printf("       (recovery disabled: before loss = %d,%d,%d, after = %d,%d,%d)\n",
                        beforeLoss.getRProperty(), beforeLoss.getGProperty(), beforeLoss.getBProperty(),
                        afterLoss.getRProperty(), afterLoss.getGProperty(), afterLoss.getBProperty());
            check(colorNear(beforeLoss, Color(128, 128, 128), 24) &&
                      !colorNear(afterLoss, Color(128, 128, 128), 24),
                  "SetContextRecoveryEnabled(false) really releases the copy, so restore no longer works");

            dev.SetContextRecoveryEnabled(true);
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (failCount_ == 0) ? 0 : 1;
        Exit();
    }

public:
    OpenGLES1MultitextureContextLossTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    OpenGLES1MultitextureContextLossTest game;
    game.Run();
    return game.getResult();
}
