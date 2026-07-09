// SPDX-License-Identifier: MS-PL
// Task 448: Implement/verify Bgfx occlusion query result behavior.
//
// Task 441's own audit found BgfxOcclusionQueryBackend::Begin()/End() were literally empty
// no-ops, and no bgfx::setCondition()/occlusion-query submit() overload was ever used anywhere in
// the backend -- the created bgfx::OcclusionQueryHandle was never wired to any draw call at all.
//
// Fixed (this task) via bgfx's own dedicated submit(id, program, occlusionQuery, depth, flags)
// overload: BgfxGraphicsBackend now tracks an activeOcclusionQuery_ handle, set by
// BgfxOcclusionQueryBackend::Begin() and cleared by End(); every one of the 12 3D-draw submit()
// call sites (colored3d/textured3d/dualTexture3d/skinned3d/envMap3d/alphaTest3d/etc.) is routed
// through a new SubmitViewProgram() helper that uses the occlusion-query overload whenever a query
// is active -- this exactly matches bgfx's own documented API contract and its official
// "26-occlusion" example's usage of the same submit() overload. Unlike Vulkan (Task 447, BLOCKED --
// see its own write-up), bgfx submits every 3D draw call synchronously, so no deferred-recording
// correlation machinery is needed for the attachment itself.
//
// IMPORTANT, EMPIRICALLY-CONFIRMED LIMITATION: discriminating power for this fix could NOT be
// established in this sandbox. Sabotaging Begin()/End() back to pure no-ops (the exact pre-fix
// state) was tried and produced IDENTICAL IsComplete()/PixelCount() behavior to the fix -- on this
// sandbox's software Mesa GL 2.1 (llvmpipe) renderer, bgfx::getResult() returns a non-"NoResult"
// value even for a query handle that was NEVER submitted anywhere at all. This means neither
// IsComplete() nor PixelCount() can be used as a discriminating signal here; both are asserted
// as PASS-if-consistent smoke checks only (no crash, a plausible value), not proof that the fix
// itself changed observable behavior in this specific environment. bgfx's own official example
// additionally attaches its occlusion-measurement submit() to a SEPARATE, dedicated view/depth
// buffer from the "real" visible scene -- reproducing that (needed for genuine, scene-depth-aware
// occlusion correctness) is a further architecture addition, opened as new Task 917, not done here.
// The production fix is still shipped since it correctly, verifiably implements bgfx's documented
// API contract (confirmed by direct comparison against bgfx's own example source) and is a
// strict improvement over the prior always-empty-no-op state, even though this specific software
// rendering sandbox cannot itself prove the resulting runtime behavior changed.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;
static constexpr int kMaxPollFrames = 30;

namespace
{
    const Color kRed(255, 0, 0, 255);

    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
            && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
            && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
    }

    void DrawQuad(GraphicsDevice& dev, float z, const Color& colour)
    {
        const Vector3 tl(-1.0f,  1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br( 1.0f, -1.0f, z), tr( 1.0f,  1.0f, z);
        const VertexPositionColor q[6] = {
            { tl, colour }, { bl, colour }, { br, colour },
            { tl, colour }, { br, colour }, { tr, colour },
        };
        // Task 364/884 finding: Bgfx's default cull state (BGFX_STATE_CULL_CCW) culls this
        // quad's winding, unlike EasyGL/Vulkan's own defaults -- needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }
}

class BgfxOcclusionQueryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<OcclusionQuery> query_;
    int frame_ = 0;

    bool complete_    = false;
    int  pixelCount_  = -12345; // sentinel: still-unset if never overwritten
    Color centre_     { 0, 0, 0, 0 };
    bool  threw_      = false;

    int pass_ = 0, fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        ok ? ++pass_ : ++fail_;
    }

    Color readCentre(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // Redraw the same quad every frame (bgfx's swapchain needs each view touched every frame
        // to keep valid content for readback) -- only frame_==0's draw is wrapped in the query's
        // Begin()/End(); later frames' redraws are plain, unrelated submits.
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        fx.Apply();

        if (frame_ == 0)
        {
            query_ = std::make_unique<OcclusionQuery>(dev);
            try
            {
                query_->Begin();
                DrawQuad(dev, 0.5f, kRed); // full-NDC quad, nothing occludes it
                query_->End();
            }
            catch (...)
            {
                threw_ = true;
            }
            ++frame_;
            return;
        }
        DrawQuad(dev, 0.5f, kRed);

        if (complete_ || frame_ >= kMaxPollFrames)
        {
            // See this file's own header comment: IsComplete()/PixelCount() are NOT reliable
            // discriminating signals in this sandbox (identical even with Begin()/End() sabotaged
            // back to no-ops), so only report them informationally, not as pass/fail assertions.
            check(!threw_, "Begin()/End() around a real attached draw call do not throw");
            check(colourMatch(centre_, kRed), "quad still renders Red at centre (new plumbing "
                                               "doesn't break normal 3D rendering)");
            std::printf("(informational only, see header comment) IsComplete()=%s PixelCount()=%d "
                        "after %d frame(s)\n", complete_ ? "true" : "false", pixelCount_, frame_);

            std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
            Exit();
            return;
        }

        complete_   = query_->getIsCompleteProperty();
        pixelCount_ = query_->getPixelCountProperty();
        centre_     = readCentre(dev);
        ++frame_;
    }

public:
    BgfxOcclusionQueryTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxOcclusionQueryTest game;
    game.Run();
    return game.getResult();
}
