// SPDX-License-Identifier: MS-PL
// XNA culling-compatibility investigation (docs/xna_culling_compatibility_audit.md), Phase 3/5:
// checks whether CullMode is handled consistently on the REAL code path
// Microsoft::Xna::Framework::Graphics::Model / ModelMesh::Draw() actually uses --
// GraphicsDevice::DrawIndexedPrimitives() with a real bound VertexBuffer/IndexBuffer and a
// BasicEffect -- as opposed to examples/rasterizerstate_cullmode_camera_test.cpp, which (like
// the pre-existing examples/easygl_rasterizerstate_cullmode_test.cpp) uses
// GraphicsDevice::DrawUserPrimitives(type, vertexData, ...), a SEPARATE, simpler
// VertexPositionColor-only "legacy" dispatch path (GraphicsDevice::DrawUserPrimitives ->
// IGraphicsBackend::DrawColoredPrimitives) that every existing CullMode test happens to use.
//
// Uses the SAME NDC-signed-area-prediction methodology as rasterizerstate_cullmode_camera_test.cpp
// (see that file's own header for the full rationale) but drives the real
// VertexBuffer+IndexBuffer+BasicEffect+DrawIndexedPrimitives path, with the SimpleAnimation
// sample's own exact camera (Matrix::CreateLookAt(eye,target,up) + CreatePerspectiveFieldOfView),
// lighting DISABLED (BasicEffect defaults -- Model/ModelMesh loading itself doesn't force lighting
// on; SimpleAnimation's own Tank::Draw() calls EnableDefaultLighting() separately, covered by
// rasterizerstate_cullmode_lit_basiceffect_test.cpp). No Model/FBX/texture/bone/animation
// involved -- two flat-colored (DiffuseColor, VertexColorEnabled=false) indexed triangles only.
//
// Exit code 0 = every check PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

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
    const Vector3 kRed(1.0f, 0.0f, 0.0f);      // triangle A DiffuseColor
    const Vector3 kGreen(0.0f, 1.0f, 0.0f);    // triangle B DiffuseColor

    bool ColorClose(const Color& a, const Color& b, int tol = 30)
    {
        auto d = [](int x, int y) { return std::abs(x - y); };
        return d(a.getRProperty(), b.getRProperty()) <= tol
            && d(a.getGProperty(), b.getGProperty()) <= tol
            && d(a.getBProperty(), b.getBProperty()) <= tol;
    }

    // EnableDefaultLighting() dims/tints exact DiffuseColor output (ambient + diffuse*NdotL), so
    // an exact (255,0,0)/(0,255,0) match is unreliable -- detect by dominant channel instead.
    bool IsDominantRed(const Color& c)
    {
        return c.getRProperty() > c.getGProperty() + 20 && c.getRProperty() > c.getBProperty() + 20
            && c.getRProperty() > 15;
    }
    bool IsDominantGreen(const Color& c)
    {
        return c.getGProperty() > c.getRProperty() + 20 && c.getGProperty() > c.getBProperty() + 20
            && c.getGProperty() > 15;
    }

    struct Tri { Vector3 v0, v1, v2; };

    Vector3 AddScaled(Vector3 base, Vector3 dir, float amount)
    {
        return Vector3(base.X + dir.X * amount, base.Y + dir.Y * amount, base.Z + dir.Z * amount);
    }
    // Same "bottom-left, bottom-right, top" pattern as the camera test, expressed in an
    // arbitrary (right, up) plane through `center`.
    Tri MakeCcwBasis(Vector3 center, Vector3 right, Vector3 up, float halfW, float halfH)
    {
        Vector3 bl  = AddScaled(AddScaled(center, right, -halfW), up, -halfH);
        Vector3 br  = AddScaled(AddScaled(center, right,  halfW), up, -halfH);
        Vector3 top = AddScaled(center, up, halfH);
        return Tri{ bl, br, top };
    }
    Tri MakeCwBasis(Vector3 center, Vector3 right, Vector3 up, float halfW, float halfH)
    {
        Tri t = MakeCcwBasis(center, right, up, halfW, halfH);
        return Tri{ t.v0, t.v2, t.v1 };
    }

    Vector3 ToNdc(const Vector3& v, const Matrix& wvp)
    {
        Vector4 clip = Vector4::Transform(v, wvp);
        const float w = (clip.W != 0.0f) ? clip.W : 1.0f;
        return Vector3(clip.X / w, clip.Y / w, clip.Z / w);
    }

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

class RasterizerStateCullModeIndexedBasicEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_  = 0;
    int  fail_  = 0;
    int  fatal_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    std::vector<Color> ReadFramebuffer(GraphicsDevice& dev)
    {
        std::vector<Color> buf(static_cast<size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        Rectangle full(0, 0, kSize, kSize);
        dev.GetBackBufferData(&full, buf.data(), 0, static_cast<int>(buf.size()));
        return buf;
    }

    template <typename Pred>
    std::optional<Rectangle> FindPixelIf(GraphicsDevice& dev, Pred pred)
    {
        const std::vector<Color> buf = ReadFramebuffer(dev);
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x)
                if (pred(buf[static_cast<size_t>(y) * kSize + x]))
                    return Rectangle(x, y, 1, 1);
        return std::nullopt;
    }

    static Color SampleAt(const std::vector<Color>& buf, const Rectangle& px)
    {
        return buf[static_cast<size_t>(px.getTopProperty()) * kSize
                   + px.getLeftProperty()];
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        const Vector3 eye(1000.0f, 500.0f, 0.0f);
        const Vector3 target(0.0f, 150.0f, 0.0f);
        const Vector3 up(0.0f, 1.0f, 0.0f);
        Matrix view = Matrix::CreateLookAt(eye, target, up);
        Matrix proj = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, dev.getViewportProperty().getAspectRatioProperty(), 10.0f, 10000.0f);
        Matrix world = Matrix::getIdentityProperty();
        const Matrix wvp = world * view * proj;

        const Vector3 camBack  = Vector3::Normalize(Vector3::Subtract(eye, target));
        const Vector3 camRight = Vector3::Normalize(Vector3::Cross(up, camBack));
        const Vector3 camUp    = Vector3::Cross(camBack, camRight);
        Vector3 centerA = AddScaled(target, camRight, -80.0f);
        Vector3 centerB = AddScaled(target, camRight,  80.0f);
        Tri triA = MakeCwBasis(centerA, camRight, camUp, 40.0f, 60.0f);   // predicted CW
        Tri triB = MakeCcwBasis(centerB, camRight, camUp, 40.0f, 60.0f);  // predicted CCW

        // Build the REAL indexed-draw geometry: VertexPositionNormalTexture (matches every
        // BasicEffect-driven ModelMeshPart's own stride-32 format), a real VertexBuffer +
        // IndexBuffer, matching GraphicsDevice::DrawIndexedPrimitives (== ModelMesh::Draw()'s
        // own dispatch) instead of DrawUserPrimitives' separate DrawColoredPrimitives path.
        // Normal is irrelevant here (lighting is off), filled with a placeholder.
        //
        // Deliberately TWO SEPARATE VertexBuffer/IndexBuffer pairs (startIndex=0 for both),
        // matching how a real Model's ModelMeshPart objects each own their own dedicated,
        // complete buffer (confirmed directly in tank.model.json: every mesh has its own
        // "vertices"/"indices" .bin pair, never a shared buffer with per-part offsets) --
        // NOT one shared buffer read at two different startIndex offsets. The latter was
        // tried first and found a genuine, separate, confirmed bug (not this task's own root
        // cause): BgfxGraphicsBackend::DrawIndexedPrimitivesEx's non-wireframe path calls
        // bgfx::setIndexBuffer(ib.handle) with no offset/count, silently ignoring
        // GpuDrawParams::startIndex entirely -- see the audit doc's own dedicated section.
        const Vector3 dummyNormal(0.0f, 0.0f, 1.0f);
        const Vector2 dummyUv(0.0f, 0.0f);
        VertexPositionNormalTexture vertsA[3] = {
            { triA.v0, dummyNormal, dummyUv }, { triA.v1, dummyNormal, dummyUv }, { triA.v2, dummyNormal, dummyUv },
        };
        VertexPositionNormalTexture vertsB[3] = {
            { triB.v0, dummyNormal, dummyUv }, { triB.v1, dummyNormal, dummyUv }, { triB.v2, dummyNormal, dummyUv },
        };
        std::uint16_t indices[3] = { 0, 1, 2 };

        VertexBuffer vbA(dev, 3); vbA.SetData(vertsA, 3);
        VertexBuffer vbB(dev, 3); vbB.SetData(vertsB, 3);
        IndexBuffer ibA(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None); ibA.SetData(indices, 3);
        IndexBuffer ibB(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None); ibB.SetData(indices, 3);

        BasicEffect fx(dev);
        fx.setWorldProperty(world);
        fx.setViewProperty(view);
        fx.setProjectionProperty(proj);
        fx.setTextureEnabledProperty(false);
        fx.VertexColorEnabled = false;
        // Matches SimpleAnimation's own Tank::Draw(), which calls this every frame for every
        // mesh part -- diagnostic check for whether lighting activation itself (a totally
        // different shader-selection path in every backend) has any bearing on CullMode.
        fx.EnableDefaultLighting();

        auto findOne = [&](VertexBuffer& vb, IndexBuffer& ib, Vector3 diffuse, bool wantRed) -> std::optional<Rectangle>
        {
            // Retry loop: Bgfx's GetBackBufferData only reliably reflects the FIRST read per
            // rendered frame (Task 406) -- a lone Clear+Draw+Read can see stale data from a
            // previous call within the same Draw() invocation. Retrying across real frames
            // gives it a chance to catch up; harmless on EasyGL/Vulkan, which succeed on the
            // first attempt.
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                dev.Clear(kBackground);
                dev.setBlendStateProperty(BlendState::Opaque);
                RasterizerState rs; rs.setCullModeProperty(CullMode::None);
                dev.setRasterizerStateProperty(rs);
                dev.SetVertexBuffer(&vb);
                dev.SetIndexBuffer(&ib);
                fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);
                fx.setDiffuseColorProperty(diffuse);
                fx.Apply();
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
                auto found = wantRed ? FindPixelIf(dev, IsDominantRed) : FindPixelIf(dev, IsDominantGreen);
                if (found.has_value())
                    return found;
            }
            return std::nullopt;
        };

        auto pxA = findOne(vbA, ibA, kRed, true);
        auto pxB = findOne(vbB, ibB, kGreen, false);

        if (!pxA.has_value() || !pxB.has_value())
        {
            std::printf("[FATAL] geometry off-screen (A found=%d, B found=%d)\n",
                        pxA.has_value(), pxB.has_value());
            ++fatal_;
            std::printf("\nResult: %d/%d PASS (%d fatal)\n", pass_, pass_ + fail_, fatal_);
            Exit();
            return;
        }
        std::printf("triangle A pixel=(%d,%d)  triangle B pixel=(%d,%d)\n",
                    pxA->getLeftProperty(), pxA->getTopProperty(), pxB->getLeftProperty(), pxB->getTopProperty());

        const float areaA = NdcSignedArea(triA, wvp);
        const float areaB = NdcSignedArea(triB, wvp);
        const bool aPredictedCw = areaA < 0.0f;
        const bool bPredictedCw = areaB < 0.0f;
        std::printf("triangle A: NDC signed area=%+.6f -> predicted %s\n",
                    areaA, aPredictedCw ? "CW (survives default)" : "CCW (culled by default)");
        std::printf("triangle B: NDC signed area=%+.6f -> predicted %s\n",
                    areaB, bPredictedCw ? "CW (survives default)" : "CCW (culled by default)");

        auto renderBoth = [&](CullMode mode)
        {
            dev.Clear(kBackground);
            dev.setBlendStateProperty(BlendState::Opaque);
            RasterizerState rs; rs.setCullModeProperty(mode);
            dev.setRasterizerStateProperty(rs);
            fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);

            dev.SetVertexBuffer(&vbA);
            dev.SetIndexBuffer(&ibA);
            fx.setDiffuseColorProperty(kRed);
            fx.Apply();
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);

            dev.SetVertexBuffer(&vbB);
            dev.SetIndexBuffer(&ibB);
            fx.setDiffuseColorProperty(kGreen);
            fx.Apply();
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
        };

        auto checkMode = [&](CullMode mode, const char* modeName)
        {
            renderBoth(mode);
            // One full-frame read samples both triangles. Bgfx's asynchronous screenshot path
            // advances a frame per GetBackBufferData call; two separate 1x1 reads made the second
            // probe observe a later clear-only frame on its Vulkan renderer and falsely looked
            // like the counter-clockwise triangle was always culled.
            const std::vector<Color> frame = ReadFramebuffer(dev);
            Color gotA = SampleAt(frame, *pxA);
            Color gotB = SampleAt(frame, *pxB);
            bool aVisible = IsDominantRed(gotA);
            bool bVisible = IsDominantGreen(gotB);

            bool expectA, expectB;
            if (mode == CullMode::None) { expectA = true; expectB = true; }
            else if (mode == CullMode::CullCounterClockwiseFace) { expectA = aPredictedCw; expectB = bPredictedCw; }
            else { expectA = !aPredictedCw; expectB = !bPredictedCw; }

            char label[256];
            std::snprintf(label, sizeof(label), "%s: triA visible=%d(expected %d) got=(%d,%d,%d)",
                          modeName, aVisible, expectA, gotA.getRProperty(), gotA.getGProperty(), gotA.getBProperty());
            Check(aVisible == expectA, label);
            std::snprintf(label, sizeof(label), "%s: triB visible=%d(expected %d) got=(%d,%d,%d)",
                          modeName, bVisible, expectB, gotB.getRProperty(), gotB.getGProperty(), gotB.getBProperty());
            Check(bVisible == expectB, label);
        };

        checkMode(CullMode::None, "None");
        checkMode(CullMode::CullCounterClockwiseFace, "CullCounterClockwiseFace(default)");
        checkMode(CullMode::CullClockwiseFace, "CullClockwiseFace");

        std::printf("\nResult: %d/%d PASS (%d fatal)\n", pass_, pass_ + fail_, fatal_);
        Exit();
    }

public:
    RasterizerStateCullModeIndexedBasicEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return (fail_ == 0 && fatal_ == 0) ? 0 : 1; }
};

int main()
{
    RasterizerStateCullModeIndexedBasicEffectTest game;
    game.Run();
    return game.getResult();
}
