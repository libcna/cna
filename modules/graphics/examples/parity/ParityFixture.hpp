// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-207: the shared EasyGL<->WebGPU behavioural fixture harness.
//
// ## What problem this solves
//
// Before this header, a behavioural WebGPU row was verified by writing a WebGPU-only test, and the
// same behaviour on the reference renderer was verified by a separate, independently written EasyGL
// test. Two tests of one behaviour are two oracles, and two oracles can disagree without either of
// them failing. `plans/plan_webgpu.md`'s Phase 70 therefore makes the shared fixture the default and
// the renderer-specific test the exception (native lifecycle / validation detail only).
//
// ## The convention
//
// A parity fixture is ONE renderer-neutral `.cpp` under this directory -- no `#ifdef`, no
// renderer-specific include, no renderer name anywhere in it -- that:
//
//   1. derives from `CNA::Parity::ParityFixture` and implements `RunFixture()`;
//   2. issues only PUBLIC XNA calls;
//   3. states its EXPECTED SEMANTICS as programmatic assertions in the fixture source itself, so
//      both renderers are judged by the same oracle rather than by two hand-written ones;
//   4. ends with `CNA_PARITY_FIXTURE_MAIN(<class>)`.
//
// `modules/graphics/examples/parity/ParityFixtures.cmake` lists the fixture names; each renderer's
// `examples/CMakeLists.txt` calls `cna_register_parity_fixtures()` once and gets one executable and
// one CTest per fixture. Adding a parity case is therefore "write the fixture and add its name to
// one list" -- not "write two tests and a runner".
//
// ## Two layers of verification, deliberately
//
// * **Programmatic assertions (primary).** Every `Expect*` below prints one `[PASS]`/`[FAIL]` line
//   and folds into the process exit code, exactly like `PixelTestGame`. These are the oracle. They
//   are written to be renderer-independent: prefer a RELATIONAL assertion (this region is brighter
//   than that one; these two regions differ by at least N) over an absolute colour wherever the
//   exact shade is a legitimate implementation detail.
// * **A raw frame dump (secondary).** Run the executable with a path argument and it also writes
//   the whole backbuffer as raw R,G,B,A bytes -- byte-for-byte the format
//   `cross_renderer_diagnostic_compare.cpp` already reads -- so `cna_diag_compare` can diff one
//   renderer's frame against another's. `scripts/run-parity-fixture.sh` drives that pair.
//
// ## The tolerance convention (`WEBGPU-193`/`WEBGPU-207`)
//
// Rasterization fill rules legitimately differ at primitive EDGES (`WEBGPU-123` measured EasyGL
// diverging from three other renderers on 57 triangle-edge pixels and nowhere else). So:
//
//   * sample INTERIOR regions, never a boundary pixel (`kEdgeInset` is the inset a fixture should
//     keep from any geometry edge it sampled from);
//   * compare a region AVERAGE rather than a single texel, so one stray edge pixel cannot decide a
//     verdict;
//   * use `kShadingTolerance` for a lit/blended colour and 0 for a flat unshaded one -- and say in
//     the call site WHY a non-zero tolerance is justified.
//
// A large difference is never papered over with a wide tolerance: an assertion that needs more than
// `kShadingTolerance` is a finding, and belongs in the plan rather than in the tolerance argument.

