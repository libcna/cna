// SPDX-License-Identifier: MS-PL
// plans/plan_direct2d.md D2D-128: the shared pixel-oracle helpers of the Direct2D example tests.
//
// Every Direct2D test needs the same two things -- a tolerance comparison and a "read one pixel,
// print PASS/FAIL with both colours, accumulate the verdict" step -- and each one had grown its
// own copy. One copy silently drifting from another is how two tests end up disagreeing about what
// a tolerance means, so the oracle lives here once.
#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <cstdlib>

namespace CnaDirect2DTestSupport
{
    /// Default per-channel tolerance. Wide enough for the 1-2 LSB an 8-bit tint or blend can round
    /// differently between an integer and a float pipeline, narrow enough that a wrong texel, a
    /// swapped channel, or a missing blend never fits inside it.
    inline constexpr int kDefaultTolerance = 4;

    /**
     * @brief Compares two colours channel by channel within a tolerance.
     *
     * @param actual Colour read back from the device.
     * @param expected Colour the contract requires.
     * @param tolerance Maximum permitted absolute difference per channel.
     * @return True when every channel is within the tolerance.
     */
    [[nodiscard]] inline bool Matches(const Microsoft::Xna::Framework::Color& actual,
                                      const Microsoft::Xna::Framework::Color& expected,
                                      int tolerance = kDefaultTolerance)
    {
        return std::abs(static_cast<int>(actual.getRProperty()) - expected.getRProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getGProperty()) - expected.getGProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getBProperty()) - expected.getBProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getAProperty()) - expected.getAProperty()) <= tolerance;
    }

    /**
     * @brief Reads single backbuffer pixels, reports each verdict, and accumulates the result.
     *
     * The printed line carries both colours on every outcome, not only on failure: a Direct2D run
     * on an unfamiliar runtime is far easier to diagnose from the values it actually produced than
     * from a bare pass count.
     */
    class PixelChecker
    {
    public:
        /**
         * @brief Binds a checker to one device and the caller's running verdict.
         *
         * @param device Device whose backbuffer is read.
         * @param passed Verdict accumulated across every check; set to false by any failure.
         */
        PixelChecker(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, bool& passed)
            : device_(device), passed_(passed)
        {
        }

        /**
         * @brief Reads one backbuffer pixel and compares it against the expected colour.
         *
         * @param label Human-readable name of the contract being checked.
         * @param x Backbuffer column.
         * @param y Backbuffer row.
         * @param expected Colour the contract requires.
         * @param tolerance Maximum permitted absolute difference per channel.
         */
        void Check(const char* label, int x, int y,
                   const Microsoft::Xna::Framework::Color& expected,
                   int tolerance = kDefaultTolerance)
        {
            Microsoft::Xna::Framework::Color actual(0, 0, 0, 0);
            const Microsoft::Xna::Framework::Rectangle region(x, y, 1, 1);
            device_.GetBackBufferData(&region, &actual, 0, 1);
            const bool matches = Matches(actual, expected, tolerance);
            std::printf("[%s] %s: got=(%d,%d,%d,%d), expected=(%d,%d,%d,%d)\n",
                        matches ? "PASS" : "FAIL", label,
                        actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                        actual.getAProperty(), expected.getRProperty(), expected.getGProperty(),
                        expected.getBProperty(), expected.getAProperty());
            passed_ = passed_ && matches;
        }

        /**
         * @brief Reports a verdict the caller computed itself.
         *
         * @param label Human-readable name of the contract being checked.
         * @param condition True when the contract held.
         */
        void Report(const char* label, bool condition)
        {
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
            passed_ = passed_ && condition;
        }

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        bool& passed_;
    };
}
