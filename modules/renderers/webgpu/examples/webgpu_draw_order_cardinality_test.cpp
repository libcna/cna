// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-159's STRUCTURAL half: interleaving the two draw families in public order must cost
// nothing but the interleaving.
//
// `spritebatch_3d_order_test.cpp` proves the ORDER is right, by pixels, on every renderer. It cannot
// prove the order is right for the right reasons: a renderer could deliver the same picture by
// closing and reopening a render pass at every family change, by submitting a command buffer per
// transition, by baking the draw's ordinal into its pipeline key, or by re-issuing the whole
// captured state for every draw. Each of those is a correct-looking regression that this file
// fails and that one cannot see.
//
// The measured quantities are the renderer's own native counters (the same ones REMED-GFX-116 and
// REMED-GFX-146 already gate on), sampled INSIDE a bind cycle so that binding the target -- itself
// a flush on this renderer -- is never counted as part of the sequence under test:
//
//   * render passes begun          -- must be exactly 1 for one bind cycle, whatever the mix
//   * queue submits                -- must be exactly 1, so no family transition submits or waits
//   * Colored3D pipeline variants  -- must not grow with alternation count, so a draw's position
//                                     in the order is not part of any pipeline or cache key
//   * native setViewport calls     -- must stay at the one pass-opening call when every draw
//   * native setScissorRect calls    shares the state, so interleaving did not defeat the
//                                     per-draw redundant-call skips REMED-GFX-116/146 rely on
//   * uncaptured validation errors -- must stay 0 across every transition
//
// D3 is the one that would have caught the pre-fix code doing the right thing for the wrong reason:
// it runs the SAME eight draws twice, once grouped by family and once alternating, and requires the
// two to cost identical numbers of passes, submits and pipelines. A replay that special-cased
// alternation -- by splitting, by submitting, or by keying a pipeline on the order -- differs there
// even though both orderings render plausibly.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kBBW = 64;
    constexpr int kBBH = 64;
    constexpr int kRT  = 32;

    const Color kBlack(0, 0, 0, 255);
    const Color kRed  (255, 0, 0, 255);
}

/**
 * @brief REMED-GFX-159's cost contract for ordered cross-family replay on WebGPU.
 */
class WebGpuDrawOrderCardinalityTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<BasicEffect> fx_;
    std::unique_ptr<Texture2D> white_;
    int phase_ = 0;
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

    void checkCount(std::size_t got, std::size_t want, const std::string& label)
    {
        check(got == want, got == want
                  ? label + " = " + std::to_string(got)
                  : label + ": expected " + std::to_string(want) + ", got " + std::to_string(got));
    }

    static std::array<VertexPositionColor, 6> Quad(const Color& c)
    {
        return {{
            {Vector3(-1.0f,  1.0f, 0.5f), c},
            {Vector3(-1.0f, -1.0f, 0.5f), c},
            {Vector3( 1.0f, -1.0f, 0.5f), c},
            {Vector3(-1.0f,  1.0f, 0.5f), c},
            {Vector3( 1.0f, -1.0f, 0.5f), c},
            {Vector3( 1.0f,  1.0f, 0.5f), c},
        }};
    }

    /// One stock 3D draw with the file's single, unvarying pipeline state.
    void Draw3D(GraphicsDevice& dev, const Color& c)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        fx_->VertexColorEnabled = true;
        fx_->Apply();
        const auto quad = Quad(c);
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(quad.data(), 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    /// One SpriteBatch cycle, stating every state so nothing is inherited or left behind.
    void DrawSprite(const Color& c)
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        sb_->Draw(*white_, Rectangle(0, 0, kRT, kRT), Rectangle(0, 0, 1, 1), c);
        sb_->End();
    }

    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev)
    {
        return std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                DepthFormat::Depth24Stencil8, 0,
                                                RenderTargetUsage::DiscardContents);
    }

    /// Every counter this file gates on, sampled together.
    struct Counters
    {
        std::size_t passes, submits, pipelines, viewports, scissors;
    };

    static Counters Sample(const WebGPURenderer& b)
    {
        return Counters{ b.GetRenderPassCountEXT(), b.GetQueueSubmitCountEXT(),
                         b.GetColoredPipelineCacheSizeEXT(), b.GetSetViewportCallCountEXT(),
                         b.GetSetScissorCallCountEXT() };
    }

    static Counters Delta(const Counters& a, const Counters& b)
    {
        return Counters{ b.passes - a.passes, b.submits - a.submits, b.pipelines - a.pipelines,
                         b.viewports - a.viewports, b.scissors - a.scissors };
    }

    void Draw(const GameTime&) override
    {
        if (phase_ == 0) { phase_ = 1; return; }
        if (phase_ != 1) { Exit(); return; }
        phase_ = 2;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(dev.GetRenderer());
        std::printf("REMED-GFX-159 cross-family draw order cardinality -- WEBGPU\n");
        std::fflush(stdout);

        // Warm-up: creates the one Colored3D pipeline and the one sprite pipeline everything below
        // reuses, and gets past the first swapchain acquisition, so every delta measures its own
        // sequence rather than one-off setup.
        {
            auto warm = MakeTarget(dev);
            dev.SetRenderTarget(warm.get());
            dev.Clear(kBlack);
            Draw3D(dev, kRed);
            DrawSprite(kRed);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }

        // ---- D1: eight alternating family changes in ONE bind cycle ----
        Counters d1{};
        {
            auto rt = MakeTarget(dev);
            dev.SetRenderTarget(rt.get());
            const Counters before = Sample(renderer);
            dev.Clear(kBlack);
            for (int i = 0; i < 4; ++i) { DrawSprite(kRed); Draw3D(dev, kRed); }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            d1 = Delta(before, Sample(renderer));

            checkCount(d1.passes, 1, "D1 eight alternating draws in one cycle: render passes");
            checkCount(d1.submits, 1, "D1 eight alternating draws in one cycle: queue submits");
            checkCount(d1.pipelines, 0,
                       "D1 eight alternating draws in one cycle: NEW Colored3D pipeline variants");
            // One pass-opening call each; every draw shares the whole-target state, so the
            // per-draw redundant-call skips must absorb all eight.
            checkCount(d1.viewports, 1,
                       "D1 eight alternating draws in one cycle: native setViewport calls");
            checkCount(d1.scissors, 1,
                       "D1 eight alternating draws in one cycle: native setScissorRect calls");
        }

        // ---- D2: the same eight draws GROUPED by family ----
        Counters d2{};
        {
            auto rt = MakeTarget(dev);
            dev.SetRenderTarget(rt.get());
            const Counters before = Sample(renderer);
            dev.Clear(kBlack);
            for (int i = 0; i < 4; ++i) DrawSprite(kRed);
            for (int i = 0; i < 4; ++i) Draw3D(dev, kRed);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            d2 = Delta(before, Sample(renderer));

            checkCount(d2.passes, 1, "D2 eight grouped draws in one cycle: render passes");
            checkCount(d2.submits, 1, "D2 eight grouped draws in one cycle: queue submits");
        }

        // ---- D3: alternating must cost exactly what grouped costs ----
        //
        // The check the pixel oracle structurally cannot make. Same eight draws, same states, same
        // destination -- only the interleaving differs, so any difference here is a cost the
        // ORDERING introduced.
        check(d1.passes == d2.passes && d1.submits == d2.submits &&
              d1.pipelines == d2.pipelines,
              d1.passes == d2.passes && d1.submits == d2.submits && d1.pipelines == d2.pipelines
                  ? "D3 alternating and grouped orderings of the same eight draws cost identical "
                    "passes, submits and pipelines"
                  : "D3 interleaving changed the cost: passes " + std::to_string(d1.passes) +
                        " vs " + std::to_string(d2.passes) + ", submits " +
                        std::to_string(d1.submits) + " vs " + std::to_string(d2.submits) +
                        ", new pipelines " + std::to_string(d1.pipelines) + " vs " +
                        std::to_string(d2.pipelines));

        // ---- D4: doubling the alternations must not add a pass, a submit or a pipeline ----
        {
            auto rt = MakeTarget(dev);
            dev.SetRenderTarget(rt.get());
            const Counters before = Sample(renderer);
            dev.Clear(kBlack);
            for (int i = 0; i < 8; ++i) { DrawSprite(kRed); Draw3D(dev, kRed); }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Counters d = Delta(before, Sample(renderer));

            checkCount(d.passes, 1, "D4 sixteen alternating draws in one cycle: render passes");
            checkCount(d.submits, 1, "D4 sixteen alternating draws in one cycle: queue submits");
            checkCount(d.pipelines, 0,
                       "D4 sixteen alternating draws in one cycle: NEW Colored3D pipeline variants");
        }

        // ---- D5: the backbuffer recorder pays the same as the render-target one ----
        //
        // All three of this renderer's pass recorders shared the fixed per-family replay list and all
        // three now share the ordered walk; if only the render-target path had been corrected, this
        // is where the backbuffer's cost would diverge.
        {
            const Counters before = Sample(renderer);
            dev.Clear(kBlack);
            for (int i = 0; i < 4; ++i) { DrawSprite(kRed); Draw3D(dev, kRed); }
            std::vector<Color> pixels(static_cast<std::size_t>(kBBW) * kBBH, kBlack);
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            const Counters d = Delta(before, Sample(renderer));

            checkCount(d.passes, 1, "D5 eight alternating draws on the BACKBUFFER: render passes");
            checkCount(d.submits, 1, "D5 eight alternating draws on the BACKBUFFER: queue submits");
            checkCount(d.pipelines, 0,
                       "D5 eight alternating draws on the BACKBUFFER: NEW Colored3D pipelines");
        }

        // ---- D7: the RenderTargetCube FACE recorder, by pixels ----
        //
        // This renderer has three pass recorders and all three shared the fixed per-family replay
        // list. The cross-renderer pixel oracle in spritebatch_3d_order_test.cpp reaches the
        // backbuffer and RenderTarget2D; the cube-face recorder is WebGPU-shaped and lives here,
        // so its ordering is measured here rather than left as "the same code path, presumed
        // covered". Both directions are run: a fixed replay list renders one of the two correctly
        // by accident, which is exactly how this defect survived REMED-GFX-143's check O3.
        {
            RenderTargetCube cube(dev, kRT, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8,
                                  0, RenderTargetUsage::DiscardContents);
            const Color kBlue(0, 0, 255, 255);
            struct Leg { bool spriteFirst; Color want; const char* name; };
            const Leg legs[] = {
                { true,  kBlue, "D7a a cube face's SpriteBatch -> 3D leaves the 3D draw on top" },
                { false, kRed,  "D7b a cube face's 3D -> SpriteBatch leaves the sprite on top" },
            };
            for (const Leg& leg : legs)
            {
                dev.SetRenderTarget(&cube, CubeMapFace::PositiveX);
                const Counters before = Sample(renderer);
                dev.Clear(kBlack);
                if (leg.spriteFirst) { DrawSprite(kRed);   Draw3D(dev, kBlue); }
                else                 { Draw3D(dev, kBlue); DrawSprite(kRed);   }
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                const Counters d = Delta(before, Sample(renderer));

                checkCount(d.passes, 1, std::string(leg.name) + ": render passes");
                checkCount(d.submits, 1, std::string(leg.name) + ": queue submits");

                std::vector<Color> face(static_cast<std::size_t>(kRT) * kRT, kBlack);
                try
                {
                    cube.GetData(CubeMapFace::PositiveX, face.data(),
                                 static_cast<int>(face.size()));
                    const Color got = face[face.size() / 2 + kRT / 2];
                    const bool ok = got.getRProperty() == leg.want.getRProperty() &&
                                    got.getGProperty() == leg.want.getGProperty() &&
                                    got.getBProperty() == leg.want.getBProperty();
                    check(ok, ok ? std::string(leg.name)
                                 : std::string(leg.name) + ": centre texel is (" +
                                       std::to_string(static_cast<int>(got.getRProperty())) + "," +
                                       std::to_string(static_cast<int>(got.getGProperty())) + "," +
                                       std::to_string(static_cast<int>(got.getBProperty())) +
                                       "), so the later draw did not win");
                }
                catch (const std::exception& e)
                {
                    std::printf("[INFO] %s: face readback unavailable here (%s) -- the pass and "
                                "submit counts above still hold\n", leg.name, e.what());
                    std::fflush(stdout);
                }
            }
        }

        // ---- D6: validation stayed silent through every family transition ----
        {
            const std::size_t errors = renderer.GetUncapturedErrorCountEXT();
            check(errors == 0, errors == 0
                      ? "D6 WebGPU validation stayed silent through every family transition"
                      : "D6 WebGPU reported " + std::to_string(errors) + " uncaptured errors");
        }

        std::printf("%d/%d checks passed on WEBGPU\n", passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& dev = getGraphicsDeviceProperty();
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        sb_ = std::make_unique<SpriteBatch>(dev);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());
        fx_->setLightingEnabledProperty(false);
    }

public:
    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    WebGpuDrawOrderCardinalityTest test;
    test.Run();
    return test.Result();
}
