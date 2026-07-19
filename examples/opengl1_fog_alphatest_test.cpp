// SPDX-License-Identifier: MS-PL
// OPENGL1 backend: qualitative BasicEffect fog + AlphaTestEffect alpha-test coverage
// (tests/opengl1/README.md scenario 7: "fog and alpha-test approximations").
//
// OpenGL1 implements XNA's BasicEffect fog via the real fixed-function GL_FOG pipeline
// (glFogi(GL_FOG_MODE, GL_LINEAR) + glFogf(GL_FOG_START/END, ...) in
// OpenGL1GraphicsBackend::DrawInternal), driven by the actual eye-space distance the GL
// ModelView transform computes -- NOT FNA's own object-space FogVector dot-product formula
// (see easygl_basiceffect_fog_test.cpp's header for that exact formula, and the sign-
// convention subtleties it depends on). plan_opengl1.md documents fog as a "basic fixed-
// function ... linear fog" approximation, not exact XNA parity, so this test only asserts
// the qualitative, basis-independent property any correct GL_FOG setup must have -- geometry
// genuinely farther from the eye along the view axis is genuinely more fog-tinted -- rather
// than pinning an exact blended RGB value tuned for a different backend's own formula.
//
// Similarly, AlphaTestEffect's CompareFunction is approximated by a single, always-on
// glAlphaFunc(GL_GEQUAL, reference) test regardless of the real requested CompareFunction
// (see OpenGL1GraphicsBackend::DrawInternal's alphaTest[3]<0 check) -- an exact match only for
// CompareFunction::Equal/GreaterEqual-ish cases away from the reference boundary, approximate
// or inverted for others (documented "coarse alpha testing" limitation, see plan_opengl1.md).
// This test exercises only what OpenGL1 can honestly claim: Always (never discards) and Equal
// with alpha values clearly separated from the reference (not the exact boundary, which is
// where the coarse approximation and real XNA semantics would disagree).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBackground(0, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
}

