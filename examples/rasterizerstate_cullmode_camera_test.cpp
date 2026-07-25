// SPDX-License-Identifier: MS-PL
// XNA culling-compatibility investigation (docs/xna_culling_compatibility_audit.md):
// minimal, deterministic CullMode reproducer under a REAL camera (perspective/orthographic
// projection + Matrix::CreateLookAt), extending the existing identity-transform-only coverage
// in examples/easygl_rasterizerstate_cullmode_test.cpp (Tasks 323-325/765, reused verbatim on
// Vulkan as Vulkan_RasterizerState_CullMode). That existing test already establishes, empirically,
// pixel-verified, on all 3 backends: under XNA's default CullCounterClockwiseFace RasterizerState,
// a triangle whose raw NDC-space vertex order has signed area < 0 (by the standard shoelace
// formula, Y-up) SURVIVES; one with signed area > 0 is CULLED. This file checks whether that same
// rule still holds once a real World/View/Projection chain is involved -- the identity-only test
// cannot distinguish "the cull-state-to-native mapping is correct" from "it only happens to look
// correct when World=View=Projection=Identity". No model loading, FBX, textures, lighting, bones,
// or animation are used here -- two flat-colored triangles only.
//
// Method (avoids hand-deriving expected screen positions, which is exactly the kind of arithmetic
// this investigation must not rely on): for every scenario below,
//   1. Render each of the 2 test triangles ALONE, under CullMode::None (so it is visible if it is
//      on screen at all, regardless of the "true" convention), and scan the ENTIRE framebuffer for
//      its own unique color to find a real, backend-reported sample pixel for it. This never
//      hardcodes an expected screen coordinate.
//   2. Compute each triangle's NDC-space signed area directly via CNA's own
//      Vector4::Transform(vertex, World*View*Projection) followed by a manual perspective divide
//      -- i.e. using CNA's own real matrix pipeline, not hand arithmetic -- and derive the
//      "predicted" winding label from the SAME already-established rule the identity test proved
//      (negative NDC signed area -> predicted to survive the default CullCounterClockwiseFace).
//   3. Render BOTH triangles together under CullMode::None / CullClockwiseFace /
//      CullCounterClockwiseFace / the default RasterizerState, sample each triangle's own pixel
//      (found in step 1), and check the actual visibility against the step-2 prediction.
//
// If a scenario's actual result disagrees with its own NDC-signed-area prediction, that is a
// genuine, precisely-located inconsistency between CNA's CPU-side matrix pipeline and its
// backend's actual rasterizer cull decision for that specific View/Projection/World combination
// -- not a guess based on enum names or native API documentation.
//
// Scenarios (Phase 1 of the audit):
//   (a) Identity World/View/Projection, NDC-range vertices -- sanity anchor, must reproduce the
//       already-passing identity test's own result exactly.
//   (b) Orthographic projection + a real Matrix::CreateLookAt view (SimpleAnimation's own camera
//       position/target/up), identity World, world-scale vertices near the view target.
//   (c) Perspective projection (SimpleAnimation's own FOV/aspect/near/far) + the same LookAt view,
//       identity World.
//   (d) Same perspective+LookAt view, POSITIVE-determinant World transform (a real, non-trivial
//       rotation) -- must still match its own NDC prediction; documents that this pipeline stage
//       preserves winding.
//   (e) Same perspective+LookAt view, NEGATIVE-determinant World transform (a mirrored scale) --
//       documented SEPARATELY per the audit's own requirement: a real winding flip is expected
//       here (this is not a bug), so this scenario's own NDC prediction (computed the same way)
//       is expected to itself be flipped relative to (c)/(d) for the same 2 triangles, and the
//       test still checks that the actual render matches ITS OWN prediction, not (c)/(d)'s.
//
// Exit code 0 = every check across every scenario PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 128;

    const Color kBackground(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);      // triangle A
    const Color kGreen(0, 255, 0, 255);    // triangle B

    bool ColorClose(const Color& a, const Color& b, int tol = 30)
    {
        auto d = [](int x, int y) { return std::abs(x - y); };
        return d(a.getRProperty(), b.getRProperty()) <= tol
            && d(a.getGProperty(), b.getGProperty()) <= tol
            && d(a.getBProperty(), b.getBProperty()) <= tol;
    }

    // Local-space triangle patterns, hand-verified via the shoelace formula (see the audit doc):
    // CCW: bottom-left -> bottom-right -> top has POSITIVE signed area (standard math / Y-up
    // convention). CW: the same shape with the last two vertices swapped has NEGATIVE signed area.
    struct Tri { Vector3 v0, v1, v2; };

    Tri MakeCcw(Vector3 center, float halfW, float halfH)
    {
        return Tri{
            Vector3(center.X - halfW, center.Y - halfH, center.Z),
            Vector3(center.X + halfW, center.Y - halfH, center.Z),
            Vector3(center.X,         center.Y + halfH, center.Z),
        };
    }
    Tri MakeCw(Vector3 center, float halfW, float halfH)
    {
        Tri t = MakeCcw(center, halfW, halfH);
        return Tri{ t.v0, t.v2, t.v1 }; // reversed order -> negated signed area
    }

    // Basis-relative variants: the same "bottom-left, bottom-right, top" pattern, but expressed
    // in an arbitrary (right, up) plane through `center` instead of assuming world X/Y axes --
    // used to place test geometry that actually faces a real camera positioned away from the
    // world origin, using that SAME camera's own right/up basis vectors (see Matrix::CreateLookAt
    // in Matrix.cpp) rather than an arbitrary world-axis offset that might not even be visible.
    Vector3 AddScaled(Vector3 base, Vector3 dir, float amount)
    {
        return Vector3(base.X + dir.X * amount, base.Y + dir.Y * amount, base.Z + dir.Z * amount);
    }
    Tri MakeCcwBasis(Vector3 center, Vector3 right, Vector3 up, float halfW, float halfH)
    {
        Vector3 bl = AddScaled(AddScaled(center, right, -halfW), up, -halfH);
        Vector3 br = AddScaled(AddScaled(center, right,  halfW), up, -halfH);
        Vector3 top = AddScaled(center, up, halfH);
        return Tri{ bl, br, top };
    }
    Tri MakeCwBasis(Vector3 center, Vector3 right, Vector3 up, float halfW, float halfH)
    {
        Tri t = MakeCcwBasis(center, right, up, halfW, halfH);
        return Tri{ t.v0, t.v2, t.v1 };
    }

    // Perspective-correct: transform via Vector4::Transform (keeps w), then divide manually.
    // This is CNA's own real matrix pipeline, not hand arithmetic.
    Vector3 ToNdc(const Vector3& v, const Matrix& wvp)
    {
        Vector4 clip = Vector4::Transform(v, wvp);
        const float w = (clip.W != 0.0f) ? clip.W : 1.0f;
        return Vector3(clip.X / w, clip.Y / w, clip.Z / w);
    }

    // Standard shoelace formula on NDC (X,Y), Y-up convention -- matches the already-established
    // rule from examples/easygl_rasterizerstate_cullmode_test.cpp: negative -> survives the
    // default CullCounterClockwiseFace.
    float NdcSignedArea(const Tri& t, const Matrix& wvp)
    {
        Vector3 n0 = ToNdc(t.v0, wvp);
        Vector3 n1 = ToNdc(t.v1, wvp);
        Vector3 n2 = ToNdc(t.v2, wvp);
        return 0.5f * ((n0.X * n1.Y - n1.X * n0.Y)
                      + (n1.X * n2.Y - n2.X * n1.Y)
                      + (n2.X * n0.Y - n0.X * n2.Y));
    }
}

class RasterizerStateCullModeCameraTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_  = 0;
    int  fail_  = 0;
    int  fatal_ = 0; // scenarios that couldn't even be set up (geometry off-screen) -- always a
                     // hard failure, counted and reported separately from a normal PASS/FAIL check.

    void Check(bool ok, const char* label, const char* detail)
    {
        std::printf("[%s] %s %s\n", ok ? "PASS" : "FAIL", label, detail);
        if (ok) ++pass_; else ++fail_;
    }

    std::vector<Color> ReadFramebuffer(GraphicsDevice& dev)
    {
        std::vector<Color> buf(static_cast<size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        Rectangle full(0, 0, kSize, kSize);
        dev.GetBackBufferData(&full, buf.data(), 0, static_cast<int>(buf.size()));
        return buf;
    }

    // Scans the WHOLE framebuffer for a pixel matching `target`; never hardcodes a screen
    // coordinate. Returns std::nullopt if not found anywhere (geometry off-screen or not drawn).
    std::optional<Rectangle> FindPixel(GraphicsDevice& dev, const Color& target)
    {
        const std::vector<Color> buf = ReadFramebuffer(dev);
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                if (ColorClose(buf[static_cast<size_t>(y) * kSize + x], target))
                    return Rectangle(x, y, 1, 1);
            }
        }
        return std::nullopt;
    }

    static Color SampleAt(const std::vector<Color>& buf, const Rectangle& px)
    {
        return buf[static_cast<size_t>(px.getTopProperty()) * kSize
                   + px.getLeftProperty()];
    }

    void DrawTri(GraphicsDevice& dev, const Tri& t, const Color& color)
    {
        const VertexPositionColor verts[3] = { { t.v0, color }, { t.v1, color }, { t.v2, color } };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1);
    }

    void RunScenario(GraphicsDevice& dev, BasicEffect& fx, const char* name,
                      const Matrix& world, const Matrix& view, const Matrix& proj,
                      const Tri& triA, const Tri& triB)
    {
        std::printf("\n--- Scenario: %s ---\n", name);
        const Matrix wvp = world * view * proj;

        // Locate each triangle's own on-screen pixel independently, under CullMode::None.
        auto findOne = [&](const Tri& t, const Color& color) -> std::optional<Rectangle>
        {
            dev.Clear(kBackground);
            dev.setBlendStateProperty(BlendState::Opaque);
            RasterizerState rs; rs.setCullModeProperty(CullMode::None);
            dev.setRasterizerStateProperty(rs);
            fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);
            fx.Apply();
            DrawTri(dev, t, color);
            return FindPixel(dev, color);
        };

        auto pxA = findOne(triA, kRed);
        auto pxB = findOne(triB, kGreen);

        if (!pxA.has_value() || !pxB.has_value())
        {
            std::printf("[FATAL] %s: geometry off-screen (A found=%d, B found=%d) -- scenario "
                        "skipped, not a PASS\n", name, pxA.has_value(), pxB.has_value());
            ++fatal_;
            return;
        }
        std::printf("  triangle A pixel=(%d,%d)  triangle B pixel=(%d,%d)\n",
                    pxA->getLeftProperty(), pxA->getTopProperty(), pxB->getLeftProperty(), pxB->getTopProperty());

        const float areaA = NdcSignedArea(triA, wvp);
        const float areaB = NdcSignedArea(triB, wvp);
        // Established rule: negative NDC signed area -> predicted to survive default
        // CullCounterClockwiseFace (i.e. predicted "CW" in the existing test's own terms).
        const bool aPredictedCw = areaA < 0.0f;
        const bool bPredictedCw = areaB < 0.0f;
        std::printf("  triangle A: NDC signed area=%+.6f -> predicted %s\n",
                    areaA, aPredictedCw ? "CW (survives default)" : "CCW (culled by default)");
        std::printf("  triangle B: NDC signed area=%+.6f -> predicted %s\n",
                    areaB, bPredictedCw ? "CW (survives default)" : "CCW (culled by default)");

        auto renderBoth = [&](CullMode mode)
        {
            dev.Clear(kBackground);
            dev.setBlendStateProperty(BlendState::Opaque);
            RasterizerState rs; rs.setCullModeProperty(mode);
            dev.setRasterizerStateProperty(rs);
            fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);
            fx.Apply();
            DrawTri(dev, triA, kRed);
            DrawTri(dev, triB, kGreen);
        };

        auto checkUnderMode = [&](CullMode mode, const char* modeName)
        {
            renderBoth(mode);
            // One full-frame read samples both triangles. Bgfx's asynchronous screenshot path
            // advances a frame per GetBackBufferData call; two separate 1x1 reads made the second
            // probe observe a later clear-only frame on its Vulkan renderer and falsely looked
            // like the counter-clockwise triangle was always culled.
            const std::vector<Color> frame = ReadFramebuffer(dev);
            Color gotA = SampleAt(frame, *pxA);
            Color gotB = SampleAt(frame, *pxB);
            bool aVisible = ColorClose(gotA, kRed);
            bool bVisible = ColorClose(gotB, kGreen);

            bool expectA, expectB;
            if (mode == CullMode::None) { expectA = true; expectB = true; }
            else if (mode == CullMode::CullCounterClockwiseFace) { expectA = aPredictedCw; expectB = bPredictedCw; }
            else /* CullClockwiseFace */ { expectA = !aPredictedCw; expectB = !bPredictedCw; }

            char detail[256];
            std::snprintf(detail, sizeof(detail),
                          "[%s / %s] triA visible=%d(expected %d) got=(%d,%d,%d)",
                          name, modeName, aVisible, expectA,
                          gotA.getRProperty(), gotA.getGProperty(), gotA.getBProperty());
            Check(aVisible == expectA, "triA", detail);

            std::snprintf(detail, sizeof(detail),
                          "[%s / %s] triB visible=%d(expected %d) got=(%d,%d,%d)",
                          name, modeName, bVisible, expectB,
                          gotB.getRProperty(), gotB.getGProperty(), gotB.getBProperty());
            Check(bVisible == expectB, "triB", detail);
        };

        checkUnderMode(CullMode::None, "None");
        checkUnderMode(CullMode::CullCounterClockwiseFace, "CullCounterClockwiseFace(default)");
        checkUnderMode(CullMode::CullClockwiseFace, "CullClockwiseFace");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;

        // (a) Identity anchor -- NDC-range vertices, matches the already-passing identity test.
        {
            Tri triA = MakeCw(Vector3(-0.5f, 0.0f, 0.0f), 0.45f, 0.45f);   // predicted to survive default
            Tri triB = MakeCcw(Vector3(0.5f, 0.0f, 0.0f), 0.45f, 0.45f);   // predicted to be culled by default
            RunScenario(dev, fx, "(a) Identity World/View/Projection",
                        Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                        Matrix::getIdentityProperty(), triA, triB);
        }

        // SimpleAnimation's own camera (SimpleAnimationGame.hpp Draw()).
        const Vector3 eye(1000.0f, 500.0f, 0.0f);
        const Vector3 target(0.0f, 150.0f, 0.0f);
        const Vector3 up(0.0f, 1.0f, 0.0f);
        Matrix view = Matrix::CreateLookAt(eye, target, up);
        Matrix persp = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, dev.getViewportProperty().getAspectRatioProperty(), 10.0f, 10000.0f);
        Matrix ortho = Matrix::CreateOrthographic(400.0f, 400.0f, 10.0f, 10000.0f);

        // World-scale test triangles placed via the camera's OWN right/up basis vectors (same
        // formula Matrix::CreateLookAt itself uses internally -- see Matrix.cpp), not raw world
        // axis offsets: guarantees the two triangles land within the view frustum and are
        // spread along the camera's actual screen-right/up directions, regardless of where the
        // camera happens to sit relative to the world axes.
        const Vector3 camBack  = Vector3::Normalize(Vector3::Subtract(eye, target));
        const Vector3 camRight = Vector3::Normalize(Vector3::Cross(up, camBack));
        const Vector3 camUp    = Vector3::Cross(camBack, camRight);
        std::printf("camera basis: right=(%.4f,%.4f,%.4f) up=(%.4f,%.4f,%.4f) back=(%.4f,%.4f,%.4f)\n",
                    camRight.X, camRight.Y, camRight.Z, camUp.X, camUp.Y, camUp.Z,
                    camBack.X, camBack.Y, camBack.Z);
        Vector3 centerA = AddScaled(target, camRight, -80.0f);
        Vector3 centerB = AddScaled(target, camRight,  80.0f);
        Tri worldTriA = MakeCwBasis(centerA, camRight, camUp, 40.0f, 60.0f);
        Tri worldTriB = MakeCcwBasis(centerB, camRight, camUp, 40.0f, 60.0f);

        // (b) Orthographic + real LookAt view, identity World.
        RunScenario(dev, fx, "(b) Orthographic + CreateLookAt",
                    Matrix::getIdentityProperty(), view, ortho, worldTriA, worldTriB);

        // (c) Perspective + real LookAt view (SimpleAnimation's exact camera), identity World.
        RunScenario(dev, fx, "(c) Perspective + CreateLookAt (SimpleAnimation camera)",
                    Matrix::getIdentityProperty(), view, persp, worldTriA, worldTriB);

        // (d) Same camera, POSITIVE-determinant World transform (real rotation).
        Matrix posWorld = Matrix::CreateRotationY(0.3f);
        RunScenario(dev, fx, "(d) Perspective + CreateLookAt + positive-determinant World (RotationY)",
                    posWorld, view, persp, worldTriA, worldTriB);

        // (e) Same camera, NEGATIVE-determinant World transform (mirrored scale) -- a real flip
        // is EXPECTED here (documented separately, not a bug); the test still checks the render
        // against THIS scenario's own (also-flipped) NDC prediction, not (c)/(d)'s.
        Matrix negWorld = Matrix::CreateScale(-1.0f, 1.0f, 1.0f);
        RunScenario(dev, fx, "(e) Perspective + CreateLookAt + negative-determinant World (mirrored)",
                    negWorld, view, persp, worldTriA, worldTriB);

        std::printf("\nResult: %d/%d PASS (%d fatal scenario setup failure(s))\n",
                    pass_, pass_ + fail_, fatal_);
        Exit();
    }

public:
    RasterizerStateCullModeCameraTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return (fail_ == 0 && fatal_ == 0) ? 0 : 1; }
};

int main()
{
    RasterizerStateCullModeCameraTest game;
    game.Run();
    return game.getResult();
}
