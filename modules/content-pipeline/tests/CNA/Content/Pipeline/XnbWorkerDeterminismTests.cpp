// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-44: identical inputs produce identical files, across worker
// counts and across process runs.
//
// The row's remaining scope was "the multi-worker CLI case", and the only existing coverage
// (`ContentPipelineCliTest.XnbDirectoryBuildIsDeterministicAcrossWorkerCountsAndRebuildsChanges`)
// builds three XNB-as-source assets to **CNB**, compares the published files, and does not look at
// the manifest at all. Neither the XNB writers nor the manifest was covered by it.
//
// This suite is that case. Every build here is a separate OS process running the real
// `cna-content` -- which is what "across process runs" has to mean, since a second call inside one
// process shares an allocator, a heap layout and every address a hash could accidentally reach.
// The asset mix is deliberately not uniform: XNB Model carries the shared-resource table and the
// type table, which is where a non-deterministic ordering would first show, and an effect built by
// an external compiler takes part so that a route with a *process* in it participates in a
// parallel build like any other.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/HostProcess.hpp"

namespace
{
#if !defined(CNA_CONTENT_TOOL_PATH)
#error "CNA_CONTENT_TOOL_PATH must be baked in; see cmake/UnitTests.cmake (XNAP-44)."
#endif
#if !defined(CNA_FAKE_EFFECT_COMPILER_PATH)
#error "CNA_FAKE_EFFECT_COMPILER_PATH must be baked in; see cmake/UnitTests.cmake (XNAP-A5)."
#endif

    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_determinism_" + tag + "_" +
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

