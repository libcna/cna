// SPDX-License-Identifier: MS-PL
// plans/plan_opengles1.md OPENGLES1-79: runtime coverage for the fixed-function shading rows --
// OPENGLES1-22 (directional lighting), OPENGLES1-23 (linear fog) and OPENGLES1-24 (alpha test).
// These are the parts of the renderer with no shader to fall back on: everything here is real
// glLight*/glFog*/glAlphaFunc state, asserted with GetBackBufferData() pixel readback.
//
// Check A -- LightingEnabled with a light aimed straight at the surface lights it up...
// Check B -- ...and turning that same light away from the surface leaves it markedly darker.
// Check C -- DiffuseColor tints an unlit surface (the fixed-function "current colour" path).
// Check D -- fog fully applied at the far plane reproduces the fog colour...
// Check E -- ...while geometry at the near plane is left unfogged.
// Check F0 -- FogStart == FogEnd is XNA's documented degenerate case: everything 100% fogged.
//   Fixed-function GL would divide by zero here, so the renderer has to emulate it.
// Check F -- AlphaTestEffect rejects fragments below the reference alpha...
// Check G -- ...keeps fragments above it,
// Check H -- ...mirrors correctly for CompareFunction::Less,
// Check I -- ...and honours CompareFunction::Never instead of degrading into "no test".
//
// The lighting checks compare relative brightness rather than exact values: ES 1.1 per-vertex
// lighting is interpolated across the primitive, so absolute pixel values depend on the driver's
// interpolation, but "lit is brighter than unlit" is unambiguous.
//
// Exit code 0 = all checks PASS, 1 = any FAILs. Requires a genuine OpenGL ES 1.1 driver; see
// docs/opengles1-renderer.md.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

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

    int luma(Color c)
    {
        return c.getRProperty() + c.getGProperty() + c.getBProperty();
    }

    /// A screen-filling quad whose normals all point straight at the viewer (+Z), so a light
    /// aimed along -Z hits it head-on and a light aimed along +Z misses it entirely.
    void drawLitQuad(GraphicsDevice& dev, BasicEffect& fx)
    {
        fx.Apply();

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

    void setIdentityTransforms(BasicEffect& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }
}

