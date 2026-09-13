// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-301: the routes at size.
//
// The committed corpus exists to cover *shapes*: every fixture in it is a few kilobytes, because
// what those tests ask is what a format means and not what it costs. A pipeline can answer every
// one of them correctly and still be unusable on a real project, and the way that happens is
// almost always the same -- something in a route is quadratic in its input and nobody noticed
// because no test ever gave it a large one.
//
// So each route here is built at two sizes, four times apart, with the sources authored by the
// test rather than downloaded. What is asserted is the *shape* of the cost: four times the input
// must not cost sixteen times the time. The measurement is deliberately generous -- this runs on a
// shared build machine and wall clock there is not a property worth failing a build over -- and
// each size is run three times with the fastest kept, which is what makes a noisy machine's slow
// run harmless. The numbers themselves are printed, and the ones worth keeping live in
// docs/content-pipeline-benchmark.md where a figure can be dated.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CNA/Internal/HostProcess.hpp"

namespace
{
#if !defined(CNA_CONTENT_TOOL_PATH)
#error "CNA_CONTENT_TOOL_PATH must be baked in; see cmake/UnitTests.cmake."
#endif

    class Scratch
    {
    public:
        explicit Scratch(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnapp301_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_ / "src");
            std::filesystem::create_directories(path_ / "out");
        }
        ~Scratch()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return path_ / "src"; }
        [[nodiscard]] std::filesystem::path Output() const { return path_ / "out"; }

    private:
        std::filesystem::path path_;
    };

    void PutU16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    }

    void PutU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    void Write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    /** @brief An uncompressed 32-bit TGA of the given side, with deterministic pixels. */
    void WriteTarga(const std::filesystem::path& path, const std::uint32_t side)
    {
        std::vector<std::uint8_t> bytes{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        PutU16(bytes, static_cast<std::uint16_t>(side));
        PutU16(bytes, static_cast<std::uint16_t>(side));
        bytes.push_back(32u);
        bytes.push_back(0u);
        bytes.reserve(bytes.size() + static_cast<std::size_t>(side) * side * 4u);
        for (std::uint32_t y = 0u; y < side; ++y)
        {
            for (std::uint32_t x = 0u; x < side; ++x)
            {
                bytes.push_back(static_cast<std::uint8_t>(x & 0xFFu));
                bytes.push_back(static_cast<std::uint8_t>(y & 0xFFu));
                bytes.push_back(static_cast<std::uint8_t>((x ^ y) & 0xFFu));
                bytes.push_back(255u);
            }
        }
        Write(path, bytes);
    }

    /** @brief A mono 16-bit 44100 Hz WAV of the given frame count. */
    void WriteWave(const std::filesystem::path& path, const std::uint32_t frames)
    {
        std::vector<std::uint8_t> pcm;
        pcm.reserve(static_cast<std::size_t>(frames) * 2u);
        for (std::uint32_t index = 0u; index < frames; ++index)
        {
            PutU16(pcm, static_cast<std::uint16_t>(static_cast<std::int16_t>(index * 37)));
        }
        std::vector<std::uint8_t> bytes;
        for (const char c : std::string("RIFF")) { bytes.push_back(static_cast<std::uint8_t>(c)); }
        PutU32(bytes, static_cast<std::uint32_t>(36u + pcm.size()));
        for (const char c : std::string("WAVEfmt ")) { bytes.push_back(static_cast<std::uint8_t>(c)); }
        PutU32(bytes, 16u);
        PutU16(bytes, 1u);
        PutU16(bytes, 1u);
        PutU32(bytes, 44100u);
        PutU32(bytes, 88200u);
        PutU16(bytes, 2u);
        PutU16(bytes, 16u);
        for (const char c : std::string("data")) { bytes.push_back(static_cast<std::uint8_t>(c)); }
        PutU32(bytes, static_cast<std::uint32_t>(pcm.size()));
        bytes.insert(bytes.end(), pcm.begin(), pcm.end());
        Write(path, bytes);
    }

    /** @brief An intermediate-XML `List[string]` of the given length. */
    void WriteXmlList(const std::filesystem::path& path, const std::uint32_t items)
    {
        std::ofstream stream(path);
        stream << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<XnaContent>\n"
               << "  <Asset Type=\"System.Collections.Generic.List[string]\">\n";
        for (std::uint32_t index = 0u; index < items; ++index)
        {
            stream << "    <Item>entry" << index << "</Item>\n";
        }
        stream << "  </Asset>\n</XnaContent>\n";
    }

    /**
     * @brief The fastest of three builds of one source, in milliseconds.
     *
     * A single source file needs an output *file* whose extension matches the container, and a
     * directory needs an output directory; the coordinator refuses either the other way round, so
     * the shape of the output follows the shape of the input.
     */
    double FastestBuildMilliseconds(const std::filesystem::path& source,
                                    const std::filesystem::path& output)
    {
        const bool tree = std::filesystem::is_directory(source);
        double fastest = 0.0;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            std::error_code error;
            std::filesystem::remove_all(output, error);
            std::filesystem::create_directories(output, error);
            const std::filesystem::path target = tree ? output : (output / "built.xnb");
            const auto started = std::chrono::steady_clock::now();
            const CNA::Internal::HostProcessResult ran = CNA::Internal::RunHostProcess(
                CNA_CONTENT_TOOL_PATH,
                {"build", source.string(), "-o", target.string(), "--format", "xnb", "--quiet"});
            const auto finished = std::chrono::steady_clock::now();
            EXPECT_TRUE(ran.started) << ran.failure;
            EXPECT_EQ(ran.exitCode, 0) << ran.standardOutput << ran.standardError;
            const double elapsed =
                std::chrono::duration<double, std::milli>(finished - started).count();
            if (attempt == 0 || elapsed < fastest) { fastest = elapsed; }
        }
        return fastest;
    }

    /**
     * @brief Reports whether four times the input cost more than a linear route should.
     *
     * The bound is eight rather than four: process start, the manifest and the staging directory
     * are a fixed cost the small build pays in full, so a perfectly linear route measures worse
     * than 4x at this scale. What eight cannot hide is the thing this exists to catch -- a
     * quadratic route measures sixteen.
     */
    void ExpectSubQuadratic(const std::string& route, const double small, const double large)
    {
        std::cout << "[  SCALING ] " << route << ": " << small << " ms -> " << large
                  << " ms (x" << (small > 0.0 ? large / small : 0.0) << " for 4x the input)"
                  << std::endl;
        if (small < 20.0)
        {
            // Below this the measurement is process start and scheduler noise, and a ratio taken
            // from it says nothing. Reported, not asserted.
            return;
        }
        EXPECT_LT(large, small * 8.0)
            << route << " costs " << (large / small) << "x for four times the input, which is the "
            << "shape of a quadratic route rather than a linear one";
    }
}