    [[nodiscard]] std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path()); }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    void CopyInto(const std::filesystem::path& from, const std::filesystem::path& to)
    {
        std::filesystem::create_directories(to.parent_path());
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
    }

    /** @brief Every published file under @p root, keyed by its relative path. */
    using FileTree = std::map<std::string, std::vector<std::uint8_t>>;

    [[nodiscard]] FileTree Snapshot(const std::filesystem::path& root)
    {
        FileTree tree;
        if (!std::filesystem::exists(root)) { return tree; }
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file()) { continue; }
            const std::string relative =
                std::filesystem::relative(entry.path(), root).generic_string();
            // The output lease is a live lock file, not a build product.
            if (relative == ".cna-content.lock") { continue; }
            tree.emplace(relative, ReadBytes(entry.path()));
        }
        return tree;
    }

    /** @brief Names the files that differ between two trees, so a failure says which asset. */
    [[nodiscard]] std::vector<std::string> Differences(const FileTree& left, const FileTree& right)
    {
        std::vector<std::string> differing;
        for (const auto& [name, bytes] : left)
        {
            const auto other = right.find(name);
            if (other == right.end()) { differing.push_back(name + " (missing on the right)"); }
            else if (other->second != bytes)
            {
                differing.push_back(name + " (" + std::to_string(bytes.size()) + " vs " +
                                    std::to_string(other->second.size()) + " bytes)");
            }
        }
        for (const auto& [name, bytes] : right)
        {
            static_cast<void>(bytes);
            if (!left.contains(name)) { differing.push_back(name + " (missing on the left)"); }
        }
        return differing;
    }

    struct ToolRun
    {
        int exitCode = -1;
        std::string output;
    };

    /** @brief Runs the real `cna-content` executable as its own OS process. */
    [[nodiscard]] ToolRun RunTool(const std::vector<std::string>& arguments)
    {
        const CNA::Internal::HostProcessResult result =
            CNA::Internal::RunHostProcess(CNA_CONTENT_TOOL_PATH, arguments);
        if (!result.started) { return {-1, "could not start cna-content: " + result.failure}; }
        return {result.exitCode, result.standardOutput + result.standardError};
    }

    /** @brief The `[BUILD] name` lines, in the order the tool printed them. */
    [[nodiscard]] std::vector<std::string> BuildOrder(const std::string& log)
    {
        std::vector<std::string> names;
        std::size_t position = 0u;
        while ((position = log.find("[BUILD] ", position)) != std::string::npos)
        {
            position += 8u;
            const std::size_t space = log.find(' ', position);
            names.push_back(log.substr(position, space - position));
        }
        return names;
    }

    [[nodiscard]] std::filesystem::path CorpusRoot()
    {
        // Tests run with the repository root as their working directory.
        return std::filesystem::current_path() / "tests" / "assets";
    }

    /**
     * @brief Writes a source tree with enough assets, of enough different kinds, to schedule.
     *
     * The count matters: with fewer assets than workers a parallel build degenerates into a serial
     * one and proves nothing about ordering. The *mix* matters more -- Model carries the XNB shared
     * resource and type tables, an effect carries an external process, and the rest are ordinary.
     *
     * @param source Directory to fill.
     * @return How many assets were written, or zero when the corpus is missing.
     */
    [[nodiscard]] std::size_t WriteMixedProject(const std::filesystem::path& source)
    {
        const std::filesystem::path corpus = CorpusRoot();
        if (!std::filesystem::is_directory(corpus / "gltf")) { return 0u; }

        std::size_t assets = 0u;

        // Models: the most structurally complex XNB this pipeline writes. Taken in sorted order so
        // the selection itself is stable across runs and machines.
        std::vector<std::filesystem::path> models;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(corpus / "gltf"))
        {
            const std::string name = entry.path().filename().string();
            if (entry.path().extension() != ".glb") { continue; }
            // Every one of these builds; the corpus sweep (XNAP-59) is what keeps that true.
            if (name.rfind("mesh-", 0u) != 0u && name.rfind("xf-", 0u) != 0u &&
                name.rfind("mat-", 0u) != 0u)
            {
                continue;
            }
            if (name.find("variants") != std::string::npos) { continue; }
            models.push_back(entry.path());
        }
        std::sort(models.begin(), models.end());
        if (models.size() > 12u) { models.resize(12u); }
        for (const std::filesystem::path& model : models)
        {
            CopyInto(model, source / "models" / model.filename());
            ++assets;
        }

        // Textures, through the image front end and the block-compression encoder.
        const std::filesystem::path picture =
            corpus / "media" / "pictures" / "Family" / "portrait.png";
        if (std::filesystem::is_regular_file(picture))
        {
            for (int index = 0; index < 4; ++index)
            {
                CopyInto(picture, source / "textures" / ("image" + std::to_string(index) + ".png"));
                ++assets;
            }
        }

        // An XNB-as-source transcode, which exercises a different importer for the same writer.
        const std::filesystem::path legacy =
            corpus / "xnb" / "monogame" / "windows" / "uncompressed" / "white-1.xnb";
        if (std::filesystem::is_regular_file(legacy))
        {
            CopyInto(legacy, source / "legacy" / "white.xnb");
            ++assets;
        }

        // A compiled effect, needing no compiler.
        std::vector<std::uint8_t> compiled(48u, 0x5Au);
        compiled[0] = 0x01u;
        compiled[1] = 0x09u;
        compiled[2] = 0xFFu;
        compiled[3] = 0xFEu;
        {
            std::filesystem::create_directories(source / "effects");
            std::ofstream stream(source / "effects" / "precompiled.fxb", std::ios::binary);
            stream.write(reinterpret_cast<const char*>(compiled.data()),
                         static_cast<std::streamsize>(compiled.size()));
        }
        ++assets;

        // Two effects built by a real external process, so a route that spawns one takes part in
        // the parallel build rather than sitting outside it.
        WriteText(source / "effects" / "common.fxh", "float4 Tint;\n");
        for (int index = 0; index < 2; ++index)
        {
            WriteText(source / "effects" / ("shader" + std::to_string(index) + ".fx"),
                      "//FAKE: payload=deterministic-effect-" + std::to_string(index) + "\n"
                      "#include \"common.fxh\"\n"
                      "float4 PS() : COLOR0 { return Tint; }\n");
            ++assets;
        }

        return assets;
    }
} // namespace