#pragma once

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Parity
{
    /** @brief The square backbuffer edge every parity fixture uses unless it asks for another. */
    inline constexpr int kDefaultFixtureSize = 128;

    /**
     * @brief Pixels a fixture should keep between a sampled region and the nearest geometry edge.
     *
     * `WEBGPU-123` measured the whole cross-renderer disagreement of a filled triangle to be its
     * edge pixels. Four is comfortably more than the one-pixel fill-rule ambiguity and still leaves
     * a usable interior in a 128x128 frame.
     */
    inline constexpr int kEdgeInset = 4;

    /**
     * @brief The per-channel tolerance a lit, filtered or blended colour comparison may use.
     *
     * Justified by arithmetic order, not by hope: two renderers evaluating the same Blinn-Phong sum
     * in a different order, or sampling the same texel through a different but equally conforming
     * filter, land within a few LSBs of each other. Anything wider hides a real difference.
     */
    inline constexpr int kShadingTolerance = 6;

    /**
     * @brief The dump path taken from `argv[1]`, or empty when the fixture was run without one.
     *
     * A file-scope inline so `CNA_PARITY_FIXTURE_MAIN` can set it before the fixture -- which is
     * default-constructed by `CNA::Examples::RunPixelTest` -- exists.
     *
     * @return The mutable storage holding the dump path.
     */
    inline std::string& DumpPathStorageEXT()
    {
        static std::string path;
        return path;
    }

    /**
     * @brief A cell grid over the fixture's backbuffer, in clip space and in pixels.
     *
     * Most parity fixtures are "draw the same thing N ways, side by side, and compare the cells",
     * so the mapping from a cell index to a clip-space quad and to the interior region that is safe
     * to sample lives here once instead of in every fixture.
     */
    struct ParityGrid
    {
        /** @brief Backbuffer width in pixels. */
        int width = kDefaultFixtureSize;
        /** @brief Backbuffer height in pixels. */
        int height = kDefaultFixtureSize;
        /** @brief How many cells across. */
        int columns = 1;
        /** @brief How many cells down. */
        int rows = 1;
        /** @brief Pixels each quad keeps inside its cell, so two neighbours never share an edge. */
        int quadMargin = 2;
        /** @brief Pixels the sampled interior keeps inside the cell. */
        int sampleInset = kEdgeInset + 4;

        /** @brief One cell's width in pixels. @return The width. */
        [[nodiscard]] int getCellWidthProperty() const { return width / columns; }
        /** @brief One cell's height in pixels. @return The height. */
        [[nodiscard]] int getCellHeightProperty() const { return height / rows; }

        /** @brief Converts a horizontal pixel coordinate to clip space. @param px The pixel. @return The clip x. */
        [[nodiscard]] float ToClipX(float px) const
        {
            return px / static_cast<float>(width) * 2.0f - 1.0f;
        }
        /** @brief Converts a vertical pixel coordinate to clip space. @param px The pixel. @return The clip y. */
        [[nodiscard]] float ToClipY(float px) const
        {
            return 1.0f - px / static_cast<float>(height) * 2.0f;
        }

        /**
         * @brief One cell's quad corners in clip space, in the order top-left, bottom-left,
         *        bottom-right, top-right.
         *
         * @param column The cell's column.
         * @param row The cell's row.
         * @return The four corners, at z = 0.
         */
        [[nodiscard]] std::array<Microsoft::Xna::Framework::Vector3, 4> QuadCorners(
            int column, int row) const
        {
            using Microsoft::Xna::Framework::Vector3;
            const float left = ToClipX(static_cast<float>(column * getCellWidthProperty() + quadMargin));
            const float right = ToClipX(static_cast<float>((column + 1) * getCellWidthProperty() - quadMargin));
            const float top = ToClipY(static_cast<float>(row * getCellHeightProperty() + quadMargin));
            const float bottom = ToClipY(static_cast<float>((row + 1) * getCellHeightProperty() - quadMargin));
            return {Vector3(left, top, 0.0f), Vector3(left, bottom, 0.0f),
                    Vector3(right, bottom, 0.0f), Vector3(right, top, 0.0f)};
        }

        /**
         * @brief The region of one cell that is safe to sample.
         *
         * @param column The cell's column.
         * @param row The cell's row.
         * @return The interior rectangle in backbuffer pixels.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Rectangle Interior(int column, int row) const
        {
            return Microsoft::Xna::Framework::Rectangle(
                column * getCellWidthProperty() + sampleInset,
                row * getCellHeightProperty() + sampleInset,
                getCellWidthProperty() - 2 * sampleInset,
                getCellHeightProperty() - 2 * sampleInset);
        }
    };

    /**
     * @brief Base class for a renderer-neutral behavioural parity fixture.
     *
     * Owns the `GraphicsDeviceManager` so every renderer renders the fixture at the same fixed,
     * unscaled resolution (`PresentationMode::NativeBackBuffer`): a parity comparison must not be
     * decided by a letterbox rectangle. Derived fixtures implement @ref RunFixture and use the
     * `Expect*` helpers below plus everything `CNA::Examples::PixelTestGame` already provides.
     */
    class ParityFixture : public CNA::Examples::PixelTestGame
    {
    public:
        /**
         * @brief Creates the fixture's device at a fixed, unscaled backbuffer size.
         *
         * @param width Backbuffer width in pixels.
         * @param height Backbuffer height in pixels.
         */
        explicit ParityFixture(int width = kDefaultFixtureSize, int height = kDefaultFixtureSize,
                               Microsoft::Xna::Framework::PresentationMode presentation =
                                   Microsoft::Xna::Framework::PresentationMode::NativeBackBuffer)
            : width_(width), height_(height)
        {
            using Microsoft::Xna::Framework::GraphicsDeviceManager;
            gdm_ = std::make_unique<GraphicsDeviceManager>(this);
            gdm_->setPreferredBackBufferWidthProperty(width);
            gdm_->setPreferredBackBufferHeightProperty(height);
            // NativeBackBuffer by default: no scaling, so the fixture's pixel coordinates are the
            // backbuffer's own and a parity verdict is never decided by a letterbox rectangle. A
            // fixture whose SUBJECT is the presentation rectangle asks for another mode here.
            gdm_->setPreferredPresentationModeProperty(presentation);
        }

        /** @brief The fixture's backbuffer width. @return Width in pixels. */
        [[nodiscard]] int getFixtureWidthProperty() const noexcept { return width_; }

        /** @brief The fixture's backbuffer height. @return Height in pixels. */
        [[nodiscard]] int getFixtureHeightProperty() const noexcept { return height_; }

        /**
         * @brief Runs the fixture's own body, then writes the frame dump when one was requested.
         *
         * `final` on purpose: a fixture overrides @ref RunFixture, so the dump can never be
         * accidentally skipped by a derived class that forgot to call up.
         */
        void RunTest() final
        {
            RunFixture();
            if (!DumpPathStorageEXT().empty()) WriteFrameDumpEXT(DumpPathStorageEXT());
        }

    protected:
        /** @brief The fixture's draw calls and assertions. */
        virtual void RunFixture() = 0;

        /**
         * @brief Reads one rectangular region of the live backbuffer.
         *
         * @param region The region to read, in backbuffer pixels.
         * @return The region's pixels in row-major order.
         */
        [[nodiscard]] std::vector<Microsoft::Xna::Framework::Color> ReadRegion(
            const Microsoft::Xna::Framework::Rectangle& region)
        {
            using Microsoft::Xna::Framework::Color;
            const std::size_t count = static_cast<std::size_t>(region.Width) *
                                      static_cast<std::size_t>(region.Height);
            std::vector<Color> pixels(count, Color(0, 0, 0, 0));
            getGraphicsDeviceProperty().GetBackBufferData(&region, pixels.data(), 0,
                                                          static_cast<int>(count));
            return pixels;
        }

        /**
         * @brief The per-channel mean of a region, rounded to the nearest integer.
         *
         * The averaging is what makes a comparison robust: a single stray edge pixel cannot decide
         * a verdict, and a whole-region difference still shows up at full strength.
         *
         * @param region The region to average.
         * @return The mean colour.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Color Average(
            const Microsoft::Xna::Framework::Rectangle& region)
        {
            using Microsoft::Xna::Framework::Color;
            const std::vector<Color> pixels = ReadRegion(region);
            if (pixels.empty()) return Color(0, 0, 0, 0);
            long r = 0, g = 0, b = 0, a = 0;
            for (const Color& p : pixels)
            {
                r += p.getRProperty(); g += p.getGProperty();
                b += p.getBProperty(); a += p.getAProperty();
            }
            const long n = static_cast<long>(pixels.size());
            const auto mean = [n](long sum) {
                return static_cast<SharpRuntime::bytecs>((sum + n / 2) / n);
            };
            return Color(mean(r), mean(g), mean(b), mean(a));
        }

        /**
         * @brief Asserts a precondition the fixture needs before it draws anything.
         *
         * plans/plan_webgpu.md WEBGPU-172. A fixture that requires a capability asserts it rather
         * than skipping on it: every renderer this harness runs against is expected to have it, so
         * a renderer that stopped supporting it is the finding, and a silent skip would hide it.
         *
         * @param condition The precondition.
         * @param label What the precondition is, for the PASS/FAIL line.
         * @return Whether the precondition held.
         */
        bool Require(bool condition, const char* label)
        {
            std::printf("[%s] precondition: %s\n", condition ? "PASS" : "FAIL", label);
            if (!condition) MarkFailedEXT();
            return condition;
        }

        /**
         * @brief Asserts a region's mean colour, RGB only, within a tolerance.
         *
         * @param label What this check proves, for the PASS/FAIL line.
         * @param region The region to average.
         * @param expected The expected mean colour.
         * @param tolerance Per-channel absolute tolerance; 0 for a flat unshaded colour.
         * @return Whether the check passed.
         */
        bool ExpectAverage(const char* label,
                           const Microsoft::Xna::Framework::Rectangle& region,
                           const Microsoft::Xna::Framework::Color& expected,
                           int tolerance = 0)
        {
            using Microsoft::Xna::Framework::Color;
            const Color got = Average(region);
            const auto close = [tolerance](int x, int y) { return std::abs(x - y) <= tolerance; };
            const bool pass = close(got.getRProperty(), expected.getRProperty()) &&
                              close(got.getGProperty(), expected.getGProperty()) &&
                              close(got.getBProperty(), expected.getBProperty());
            std::printf("[%s] %s: mean=(%d,%d,%d) expected=(%d,%d,%d) +/-%d\n",
                        pass ? "PASS" : "FAIL", label,
                        got.getRProperty(), got.getGProperty(), got.getBProperty(),
                        expected.getRProperty(), expected.getGProperty(),
                        expected.getBProperty(), tolerance);
            if (!pass) MarkFailedEXT();
            return pass;
        }

        /**
         * @brief Asserts two regions differ by at least @p minDelta in some channel.
         *
         * This is the assertion that stops "both renderers drew nothing" from passing: a fixture
         * proving an attribute reaches the shader states what that attribute CHANGED, and a frame
         * where it changed nothing fails here whatever its absolute colour is.
         *
         * @param label What this check proves.
         * @param a The first region.
         * @param b The second region.
         * @param minDelta The smallest per-channel difference that counts as materially different.
         * @return Whether the check passed.
         */
        bool ExpectDistinct(const char* label,
                            const Microsoft::Xna::Framework::Rectangle& a,
                            const Microsoft::Xna::Framework::Rectangle& b,
                            int minDelta)
        {
            using Microsoft::Xna::Framework::Color;
            const Color ca = Average(a);
            const Color cb = Average(b);
            const int delta = std::max({std::abs(ca.getRProperty() - cb.getRProperty()),
                                        std::abs(ca.getGProperty() - cb.getGProperty()),
                                        std::abs(ca.getBProperty() - cb.getBProperty())});
            const bool pass = delta >= minDelta;
            std::printf("[%s] %s: (%d,%d,%d) vs (%d,%d,%d), max channel delta %d >= %d\n",
                        pass ? "PASS" : "FAIL", label,
                        ca.getRProperty(), ca.getGProperty(), ca.getBProperty(),
                        cb.getRProperty(), cb.getGProperty(), cb.getBProperty(),
                        delta, minDelta);
            if (!pass) MarkFailedEXT();
            return pass;
        }

        /**
         * @brief Asserts one region's mean luminance exceeds another's by at least @p minDelta.
         *
         * Exactly the shape a lighting fixture needs: "the face turned towards the light is
         * brighter than the one turned away" is true on every conforming renderer, while the two
         * absolute shades are not required to match to the LSB.
         *
         * @param label What this check proves.
         * @param brighter The region expected to be brighter.
         * @param darker The region expected to be darker.
         * @param minDelta The smallest luminance difference that counts.
         * @return Whether the check passed.
         */
        bool ExpectBrighter(const char* label,
                            const Microsoft::Xna::Framework::Rectangle& brighter,
                            const Microsoft::Xna::Framework::Rectangle& darker,
                            int minDelta)
        {
            using Microsoft::Xna::Framework::Color;
            const Color hi = Average(brighter);
            const Color lo = Average(darker);
            const auto luma = [](const Color& c) {
                return c.getRProperty() + c.getGProperty() + c.getBProperty();
            };
            const int delta = (luma(hi) - luma(lo)) / 3;
            const bool pass = delta >= minDelta;
            std::printf("[%s] %s: (%d,%d,%d) over (%d,%d,%d), luminance delta %d >= %d\n",
                        pass ? "PASS" : "FAIL", label,
                        hi.getRProperty(), hi.getGProperty(), hi.getBProperty(),
                        lo.getRProperty(), lo.getGProperty(), lo.getBProperty(),
                        delta, minDelta);
            if (!pass) MarkFailedEXT();
            return pass;
        }

        /**
         * @brief Asserts every pixel of a region is within @p maxSpread of that region's mean.
         *
         * Proves a region really is the flat interior a fixture claims to be sampling, so a later
         * average-based assertion cannot be reading across a geometry edge by accident.
         *
         * @param label What this check proves.
         * @param region The region to test.
         * @param maxSpread The largest per-channel deviation from the mean that still counts as flat.
         * @return Whether the check passed.
         */
        bool ExpectFlat(const char* label,
                        const Microsoft::Xna::Framework::Rectangle& region,
                        int maxSpread)
        {
            using Microsoft::Xna::Framework::Color;
            const Color mean = Average(region);
            int worst = 0;
            for (const Color& p : ReadRegion(region))
            {
                worst = std::max({worst,
                                  std::abs(p.getRProperty() - mean.getRProperty()),
                                  std::abs(p.getGProperty() - mean.getGProperty()),
                                  std::abs(p.getBProperty() - mean.getBProperty())});
            }
            const bool pass = worst <= maxSpread;
            std::printf("[%s] %s: mean=(%d,%d,%d) worst deviation %d <= %d\n",
                        pass ? "PASS" : "FAIL", label,
                        mean.getRProperty(), mean.getGProperty(), mean.getBProperty(),
                        worst, maxSpread);
            if (!pass) MarkFailedEXT();
            return pass;
        }

        /**
         * @brief How many pixels of a region differ from @p background.
         *
         * The measurement a coverage question needs -- "is this interior empty", "did these edges
         * get drawn" -- which an average cannot answer: a thin bright line and a faint wash have
         * the same mean.
         *
         * @param region The region to count in.
         * @param background The colour that counts as "not drawn".
         * @param tolerance Per-channel slack when deciding a pixel is still the background.
         * @return How many pixels differ from @p background.
         */
        [[nodiscard]] int CountLit(const Microsoft::Xna::Framework::Rectangle& region,
                                   const Microsoft::Xna::Framework::Color& background,
                                   int tolerance = 2)
        {
            using Microsoft::Xna::Framework::Color;
            int lit = 0;
            for (const Color& p : ReadRegion(region))
            {
                const auto close = [tolerance](int x, int y) { return std::abs(x - y) <= tolerance; };
                if (!(close(p.getRProperty(), background.getRProperty()) &&
                      close(p.getGProperty(), background.getGProperty()) &&
                      close(p.getBProperty(), background.getBProperty())))
                    ++lit;
            }
            return lit;
        }

        /**
         * @brief Asserts a region's drawn-pixel count falls in an inclusive range.
         *
         * @param label What this check proves.
         * @param region The region to count in.
         * @param background The colour that counts as "not drawn".
         * @param minLit The smallest acceptable count.
         * @param maxLit The largest acceptable count.
         * @param tolerance Per-channel slack when deciding a pixel is still the background.
         * @return Whether the check passed.
         */
        bool ExpectLitCount(const char* label,
                            const Microsoft::Xna::Framework::Rectangle& region,
                            const Microsoft::Xna::Framework::Color& background,
                            int minLit, int maxLit, int tolerance = 2)
        {
            const int lit = CountLit(region, background, tolerance);
            const int area = region.Width * region.Height;
            const bool pass = lit >= minLit && lit <= maxLit;
            std::printf("[%s] %s: %d of %d pixels drawn, expected %d..%d\n",
                        pass ? "PASS" : "FAIL", label, lit, area, minLit, maxLit);
            if (!pass) MarkFailedEXT();
            return pass;
        }

        /**
         * @brief Writes the whole backbuffer as raw R,G,B,A bytes for `cna_diag_compare`.
         *
         * Byte order goes through `Color`'s accessors, not its packed layout, so the dump format is
         * identical to `cross_renderer_diagnostic_scene.cpp`'s and the existing comparator reads it
         * unchanged.
         *
         * @param path The file to write.
         */
        void WriteFrameDumpEXT(const std::string& path)
        {
            using Microsoft::Xna::Framework::Color;
            using Microsoft::Xna::Framework::Rectangle;
            const std::vector<Color> pixels = ReadRegion(Rectangle(0, 0, width_, height_));
            std::vector<std::uint8_t> rgba(pixels.size() * 4u);
            for (std::size_t i = 0; i < pixels.size(); ++i)
            {
                rgba[i * 4 + 0] = static_cast<std::uint8_t>(pixels[i].getRProperty());
                rgba[i * 4 + 1] = static_cast<std::uint8_t>(pixels[i].getGProperty());
                rgba[i * 4 + 2] = static_cast<std::uint8_t>(pixels[i].getBProperty());
                rgba[i * 4 + 3] = static_cast<std::uint8_t>(pixels[i].getAProperty());
            }
            std::FILE* file = std::fopen(path.c_str(), "wb");
            if (file == nullptr)
            {
                std::printf("[FAIL] frame dump: cannot open '%s' for writing\n", path.c_str());
                MarkFailedEXT();
                return;
            }
            std::fwrite(rgba.data(), 1, rgba.size(), file);
            std::fclose(file);
            std::printf("[dump] wrote %zu bytes (%dx%d RGBA8) to %s\n",
                        rgba.size(), width_, height_, path.c_str());
        }

    private:
        std::unique_ptr<Microsoft::Xna::Framework::GraphicsDeviceManager> gdm_;
        int width_ = kDefaultFixtureSize;
        int height_ = kDefaultFixtureSize;
    };
}

/**
 * @brief Declares a parity fixture's `main()`: optional dump path in `argv[1]`, then the fixture.
 *
 * The dump path has to be stored before the fixture is constructed because
 * `CNA::Examples::RunPixelTest` default-constructs it; that is what `DumpPathStorageEXT` is for.
 */
#define CNA_PARITY_FIXTURE_MAIN(FixtureType)                                    \
    int main(int argc, char** argv)                                             \
    {                                                                           \
        if (argc > 1) CNA::Parity::DumpPathStorageEXT() = argv[1];               \
        return CNA::Examples::RunPixelTest<FixtureType>();                      \
    }
