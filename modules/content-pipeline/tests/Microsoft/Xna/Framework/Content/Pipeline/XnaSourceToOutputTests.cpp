// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-021, XNAPP-167: the source-to-XNB and source-to-CNB legs
// of the input-parity matrix.
//
// The matrix (tests/reference/xna40/content-pipeline-inputs.json) holds eighteen extensions and
// asks six things of each. Four of them -- the importer, the processor, the malformed input and
// the target -- are covered by the per-family differential suites. These are the other two: every
// committed source fixture actually built, through the one canonical coordinator, into both
// containers CNA emits.
//
// This is the leg that catches a route that imports beautifully and cannot be written. It runs
// the real coordinator rather than calling a writer directly, so a component that is implemented
// but not *registered* fails here -- which is exactly how the .fbx and .x mappings were found to
// be stale after both importers had landed.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace Canon = CNA::Content::Pipeline;

namespace
{
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
     * @brief A source root holding one copied fixture, removed when the test ends.
     *
     * A companion is a file the fixture needs beside it and that is not itself an asset -- the
     * font file a `.spritefont` names is the only one today. It is copied into the same root,
     * where the coordinator ignores it because no importer claims its extension.
     */
    class OneAsset
    {
    public:
        OneAsset(const std::string& label, const std::filesystem::path& fixture,
                 const std::vector<std::filesystem::path>& companions = {})
            : root_(std::filesystem::temp_directory_path() / ("cna_xnapp021_" + label))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(Source());
            std::filesystem::create_directories(Output());
            name_ = fixture.filename().string();
            std::filesystem::copy_file(fixture, Source() / name_,
                                       std::filesystem::copy_options::overwrite_existing);
            for (const std::filesystem::path& companion : companions)
            {
                std::filesystem::copy_file(companion, Source() / companion.filename(),
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
        ~OneAsset()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
        OneAsset(const OneAsset&) = delete;
        OneAsset& operator=(const OneAsset&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return root_ / "src"; }
        [[nodiscard]] std::filesystem::path Output() const { return root_ / "out"; }
        [[nodiscard]] const std::string& Name() const { return name_; }

        /** @brief Every file the build left, other than its own manifest. */
        [[nodiscard]] std::vector<std::filesystem::path> Produced() const
        {
            std::vector<std::filesystem::path> files;
            std::error_code error;
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::recursive_directory_iterator(Output(), error))
            {
                if (!error && entry.is_regular_file() &&
                    entry.path().filename() != Canon::ContentBuildManifestFileName)
                {
                    files.push_back(entry.path());
                }
            }
            return files;
        }

    private:
        std::filesystem::path root_;
        std::string name_;
    };

    /** @brief Builds the one asset in @p asset into @p format; answers the coordinator's status. */
    int Build(const OneAsset& asset, const std::string& format)
    {
        const std::vector<std::filesystem::path> arguments{
            "build", asset.Source(), "-o", asset.Output(), "--format", format, "--quiet"};
        return Canon::RunContentCompiler(arguments, [](const Canon::ContentCompilerOptions& options)
                                         {
                                             auto registry =
                                                 std::make_shared<Canon::ContentPipelineRegistry>();
                                             Canon::RegisterBuiltInContentPipeline(*registry, options);
                                             return registry;
                                         });
    }

    /** @brief The first four bytes, for telling one container from another. */
    [[nodiscard]] std::string Magic(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        char magic[4] = {};
        stream.read(magic, sizeof(magic));
        return std::string(magic, static_cast<std::size_t>(stream.gcount()));
    }

    struct Route
    {
        std::string extension;
        std::filesystem::path fixture;
        std::vector<std::filesystem::path> companions;
    };

