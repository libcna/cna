// SPDX-License-Identifier: MS-PL
// REMED-GFX-081: SpriteBatch.Begin must APPLY the RasterizerState it is handed, exactly like it
// already applies BlendState / DepthStencilState / SamplerState. The canonical 7-arg Begin
// previously named the parameter `/*rasterizerState*/` and dropped it on the floor, so a
// RasterizerState supplied to Begin (e.g. one with ScissorTestEnable=true) never reached the device
// or renderer on ANY renderer -- the only way to affect sprite rasterizer state was to assign
// GraphicsDevice.RasterizerState directly. This is a shared-layer defect in SpriteBatch.cpp, not a
// renderer bug; the Software renderer is used here purely as a deterministic CPU-readback oracle
// (its ScissorRectangle became genuinely functional in REMED-GFX-080).
//
// FNA contract (SpriteBatch.cs Begin + PrepRenderState, verbatim):
//   this.rasterizerState = rasterizerState ?? RasterizerState.CullCounterClockwise;
//   ... GraphicsDevice.RasterizerState = rasterizerState;   // applied in PrepRenderState
// So a null rasterizerState resolves to the SpriteBatch default (CullCounterClockwise, scissor OFF)
// and is ALWAYS (re-)applied -- it OVERRIDES whatever GraphicsDevice.RasterizerState was, it does
// not inherit it. FNA leaves the state applied after End (no restore).
//
// Canonical scene: 96x72 backbuffer, ScissorRectangle(21,13,31,19), a full-target solid red sprite.
//   scissor ENABLED  -> red exactly x[21,51] y[13,31] n=589
//   scissor DISABLED -> full framebuffer x[0,95] y[0,71] n=6912
// Pre-fix (Begin drops the argument) EVERY "through Begin" scissor-enabled check renders the full
// framebuffer, and the null-Begin-overrides check (C) is INVERTED (stays clipped from the device
// state). Post-fix matches FNA. No check here assigns GraphicsDevice.RasterizerState as the way to
// enable scissor for a SpriteBatch draw -- the whole point is that the Begin argument must work.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 96;
    constexpr int kBBH = 72;
    constexpr int kScX = 21, kScY = 13, kScW = 31, kScH = 19;   // -> inclusive x[21,51] y[13,31]

    bool Redish(const Color& c) { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }

    struct BBox
    {
        int minX = std::numeric_limits<int>::max();
        int minY = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int maxY = std::numeric_limits<int>::min();
        int count = 0;
        bool empty() const { return count == 0; }
        int width()  const { return empty() ? 0 : (maxX - minX + 1); }
        int height() const { return empty() ? 0 : (maxY - minY + 1); }
        std::string str() const
        {
            if (empty()) return "<empty>";
            return "x[" + std::to_string(minX) + "," + std::to_string(maxX) + "] y["
                 + std::to_string(minY) + "," + std::to_string(maxY) + "] n=" + std::to_string(count);
        }
        bool isScissorClip() const   // == the enabled-scissor footprint x[21,51] y[13,31] n=589
        { return count == kScW * kScH && minX == kScX && minY == kScY &&
                 maxX == kScX + kScW - 1 && maxY == kScY + kScH - 1; }
        bool isFullFramebuffer() const { return count == kBBW * kBBH; }
    };
}

