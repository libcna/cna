// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-160 -- the XNA/FNA front-face winding contract, measured against an explicit oracle.
//
// THE AUTHORITATIVE CONTRACT, taken from the FNA source and not from any renderer's behaviour:
//
//   FNA SpriteBatch.cs lays a sprite's four corners out as
//       CornerOffsetX = { 0, 1, 0, 1 }      CornerOffsetY = { 0, 0, 1, 1 }
//   i.e. corner 0 = TOP-LEFT, 1 = TOP-RIGHT, 2 = BOTTOM-LEFT, 3 = BOTTOM-RIGHT
//   (GenerateVertexInfo() builds Position0..3 from exactly that (x,y) progression), and
//   GenerateIndexArray() emits  j, j+1, j+2,  j+3, j+2, j+1  so the two triangles are
//       triangle 1 = TL -> TR -> BL          triangle 2 = BR -> BL -> TR
//   In XNA screen space (x right, y DOWN) both of those are CLOCKWISE as displayed.
//
//   SpriteBatch.Begin() defaults its rasterizer state to `RasterizerState.CullCounterClockwise`
//   (SpriteBatch.cs: `rasterizerState ?? RasterizerState.CullCounterClockwise`), assigns it to the
//   device, and sprites are visible. A CLOCKWISE-as-displayed triangle therefore SURVIVES
//   CullCounterClockwise, which is only possible if:
//
//       *** clockwise-as-displayed is the FRONT face, and each enum names the face it CULLS ***
//
//   Two independent FNA3D drivers agree, by different routes:
//     - OpenGL: XNAToGL_FrontFace = { -, GL_CW, GL_CCW } with glCullFace(GL_BACK). GL window space
//       is y-UP, so GL's "CCW" is displayed-CW; CullCounterClockwiseFace -> glFrontFace(GL_CCW)
//       makes displayed-CW the front face and culls the displayed-CCW back faces.
//     - D3D11: FrontCounterClockwise = 1 AND the cull enum swapped
//       ({ NONE, CULL_BACK, CULL_FRONT }) -- a double inversion that nets out to the same thing:
//       CullCounterClockwiseFace culls the displayed-CCW faces.
//   CNA's own public documentation says the same (RasterizerState.hpp: "Preset: cull
//   counter-clockwise-wound faces (XNA default)", CullMode.hpp: "Cull faces with counter-clockwise
//   vertex order").
//
// THE ORACLE. Two triangles in DISJOINT quadrants, so one readback classifies both independently:
//
//     +--------+--------+        A = top-left quadrant, RED
//     |  A /|  |        |        B = bottom-right quadrant, GREEN
//     |   / |  |        |        The top-right and bottom-left quadrants are NEVER covered by
//     |  /__|  |        |        either triangle and are asserted clear in every single leg --
//     +--------+--------+        that is what makes a projection/viewport MIRROR (which would move
//     |        |  |\     |       a triangle into one of them) impossible to mistake for a cull.
//     |        |  | \    |
//     |        |  |__\ B |       A's clockwise order is (TL, TR, BL) -- FNA's sprite triangle 1.
//     +--------+--------+        B's clockwise order is (BR, BL, TR) -- FNA's sprite triangle 2.
//
// Reversing a triangle's three indices gives the identical coverage with the opposite winding, so
// "was it culled" is never confounded with "did it move" or "did it shrink". Drawing A clockwise
// and B counter-clockwise in the SAME pass makes each cull mode's expected answer complementary:
//
//     CullNone                  -> A present, B present
//     CullClockwiseFace         -> A CULLED,  B present
//     CullCounterClockwiseFace  -> A present, B CULLED      (this is XNA's default)
//
// which separates every failure this ticket has to tell apart: correct winding, reversed winding
// (the two rows swap), culling not applied at all (both present under both modes), everything
// culled (neither present), and a mirror (a never-covered quadrant lights up).
//
// This file asserts the XNA contract UNCONDITIONALLY on every rasterizing renderer. There is no
// per-renderer "declared defect" constant for the winding itself: a renderer that inverts it fails
// here, which is the point.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /** @brief Whether this renderer rasterizes at all (HEADLESS does not; readbacks must reject). */
    /** @brief Whether the public backbuffer readback works here. */
    /** @brief Whether stock 3D geometry rasterizes here at all. */
#if defined(CNA_RENDERER_HEADLESS)
    constexpr bool kRasterizes = false;
    constexpr bool kReadsBackbuffer = false;
    constexpr bool kDraws3D = false;
    constexpr const char* kRendererName = "HEADLESS";
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "SOFTWARE";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "EASYGL";
#elif defined(CNA_RENDERER_BGFX)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "BGFX";
#elif defined(CNA_RENDERER_VULKAN)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "VULKAN";
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "WEBGPU";
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = false;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "SDL_GPU";
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "DIRECTX9";
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "DIRECTX11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "DIRECTX12";
#elif defined(CNA_RENDERER_LLGL)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "LLGL";
#else
#error "REMED-GFX-160: this renderer has no declared winding contract."
#endif

    /**
     * @brief Whether the FIRST backbuffer readback of the process returns unwritten memory.
     *
     * REMED-GFX-161, measured by REMED-GFX-157 and still open: on WebGPU the very first
     * GetBackBufferData of a run leaves part of the caller's buffer untouched while reporting
     * success. That is a readback warm-up defect, not a winding one, so the first backbuffer leg
     * performs one discarded read and this file measures its own subject. Remove when 161 lands.
     */
    constexpr bool kFirstBackbufferReadIsWarmUp =
#if defined(CNA_RENDERER_WEBGPU)
        true;
#else
        false;
#endif

    /**
     * @brief Whether the BACKBUFFER readback rejects on a renderer that rasterizes nothing.
     *
     * REMED-GFX-127/130 brought the texture and render-target readbacks under a contract that
     * REJECTS rather than handing back scratch as a rendered frame; the backbuffer readback was
     * never brought under it. That gap is REMED-GFX-162, already open and out of this task's scope,
     * so it is declared here rather than absorbed into a winding assertion.
     */
    constexpr bool kBackbufferReadRejectsWhenNonRasterizing =
#if defined(CNA_RENDERER_HEADLESS)
        false;   // REMED-GFX-162, open
#else
        true;
