// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-050 -- SpriteSortMode::Immediate and SpriteSortMode::Texture, asserted
// through behaviour the modes actually control rather than through the mode being accepted.
//
// Renderer-agnostic on purpose, and registered for more than one renderer, because the row's
// premise was that Vulkan is the only renderer that overrides SetImmediateMode and therefore the
// only one whose Immediate handling could be wrong. Running the same measurement on EasyGL is what
// separates a Vulkan defect from a CNA-wide one, and here it separated three things:
//
//   1. `SetImmediateMode` is dead code in this configuration. `VulkanSpriteBatchRenderer` stores
//      `immediateMode_` and reads it ONLY inside `#if defined(CNA_VULKAN_COMPILED_EFFECTS)`, which
//      is OFF in cmake-build-vulkan. The row's "must fail if SetImmediateMode is stubbed out" can
//      not be honoured as written, because stubbing it out changes nothing here. What CAN be
//      asserted is the seam above it: that Begin() propagates the mode at all (leg B2).
//
//   2. A texture mutated between two Draw calls does NOT distinguish the modes on either renderer,
//      and the reason is below the SpriteBatch seam. `SpriteBatch::flushSingle` does forward each
//      Immediate sprite straight to the renderer, but `EasyGLSpriteBatchRenderer::Draw` appends to
//      a vertex batch that is only flushed when the bound texture changes or at End(), and
//      `VulkanSpriteBatchRenderer::Draw` records into `activeBatches_` for replay at Present. Both
//      therefore rasterize every sprite of the batch against the texture's FINAL contents. That is
//      a divergence from XNA, it is CNA-wide rather than Vulkan-specific, and it is recorded as
//      VULKAN-057 rather than papered over here: leg B reports the measurement and asserts only
//      what Immediate must do regardless (draw every sprite, in place, in issue order).
//
//   3. The one thing the mode genuinely gates today is `DrawMeshEXT`, which refuses any sort mode
//      but Immediate. Leg B2 uses it as the discriminator: under Deferred the sort-mode refusal
//      must fire, under Immediate it must not (this renderer then refuses on capability grounds
//      instead, which is a different message and a different reason). That leg fails the moment
//      Begin() stops carrying the mode to the seam.
//
// Leg C covers SpriteSortMode::Texture, and its first draft asserted the opposite of the actual
// contract, so the correction is recorded here rather than quietly dropped. "Two overlapping
// sprites with different textures must still come out in issue order" is NOT a guarantee: the mode
// is defined as "same as Deferred, except sprites are sorted by texture prior to drawing"
// (SpriteSortMode.cs), FNA orders them by an arbitrary `textureHash` through an UNSTABLE
// `Array.Sort`, and Microsoft's own remark scopes the mode to non-overlapping sprites. Asserted
// that way the leg passed on Vulkan and failed on EasyGL for no better reason than which
// `ITextureRenderer` allocation happened to hold the lower address -- CNA sorts on the pointer.
//
// What the mode does guarantee, and what leg C asserts instead:
//   * no sprite is lost -- sprites with different textures still each reach their own destination;
//   * grouping cannot reorder WITHIN a group -- CNA uses `std::stable_sort`, so two overlapping
//     sprites that share one texture come out in issue order, and the second one wins.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SortModeSemanticsTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    inline static const Color kClear{13, 17, 19, 255};
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    /// Mutates the texture IN PLACE.
    ///
    /// `SetDataRGBA` is used rather than `SetData(const Color*)` deliberately, and the difference
    /// is the whole reason this test can discriminate anything. A full-level `SetData` builds a
    /// FRESH renderer texture and detaches the wrapper from the old one (Texture2D.cpp, and
    /// REMED-GFX-223 explains why: a ContentManager-cached texture must not publish an upload to
    /// every other holder). A sprite already queued holds a share of the OLD resource, so it keeps
    /// the old pixels -- on EVERY renderer, in EVERY sort mode. Measured on both EasyGL and Vulkan:
    /// identical results, so that route cannot tell Immediate from Deferred.
    ///
    /// `SetDataRGBA` goes to `UpdatePixels` on the existing renderer texture, so a queued sprite
    /// and an already-submitted one genuinely see different things -- which is exactly the
    /// distinction Immediate is defined by.
    static void Fill(Texture2D& t, const Color& c)
    {
        const std::array<std::uint8_t, 16> rgba{
            c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty(),
            c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty(),
            c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty(),
            c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty()};
        t.SetDataRGBA(rgba.data(), 4);
    }

    static bool Is(const Color& got, const Color& want)
    {
        return got.getRProperty() == want.getRProperty() &&
               got.getGProperty() == want.getGProperty() &&
               got.getBProperty() == want.getBProperty();
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    /// Draws the same texture into two 2x2 quadrants of a 4x2 target, mutating it in between.
    /// Returns all eight texels of the target, row-major.
    std::vector<Color> MutateBetweenDraws(GraphicsDevice& dev, SpriteSortMode mode,
                                          const Color& first, const Color& second)
    {
        RenderTarget2D rt(dev, 4, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Texture2D tex(dev, 2, 2, false, SurfaceFormat::Color);
        Fill(tex, first);

        dev.SetRenderTarget(&rt);
        dev.Clear(kClear);
        {
            SamplerState point = SamplerState::PointClamp;
            SpriteBatch batch(dev);
            batch.Begin(mode, BlendState::Opaque, &point, nullptr, nullptr);
            batch.Draw(tex, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
            Fill(tex, second);   // the mutation Immediate is supposed to be on the far side of
            batch.Draw(tex, Rectangle(2, 0, 2, 2), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
            batch.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(8, Color(0, 0, 0, 0));
        rt.GetData(pixels.data(), 0, 8);
        return pixels;
    }

    /// True when every texel of one 2x2 quadrant of the 4x2 target carries `want`.
    /// `x0` is 0 for the left sprite, 2 for the right one.
    static bool QuadrantIs(const std::vector<Color>& p, int x0, const Color& want)
    {
        for (int y = 0; y < 2; ++y)
            for (int x = x0; x < x0 + 2; ++x)
                if (!Is(p[static_cast<std::size_t>(y * 4 + x)], want)) return false;
        return true;
    }

    /// Issues a DrawMeshEXT under `mode` and reports which refusal, if any, came back.
    /// `SpriteBatch::DrawMeshEXT` rejects every sort mode but Immediate BEFORE it touches the
    /// renderer, so the message identifies whether Begin() carried the mode down to the seam.
    enum class MeshOutcome { NoThrow, RefusedForSortMode, RefusedForCapability };

    static MeshOutcome ProbeMeshGate(GraphicsDevice& dev, SpriteSortMode mode)
    {
        BasicEffect effect(dev);
        const std::array<Vector2, 3> positions{Vector2(0, 0), Vector2(2, 0), Vector2(0, 2)};
        const std::array<Color, 3> colors{Color(255, 255, 255, 255), Color(255, 255, 255, 255),
                                          Color(255, 255, 255, 255)};
        const std::array<Vector2, 3> uvs{Vector2(0, 0), Vector2(1, 0), Vector2(0, 1)};
        const std::array<std::uint16_t, 3> indices{0, 1, 2};

        SpriteBatch batch(dev);
        batch.Begin(mode, BlendState::Opaque, nullptr, nullptr, nullptr);
        MeshOutcome outcome = MeshOutcome::NoThrow;
        try
        {
            batch.DrawMeshEXT(effect, positions.data(), colors.data(), uvs.data(), 3,
                              indices.data(), 3);
        }
        catch (const std::exception& e)
        {
            const std::string what = e.what();
            outcome = what.find("requires SpriteSortMode::Immediate") != std::string::npos
                          ? MeshOutcome::RefusedForSortMode
                          : MeshOutcome::RefusedForCapability;
        }
        try { batch.End(); } catch (const std::exception&) { /* a refused mesh must not wedge End */ }
        return outcome;
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const Color kRed(255, 0, 0, 255);
        const Color kBlue(0, 0, 255, 255);

        // A. Deferred is the control: both sprites are submitted after the mutation, so both show
        //    the second colour, and nothing outside the two quadrants is painted.
        const std::vector<Color> deferred =
            MutateBetweenDraws(dev, SpriteSortMode::Deferred, kRed, kBlue);
        check(QuadrantIs(deferred, 0, kBlue) && QuadrantIs(deferred, 2, kBlue),
              "A Deferred: both sprites show the texture's FINAL contents, left=" +
                  Text(deferred[0]) + " right=" + Text(deferred[2]));

        // B. Immediate. XNA submits each Draw as it is issued, so the first sprite would keep the
        //    old colour; neither CNA renderer can produce that, because both accumulate below the
        //    SpriteBatch seam (see the header, and VULKAN-057). What Immediate must still do is
        //    draw EVERY sprite, into its own destination rectangle, leaving the rest cleared --
        //    this leg fails if the mode drops a sprite, duplicates one, or misplaces it.
        {
            const std::vector<Color> immediate =
                MutateBetweenDraws(dev, SpriteSortMode::Immediate, kRed, kBlue);
            const bool leftPainted  = !Is(immediate[0], kClear);
            const bool rightPainted = !Is(immediate[2], kClear);
            const bool uniform = QuadrantIs(immediate, 0, immediate[0]) &&
                                 QuadrantIs(immediate, 2, immediate[2]);
            check(leftPainted && rightPainted && uniform,
                  "B Immediate: both sprites are drawn, each filling its own quadrant, left=" +
                      Text(immediate[0]) + " right=" + Text(immediate[2]));

            // The measurement itself, printed rather than asserted: asserting XNA's answer here
            // would fail on every CNA renderer, and asserting CNA's would cement the divergence.
            std::printf("[INFO] mid-batch texture mutation under Immediate: left=%s right=%s "
                        "(XNA would give red/blue; VULKAN-057) -- Deferred gave %s/%s\n",
                        Text(immediate[0]).c_str(), Text(immediate[2]).c_str(),
                        Text(deferred[0]).c_str(), Text(deferred[2]).c_str());
            std::fflush(stdout);
        }

        // B2. The sort mode does reach the renderer seam, and DrawMeshEXT is what proves it: it
        //     refuses every mode but Immediate, before the renderer is consulted. Under Immediate
        //     that refusal must NOT fire -- this renderer may still refuse the mesh on capability
        //     grounds, which is a different message and a different reason.
        {
            const MeshOutcome deferredGate  = ProbeMeshGate(dev, SpriteSortMode::Deferred);
            const MeshOutcome immediateGate = ProbeMeshGate(dev, SpriteSortMode::Immediate);
            check(deferredGate == MeshOutcome::RefusedForSortMode &&
                      immediateGate != MeshOutcome::RefusedForSortMode,
                  "B2 Begin() carries the sort mode to the seam: Deferred refused the mesh for its "
                  "sort mode, Immediate did not (" +
                      std::string(immediateGate == MeshOutcome::NoThrow ? "accepted"
                                                                        : "refused on capability") +
                      ")");
        }

        // C. Texture sort mode: nothing is lost, and grouping never reorders within a group.
        //    C1 -- two DIFFERENT textures at two DIFFERENT destinations: whatever order the sort
        //    picks, both sprites must arrive, each carrying its own texture's colour.
        {
            RenderTarget2D rt(dev, 4, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            Texture2D a(dev, 2, 2, false, SurfaceFormat::Color);
            Texture2D b(dev, 2, 2, false, SurfaceFormat::Color);
            Fill(a, kRed);
            Fill(b, kBlue);
            dev.SetRenderTarget(&rt);
            dev.Clear(kClear);
            {
                SamplerState point = SamplerState::PointClamp;
                SpriteBatch batch(dev);
                batch.Begin(SpriteSortMode::Texture, BlendState::Opaque, &point, nullptr, nullptr);
                batch.Draw(a, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
                batch.Draw(b, Rectangle(2, 0, 2, 2), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
                batch.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<Color> pixels(8, Color(0, 0, 0, 0));
            rt.GetData(pixels.data(), 0, 8);
            check(QuadrantIs(pixels, 0, kRed) && QuadrantIs(pixels, 2, kBlue),
                  "C1 Texture: grouping by texture lost no sprite, left=" + Text(pixels[0]) +
                      " (want red) right=" + Text(pixels[2]) + " (want blue)");
        }

        //    C2 -- the ordering promise that DOES exist. Two overlapping sprites share ONE texture,
        //    so they are one group, and a stable sort must leave them in issue order: the second
        //    Draw's tint is what survives. This fails if the sort is made unstable.
        {
            RenderTarget2D rt(dev, 2, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            Texture2D white(dev, 2, 2, false, SurfaceFormat::Color);
            Fill(white, Color(255, 255, 255, 255));
            dev.SetRenderTarget(&rt);
            dev.Clear(kClear);
            {
                SamplerState point = SamplerState::PointClamp;
                SpriteBatch batch(dev);
                batch.Begin(SpriteSortMode::Texture, BlendState::Opaque, &point, nullptr, nullptr);
                batch.Draw(white, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), kRed);
                batch.Draw(white, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), kBlue);
                batch.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<Color> pixels(4, Color(0, 0, 0, 0));
            rt.GetData(pixels.data(), 0, 4);
            check(Is(pixels[0], kBlue),
                  "C2 Texture: sprites sharing a texture keep issue order, got=" +
                      Text(pixels[0]) + " (want blue, the one issued second)");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    SortModeSemanticsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SortModeSemanticsTest game;
    game.Run();
    return game.getResult();
}
