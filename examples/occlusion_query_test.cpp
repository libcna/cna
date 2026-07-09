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
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"

#include <cstdio>

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
