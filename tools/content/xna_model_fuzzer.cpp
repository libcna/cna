// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-310: fuzz harness for the two modelling readers.
//
// A `.x` file and an FBX document both arrive from outside the build. One entry point hands
// untrusted bytes to whichever reader their first bytes claim, and lets the process die on
// anything but the two outcomes a malformed document may have: a node tree, or that reader's own
// exception. Everything the reader answers is then walked, so a tree holding a view of freed or
// out-of-range memory is caught rather than merely returned.
//
// Two shapes, matching the intermediate-XML harness beside it:
//
//   * standalone replay (default): `cna_xna_model_fuzzer replay <file|dir>...` runs every file
//     once; `cna_xna_model_fuzzer mutate <dir> <iterations> [seed]` mutates the corpus
//     deterministically -- this is how the committed corpus is exercised and how a campaign's
//     crashing input is reproduced;
//   * libFuzzer/AFL++ (`-DCNA_XNA_MODEL_FUZZER_ENTRY_POINT=ON`, clang): exports
//     LLVMFuzzerTestOneInput and lets the driver own main().
//
// Start a campaign from tests/assets/xna40/model.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/DirectXFileReader.hpp"
#include "CNA/Content/Pipeline/FbxFileReader.hpp"

namespace Canon = CNA::Content::Pipeline;

namespace
{
    /** @brief Walks everything a `.x` object holds, so a bad view is touched and not just held. */
    void Walk(const Canon::DirectXFileObject& object)
    {
        volatile double sum = 0.0;
        for (const double one : object.numbers) { sum = sum + one; }
        for (const std::string& text : object.strings) { sum = sum + static_cast<double>(text.size()); }
        for (const std::string& text : object.references) { sum = sum + static_cast<double>(text.size()); }
        (void)sum;
        for (const Canon::DirectXFileObject& child : object.children) { Walk(child); }
    }

    void Walk(const Canon::FbxNode& node)
    {
        volatile double sum = 0.0;
        for (const double one : node.Numbers()) { sum = sum + one; }
        sum = sum + static_cast<double>(node.Text(0).size());
        sum = sum + node.Number(0, 0.0);
        (void)sum;
        for (const Canon::FbxNode& child : node.children) { Walk(child); }
    }

    /** @brief One document, through whichever reader its first bytes claim. */
    void RunOne(const std::uint8_t* data, const std::size_t size)
    {
        const std::vector<std::uint8_t> bytes(data, data + size);
        const bool looksLikeX = size >= 4u && std::memcmp(data, "xof ", 4u) == 0;
        try
        {
            if (looksLikeX)
            {
                const Canon::DirectXFile document = Canon::ReadDirectXFile(bytes);
                for (const Canon::DirectXFileObject& object : document.objects) { Walk(object); }
                return;
            }
            const Canon::FbxFile document = Canon::ReadFbxFile(bytes);
            for (const Canon::FbxNode& node : document.nodes) { Walk(node); }
        }
        catch (const Canon::DirectXFileException&)
        {
        }
        catch (const Canon::FbxFileException&)
        {
        }
        // Every other exception escapes on purpose: a reader that answers std::bad_alloc, a
        // std::out_of_range or anything else has a defect this harness exists to surface.
    }

    std::vector<std::uint8_t> Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    }

    std::vector<std::filesystem::path> Gather(const std::filesystem::path& root)
    {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        if (std::filesystem::is_directory(root, error))
        {
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::directory_iterator(root, error))
            {
                if (error) { break; }
                const std::string extension = entry.path().extension().string();
                if (entry.is_regular_file(error) && (extension == ".x" || extension == ".fbx"))
                {
                    files.push_back(entry.path());
                }
            }
        }
        else if (std::filesystem::is_regular_file(root, error))
        {
            files.push_back(root);
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    /** @brief A byte flipped, a run truncated, or a run zeroed: three ways to break a document. */
    std::vector<std::uint8_t> Mutate(const std::vector<std::uint8_t>& source, std::mt19937& random)
    {
        if (source.empty()) { return source; }
        std::vector<std::uint8_t> out = source;
        switch (random() % 3u)
        {
            case 0:
                out[random() % out.size()] = static_cast<std::uint8_t>(random() % 256u);
                break;
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
}

#ifdef CNA_XNA_MODEL_FUZZER_ENTRY_POINT
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    RunOne(data, size);
    return 0;
}
#else
int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "replay") == 0)
    {
        std::size_t count = 0;
        for (int i = 2; i < argc; ++i)
        {
            for (const std::filesystem::path& file : Gather(argv[i]))
            {
                const std::vector<std::uint8_t> bytes = Read(file);
                RunOne(bytes.data(), bytes.size());
                ++count;
            }
        }
        std::printf("cna_xna_model_fuzzer: replayed %zu document(s)\n", count);
        return 0;
    }
    if (argc >= 4 && std::strcmp(argv[1], "mutate") == 0)
    {
        const std::vector<std::filesystem::path> corpus = Gather(argv[2]);
        if (corpus.empty())
        {
            std::fprintf(stderr, "cna_xna_model_fuzzer: no .x or .fbx documents under %s\n", argv[2]);
            return 2;
        }
        const long iterations = std::strtol(argv[3], nullptr, 10);
        const unsigned seed = argc >= 5 ? static_cast<unsigned>(std::strtoul(argv[4], nullptr, 10))
                                        : 20260906u;
        std::mt19937 random(seed);
        for (const std::filesystem::path& file : corpus)
        {
            const std::vector<std::uint8_t> original = Read(file);
            for (long i = 0; i < iterations; ++i)
            {
                const std::vector<std::uint8_t> mutated = Mutate(original, random);
                RunOne(mutated.data(), mutated.size());
            }
        }
        std::printf("cna_xna_model_fuzzer: %zu document(s) x %ld mutation(s), seed %u\n",
                    corpus.size(), iterations, seed);
        return 0;
    }
    std::fprintf(stderr,
                 "Usage: cna_xna_model_fuzzer replay <file|dir>...\n"
                 "       cna_xna_model_fuzzer mutate <dir> <iterations> [seed]\n");
    return 2;
}
#endif