#endif

    constexpr int kW = 64;   ///< Target width; the quadrant probes assume a multiple of 8.
    constexpr int kH = 64;   ///< Target height.

    /// Fully saturated, so sRGB encoding is the identity on every asserted texel.
    const Color kClear(  0,   0,   0, 255);
    const Color kRedC (255,   0,   0, 255);
    const Color kGrnC (  0, 255,   0, 255);
    const Color kBluC (  0,   0, 255, 255);
    const Color kWhtC (255, 255, 255, 255);

    /** @brief Formats a colour as R,G,B,A for diagnostics. */
    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /** @brief Exact byte equality on all four channels. */
    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    /** @brief Which quadrant a triangle occupies. */
    enum class Quad
    {
        TopLeft,      ///< Triangle A.
        BottomRight   ///< Triangle B.
    };

    /** @brief A triangle's vertex order, named for how it reads ON SCREEN. */
    enum class Wind
    {
        Cw,   ///< Clockwise as displayed: FNA's own sprite winding, XNA's FRONT face.
        Ccw   ///< Counter-clockwise as displayed: XNA's BACK face.
    };

    /** @brief Which GraphicsDevice entry point a triangle is issued through. */
    enum class Entry
    {
        UserPrimitives,        ///< DrawUserPrimitives.
        UserIndexed16,         ///< DrawUserIndexedPrimitives, 16-bit indices.
        UserIndexed32,         ///< DrawUserIndexedPrimitives, 32-bit indices.
        BufferPrimitives,      ///< SetVertexBuffer + DrawPrimitives.
        BufferPrimitivesRange, ///< The same from a NONZERO vertexStart inside a wider buffer.
        BufferIndexed,         ///< SetVertexBuffer + Indices + DrawIndexedPrimitives.
        BufferIndexedRange,    ///< The same from a NONZERO startIndex and baseVertex.
        Strip                  ///< A 4-vertex TriangleStrip covering the whole quadrant.
    };

    /** @brief Which stock effect draws a triangle. */
    enum class Fx
    {
        VertexColor,   ///< BasicEffect, VertexColorEnabled, no texture.
        BasicTextured, ///< BasicEffect with a 1x1 solid texture.
        AlphaTest,     ///< AlphaTestEffect, CompareFunction::Always so nothing is discarded.
        DualTexture    ///< DualTextureEffect, second texture white so the modulate is a no-op.
    };

    /** @brief What one leg observed in the four probed quadrants. */
    struct Outcome
    {
        bool  aPresent = false;   ///< Triangle A's probe holds A's colour.
        bool  bPresent = false;   ///< Triangle B's probe holds B's colour.
        bool  strayTR  = false;   ///< The never-covered top-right quadrant is not clear.
        bool  strayBL  = false;   ///< The never-covered bottom-left quadrant is not clear.
        bool  unsupported = false;///< The draw itself was rejected (e.g. an unsupported topology).
        bool  unreadable  = false;///< The destination could not be read back.
        std::string what;         ///< The rejection message, when @c unsupported.
        Color atA = kClear, atB = kClear, atTR = kClear, atBL = kClear;

        /** @brief A compact human-readable form for diagnostics. */
        [[nodiscard]] std::string Text() const
        {
            return std::string("A=") + (aPresent ? "present" : "absent") +
                   " B=" + (bPresent ? "present" : "absent") +
                   " [probes A" + ColorText(atA) + " B" + ColorText(atB) +
                   " TR" + ColorText(atTR) + " BL" + ColorText(atBL) + "]";
        }
    };
}

/**
 * @brief REMED-GFX-160's public front-face winding contract.
 */
class FrontFaceWindingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;
    int  skipCount_ = 0;

    std::unique_ptr<SpriteBatch> sb_;
    Texture2D white_;                  ///< 1x1 white; sprites tint it.
    Texture2D redTex_;                 ///< 1x1 red, for the textured effect legs.
    Texture2D grnTex_;                 ///< 1x1 green.

    std::unique_ptr<BasicEffect>       vcolFx_;
    std::unique_ptr<BasicEffect>       texFx_;
    std::unique_ptr<AlphaTestEffect>   alphaFx_;
    std::unique_ptr<DualTextureEffect> dualFx_;

    std::unique_ptr<VertexBuffer> triVb_;     ///< 3 VertexPositionColor, rewritten per draw.
    std::unique_ptr<VertexBuffer> wideVb_;    ///< 9 vertices: a nonzero vertexStart range.
    std::unique_ptr<IndexBuffer>  triIb_;     ///< 3 x 16-bit indices.
    std::unique_ptr<IndexBuffer>  wideIb_;    ///< 9 indices: a nonzero startIndex range.
    std::unique_ptr<VertexBuffer> stripVb_;   ///< 4 VertexPositionColor for the strip leg.

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void skip(const std::string& label)
    {
        std::printf("[SKIP] %s\n", label.c_str());
        std::fflush(stdout);
        ++skipCount_;
    }

    void info(const std::string& text)
    {
        std::printf("[INFO] %s\n", text.c_str());
        std::fflush(stdout);
    }

    // ------------------------------------------------------------------ destination

    /** @brief Where a leg draws. A null target means the backbuffer. */
    struct Dest
    {
        RenderTarget2D* rt = nullptr;
        int w = kW;
        int h = kH;
        const char* name = "backbuffer";
    };

    // ------------------------------------------------------------------ readback

    /** @brief The outcome of a public readback. */
    struct Readback
    {
        bool threwNotSupported = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> pixels;
        int w = 0;
        int h = 0;

        [[nodiscard]] bool ok() const { return !threwNotSupported && !threwSomethingElse; }
        [[nodiscard]] const Color& at(int x, int y) const
        {
            return pixels[static_cast<std::size_t>(y) * w + x];
        }
    };

    /// Reads @p dest back, pre-filled with poison so "wrote nothing" cannot look like "wrote black".
    Readback Read(GraphicsDevice& dev, const Dest& dest)
    {
        Readback r;
        r.w = dest.w;
        r.h = dest.h;
        r.pixels.assign(static_cast<std::size_t>(dest.w) * dest.h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            if (dest.rt) dest.rt->GetData(r.pixels.data(), 0, static_cast<int>(r.pixels.size()));
            else         dev.GetBackBufferData(r.pixels.data(), 0, static_cast<int>(r.pixels.size()));
        }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /// True when @p dest can be asserted on at all here; reports the boundary otherwise.
    bool Readable(const Readback& r, const Dest& dest, const std::string& label)
    {
        if (!kRasterizes)
        {
            if (dest.rt == nullptr && !kBackbufferReadRejectsWhenNonRasterizing)
            {
                skip(label + ": " + kRendererName + " does not yet reject the BACKBUFFER readback "
                     "on a non-rasterizing device (REMED-GFX-162, open) -- declared, not absorbed");
                return false;
            }
            check(r.threwNotSupported,
                  label + ": declared non-rasterizing, every readback must throw "
                          "NotSupportedException");
            return false;
        }
        if (!r.ok())
        {
            skip(label + ": " + dest.name + " oracle unavailable on " + kRendererName + " (" +
                 (r.threwNotSupported ? "NotSupportedException" : r.otherWhat) +
                 ") -- boundary recorded");
            return false;
        }
        return true;
    }

    /**
     * @brief The colour of a 3x3 block centred on (@p cx, @p cy), or poison if it is not uniform.
     *
     * Every probe sits deep inside its quadrant, far from any primitive edge, so a renderer's fill
     * rule cannot influence the answer and the comparison stays byte-exact.
     */
    static Color Probe(const Readback& r, int cx, int cy)
    {
        const Color first = r.at(cx, cy);
        for (int y = cy - 1; y <= cy + 1; ++y)
            for (int x = cx - 1; x <= cx + 1; ++x)
                if (!Same(r.at(x, y), first)) return Color(0xCD, 0xCD, 0xCD, 0xCD);
        return first;
    }

    /**
     * @brief Probes @p r at the image of @p q's centroid under @p world.
     *
     * A triangle's centroid is strictly interior and an affine transform carries centroids to
     * centroids, so this follows geometry that a transform legitimately moves or shrinks without
     * needing a hand-computed probe per case.
     */
    static Color ProbeCentroid(const Readback& r, Quad q, const Matrix& world)
    {
        Vector3 p[3];
        Corners(q, p);
        const Vector3 c((p[0].X + p[1].X + p[2].X) / 3.f,
                        (p[0].Y + p[1].Y + p[2].Y) / 3.f, 0.f);
        const Vector3 t = Vector3::Transform(c, world);
        int px = static_cast<int>((t.X + 1.f) * 0.5f * static_cast<float>(r.w));
        int py = static_cast<int>((1.f - t.Y) * 0.5f * static_cast<float>(r.h));
        px = (px < 1) ? 1 : (px > r.w - 2 ? r.w - 2 : px);
        py = (py < 1) ? 1 : (py > r.h - 2 ? r.h - 2 : py);
        return Probe(r, px, py);
    }

    /// Classifies a readback at the four quadrant probes.
    static Outcome Classify(const Readback& r, const Color& aColour, const Color& bColour)
    {
        Outcome o;
        o.atA  = Probe(r, r.w / 8,     r.h / 8);
        o.atB  = Probe(r, r.w * 7 / 8, r.h * 7 / 8);
        o.atTR = Probe(r, r.w * 7 / 8, r.h / 8);
        o.atBL = Probe(r, r.w / 8,     r.h * 7 / 8);
        o.aPresent = Same(o.atA, aColour);
        o.bPresent = Same(o.atB, bColour);
        o.strayTR  = !Same(o.atTR, kClear);
        o.strayBL  = !Same(o.atBL, kClear);
        return o;
    }

    // ------------------------------------------------------------------ geometry

    /**
     * @brief The three clip-space corners of @p q's triangle, in CLOCKWISE-as-displayed order.
     *
     * Triangle A occupies the top-left quadrant (clip x in [-1,0], y in [0,1]) and its clockwise
     * order (quadrant TL, TR, BL) is exactly FNA's sprite triangle 1. Triangle B occupies the
     * bottom-right quadrant and its clockwise order (quadrant BR, BL, TR) is exactly FNA's sprite
     * triangle 2. The two quadrants are disjoint, and the other two are never covered.
     */
    static void Corners(Quad q, Vector3 out[3])
    {
        if (q == Quad::TopLeft)
        {
            out[0] = Vector3(-1.f,  1.f, 0.f);   // quadrant top-left
            out[1] = Vector3( 0.f,  1.f, 0.f);   // quadrant top-right
            out[2] = Vector3(-1.f,  0.f, 0.f);   // quadrant bottom-left
        }
        else
        {
            out[0] = Vector3( 1.f, -1.f, 0.f);   // quadrant bottom-right
            out[1] = Vector3( 0.f, -1.f, 0.f);   // quadrant bottom-left
            out[2] = Vector3( 1.f,  0.f, 0.f);   // quadrant top-right
        }
    }

    /// Builds @p q's triangle with winding @p w and colour @p c. Ccw reverses the last two corners,
    /// which keeps the coverage identical and flips only the winding.
    static void Tri(Quad q, Wind w, const Color& c, VertexPositionColor* out)
    {
        Vector3 p[3];
        Corners(q, p);
        out[0] = VertexPositionColor(p[0], c);
        out[1] = VertexPositionColor(w == Wind::Cw ? p[1] : p[2], c);
        out[2] = VertexPositionColor(w == Wind::Cw ? p[2] : p[1], c);
    }

    /// The same three corners as VertexPositionTexture, for the textured-effect legs.
    static void TriT(Quad q, Wind w, VertexPositionTexture* out)
    {
        Vector3 p[3];
        Corners(q, p);
        const Vector3 v1 = (w == Wind::Cw) ? p[1] : p[2];
        const Vector3 v2 = (w == Wind::Cw) ? p[2] : p[1];
        out[0] = VertexPositionTexture(p[0], Vector2(0.f, 0.f));
        out[1] = VertexPositionTexture(v1,   Vector2(1.f, 0.f));
        out[2] = VertexPositionTexture(v2,   Vector2(0.f, 1.f));
    }

    /**
     * @brief The four clip-space vertices of @p q's quadrant square as a TriangleStrip.
     *
     * A strip's odd-numbered triangle is emitted with its first two vertices swapped by the
     * hardware precisely so that every triangle of the strip keeps the SAME winding. Laying the
     * square out as (TL, TR, BL, BR) therefore yields two clockwise triangles that tile the whole
     * quadrant; swapping the middle pair yields two counter-clockwise ones. Both halves of the
     * quadrant are probed, so a renderer that gets the odd triangle's winding wrong is caught.
     */
    static void Strip(Quad q, Wind w, const Color& c, VertexPositionColor* out)
    {
        const float x0 = (q == Quad::TopLeft) ? -1.f : 0.f;
        const float x1 = (q == Quad::TopLeft) ?  0.f : 1.f;
        const float y0 = (q == Quad::TopLeft) ?  1.f : 0.f;   // top edge
        const float y1 = (q == Quad::TopLeft) ?  0.f : -1.f;  // bottom edge
        const Vector3 tl(x0, y0, 0.f), tr(x1, y0, 0.f), bl(x0, y1, 0.f), br(x1, y1, 0.f);
        out[0] = VertexPositionColor(tl, c);
        out[1] = VertexPositionColor(w == Wind::Cw ? tr : bl, c);
        out[2] = VertexPositionColor(w == Wind::Cw ? bl : tr, c);
        out[3] = VertexPositionColor(br, c);
    }

    // ------------------------------------------------------------------ state

    /// The state-neutral baseline: no depth, no blending, opaque, no culling.
    static void ResetState(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    /// Opens a bind cycle on @p dest: bind, reset state, clear to opaque black.
    void Begin(GraphicsDevice& dev, const Dest& dest)
    {
        dev.SetRenderTarget(dest.rt);
        ResetState(dev);
        dev.Clear(kClear);
    }

    /// The preset for a raw CullMode ordinal.
    static const RasterizerState& Preset(CullMode m)
    {
        if (m == CullMode::None) return RasterizerState::CullNone;
        if (m == CullMode::CullClockwiseFace) return RasterizerState::CullClockwise;
        return RasterizerState::CullCounterClockwise;
    }

    /** @brief A cull mode's public name, for diagnostics. */
    static const char* CullText(CullMode m)
    {
        if (m == CullMode::None) return "CullNone";
        if (m == CullMode::CullClockwiseFace) return "CullClockwise";
        return "CullCounterClockwise";
    }

    // ------------------------------------------------------------------ effects

    /// Configures @p fx with identity transforms and the colour @p c's texture.
    void ConfigureEffect(Fx fx, const Matrix& world, Texture2D* tex)
    {
        const Matrix id = Matrix::getIdentityProperty();
        switch (fx)
        {
        case Fx::VertexColor:
            vcolFx_->setWorldProperty(world); vcolFx_->setViewProperty(id);
            vcolFx_->setProjectionProperty(id);
            vcolFx_->setTextureEnabledProperty(false);
            vcolFx_->setTextureProperty(nullptr);
            vcolFx_->VertexColorEnabled = true;
            vcolFx_->setLightingEnabledProperty(false);
            break;
        case Fx::BasicTextured:
            texFx_->setWorldProperty(world); texFx_->setViewProperty(id);
            texFx_->setProjectionProperty(id);
            texFx_->setTextureEnabledProperty(true);
            texFx_->setTextureProperty(tex);
            texFx_->VertexColorEnabled = false;
            texFx_->setLightingEnabledProperty(false);
            break;
        case Fx::AlphaTest:
            alphaFx_->setWorldProperty(world); alphaFx_->setViewProperty(id);
            alphaFx_->setProjectionProperty(id);
            alphaFx_->setTextureProperty(tex);
            alphaFx_->setVertexColorEnabledProperty(false);
            alphaFx_->setAlphaFunctionProperty(CompareFunction::Always);
            alphaFx_->setReferenceAlphaProperty(0);
            break;
        case Fx::DualTexture:
            dualFx_->setWorldProperty(world); dualFx_->setViewProperty(id);
            dualFx_->setProjectionProperty(id);
            dualFx_->setTextureProperty(tex);
            dualFx_->setTexture2Property(&white_);
            dualFx_->setVertexColorEnabledProperty(false);
            break;
        }
    }

    /// Applies the configured effect so the following draw has a current one.
    void ApplyEffect(Fx fx)
    {
        switch (fx)
        {
        case Fx::VertexColor:   vcolFx_->Apply();  break;
        case Fx::BasicTextured: texFx_->Apply();   break;
        case Fx::AlphaTest:     alphaFx_->Apply(); break;
        case Fx::DualTexture:   dualFx_->Apply();  break;
        }
    }

    // ------------------------------------------------------------------ draw

    /**
     * @brief Issues @p q's triangle with winding @p w and colour @p c.
     *
     * Every entry point draws the SAME three clip-space corners in the same order, so a difference
     * between two entry points is a difference in that entry point's index/offset handling and
     * nothing else.
     */
    void DrawTri(GraphicsDevice& dev, Quad q, Wind w, const Color& c,
                 Entry entry = Entry::UserPrimitives, Fx fx = Fx::VertexColor,
                 const Matrix& world = Matrix::getIdentityProperty())
    {
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;

        Texture2D* tex = Same(c, kRedC) ? &redTex_ : &grnTex_;

        if (fx != Fx::VertexColor)
        {
            VertexPositionTexture t[3];
            TriT(q, w, t);
            ConfigureEffect(fx, world, tex);
            ApplyEffect(fx);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, t, 0, 1);
            return;
        }

        ConfigureEffect(Fx::VertexColor, world, nullptr);

        if (entry == Entry::Strip)
        {
            VertexPositionColor s[4];
            Strip(q, w, c, s);
            ApplyEffect(Fx::VertexColor);
            dev.DrawUserPrimitives(PrimitiveType::TriangleStrip, s, 0, 2);
            return;
        }

        VertexPositionColor v[3];
        Tri(q, w, c, v);

        switch (entry)
        {
        case Entry::UserPrimitives:
            ApplyEffect(Fx::VertexColor);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 1);
            break;
        case Entry::UserIndexed16:
        {
            const std::uint16_t idx[3] = { 0, 1, 2 };
            ApplyEffect(Fx::VertexColor);
            dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, v, 0, 3, idx, 0, 1);
            break;
        }
        case Entry::UserIndexed32:
        {
            const std::uint32_t idx[3] = { 0, 1, 2 };
            ApplyEffect(Fx::VertexColor);
            dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, v, 0, 3, idx, 0, 1);
            break;
        }
        case Entry::BufferPrimitives:
            triVb_->SetData(v, 0, 3);
            dev.SetVertexBuffer(triVb_.get());
            ApplyEffect(Fx::VertexColor);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            break;
        case Entry::BufferPrimitivesRange:
        {
            // The triangle is written at vertex 6 of a 9-vertex buffer; the first six are a
            // degenerate decoy that must never be drawn.
            VertexPositionColor wide[9];
            for (int i = 0; i < 6; ++i) wide[i] = VertexPositionColor(Vector3(2.f, 2.f, 0.f), kBluC);
            wide[6] = v[0]; wide[7] = v[1]; wide[8] = v[2];
            wideVb_->SetData(wide, 0, 9);
            dev.SetVertexBuffer(wideVb_.get());
            ApplyEffect(Fx::VertexColor);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 6, 1);
            break;
        }
        case Entry::BufferIndexed:
        {
            triVb_->SetData(v, 0, 3);
            const std::uint16_t idx[3] = { 0, 1, 2 };
            triIb_->SetData(idx, 0, 3);
            dev.SetVertexBuffer(triVb_.get());
            dev.setIndicesProperty(triIb_.get());
            ApplyEffect(Fx::VertexColor);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
            break;
        }
        case Entry::BufferIndexedRange:
        {
            VertexPositionColor wide[9];
            for (int i = 0; i < 6; ++i) wide[i] = VertexPositionColor(Vector3(2.f, 2.f, 0.f), kBluC);
            wide[6] = v[0]; wide[7] = v[1]; wide[8] = v[2];
            wideVb_->SetData(wide, 0, 9);
            // Indices 6..8 select the real triangle relative to baseVertex 6, i.e. {0,1,2}+6.
            const std::uint16_t idx[9] = { 5, 5, 5, 5, 5, 5, 0, 1, 2 };
            wideIb_->SetData(idx, 0, 9);
            dev.SetVertexBuffer(wideVb_.get());
            dev.setIndicesProperty(wideIb_.get());
            ApplyEffect(Fx::VertexColor);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 6, 0, 3, 6, 1);
            break;
        }
        case Entry::Strip:
            break;   // handled above
        }
    }

    /**
     * @brief Draws A clockwise and B counter-clockwise under @p cull and classifies the result.
     *
     * A is the FRONT face and B the BACK face, so a correct renderer answers each cull mode with a
     * COMPLEMENTARY pair and the two modes are mirror images of one another.
     */
    Outcome RunPair(GraphicsDevice& dev, const Dest& dest, CullMode cull,
                    Entry entry = Entry::UserPrimitives, Fx fx = Fx::VertexColor,
                    const Matrix& world = Matrix::getIdentityProperty())
    {
        Begin(dev, dest);
        dev.setRasterizerStateProperty(Preset(cull));
        try
        {
            DrawTri(dev, Quad::TopLeft,     Wind::Cw,  kRedC, entry, fx, world);
            DrawTri(dev, Quad::BottomRight, Wind::Ccw, kGrnC, entry, fx, world);
        }
        catch (const std::exception& e)
        {
            // A renderer that does not implement this entry point / topology at all rejects the
            // draw. That is a capability boundary, not a winding result: record it and let the
            // caller skip, rather than inventing the capability here.
            Outcome o; o.unsupported = true; o.what = e.what();
            return o;
        }
        const Readback r = Read(dev, dest);
        if (!r.ok()) { Outcome o; o.unreadable = true; return o; }
        return Classify(r, kRedC, kGrnC);
    }

    /**
     * @brief Asserts the full three-mode contract for one destination / entry point / effect.
     *
     * @param tag   Leg label prefix.
     * @param dest  Where to draw.
     * @param entry Which GraphicsDevice entry point issues the triangles.
     * @param fx    Which stock effect shades them.
     */
    void AssertContract(GraphicsDevice& dev, const std::string& tag, const Dest& dest,
                        Entry entry = Entry::UserPrimitives, Fx fx = Fx::VertexColor)
    {
        const Dest probe = dest;
        {
            const Readback r0 = Read(dev, probe);
            if (!Readable(r0, probe, tag)) return;
        }

        const Outcome none = RunPair(dev, dest, CullMode::None, entry, fx);
        const Outcome cw   = RunPair(dev, dest, CullMode::CullClockwiseFace, entry, fx);
        const Outcome ccw  = RunPair(dev, dest, CullMode::CullCounterClockwiseFace, entry, fx);

        if (none.unsupported || cw.unsupported || ccw.unsupported)
        {
            skip(tag + ": not supported on " + kRendererName + " (" +
                 (none.unsupported ? none.what : cw.unsupported ? cw.what : ccw.what) +
                 ") -- capability boundary recorded, not a winding result");
            return;
        }
        if (none.unreadable || cw.unreadable || ccw.unreadable)
        {
            skip(tag + ": " + dest.name + " became unreadable mid-leg on " + kRendererName +
                 " -- boundary recorded");
            return;
        }

        // A destination is only an oracle if it reproduces what was rendered into it at all. When
        // the two NEVER-COVERED quadrants do not even hold the clear colour, the readback is not
        // showing this frame, and scoring the cull modes against it would report a winding defect
        // that is really a readback one. REMED-GFX-164 originally tripped this guard with stale
        // transparent-black resolve storage; retain it so that alpha/readback regression cannot be
        // misreported as a winding failure.
        if (!Same(none.atTR, kClear) || !Same(none.atBL, kClear))
        {
            skip(tag + ": " + dest.name + " does not reproduce even the clear colour on " +
                 kRendererName + " (never-covered quadrants read " + ColorText(none.atTR) + "/" +
                 ColorText(none.atBL) + ", expected " + ColorText(kClear) +
                 ") -- readback boundary recorded, no winding claim made");
            return;
        }

        info(tag + " on " + dest.name + ": CullNone { " + none.Text() + " }");
        info(tag + " on " + dest.name + ": CullClockwise { " + cw.Text() + " }");
        info(tag + " on " + dest.name + ": CullCounterClockwise { " + ccw.Text() + " }");

        // The probe itself must be sound before any culling claim means anything: with culling off
        // BOTH triangles land, in their OWN quadrants, and the other two quadrants stay clear.
        check(none.aPresent && none.bPresent,
              tag + " CullNone draws BOTH windings, so the probe is sound and neither triangle "
                    "is lost for a reason unrelated to culling");
        check(!none.strayTR && !none.strayBL,
              tag + " CullNone leaves the two never-covered quadrants clear, so no projection or "
                    "viewport mirror is moving geometry");

        // The contract.
        check(!cw.aPresent,
              tag + " CullClockwise REMOVES the clockwise-wound triangle (XNA's front face), which "
                    "is the face that mode names");
        check(cw.bPresent,
              tag + " CullClockwise KEEPS the counter-clockwise-wound triangle");
        check(ccw.aPresent,
              tag + " CullCounterClockwise KEEPS the clockwise-wound triangle -- FNA's own "
                    "SpriteBatch winding must survive XNA's default cull mode");
        check(!ccw.bPresent,
              tag + " CullCounterClockwise REMOVES the counter-clockwise-wound triangle");

        // Complementarity, stated as its own assertion so a renderer that culls everything or
        // nothing cannot satisfy the four checks above by accident.
        check(cw.aPresent != ccw.aPresent && cw.bPresent != ccw.bPresent,
              tag + " the two cull modes are exact complements of one another on both windings");
        check(!cw.strayTR && !cw.strayBL && !ccw.strayTR && !ccw.strayBL,
              tag + " no cull mode moves geometry into a never-covered quadrant");
    }

    // ------------------------------------------------------------------ legs

    /// W0 -- the contract on the backbuffer, plus the device's own default rasterizer state.
    void LegBackbuffer(GraphicsDevice& dev)
    {
        Dest bb{ nullptr, kW, kH, "backbuffer" };
        if (!kReadsBackbuffer)
        {
            const Readback r = Read(dev, bb);
            if (!Readable(r, bb, "W0")) return;
        }
        if (kFirstBackbufferReadIsWarmUp)
        {
            Begin(dev, bb);
            DrawTri(dev, Quad::TopLeft, Wind::Cw, kRedC);
            (void)Read(dev, bb);   // discarded: see kFirstBackbufferReadIsWarmUp
        }
        AssertContract(dev, "W0", bb);
    }

    /// W1 -- the same contract on a RenderTarget2D, and on an MSAA one.
    void LegRenderTarget(GraphicsDevice& dev)
    {
        {
            RenderTarget2D rt(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            Dest d{ &rt, kW, kH, "render target" };
            AssertContract(dev, "W1", d);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }
        try
        {
            // DepthFormat::None deliberately: culling is independent of depth (leg W7 proves that
            // separately), and asking bgfx for a MULTISAMPLED render target that also carries a
            // depth attachment trips a bgfx assertion of its own ("Frame buffer depth MSAA texture
            // cannot be resolved"), which is a render-target-creation defect and not a winding one.
            RenderTarget2D rt(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None,
                              4, RenderTargetUsage::DiscardContents);
            Dest d{ &rt, kW, kH, "render target (MSAA 4)" };
            AssertContract(dev, "W1msaa", d);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }
        catch (const std::exception& e)
        {
            skip(std::string("W1msaa: MSAA render target unavailable on ") + kRendererName + " (" +
                 e.what() + ") -- boundary recorded");
        }
    }

    /// W2 -- the classification must not depend on which destination was bound previously.
    void LegDestinationTransitions(GraphicsDevice& dev)
    {
        if (!kReadsBackbuffer)
        {
            skip("W2: needs the backbuffer oracle; W1 carries the target half");
            return;
        }
        RenderTarget2D rt(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest bb{ nullptr, kW, kH, "backbuffer" };
        Dest tg{ &rt, kW, kH, "render target" };

        // target first, then backbuffer, inside one frame.
        (void)RunPair(dev, tg, CullMode::CullCounterClockwiseFace);
        const Outcome afterTarget = RunPair(dev, bb, CullMode::CullCounterClockwiseFace);
        if (afterTarget.unsupported || afterTarget.unreadable)
        {
            skip("W2: unavailable on " + std::string(kRendererName) + " -- boundary recorded");
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            return;
        }
        check(afterTarget.aPresent && !afterTarget.bPresent,
              "W2 backbuffer keeps the clockwise face and culls the counter-clockwise one after a "
              "render-target cycle in the same frame");

        // backbuffer first, then target.
        (void)RunPair(dev, bb, CullMode::CullCounterClockwiseFace);
        const Outcome afterBackbuffer = RunPair(dev, tg, CullMode::CullCounterClockwiseFace);
        check(afterBackbuffer.aPresent && !afterBackbuffer.bPresent,
              "W2 render target keeps the clockwise face and culls the counter-clockwise one after "
              "a backbuffer cycle in the same frame");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    /// W3 -- every draw entry point agrees, including the nonzero-offset ranges and strips.
    void LegEntryPoints(GraphicsDevice& dev)
    {
        struct Case { Entry e; const char* name; };
        const Case cases[] = {
            { Entry::UserIndexed16,        "DrawUserIndexedPrimitives/16" },
            { Entry::UserIndexed32,        "DrawUserIndexedPrimitives/32" },
            { Entry::BufferPrimitives,     "SetVertexBuffer+DrawPrimitives" },
            { Entry::BufferPrimitivesRange,"DrawPrimitives from a nonzero vertexStart" },
            { Entry::BufferIndexed,        "DrawIndexedPrimitives" },
            { Entry::BufferIndexedRange,   "DrawIndexedPrimitives from a nonzero range" },
            { Entry::Strip,                "TriangleStrip" },
        };
        Dest bb{ nullptr, kW, kH, "backbuffer" };
        if (!kReadsBackbuffer)
        {
            skip("W3: needs the backbuffer oracle");
            return;
        }
        for (const Case& c : cases)
            AssertContract(dev, std::string("W3 ") + c.name, bb, c.e);
    }

    /// W4 -- culling is independent of the shader family.
    void LegStockEffects(GraphicsDevice& dev)
    {
        struct Case { Fx fx; const char* name; };
        const Case cases[] = {
            { Fx::BasicTextured, "BasicEffect (textured)" },
            { Fx::AlphaTest,     "AlphaTestEffect" },
            { Fx::DualTexture,   "DualTextureEffect" },
        };
        Dest bb{ nullptr, kW, kH, "backbuffer" };
        if (!kReadsBackbuffer)
        {
            skip("W4: needs the backbuffer oracle");
            return;
        }
        for (const Case& c : cases)
            AssertContract(dev, std::string("W4 ") + c.name, bb, Entry::UserPrimitives, c.fx);
    }

    /**
     * @brief W5 -- a transform's DETERMINANT SIGN decides whether it reverses the facing.
     *
     * A negative-determinant world matrix legitimately turns a front face into a back face, and
     * XNA does not "correct" that. The control compares the mathematical determinant sign against
     * the observed cull result rather than asserting a fixed answer, so an intentional mirror is
     * never compensated twice.
     */
    void LegTransforms(GraphicsDevice& dev)
    {
        Dest bb{ nullptr, kW, kH, "backbuffer" };
        if (!kReadsBackbuffer)
        {
            skip("W5: needs the backbuffer oracle");
            return;
        }
        struct Case { Matrix m; const char* name; bool mirrors; };
        const Case cases[] = {
            { Matrix::getIdentityProperty(),            "identity",              false },
            { Matrix::CreateTranslation(0.f, 0.f, 0.f), "zero translation",      false },
            { Matrix::CreateScale(0.5f),                "uniform positive scale",false },
            { Matrix::CreateScale(0.5f, 0.25f, 1.f),    "non-uniform positive scale", false },
            { Matrix::CreateScale(-1.f, 1.f, 1.f),      "ONE negative axis (mirror)", true  },
            { Matrix::CreateScale(-1.f, -1.f, 1.f),     "TWO negative axes (rotation)", false },
        };

        for (const Case& c : cases)
        {
            // These transforms MOVE their triangles (a mirror sends A into a different quadrant, a
            // scale shrinks it towards the origin), so the probe has to follow the geometry rather
            // than sit at a fixed quadrant centre. A triangle's centroid is always strictly
            // interior, and an affine transform maps a centroid to the transformed centroid, so
            // probing the transformed centroid works for every case here without special-casing.
            Begin(dev, bb);
            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            DrawTri(dev, Quad::TopLeft,     Wind::Cw,  kRedC,
                    Entry::UserPrimitives, Fx::VertexColor, c.m);
            DrawTri(dev, Quad::BottomRight, Wind::Ccw, kGrnC,
                    Entry::UserPrimitives, Fx::VertexColor, c.m);
            const Readback r = Read(dev, bb);
            if (!r.ok())
            {
                skip(std::string("W5 ") + c.name + ": unavailable on " + kRendererName +
                     " -- boundary recorded");
                continue;
            }
            const Color atA = ProbeCentroid(r, Quad::TopLeft,     c.m);
            const Color atB = ProbeCentroid(r, Quad::BottomRight, c.m);
            const bool aPresent = Same(atA, kRedC);
            const bool bPresent = Same(atB, kGrnC);
            info(std::string("W5 ") + c.name + " under CullCounterClockwise: A=" +
                 (aPresent ? "present" : "absent") + " B=" + (bPresent ? "present" : "absent") +
                 " [A" + ColorText(atA) + " B" + ColorText(atB) + "]");
            check(aPresent == !c.mirrors,
                  std::string("W5 ") + c.name + ": a determinant that " +
                  (c.mirrors ? "REVERSES" : "preserves") + " orientation " +
                  (c.mirrors ? "makes the clockwise triangle a back face and it is culled"
                             : "leaves the clockwise triangle a front face and it survives"));
            // And the counter-clockwise triangle answers the complementary question.
            check(bPresent == c.mirrors,
                  std::string("W5 ") + c.name +
                  ": the counter-clockwise triangle answers the complementary question");
        }
    }

    /// W6 -- A -> B -> A rasterizer-state transitions leave no stale native pipeline state.
    void LegStateTransitions(GraphicsDevice& dev)
    {
        Dest bb{ nullptr, kW, kH, "backbuffer" };
        if (!kReadsBackbuffer)
        {
            skip("W6: needs the backbuffer oracle");
            return;
        }

        // CCW -> CW -> CCW: the final result must equal a standalone CCW pass.
        const Outcome first = RunPair(dev, bb, CullMode::CullCounterClockwiseFace);
        (void)RunPair(dev, bb, CullMode::CullClockwiseFace);
        (void)RunPair(dev, bb, CullMode::None);
        const Outcome back = RunPair(dev, bb, CullMode::CullCounterClockwiseFace);
        if (first.unsupported || first.unreadable || back.unsupported || back.unreadable)
        {
            skip("W6: unavailable on " + std::string(kRendererName) + " -- boundary recorded");
            return;
        }
        check(first.aPresent == back.aPresent && first.bPresent == back.bPresent &&
              back.aPresent && !back.bPresent,
              "W6 returning to CullCounterClockwise after CullClockwise and CullNone reproduces "
              "the original classification exactly, so no native rasterizer state went stale");

        // A DISTINCT RasterizerState object with an EQUAL value must behave identically -- a
        // pipeline cache keyed on object identity rather than value would diverge here.
        RasterizerState distinct;
        distinct.setCullModeProperty(CullMode::CullCounterClockwiseFace);
        Begin(dev, bb);
        dev.setRasterizerStateProperty(distinct);
        DrawTri(dev, Quad::TopLeft,     Wind::Cw,  kRedC);
        DrawTri(dev, Quad::BottomRight, Wind::Ccw, kGrnC);
        const Readback rd = Read(dev, bb);
        if (rd.ok())
        {
            const Outcome o = Classify(rd, kRedC, kGrnC);
            check(o.aPresent && !o.bPresent,
                  "W6 a distinct RasterizerState object of equal value classifies identically to "
                  "the preset");
        }

        // The device's own default, never assigned by this fixture.
        check(RasterizerState::CullCounterClockwise.getCullModeProperty() ==
                  CullMode::CullCounterClockwiseFace,
              "W6 RasterizerState.CullCounterClockwise really carries CullCounterClockwiseFace");
    }

    /// W7 -- the classification is independent of every unrelated render state.
    void LegStateIndependence(GraphicsDevice& dev)
    {
        Dest bb{ nullptr, kW, kH, "backbuffer" };
        if (!kReadsBackbuffer)
        {
            skip("W7: needs the backbuffer oracle");
            return;
        }

        // Depth on (with a depth buffer), alpha blending on, a viewport covering the whole target
        // and scissor enabled over the whole target must not change WHICH triangle is culled.
        Begin(dev, bb);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.setBlendStateProperty(BlendState::AlphaBlend);
        DrawTri(dev, Quad::TopLeft,     Wind::Cw,  kRedC);
        DrawTri(dev, Quad::BottomRight, Wind::Ccw, kGrnC);
        const Readback r = Read(dev, bb);
        if (r.ok())
        {
            const Outcome o = Classify(r, kRedC, kGrnC);
            info("W7 depth+blend enabled { " + o.Text() + " }");
            check(o.aPresent && !o.bPresent,
                  "W7 enabling DepthStencilState.Default and BlendState.AlphaBlend does not change "
                  "which winding CullCounterClockwise removes");
        }
        ResetState(dev);
    }

    /**
     * @brief W8 -- the SpriteBatch reference oracle.
     *
     * SpriteBatch is the one public API whose emitted winding is fixed by FNA, so it is the
     * strongest available cross-check that this file's "clockwise" really is XNA's front face:
     * a default SpriteBatch.Begin() assigns RasterizerState.CullCounterClockwise to the device and
     * its own sprites must still be visible.
     */
    void LegSpriteBatchReference(GraphicsDevice& dev)
    {
        Dest bb{ nullptr, kW, kH, "backbuffer" };
        if (!kReadsBackbuffer)
        {
            skip("W8: needs the backbuffer oracle");
            return;
        }

        // 1 -- a sprite drawn through a DEFAULTED rasterizer state (null => CullCounterClockwise)
        //      must be visible. If it is not, this renderer culls FNA's own sprite winding.
        Begin(dev, bb);
        {
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, nullptr);
            sb_->Draw(white_, Rectangle(0, 0, kW / 2, kH / 2), Rectangle(0, 0, 1, 1), kRedC);
            sb_->End();
        }
        const Readback rs = Read(dev, bb);
        if (rs.ok())
        {
            const Color atA = Probe(rs, kW / 8, kH / 8);
            info("W8 sprite under a defaulted (CullCounterClockwise) rasterizer state: " +
                 ColorText(atA));
            check(Same(atA, kRedC),
                  "W8 a SpriteBatch sprite survives its OWN default rasterizer state "
                  "(CullCounterClockwise) -- FNA's sprite winding is a FRONT face");
        }

        // 2 -- a stock 3D triangle wound the SAME way as FNA's sprite triangle must survive the
        //      state SpriteBatch leaves behind. This is the equivalence REMED-GFX-157 needed.
        Begin(dev, bb);
        {
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, nullptr);
            sb_->Draw(white_, Rectangle(kW / 2, 0, kW / 2, kH / 2), Rectangle(0, 0, 1, 1), kBluC);
            sb_->End();
            // SpriteBatch.End does NOT restore the previous rasterizer state (XNA-correct), so the
            // device is still on CullCounterClockwise here -- deliberately not reset.
            DrawTri(dev, Quad::TopLeft, Wind::Cw, kRedC);
        }
        const Readback rs2 = Read(dev, bb);
        if (rs2.ok())
        {
            const Color atA  = Probe(rs2, kW / 8, kH / 8);
            const Color atTR = Probe(rs2, kW * 7 / 8, kH / 8);
            info("W8 3D clockwise triangle after a defaulted SpriteBatch: A=" + ColorText(atA) +
                 " sprite=" + ColorText(atTR));
            check(Same(atTR, kBluC),
                  "W8 the sprite itself landed, so the sequence ran");
            check(Same(atA, kRedC),
                  "W8 a stock 3D triangle wound like an FNA sprite survives the "
                  "CullCounterClockwise state SpriteBatch legitimately leaves behind");
        }

        // 3 -- and the reversed one does not, under that same left-behind state.
        Begin(dev, bb);
        {
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, nullptr);
            sb_->Draw(white_, Rectangle(kW / 2, 0, kW / 2, kH / 2), Rectangle(0, 0, 1, 1), kBluC);
            sb_->End();
            DrawTri(dev, Quad::TopLeft, Wind::Ccw, kRedC);
        }
        const Readback rs3 = Read(dev, bb);
        if (rs3.ok())
        {
            const Color atA = Probe(rs3, kW / 8, kH / 8);
            check(Same(atA, kClear),
                  "W8 the REVERSED triangle is removed by that same left-behind state, so the two "
                  "windings are genuinely complementary through the SpriteBatch path too");
        }
        ResetState(dev);
    }

    // ------------------------------------------------------------------ Game

    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        white_ = Texture2D(dev, 1, 1);
        const Color w = kWhtC; white_.SetData(&w, 1);
        redTex_ = Texture2D(dev, 1, 1);
        const Color rr = kRedC; redTex_.SetData(&rr, 1);
        grnTex_ = Texture2D(dev, 1, 1);
        const Color gg = kGrnC; grnTex_.SetData(&gg, 1);

        vcolFx_  = std::make_unique<BasicEffect>(dev);
        texFx_   = std::make_unique<BasicEffect>(dev);
        alphaFx_ = std::make_unique<AlphaTestEffect>(dev);
        dualFx_  = std::make_unique<DualTextureEffect>(dev);

        const auto& decl = VertexPositionColor::getVertexDeclarationStatic();
        triVb_   = std::make_unique<VertexBuffer>(dev, decl, 3, BufferUsage::None);
        wideVb_  = std::make_unique<VertexBuffer>(dev, decl, 9, BufferUsage::None);
        stripVb_ = std::make_unique<VertexBuffer>(dev, decl, 4, BufferUsage::None);
        triIb_   = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 3,
                                                 BufferUsage::None);
        wideIb_  = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 9,
                                                 BufferUsage::None);

        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // Read the device's own starting rasterizer state BEFORE this fixture assigns anything.
        const CullMode startCull = dev.getRasterizerStateProperty().getCullModeProperty();

        ResetState(dev);
        dev.Clear(kClear);

        std::printf("[INFO] REMED-GFX-160 front-face winding on %s "
                    "(requested %dx%d backbuffer, actual %dx%d)\n",
                    kRendererName, kW, kH,
                    dev.getPresentationParametersProperty().getBackBufferWidthProperty(),
                    dev.getPresentationParametersProperty().getBackBufferHeightProperty());
        std::fflush(stdout);

        check(startCull == CullMode::CullCounterClockwiseFace,
              "W0 the device's initial RasterizerState is CullCounterClockwise, XNA's documented "
              "default");

        LegBackbuffer(dev);
        LegRenderTarget(dev);
        LegDestinationTransitions(dev);
        LegEntryPoints(dev);
        LegStockEffects(dev);
        LegTransforms(dev);
        LegStateTransitions(dev);
        LegStateIndependence(dev);
        LegSpriteBatchReference(dev);

        std::printf("[INFO] %s: %d/%d checks passed (%d skipped)\n",
                    kRendererName, passCount_, totalCount_, skipCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    /** @brief Builds the fixture at a fixed backbuffer size. */
    FrontFaceWindingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kW);
        gdm_->setPreferredBackBufferHeightProperty(kH);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    FrontFaceWindingTest test;
    test.Run();
    return test.Result();
}
