// SPDX-License-Identifier: MS-PL
// Task 461: shared, opt-in helper for the common "create device, draw one frame, read back
// pixels, compare, report PASS/FAIL" shape used by most examples/*.cpp pixel-readback tests.
//
// This is new, additive infrastructure only -- no existing examples/*.cpp file has been
// modified to use it. Retrofitting the ~330 existing, working, already-verified test files
// into a shared base class would be a large, high-risk, low-value mechanical refactor; this
// header exists so *future* single-frame pixel tests can opt in and skip re-typing the same
// ~20 lines of Game-subclass/main() boilerplate every time. Multi-frame state-machine tests
// (e.g. examples/bgfx_render_target_usage_test.cpp) have per-test frame/stage logic that does
// not fit this single-shot shape and should keep hand-rolling their own Game subclass, exactly
// as they do today.
//
// Usage:
//
//     #include "common/PixelTestGame.hpp"
//
//     class MyTest : public CNA::Examples::PixelTestGame
//     {
//     protected:
//         void RunTest() override
//         {
//             auto& device = getGraphicsDeviceProperty();
//             device.Clear(Color(255, 0, 0, 255));
//             // ... draw calls ...
//             ExpectPixel("centre", Rectangle(W / 2, H / 2, 1, 1), Color(255, 0, 0, 255));
//             // Or, for a channel known to legitimately vary a little by backend/driver
//             // (Task 462 -- e.g. MSAA edge blending): pass a tolerance explicitly.
//             ExpectPixel("edge", Rectangle(0, 0, 1, 1), Color(128, 128, 128, 255), /*tolerance=*/20);
//         }
//     };
//
//     int main() { return CNA::Examples::RunPixelTest<MyTest>(); }

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace CNA::Examples
{
    // Shared base for single-frame pixel-readback example tests (Task 461). Derived classes
    // override RunTest() to issue their draw calls and call ExpectPixel() for each pixel they
    // want to check; the overall exit code (0 = all checks passed, 1 = at least one failed) is
    // available via getResultProperty() once the game has run.
    class PixelTestGame : public Microsoft::Xna::Framework::Game
    {
    public:
        // Runs the derived test's draw calls and pixel checks; called exactly once, on the
        // first Draw() call, then immediately exits the game loop.
        virtual void RunTest() = 0;

        // Reads back a single-pixel region and compares it (R/G/B only, matching this
        // project's existing example convention of ignoring alpha unless a test cares about
        // it explicitly) against expectedColor, allowing each channel to differ by up to
        // tolerance (Task 462; matches the per-channel abs-difference convention already used
        // by ~98 existing hand-rolled example tests dealing with GPU/driver blending or
        // rounding noise -- e.g. MSAA edge antialiasing, alpha-blend precision). tolerance
        // defaults to 0 (exact match, Task 461's original behavior) since most pixel tests
        // check flat, unblended colours where an exact match is both correct and desirable;
        // pass a non-zero tolerance only for scenarios that are known to legitimately vary.
        // Prints a "[PASS]"/"[FAIL]" line naming label, and marks the overall result as failed
        // if this check does not match. Returns true if this specific check passed.
        bool ExpectPixel(const char* label,
                          const Microsoft::Xna::Framework::Rectangle& region,
                          const Microsoft::Xna::Framework::Color& expectedColor,
                          int tolerance = 0)
        {
            using Microsoft::Xna::Framework::Color;

            Color pixel(0, 0, 0, 0);
            getGraphicsDeviceProperty().GetBackBufferData(&region, &pixel, 0, 1);

            const auto closeEnough = [tolerance](int a, int b)
            {
                return std::abs(a - b) <= tolerance;
            };
            const bool pass = closeEnough(pixel.getRProperty(), expectedColor.getRProperty()) &&
                               closeEnough(pixel.getGProperty(), expectedColor.getGProperty()) &&
                               closeEnough(pixel.getBProperty(), expectedColor.getBProperty());

            if (pass)
            {
                std::printf("[PASS] %s: pixel=(%d,%d,%d,%d)\n", label,
                            pixel.getRProperty(), pixel.getGProperty(),
                            pixel.getBProperty(), pixel.getAProperty());
            }
            else if (tolerance > 0)
            {
                std::printf("[FAIL] %s: pixel=(%d,%d,%d,%d), expected (%d,%d,%d,*) +/- %d\n",
                            label,
                            pixel.getRProperty(), pixel.getGProperty(),
                            pixel.getBProperty(), pixel.getAProperty(),
                            expectedColor.getRProperty(), expectedColor.getGProperty(),
                            expectedColor.getBProperty(), tolerance);
                result_ = 1;
            }
            else
            {
                std::printf("[FAIL] %s: pixel=(%d,%d,%d,%d), expected (%d,%d,%d,*)\n", label,
                            pixel.getRProperty(), pixel.getGProperty(),
                            pixel.getBProperty(), pixel.getAProperty(),
                            expectedColor.getRProperty(), expectedColor.getGProperty(),
                            expectedColor.getBProperty());
                result_ = 1;
            }
            return pass;
        }

        // The process exit code to return from main(): 0 if every ExpectPixel() call so far
        // has passed, 1 if at least one has failed.
        [[nodiscard]] int getResultProperty() const { return result_; }

    protected:
        void Draw(const Microsoft::Xna::Framework::GameTime&) override
        {
            if (done_) return;
            done_ = true;
            RunTest();
            Exit();
        }

    private:
        bool done_ = false;
        int result_ = 0; // 0 = pass until a check proves otherwise
    };

    // Constructs, runs, and returns the process exit code for a PixelTestGame-derived test in
    // one line, matching this project's existing single-shot example main() convention.
    template <typename TGame>
    int RunPixelTest()
    {
        static_assert(std::is_base_of_v<PixelTestGame, TGame>,
                      "RunPixelTest<TGame>: TGame must derive from CNA::Examples::PixelTestGame");
        TGame game;
        game.Run();
        return game.getResultProperty();
    }
}
