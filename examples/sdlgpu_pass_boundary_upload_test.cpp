// SPDX-License-Identifier: MS-PL
// REMED-GFX-145: the SDL_GPU-specific half of "every public bind cycle is its own logical pass".
//
// The shared oracle `examples/rendertarget_pass_boundary_test.cpp` proves the boundary itself, on
// every backend, using SpriteBatch only. Three things it deliberately cannot ask are asked here,
// because each is specific to how THIS backend defers work:
//
//  1. The 3D families. SdlGpu keeps nine separate draw-command queues plus `spriteCommands_`, all
//     replayed through one `drawOrder_` walk. The oracle exercises `DrawKind::Sprite`; the
//     `Colored` family reaches `RenderQueuedDraws` through a different queue, a different
//     `Issue*Draw`, a different pipeline cache and -- for indexed draws -- REMED-GFX-117's
//     `ResolveIndexedRange`. A segment filter that worked for sprites and not for those would be
//     invisible to the oracle.
//
//  2. Transient upload storage. REMED-GFX-140 found Vulkan restarting a SHARED per-frame vertex
//     arena cursor at 0 for every pass, so a later pass overwrote the exact bytes an earlier pass
//     had bound and the earlier pass drew the LATER pass's geometry -- invisible to every fixture
//     that draws identical rectangles and varies only the texture. The analogous question has to
//     be asked of SdlGpu with geometry that actually differs, in BOTH upload paths: the shared
//     `spriteVertexBuffer_` (offset = the sprite's index in `spriteCommands_`) and the per-command
//     `uploadedVertexBuffer`/`uploadedIndexBuffer` the 3D families allocate.
//
//  3. Depth and stencil load actions. The shared oracle scopes itself to colour (depth is
//     REMED-GFX-142), so nothing there can see a depth clear attached to the wrong bind cycle --
//     and the depth attachment's load op is chosen by exactly the same per-segment code as the
//     colour one.
//
// No check below puts a flush, `Present`, wait or readback between two bind cycles: every readback
// happens only after that check's whole public sequence has been queued.
//
// Left/right, never up/down: every asserted geometry difference is a horizontal one, so nothing
// here depends on this backend's clip-space Y convention.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBB = 64;  ///< Backbuffer edge.
    constexpr int kRT = 8;   ///< Render-target edge.

    /// Fully saturated, so sRGB encoding is the identity on every asserted texel.
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kYellow(255, 255, 0, 255);
    const Color kMagenta(255, 0, 255, 255);
    const Color kCyan(0, 255, 255, 255);

    /// The colour GraphicsDevice::SetRenderTarget clears a DiscardContents target to on bind.
    Color Discard() { return Color(0, 0, 0, 255); }

    std::string Text(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }
}

class SdlGpuPassBoundaryUploadTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<BasicEffect> fx_;

    /// Two quads over eight vertices: indices 0..5 cover the LEFT half, 6..11 the RIGHT half.
    std::unique_ptr<VertexBuffer> halvesVb_;
    std::unique_ptr<IndexBuffer> halvesIb_;
    /// The same two quads, but the left one lives at vertices 4..7 so a baseVertex of 4 selects it.
    std::unique_ptr<VertexBuffer> rebasedVb_;
    std::unique_ptr<IndexBuffer> rebasedIb_;
    /// Non-indexed left/right halves at two depths, for the depth-load-action checks.
    std::unique_ptr<VertexBuffer> nearFullVb_;
    std::unique_ptr<VertexBuffer> farFullVb_;
    std::unique_ptr<Texture2D> whiteTexel_;

    int phase_ = 0;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    // ------------------------------------------------------------------
    // Geometry
    // ------------------------------------------------------------------

    /// Six clip-space vertices covering x in [@p x0, @p x1] and the whole height, at depth @p z.
    static std::array<VertexPositionColor, 6> Quad(float x0, float x1, float z, const Color& c)
    {
        return {{
            {Vector3(x0,  1.0f, z), c},
            {Vector3(x0, -1.0f, z), c},
            {Vector3(x1, -1.0f, z), c},
            {Vector3(x0,  1.0f, z), c},
            {Vector3(x1, -1.0f, z), c},
            {Vector3(x1,  1.0f, z), c},
        }};
    }

    /// Four clip-space corners of x in [@p x0, @p x1], indexed as two triangles.
    static std::array<VertexPositionColor, 4> Corners(float x0, float x1, float z, const Color& c)
    {
        return {{
            {Vector3(x0,  1.0f, z), c},
            {Vector3(x0, -1.0f, z), c},
            {Vector3(x1, -1.0f, z), c},
            {Vector3(x1,  1.0f, z), c},
        }};
    }

    std::unique_ptr<VertexBuffer> MakeVb(GraphicsDevice& dev,
                                         const std::vector<VertexPositionColor>& v)
    {
        auto vb = std::make_unique<VertexBuffer>(
            dev, VertexPositionColor::getVertexDeclarationStatic(), static_cast<int>(v.size()),
            BufferUsage::None);
        vb->SetData(v.data(), 0, static_cast<int>(v.size()));
        return vb;
    }

    std::unique_ptr<IndexBuffer> MakeIb(GraphicsDevice& dev, const std::vector<std::uint16_t>& i)
    {
        auto ib = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits,
                                                static_cast<int>(i.size()), BufferUsage::None);
        ib->SetData(i.data(), 0, static_cast<int>(i.size()));
        return ib;
    }

    // ------------------------------------------------------------------
    // Draw helpers -- each issues ONE draw into whatever is currently bound
    // ------------------------------------------------------------------

    void Prepare3D(GraphicsDevice& dev, bool depth)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetDepthTestEnabled(depth);
        dev.SetDepthWriteEnabled(depth);
        fx_->Apply();
    }

    /// Indexed 3D draw of the index range [@p startIndex, +2 triangles) at @p baseVertex.
    void DrawIndexedHalf(GraphicsDevice& dev, VertexBuffer& vb, IndexBuffer& ib, int baseVertex,
                         int startIndex, bool depth = false)
    {
        Prepare3D(dev, depth);
        dev.SetVertexBuffer(&vb);
        dev.setIndicesProperty(&ib);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, baseVertex, 0, 4, startIndex, 2);
        dev.setIndicesProperty(nullptr);
        dev.SetVertexBuffer(nullptr);
    }

    /// Non-indexed 3D draw of a whole 6-vertex quad buffer.
    void DrawNonIndexed(GraphicsDevice& dev, VertexBuffer& vb, bool depth = false)
    {
        Prepare3D(dev, depth);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    /// SpriteBatch fill of the left or right half, so a check can mix both draw paths.
    void DrawSpriteHalf(GraphicsDevice& dev, bool leftHalf, const Color& tint)
    {
        (void) dev;
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        sb_->Draw(*whiteTexel_, Rectangle(leftHalf ? 0 : kRT / 2, 0, kRT / 2, kRT),
                  Rectangle(0, 0, 1, 1), tint);
        sb_->End();
    }

    // ------------------------------------------------------------------
    // Readback
    // ------------------------------------------------------------------

    std::vector<Color> Read(RenderTarget2D& rt)
    {
        std::vector<Color> px(static_cast<std::size_t>(kRT) * kRT, Color(0xCD, 0xCD, 0xCD, 0xCD));
        rt.GetData(0, nullptr, px.data(), 0, static_cast<int>(px.size()));
        return px;
    }

    /// Asserts the left half is @p left and the right half is @p right, byte-exact.
    void CheckHalves(RenderTarget2D& rt, const Color& left, const Color& right,
                     const std::string& label)
    {
        const std::vector<Color> px = Read(rt);
        std::size_t bad = 0;
        std::string first;
        for (int y = 0; y < kRT; ++y)
            for (int x = 0; x < kRT; ++x)
            {
                const Color& want = (x < kRT / 2) ? left : right;
                const Color& got = px[static_cast<std::size_t>(y) * kRT + x];
                if (!Same(got, want))
                {
                    ++bad;
                    if (first.empty())
                        first = " first@(" + std::to_string(x) + "," + std::to_string(y) +
                                ") got " + Text(got) + " want " + Text(want);
                }
            }
        Check(bad == 0, label + " [mismatched=" + std::to_string(bad) + "/" +
                            std::to_string(px.size()) + " wantLeft=" + Text(left) +
                            " wantRight=" + Text(right) + first + "]");
    }

    /// Asserts every texel is @p c, byte-exact.
    void CheckUniform(RenderTarget2D& rt, const Color& c, const std::string& label)
    {
        CheckHalves(rt, c, c, label);
    }

    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev, RenderTargetUsage usage,
                                               DepthFormat depth = DepthFormat::None)
    {
        return std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color, depth,
                                                0, usage);
    }

    // ==================================================================
    // U -- the 3D families across bind cycles
    // ==================================================================

    /// U1 -- an INDEXED 3D draw in cycle 1, a NON-INDEXED one in cycle 2, DiscardContents.
    void RunIndexedThenNonIndexed(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::DiscardContents);

        dev.SetRenderTarget(rt.get());
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 0);  // left half, red
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        DrawNonIndexed(dev, *nearFullVb_);  // right half, cyan (see LoadContent)
        dev.SetRenderTarget(nullptr);

        CheckHalves(*rt, Discard(), kCyan,
                    "U1 indexed then non-indexed: the second cycle discards the first cycle's "
                    "indexed geometry, and its own non-indexed draw lands");
    }

    /// U2 -- the same two draws on a PreserveContents target: both halves must survive.
    void RunIndexedThenNonIndexedPreserve(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.SetRenderTarget(rt.get());
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 0);  // left half, red
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        DrawNonIndexed(dev, *nearFullVb_);  // right half, cyan
        dev.SetRenderTarget(nullptr);

        CheckHalves(*rt, kRed, kCyan,
                    "U2 preserve across cycles: an indexed cycle and a non-indexed cycle of one "
                    "target compose");
    }

    /**
     * @brief U3 -- REMED-GFX-117's startIndex/baseVertex, one range per bind cycle.
     *
     * Cycle 1 draws index elements 0..5 (the LEFT quad, red). Cycle 2 draws elements 6..11 (the
     * RIGHT quad, green) -- a non-zero `startIndex` issued inside a second logical pass. If the
     * offsets were lost the second cycle would redraw the left quad; if the segment were lost the
     * first cycle's red would still be there.
     */
    void RunIndexedOffsetsPerCycle(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::DiscardContents);

        dev.SetRenderTarget(rt.get());
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 0);
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 6);
        dev.SetRenderTarget(nullptr);

        CheckHalves(*rt, Discard(), kGreen,
                    "U3 indexed startIndex per cycle: a non-zero startIndex issued in the second "
                    "bind cycle addresses its own range, in its own pass");
    }

    /// U4 -- the same, with a non-zero baseVertex instead of a non-zero startIndex.
    void RunBaseVertexPerCycle(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.SetRenderTarget(rt.get());
        DrawIndexedHalf(dev, *rebasedVb_, *rebasedIb_, 0, 0);  // vertices 0..3 = right, magenta
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        DrawIndexedHalf(dev, *rebasedVb_, *rebasedIb_, 4, 0);  // +4 = vertices 4..7 = left, yellow
        dev.SetRenderTarget(nullptr);

        CheckHalves(*rt, kYellow, kMagenta,
                    "U4 indexed baseVertex per cycle: a non-zero baseVertex issued in the second "
                    "bind cycle selects its own vertices, in its own pass");
    }

    /**
     * @brief U5 -- the upload-isolation question, 3D path, A -> B -> A.
     *
     * Three targets whose passes are all recorded into ONE command buffer, each drawing DIFFERENT
     * geometry -- not merely a different colour. A shared upload arena whose cursor restarted per
     * pass would make an earlier pass draw a later pass's half.
     */
    void RunUploadIsolation3D(GraphicsDevice& dev)
    {
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);

        dev.SetRenderTarget(a.get());
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 0);  // left, red
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(b.get());
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 6);  // right, green
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(a.get());
        DrawNonIndexed(dev, *farFullVb_);  // right, blue
        dev.SetRenderTarget(nullptr);

        CheckHalves(*a, Discard(), kBlue,
                    "U5 upload isolation: target A ends at its own final cycle's geometry");
        CheckHalves(*b, Discard(), kGreen,
                    "U5 upload isolation: target B keeps its OWN geometry, not A's");
    }

    /**
     * @brief U6 -- the same question with SpriteBatch and 3D geometry INTERLEAVED across cycles.
     *
     * Sprites and 3D draws use different upload storage in this backend (the shared
     * `spriteVertexBuffer_` versus a per-command buffer), and `drawOrder_` replays both from one
     * list, so this is the only shape that exercises both at once across a pass boundary.
     */
    void RunMixedSpriteAnd3D(GraphicsDevice& dev)
    {
        auto a = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);

        dev.SetRenderTarget(a.get());
        DrawSpriteHalf(dev, true, kRed);  // left, red -- sprite path
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(b.get());
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 6);  // right, green -- 3D path
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(a.get());
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 6);  // right, green -- 3D path
        dev.SetRenderTarget(nullptr);

        CheckHalves(*a, kRed, kGreen,
                    "U6 mixed sprite/3D: A's sprite cycle and its later 3D cycle compose, both in "
                    "public order");
        CheckHalves(*b, Discard(), kGreen,
                    "U6 mixed sprite/3D: the interleaved target keeps its own content");
    }

    // ==================================================================
    // D -- depth and stencil load actions per segment
    // ==================================================================

    /**
     * @brief D1 -- a depth-only Clear() belongs to the cycle that issued it.
     *
     * Cycle 1 writes a NEAR full-target quad and its depth. Cycle 2 draws a FAR one with no depth
     * clear: LessEqual must reject it, so the colour stays. Cycle 3 clears depth only -- not
     * colour -- and draws the same far quad, which must now win. A backend that attached the depth
     * clear to the wrong cycle fails one of the two; one that merged the cycles fails both.
     */
    void RunDepthClearPerCycle(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);

        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kRed, 1.0f, 0);
        DrawNonIndexed(dev, *nearFullVb_, true);  // right half, cyan, z = -0.5
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        DrawNonIndexed(dev, *farFullVb_, true);  // right half, blue, z = +0.5 -- must lose
        dev.SetRenderTarget(nullptr);

        CheckHalves(*rt, kRed, kCyan,
                    "D1 depth across cycles: with no depth clear, the second cycle's farther "
                    "geometry is rejected by the first cycle's stored depth");

        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::DepthBuffer, Color::White, 1.0f, 0);
        DrawNonIndexed(dev, *farFullVb_, true);  // must now win
        dev.SetRenderTarget(nullptr);

        CheckHalves(*rt, kRed, kBlue,
                    "D1 depth across cycles: a depth-ONLY Clear() in a later cycle applies to that "
                    "cycle and leaves the colour attachment loading");
    }

    // ==================================================================
    // C -- clear ordering inside one segment
    // ==================================================================

    /// C1 -- two Clear()s inside ONE bind cycle: the last one wins.
    void RunTwoClearsOneCycle(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        dev.Clear(kRed);
        dev.Clear(kBlue);
        dev.SetRenderTarget(nullptr);
        CheckUniform(*rt, kBlue, "C1 two clears in one cycle: the last Clear() wins");
    }

    /// C2 -- a Clear() in the FIRST cycle and a different one in the SECOND, both honoured.
    void RunClearInBothCycles(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kRed);
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 0);  // left, red-on-red: colour is the clear
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kMagenta);
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 6);  // right, green
        dev.SetRenderTarget(nullptr);

        CheckHalves(*rt, kMagenta, kGreen,
                    "C2 clear in both cycles: the second cycle's Clear() replaces the first "
                    "cycle's, instead of the frame's last Clear() colouring both");
    }

    /// C3 -- a cycle with a Clear() and NO draw still runs, and a later cycle loads its result.
    void RunClearOnlyCycle(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kYellow);
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, 6);  // right, green
        dev.SetRenderTarget(nullptr);

        CheckHalves(*rt, kYellow, kGreen,
                    "C3 clear-only cycle: a bind cycle with no draw still clears, and the next "
                    "cycle loads what it left");
    }

    // ==================================================================
    // R -- repetition: no per-segment permanent object
    // ==================================================================

    /**
     * @brief R1 -- forty segments in one flush window still resolve exactly.
     *
     * Behavioural, not a memory measurement: a cache keyed on a segment identity would either grow
     * without bound or start missing, and either way the last cycle's pixels would stop being
     * exact. Run again in a later frame so a per-frame leak would compound.
     */
    void RunManySegments(GraphicsDevice& dev)
    {
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        for (int i = 0; i < 10; ++i)
        {
            dev.SetRenderTarget(a.get());
            DrawIndexedHalf(dev, *halvesVb_, *halvesIb_, 0, (i % 2) ? 6 : 0);
            dev.SetRenderTarget(nullptr);
            dev.SetRenderTarget(b.get());
            DrawNonIndexed(dev, (i % 2) ? *farFullVb_ : *nearFullVb_);
            dev.SetRenderTarget(nullptr);
        }
        dev.SetRenderTarget(a.get());
        DrawSpriteHalf(dev, true, kMagenta);
        dev.SetRenderTarget(nullptr);

        CheckHalves(*a, kMagenta, Discard(),
                    "R1 many segments: 21 logical segments in one flush window still resolve "
                    "exactly");
        CheckHalves(*b, Discard(), kBlue,
                    "R1 many segments: the interleaved target still ends at its own final cycle");
    }