class OpenGLES1LightingFogAlphaTest : public Game
{
    static constexpr int kChecks = 10;

    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const int mid = kSize / 2;

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        // ---- Checks A/B: directional lighting -------------------------------------------
        int litLuma = 0;
        int unlitLuma = 0;
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            setIdentityTransforms(fx);
            fx.setLightingEnabledProperty(true);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.DirectionalLight0.setEnabledProperty(true);
            fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            // Pointing along -Z, i.e. straight into a +Z-facing surface.
            fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            drawLitQuad(dev, fx);
            litLuma = luma(readPixel(dev, mid, mid));
        }
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            setIdentityTransforms(fx);
            fx.setLightingEnabledProperty(true);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.DirectionalLight0.setEnabledProperty(true);
            fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            // Pointing along +Z -- away from the same surface.
            fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
            drawLitQuad(dev, fx);
            unlitLuma = luma(readPixel(dev, mid, mid));
        }
        std::printf("       (lit luma=%d, back-facing-light luma=%d)\n", litLuma, unlitLuma);
        check(litLuma > 150,
              "BasicEffect lighting -- a light aimed at the surface lights it up");
        check(litLuma > unlitLuma + 60,
              "BasicEffect lighting -- turning the light away leaves the surface markedly darker");

        // ---- Check C: DiffuseColor on an unlit surface -----------------------------------
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            setIdentityTransforms(fx);
            fx.setLightingEnabledProperty(false);
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            drawLitQuad(dev, fx);
            check(colorNear(readPixel(dev, mid, mid), Color::Red, 24),
                  "BasicEffect DiffuseColor tints an unlit surface");
        }

        // ---- Checks D/E: linear fog -----------------------------------------------------
        // Fog is an eye-space distance ramp; with an identity view the quad's own Z places it in
        // or out of the ramp. FogStart/FogEnd are chosen so the far quad is fully fogged.
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            setIdentityTransforms(fx);
            fx.setLightingEnabledProperty(false);
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setFogEnabledProperty(true);
            fx.setFogColorProperty(Vector3(0.0f, 0.0f, 1.0f));
            fx.setFogStartProperty(0.0f);
            fx.setFogEndProperty(1.0f);
            fx.Apply();

            // Depth 0.95 in NDC -- deep inside the fog ramp, so the fog colour should dominate.
            const Vector3 n(0.0f, 0.0f, 1.0f);
            const float z = 0.95f;
            const VertexPositionNormalTexture quad[6] = {
                { Vector3(-1.0f, 1.0f, z), n, Vector2(0.0f, 0.0f) },
                { Vector3(-1.0f, -1.0f, z), n, Vector2(0.0f, 1.0f) },
                { Vector3(1.0f, -1.0f, z), n, Vector2(1.0f, 1.0f) },
                { Vector3(-1.0f, 1.0f, z), n, Vector2(0.0f, 0.0f) },
                { Vector3(1.0f, -1.0f, z), n, Vector2(1.0f, 1.0f) },
                { Vector3(1.0f, 1.0f, z), n, Vector2(1.0f, 0.0f) },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

            const Color fogged = readPixel(dev, mid, mid);
            std::printf("       (fogged pixel = %d,%d,%d)\n", fogged.getRProperty(),
                        fogged.getGProperty(), fogged.getBProperty());
            check(fogged.getBProperty() > fogged.getRProperty(),
                  "BasicEffect fog -- distant geometry takes on the fog colour");
        }
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            setIdentityTransforms(fx);
            fx.setLightingEnabledProperty(false);
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setFogEnabledProperty(true);
            fx.setFogColorProperty(Vector3(0.0f, 0.0f, 1.0f));
            fx.setFogStartProperty(10.0f);   // ramp starts well beyond the geometry
            fx.setFogEndProperty(20.0f);
            drawLitQuad(dev, fx);            // sits at z = 0

            const Color unfogged = readPixel(dev, mid, mid);
            check(unfogged.getRProperty() > unfogged.getBProperty(),
                  "BasicEffect fog -- geometry in front of the fog ramp is left unfogged");
        }

        // ---- Check F0: the FogStart == FogEnd degenerate case ---------------------------
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            setIdentityTransforms(fx);
            fx.setLightingEnabledProperty(false);
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setFogEnabledProperty(true);
            fx.setFogColorProperty(Vector3(0.0f, 0.0f, 1.0f));
            // XNA: "force everything to 100% fogged if start and end are the same".
            fx.setFogStartProperty(5.0f);
            fx.setFogEndProperty(5.0f);
            drawLitQuad(dev, fx);

            const Color got = readPixel(dev, mid, mid);
            std::printf("       (FogStart == FogEnd pixel = %d,%d,%d)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            check(got.getBProperty() > 200 && got.getRProperty() < 60,
                  "BasicEffect fog -- FogStart == FogEnd fogs everything completely");
        }

        // ---- Checks F/G: alpha test ------------------------------------------------------
        // AlphaTestEffect keeps fragments whose alpha compares Greater than the reference.
        {
            auto drawAlphaQuad = [&](int alpha, CompareFunction fn = CompareFunction::Greater) {
                AlphaTestEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(&whiteTex_);
                fx.setVertexColorEnabledProperty(true);
                fx.setAlphaFunctionProperty(fn);
                fx.setReferenceAlphaProperty(128);
                fx.Apply();

                const Color col(0, 255, 0, static_cast<std::uint8_t>(alpha));
                const VertexPositionColorTexture quad[6] = {
                    { Vector3(-1.0f, 1.0f, 0.0f), col, Vector2(0.0f, 0.0f) },
                    { Vector3(-1.0f, -1.0f, 0.0f), col, Vector2(0.0f, 1.0f) },
                    { Vector3(1.0f, -1.0f, 0.0f), col, Vector2(1.0f, 1.0f) },
                    { Vector3(-1.0f, 1.0f, 0.0f), col, Vector2(0.0f, 0.0f) },
                    { Vector3(1.0f, -1.0f, 0.0f), col, Vector2(1.0f, 1.0f) },
                    { Vector3(1.0f, 1.0f, 0.0f), col, Vector2(1.0f, 0.0f) },
                };
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            };

            dev.Clear(Color::Black);
            drawAlphaQuad(32);    // below the reference -- must be discarded
            check(colorNear(readPixel(dev, mid, mid), Color::Black),
                  "AlphaTestEffect -- fragments below the reference alpha are discarded");

            dev.Clear(Color::Black);
            drawAlphaQuad(255);   // above the reference -- must survive
            check(!colorNear(readPixel(dev, mid, mid), Color::Black),
                  "AlphaTestEffect -- fragments above the reference alpha are kept");

            // CompareFunction::Less is the mirror image, and catches an inverted mapping that
            // Greater alone would not: both comparisons must not resolve to the same GL func.
            dev.Clear(Color::Black);
            drawAlphaQuad(32, CompareFunction::Less);
            const bool lessKeptLowAlpha = !colorNear(readPixel(dev, mid, mid), Color::Black);
            dev.Clear(Color::Black);
            drawAlphaQuad(255, CompareFunction::Less);
            const bool lessKeptHighAlpha = !colorNear(readPixel(dev, mid, mid), Color::Black);
            check(lessKeptLowAlpha && !lessKeptHighAlpha,
                  "AlphaTestEffect -- CompareFunction::Less keeps the low alpha and drops the high one");

            // CompareFunction::Never must discard unconditionally -- not degrade into "no test".
            dev.Clear(Color::Black);
            drawAlphaQuad(255, CompareFunction::Never);
            check(colorNear(readPixel(dev, mid, mid), Color::Black),
                  "AlphaTestEffect -- CompareFunction::Never discards even a fully opaque fragment");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    OpenGLES1LightingFogAlphaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    OpenGLES1LightingFogAlphaTest game;
    game.Run();
    return game.getResult();
}