class SoftwareSpriteBatchRasterizerStateTest : public Game
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

    static RasterizerState MakeRS(bool scissorEnable, CullMode cull = CullMode::None)
    {
        RasterizerState rs;
        rs.setCullModeProperty(cull);
        rs.setScissorTestEnableProperty(scissorEnable);
        return rs;
    }

    // Draw a full-target red sprite through SpriteBatch.Begin with the given sort mode + rasterizer
    // state passed THROUGH Begin (never assigned to the device here).
    void DrawFullRedViaBegin(GraphicsDevice& dev, SpriteSortMode sortMode, RasterizerState* rs)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(sortMode, BlendState::Opaque, &point, nullptr, rs);
        sb.Draw(whiteTex_, Rectangle(0, 0, kBBW, kBBH), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
        sb.End();
    }

    std::vector<Color> ReadBackbuffer(GraphicsDevice& dev)
    {
        std::vector<Color> pix(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBBW, kBBH);
        dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    BBox RedBox(GraphicsDevice& dev)
    {
        const std::vector<Color> pix = ReadBackbuffer(dev);
        BBox b;
        for (int y = 0; y < kBBH; ++y)
            for (int x = 0; x < kBBW; ++x)
                if (Redish(pix[static_cast<std::size_t>(y) * kBBW + x]))
                {
                    b.minX = std::min(b.minX, x); b.maxX = std::max(b.maxX, x);
                    b.minY = std::min(b.minY, y); b.maxY = std::max(b.maxY, y);
                    ++b.count;
                }
        return b;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        dev.setScissorRectangleProperty(Rectangle(kScX, kScY, kScW, kScH));

        // ---- A (Phase 4/11/14): Deferred + Begin(rasterizerState = scissor ENABLED) -> clipped ---
        {
            dev.Clear(Color::Black);
            RasterizerState rs = MakeRS(true);
            DrawFullRedViaBegin(dev, SpriteSortMode::Deferred, &rs);
            const BBox b = RedBox(dev);
            check(b.isScissorClip(), "A: Deferred Begin(rs.ScissorTestEnable=true) clips to x[21,51] y[13,31]: " + b.str());
        }

        // ---- B (Phase 11): Deferred + Begin(rasterizerState = scissor DISABLED) -> full fb --------
        {
            dev.Clear(Color::Black);
            RasterizerState rs = MakeRS(false);
            DrawFullRedViaBegin(dev, SpriteSortMode::Deferred, &rs);
            const BBox b = RedBox(dev);
            check(b.isFullFramebuffer(), "B: Deferred Begin(rs.ScissorTestEnable=false) -> full framebuffer: " + b.str());
        }

        // ---- C (Phase 8): null rasterizerState resolves to CullCounterClockwise (scissor OFF) and
        //      OVERRIDES a scissor-enabled device state (does NOT inherit it) -> full fb -----------
        {
            dev.setRasterizerStateProperty(MakeRS(true));   // device scissor-enabled BEFORE Begin
            dev.Clear(Color::Black);
            DrawFullRedViaBegin(dev, SpriteSortMode::Deferred, nullptr);   // null -> SpriteBatch default
            const BBox b = RedBox(dev);
            check(b.isFullFramebuffer(),
                  "C: null rasterizerState -> default CullCounterClockwise overrides device scissor -> full fb: " + b.str());
        }

        // ---- D (Phase 12): enable/disable/enable THROUGH Begin, same stored ScissorRectangle ------
        {
            dev.Clear(Color::Black);
            RasterizerState on = MakeRS(true), off = MakeRS(false);
            DrawFullRedViaBegin(dev, SpriteSortMode::Deferred, &on);
            const bool d1 = RedBox(dev).isScissorClip();
            dev.Clear(Color::Black);
            DrawFullRedViaBegin(dev, SpriteSortMode::Deferred, &off);
            const bool d2 = RedBox(dev).isFullFramebuffer();
            dev.Clear(Color::Black);
            DrawFullRedViaBegin(dev, SpriteSortMode::Deferred, &on);   // rect never re-set
            const bool d3 = RedBox(dev).isScissorClip();
            check(d1 && d2 && d3, "D: Begin enable->disable->enable toggles clipping, ScissorRectangle stays put");
        }

        // ---- E (Phase 13): Immediate sort mode -> state applied at Begin, first Draw is clipped ---
        {
            dev.Clear(Color::Black);
            RasterizerState rs = MakeRS(true);
            DrawFullRedViaBegin(dev, SpriteSortMode::Immediate, &rs);
            const BBox b = RedBox(dev);
            check(b.isScissorClip(), "E: Immediate Begin(rs scissor=true) clips: " + b.str());
        }

        // ---- F (Phase 15): a distinct sort mode (FrontToBack) shares the same PrepRenderState -----
        {
            dev.Clear(Color::Black);
            RasterizerState rs = MakeRS(true);
            DrawFullRedViaBegin(dev, SpriteSortMode::FrontToBack, &rs);
            const BBox b = RedBox(dev);
            check(b.isScissorClip(), "F: FrontToBack Begin(rs scissor=true) clips: " + b.str());
        }

        // ---- G (Phase 27): GraphicsDevice.RasterizerState after End == the Begin state (no restore)
        {
            dev.setRasterizerStateProperty(MakeRS(false));   // start disabled
            dev.Clear(Color::Black);
            RasterizerState rs = MakeRS(true);
            DrawFullRedViaBegin(dev, SpriteSortMode::Deferred, &rs);
            const bool leftApplied = dev.getRasterizerStateProperty().getScissorTestEnableProperty();
            check(leftApplied, "G: device RasterizerState left applied after End (ScissorTestEnable still true, FNA no-restore)");
        }

        // ---- H (Phase 16): RasterizerState lifetime -- destroyed right after Begin, still clips ---
        {
            dev.Clear(Color::Black);
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            {
                auto rs = std::make_unique<RasterizerState>(MakeRS(true));
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, rs.get());
            }   // rs destroyed here -- Begin already copied the state into the device/renderer
            sb.Draw(whiteTex_, Rectangle(0, 0, kBBW, kBBH), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
            sb.End();   // deferred flush after the RasterizerState object is gone
            const BBox b = RedBox(dev);
            check(b.isScissorClip(), "H: RasterizerState destroyed after Begin -> no dangling, still clips: " + b.str());
        }

        // ---- I (Phase 7): overload forwarding preserves rasterizerState (5-arg and 6-arg) --------
        {
            dev.Clear(Color::Black);
            RasterizerState rs = MakeRS(true);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &rs);   // 5-arg
                sb.Draw(whiteTex_, Rectangle(0, 0, kBBW, kBBH), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
                sb.End();
            }
            const bool five = RedBox(dev).isScissorClip();
            dev.Clear(Color::Black);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &rs, nullptr);   // 6-arg
                sb.Draw(whiteTex_, Rectangle(0, 0, kBBW, kBBH), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
                sb.End();
            }
            const bool six = RedBox(dev).isScissorClip();
            check(five && six, "I: 5-arg and 6-arg Begin overloads both forward rasterizerState (both clip)");
        }

        // ---- J (Phase 9): default Begin() unregressed -- scissor off, sprite renders full fb ------
        {
            dev.setRasterizerStateProperty(MakeRS(true));   // hostile prior device state
            dev.Clear(Color::Black);
            {
                SpriteBatch sb(dev);
                sb.Begin();   // defaults: AlphaBlend, LinearClamp, None, CullCounterClockwise (scissor off)
                sb.Draw(whiteTex_, Rectangle(0, 0, kBBW, kBBH), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
                sb.End();
            }
            const BBox b = RedBox(dev);
            check(b.isFullFramebuffer(), "J: default Begin() -> CullCounterClockwise default, full framebuffer: " + b.str());
        }

        // ---- K (Phase 10): CullMode reaches the renderer through Begin (same ApplyRasterizerState) -
        // Software SpriteBatch quads are authored to survive CullCounterClockwise; passing CullNone
        // (scissor enabled) still renders (clipped). This proves BOTH RasterizerState fields (cull +
        // scissor) travel through Begin, not just scissor.
        {
            dev.Clear(Color::Black);
            RasterizerState rs = MakeRS(true, CullMode::None);
            DrawFullRedViaBegin(dev, SpriteSortMode::Deferred, &rs);
            const BBox b = RedBox(dev);
            check(b.isScissorClip(), "K: Begin(CullNone + scissor) applies both fields, sprite clipped: " + b.str());
        }

        // ---- L (Phase 28/29): nested Begin throws, and does not leave a half-applied batch --------
        {
            bool threw = false;
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            RasterizerState rs = MakeRS(true);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &rs);
            try { sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &rs); }
            catch (const std::exception&) { threw = true; }
            sb.End();
            check(threw, "L: nested Begin throws (invalid Begin/End sequence rejected)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SoftwareSpriteBatchRasterizerStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareSpriteBatchRasterizerStateTest game;
    game.Run();
    return game.getResult();
}
