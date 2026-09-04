// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-93: the Model route at size.
//
// The committed glTF corpus exists to cover *shapes*, not sizes -- the largest fixture in it is a
// few hundred kilobytes -- so the performance row was honestly missing its large-model line rather
// than extrapolating one. `tools/xnb/generate_large_model.py` authors a deterministic source
// instead of downloading one, which would be neither reproducible nor licensable.
//
// This test is not a timing gate. Wall clock on a shared build machine is not a property worth
// failing a build over, and the measured figures live in `docs/content-pipeline-benchmark.md`
// where a number can be dated. What it asserts is what a benchmark *cannot*: that the generated
// sources really do describe the scale they claim, that both build, and that the cost that shows
// up in the output scales with the input rather than exploding -- which is the failure a
// benchmark on one size would miss entirely.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/HostProcess.hpp"

namespace
{
#if !defined(CNA_CONTENT_TOOL_PATH)
#error "CNA_CONTENT_TOOL_PATH must be baked in; see cmake/UnitTests.cmake (XNAP-44)."
#endif

    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_largemodel_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    /** @brief What one generated scale weighs, once it has been built. */
    struct Measurement
    {
        std::uintmax_t sourceBytes = 0u;
        std::uintmax_t outputBytes = 0u;
    };

    /** @brief The interpreter the generator runs under, overridable for an unusual host. */
    [[nodiscard]] std::string Python()
    {
        const char* const configured = std::getenv("CNA_PYTHON");
        return configured == nullptr ? "python3" : configured;
    }

    /** @brief Generates one scale and builds it, or returns false with why. */
    [[nodiscard]] bool GenerateAndBuild(const std::string& scale,
                                        const std::filesystem::path& root,
                                        const std::string& format, Measurement& measurement,
                                        std::string& failure)
    {
        const std::filesystem::path source = root / scale / "src";
        const std::filesystem::path output = root / scale / ("out-" + format);
        std::filesystem::create_directories(source);

        const CNA::Internal::HostProcessResult generated = CNA::Internal::RunHostProcess(
            Python(), {"tools/xnb/generate_large_model.py", "--scale", scale, "--out",
                       (source / "model.glb").string()});
        if (!generated.started)
        {
            failure = "the fixture generator could not be run: " + generated.failure;
            return false;
        }
        if (generated.exitCode != 0)
        {
            failure = "the fixture generator failed: " + generated.standardError;
            return false;
        }

        const CNA::Internal::HostProcessResult built = CNA::Internal::RunHostProcess(
            CNA_CONTENT_TOOL_PATH,
            {"build", source.string(), "-o", output.string(), "--format", format, "--quiet"});
        if (!built.started || built.exitCode != 0)
        {
            failure = "the build failed: " + built.standardOutput + built.standardError;
            return false;
        }

        const std::filesystem::path compiled = output / ("model." + format);
        if (!std::filesystem::is_regular_file(compiled))
        {
            failure = "the build reported success and published no " + compiled.filename().string();
            return false;
        }
        measurement.sourceBytes = std::filesystem::file_size(source / "model.glb");
        measurement.outputBytes = std::filesystem::file_size(compiled);
        return true;
    }
} // namespace

TEST(LargeModelScalingTest, TheGeneratedScalesDescribeTheSizesTheyClaim)
{
    // The generator is the fixture, so its own arithmetic has to be checked: a scale that
    // silently produced the same model twice would make every scaling conclusion below vacuous.
    const CNA::Internal::HostProcessResult listed =
        CNA::Internal::RunHostProcess(Python(), {"tools/xnb/generate_large_model.py", "--list"});
    ASSERT_TRUE(listed.started) << listed.failure;
    ASSERT_EQ(listed.exitCode, 0) << listed.standardError;

    // small, medium and large, each roughly an order of magnitude apart.
    EXPECT_NE(listed.standardOutput.find("small"), std::string::npos) << listed.standardOutput;
    EXPECT_NE(listed.standardOutput.find("medium"), std::string::npos) << listed.standardOutput;
    EXPECT_NE(listed.standardOutput.find("large"), std::string::npos) << listed.standardOutput;

    std::vector<long long> vertices;
    std::size_t position = 0u;
    while ((position = listed.standardOutput.find(" vertices", position)) != std::string::npos)
    {
        std::size_t start = listed.standardOutput.find_last_not_of("0123456789", position - 1u);
        vertices.push_back(std::stoll(listed.standardOutput.substr(start + 1u, position - start)));
        position += 9u;
    }
    ASSERT_EQ(vertices.size(), 3u) << listed.standardOutput;
    EXPECT_GT(vertices[1], vertices[0] * 10) << "medium must be an order of magnitude over small";
    EXPECT_GT(vertices[2], vertices[1] * 10) << "large must be an order of magnitude over medium";
}

TEST(LargeModelScalingTest, ALargeModelBuildsToXnbAndItsOutputScalesWithItsInput)
{
    ScratchDirectory scratch("scaling");
    Measurement small;
    Measurement medium;
    std::string failure;

    // `large` is deliberately not built here: it is a ten-second build, and this is a correctness
    // test rather than the benchmark. `docs/content-pipeline-benchmark.md` records all three.
    ASSERT_TRUE(GenerateAndBuild("small", scratch.Path(), "xnb", small, failure)) << failure;
    ASSERT_TRUE(GenerateAndBuild("medium", scratch.Path(), "xnb", medium, failure)) << failure;

    ASSERT_GT(small.outputBytes, 0u);
    ASSERT_GT(medium.outputBytes, 0u);
    EXPECT_GT(medium.sourceBytes, small.sourceBytes * 10u)
        << "the two scales are not actually different sizes";

    // The interesting property: output bytes per source byte must stay in the same band. A Model
    // writer that duplicated a shared resource per part, or interned effects by identity rather
    // than by value, would show up here as a ratio that climbs with size -- which one measurement
    // at one size cannot see.
    const double smallRatio =
        static_cast<double>(small.outputBytes) / static_cast<double>(small.sourceBytes);
    const double mediumRatio =
        static_cast<double>(medium.outputBytes) / static_cast<double>(medium.sourceBytes);
    EXPECT_LT(mediumRatio, smallRatio * 1.5)
        << "output grew faster than input: " << smallRatio << " -> " << mediumRatio
        << " bytes out per byte in. Something in the Model writer is superlinear in part count.";
    EXPECT_GT(mediumRatio, smallRatio * 0.5)
        << "output grew far slower than input: " << smallRatio << " -> " << mediumRatio
        << ". Either the larger model lost data, or the smaller one carries fixed overhead this "
           "test should stop treating as proportional.";
}

TEST(LargeModelScalingTest, TheSameLargeSourceAlsoBuildsToCnb)
{
    // The Model route is shared: XNB is a second writer, not a second pipeline. A size that broke
    // only one of them would be a writer bug rather than an importer one, and this says which.
    ScratchDirectory scratch("cnb");
    Measurement medium;
    std::string failure;
    ASSERT_TRUE(GenerateAndBuild("medium", scratch.Path(), "cnb", medium, failure)) << failure;
    EXPECT_GT(medium.outputBytes, 0u);
}
