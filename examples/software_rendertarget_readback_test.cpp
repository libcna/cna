// SPDX-License-Identifier: MS-PL
// REMED-GFX-124: a Software (CPU-raster) RenderTarget2D must be readable and sampleable once it is
// no longer the active target -- the ordinary XNA/FNA producer -> unbind -> consumer contract every
// GPU backend already honors.
//
// Public contract under test:
//   * RenderTarget2D::GetData returns the target's rendered COLOUR attachment (full level, arbitrary
//     rectangle, arbitrary destination start index), never leaving the caller's memory untouched.
//   * After unbinding, the same target samples as an ordinary Texture2D -- through SpriteBatch and
//     through a textured 3D primitive alike -- reproducing its real pixels, not a fallback colour.
//
// Pre-fix Software signature (this test FAILS pre-fix, two independent causes):
//   A  readback: SoftwareRenderTargetBackend overrides no ITextureBackend::GetData, so
//      Texture2D::GetData's render-target fallback reaches the interface's DEFAULT no-op. The
//      backend writes nothing -- but the buffer it was handed is a scratch vector Texture2D
//      ZERO-INITIALIZED itself, and those zeros are then converted into the caller's Color array.
//      The observable public result is therefore not an untouched destination but a fabricated,
//      fully written, uniformly transparent-black frame, which is strictly worse: a "did GetData
//      write anything?" sentinel check passes, and any assertion whose expected content is
//      transparent black passes on an empty target.
//   B  sampling: SoftwareRenderTargetBackend is not a SoftwareTextureBackend, so both sampler entry
//      points (SoftwareSpriteBatchBackend::Draw and RasterizeTriangleShaded) dynamic_cast it to
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
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
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
        // Texture2D::GetData's render-target fallback hands the backend a scratch buffer it
        // zero-initialized itself, so a no-op backend still yields a fully written destination -- of
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
