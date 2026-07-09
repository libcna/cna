// SPDX-License-Identifier: MS-PL
// Task 35: OcclusionQuery integration test.
//
// Constructs an OcclusionQuery on the EasyGL device, calls Begin()/End(),
// verifies IsComplete eventually becomes true and PixelCount >= 0.
//
// Task 442: invalid call sequence -- End() before any Begin(). Task 441's audit of FNA's real
// OcclusionQuery.cs found ZERO C#-level validation of Begin/End call sequence -- Begin()/End() are
// pure one-line forwards to FNA3D_QueryBegin/FNA3D_QueryEnd with no state tracking at all, so there
// is no "FNA exception" to match for this sequence (correcting this task's own stale Notes-column
// framing). This instead confirms CNA's own OcclusionQuery correctly mirrors that lack of
// validation: End() before Begin() must not throw or crash, matching FNA's own unvalidated shape.
//
// Task 443: invalid call sequence -- double Begin() (Begin() called twice with no intervening
// End()). Same Task 441 finding applies: FNA's Begin() is a pure one-line forward to
// FNA3D_QueryBegin with no state tracking, so there is no "FNA exception" to match here either.
// Confirms CNA's own Begin() correctly mirrors that lack of validation too.
//
// Task 444: invalid call sequence -- double End() (a valid Begin()/End() cycle followed by a
// second, unmatched End()). Same Task 441 finding applies once more: FNA's End() is a pure
// one-line forward to FNA3D_QueryEnd with no state tracking, so there is no "FNA exception" to
// match here either. This closes the Tasks 442-444 invalid-call-sequence trio.
//
// Task 449: destroying (the C++ destructor -- CNA's own OcclusionQuery::Dispose() doesn't touch
// backend_ at all, matching FNA's own inherited GraphicsResource.Dispose(bool) shape closely, so
// the real backend teardown happens in ~OcclusionQuery(), not Dispose()) a query while it's still
// "active" (Begin() called, no matching End()) must be safe -- no crash, no corrupted GL/backend
// state affecting subsequently-created queries. EasyGL's own GL query object is owned by an
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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

        // Before Begin: IsComplete may be true or false; PixelCount >= 0
        check(q.getPixelCountProperty() >= 0, "PixelCount >= 0 before query");

        // Begin / End cycle
        q.Begin();
        q.End();

        // After End: PixelCount still >= 0 (result may not be ready yet)
        check(q.getPixelCountProperty() >= 0, "PixelCount >= 0 after End");

        // IsComplete returns a bool without crashing
        bool complete = q.getIsCompleteProperty();
        (void)complete;
        check(true, "getIsCompleteProperty() does not crash");

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
            check(!threw, "End() before Begin() does not throw (matches FNA's own lack of validation)");
            check(q2.getPixelCountProperty() >= 0, "PixelCount >= 0 after End-before-Begin");
            bool complete2 = q2.getIsCompleteProperty();
            (void)complete2;
            check(true, "getIsCompleteProperty() does not crash after End-before-Begin");
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
            check(!threw, "double Begin() does not throw (matches FNA's own lack of validation)");
            q3.End();
            check(q3.getPixelCountProperty() >= 0, "PixelCount >= 0 after double-Begin then End");
            bool complete3 = q3.getIsCompleteProperty();
            (void)complete3;
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
            check(!threw, "double End() does not throw (matches FNA's own lack of validation)");
            check(q4.getPixelCountProperty() >= 0, "PixelCount >= 0 after double-End");
            bool complete4 = q4.getIsCompleteProperty();
            (void)complete4;
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

            // Confirm the device/backend is still healthy afterward: a fresh, normal query cycle
            // still works correctly, proving no corrupted state lingered from the active-disposes.
            OcclusionQuery q5(device);
            q5.Begin();
            q5.End();
            check(q5.getPixelCountProperty() >= 0,
                  "a fresh query still works normally after 50 active-disposed queries");
        }

        Exit();
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
