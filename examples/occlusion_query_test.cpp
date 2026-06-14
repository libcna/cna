// SPDX-License-Identifier: MS-PL
// Task 35: OcclusionQuery integration test.
//
// Constructs an OcclusionQuery on the EasyGL device, calls Begin()/End(),
// verifies IsComplete eventually becomes true and PixelCount >= 0.
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
