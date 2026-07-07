// SPDX-License-Identifier: MS-PL
// Task 906: verify RenderTarget2D's mip chain is genuinely generated with real, correctly
// downsampled content on Bgfx (not just present-but-undefined storage).
//
// Direct port of examples/vulkan_rendertarget2d_mip_test.cpp (Task 878) -- same methodology, same
// kRTSize, same 7:1-split/forced-centre-sample assertions. See that file's header comment for the
// full rationale (including why an earlier 50/50-split version of this methodology was a false
// positive). Summarized here: render a 7:1 asymmetric red/blue split into a mipMap=true
// RenderTarget2D, then sample it back two ways -- (1) at native 1:1 size, which must stay crisp
// (level 0 only); (2) scaled to a 1x1 destination (64:1 minification, log2(64)=6 exactly), which
// always samples the texture's exact centre (deep inside the red region) and forces the GPU's
// automatic LOD selection to the single coarsest mip level, whose content must be the whole
// image's true 7:1 weighted average (~223,0,32) -- NOT pure red.
//
// Unlike Vulkan (a hand-written vkCmdBlitImage cascade), Bgfx's mip regeneration is a real,
// built-in bgfx feature: bgfx::createTexture2D's hasMips=true plus the framebuffer-attachment
// default resolve flag (BGFX_RESOLVE_AUTO_GEN_MIPS, confirmed in bgfx.cpp's createFrameBuffer)
// makes bgfx call the platform's own mip-generation primitive (e.g. glGenerateMipmap on the GL
// renderer, confirmed in renderer_gl.cpp's TextureGL::resolve()) automatically, every time bgfx
// switches away from that framebuffer -- exactly the trigger point RenderTarget2D's own
// SetRenderTarget(nullptr) call reaches. No custom shader or geometry needed on this backend,
// unlike Vulkan's Task 878 fix -- a correction to that task's own research finding ("bgfx has no
// glGenerateMipmap equivalent"), which was only true of bgfx::blit() (a same-size copy, still not
// a resize/filter primitive), not of the framebuffer-resolve path.
//
// Bgfx-only test-harness finding (separate from the mip work itself): sampling the SAME
// RenderTarget2D object via SpriteBatch across more than one independent Clear+Draw+
// GetBackBufferData cycle only reliably produces fresh data on the FIRST such cycle -- every
// later cycle reads back solid black no matter how many retries, even with mipMap=false
// (confirmed empirically, so this is unrelated to Task 906's own change). Every other multi-
// checkpoint Bgfx pixel test in this project avoids the pattern by constructing a fresh
// RenderTarget2D per checkpoint (see bgfx_rendertarget2d_msaa_test.cpp's RenderAndReadRow) rather
// than reusing one persistent instance -- this test follows that same established, proven-working
// shape instead of reusing a single rt_ across all 3 checkpoints.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 64;