TEST(XnaRouteScaling, TheTextureRouteIsNotQuadraticInItsPixels)
{
    const Scratch scratch("texture");
    WriteTarga(scratch.Source() / "small.tga", 512u);
    WriteTarga(scratch.Source() / "large.tga", 1024u);
    const double small = FastestBuildMilliseconds(scratch.Source() / "small.tga",
                                                  scratch.Output() / "s");
    const double large = FastestBuildMilliseconds(scratch.Source() / "large.tga",
                                                  scratch.Output() / "l");
    ExpectSubQuadratic("texture (512x512 -> 1024x1024)", small, large);
}

TEST(XnaRouteScaling, TheSoundEffectRouteIsNotQuadraticInItsFrames)
{
    const Scratch scratch("audio");
    WriteWave(scratch.Source() / "small.wav", 44100u * 5u);
    WriteWave(scratch.Source() / "large.wav", 44100u * 20u);
    const double small = FastestBuildMilliseconds(scratch.Source() / "small.wav",
                                                  scratch.Output() / "s");
    const double large = FastestBuildMilliseconds(scratch.Source() / "large.wav",
                                                  scratch.Output() / "l");
    ExpectSubQuadratic("sound effect (5 s -> 20 s)", small, large);
}

TEST(XnaRouteScaling, TheIntermediateXmlRouteIsNotQuadraticInItsElements)
{
    const Scratch scratch("xml");
    WriteXmlList(scratch.Source() / "small.xml", 25000u);
    WriteXmlList(scratch.Source() / "large.xml", 100000u);
    const double small = FastestBuildMilliseconds(scratch.Source() / "small.xml",
                                                  scratch.Output() / "s");
    const double large = FastestBuildMilliseconds(scratch.Source() / "large.xml",
                                                  scratch.Output() / "l");
    ExpectSubQuadratic("intermediate XML (25k -> 100k elements)", small, large);
}

// And the whole tree at once: the coordinator's own bookkeeping -- discovery, the manifest, the
// staging directory, the ownership maps -- is per asset, and a build of many small assets is where
// a quadratic in the *number* of them would show up rather than in any one route.
TEST(XnaRouteScaling, TheCoordinatorIsNotQuadraticInTheNumberOfAssets)
{
    const Scratch scratch("many");
    const std::filesystem::path few = scratch.Source() / "few";
    const std::filesystem::path many = scratch.Source() / "many";
    std::filesystem::create_directories(few);
    std::filesystem::create_directories(many);
    for (std::uint32_t index = 0u; index < 400u; ++index)
    {
        WriteTarga(few / ("asset" + std::to_string(index) + ".tga"), 8u);
    }
    for (std::uint32_t index = 0u; index < 1600u; ++index)
    {
        WriteTarga(many / ("asset" + std::to_string(index) + ".tga"), 8u);
    }
    const double small = FastestBuildMilliseconds(few, scratch.Output() / "s");
    const double large = FastestBuildMilliseconds(many, scratch.Output() / "l");
    ExpectSubQuadratic("coordinator (400 -> 1600 assets)", small, large);
}
