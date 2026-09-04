// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-130 (finding F-01): a VertexBuffer upload must not write past its
// own mapping.
//
// VulkanRenderer::CreateVertexBuffer is handed a vertex COUNT, never a stride, so
// VulkanVertexBufferRenderer opens with a 64-byte-per-vertex guess. That guess is not a bound:
// the same file's MakeExt3DKey recognises strides 68 (SkinnedPbrEffect), 76 (its dual-UV
// variant) and 80 (glTF skinned PBR + COLOR_0), and SetData used to memcpy
// vertex_count * stride bytes into it unchecked. A 512-vertex stride-80 buffer therefore wrote
// 8 KiB past a 32 KiB mapping, and every draw route -- which copies the vertices back OUT of
// that mapping into its deferred record -- read past it too.
//
// Three legs, in increasing strength:
//
//   A  The allocation is actually widened. Measured through the renderer's own
//      GetLiveVertexBufferBytesEXT() counter as a before/after delta, so this leg fails with a
//      number rather than by whether the process survived its own overrun.
//   B  The bytes survive. GetData round-trips every byte of the widest upload.
//   C  The far end of a wide buffer draws correctly. 512 stride-68 vertices whose only
//      on-screen geometry is the LAST six; DrawPrimitives(vertexStart=506) makes the renderer
//      copy from byte 34,408 of a mapping the old code sized at 32,768. A wrong pixel here is a
//      read that left the mapping.
//
// Validation messages are judged at the end: a leg that passes while the layer complains is not
// a pass.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    // The three strides this renderer's pipeline-key table recognises above 64 bytes, i.e. exactly
    // the layouts the old 64-byte-per-vertex allocation could not hold.
    constexpr int kWideStrides[] = { 68, 76, 80 };

    // Enough vertices that the shortfall is several pages, not an allocation-rounding accident:
    // at stride 80 the old code overran by 512 * 16 = 8192 bytes.
    constexpr int kVertexCount = 512;

    // pbr3d_skinned.vert.glsl's attribute layout: position, normal, tangent(4), uv, blend
    // weights(4), blend indices(4 x uint8).
    struct SkinnedPbrGpuVertex
    {
        float        px, py, pz;
        float        nx, ny, nz;
        float        tx, ty, tz, tw;
        float        u, v;
        float        w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrGpuVertex) == 68, "skinned PBR vertex must be 68 bytes");

    constexpr int kSize = 64;

    bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
}

class VulkanVertexBufferWideStrideBoundsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label, detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        // Registered only for CNA_GRAPHICS_RENDERER=VULKAN, so this cannot be null.
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // ── Leg A + B: the allocation is widened, and the bytes survive ───────────

    void testAllocationAndRoundTrip(GraphicsDevice& dev, int stride)
    {
        // A recognisable pattern, distinct per byte position, so a short allocation cannot pass by
        // coincidence and a truncating "fix" cannot pass at all.
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(kVertexCount) * stride);
        for (std::size_t i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<std::uint8_t>((i * 31u + 7u) & 0xFFu);

        const VkDeviceSize before = Renderer().GetLiveVertexBufferBytesEXT();
        {
            VertexBuffer vb(dev, kVertexCount);
            vb.SetDataRaw(payload.data(), kVertexCount, stride);

            const VkDeviceSize after  = Renderer().GetLiveVertexBufferBytesEXT();
            const VkDeviceSize mapped = after - before;
            const VkDeviceSize wanted =
                static_cast<VkDeviceSize>(kVertexCount) * static_cast<VkDeviceSize>(stride);

            char label[96];
            std::snprintf(label, sizeof(label), "A stride-%d allocation covers the upload", stride);
            check(mapped >= wanted,
                  label,
                  "mapped=" + std::to_string(mapped) + " bytes, upload needs "
                      + std::to_string(wanted));

            std::vector<std::uint8_t> readBack(payload.size(), 0u);
            vb.GetDataRawEXT(0, readBack.data(), kVertexCount, stride);
            std::size_t firstBad = payload.size();
            for (std::size_t i = 0; i < payload.size(); ++i)
                if (readBack[i] != payload[i]) { firstBad = i; break; }

            std::snprintf(label, sizeof(label), "B stride-%d GetData round-trips every byte", stride);
            check(firstBad == payload.size(),
                  label,
                  firstBad == payload.size()
                      ? std::to_string(payload.size()) + " bytes identical"
                      : "first mismatch at byte " + std::to_string(firstBad));
        }
    }

    // ── Leg C: the far end of a wide buffer draws ────────────────────────────

    void testFarEndDraw(GraphicsDevice& dev)
    {
        // Every vertex but the last six sits far behind the camera, so only the tail can colour
        // the centre pixel: a draw that read the wrong bytes cannot accidentally produce the
        // expected colour from the head of the buffer.
        std::vector<SkinnedPbrGpuVertex> verts(kVertexCount);
        for (auto& v : verts) {
            v.px = v.py = 0.0f; v.pz = -1000.0f;
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f; v.tw = 1.0f;
            v.u = v.v = 0.0f;
            v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;
            v.i0 = v.i1 = v.i2 = v.i3 = 0;
        }

        const int start = kVertexCount - 6;
        const float xs[6] = { -4.0f, -4.0f,  4.0f, -4.0f,  4.0f, 4.0f };
        const float ys[6] = {  4.0f, -4.0f, -4.0f,  4.0f, -4.0f, 4.0f };
        for (int i = 0; i < 6; ++i) {
            SkinnedPbrGpuVertex& v = verts[static_cast<std::size_t>(start + i)];
            v.px = xs[i]; v.py = ys[i]; v.pz = 0.0f;
        }

        VertexBuffer vb(dev, kVertexCount);
        vb.SetDataRaw(verts.data(), kVertexCount, static_cast<int>(sizeof(SkinnedPbrGpuVertex)));

        Texture2D albedo(dev, 1, 1);
        Color white(255, 255, 255, 255);
        albedo.SetData(&white, 1);

        SkinnedPbrEffect fx(dev);
        fx.setBaseColorTextureIsSrgbEXTProperty(false);
        fx.setEmissiveTextureIsSrgbEXTProperty(false);
        fx.setEncodeOutputToSrgbEXTProperty(false);
        fx.setTextureProperty(&albedo);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        dev.Clear(Color(0, 0, 0, 255));
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, start, 2);
        dev.SetVertexBuffer(nullptr);

        const Color got = readCenter(dev);
        // Ambient-only, unlit white albedo: the quad is lit, the clear is not. The discriminating
        // question is "did the tail six vertices reach the rasteriser", so the assertion is that
        // the centre pixel is no longer the black clear -- with a same-frame control below that
        // proves black is otherwise what this scene produces.
        const bool covered = got.getRProperty() > 40 || got.getGProperty() > 40
                          || got.getBProperty() > 40;
        check(covered, "C stride-68 draw from vertexStart=506 covers the centre",
              "centre=(" + std::to_string(got.getRProperty()) + ","
                         + std::to_string(got.getGProperty()) + ","
                         + std::to_string(got.getBProperty()) + ")");

        // Control: the same buffer drawn from the HEAD, where every vertex is behind the camera.
        // If this also covered the centre, leg C above would prove nothing about which bytes the
        // renderer read.
        dev.Clear(Color(0, 0, 0, 255));
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        const Color control = readCenter(dev);
        const bool blank = control.getRProperty() <= 40 && control.getGProperty() <= 40
                        && control.getBProperty() <= 40;
        check(blank, "C control: the head of the same buffer draws nothing",
              "centre=(" + std::to_string(control.getRProperty()) + ","
                         + std::to_string(control.getGProperty()) + ","
                         + std::to_string(control.getBProperty()) + ")");

        // The two readings must also differ from each other, which no single threshold can fake.
        const bool distinguishable =
            !(closeTo(got.getRProperty(), control.getRProperty(), 8)
              && closeTo(got.getGProperty(), control.getGProperty(), 8)
              && closeTo(got.getBProperty(), control.getBProperty(), 8));
        check(distinguishable, "C tail and head readings are distinguishable",
              "tail=(" + std::to_string(got.getRProperty()) + ","
                       + std::to_string(got.getGProperty()) + ","
                       + std::to_string(got.getBProperty()) + ") head=("
                       + std::to_string(control.getRProperty()) + ","
                       + std::to_string(control.getGProperty()) + ","
                       + std::to_string(control.getBProperty()) + ")");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        for (int stride : kWideStrides)
            testAllocationAndRoundTrip(dev, stride);

        testFarEndDraw(dev);

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "D no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanVertexBufferWideStrideBoundsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanVertexBufferWideStrideBoundsTest g;
    g.Run();
    return g.getResult();
}