TEST(XnbWorkerDeterminismTest, EveryWorkerCountAndEveryProcessRunProducesTheSameBytes)
{
    ScratchDirectory scratch("workers");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::size_t assets = WriteMixedProject(source);
    ASSERT_GT(assets, 12u) << "the fixture corpus is missing; this test must not run degraded";

    // 1 and 2 prove the parallel path against the serial one; 8 puts more workers than some
    // asset kinds have assets, which is where a scheduler-order dependency shows.
    FileTree reference;
    std::vector<std::string> referenceOrder;
    std::string referenceLabel;
    for (const std::string& workers : {"1", "2", "3", "8"})
    {
        for (const std::string& run : {"a", "b"})
        {
            const std::filesystem::path output =
                scratch.Path() / ("out-" + workers + "-" + run);
            const ToolRun built = RunTool({"build", source.string(), "-o", output.string(),
                                           "--format", "xnb", "--workers", workers,
                                           "--fx-compiler", CNA_FAKE_EFFECT_COMPILER_PATH});
            const std::string label = "workers=" + workers + " run=" + run;
            ASSERT_EQ(built.exitCode, 0) << label << "\n" << built.output;
            ASSERT_NE(built.output.find("Built: " + std::to_string(assets) + "  Skipped: 0  "
                                        "Failed: 0"),
                      std::string::npos)
                << label << "\n" << built.output;

            const FileTree tree = Snapshot(output);
            ASSERT_FALSE(tree.empty()) << label;
            // The manifest is part of the output and must be identical too: a manifest that
            // differs between runs makes every later incremental decision depend on which one
            // happened, which is the same defect wearing a different hat.
            ASSERT_TRUE(tree.contains(".cna-content-manifest.json"))
                << label << ": no build manifest was published, so comparing the trees would "
                            "silently stop covering it";

            const std::vector<std::string> order = BuildOrder(built.output);
            if (reference.empty())
            {
                reference = tree;
                referenceOrder = order;
                referenceLabel = label;
                continue;
            }
            const std::vector<std::string> differing = Differences(reference, tree);
            EXPECT_TRUE(differing.empty())
                << label << " differs from " << referenceLabel << " in "
                << differing.size() << " file(s): "
                << (differing.empty() ? std::string{} : differing.front());
            // Asset order is contractual: the coordinator sorts by logical name before it
            // schedules anything, so the diagnostics a user reads do not depend on timing.
            EXPECT_EQ(order, referenceOrder) << label << " reported a different build order";
        }
    }

    ASSERT_FALSE(referenceOrder.empty());
    std::vector<std::string> sorted = referenceOrder;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(referenceOrder, sorted) << "the build order must be the sorted logical-name order";
}

TEST(XnbWorkerDeterminismTest, ARepeatBuildIntoTheSameOutputSkipsEverythingAtEveryWorkerCount)
{
    ScratchDirectory scratch("workers-skip");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::size_t assets = WriteMixedProject(source);
    ASSERT_GT(assets, 12u) << "the fixture corpus is missing; this test must not run degraded";
    const std::filesystem::path output = scratch.Path() / "Content";

    const ToolRun first = RunTool({"build", source.string(), "-o", output.string(),
                                   "--format", "xnb", "--workers", "1",
                                   "--fx-compiler", CNA_FAKE_EFFECT_COMPILER_PATH});
    ASSERT_EQ(first.exitCode, 0) << first.output;
    const FileTree published = Snapshot(output);

    // A different worker count must not invalidate anything: worker count is a scheduling choice,
    // not an input, and a manifest that recorded it would rebuild the world on a machine with a
    // different core count.
    for (const std::string& workers : {"4", "1", "8"})
    {
        const ToolRun again = RunTool({"build", source.string(), "-o", output.string(),
                                       "--format", "xnb", "--workers", workers,
                                       "--fx-compiler", CNA_FAKE_EFFECT_COMPILER_PATH});
        ASSERT_EQ(again.exitCode, 0) << again.output;
        EXPECT_NE(again.output.find("Built: 0  Skipped: " + std::to_string(assets)),
                  std::string::npos)
            << "workers=" << workers << "\n" << again.output;
        EXPECT_TRUE(Differences(published, Snapshot(output)).empty())
            << "workers=" << workers << " changed the published bytes without rebuilding";
    }
}
