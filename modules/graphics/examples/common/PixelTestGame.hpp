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
//             // Or, for a channel known to legitimately vary a little by renderer/driver
//             // (Task 462 -- e.g. MSAA edge blending): pass a tolerance explicitly.
//             ExpectPixel("edge", Rectangle(0, 0, 1, 1), Color(128, 128, 128, 255), /*tolerance=*/20);
//             // Or, to verify a whole small rendered region at once against a checked-in
//             // reference PNG (Task 463) instead of a handful of hand-picked sample pixels:
//             CompareGoldenImage("scene", Rectangle(0, 0, 16, 16),
//                                "examples/golden/my_test.png", /*tolerance=*/20);
//         }
//     };
//
//     int main() { return CNA::Examples::RunPixelTest<MyTest>(); }
//
// To create or intentionally update a golden image, run the test once with
// CNA_UPDATE_GOLDEN=1 in the environment, review the written PNG, then commit it.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IO/FileStream.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

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
        // Records a non-pixel precondition -- e.g. "the texture this test draws was actually
        // created" -- in the same PASS/FAIL stream as the pixel checks, so a test whose setup
        // failed reports that instead of silently checking pixels no draw ever produced.
        // Returns the condition, so a caller can bail out of RunTest() when it is false.
        bool ExpectTrue(const char* label, bool condition)
        {
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
            if (!condition)
                result_ = 1;
            return condition;
        }

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

        // Generic boolean assertion (Task 465), for a check that isn't itself a pixel-region
        // compare -- e.g. "this rendered output must differ from that one" (proving a code path
        // had a real effect, not just "didn't throw"). Mirrors the per-file local `check()`
        // lambda every hand-rolled Game-subclass example test already defines for this exact
        // purpose (e.g. sdlgpu_smoke_test.cpp), lifted here so PixelTestGame-derived tests don't
        // need to redeclare it. Prints the same "[PASS]"/"[FAIL] label" line and folds into the
        // same getResultProperty() as ExpectPixel/CompareGoldenImage.
        bool Check(bool ok, const char* label)
        {
            std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
            if (!ok) result_ = 1;
            return ok;
        }

        // Compares a region of the live rendered backbuffer against a small, checked-in
        // reference PNG image at goldenPath (Task 463), per-channel, with the same tolerance
        // convention as ExpectPixel (0 = exact match). Reuses Texture2D::FromStream/SaveAsPng
        // (both already existing, independently verified by Task 683's own round-trip test) --
        // no new image-decoding/encoding code is added here.
        //
        // Set the CNA_UPDATE_GOLDEN environment variable to any non-empty value to instead
        // (re)write goldenPath from the current live render and report [UPDATE] -- the intended
        // workflow for an intentional rendering change: run once with CNA_UPDATE_GOLDEN=1 to
        // regenerate the reference, review the new file with `git diff`/an image viewer, then
        // commit it; every subsequent run without the env var compares against it normally.
        //
        // Exact pixel-perfect reproduction across this project's 4 renderers/drivers is already
        // known unreliable (Task 462's own 98-file tolerance survey) -- pass a non-zero
        // tolerance for any golden image that isn't a single flat, unblended colour.
        bool CompareGoldenImage(const char* label,
                                 const Microsoft::Xna::Framework::Rectangle& region,
                                 const std::string& goldenPath,
                                 int tolerance = 0)
        {
            using Microsoft::Xna::Framework::Color;
            using Microsoft::Xna::Framework::Graphics::Texture2D;

            auto& device = getGraphicsDeviceProperty();
            const int w = region.Width;
            const int h = region.Height;
            const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);

            std::vector<Color> live(count, Color(0, 0, 0, 0));
            device.GetBackBufferData(&region, live.data(), 0, static_cast<int>(count));

            if (std::getenv("CNA_UPDATE_GOLDEN") != nullptr)
            {
                std::vector<std::uint8_t> rgba(count * 4);
                for (std::size_t i = 0; i < count; ++i)
                {
                    rgba[i * 4 + 0] = static_cast<std::uint8_t>(live[i].getRProperty());
                    rgba[i * 4 + 1] = static_cast<std::uint8_t>(live[i].getGProperty());
                    rgba[i * 4 + 2] = static_cast<std::uint8_t>(live[i].getBProperty());
                    rgba[i * 4 + 3] = static_cast<std::uint8_t>(live[i].getAProperty());
                }
                Texture2D golden = Texture2D::CreateFromPixels(device, w, h, rgba);
                golden.SaveAsPng(goldenPath);
                std::printf("[UPDATE] %s: wrote golden image to %s\n", label, goldenPath.c_str());
                return true;
            }

            System::IO::FileStream fileStream(goldenPath);
            Texture2D golden = Texture2D::FromStream(device, fileStream);

            if (golden.getWidthProperty() != w || golden.getHeightProperty() != h)
            {
                std::printf("[FAIL] %s: golden image %s is %dx%d, region is %dx%d\n", label,
                            goldenPath.c_str(), golden.getWidthProperty(),
                            golden.getHeightProperty(), w, h);
                result_ = 1;
                return false;
            }

            std::vector<Color> expected(count, Color(0, 0, 0, 0));
            golden.GetData(expected.data(), 0, static_cast<int>(count));

            const auto closeEnough = [tolerance](int a, int b)
            {
                return std::abs(a - b) <= tolerance;
            };

            for (std::size_t i = 0; i < count; ++i)
            {
                const Color& got = live[i];
                const Color& want = expected[i];
                if (!closeEnough(got.getRProperty(), want.getRProperty()) ||
                    !closeEnough(got.getGProperty(), want.getGProperty()) ||
                    !closeEnough(got.getBProperty(), want.getBProperty()))
                {
                    std::printf("[FAIL] %s: pixel %zu of %dx%d mismatches golden image %s: "
                                "got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) +/- %d\n",
                                label, i, w, h, goldenPath.c_str(),
                                got.getRProperty(), got.getGProperty(),
                                got.getBProperty(), got.getAProperty(),
                                want.getRProperty(), want.getGProperty(),
                                want.getBProperty(), want.getAProperty(), tolerance);
                    result_ = 1;
                    return false;
                }
            }

            std::printf("[PASS] %s: %dx%d region matches golden image %s\n", label, w, h,
                        goldenPath.c_str());
            return true;
        }

        // The process exit code to return from main(): 0 if every ExpectPixel()/
        // CompareGoldenImage() call so far has passed, 1 if at least one has failed.
        [[nodiscard]] int getResultProperty() const { return result_; }

        // Marks the overall result as failed for a check that is not a pixel comparison --
        // e.g. "this unimplemented path must throw" (plans/plan_sokol.md SOKOL-20). Without this a
        // derived test could only fail the run through ExpectPixel(), which would mean either
        // hand-rolling its own Game subclass for one non-pixel assertion or, worse, printing
        // [FAIL] while still exiting 0. Printing the message stays the caller's job, so the
        // wording can name whatever it actually checked.
        void MarkFailedEXT() { result_ = 1; }

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

    // The sentinel exit code a test uses to report "skipped, not failed" (Task 470) -- the
    // conventional Automake/BSD "test skipped" code, also CMake's own documented example value
    // for the CTest SKIP_RETURN_CODE test property. This project applies SKIP_RETURN_CODE 77 to
    // every registered test in bulk (see the bottom of the root CMakeLists.txt), so any test
    // that exits with this code is reported by ctest as SKIPPED rather than FAILED.
    inline constexpr int kSkipExitCode = 77;

    // Headless-safe pre-flight check (Task 470) for hand-rolled, multi-frame Game-subclass tests
    // that don't fit PixelTestGame's single-shot shape (this project's own convention above --
    // see this header's own top comment for why those tests keep hand-rolling their own
    // Game subclass instead of using RunPixelTest<TGame>()). Returns true if a GPU/display is
    // available (safe to proceed constructing a real Game/GraphicsDevice); false if it already
    // printed "[SKIP] ..." and the caller should immediately `return kSkipExitCode;` from main()
    // without ever constructing its Game subclass. Same SDL_InitSubSystem/SDL_QuitSubSystem probe
    // RunPixelTest<TGame>() itself uses.
    inline bool ProbeGpuDisplayAvailable()
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            std::printf("[SKIP] no GPU/display available (SDL_InitSubSystem(SDL_INIT_VIDEO) "
                        "failed: %s)\n", SDL_GetError());
            return false;
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return true;
    }

    // Constructs, runs, and returns the process exit code for a PixelTestGame-derived test in
    // one line, matching this project's existing single-shot example main() convention.
    //
    // Task 470: headless-safe pre-flight check. Constructing the real Game/GraphicsDevice below
    // throws a std::runtime_error out of SDL_InitSubSystem(SDL_INIT_VIDEO) if no GPU/display is
    // available at all (e.g. a machine with no X server and no Xvfb) -- detect that up front and
    // skip cleanly (kSkipExitCode) instead of crashing with an uncaught exception.
    template <typename TGame>
    int RunPixelTest()
    {
        static_assert(std::is_base_of_v<PixelTestGame, TGame>,
                      "RunPixelTest<TGame>: TGame must derive from CNA::Examples::PixelTestGame");

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            std::printf("[SKIP] no GPU/display available (SDL_InitSubSystem(SDL_INIT_VIDEO) "
                        "failed: %s)\n", SDL_GetError());
            return kSkipExitCode;
        }
        // Release the probe -- the real Game/GraphicsDevice construction below re-acquires the
        // subsystem itself (SDL reference-counts SDL_InitSubSystem/SDL_QuitSubSystem pairs, so
        // this leaves it exactly as this function found it).
        SDL_QuitSubSystem(SDL_INIT_VIDEO);

        // LLGL-55: a real display is present (the probe above succeeded), but the LLGL Vulkan
        // module still cannot actually PRESENT on it when the X server has no DRI3 support --
        // confirmed, not guessed: LLGL::VKSwapChain's own surface creation throws
        // "failed to create Vulkan swap-chain (error code = VK_ERROR_SURFACE_LOST_KHR)" out of
        // the GraphicsDevice/Game constructor itself, well after this function's own display
        // probe already passed. Uncaught, this used to abort the whole test process (every
        // Vulkan-selecting LLGL test on a DRI3-less Xvfb) instead of reporting a clean result --
        // this is a genuine infrastructure gap (this renderer's OWN choice of renderer module vs.
        // what the X server actually supports), not a rendering defect, so it is reported the
        // same way "no display at all" already is: SKIPPED, not FAILED, and NOT silently treated
        // as a pass. The substring match is deliberately narrow (this exact LLGL/Vulkan surface
        // error, not `std::exception` in general) so a genuine Vulkan pipeline/rendering bug
        // still surfaces as an uncaught exception -- a real crash, correctly reported as FAILED.
        try
        {
            TGame game;
            game.Run();
            return game.getResultProperty();
        }
        catch (const std::exception& ex)
        {
            const std::string what = ex.what();
            if (what.find("VK_ERROR_SURFACE_LOST_KHR") != std::string::npos)
            {
                std::printf("[SKIP] Vulkan WSI unavailable on this display (no DRI3?): %s\n",
                            ex.what());
                return kSkipExitCode;
            }
            throw;
        }
    }
}
