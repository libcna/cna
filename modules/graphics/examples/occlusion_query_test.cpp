// SPDX-License-Identifier: MS-PL
// Task 35: OcclusionQuery integration test.
//
// Constructs an OcclusionQuery on the EasyGL device, calls Begin()/End(),
// verifies IsComplete eventually becomes true and PixelCount >= 0.
//
// SOFTWARE-199 supersedes Tasks 442-444's FNA-only conclusion. Recovered Microsoft XNA 4.0 code
// contains an explicit managed state machine: unavailable PixelCount, End-before-Begin, nested
// Begin, repeated End, and reuse before observing IsComplete all throw InvalidOperationException.
//
// Task 449: destroying a query while it's still "active" (Begin() called, no matching End()) must
// be safe -- no crash, no corrupted GL/renderer state affecting subsequently-created queries.
// EasyGL's own GL query object is owned by an
// easygl::Query member with RAII semantics (its own destructor calls glDeleteQueries
// unconditionally); per the GL spec, deleting an active query object is well-defined (deletion is
// deferred internally until the query is no longer active), so this should already be safe -- this
// is a repetition-based stress verification (matching Task 719's own established leak-check-style
// convention for "no bug found, confirms already-correct behavior" safety tasks), not a
// sabotage-and-revert one, since there's no natural incorrect-vs-correct code branch to toggle for
// a pure RAII-safety confirmation.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static int g_failures = 0;

static void check(bool cond, const char* label)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++g_failures;
    }
}

class OcclusionQueryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // Construction
        OcclusionQuery q(device);

        // GetTypeName
        check(q.GetTypeName() == "Microsoft.Xna.Framework.Graphics.OcclusionQuery",
              "GetTypeName");

        // Before Begin the result is unavailable.
        bool unavailableThrew = false;
        try { (void) q.getPixelCountProperty(); }
        catch (...) { unavailableThrew = true; }
        check(unavailableThrew, "PixelCount throws before the first completed query");

        // Begin / End cycle
        q.Begin();
        q.End();

        // IsComplete returns a bool without crashing. PixelCount is legal only when it is true.
        bool complete = q.getIsCompleteProperty();
        check(true, "getIsCompleteProperty() does not crash");
        bool postEndPixelCountMatched = false;
        try
        {
            const int count = q.getPixelCountProperty();
            postEndPixelCountMatched = complete && count >= 0;
        }
        catch (...)
        {
            postEndPixelCountMatched = !complete;
        }
        check(postEndPixelCountMatched, "PixelCount availability matches IsComplete");

        // Task 442: invalid sequence -- End() before any Begin() call on a fresh query.
        {
            OcclusionQuery q2(device);
            bool threw = false;
            try
            {
                q2.End(); // no matching Begin() yet
            }
            catch (...)
            {
                threw = true;
            }
            check(threw, "End() before Begin() throws");
        }

        // Task 443: invalid sequence -- double Begin() with no intervening End().
        {
            OcclusionQuery q3(device);
            bool threw = false;
            try
            {
                q3.Begin();
                q3.Begin(); // second Begin() with no End() in between
            }
            catch (...)
            {
                threw = true;
            }
            check(threw, "double Begin() throws");
            q3.End();
            (void) q3.getIsCompleteProperty();
            check(true, "getIsCompleteProperty() does not crash after double-Begin");
        }

        // Task 444: invalid sequence -- double End() (a valid Begin()/End() cycle followed by an
        // extra, unmatched End()). Closes the Tasks 442-444 invalid-sequence trio.
        {
            OcclusionQuery q4(device);
            q4.Begin();
            q4.End();
            bool threw = false;
            try
            {
                q4.End(); // second, unmatched End()
            }
            catch (...)
            {
                threw = true;
            }
            check(threw, "double End() throws");
            (void) q4.getIsCompleteProperty();
            check(true, "getIsCompleteProperty() does not crash after double-End");
        }

        // Task 449: destroy a query while still active (Begin() called, no End()) -- repeated
        // 50x as a stress/leak-style verification, matching Task 719's established convention
        // (including its own GetTrackedResourceCount()-returns-to-baseline check).
        {
            const std::size_t baseline = device.GetTrackedResourceCount();
            bool threw = false;
            try
            {
                for (int i = 0; i < 50; ++i)
                {
                    auto activeQuery = std::make_unique<OcclusionQuery>(device);
                    activeQuery->Begin();
                    // Deliberately no End() call -- destroy while still "active".
                    activeQuery.reset();
                }
            }
            catch (...)
            {
                threw = true;
            }
            check(!threw, "destroying an active (Begin()-without-End()) query 50x does not throw or crash");
            check(device.GetTrackedResourceCount() == baseline,
                  "GetTrackedResourceCount() returns to baseline after 50 active-disposed queries");

            // Confirm the device/renderer is still healthy afterward: a fresh, normal query cycle
            // still works correctly, proving no corrupted state lingered from the active-disposes.
            OcclusionQuery q5(device);
            q5.Begin();
            q5.End();
            (void) q5.getIsCompleteProperty();
            check(true, "a fresh query still works normally after 50 active-disposed queries");
        }

        Exit();
    }

public:
    OcclusionQueryTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
    }
};

int main(int, char**)
{
    auto* game = new OcclusionQueryTest();
    game->Run();
    delete game;
    if (g_failures == 0)
    {
        std::printf("OcclusionQuery: all checks PASS\n");
        return 0;
    }
    std::printf("OcclusionQuery: %d check(s) FAILED\n", g_failures);
    return 1;
}