class BgfxRenderTarget2DMipTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    Texture2D                              patternTex_;
    int result_ = 1;

    // Renders the 7:1 red/blue split into a FRESH RenderTarget2D, unbinds it (triggering bgfx's
    // own auto-mip-regeneration), then draws it back scaled to destW x destH and reads back a
    // single column of destH pixels at column sampleX -- retried until non-black (Bgfx's
    // first-read-per-frame quirk). A fresh RT per call (mirroring
    // bgfx_rendertarget2d_msaa_test.cpp's RenderAndReadRow) and exactly ONE GetBackBufferData
    // call per independent render (mirroring every other multi-point Bgfx pixel test in this
    // project, e.g. bgfx_viewport_subregion_test.cpp's renderAndReadFresh) avoids a harness-only
    // issue found while writing this test: sampling the same already-rendered RT/texture a
    // SECOND time via a separate Clear+Draw+Read cycle does not reliably reproduce the first
    // cycle's correct content, regardless of retries -- confirmed unrelated to Task 906's own
    // mip fix (reproduces identically with mipMap=false).
    std::vector<Color> RenderIntoRTAndReadColumn(GraphicsDevice& device, SamplerState& sampler,
                                                  int destW, int destH, int sampleX)
    {
        RenderTarget2D rt(device, kRTSize, kRTSize, /*mipMap=*/true,
                           SurfaceFormat::Color, DepthFormat::None);

        device.SetRenderTarget(&rt);
        SamplerState point = SamplerState::PointClamp;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb_->Draw(patternTex_, Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> column(destH, Color(0, 0, 0, 0));
        for (int i = 0; i < 20; ++i)
        {
            device.Clear(Color(0, 0, 0, 255));
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr);
            sb_->Draw(rt, Rectangle(0, 0, destW, destH), Rectangle(0, 0, kRTSize, kRTSize),
                      Color::White);
            sb_->End();

            const Rectangle reg(sampleX, 0, 1, destH);
            device.GetBackBufferData(&reg, column.data(), 0, destH);
            bool anyNonBlack = false;
            for (const auto& c : column)
                if (c.getRProperty() > 10 || c.getGProperty() > 10 || c.getBProperty() > 10)
                { anyNonBlack = true; break; }
            if (anyNonBlack) break;
        }
        return column;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        // 1x8: rows 0..6 = red, row 7 = blue -- point-stretched to fill the 64-tall RT below,
        // giving an exact 56-red-rows/8-blue-rows (7:1) split with no seam aliasing (64 is an
        // integer multiple of 8).
        std::vector<uint8_t> pattern;
        pattern.reserve(8 * 4);
        for (int i = 0; i < 7; ++i) { pattern.insert(pattern.end(), {255, 0, 0, 255}); }
        pattern.insert(pattern.end(), {0, 0, 255, 255});
        patternTex_ = Texture2D::CreateFromPixels(device, 1, 8, pattern);
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        SamplerState point = SamplerState::PointClamp;
        SamplerState linear = SamplerState::LinearClamp;

        // --- Check 1: native 1:1 size -- no minification, level 0 only, must stay crisp. One
        // single column read (destH pixels in one GetBackBufferData call) covers both sample
        // points -- row 16 (well inside 0..55 red) and row 60 (well inside 56..63 blue). ---
        const std::vector<Color> column = RenderIntoRTAndReadColumn(device, point, kRTSize,
                                                                     kRTSize, kRTSize / 2);
        const Color topPx = column[16];
        const Color botPx = column[60];

        const bool topCrisp = topPx.getRProperty() >= 200 && topPx.getBProperty() <= 30;
        const bool botCrisp = botPx.getBProperty() >= 200 && botPx.getRProperty() <= 30;
        const bool check1 = topCrisp && botCrisp;

        // --- Check 2: scaled down to a 1x1 destination (64:1 minification, log2(64)=6 exactly)
        // -- always samples the texture's exact centre (row 32, deep inside the red region),
        // forced to the single coarsest mip level, whose content (per bgfx's own auto-generated
        // mip chain) is the whole image's true 7:1 weighted average, ~(223,0,32) -- NOT pure
        // red. A second, fully independent fresh-RT render (mirrors the MSAA test's own 2nd
        // RenderAndReadRow call, a pattern already proven to work reliably). ---
        const Color avgPx = RenderIntoRTAndReadColumn(device, linear, 1, 1, 0)[0];

        const bool check2 = avgPx.getRProperty() >= 190 && avgPx.getRProperty() <= 240 &&
                            avgPx.getBProperty() >= 15  && avgPx.getBProperty() <= 55  &&
                            avgPx.getGProperty() <= 20;

        std::printf("[%s] Check 1 (1:1, level 0 only): top=(%d,%d,%d) bottom=(%d,%d,%d), "
                    "expected crisp red/blue\n",
                    check1 ? "PASS" : "FAIL",
                    topPx.getRProperty(), topPx.getGProperty(), topPx.getBProperty(),
                    botPx.getRProperty(), botPx.getGProperty(), botPx.getBProperty());
        std::printf("[%s] Check 2 (64:1 minification, coarsest mip, forced centre sample): "
                    "sample=(%d,%d,%d), expected ~7:1 weighted average (223,0,32), not pure red\n",
                    check2 ? "PASS" : "FAIL",
                    avgPx.getRProperty(), avgPx.getGProperty(), avgPx.getBProperty());

        result_ = (check1 && check2) ? 0 : 1;
        Exit();
    }

public:
    BgfxRenderTarget2DMipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kRTSize);
        gdm_->setPreferredBackBufferHeightProperty(kRTSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxRenderTarget2DMipTest game;
    game.Run();
    return game.getResult();
}
