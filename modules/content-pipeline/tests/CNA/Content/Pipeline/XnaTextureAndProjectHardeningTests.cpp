// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-290: the readers this plan added, under hostile input.
//
// `XnaModelImporterHardeningTests.cpp` did the two modelling readers and
// `XnaIntermediateSerializerHardeningTests.cpp` the intermediate XML. These are the rest of what
// this plan wrote itself: the DDS surface reader, the portable-float-map decoder, the Radiance
// picture decoder, and the `.contentproj` reader. Every one of them takes bytes from outside the
// build and every one has exactly two permitted outcomes -- a value, or its own refusal -- never a
// crash, a hang, an unbounded allocation or another exception type.
//
// Two readers this plan's routes depend on are deliberately **not** here, and the reason is the
// same for both. A `.ppm`, `.png`, `.bmp`, `.tga` and `.jpg` are decoded by `stb_image`, and an
// MP3, WMA or WMV by FFmpeg; both are properly licensed third-party parsers with their own
// hardening, and a campaign against them here would be measuring somebody else's code. What is
// CNA's is the *use* of them -- what it accepts, what it refuses, what it does with the answer --
// and that is measured against the genuine importers in the per-extension differential suites.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "CNA/Internal/Graphics/DdsSurfaceReader.hpp"
#include "CNA/Internal/Graphics/PfmDecoder.hpp"
#include "CNA/Internal/Graphics/RadianceHdrDecoder.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/ContentProject.hpp"

namespace Graphics = CNA::Internal::Graphics;
namespace Tasks = Microsoft::Xna::Framework::Content::Pipeline::Tasks;

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

    std::vector<std::uint8_t> Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    }

    /**
     * @brief One mutated copy: a byte flipped, a run truncated, or a run zeroed.
     *
     * The same three shapes the modelling harness uses, because they break different things: a
     * flip corrupts a value in place, a truncation ends the file mid-structure, and a zero run
     * turns a length or a count into something a reader might trust.
     */
    std::vector<std::uint8_t> Mutate(const std::vector<std::uint8_t>& source, std::mt19937& random)
    {
        if (source.empty()) { return source; }
        std::vector<std::uint8_t> out = source;
        switch (random() % 3u)
        {
            case 0:
            {
                out[random() % out.size()] = static_cast<std::uint8_t>(random() % 256u);
                break;
            }
            case 1:
                out.resize(1u + (random() % out.size()));
                break;
            default:
            {
                const std::size_t at = random() % out.size();
                const std::size_t length =
                    std::min<std::size_t>(out.size() - at, 1u + (random() % 32u));
                std::fill(out.begin() + static_cast<std::ptrdiff_t>(at),
                          out.begin() + static_cast<std::ptrdiff_t>(at + length), 0u);
                break;
            }
        }
        return out;
    }

    std::vector<std::filesystem::path> Corpus(const std::string& directory,
                                              const std::string& extension)
    {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(Locate(directory), error))
        {
            if (!error && entry.is_regular_file() && entry.path().extension() == extension)
            {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }
}

TEST(XnaTextureReaderHardening, NoMutatedDdsCrashesOrEscapesItsOwnRefusal)
{
    const std::vector<std::filesystem::path> corpus = Corpus("tests/assets/xna40/texture", ".dds");
    ASSERT_FALSE(corpus.empty()) << "the DDS corpus is not on disk";
    std::mt19937 random(20260907u);
    std::size_t read = 0u;
    std::size_t refused = 0u;
    for (const std::filesystem::path& file : corpus)
    {
        const std::vector<std::uint8_t> original = Read(file);
        for (int attempt = 0; attempt < 400; ++attempt)
        {
            const std::vector<std::uint8_t> mutated = Mutate(original, random);
            try
            {
                const Graphics::DdsSurfaces surfaces =
                    Graphics::ReadDdsSurfaces(mutated, file.filename().string());
                ++read;
                // Walk what came back rather than merely holding it: a surface describing a view
                // of memory the file does not contain is caught here and not at the next reader.
                volatile std::size_t touched = 0u;
                for (const std::vector<std::vector<std::uint8_t>>& face : surfaces.surfaces)
                {
                    for (const std::vector<std::uint8_t>& level : face)
                    {
                        ASSERT_LE(level.size(), 256u * 1024u * 1024u);
                        for (const std::uint8_t byte : level) { touched = touched + byte; }
                    }
                }
                static_cast<void>(touched);
            }
            catch (const std::runtime_error&)
            {
                ++refused;
            }
        }
    }
    EXPECT_GT(refused, 0u);
    EXPECT_GT(read, 0u) << "every mutation was refused, so nothing exercised the reader's body";
}

