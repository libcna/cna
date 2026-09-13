// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-300: the same sources build to the same bytes, however
// many workers ran and whichever process ran them.
//
// A content build that is not deterministic breaks two things at once, quietly. Every incremental
// rebuild becomes a coin toss -- the manifest fingerprints the output, so an output that differs
// for no reason rebuilds everything downstream of it -- and every recorded interoperability result
// stops meaning anything, because the file somebody loaded in a genuine XNA runtime is not the file
// the next build produces. Neither failure announces itself.
//
// So this builds one corpus four times, in four separate processes, at four worker counts, and
// compares every byte. Separate processes rather than four calls in one, because a single process
// can be deterministic by accident: a cache warmed by the first build, a static initialised once,
// an allocator handing back the same addresses. Four processes share none of that.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "CNA/Internal/HostProcess.hpp"

namespace
{
#if !defined(CNA_CONTENT_TOOL_PATH)
#error "CNA_CONTENT_TOOL_PATH must be baked in; see cmake/UnitTests.cmake."
#endif

    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

    /**
     * @brief The corpus this builds, one source per route the tree can build without a compiler.
     *
     * The nine texture extensions are copied under distinct names on purpose: they all import to
     * the same logical name otherwise, which the coordinator refuses -- correctly, and not what is
     * being measured here.
     */
    struct CorpusFile
    {
        const char* from;
        const char* as;
    };

    std::vector<CorpusFile> Corpus(const bool includeXml = true)
    {
        std::vector<CorpusFile> files{
            {"tests/assets/xna40/texture/probe.png", "image_png.png"},
            {"tests/assets/xna40/texture/probe.bmp", "image_bmp.bmp"},
            {"tests/assets/xna40/texture/probe.tga", "image_tga.tga"},
            {"tests/assets/xna40/texture/probe.dds", "image_dds.dds"},
            {"tests/assets/xna40/texture/probe.jpg", "image_jpg.jpg"},
            {"tests/assets/xna40/texture/probe.hdr", "image_hdr.hdr"},
            {"tests/assets/xna40/texture/probe.pfm", "image_pfm.pfm"},
            {"tests/assets/xna40/texture/probe.ppm", "image_ppm.ppm"},
            {"tests/assets/xna40/texture/probe.dib", "image_dib.dib"},
            {"tests/assets/xna40/texture/font_sheet.png", "sheet.png"},
            {"tests/assets/xna40/media/tone_mono_44100.wav", "tone.wav"},
            {"tests/assets/xna40/media/mp3_mono_44100_128k.mp3", "song.mp3"},
            {"tests/assets/xna40/media/wma_mono_44100.wma", "music.wma"},
            {"tests/assets/xna40/media/wmv_64x48_15fps_silent.wmv", "clip.wmv"},
            {"tests/assets/xna40/model/quad_textured.x", "quad_textured.x"},
            {"tests/assets/xna40/model/surface.png", "surface.png"},
            {"tests/assets/xna40/model/fbx_quad_textured.fbx", "quad.fbx"},
            {"tests/assets/xna40/source/buildable.spritefont", "buildable.spritefont"},
            {"tests/assets/fonts/LiberationMono-Regular.ttf", "LiberationMono-Regular.ttf"},
        };
        if (includeXml)
        {
            // The `.xml` route is deliberately XNB-only, so a `.cnb` corpus leaves it out rather
            // than being refused for a decision that is recorded elsewhere (XNAPP-261).
            files.push_back({"tests/assets/xna40/source/probe.xml", "list.xml"});
            files.push_back({"tests/assets/xna40/source/xml_curve.xml", "curve.xml"});
        }
        return files;
    }

    /** @brief A source tree and as many output trees as a test asks for. */
    class Trees
    {
    public:
        explicit Trees(const std::string& label, const bool includeXml = true)
            : root_(std::filesystem::temp_directory_path() /
                    ("cna_xnapp300_" + label + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(Source());
            for (const CorpusFile& file : Corpus(includeXml))
            {
                std::error_code error;
                std::filesystem::copy_file(Locate(file.from), Source() / file.as, error);
            }
        }
        ~Trees()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
        Trees(const Trees&) = delete;
        Trees& operator=(const Trees&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return root_ / "src"; }
        [[nodiscard]] std::filesystem::path Output(const int index) const
        {
            return root_ / ("out" + std::to_string(index));
        }

    private:
        std::filesystem::path root_;
    };

    /** @brief Every file a build left, by name, with its bytes. */
    std::map<std::string, std::vector<std::uint8_t>> Produced(const std::filesystem::path& output)
    {
        std::map<std::string, std::vector<std::uint8_t>> files;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(output, error))
        {
            if (error || !entry.is_regular_file()) { continue; }
            const std::string name = std::filesystem::relative(entry.path(), output).string();
            // The manifest carries absolute paths and the lock is a lock; neither is content.
            if (name.rfind(".cna-content", 0) == 0) { continue; }
            std::ifstream stream(entry.path(), std::ios::binary);
            files.emplace(name, std::vector<std::uint8_t>{std::istreambuf_iterator<char>(stream),
                                                          std::istreambuf_iterator<char>()});
        }
        return files;
    }
}

