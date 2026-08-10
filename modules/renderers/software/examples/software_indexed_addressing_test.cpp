// SPDX-License-Identifier: MS-PL
// REMED-GFX-110: the Software (CPU-raster) renderer's indexed draw paths must honor the exact
// public indexed-draw contract reconciled by REMED-GFX-106 and already verified on the GPU
// renderers by REMED-GFX-104/105/107/108/109/112:
//
//   * `startIndex` selects the first consumed index ELEMENT of the bound index buffer. It is an
//     element offset -- never a byte offset, a vertex offset, or a source-array offset.
//   * `baseVertex` is added to every decoded index exactly once before the vertex fetch.
//   * `primitiveCount` determines the exact topology-derived consumed index count
//     (3 * primitiveCount for the only indexed topology Software v1 supports, TriangleList).
//   * The declared index width decides 16-bit or 32-bit decoding and never changes any of the
//     three values above.
//   * `minVertexIndex` / `numVertices` are validation/range hints. They never add to a decoded
//     index, never replace `startIndex`, and never trim the vertices an index legitimately reaches.
//
// Pre-fix Software signature (this test FAILS pre-fix): both indexed raster loops
// (DrawIndexedColoredPrimitives and DrawIndexedPrimitivesEx) read `readIndex(i * 3 + k)` -- always
// from element zero -- and fetched `vbBase + index * stride` with no base addend, so `startIndex`
// and a positive `baseVertex` were silently discarded and every draw rendered the buffer prefix.
// Their available-index guard (`primitiveCount * 3 > ib.GetIndexCount()`) likewise omitted
// `startIndex`, and neither path validated the decoded vertex address at all, so an index outside
// the bound vertex buffer formed an out-of-range pointer into the CPU vertex storage.
//
// All geometry uses an identity World*View*Projection, so clip.W == 1 and NDC == the raw vertex
// position. Four separated "slots" across the framebuffer carry distinctive full-intensity colors,
// so any extra, missing, or wrongly addressed triangle is unambiguous: a wrong range lights a
// wrong slot, and a wrong base lights a differently coloured slot.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 128;
    constexpr int kBBH = 64;

    /// Four separated horizontal slots. A triangle in slot i occupies NDC x in
    /// [kSlotX[i] - 0.18, kSlotX[i] + 0.18], so no two slots can overlap.
    constexpr std::array<float, 4> kSlotX{-0.75f, -0.25f, 0.25f, 0.75f};

    /// Vertex-buffer A colours: slot i is built from vertices 3i, 3i+1, 3i+2. Returned from a
    /// function (not a file-scope object) because Color's own static constants live in another
    /// translation unit and are not guaranteed to be initialized before this one.
    std::array<Color, 4> ColorsA()
    {
        return {Color::Lime, Color::Yellow, Color::Red, Color::Blue};
    }
    /// Vertex-buffer B colours -- same positions, different colours, so an A->B->A leak is visible.
    std::array<Color, 4> ColorsB()
    {
        return {Color::White, Color::Magenta, Color::Cyan, Color::Orange};
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() &&
               a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() &&
               a.getAProperty() == b.getAProperty();
    }

    /// Twelve vertices = four slot triangles, in vertex-buffer order.
    std::vector<VertexPositionColor> SlotVertices(const std::array<Color, 4>& colors)
    {
        std::vector<VertexPositionColor> v;
        v.reserve(12);
        for (std::size_t slot = 0; slot < 4; ++slot)
        {
            const float x = kSlotX[slot];
            v.emplace_back(Vector3(x - 0.18f, -0.45f, 0.5f), colors[slot]);
            v.emplace_back(Vector3(x + 0.18f, -0.45f, 0.5f), colors[slot]);
            v.emplace_back(Vector3(x, 0.45f, 0.5f), colors[slot]);
        }
        return v;
    }

    /// Identity 16-bit index buffer contents: element i decodes to vertex i.
    std::array<std::uint16_t, 12> IdentityIndices16()
    {
        std::array<std::uint16_t, 12> indices{};
        for (std::uint16_t i = 0; i < 12; ++i) indices[i] = i;
        return indices;
    }

    std::array<std::uint32_t, 12> IdentityIndices32()
    {
        std::array<std::uint32_t, 12> indices{};
        for (std::uint32_t i = 0; i < 12; ++i) indices[i] = i;
        return indices;
    }
}

class SoftwareIndexedAddressingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    static void ResetState(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
    }

    std::vector<Color> ReadWhole(GraphicsDevice& dev, int w, int h)
    {
        std::vector<Color> pix(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
                               Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, w, h);
        dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    /// The pixel inside slot `slot`'s triangle: NDC (kSlotX[slot], -0.15) is always interior
    /// (the triangle is 0.12 wide there), so the probe never lands on an edge.
    static Color ProbeSlot(const std::vector<Color>& pix, int w, int h, std::size_t slot)
    {
        const int x = static_cast<int>((kSlotX[slot] * 0.5f + 0.5f) * static_cast<float>(w - 1));
        const int y = static_cast<int>((0.15f * 0.5f + 0.5f) * static_cast<float>(h - 1));
        return pix[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                   static_cast<std::size_t>(x)];
    }

    /// Asserts the exact RGBA of all four slots at once. `expected[i]` is the colour slot i must
    /// show; Color::Black means "no geometry may reach this slot".
    void CheckScene(GraphicsDevice& dev, int w, int h,
                    const std::array<Color, 4>& expected, const std::string& label)
    {
        const std::vector<Color> pix = ReadWhole(dev, w, h);
        bool ok = true;
        std::string detail;
        for (std::size_t slot = 0; slot < 4; ++slot)
        {
            const Color actual = ProbeSlot(pix, w, h, slot);
            if (!Same(actual, expected[slot])) ok = false;
            detail += " s" + std::to_string(slot) + "=" + ColorText(actual) +
                      (Same(actual, expected[slot]) ? "" : "!=" + ColorText(expected[slot]));
        }
        check(ok, label + ":" + detail);
    }

    static std::array<Color, 4> Lit(const std::array<Color, 4>& palette,
                                    std::initializer_list<int> slots)
    {
        std::array<Color, 4> expected{Color::Black, Color::Black, Color::Black, Color::Black};
        for (int slot : slots) expected[static_cast<std::size_t>(slot)] = palette[static_cast<std::size_t>(slot)];
        return expected;
    }

    static void DrawRange(GraphicsDevice& dev, BasicEffect& fx,
                          VertexBuffer& vb, IndexBuffer& ib,
                          int baseVertex, int minVertexIndex, int numVertices,
                          int startIndex, int primitiveCount)
    {
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.SetIndexBuffer(&ib);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, baseVertex, minVertexIndex,
                                  numVertices, startIndex, primitiveCount);
        dev.SetVertexBuffer(nullptr);
        dev.SetIndexBuffer(nullptr);
    }

    template <typename ExceptionT, typename Fn>
    bool Throws(Fn&& fn)
    {
        try { fn(); }
        catch (const ExceptionT&) { return true; }
        catch (...) { return false; }
        return false;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        ResetState(dev);

        const std::array<Color, 4> colorsA = ColorsA();
        const std::array<Color, 4> colorsB = ColorsB();
        const std::vector<VertexPositionColor> vertsA = SlotVertices(colorsA);
        const std::vector<VertexPositionColor> vertsB = SlotVertices(colorsB);
        const std::array<std::uint16_t, 12> identity16 = IdentityIndices16();
        const std::array<std::uint32_t, 12> identity32 = IdentityIndices32();

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;

        VertexBuffer vbA(dev, VertexPositionColor::getVertexDeclarationStatic(), 12,
                         BufferUsage::None);
        vbA.SetData(vertsA.data(), 12);
        IndexBuffer ib16(dev, IndexElementSize::SixteenBits, 12, BufferUsage::None);
        ib16.SetData(identity16.data(), 12);
        IndexBuffer ib32(dev, IndexElementSize::ThirtyTwoBits, 12, BufferUsage::None);
        ib32.SetData(identity32.data(), 12);

        // ---- A: startIndex is an index-element offset (first / middle / final valid ranges) ----
        {
            const std::array<int, 4> starts{0, 3, 6, 9};
            for (std::size_t i = 0; i < starts.size(); ++i)
            {
                dev.Clear(Color::Black);
                DrawRange(dev, fx, vbA, ib16, 0, 0, 12, starts[i], 1);
                CheckScene(dev, kBBW, kBBH, Lit(colorsA, {static_cast<int>(i)}),
                           "A" + std::to_string(i + 1) + ": startIndex=" +
                               std::to_string(starts[i]) + " consumes elements " +
                               std::to_string(starts[i]) + ".." + std::to_string(starts[i] + 2) +
                               " only");
            }
        }

        // ---- B: primitiveCount limits the exact consumed range (decoys on both sides) ---------
        {
            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 0, 0, 12, 3, 2);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {1, 2}),
                       "B1: startIndex=3 primitiveCount=2 consumes exactly 6 elements");

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 0, 0, 12, 0, 4);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {0, 1, 2, 3}),
                       "B2: the complete four-primitive range consumes every element");
        }

        // ---- C: positive baseVertex is added to every decoded index exactly once --------------
        {
            const std::array<std::uint16_t, 3> firstTriangle{0, 1, 2};
            IndexBuffer ibFirst(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            ibFirst.SetData(firstTriangle.data(), 3);
            const std::array<int, 4> bases{0, 3, 6, 9};
            for (std::size_t i = 0; i < bases.size(); ++i)
            {
                dev.Clear(Color::Black);
                DrawRange(dev, fx, vbA, ibFirst, bases[i], 0, 12 - bases[i], 0, 1);
                CheckScene(dev, kBBW, kBBH, Lit(colorsA, {static_cast<int>(i)}),
                           "C" + std::to_string(i + 1) + ": baseVertex=" +
                               std::to_string(bases[i]) + " shifts {0,1,2} exactly once");
            }
        }

        // ---- D: startIndex and baseVertex combine without either replacing the other ----------
        {
            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 3, 0, 9, 3, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "D1: startIndex=3 + baseVertex=3 decodes {3,4,5}+3 = {6,7,8}");

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 3, 0, 9, 6, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {3}),
                       "D2: startIndex=6 + baseVertex=3 decodes {6,7,8}+3 = {9,10,11}");

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 6, 0, 6, 0, 2);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2, 3}),
                       "D3: baseVertex=6 + primitiveCount=2 decodes {0..5}+6 = {6..11}");
        }

        // ---- E: minVertexIndex / numVertices stay validation hints, never addressing ----------
        {
            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 0, 6, 3, 6, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "E1: exact hints (minVertexIndex=6,numVertices=3) keep exact addressing");

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 0, 0, 12, 6, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "E2: loose hints (0,12) do not widen the consumed range");

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 0, 0, 1, 9, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {3}),
                       "E3: a narrow valid hint (numVertices=1) does not trim decoded addressing");

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 3, 3, 3, 3, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "E4: hints combined with nonzero startIndex and baseVertex are inert");
        }

        // ---- F: 32-bit index elements decode at four bytes, with the same range semantics -----
        {
            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib32, 0, 0, 12, 3, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {1}),
                       "F1: 32-bit startIndex=3 is an element offset, not a byte offset");

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib32, 6, 0, 6, 0, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "F2: 32-bit decoding plus baseVertex=6");

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib32, 0, 0, 12, 6, 2);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2, 3}),
                       "F3: 32-bit startIndex=6 primitiveCount=2 consumes the final six elements");

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib32, 3, 0, 9, 6, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {3}),
                       "F4: 32-bit startIndex=6 + baseVertex=3 reaches the last vertex exactly");
        }

        // ---- G: dynamic vertex and index buffers behave exactly like the static ones ----------
        {
            DynamicIndexBuffer dynIb(dev, IndexElementSize::SixteenBits, 12, BufferUsage::None);
            dynIb.SetData(identity16.data(), 0, 12, SetDataOptions::Discard);
            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, dynIb, 3, 0, 9, 3, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "G1: DynamicIndexBuffer honors startIndex=3 + baseVertex=3");

            DynamicIndexBuffer dynIb32(dev, IndexElementSize::ThirtyTwoBits, 12, BufferUsage::None);
            dynIb32.SetData(identity32.data(), 0, 12, SetDataOptions::Discard);
            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, dynIb32, 0, 0, 12, 9, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {3}),
                       "G2: 32-bit DynamicIndexBuffer honors the final valid range");

            DynamicVertexBuffer dynVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 12,
                                      BufferUsage::None);
            dynVb.SetData(vertsA.data(), 0, 12, SetDataOptions::Discard);
            fx.Apply();
            dev.Clear(Color::Black);
            dev.SetVertexBuffer(&dynVb);
            dev.SetIndexBuffer(&ib16);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 6, 0, 6, 0, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "G3: DynamicVertexBuffer honors baseVertex=6");
        }

        // ---- H: three independent public calls (A -> B -> A) keep their own parameters --------
        {
            VertexBuffer vbB(dev, VertexPositionColor::getVertexDeclarationStatic(), 12,
                             BufferUsage::None);
            vbB.SetData(vertsB.data(), 12);
            IndexBuffer ibB(dev, IndexElementSize::SixteenBits, 9, BufferUsage::None);
            const std::array<std::uint16_t, 9> indicesB{0, 1, 2, 3, 4, 5, 6, 7, 8};
            ibB.SetData(indicesB.data(), 9);

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 0, 0, 12, 0, 1);          // A: slot 0 from palette A
            DrawRange(dev, fx, vbB, ibB, 1, 0, 11, 2, 2);           // B: slots 1,2 from palette B
            DrawRange(dev, fx, vbA, ib16, 0, 0, 12, 9, 1);          // A: slot 3 from palette A

            const std::array<Color, 4> expected{
                colorsA[0], colorsB[1], colorsB[2], colorsA[3]};
            CheckScene(dev, kBBW, kBBH, expected,
                       "H1: A->B->A keeps per-call range, base, count, and buffer identity");
        }

        // ---- O: Software executes immediately -- later buffer writes cannot rewrite a done draw -
        {
            DynamicIndexBuffer mutableIb(dev, IndexElementSize::SixteenBits, 12,
                                         BufferUsage::None);
            mutableIb.SetData(identity16.data(), 0, 12, SetDataOptions::Discard);

            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, mutableIb, 0, 0, 12, 0, 1);      // slot 0, from {0,1,2}

            // Rewrite the same handle so its first three elements now select slot 3, then draw a
            // different range from the new contents. The completed draw above must not change.
            std::array<std::uint16_t, 12> rewritten = identity16;
            rewritten[0] = 9; rewritten[1] = 10; rewritten[2] = 11;
            mutableIb.SetData(rewritten.data(), 0, 12, SetDataOptions::Discard);
            DrawRange(dev, fx, vbA, mutableIb, 0, 0, 12, 3, 1);      // slot 1, from {3,4,5}

            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {0, 1}),
                       "O1: an index update after a completed draw does not rewrite its pixels");

            // Two independently created buffers holding the same bytes must not alias.
            IndexBuffer twinA(dev, IndexElementSize::SixteenBits, 12, BufferUsage::None);
            IndexBuffer twinB(dev, IndexElementSize::SixteenBits, 12, BufferUsage::None);
            twinA.SetData(identity16.data(), 12);
            twinB.SetData(identity16.data(), 12);
            twinB.SetData(rewritten.data(), 12);
            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, twinA, 0, 0, 12, 0, 1);          // twinA still selects slot 0
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {0}),
                       "O2: two independent index buffers with equal bytes do not alias");

            // A scoped buffer's pixels survive its destruction (immediate CPU execution).
            dev.Clear(Color::Black);
            {
                IndexBuffer scoped(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
                const std::array<std::uint16_t, 3> scopedIndices{6, 7, 8};
                scoped.SetData(scopedIndices.data(), 3);
                DrawRange(dev, fx, vbA, scoped, 0, 0, 12, 0, 1);
            }
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "O3: pixels survive the destruction of the buffers that produced them");
        }

        // ---- I: DrawUserIndexedPrimitives remains the zero-offset oracle ----------------------
        {
            const std::array<std::uint16_t, 6> userIndices{9, 9, 9, 0, 1, 2};
            fx.Apply();
            dev.Clear(Color::Black);
            dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                          static_cast<const void*>(vertsA.data()), 3, 3,
                                          static_cast<const void*>(userIndices.data()), 3, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {1}),
                       "I1: raw DrawUserIndexedPrimitives (DrawIndexedColoredPrimitives) copies"
                       " the selected source slices");

            fx.Apply();
            dev.Clear(Color::Black);
            dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                          vertsA.data(), 6, 3,
                                          userIndices.data(), 3, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "I2: typed DrawUserIndexedPrimitives copies the selected source slices");
        }

        // ---- J: exact addressing on a bound RenderTarget2D, then backbuffer isolation ---------
        {
            constexpr int rtW = 64;
            constexpr int rtH = 48;
            RenderTarget2D target(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::Depth24,
                                  0, RenderTargetUsage::PreserveContents);
            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 0, 0, 12, 0, 1);   // backbuffer keeps slot 0 only

            dev.SetRenderTarget(&target);
            dev.setViewportProperty(Viewport(0, 0, rtW, rtH));
            ResetState(dev);
            dev.Clear(Color::Black);
            DrawRange(dev, fx, vbA, ib16, 3, 0, 9, 3, 1);
            CheckScene(dev, rtW, rtH, Lit(colorsA, {2}),
                       "J1: RenderTarget2D honors startIndex=3 + baseVertex=3");

            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.setViewportProperty(Viewport(0, 0, kBBW, kBBH));
            ResetState(dev);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {0}),
                       "J2: the backbuffer keeps its own earlier indexed range");
        }

        // ---- K: unsupported indexed topologies stay explicit capability rejections ------------
        {
            dev.Clear(Color::Black);
            fx.Apply();
            dev.SetVertexBuffer(&vbA);
            dev.SetIndexBuffer(&ib16);
            struct TopologyCase { PrimitiveType primitive; int primitiveCount; const char* name; };
            const std::array<TopologyCase, 4> cases{{
                {PrimitiveType::TriangleStrip, 10, "TriangleStrip"},
                {PrimitiveType::LineList, 6, "LineList"},
                {PrimitiveType::LineStrip, 11, "LineStrip"},
                {PrimitiveType::PointListEXT, 12, "PointListEXT"},
            }};
            bool allRejected = true;
            for (const auto& topology : cases)
            {
                allRejected &= Throws<std::runtime_error>([&] {
                    dev.DrawIndexedPrimitives(topology.primitive, 0, 0, 12, 0,
                                              topology.primitiveCount);
                });
            }
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);
            check(allRejected,
                  "K1: TriangleStrip/LineList/LineStrip/PointListEXT are rejected, not approximated");
            CheckScene(dev, kBBW, kBBH,
                       {Color::Black, Color::Black, Color::Black, Color::Black},
                       "K2: a rejected topology rasterizes nothing");
        }

        // ---- L: invalid public ranges are rejected deterministically before any storage read --
        {
            fx.Apply();
            dev.SetVertexBuffer(&vbA);
            dev.SetIndexBuffer(&ib16);
            struct RangeCase
            {
                int baseVertex, minVertexIndex, numVertices, startIndex, primitiveCount;
                const char* name;
            };
            const std::array<RangeCase, 12> cases{{
                {0, 0, 12, -1, 1, "negative startIndex"},
                {-1, 0, 12, 0, 1, "negative baseVertex"},
                {0, -1, 12, 0, 1, "negative minVertexIndex"},
                {0, 0, -1, 0, 1, "negative numVertices"},
                {0, 0, 0, 0, 1, "zero numVertices"},
                {0, 0, 12, 0, 0, "zero primitiveCount"},
                {0, 0, 12, 0, -1, "negative primitiveCount"},
                {0, 0, 12, 13, 1, "startIndex past the index buffer"},
                {0, 0, 12, 10, 1, "startIndex + consumed count past the index buffer"},
                {13, 0, 1, 0, 1, "baseVertex past the vertex buffer"},
                {6, 0, 7, 0, 1, "hint range past the vertex buffer"},
                {0, 0, 12, 0, std::numeric_limits<int>::max(), "overflowing primitiveCount"},
            }};
            bool allRejected = true;
            std::string failing;
            for (const auto& rangeCase : cases)
            {
                const bool rejected = Throws<System::ArgumentOutOfRangeException>([&] {
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, rangeCase.baseVertex,
                                              rangeCase.minVertexIndex, rangeCase.numVertices,
                                              rangeCase.startIndex, rangeCase.primitiveCount);
                });
                if (!rejected) { allRejected = false; failing += std::string(" ") + rangeCase.name; }
            }
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);
            check(allRejected,
                  "L1: every invalid indexed range throws ArgumentOutOfRangeException;"
                  " unrejected:" + (failing.empty() ? std::string(" <none>") : failing));
        }

        // ---- M: a decoded vertex address outside the bound vertex buffer is rejected ----------
        {
            VertexBuffer smallVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6,
                                 BufferUsage::None);
            smallVb.SetData(vertsA.data(), 6);

            // Decoded index 11 with a six-vertex buffer: valid public arguments, invalid address.
            const std::array<std::uint16_t, 3> pastEnd{0, 1, 11};
            IndexBuffer ibPastEnd(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            ibPastEnd.SetData(pastEnd.data(), 3);

            // baseVertex pushes an in-range decoded index past the end.
            const std::array<std::uint16_t, 3> nearEnd{3, 4, 5};
            IndexBuffer ibNearEnd(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            ibNearEnd.SetData(nearEnd.data(), 3);

            // A 32-bit value that would wrap to a small index under unsigned arithmetic.
            const std::array<std::uint32_t, 3> wrapping{
                0, 1, std::numeric_limits<std::uint32_t>::max()};
            IndexBuffer ibWrapping(dev, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
            ibWrapping.SetData(wrapping.data(), 3);

            dev.Clear(Color::Black);
            fx.Apply();
            dev.SetVertexBuffer(&smallVb);

            dev.SetIndexBuffer(&ibPastEnd);
            const bool rejectedPastEnd = Throws<System::ArgumentOutOfRangeException>([&] {
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 1);
            });
            dev.SetIndexBuffer(&ibNearEnd);
            const bool rejectedBased = Throws<System::ArgumentOutOfRangeException>([&] {
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
            });
            dev.SetIndexBuffer(&ibWrapping);
            const bool rejectedWrapping = Throws<System::ArgumentOutOfRangeException>([&] {
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 1, 0, 5, 0, 1);
            });
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            check(rejectedPastEnd, "M1: a decoded index past the vertex buffer is rejected");
            check(rejectedBased,
                  "M2: baseVertex pushing a decoded index past the vertex buffer is rejected");
            check(rejectedWrapping,
                  "M3: a 32-bit index near UINT32_MAX is rejected instead of wrapping");
            CheckScene(dev, kBBW, kBBH,
                       {Color::Black, Color::Black, Color::Black, Color::Black},
                       "M4: a rejected indexed draw writes no pixels at all");
        }

        // ---- N: exact addressing survives the established depth/rasterizer contract -----------
        {
            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
            DrawRange(dev, fx, vbA, ib16, 3, 0, 9, 3, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {2}),
                       "N1: exact addressing with depth test/write enabled (GFX-030 Default)");

            dev.setDepthStencilStateProperty(DepthStencilState::DepthRead);
            DrawRange(dev, fx, vbA, ib16, 0, 0, 12, 0, 1);
            CheckScene(dev, kBBW, kBBH, Lit(colorsA, {0, 2}),
                       "N2: DepthRead still fetches the exact selected range");
            ResetState(dev);
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SoftwareIndexedAddressingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareIndexedAddressingTest game;
    game.Run();
    return game.getResult();
}
