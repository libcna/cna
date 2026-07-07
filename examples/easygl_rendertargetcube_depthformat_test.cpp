// SPDX-License-Identifier: MS-PL
// Task 877: verify RenderTargetCube honors its exact requested DepthFormat on EasyGL, not just
// RenderTarget2D (which Task 335 already covers).
//
// Before this task, EasyGLGraphicsBackend::CreateRenderTargetCube() hardcoded `hasDepth=true`
// unconditionally, ignoring whatever DepthFormat the caller actually requested — so a
// RenderTargetCube constructed with DepthFormat::None still silently got a real depth buffer.
//
// Method (mirrors rendertarget2d_depth_test.cpp's near/far-quad methodology, applied to a single
// cube face, sampled back the same way easygl_rendertargetcube_sample_test.cpp does): render a
// near GREEN quad (z=0.2) then a far RED quad (z=0.8) at the same screen position into the
// PositiveZ face, with DepthStencilState::Default (depth test+write, LessEqual) enabled.
//   - DepthFormat::None: no depth buffer should exist, so the depth test is bypassed entirely —
//     the far RED quad (drawn last) incorrectly-but-correctly-per-XNA-semantics overwrites GREEN.
//     This is the actual bug this task fixes: pre-fix, EasyGL always allocated a depth buffer
//     regardless of the request, so this face would incorrectly come back GREEN.
//   - DepthFormat::Depth24Stencil8: a real depth buffer must gate the draws — GREEN (the nearer
//     quad) must win. This is the positive control proving the test methodology can actually
//     detect "depth test is working" (not a harness that always reads back RED/GREEN regardless).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCubeSize = 32;

namespace
{
    void DrawFullQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3(-1.0f, -1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3( 1.0f,  1.0f, z), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class RenderTargetCubeDepthFormatTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 40)
            && closeTo(c.getGProperty(), expected.getGProperty(), 40)
            && closeTo(c.getBProperty(), expected.getBProperty(), 40);
    }

    // Renders near-green-then-far-red into EVERY face of the RenderTargetCube (with real depth
    // testing enabled), unbinds, then samples it back via EnvironmentMapEffect. All 6 faces get
    // identical content so the result doesn't depend on exactly which face the reflection vector
    // happens to land on (mirrors easygl_rendertargetcube_sample_test.cpp's own all-faces-uniform
    // approach) — only the 2 faces actually reached by the depth-test outcome matter.
    Color RenderAndSampleFace(GraphicsDevice& dev, DepthFormat depthFormat)
    {
        RenderTargetCube rtc(dev, kCubeSize, false, SurfaceFormat::Color, depthFormat);

        static const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;

        for (CubeMapFace face : faces)
        {
            dev.SetRenderTarget(&rtc, face);
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f, 0);

            fx.Apply();
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            DrawFullQuad(dev, 0.2f, Color(0, 255, 0, 255));  // near, green — should win if depth-tested
            DrawFullQuad(dev, 0.8f, Color(255, 0, 0, 255));  // far, red — must be rejected if depth-tested
        }

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Texture2D whiteTex(dev, 1, 1);
        const Color kWhite(255, 255, 255, 255);
        whiteTex.SetData(&kWhite, 1);

        dev.Clear(Color(0, 0, 0, 255));

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        // The RT face draw above left DepthStencilState::Default (depth test enabled) active;
        // the backbuffer's own depth buffer was never cleared this frame, so reset to None
        // before the outer sampling quad or it may be incorrectly depth-rejected.
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        EnvironmentMapEffect envFx(dev);
        envFx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        envFx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        envFx.setTextureProperty(&whiteTex);
        envFx.setEnvironmentMapProperty(&rtc);
        envFx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        envFx.setEnvironmentMapAmountProperty(1.0f);
        envFx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        envFx.setWorldProperty(Matrix::getIdentityProperty());
        envFx.setViewProperty(Matrix::getIdentityProperty());
        envFx.setProjectionProperty(Matrix::getIdentityProperty());
        envFx.Apply();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);

        const Color kGreen(0, 255, 0, 255);
        const Color kRed(255, 0, 0, 255);

        Color gotNone = RenderAndSampleFace(dev, DepthFormat::None);
        check(matches(gotNone, kRed),
              "DepthFormat::None: far quad wins (no depth buffer to gate draws)",
              gotNone, "(255,0,0)");

        Color gotDepth = RenderAndSampleFace(dev, DepthFormat::Depth24Stencil8);
        check(matches(gotDepth, kGreen),
              "DepthFormat::Depth24Stencil8: near quad wins (real depth buffer gates draws)",
              gotDepth, "(0,255,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    RenderTargetCubeDepthFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    RenderTargetCubeDepthFormatTest game;
    game.Run();
    return game.getResult();
}