// One corpus, four processes, four worker counts, the same bytes.
TEST(XnaBuildDeterminism, EveryRouteBuildsToTheSameBytesAtEveryWorkerCount)
{
    const Trees trees("workers");
    const std::vector<std::string> workerCounts{"1", "2", "4", "8"};
    std::vector<std::map<std::string, std::vector<std::uint8_t>>> builds;

    for (std::size_t index = 0u; index < workerCounts.size(); ++index)
    {
        const std::filesystem::path output = trees.Output(static_cast<int>(index));
        std::filesystem::create_directories(output);
        const CNA::Internal::HostProcessResult ran = CNA::Internal::RunHostProcess(
            CNA_CONTENT_TOOL_PATH,
            {"build", trees.Source().string(), "-o", output.string(), "--format", "xnb",
             "--workers", workerCounts[index], "--quiet"});
        ASSERT_TRUE(ran.started) << ran.failure;
        ASSERT_EQ(ran.exitCode, 0) << ran.standardOutput << ran.standardError;
        builds.push_back(Produced(output));
        ASSERT_FALSE(builds.back().empty()) << "the build at " << workerCounts[index]
                                            << " worker(s) produced nothing";
    }

    // Every build produced the same set of files.
    for (std::size_t index = 1u; index < builds.size(); ++index)
    {
        std::vector<std::string> first;
        std::vector<std::string> other;
        for (const auto& [name, bytes] : builds.front()) { static_cast<void>(bytes); first.push_back(name); }
        for (const auto& [name, bytes] : builds[index]) { static_cast<void>(bytes); other.push_back(name); }
        EXPECT_EQ(first, other) << "at " << workerCounts[index] << " worker(s)";
    }

    // And the same bytes in each of them. Reported per file, because "the builds differ" is not
    // something anyone can act on and "surface_0.xnb differs at 8 workers" is.
    for (const auto& [name, bytes] : builds.front())
    {
        for (std::size_t index = 1u; index < builds.size(); ++index)
        {
            const auto found = builds[index].find(name);
            if (found == builds[index].end()) { continue; }
            EXPECT_EQ(bytes.size(), found->second.size())
                << name << " is a different size at " << workerCounts[index] << " worker(s)";
            EXPECT_TRUE(bytes == found->second)
                << name << " differs at " << workerCounts[index] << " worker(s)";
        }
    }
}

// The same corpus built twice into the *same* directory is skipped entirely the second time, and
// the outputs are untouched: the fingerprint agrees with itself across processes.
TEST(XnaBuildDeterminism, ASecondProcessSkipsWhatTheFirstBuilt)
{
    const Trees trees("incremental");
    const std::filesystem::path output = trees.Output(0);
    std::filesystem::create_directories(output);

    const CNA::Internal::HostProcessResult first = CNA::Internal::RunHostProcess(
        CNA_CONTENT_TOOL_PATH,
        {"build", trees.Source().string(), "-o", output.string(), "--format", "xnb"});
    ASSERT_TRUE(first.started) << first.failure;
    ASSERT_EQ(first.exitCode, 0) << first.standardOutput << first.standardError;
    const std::map<std::string, std::vector<std::uint8_t>> after = Produced(output);
    ASSERT_FALSE(after.empty());

    const CNA::Internal::HostProcessResult second = CNA::Internal::RunHostProcess(
        CNA_CONTENT_TOOL_PATH,
        {"build", trees.Source().string(), "-o", output.string(), "--format", "xnb"});
    ASSERT_TRUE(second.started) << second.failure;
    ASSERT_EQ(second.exitCode, 0) << second.standardOutput << second.standardError;
    const std::string said = second.standardOutput + second.standardError;
    EXPECT_NE(said.find("Built: 0"), std::string::npos)
        << "a second process rebuilt something the first had already built\n"
        << second.standardOutput << second.standardError;
    EXPECT_EQ(Produced(output), after) << "a skipped build changed its own outputs";
}

// And the same corpus built for two containers is deterministic in both, which is the case a
// single-format check would miss: the writer is the half that differs between them.
TEST(XnaBuildDeterminism, TheCnbContainerIsDeterministicToo)
{
    const Trees trees("cnb", false);
    std::vector<std::map<std::string, std::vector<std::uint8_t>>> builds;
    for (int index = 0; index < 2; ++index)
    {
        const std::filesystem::path output = trees.Output(index);
        std::filesystem::create_directories(output);
        const CNA::Internal::HostProcessResult ran = CNA::Internal::RunHostProcess(
            CNA_CONTENT_TOOL_PATH,
            {"build", trees.Source().string(), "-o", output.string(), "--format", "cnb",
             "--workers", index == 0 ? "1" : "8", "--quiet"});
        ASSERT_TRUE(ran.started) << ran.failure;
        ASSERT_EQ(ran.exitCode, 0) << ran.standardOutput << ran.standardError;
        builds.push_back(Produced(output));
        ASSERT_FALSE(builds.back().empty());
    }
    EXPECT_EQ(builds[0], builds[1]);
}
