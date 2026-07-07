// SPDX-License-Identifier: MS-PL
// Task 189: BasicEffect pixel integration tests — EasyGL backend.
//
// Tests 5 rendering combinations via BasicEffect + DrawUserPrimitives + pixel readback:
//   (a) Vertex color only   — VertexPositionColor (stride 16), red vertices → red pixel
//   (b) Texture only        — VertexPositionTexture (stride 20), blue 1×1 tex → blue pixel
//   (c) Diffuse color tint  — VertexPositionTexture (stride 20), white tex × red diffuse → red pixel
//   (d) Color × texture     — VertexPositionColorTexture (stride 24), white tex × green vertex → green pixel
//   (e) Directional lighting — VertexPositionNormalTexture (stride 32), white tex, red light → reddish pixel
//
// Fog is not tested: EasyGL's GpuDrawParams has no fog fields.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kBlack(0, 0, 0, 255);
static const Color kRed  (255,   0,   0, 255);
static const Color kGreen(  0, 255,   0, 255);
static const Color kBlue (  0,   0, 255, 255);
static const Color kWhite(255, 255, 255, 255);

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

// Full-screen NDC quad (two triangles, 6 vertices).
static const Vector3 kTL(-1.0f,  1.0f, 0.0f);
static const Vector3 kBL(-1.0f, -1.0f, 0.0f);
static const Vector3 kBR( 1.0f, -1.0f, 0.0f);
static const Vector3 kTR( 1.0f,  1.0f, 0.0f);

class BasicEffectCombinationsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            result_ = 1;
        }
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding: this file's shared quad winding (kTL/kBL/kBR/kTR, used by all 5
        // sub-cases below) is CCW/back-facing under CNA's real default RasterizerState —
        // needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // ── Shared textures ───────────────────────────────────────────────
        Texture2D blueTex(dev, 1, 1);
        blueTex.SetData(&kBlue, 1);

        Texture2D whiteTex(dev, 1, 1);
        whiteTex.SetData(&kWhite, 1);

        // ── (a) Vertex color only ─────────────────────────────────────────
        // VertexPositionColor (stride 16), VertexColorEnabled=true, red vertices.
        {
            dev.Clear(kBlack);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            const VertexPositionColor q[6] = {
                { kTL, kRed }, { kBL, kRed }, { kBR, kRed },
                { kTL, kRed }, { kBR, kRed }, { kTR, kRed },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

            Color got = readCenter(dev);
            check(colourMatch(got, kRed), "(a) VertexColor-only red", got, kRed);
        }

        // ── (b) Texture only ──────────────────────────────────────────────
        // VertexPositionTexture (stride 20), blue 1×1 texture, default diffuse=(1,1,1).
        {
            dev.Clear(kBlack);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = false;
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&blueTex);
            fx.Apply();

            const VertexPositionTexture q[6] = {
                { kTL, Vector2(0.0f, 1.0f) }, { kBL, Vector2(0.0f, 0.0f) }, { kBR, Vector2(1.0f, 0.0f) },
                { kTL, Vector2(0.0f, 1.0f) }, { kBR, Vector2(1.0f, 0.0f) }, { kTR, Vector2(1.0f, 1.0f) },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

            Color got = readCenter(dev);
            check(colourMatch(got, kBlue), "(b) Texture-only blue", got, kBlue);
        }

        // ── (c) Diffuse color tint ────────────────────────────────────────
        // VertexPositionTexture (stride 20), white texture × red diffuse → red pixel.
        {
            dev.Clear(kBlack);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = false;
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&whiteTex);
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.Apply();

            const VertexPositionTexture q[6] = {
                { kTL, Vector2(0.0f, 1.0f) }, { kBL, Vector2(0.0f, 0.0f) }, { kBR, Vector2(1.0f, 0.0f) },
                { kTL, Vector2(0.0f, 1.0f) }, { kBR, Vector2(1.0f, 0.0f) }, { kTR, Vector2(1.0f, 1.0f) },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

            Color got = readCenter(dev);
            check(colourMatch(got, kRed), "(c) Diffuse-tint red", got, kRed);
        }

        // ── (d) Vertex color × texture ────────────────────────────────────
        // VertexPositionColorTexture (stride 24), white tex × green vertex → green pixel.
        // VertexColorEnabled must be true for the vertex color to apply (Task 367 finding: this
        // case previously left the flag false and still passed, because EasyGL's stride-24 shader
        // multiplied by vertex color unconditionally, ignoring the flag entirely — a real bug
        // fixed in Task 367 alongside a missing DiffuseColor multiply on the same shader path).
        {
            dev.Clear(kBlack);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&whiteTex);
            fx.Apply();

            const VertexPositionColorTexture q[6] = {
                { kTL, kGreen, Vector2(0.0f, 1.0f) }, { kBL, kGreen, Vector2(0.0f, 0.0f) }, { kBR, kGreen, Vector2(1.0f, 0.0f) },
                { kTL, kGreen, Vector2(0.0f, 1.0f) }, { kBR, kGreen, Vector2(1.0f, 0.0f) }, { kTR, kGreen, Vector2(1.0f, 1.0f) },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

            Color got = readCenter(dev);
            check(colourMatch(got, kGreen), "(d) Color×Texture green", got, kGreen);
        }

        // ── (e) Directional lighting ──────────────────────────────────────
        // VertexPositionNormalTexture (stride 32).
        // Light direction (0,0,1); normal (0,0,-1) → NdotL = dot(N, -lightDir) = 1.
        // Diffuse light = red, ambient = black, diffuse material = white.
        // Shader: litRGB = (ambient + light0Diffuse * NdotL) * diffuseMaterial = red.
        // FragColor = texture(white) * vec4(litRGB, 1) = red.
        {
            dev.Clear(kBlack);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = false;
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&whiteTex);
            fx.setLightingEnabledProperty(true);
            fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.DirectionalLight0.setEnabledProperty(true);
            fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
            fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.Apply();

            const Vector3 normal(0.0f, 0.0f, -1.0f);
            const VertexPositionNormalTexture q[6] = {
                { kTL, normal, Vector2(0.0f, 1.0f) }, { kBL, normal, Vector2(0.0f, 0.0f) }, { kBR, normal, Vector2(1.0f, 0.0f) },
                { kTL, normal, Vector2(0.0f, 1.0f) }, { kBR, normal, Vector2(1.0f, 0.0f) }, { kTR, normal, Vector2(1.0f, 1.0f) },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

            Color got = readCenter(dev);
            check(colourMatch(got, kRed), "(e) DirectionalLight red", got, kRed);
        }

        Exit();
    }

public:
    BasicEffectCombinationsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

int main()
{
    BasicEffectCombinationsTest game;
    game.Run();
    return game.getResult();
}
