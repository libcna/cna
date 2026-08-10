// SPDX-License-Identifier: MS-PL
// Task 193: SkinnedEffect bone count pixel integration test — EasyGL.
//
// Tests three bone scenarios using a full-screen red quad driven by SkinnedEffect.
// Vertices start at NDC x ∈ [-1, 0]; bones move the quad into different positions.
// Background is cleared to green; drawn pixels are red (ambient=(1,1,1), diffuse=(1,0,0)).
//
// Shader: gl_Position = uWVP * skinMat * aPos
//   skinMat = sum_i(uBones[indices[i]] * weights[i])
//
// Sub-tests:
//   (a) 1 bone, identity: quad starts at [-0.5, 0.5], bone=identity → stays at [-0.5, 0.5]
//       centre=red, left(-0.75)=green, right(+0.75)=green
//   (b) 1 bone, translate(+0.5): quad starts at [-1, 0], bone shifts to [-0.5, 0.5]
//       centre=red, left(-0.75)=green
//   (c) 2 bones 50/50 blend: bone0=translate(+1), bone1=identity, weights=(0.5,0.5,0,0)
//       net shift = 0.5 → quad [-1,0] → [-0.5, 0.5]
//       centre=red, left(-0.75)=green, right(+0.75)=green
//       (right=green proves bone0 (translate+1) was not applied at full weight)
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// GPU-compact skinned vertex: matches the EasyGL stride-52 layout.
// pos(12) + normal(12) + uv(8) + weights(16) + indices(4) = 52 bytes
struct SkinnedGpuVertex
{
    float   px, py, pz;      // position
    float   nx, ny, nz;      // normal (facing +z for correct NdotL)
    float   u, v;            // texture coord
    float   w0, w1, w2, w3; // bone weights
    uint8_t i0, i1, i2, i3; // bone indices
};
static_assert(sizeof(SkinnedGpuVertex) == 52, "SkinnedGpuVertex must be 52 bytes");

class SkinnedBonesTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D   whiteTex_;
    bool        done_   = false;
    int         result_ = 0;

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

    Color readPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle reg(x, y, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // Build a 6-vertex (2-triangle) quad from NDC (x0,y0) to (x1,y1).
    // All vertices use the same bone weights and indices.
    static std::array<SkinnedGpuVertex, 6> makeQuad(
        float x0, float y0, float x1, float y1,
        float w0, float w1, float w2, float w3,
        uint8_t i0, uint8_t i1, uint8_t i2, uint8_t i3)
    {
        auto v = [&](float x, float y) -> SkinnedGpuVertex {
            return { x, y, 0.0f,   0.0f, 0.0f, 1.0f,   0.5f, 0.5f,
                     w0, w1, w2, w3,   i0, i1, i2, i3 };
        };
        return {{
            v(x0, y1), v(x0, y0), v(x1, y0),
            v(x0, y1), v(x1, y0), v(x1, y1),
        }};
    }

    void drawAndCheck(GraphicsDevice& dev, const SkinnedGpuVertex* verts,
                      SkinnedEffect& fx, int W, int H)
    {
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        dev.SetVertexBuffer(&vb);
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        const Color kWhite(255, 255, 255, 255);
        whiteTex_ = Texture2D(dev, 1, 1);
        whiteTex_.SetData(&kWhite, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        // Pixel positions corresponding to NDC x ≈ -0.75, 0.00, +0.75
        const int xLeft   = W / 8;          // NDC ≈ -0.75
        const int xCenter = W / 2;          // NDC ≈  0.00
        const int xRight  = 7 * W / 8;     // NDC ≈ +0.75
        const int yCenter = H / 2;

        const Color kRed  (255, 0, 0, 255);
        const Color kGreen(0, 255, 0, 255);

        // Shared SkinnedEffect setup: ambient=(1,1,1), diffuse=(1,0,0) → red output.
        // litRGB = (emissive + ambient*diffuse + 0) * diffuse = (1,0,0)*(1,0,0) = red.
        auto setupFx = [&](SkinnedEffect& fx) {
            fx.setTextureProperty(&whiteTex_);
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
        };

        // ── (a) 1 bone, identity ──────────────────────────────────────────
        // Quad at NDC x:[-0.5, 0.5], bone[0]=identity → no movement.
        // Expected: centre=red, left=green, right=green.
        {
            dev.Clear(kGreen);

            SkinnedEffect fx(dev);
            setupFx(fx);
            fx.SetBoneTransforms({ Matrix::getIdentityProperty() });
            fx.setWeightsPerVertexProperty(1);
            fx.Apply();

            auto quad = makeQuad(-0.5f, -0.5f, 0.5f, 0.5f,
                                 1.0f, 0.0f, 0.0f, 0.0f,
                                 0, 0, 0, 0);
            drawAndCheck(dev, quad.data(), fx, W, H);

            Color left   = readPixel(dev, xLeft,   yCenter);
            Color center = readPixel(dev, xCenter, yCenter);
            Color right  = readPixel(dev, xRight,  yCenter);

            check(center.getRProperty() > center.getGProperty(), "(a) identity bone: centre=red",  center, kRed);
            check(left.getGProperty()   > left.getRProperty(),   "(a) identity bone: left=green",  left,   kGreen);
            check(right.getGProperty()  > right.getRProperty(),  "(a) identity bone: right=green", right,  kGreen);
        }

        // ── (b) 1 bone translate(+0.5) ───────────────────────────────────
        // Quad at NDC x:[-1, 0], bone[0]=translate(+0.5) → quad at [-0.5, 0.5].
        // Expected: centre=red (now covered), left(-0.75)=green (quad moved away).
        {
            dev.Clear(kGreen);

            SkinnedEffect fx(dev);
            setupFx(fx);
            fx.SetBoneTransforms({ Matrix::CreateTranslation(0.5f, 0.0f, 0.0f) });
            fx.setWeightsPerVertexProperty(1);
            fx.Apply();

            auto quad = makeQuad(-1.0f, -0.5f, 0.0f, 0.5f,
                                 1.0f, 0.0f, 0.0f, 0.0f,
                                 0, 0, 0, 0);
            drawAndCheck(dev, quad.data(), fx, W, H);

            Color left   = readPixel(dev, xLeft,   yCenter);
            Color center = readPixel(dev, xCenter, yCenter);

            check(center.getRProperty() > center.getGProperty(), "(b) translate bone: centre=red", center, kRed);
            check(left.getGProperty()   > left.getRProperty(),   "(b) translate bone: left=green", left,   kGreen);
        }

        // ── (c) 2 bones, 50/50 blend ─────────────────────────────────────
        // Quad at NDC x:[-1, 0].
        // bone[0]=translate(+1), weight=0.5; bone[1]=identity, weight=0.5.
        // Net: translate(0.5*1 + 0.5*0) = translate(+0.5) → quad at [-0.5, 0.5].
        // right(+0.75)=green proves bone[0]'s full translation was NOT applied alone
        // (if only bone[0] at weight=1, quad at [0,1] and right pixel would be red).
        {
            dev.Clear(kGreen);

            SkinnedEffect fx(dev);
            setupFx(fx);
            fx.SetBoneTransforms({
                Matrix::CreateTranslation(1.0f, 0.0f, 0.0f),  // bone 0: translate +1
                Matrix::getIdentityProperty(),                  // bone 1: no movement
            });
            fx.setWeightsPerVertexProperty(2);
            fx.Apply();

            auto quad = makeQuad(-1.0f, -0.5f, 0.0f, 0.5f,
                                 0.5f, 0.5f, 0.0f, 0.0f,
                                 0, 1, 0, 0);
            drawAndCheck(dev, quad.data(), fx, W, H);

            Color left   = readPixel(dev, xLeft,   yCenter);
            Color center = readPixel(dev, xCenter, yCenter);
            Color right  = readPixel(dev, xRight,  yCenter);

            check(center.getRProperty() > center.getGProperty(), "(c) 2-bone 50/50: centre=red",  center, kRed);
            check(left.getGProperty()   > left.getRProperty(),   "(c) 2-bone 50/50: left=green",  left,   kGreen);
            check(right.getGProperty()  > right.getRProperty(),  "(c) 2-bone 50/50: right=green", right,  kGreen);
        }

        Exit();
    }

public:
    SkinnedBonesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

int main()
{
    SkinnedBonesTest game;
    game.Run();
    return game.getResult();
}
