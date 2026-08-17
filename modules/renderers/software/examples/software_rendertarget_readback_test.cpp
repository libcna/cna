// SPDX-License-Identifier: MS-PL
// REMED-GFX-124: a Software (CPU-raster) RenderTarget2D must be readable and sampleable once it is
// no longer the active target -- the ordinary XNA/FNA producer -> unbind -> consumer contract every
// GPU renderer already honors.
//
// Public contract under test:
//   * RenderTarget2D::GetData returns the target's rendered COLOUR attachment (full level, arbitrary
//     rectangle, arbitrary destination start index), never leaving the caller's memory untouched.
//   * After unbinding, the same target samples as an ordinary Texture2D -- through SpriteBatch and
//     through a textured 3D primitive alike -- reproducing its real pixels, not a fallback colour.
//
// Pre-fix Software signature (this test FAILS pre-fix, two independent causes):
//   A  readback: SoftwareRenderTargetRenderer overrides no ITextureRenderer::GetData, so
//      Texture2D::GetData's render-target fallback reaches the interface's DEFAULT no-op. The
//      renderer writes nothing -- but the buffer it was handed is a scratch vector Texture2D
//      ZERO-INITIALIZED itself, and those zeros are then converted into the caller's Color array.
//      The observable public result is therefore not an untouched destination but a fabricated,
//      fully written, uniformly transparent-black frame, which is strictly worse: a "did GetData
//      write anything?" sentinel check passes, and any assertion whose expected content is
//      transparent black passes on an empty target.
//   B  sampling: SoftwareRenderTargetRenderer is not a SoftwareTextureRenderer, so both sampler entry
//      points (SoftwareSpriteBatchRenderer::Draw and RasterizeTriangleShaded) dynamic_cast it to
//      nullptr and shade the geometry untextured -- a white quad instead of the target's contents.
//
// Every readback check here fills its destination with a distinctive NON-RESULT sentinel first and
// asserts both that the sentinel was overwritten and that the exact expected pixels arrived, with two
// different sentinel patterns (0xCD and 0xA5) so no single pre-fill can be mistaken for a result.
// The rendered pattern carries four distinct opaque/semi-transparent block colours plus a
// semi-transparent background, so a fallback white frame, an all-zero frame, or an untouched
// destination all fail decisively.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 64;   ///< Backbuffer width.
    constexpr int kBBH = 32;   ///< Backbuffer height.
    constexpr int kRTW = 32;   ///< Render-target width (deliberately != the backbuffer's).
    constexpr int kRTH = 16;   ///< Render-target height.
    constexpr int kBlock = 8;  ///< Side of each corner colour block inside the target.

    /// Two unrelated destination pre-fills. Neither equals any rendered colour, so "the buffer still
    /// holds the sentinel" and "the buffer holds the rendered pixel" can never be confused.
    Color SentinelCD() { return Color(0xCD, 0xCD, 0xCD, 0xCD); }
    Color SentinelA5() { return Color(0xA5, 0xA5, 0xA5, 0xA5); }

    /// The target's four corner blocks and its background. Returned from functions because Color's
    /// static constants live in another translation unit with no guaranteed initialization order.
    Color BlockTopLeft()     { return Color(255, 0, 0, 255); }      ///< opaque red
    Color BlockTopRight()    { return Color(0, 255, 0, 255); }      ///< opaque green
    Color BlockBottomLeft()  { return Color(0, 0, 255, 255); }      ///< opaque blue
    Color BlockBottomRight() { return Color(255, 128, 0, 64); }     ///< partially transparent orange
    Color TargetBackground() { return Color(20, 40, 60, 128); }     ///< partially transparent slate

    std::string ColorText(const Color& c)
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

    /// The colour the rendered pattern must hold at target pixel (x, y).
    Color ExpectedTargetPixel(int x, int y)
    {
        const bool left = x < kBlock;
        const bool right = x >= kRTW - kBlock;
        const bool top = y < kBlock;
        if (left && top) return BlockTopLeft();
        if (right && top) return BlockTopRight();
        if (left && !top) return BlockBottomLeft();
        if (right && !top) return BlockBottomRight();
        return TargetBackground();
    }

    /// Probe points that prove real coordinates were addressed: one interior pixel per block plus two
    /// background pixels between them. Deliberately away from every block edge, so a legitimate
    /// half-pixel raster convention difference cannot change the expected colour.
    struct Probe
    {
        int x, y;
    };
    std::array<Probe, 6> Probes()
    {
        return {Probe{3, 3}, Probe{28, 3}, Probe{3, 12}, Probe{28, 12},
                Probe{16, 3}, Probe{16, 12}};
    }
}

class SoftwareRenderTargetReadbackTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
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
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    /// Draws one solid rectangle of exactly @p tint into whatever target is bound. BlendState::Opaque
    /// stores the source colour verbatim (alpha included), so a semi-transparent block survives.
    void FillRect(GraphicsDevice& dev, const Rectangle& destination, const Color& tint)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, destination, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    /// Renders the canonical spatially varied pattern into @p target and unbinds it, leaving the
    /// backbuffer active. Nothing here reads the target back.
    void ProduceTargetPattern(GraphicsDevice& dev, RenderTarget2D& target)
    {
        dev.SetRenderTarget(&target);
        ResetState(dev);
        dev.Clear(TargetBackground());
        FillRect(dev, Rectangle(0, 0, kBlock, kBlock), BlockTopLeft());
        FillRect(dev, Rectangle(kRTW - kBlock, 0, kBlock, kBlock), BlockTopRight());
        FillRect(dev, Rectangle(0, kRTH - kBlock, kBlock, kBlock), BlockBottomLeft());
        FillRect(dev, Rectangle(kRTW - kBlock, kRTH - kBlock, kBlock, kBlock), BlockBottomRight());
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    std::vector<Color> ReadBackbuffer(GraphicsDevice& dev)
    {
        std::vector<Color> pix(static_cast<std::size_t>(kBBW) * kBBH, SentinelCD());
        const Rectangle whole(0, 0, kBBW, kBBH);
        dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    static const Color& AtBackbuffer(const std::vector<Color>& pix, int x, int y)
    {
        return pix[static_cast<std::size_t>(y) * kBBW + x];
    }

    /// How many entries of @p pixels still hold @p sentinel. Zero proves GetData wrote the whole
    /// destination; `pixels.size()` proves it wrote nothing at all.
    static std::size_t SentinelSurvivors(const std::vector<Color>& pixels, const Color& sentinel)
    {
        std::size_t n = 0;
        for (const Color& c : pixels)
            if (Same(c, sentinel)) ++n;
        return n;
    }

    /// Asserts a full-target readback into a destination pre-filled with @p sentinel.
    void CheckFullReadback(RenderTarget2D& target, const Color& sentinel, const std::string& label)
    {
        const std::size_t total = static_cast<std::size_t>(kRTW) * kRTH;
        std::vector<Color> pixels(total, sentinel);
        target.GetData(pixels.data(), 0, static_cast<int>(total));

        const std::size_t survivors = SentinelSurvivors(pixels, sentinel);
        check(survivors != total,
              label + ".changed: GetData overwrote the " + ColorText(sentinel) +
                  " sentinel destination (surviving sentinel pixels " + std::to_string(survivors) +
                  " of " + std::to_string(total) + ")");

        // The sentinel check above is necessary but NOT sufficient, and pre-fix it already passes:
        // Texture2D::GetData's render-target fallback hands the renderer a scratch buffer it
        // zero-initialized itself, so a no-op renderer still yields a fully written destination -- of
        // fabricated Color(0,0,0,0). A uniform frame therefore has to fail on its own, or a caller
        // whose expected content happens to be transparent black could never tell the difference.
        std::size_t blank = 0;
        for (const Color& c : pixels)
            if (Same(c, Color(0, 0, 0, 0))) ++blank;
        check(blank != total,
              label + ".notblank: the readback is real content, not a uniform fabricated "
                      "(0,0,0,0) frame (" + std::to_string(blank) + " of " +
                  std::to_string(total) + " pixels blank)");

        bool exact = true;
        std::string detail;
        for (const Probe& p : Probes())
        {
            const Color actual = pixels[static_cast<std::size_t>(p.y) * kRTW + p.x];
            const Color expected = ExpectedTargetPixel(p.x, p.y);
            if (!Same(actual, expected))
            {
                exact = false;
                detail += " (" + std::to_string(p.x) + "," + std::to_string(p.y) + ")=" +
                          ColorText(actual) + "!=" + ColorText(expected);
            }
        }
        check(exact, label + ".exact: every probed pixel matches the rendered pattern" + detail);

        bool wholeExact = true;
        for (int y = 0; y < kRTH && wholeExact; ++y)
            for (int x = 0; x < kRTW; ++x)
                if (!Same(pixels[static_cast<std::size_t>(y) * kRTW + x], ExpectedTargetPixel(x, y)))
                {
                    wholeExact = false;
                    detail = " first mismatch (" + std::to_string(x) + "," + std::to_string(y) + ")=" +
                             ColorText(pixels[static_cast<std::size_t>(y) * kRTW + x]) + "!=" +
                             ColorText(ExpectedTargetPixel(x, y));
                    break;
                }
        check(wholeExact, label + ".whole: all " + std::to_string(total) +
                              " readback pixels match the rendered pattern" +
                              (wholeExact ? "" : detail));
    }

    /// Samples @p target over the backbuffer at 1:1 through SpriteBatch and probes the result.
    void CheckSpriteBatchSampling(GraphicsDevice& dev, RenderTarget2D& target,
                                  const std::string& label)
    {
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(target, Rectangle(0, 0, kRTW, kRTH), Rectangle(0, 0, kRTW, kRTH),
                    Color(255, 255, 255, 255));
            sb.End();
        }
        const std::vector<Color> pix = ReadBackbuffer(dev);

        bool exact = true;
        std::string detail;
        for (const Probe& p : Probes())
        {
            const Color actual = AtBackbuffer(pix, p.x, p.y);
            const Color expected = ExpectedTargetPixel(p.x, p.y);
            if (!Same(actual, expected))
            {
                exact = false;
                detail += " (" + std::to_string(p.x) + "," + std::to_string(p.y) + ")=" +
                          ColorText(actual) + "!=" + ColorText(expected);
            }
        }
        check(exact, label + ": SpriteBatch reproduced the target's real pixels" + detail);
    }

    /// Samples @p target through an ordinary textured 3D primitive (BasicEffect + a two-triangle
    /// VertexPositionTexture quad) inside a target-sized viewport, so the mapping is 1:1.
    void Check3DSampling(GraphicsDevice& dev, RenderTarget2D& target, const std::string& label)
    {
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        dev.setViewportProperty(Viewport(0, 0, kRTW, kRTH));

        const std::array<VertexPositionTexture, 6> quad{
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)),
        };

        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&target);
        fx.Apply();
        dev.SetVertexBuffer(nullptr);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);

        dev.setViewportProperty(Viewport(0, 0, kBBW, kBBH));
        const std::vector<Color> pix = ReadBackbuffer(dev);

        bool exact = true;
        std::string detail;
        for (const Probe& p : Probes())
        {
            const Color actual = AtBackbuffer(pix, p.x, p.y);
            const Color expected = ExpectedTargetPixel(p.x, p.y);
            if (!Same(actual, expected))
            {
                exact = false;
                detail += " (" + std::to_string(p.x) + "," + std::to_string(p.y) + ")=" +
                          ColorText(actual) + "!=" + ColorText(expected);
            }
        }
        check(exact, label + ": a textured 3D primitive reproduced the target's real pixels" + detail);
    }

    // ---- readback-matrix helpers ------------------------------------------------------------

    /// One rectangle readback into a destination pre-filled with @p sentinel at @p startIndex, with
    /// startIndex leading entries and a trailing margin that must both survive untouched. Asserts
    /// the exact region contents, so a wrong row pitch or origin cannot pass.
    void CheckRectReadback(RenderTarget2D& target, int rx, int ry, int rw, int rh, int startIndex,
                           const std::string& label)
    {
        const Color sentinel = SentinelA5();
        const std::size_t region = static_cast<std::size_t>(rw) * rh;
        const std::size_t margin = 5;
        std::vector<Color> pixels(static_cast<std::size_t>(startIndex) + region + margin, sentinel);
        const Rectangle rect(rx, ry, rw, rh);
        target.GetData(0, &rect, pixels.data(), startIndex, static_cast<int>(region));

        bool exact = true;
        std::string detail;
        for (int row = 0; row < rh; ++row)
            for (int col = 0; col < rw; ++col)
            {
                const Color actual =
                    pixels[static_cast<std::size_t>(startIndex) + static_cast<std::size_t>(row) * rw + col];
                const Color expected = ExpectedTargetPixel(rx + col, ry + row);
                if (!Same(actual, expected) && exact)
                {
                    exact = false;
                    detail = " first mismatch at region (" + std::to_string(col) + "," +
                             std::to_string(row) + ") = target (" + std::to_string(rx + col) + "," +
                             std::to_string(ry + row) + "): " + ColorText(actual) + "!=" +
                             ColorText(expected);
                }
            }
        check(exact, label + ".exact: rectangle (" + std::to_string(rx) + "," + std::to_string(ry) +
                         "," + std::to_string(rw) + "," + std::to_string(rh) +
                         ") returned the requested region" + detail);

        std::size_t intact = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(startIndex); ++i)
            if (Same(pixels[i], sentinel)) ++intact;
        for (std::size_t i = static_cast<std::size_t>(startIndex) + region; i < pixels.size(); ++i)
            if (Same(pixels[i], sentinel)) ++intact;
        check(intact == static_cast<std::size_t>(startIndex) + margin,
              label + ".margins: the " + std::to_string(startIndex) + " destination entries before "
                      "startIndex and the " + std::to_string(margin) +
                  " after the region kept their sentinel (" + std::to_string(intact) + " intact)");
    }

    template <typename ExceptionT, typename Fn>
    static bool Throws(Fn&& fn)
    {
        try { fn(); }
        catch (const ExceptionT&) { return true; }
        catch (...) { return false; }
        return false;
    }

    template <typename Fn>
    static bool ThrowsAnything(Fn&& fn)
    {
        try { fn(); }
        catch (...) { return true; }
        return false;
    }

    // ---- sampler-parity helpers -------------------------------------------------------------

    /// A plain Texture2D holding exactly the pattern the render target is supposed to hold, built
    /// from the expected pattern alone -- never from a readback -- so sampler parity cannot be
    /// satisfied by two paths being wrong in the same way.
    static Texture2D MakePatternTexture(GraphicsDevice& dev)
    {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(static_cast<std::size_t>(kRTW) * kRTH * 4);
        for (int y = 0; y < kRTH; ++y)
            for (int x = 0; x < kRTW; ++x)
            {
                const Color c = ExpectedTargetPixel(x, y);
                bytes.push_back(c.getRProperty());
                bytes.push_back(c.getGProperty());
                bytes.push_back(c.getBProperty());
                bytes.push_back(c.getAProperty());
            }
        return Texture2D::CreateFromPixels(dev, kRTW, kRTH, bytes);
    }

    /// Draws @p texture once with the supplied sampler state / source rectangle / flip and returns
    /// the whole backbuffer, so two textures can be compared byte for byte.
    std::vector<Color> SampleFrame(GraphicsDevice& dev, const Texture2D& texture,
                                   const SamplerState& sampler, const Rectangle& destination,
                                   const Rectangle& source, SpriteEffects effects)
    {
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState state = sampler;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &state, nullptr, nullptr);
            sb.Draw(texture, destination, std::optional<Rectangle>(source), Color(255, 255, 255, 255),
                    0.0f, Vector2(0.0f, 0.0f), effects, 0.0f);
            sb.End();
        }
        return ReadBackbuffer(dev);
    }

    /// The render target and an identical plain texture must produce byte-identical frames under
    /// the same sampler state -- proving the target goes through the ORDINARY sampler rather than
    /// any special-cased path, whatever filtering and addressing this renderer actually implements.
    void CheckSamplerParity(GraphicsDevice& dev, RenderTarget2D& target, const Texture2D& plain,
                            const SamplerState& sampler, const Rectangle& destination,
                            const Rectangle& source, SpriteEffects effects,
                            const std::string& label)
    {
        const std::vector<Color> fromTarget =
            SampleFrame(dev, target, sampler, destination, source, effects);
        const std::vector<Color> fromPlain =
            SampleFrame(dev, plain, sampler, destination, source, effects);

        std::size_t differing = 0;
        std::string detail;
        for (std::size_t i = 0; i < fromTarget.size(); ++i)
            if (!Same(fromTarget[i], fromPlain[i]))
            {
                if (differing == 0)
                    detail = " first at (" + std::to_string(static_cast<int>(i) % kBBW) + "," +
                             std::to_string(static_cast<int>(i) / kBBW) + "): target " +
                             ColorText(fromTarget[i]) + " vs texture " + ColorText(fromPlain[i]);
                ++differing;
            }

        // A frame that is uniformly the clear colour would compare equal without proving anything.
        std::size_t lit = 0;
        for (const Color& c : fromPlain)
            if (!Same(c, Color(0, 0, 0, 255))) ++lit;

        check(differing == 0 && lit > 0,
              label + ": render target and identical plain texture sample identically (" +
                  std::to_string(differing) + " differing pixels, " + std::to_string(lit) +
                  " non-background pixels in the reference frame)" + detail);
    }

    /// Renders a single solid @p tint over the whole of @p target and returns nothing; used where a
    /// check only needs a target to hold a distinguishable uniform colour.
    void FillTarget(GraphicsDevice& dev, RenderTarget2D& target, const Color& tint)
    {
        dev.SetRenderTarget(&target);
        ResetState(dev);
        dev.Clear(tint);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// The single colour a whole target holds, or a mismatch report.
    static bool WholeTargetIs(RenderTarget2D& target, const Color& expected, std::string& detail)
    {
        const int w = target.getWidthProperty();
        const int h = target.getHeightProperty();
        std::vector<Color> pixels(static_cast<std::size_t>(w) * h, SentinelCD());
        target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        for (std::size_t i = 0; i < pixels.size(); ++i)
            if (!Same(pixels[i], expected))
            {
                detail = " (" + std::to_string(static_cast<int>(i) % w) + "," +
                         std::to_string(static_cast<int>(i) / w) + ")=" + ColorText(pixels[i]) +
                         "!=" + ColorText(expected);
                return false;
            }
        return true;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        ResetState(dev);

        RenderTarget2D target(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        ProduceTargetPattern(dev, target);

        // ---- A: full-level readback into two different sentinel destinations -------------------
        CheckFullReadback(target, SentinelCD(), "A1 full readback (0xCD sentinel)");
        CheckFullReadback(target, SentinelA5(), "A2 full readback (0xA5 sentinel)");

        // ---- B: rectangle readback keeps the surrounding destination sentinel intact -----------
        {
            const Color sentinel = SentinelCD();
            const int rx = 12, ry = 5, rw = 6, rh = 4;
            std::vector<Color> pixels(64, sentinel);
            const Rectangle rect(rx, ry, rw, rh);
            target.GetData(0, &rect, pixels.data(), 0, static_cast<int>(pixels.size()));

            bool exact = true;
            std::string detail;
            for (int row = 0; row < rh; ++row)
                for (int col = 0; col < rw; ++col)
                {
                    const Color actual = pixels[static_cast<std::size_t>(row) * rw + col];
                    const Color expected = ExpectedTargetPixel(rx + col, ry + row);
                    if (!Same(actual, expected) && exact)
                    {
                        exact = false;
                        detail = " first mismatch (" + std::to_string(col) + "," +
                                 std::to_string(row) + ")=" + ColorText(actual) + "!=" +
                                 ColorText(expected);
                    }
                }
            check(exact, "B1 rectangle readback returns the requested region" + detail);

            std::size_t tail = 0;
            for (std::size_t i = static_cast<std::size_t>(rw) * rh; i < pixels.size(); ++i)
                if (Same(pixels[i], sentinel)) ++tail;
            check(tail == pixels.size() - static_cast<std::size_t>(rw) * rh,
                  "B2 rectangle readback left the destination past the region untouched (" +
                      std::to_string(tail) + " sentinel entries kept)");
        }

        // ---- C: producer -> unbind -> consumer, through both sampler entry points --------------
        CheckSpriteBatchSampling(dev, target, "C1 SpriteBatch sampling");
        Check3DSampling(dev, target, "C2 textured 3D primitive sampling");

        // ---- D: the rectangle matrix -- pitch, origin and destination start index --------------
        // Every rectangle below straddles at least one block boundary or an edge, so a readback that
        // used the wrong row pitch, the wrong origin, or the target width instead of the rectangle
        // width returns visibly different colours rather than a plausible-looking region.
        CheckRectReadback(target, 0, 0, kRTW, kRTH, 0, "D1 whole target through a rectangle");
        CheckRectReadback(target, kRTW - 1, kRTH - 1, 1, 1, 0, "D2 single lower-right pixel");
        CheckRectReadback(target, 0, kRTH - 1, kRTW, 1, 0, "D3 final row");
        CheckRectReadback(target, kRTW - 1, 0, 1, kRTH, 0, "D4 final column");
        CheckRectReadback(target, kRTW - 5, kRTH - 3, 5, 3, 0, "D5 rectangle touching the lower-right edge");
        CheckRectReadback(target, 6, 4, 9, 7, 0, "D6 interior rectangle across three regions");
        CheckRectReadback(target, 6, 4, 9, 7, 11, "D7 same rectangle at destination start index 11");
        CheckRectReadback(target, kRTW - 5, kRTH - 3, 5, 3, 3, "D8 edge rectangle at destination start index 3");

        // ---- E: every unsupported or out-of-range request is rejected, never silently ignored ---
        {
            const std::size_t total = static_cast<std::size_t>(kRTW) * kRTH;
            std::vector<Color> buffer(total, SentinelCD());
            const Rectangle whole(0, 0, kRTW, kRTH);

            check(Throws<std::invalid_argument>(
                      [&] { target.GetData(0, &whole, nullptr, 0, static_cast<int>(total)); }),
                  "E1 null destination rejected");
            check(Throws<std::out_of_range>(
                      [&] { target.GetData(0, &whole, buffer.data(), -1, static_cast<int>(total)); }),
                  "E2 negative destination start index rejected");
            check(Throws<std::invalid_argument>(
                      [&] { target.GetData(0, &whole, buffer.data(), 0, 0); }),
                  "E3 zero element count rejected");
            check(Throws<std::out_of_range>(
                      [&] {
                          const Rectangle outside(kRTW - 2, 0, 4, 4);
                          target.GetData(0, &outside, buffer.data(), 0, static_cast<int>(total));
                      }),
                  "E4 rectangle leaving the target rejected");
            check(Throws<std::out_of_range>(
                      [&] {
                          const Rectangle negative(-1, -1, 4, 4);
                          target.GetData(0, &negative, buffer.data(), 0, static_cast<int>(total));
                      }),
                  "E5 negative rectangle origin rejected");
            check(Throws<std::out_of_range>(
                      [&] {
                          const Rectangle empty(0, 0, 0, 4);
                          target.GetData(0, &empty, buffer.data(), 0, static_cast<int>(total));
                      }),
                  "E6 zero-width rectangle rejected");
            check(Throws<std::out_of_range>(
                      [&] {
                          const Rectangle region(0, 0, 8, 8);
                          target.GetData(0, &region, buffer.data(), 0, 8 * 8 - 1);
                      }),
                  "E7 element count smaller than the requested region rejected");
            check(Throws<std::out_of_range>(
                      [&] { target.GetData(-1, &whole, buffer.data(), 0, static_cast<int>(total)); }),
                  "E8 negative mip level rejected");
            // REMED-GFX-198: this public request is an invalid Texture2D argument, not a request
            // for a valid level that Software happens not to implement. Before GFX-192, level 1
            // reached SoftwareRenderTargetRenderer::GetData and raised its level-0-only
            // NotSupportedException after the shared layer had allocated scratch storage. The
            // shared [0, LevelCount) validator now owns the request and must reject it before mip
            // dimensions, scratch allocation, capability policy or Software dispatch.
            {
                using CNA::Internal::Renderers::Software::SoftwareRenderTargetRenderer;
                constexpr int requestedLevel = 1;
                constexpr int prefix = 3;
                constexpr int requestedCount = 1;
                constexpr int suffix = 5;
                const int publicLevelCount = target.getLevelCountProperty();
                auto* softwareRenderer =
                    dynamic_cast<SoftwareRenderTargetRenderer*>(target.GetRenderTargetRenderer());

                check(publicLevelCount == 1 && requestedLevel == publicLevelCount,
                      "E9a request level 1 is exactly LevelCount 1, outside [0, LevelCount)");
                check(softwareRenderer != nullptr,
                      "E9b the production Software render-target renderer is observable");

                std::vector<Color> rejected(prefix + requestedCount + suffix, SentinelA5());
                const std::vector<Color> rejectedBefore = rejected;
                std::size_t callsBefore = 0;
                const std::size_t trackedBefore = dev.GetTrackedResourceCount();
                const std::uint8_t* colorAddressBefore = nullptr;
                const float* depthAddressBefore = nullptr;
                std::size_t colorSizeBefore = 0, colorCapacityBefore = 0;
                std::size_t depthSizeBefore = 0, depthCapacityBefore = 0;
                std::vector<std::uint8_t> colorBefore;
                std::vector<float> depthBefore;
                if (softwareRenderer != nullptr)
                {
                    callsBefore = softwareRenderer->GetReadbackCallCountEXT();
                    const auto& framebuffer = softwareRenderer->Framebuffer();
                    colorAddressBefore = framebuffer.color.data();
                    depthAddressBefore = framebuffer.depthBuffer.data();
                    colorSizeBefore = framebuffer.color.size();
                    colorCapacityBefore = framebuffer.color.capacity();
                    depthSizeBefore = framebuffer.depthBuffer.size();
                    depthCapacityBefore = framebuffer.depthBuffer.capacity();
                    colorBefore = framebuffer.color;
                    depthBefore = framebuffer.depthBuffer;
                }

                bool wasOutOfRange = false;
                std::string what;
                try
                {
                    target.GetData(requestedLevel, nullptr, rejected.data(), prefix,
                                   requestedCount);
                }
                catch (const std::out_of_range& e)
                {
                    wasOutOfRange = true;
                    what = e.what();
                }
                catch (const std::exception& e) { what = e.what(); }

                check(wasOutOfRange,
                      "E9c invalid public level is std::out_of_range, not Software's "
                      "NotSupportedException");
                check(what.find("level 1") != std::string::npos &&
                          what.find("LevelCount 1") != std::string::npos,
                      "E9d exception identifies requested level 1 and public LevelCount 1: " +
                          what);
                check(rejected == rejectedBefore,
                      "E9e rejected read leaves destination prefix, requested range and suffix "
                      "unchanged");

                check(softwareRenderer != nullptr &&
                          softwareRenderer->GetReadbackCallCountEXT() == callsBefore,
                      "E9f invalid public level makes zero Software renderer readback calls");

                bool storageUnchanged = false;
                if (softwareRenderer != nullptr)
                {
                    const auto& framebuffer = softwareRenderer->Framebuffer();
                    storageUnchanged = framebuffer.color.data() == colorAddressBefore &&
                        framebuffer.depthBuffer.data() == depthAddressBefore &&
                        framebuffer.color.size() == colorSizeBefore &&
                        framebuffer.color.capacity() == colorCapacityBefore &&
                        framebuffer.depthBuffer.size() == depthSizeBefore &&
                        framebuffer.depthBuffer.capacity() == depthCapacityBefore &&
                        framebuffer.color == colorBefore && framebuffer.depthBuffer == depthBefore;
                }
                check(storageUnchanged && dev.GetTrackedResourceCount() == trackedBefore,
                      "E9g rejection allocates no Software resource and leaves colour/depth "
                      "storage byte-exact at the same address and capacity");

                // The same object must remain a live render target and a readable texture after
                // the refusal. This deliberately performs new rendering before the valid read.
                ProduceTargetPattern(dev, target);
                const std::size_t callsBeforeValid = softwareRenderer != nullptr
                    ? softwareRenderer->GetReadbackCallCountEXT() : 0;
                CheckFullReadback(target, SentinelCD(),
                                  "E9h valid render/GetData after invalid level");
                check(softwareRenderer != nullptr &&
                          softwareRenderer->GetReadbackCallCountEXT() == callsBeforeValid + 1,
                      "E9i the later valid GetData reaches Software production exactly once");
            }
            // REMED-GFX-149 (REMED-GFX-128's expression). This check used to assert the DEFECT --
            // that the whole-level overload rejected every non-zero startIndex with
            // std::runtime_error("no CPU-side pixel data available") -- and so pinned the very
            // behaviour that diverged from XNA, from FNA and from this file's own D7/D8 rectangle
            // checks three lines above. `startIndex` is a DESTINATION element offset on BOTH
            // overloads, so the whole-level read must now land at [3, 3 + total) with the three
            // leading entries and the trailing margin untouched.
            {
                const std::size_t margin = 5;
                std::vector<Color> offset(3 + total + margin, SentinelA5());
                target.GetData(offset.data(), 3, static_cast<int>(total));

                bool exact = true;
                std::string detail;
                for (int y = 0; y < kRTH && exact; ++y)
                    for (int x = 0; x < kRTW && exact; ++x)
                    {
                        const Color actual = offset[3 + static_cast<std::size_t>(y) * kRTW + x];
                        const Color expected = ExpectedTargetPixel(x, y);
                        if (!Same(actual, expected))
                        {
                            exact = false;
                            detail = " first mismatch at (" + std::to_string(x) + "," +
                                     std::to_string(y) + "): " + ColorText(actual) +
                                     "!=" + ColorText(expected);
                        }
                    }
                check(exact, "E10 the whole-level overload honours a non-zero DESTINATION start "
                             "index" + detail);

                std::size_t intact = 0;
                for (std::size_t i = 0; i < 3; ++i)
                    if (Same(offset[i], SentinelA5())) ++intact;
                for (std::size_t i = 3 + total; i < offset.size(); ++i)
                    if (Same(offset[i], SentinelA5())) ++intact;
                check(intact == 3 + margin,
                      "E10b the 3 entries before startIndex and the " + std::to_string(margin) +
                          " after the region kept their sentinel (" + std::to_string(intact) +
                          " intact)");
            }

            // The capacity rule is the same on both overloads: one element short of the region is
            // rejected before any transfer, and a larger capacity is legal and leaves its tail
            // alone.
            check(Throws<std::out_of_range>(
                      [&] { target.GetData(buffer.data(), 0, static_cast<int>(total) - 1); }),
                  "E10c the whole-level overload rejects an elementCount below the region size");
            {
                std::vector<Color> roomy(total + 40, SentinelA5());
                target.GetData(roomy.data(), 0, static_cast<int>(total) + 40);
                std::size_t tail = 0;
                for (std::size_t i = total; i < roomy.size(); ++i)
                    if (Same(roomy[i], SentinelA5())) ++tail;
                check(tail == 40, "E10d excess elementCount is accepted and its tail untouched (" +
                                      std::to_string(tail) + "/40 intact)");
            }

            // The same guards straight against the renderer, where a hostile rectangle can be built
            // that the public layer would have rejected first. All arithmetic there is 64-bit, so a
            // rectangle near INT_MAX must be rejected rather than wrap into an apparently valid one.
            auto* rtRenderer = target.GetRenderTargetRenderer();
            check(rtRenderer != nullptr, "E11 the render target exposes its renderer handle");
            if (rtRenderer != nullptr)
            {
                std::vector<std::uint8_t> raw(total * 4, 0xCD);
                const int rawLength = static_cast<int>(raw.size());
                check(Throws<System::ArgumentNullException>(
                          [&] { rtRenderer->GetData(0, 0, 0, kRTW, kRTH, nullptr, rawLength); }),
                      "E12 renderer rejects a null destination");
                check(Throws<System::NotSupportedException>(
                          [&] { rtRenderer->GetData(2, 0, 0, 4, 4, raw.data(), rawLength); }),
                      "E13 renderer rejects a mip level above 0");
                check(Throws<System::ArgumentOutOfRangeException>(
                          [&] { rtRenderer->GetData(-1, 0, 0, 4, 4, raw.data(), rawLength); }),
                      "E14 renderer rejects a negative mip level");
                check(Throws<System::ArgumentOutOfRangeException>(
                          [&] { rtRenderer->GetData(0, 0, 0, 0, 4, raw.data(), rawLength); }),
                      "E15 renderer rejects an empty rectangle");
                check(Throws<System::ArgumentOutOfRangeException>(
                          [&] { rtRenderer->GetData(0, 0, 0, -4, 4, raw.data(), rawLength); }),
                      "E16 renderer rejects a negative rectangle size");
                check(Throws<System::ArgumentOutOfRangeException>(
                          [&] {
                              rtRenderer->GetData(0, std::numeric_limits<int>::max() - 1, 0, 4, 4,
                                                 raw.data(), rawLength);
                          }),
                      "E17 renderer rejects a rectangle whose right edge would overflow int32");
                check(Throws<System::ArgumentOutOfRangeException>(
                          [&] {
                              rtRenderer->GetData(0, 0, std::numeric_limits<int>::max() - 1, 4, 4,
                                                 raw.data(), rawLength);
                          }),
                      "E18 renderer rejects a rectangle whose bottom edge would overflow int32");
                check(Throws<System::ArgumentOutOfRangeException>(
                          [&] {
                              rtRenderer->GetData(0, 0, 0, kRTW, kRTH, raw.data(),
                                                 kRTW * kRTH * 4 - 1);
                          }),
                      "E19 renderer rejects a destination smaller than the rectangle needs");
                check(Throws<System::ArgumentOutOfRangeException>(
                          [&] {
                              rtRenderer->GetData(0, 0, 0, std::numeric_limits<int>::max(),
                                                 std::numeric_limits<int>::max(), raw.data(),
                                                 rawLength);
                          }),
                      "E20 renderer rejects a rectangle whose byte size would overflow int32");

                // A rejected request must leave the destination completely alone -- the whole point
                // of throwing rather than partially filling it.
                std::size_t intact = 0;
                for (std::uint8_t b : raw)
                    if (b == 0xCD) ++intact;
                check(intact == raw.size(),
                      "E21 every rejected renderer request left the destination untouched (" +
                          std::to_string(intact) + " of " + std::to_string(raw.size()) +
                          " sentinel bytes)");
            }

            // The Software format boundary is the framework-wide one: only SurfaceFormat::Color is
            // implemented, and a render target of any other format fails at CONSTRUCTION rather
            // than reporting a format its storage does not have.
            check(ThrowsAnything([&] {
                      RenderTarget2D bad(dev, 8, 8, false, SurfaceFormat::Bgra4444,
                                         DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
                  }),
                  "E22 a non-Color render target format is rejected at construction");
            check(target.getFormatProperty() == SurfaceFormat::Color,
                  "E23 a supported render target reports SurfaceFormat::Color (4 bytes, RGBA order)");
        }

        // ---- F: the active-target hazard (REMED-GFX-039) ----------------------------------------
        {
            dev.SetRenderTarget(&target);
            check(Throws<System::InvalidOperationException>(
                      [&] { dev.getTexturesProperty()(0, &target); }),
                  "F1 sampling the ACTIVE render target through the texture collection is rejected");
            check(Throws<System::InvalidOperationException>(
                      [&] { dev.getVertexTexturesProperty()(0, &target); }),
                  "F2 the vertex texture collection rejects the active render target too");
            check(Throws<System::InvalidOperationException>([&] { target.Dispose(); }),
                  "F3 disposing the ACTIVE render target is rejected");

            // GetData while the target is still bound: CNA establishes no rejection here, and the
            // corrected storage is the live colour attachment, so this returns the real current
            // contents rather than a stale or fabricated frame. Recorded, not invented.
            std::string detail;
            const std::size_t total = static_cast<std::size_t>(kRTW) * kRTH;
            std::vector<Color> pixels(total, SentinelCD());
            bool ok = true;
            target.GetData(pixels.data(), 0, static_cast<int>(total));
            for (int y = 0; y < kRTH && ok; ++y)
                for (int x = 0; x < kRTW; ++x)
                    if (!Same(pixels[static_cast<std::size_t>(y) * kRTW + x], ExpectedTargetPixel(x, y)))
                    {
                        ok = false;
                        detail = " (" + std::to_string(x) + "," + std::to_string(y) + ")=" +
                                 ColorText(pixels[static_cast<std::size_t>(y) * kRTW + x]);
                        break;
                    }
            check(ok, "F4 GetData while the target is bound returns its live colour attachment" + detail);

            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            check(!ThrowsAnything([&] { dev.getTexturesProperty()(0, &target); }),
                  "F5 the same target is accepted for sampling once it is no longer active");
            dev.getTexturesProperty()(0, nullptr);
            dev.getVertexTexturesProperty()(0, nullptr);
        }

        // ---- G: RenderTargetUsage --------------------------------------------------------------
        {
            RenderTarget2D preserve(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                    RenderTargetUsage::PreserveContents);
            FillTarget(dev, preserve, Color(10, 120, 200, 255));
            std::string detail;
            check(WholeTargetIs(preserve, Color(10, 120, 200, 255), detail),
                  "G1 PreserveContents target holds what was drawn into it" + detail);
            dev.SetRenderTarget(&preserve);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            check(WholeTargetIs(preserve, Color(10, 120, 200, 255), detail),
                  "G2 PreserveContents survives an unbind/rebind cycle with no draw" + detail);

            RenderTarget2D discard(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
            FillTarget(dev, discard, Color(200, 30, 90, 255));
            check(WholeTargetIs(discard, Color(200, 30, 90, 255), detail),
                  "G3 DiscardContents target is readable after the frame that drew it" + detail);
            dev.SetRenderTarget(&discard);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            check(WholeTargetIs(discard, Color(0, 0, 0, 255), detail),
                  "G4 DiscardContents is cleared to opaque black on rebind, as its contract permits" +
                      detail);
        }

        // ---- H: the multisample boundary --------------------------------------------------------
        {
            RenderTarget2D msaa(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 4,
                                RenderTargetUsage::PreserveContents);
            FillTarget(dev, msaa, Color(70, 180, 40, 255));
            std::string detail;
            check(WholeTargetIs(msaa, Color(70, 180, 40, 255), detail),
                  "H1 a target created with a multisample request still reads back exact "
                  "single-sample colour, with no resolve step" + detail);

            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
                sb.Draw(msaa, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 16, 8),
                        Color(255, 255, 255, 255));
                sb.End();
            }
            check(Same(AtBackbuffer(ReadBackbuffer(dev), 8, 4), Color(70, 180, 40, 255)),
                  "H2 a multisample-requested target samples as ordinary single-sample colour");

            // Software implements a real four-sample colour plane and resolves it when the target
            // is unbound (or snapshots it for an active level-zero GetData). The public property
            // therefore reports the same actual sample count used by rasterization.
            check(msaa.getMultiSampleCountProperty() == 4,
                  "H3 Software reports the implemented MultiSampleCount (" +
                      std::to_string(msaa.getMultiSampleCountProperty()) +
                      ")");

            // A clear updates both planes, but rasterization updates only the live samples. Reading
            // before unbind therefore distinguishes a real active-pass resolve from stale `color`.
            dev.SetRenderTarget(&msaa);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            // Primary channels are exact through the byte->float->byte tint path, keeping this an
            // MSAA/readback assertion rather than a floating-point colour-rounding assertion.
            FillRect(dev, Rectangle(0, 0, 16, 8), Color(255, 0, 255, 255));
            const bool activeSnapshotExact =
                WholeTargetIs(msaa, Color(255, 0, 255, 255), detail);
            check(activeSnapshotExact,
                  "H4 GetData snapshots live four-sample raster output while the target remains "
                  "bound" + detail);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
        }

        // ---- I: depth/stencil isolation ---------------------------------------------------------
        {
            // Software's framebuffer always carries a depth buffer and never any stencil storage, so
            // the interesting question is only whether depth work can corrupt or leak into colour.
            RenderTarget2D depth(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                                 RenderTargetUsage::PreserveContents);
            dev.SetRenderTarget(&depth);
            ResetState(dev);
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                      Color(90, 160, 220, 255), 1.0f, 0);
            dev.Clear(ClearOptions::DepthBuffer, Color(255, 0, 255, 255), 0.25f, 0);
            dev.Clear(ClearOptions::Stencil, Color(255, 0, 255, 255), 1.0f, 7);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            std::string detail;
            check(WholeTargetIs(depth, Color(90, 160, 220, 255), detail),
                  "I1 depth-only and stencil-only clears never touch the colour attachment" + detail);

            RenderTarget2D depthStencil(dev, 16, 8, false, SurfaceFormat::Color,
                                        DepthFormat::Depth24Stencil8, 0,
                                        RenderTargetUsage::PreserveContents);
            FillTarget(dev, depthStencil, Color(230, 110, 25, 200));
            check(WholeTargetIs(depthStencil, Color(230, 110, 25, 200), detail),
                  "I2 a Depth24Stencil8 target returns colour, never depth or stencil, through "
                  "GetData" + detail);

            // Sampling a depth-format target must likewise see colour only.
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
                sb.Draw(depthStencil, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 16, 8),
                        Color(255, 255, 255, 255));
                sb.End();
            }
            const std::vector<Color> sampled = ReadBackbuffer(dev);
            check(Same(AtBackbuffer(sampled, 8, 4), Color(230, 110, 25, 200)),
                  "I3 sampling a Depth24Stencil8 target reads its colour attachment: " +
                      ColorText(AtBackbuffer(sampled, 8, 4)));
        }

        // ---- J: target switching, multiple targets and cross-target sampling --------------------
        {
            RenderTarget2D a(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::PreserveContents);
            RenderTarget2D b(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::PreserveContents);
            FillTarget(dev, a, Color(210, 40, 40, 255));
            FillTarget(dev, b, Color(40, 210, 40, 255));

            std::string detail;
            check(WholeTargetIs(a, Color(210, 40, 40, 255), detail) &&
                      WholeTargetIs(b, Color(40, 210, 40, 255), detail),
                  "J1 two live targets keep separate storage -- neither aliases the other" + detail);

            // A -> backbuffer -> A: the backbuffer draw between them must not reach either target.
            ResetState(dev);
            dev.Clear(Color(5, 5, 5, 255));
            check(WholeTargetIs(a, Color(210, 40, 40, 255), detail),
                  "J2 a backbuffer clear does not reach a target that is merely alive" + detail);

            dev.SetRenderTarget(&a);
            ResetState(dev);
            FillRect(dev, Rectangle(0, 0, 4, 4), Color(0, 0, 255, 255));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            {
                std::vector<Color> pixels(static_cast<std::size_t>(kRTW) * kRTH, SentinelA5());
                a.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                const bool corner = Same(pixels[0], Color(0, 0, 255, 255));
                const bool rest = Same(pixels[static_cast<std::size_t>(8) * kRTW + 8],
                                       Color(210, 40, 40, 255));
                check(corner && rest,
                      "J3 rebinding a PreserveContents target adds to its existing content: corner " +
                          ColorText(pixels[0]) + ", interior " +
                          ColorText(pixels[static_cast<std::size_t>(8) * kRTW + 8]));
            }
            check(WholeTargetIs(b, Color(40, 210, 40, 255), detail),
                  "J4 drawing into A left B byte-identical" + detail);

            // Producer A -> consumer B: sample A while B is the active target.
            dev.SetRenderTarget(&b);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
                sb.Draw(a, Rectangle(0, 0, kRTW, kRTH), Rectangle(0, 0, kRTW, kRTH),
                        Color(255, 255, 255, 255));
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            {
                std::vector<Color> pixels(static_cast<std::size_t>(kRTW) * kRTH, SentinelCD());
                b.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                check(Same(pixels[0], Color(0, 0, 255, 255)) &&
                          Same(pixels[static_cast<std::size_t>(8) * kRTW + 8], Color(210, 40, 40, 255)),
                      "J5 target A sampled into target B while B was active: corner " +
                          ColorText(pixels[0]) + ", interior " +
                          ColorText(pixels[static_cast<std::size_t>(8) * kRTW + 8]));
            }

            // Consecutive readbacks must agree exactly.
            {
                std::vector<Color> first(static_cast<std::size_t>(kRTW) * kRTH, SentinelCD());
                std::vector<Color> second(static_cast<std::size_t>(kRTW) * kRTH, SentinelA5());
                a.GetData(first.data(), 0, static_cast<int>(first.size()));
                a.GetData(second.data(), 0, static_cast<int>(second.size()));
                std::size_t differing = 0;
                for (std::size_t i = 0; i < first.size(); ++i)
                    if (!Same(first[i], second[i])) ++differing;
                check(differing == 0,
                      "J6 consecutive GetData calls return identical data from two differently "
                      "pre-filled destinations (" + std::to_string(differing) + " differing)");
            }

            // Disposing one target must not disturb the other.
            b.Dispose();
            {
                std::vector<Color> pixels(static_cast<std::size_t>(kRTW) * kRTH, SentinelCD());
                a.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                check(Same(pixels[0], Color(0, 0, 255, 255)) &&
                          Same(pixels[static_cast<std::size_t>(8) * kRTW + 8], Color(210, 40, 40, 255)),
                      "J8 disposing one target leaves the other's storage intact");
            }
            check(ThrowsAnything([&] {
                      std::vector<Color> pixels(static_cast<std::size_t>(kRTW) * kRTH, SentinelCD());
                      b.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                  }),
                  "J9 reading a disposed target fails deterministically instead of returning stale "
                  "or fabricated pixels");
        }

        // ---- K: repeated create/draw/read/sample/dispose cycles ---------------------------------
        {
            bool stable = true;
            std::string detail;
            for (int cycle = 0; cycle < 24 && stable; ++cycle)
            {
                const Color tint(static_cast<std::uint8_t>(10 + cycle * 9),
                                 static_cast<std::uint8_t>(200 - cycle * 5),
                                 static_cast<std::uint8_t>(30 + cycle * 3),
                                 static_cast<std::uint8_t>(255 - cycle));
                RenderTarget2D scratch(dev, 12, 6, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                       RenderTargetUsage::PreserveContents);
                FillTarget(dev, scratch, tint);
                if (!WholeTargetIs(scratch, tint, detail))
                {
                    stable = false;
                    detail = " cycle " + std::to_string(cycle) + detail;
                    break;
                }
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                {
                    SpriteBatch sb(dev);
                    SamplerState point = SamplerState::PointClamp;
                    sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
                    sb.Draw(scratch, Rectangle(0, 0, 12, 6), Rectangle(0, 0, 12, 6),
                            Color(255, 255, 255, 255));
                    sb.End();
                }
                const std::vector<Color> frame = ReadBackbuffer(dev);
                if (!Same(AtBackbuffer(frame, 6, 3), tint))
                {
                    stable = false;
                    detail = " cycle " + std::to_string(cycle) + " sampled " +
                             ColorText(AtBackbuffer(frame, 6, 3)) + " != " + ColorText(tint);
                    break;
                }
                scratch.Dispose();
            }
            check(stable,
                  "K1 24 create/draw/read/sample/dispose cycles each produced their own exact "
                  "colour" + detail);
        }

        // ---- L: sampler parity with an identical plain texture -----------------------------------
        {
            const Texture2D plain = MakePatternTexture(dev);
            const Rectangle whole(0, 0, kRTW, kRTH);
            CheckSamplerParity(dev, target, plain, SamplerState::PointClamp,
                               Rectangle(0, 0, kRTW, kRTH), whole, SpriteEffects::None,
                               "L1 PointClamp at 1:1");
            CheckSamplerParity(dev, target, plain, SamplerState::PointClamp,
                               Rectangle(0, 0, kBBW, kBBH), whole, SpriteEffects::None,
                               "L2 PointClamp magnified 2x");
            CheckSamplerParity(dev, target, plain, SamplerState::LinearClamp,
                               Rectangle(0, 0, kBBW, kBBH), whole, SpriteEffects::None,
                               "L3 LinearClamp magnified 2x");
            CheckSamplerParity(dev, target, plain, SamplerState::PointWrap,
                               Rectangle(0, 0, kBBW, kBBH), Rectangle(-8, -4, kRTW + 16, kRTH + 8),
                               SpriteEffects::None,
                               "L4 PointWrap with a source rectangle outside the texture");
            CheckSamplerParity(dev, target, plain, SamplerState::LinearWrap,
                               Rectangle(0, 0, kRTW, kRTH), Rectangle(4, 2, 12, 9),
                               SpriteEffects::None, "L5 LinearWrap over a source sub-rectangle");
            CheckSamplerParity(dev, target, plain, SamplerState::PointClamp,
                               Rectangle(0, 0, kRTW, kRTH), whole, SpriteEffects::FlipHorizontally,
                               "L6 horizontally flipped UVs");
            CheckSamplerParity(dev, target, plain, SamplerState::PointClamp,
                               Rectangle(0, 0, kRTW, kRTH), whole, SpriteEffects::FlipVertically,
                               "L7 vertically flipped UVs");
            CheckSamplerParity(dev, target, plain, SamplerState::PointClamp,
                               Rectangle(3, 5, 17, 11), Rectangle(1, 1, kRTW - 2, kRTH - 2),
                               SpriteEffects::None, "L8 offset destination and inset source");

            // Row orientation, stated directly rather than only implied by parity: the block that was
            // drawn at the TOP of the target must sample at the TOP of the frame.
            const std::vector<Color> frame =
                SampleFrame(dev, target, SamplerState::PointClamp, Rectangle(0, 0, kRTW, kRTH),
                            whole, SpriteEffects::None);
            check(Same(AtBackbuffer(frame, 3, 3), BlockTopLeft()) &&
                      Same(AtBackbuffer(frame, 3, 12), BlockBottomLeft()) &&
                      Same(AtBackbuffer(frame, 28, 3), BlockTopRight()) &&
                      Same(AtBackbuffer(frame, 28, 12), BlockBottomRight()),
                  "L9 sampled rows are top-first and columns are left-first, with the alpha channel "
                  "carried through: top-left " + ColorText(AtBackbuffer(frame, 3, 3)) +
                      ", bottom-right " + ColorText(AtBackbuffer(frame, 28, 12)));
        }

        // The original target must still be exactly what it was after everything above.
        CheckFullReadback(target, SentinelCD(), "M1 original target after the whole matrix");

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SoftwareRenderTargetReadbackTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareRenderTargetReadbackTest game;
    game.Run();
    return game.getResult();
}