TEST(XnaTextureReaderHardening, NoMutatedPortableFloatMapOrRadiancePictureEscapesItsOwnRefusal)
{
    struct Case
    {
        const char* extension;
        bool radiance;
    };
    std::mt19937 random(20260907u);
    for (const Case& one : {Case{".pfm", false}, Case{".hdr", true}})
    {
        const std::vector<std::filesystem::path> corpus =
            Corpus("tests/assets/xna40/texture", one.extension);
        ASSERT_FALSE(corpus.empty()) << one.extension << " corpus is not on disk";
        std::size_t read = 0u;
        std::size_t refused = 0u;
        for (const std::filesystem::path& file : corpus)
        {
            const std::vector<std::uint8_t> original = Read(file);
            for (int attempt = 0; attempt < 400; ++attempt)
            {
                const std::vector<std::uint8_t> mutated = Mutate(original, random);
                try
                {
                    std::vector<float> pixels;
                    std::uint32_t width = 0u;
                    std::uint32_t height = 0u;
                    if (one.radiance)
                    {
                        const Graphics::DecodedRadianceHdr decoded =
                            Graphics::DecodeRadianceHdr(mutated, file.filename().string());
                        pixels = decoded.pixels;
                        width = decoded.width;
                        height = decoded.height;
                    }
                    else
                    {
                        const Graphics::DecodedPfm decoded =
                            Graphics::DecodePfm(mutated, file.filename().string());
                        pixels = decoded.pixels;
                        width = decoded.width;
                        height = decoded.height;
                    }
                    ++read;
                    // The dimensions and the buffer must agree: four floats per texel, and the
                    // buffer no larger than the dimensions claim.
                    ASSERT_EQ(pixels.size(),
                              static_cast<std::size_t>(width) * height * 4u)
                        << file.filename().string() << " decoded a buffer its own dimensions do "
                                                       "not describe";
                    volatile double touched = 0.0;
                    for (const float value : pixels) { touched = touched + value; }
                    static_cast<void>(touched);
                }
                catch (const std::runtime_error&)
                {
                    ++refused;
                }
            }
        }
        EXPECT_GT(refused, 0u) << one.extension;
        EXPECT_GT(read, 0u) << one.extension
                            << ": every mutation was refused, so nothing exercised the decoder";
    }
}

// The `.contentproj` reader takes a project written by somebody else's tool, and its refusals are
// the ones a user sees; an exception of another type escaping is a crash report rather than a
// diagnostic.
TEST(XnaContentProjectHardening, NoMutatedProjectEscapesItsOwnRefusal)
{
    const std::vector<std::filesystem::path> corpus =
        Corpus("tests/assets/xna_custom_pipeline", ".contentproj");
    std::vector<std::filesystem::path> files = corpus;
    // The committed acceptance project is one shape; the sample tree, where it is on this machine,
    // is 170 more written by Microsoft's own tooling.
    const std::filesystem::path samples("/rv/tmp/samples");
    std::error_code error;
    if (std::filesystem::exists(samples, error) && !error)
    {
        std::size_t taken = 0u;
        for (std::filesystem::recursive_directory_iterator it(
                 samples, std::filesystem::directory_options::skip_permission_denied, error);
             it != std::filesystem::recursive_directory_iterator() && taken < 8u; it.increment(error))
        {
            if (error) { break; }
            if (it->is_regular_file(error) && it->path().extension() == ".contentproj")
            {
                files.push_back(it->path());
                ++taken;
            }
        }
    }
    if (files.empty())
    {
        GTEST_SKIP() << "no .contentproj corpus on this machine";
    }

    std::mt19937 random(20260907u);
    std::size_t read = 0u;
    std::size_t refused = 0u;
    for (const std::filesystem::path& file : files)
    {
        const std::vector<std::uint8_t> original = Read(file);
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            const std::vector<std::uint8_t> mutated = Mutate(original, random);
            const std::string text(mutated.begin(), mutated.end());
            try
            {
                const Tasks::ContentProject project =
                    Tasks::ContentProject::Parse(text, file.string());
                ++read;
                // Everything the project answers is walked, and the counts stay sane: a project
                // claiming a million items from a few kilobytes of text is the failure this
                // catches.
                ASSERT_LE(project.Items().size(), 100000u);
                volatile std::size_t touched = 0u;
                for (const Tasks::ContentProject::Item& item : project.Items())
                {
                    touched = touched + item.include.size() + item.kind.size();
                    for (const auto& [name, value] : item.metadata)
                    {
                        touched = touched + name.size() + value.size();
                    }
                }
                static_cast<void>(touched);
                static_cast<void>(project.SourceAssets().size());
                static_cast<void>(project.CopiedFiles().size());
                static_cast<void>(project.UnroutableEXT().size());
            }
            catch (const Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException&)
            {
                ++refused;
            }
        }
    }
    EXPECT_GT(refused, 0u);
    EXPECT_GT(read, 0u) << "every mutation was refused, so nothing exercised the reader's body";
}