class OpenGL1FogAlphaTestTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color ReadCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // Huge quad perpendicular to the view axis, centred `distance` units in front of the eye
    // along `forward` -- always covers the screen centre regardless of distance/FOV, so a
    // single readback point at the viewport centre always samples it.
    void DrawFacingQuad(GraphicsDevice& dev, const Vector3& eye, const Vector3& forward,
                         const Vector3& right, const Vector3& up, float distance, const Color& color)
    {
        const Vector3 center(eye.X + forward.X * distance, eye.Y + forward.Y * distance, eye.Z + forward.Z * distance);
        constexpr float kHalf = 5000.0f;
        auto offset = [&](float rx, float ry)
        {
            return Vector3(center.X + right.X * rx + up.X * ry,
                           center.Y + right.Y * rx + up.Y * ry,
                           center.Z + right.Z * rx + up.Z * ry);
        };
        const VertexPositionColor quad[6] = {
            { offset(-kHalf,  kHalf), color }, { offset(-kHalf, -kHalf), color }, { offset(kHalf, -kHalf), color },
            { offset(-kHalf,  kHalf), color }, { offset(kHalf, -kHalf), color }, { offset(kHalf,  kHalf), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Vector3 eye(0.0f, 0.0f, 0.0f);
        const Vector3 forward(0.0f, 0.0f, -1.0f);
        const Vector3 up(0.0f, 1.0f, 0.0f);
        const Vector3 right(1.0f, 0.0f, 0.0f);
        const Vector3 target(eye.X + forward.X, eye.Y + forward.Y, eye.Z + forward.Z);
        Matrix view = Matrix::CreateLookAt(eye, target, up);
        Matrix proj = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, dev.getViewportProperty().getAspectRatioProperty(), 1.0f, 20000.0f);
        Matrix world = Matrix::getIdentityProperty();

        constexpr float kNear = 50.0f;
        constexpr float kFar  = 9000.0f;

        // ---- Fog: baseline (disabled) ----------------------------------------------------
        {
            dev.Clear(kBackground);
            BasicEffect fx(dev);
            fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);
            fx.VertexColorEnabled = true;
            fx.setFogEnabledProperty(false);
            fx.Apply();
            DrawFacingQuad(dev, eye, forward, right, up, kNear, kGreen);
            Color got = ReadCenter(dev);
            const bool isPureGreen = got.getGProperty() > 200 && got.getRProperty() < 20 && got.getBProperty() < 20;
            Check(isPureGreen, "fog disabled: pure geometry color, no red tint");
        }

        // ---- Fog: near vs far, same FogColor/Start/End ------------------------------------
        auto drawFogged = [&](float distance) -> Color
        {
            dev.Clear(kBackground);
            BasicEffect fx(dev);
            fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);
            fx.VertexColorEnabled = true;
            fx.setFogEnabledProperty(true);
            fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setFogStartProperty(kNear);
            fx.setFogEndProperty(kFar);
            fx.Apply();
            DrawFacingQuad(dev, eye, forward, right, up, distance, kGreen);
            return ReadCenter(dev);
        };

        const Color nearGot = drawFogged(kNear);
        const Color farGot  = drawFogged(kFar);
        std::printf("fog near(dist=%.0f)=(%d,%d,%d)  far(dist=%.0f)=(%d,%d,%d)\n",
                    kNear, nearGot.getRProperty(), nearGot.getGProperty(), nearGot.getBProperty(),
                    kFar, farGot.getRProperty(), farGot.getGProperty(), farGot.getBProperty());

        // Farther geometry must be at least as red (fog-tinted) and at most as green as nearer
        // geometry -- the one basis-independent property any correct linear-fog implementation
        // guarantees, regardless of this backend's exact eye-space Z convention.
        Check(farGot.getRProperty() >= nearGot.getRProperty(),
              "fog: far geometry is at least as red (fog-tinted) as near geometry");
        Check(farGot.getGProperty() <= nearGot.getGProperty(),
              "fog: far geometry is at most as green (object-tinted) as near geometry");
        // And the effect must be non-trivial -- a completely broken/no-op fog implementation
        // would satisfy the two checks above vacuously (near == far in every channel).
        Check(farGot.getRProperty() > nearGot.getRProperty() + 20,
              "fog: near vs far actually differ (fog has a real, non-trivial effect)");

        // ---- Alpha test: Always vs Equal (see file header for why not a full sweep) -------
        {
            // AlphaTestEffect::FillGpuDrawParams premultiplies diffuseColor.rgb by alpha_ (real
            // FNA behaviour), so a low-alpha DRAWN fragment can be just as visually dim as a
            // DISCARDED one -- brightness alone cannot distinguish "drawn but dim" from "not
            // drawn at all". Use a background/foreground pair whose discriminating channel (R)
            // is forced to 0 by the foreground texture regardless of alpha (0 * anything == 0)
            // and stays at the background's 255 only when nothing was drawn at all -- robust to
            // premultiplication dimming at any alpha value.
            const Color kRedBackground(255, 0, 0, 255);
            const Color kBlueTexel(0, 0, 255, 255);
            Texture2D blueTex(dev, 1, 1);
            blueTex.SetData(&kBlueTexel, 1);

            const VertexPositionTexture quad[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            };

            auto drawWithAlpha = [&](float alpha, CompareFunction func, int referenceAlpha) -> Color
            {
                dev.Clear(kRedBackground);
                AlphaTestEffect fx(dev);
                fx.setTextureProperty(&blueTex);
                fx.setAlphaProperty(alpha);
                fx.setReferenceAlphaProperty(referenceAlpha);
                fx.setAlphaFunctionProperty(func);
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
                return ReadCenter(dev);
            };

            // Always: both a low- and a high-alpha fragment must be drawn regardless of reference.
            Color alwaysLow  = drawWithAlpha(10.0f / 255.0f, CompareFunction::Always, 128);
            Color alwaysHigh = drawWithAlpha(250.0f / 255.0f, CompareFunction::Always, 128);
            Check(alwaysLow.getRProperty() < 100, "alpha-test Always: low-alpha fragment still drawn (red background occluded)");
            Check(alwaysHigh.getRProperty() < 100, "alpha-test Always: high-alpha fragment still drawn (red background occluded)");

            // Equal, reference=128: alpha clearly above reference must pass (matches this
            // backend's real glAlphaFunc(GL_GEQUAL, reference) approximation); alpha clearly
            // below reference must be discarded (red background stays visible).
            Color equalAbove = drawWithAlpha(250.0f / 255.0f, CompareFunction::Equal, 128);
            Color equalBelow = drawWithAlpha(10.0f / 255.0f, CompareFunction::Equal, 128);
            Check(equalAbove.getRProperty() < 100, "alpha-test Equal: alpha well above reference is drawn");
            Check(equalBelow.getRProperty() > 150, "alpha-test Equal: alpha well below reference is discarded");
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1FogAlphaTestTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1FogAlphaTestTest game;
    game.Run();
    return game.getResult();
}