    /** @brief Every extension whose committed fixture the canonical graph can route today. */
    std::vector<Route> Routes()
    {
        const std::filesystem::path texture = Locate("tests/assets/xna40/texture");
        const std::filesystem::path source = Locate("tests/assets/xna40/source");
        const std::filesystem::path media = Locate("tests/assets/xna40/media");
        const std::filesystem::path font = Locate("tests/assets/fonts/LiberationMono-Regular.ttf");
        return {
            {".bmp", texture / "probe.bmp", {}},
            {".dds", texture / "probe.dds", {}},
            {".dib", texture / "probe.dib", {}},
            {".hdr", texture / "probe.hdr", {}},
            {".jpg", texture / "probe.jpg", {}},
            {".pfm", texture / "probe.pfm", {}},
            {".png", texture / "probe.png", {}},
            {".ppm", texture / "probe.ppm", {}},
            {".tga", texture / "probe.tga", {}},
            {".spritefont", source / "buildable.spritefont", {font}},
            {".wav", media / "tone_mono_44100.wav", {}},
            {".mp3", media / "mp3_mono_44100_128k.mp3", {}},
            {".wma", media / "wma_mono_44100.wma", {}},
        };
    }
}

// Every routable source builds to an .xnb the container's own magic identifies.
TEST(XnaSourceToOutput, EveryRoutableSourceBuildsToXnb)
{
    for (const Route& route : Routes())
    {
        ASSERT_TRUE(std::filesystem::exists(route.fixture))
            << route.extension << ": " << route.fixture.string();
        OneAsset asset("xnb_" + route.extension.substr(1), route.fixture, route.companions);
        EXPECT_EQ(Build(asset, "xnb"), 0) << route.extension << " (" << asset.Name() << ")";
        const std::vector<std::filesystem::path> produced = asset.Produced();
        ASSERT_FALSE(produced.empty()) << route.extension << ": the build wrote nothing";
        bool sawXnb = false;
        for (const std::filesystem::path& file : produced)
        {
            if (file.extension() == ".xnb")
            {
                sawXnb = true;
                EXPECT_EQ(Magic(file).substr(0, 3), "XNB") << file.string();
                EXPECT_GT(std::filesystem::file_size(file), 10u) << file.string();
            }
        }
        EXPECT_TRUE(sawXnb) << route.extension << ": no .xnb among the outputs";
    }
}

// And to CNB, which is CNA's own container and must reach every route the XNB one does.
TEST(XnaSourceToOutput, EveryRoutableSourceBuildsToCnb)
{
    for (const Route& route : Routes())
    {
        OneAsset asset("cnb_" + route.extension.substr(1), route.fixture, route.companions);
        EXPECT_EQ(Build(asset, "cnb"), 0) << route.extension << " (" << asset.Name() << ")";
        const std::vector<std::filesystem::path> produced = asset.Produced();
        ASSERT_FALSE(produced.empty()) << route.extension << ": the build wrote nothing";
        bool sawCnb = false;
        for (const std::filesystem::path& file : produced)
        {
            if (file.extension() == ".cnb")
            {
                sawCnb = true;
                EXPECT_GT(std::filesystem::file_size(file), 8u) << file.string();
            }
        }
        EXPECT_TRUE(sawCnb) << route.extension << ": no .cnb among the outputs";
    }
}

// The same build twice writes the same bytes: an output that depends on anything but its inputs
// would make every incremental build and every recorded interoperability result unreliable.
TEST(XnaSourceToOutput, BuildingTheSameSourceTwiceWritesTheSameBytes)
{
    for (const Route& route : Routes())
    {
        const auto bytesOf = [&route](const std::string& label)
        {
            OneAsset asset(label + route.extension.substr(1), route.fixture, route.companions);
            EXPECT_EQ(Build(asset, "xnb"), 0) << route.extension;
            std::vector<std::pair<std::string, std::string>> files;
            for (const std::filesystem::path& file : asset.Produced())
            {
                std::ifstream stream(file, std::ios::binary);
                files.emplace_back(file.filename().string(),
                                   std::string((std::istreambuf_iterator<char>(stream)),
                                               std::istreambuf_iterator<char>()));
            }
            std::sort(files.begin(), files.end());
            return files;
        };
        EXPECT_EQ(bytesOf("det1_"), bytesOf("det2_")) << route.extension;
    }
}