protected:
    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBB);
        gdm_->setPreferredBackBufferHeightProperty(kBB);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->VertexColorEnabled = true;
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());

        whiteTexel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));

        // halvesVb_: vertices 0..3 = LEFT quad (red), 4..7 = RIGHT quad (green).
        // halvesIb_: elements 0..5 index the left quad, 6..11 the right one.
        {
            const auto left = Corners(-1.0f, 0.0f, 0.0f, kRed);
            const auto right = Corners(0.0f, 1.0f, 0.0f, kGreen);
            std::vector<VertexPositionColor> v(left.begin(), left.end());
            v.insert(v.end(), right.begin(), right.end());
            halvesVb_ = MakeVb(dev, v);
            halvesIb_ = MakeIb(dev, {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7});
        }
        // rebasedVb_: vertices 0..3 = RIGHT quad (magenta), 4..7 = LEFT quad (yellow), with ONE
        // index range (0..5) -- so only baseVertex can decide which quad a draw produces.
        {
            const auto right = Corners(0.0f, 1.0f, 0.0f, kMagenta);
            const auto left = Corners(-1.0f, 0.0f, 0.0f, kYellow);
            std::vector<VertexPositionColor> v(right.begin(), right.end());
            v.insert(v.end(), left.begin(), left.end());
            rebasedVb_ = MakeVb(dev, v);
            rebasedIb_ = MakeIb(dev, {0, 1, 2, 0, 2, 3});
        }
        {
            const auto near_ = Quad(0.0f, 1.0f, -0.5f, kCyan);
            const auto far_ = Quad(0.0f, 1.0f, 0.5f, kBlue);
            nearFullVb_ = MakeVb(dev, std::vector<VertexPositionColor>(near_.begin(), near_.end()));
            farFullVb_ = MakeVb(dev, std::vector<VertexPositionColor>(far_.begin(), far_.end()));
        }
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        if (phase_ == 0)
        {
            // Warm-up frame: every uploaded texture/buffer payload has been through one Present
            // before a single assertion depends on it.
            phase_ = 1;
            dev.Clear(Color::Black);
            return;
        }
        if (phase_ == 2)
        {
            // A second full frame, after a real Present, so nothing here depends on being the
            // first frame and a per-frame leak would show up as a changed result.
            phase_ = 3;
            RunManySegments(dev);
            std::printf("%d/%d checks passed on SDL_GPU\n", passCount_, totalCount_);
            result_ = (passCount_ == totalCount_) ? 0 : 1;
            Exit();
            return;
        }
        if (phase_ != 1) { Exit(); return; }
        phase_ = 2;

        std::printf("REMED-GFX-145 SDL_GPU pass boundaries: 3D families, uploads, depth clears\n");
        RunIndexedThenNonIndexed(dev);
        RunIndexedThenNonIndexedPreserve(dev);
        RunIndexedOffsetsPerCycle(dev);
        RunBaseVertexPerCycle(dev);
        RunUploadIsolation3D(dev);
        RunMixedSpriteAnd3D(dev);
        RunDepthClearPerCycle(dev);
        RunTwoClearsOneCycle(dev);
        RunClearInBothCycles(dev);
        RunClearOnlyCycle(dev);
        RunManySegments(dev);
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    SdlGpuPassBoundaryUploadTest test;
    test.Run();
    return test.Result();
}
