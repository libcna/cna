// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-106: direct glTF -> .cnb compilation.
//
// The architectural requirement here is negative: this tool must NOT be a second interpretation of
// glTF. It links tools/gltf_to_cnj/gltf_to_cnj.cpp and calls its orchestration directly, so the
// two formats cannot disagree about what a glTF file means -- there is only one implementation to
// disagree with. What this file adds is the .cnb writer on the end of that pipeline and a
// single-command interface, so a project never has to keep .cnj files it does not want.
//
// The .cnj stage still exists as an internal staging step, written into a temporary directory this
// tool creates and removes. Collapsing it into a purely in-memory canonical form is CNBF-106B and
// is deliberately not done here: the conversion writes its sidecars from a dozen places inside a
// working, equivalence-tested function, and rewriting that is a change to the .cnj path -- the very
// reference the CNB equivalence tests compare against. The user-facing property is unaffected, and
// is asserted by test: this tool's output is byte-identical to running the two tools by hand.

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <algorithm>
#include <system_error>
#include <vector>

#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "CnaToolNumericArgs.hpp"
#include "GltfToCnjEntry.hpp"

namespace
{
    /// A temporary directory for the staging .cnj and its sidecars, removed however this exits.
    class StagingDirectory
    {
    public:
        explicit StagingDirectory(const std::string& tag)
        {
            std::error_code ec;
            const auto base = std::filesystem::temp_directory_path(ec);
            if (ec) { throw std::runtime_error("no temporary directory available"); }
            for (int attempt = 0; attempt < 64; ++attempt)
            {
                // No clock and no RNG: the process id plus a counter is enough to be unique among
                // concurrent runs, and keeps the tool's behaviour reproducible.
                const auto candidate =
                    base / ("cna_gltf_to_cnb_" + tag + "_" + std::to_string(attempt));
                if (std::filesystem::create_directory(candidate, ec))
                {
                    path_ = candidate;
                    return;
                }
            }
            throw std::runtime_error("could not create a staging directory");
        }

        ~StagingDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        StagingDirectory(const StagingDirectory&) = delete;
        StagingDirectory& operator=(const StagingDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) { throw std::runtime_error("cannot write '" + path.string() + "'"); }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) { throw std::runtime_error("failed while writing '" + path.string() + "'"); }
    }

    void PrintUsage(const char* argv0)
    {
        std::cerr
            << "Usage: " << argv0 << " <input.gltf|input.glb> <outputDir> <baseName> [options]\n\n"
            << "Compiles a glTF 2.0 file directly into one or more .cnb assets, without\n"
            << "leaving a .cnj document behind. Uses exactly the same glTF interpretation as\n"
            << "cna_tool_gltf_to_cnj, because it runs the same code.\n\n"
            << "Options:\n"
            << "  --unit-scale <f>  Multiplier applied to all positions and bone translations\n"
            << "                    (default 1.0; glTF mandates meters -- use 0.01 for a source\n"
            << "                    authored in centimetres).\n"
            << "  --keep-cnj <dir>  Also write the intermediate .cnj and its sidecars into <dir>,\n"
            << "                    for inspection. Off by default.\n"
            << "  --quiet           Print nothing on success.\n"
            << "  --help            Show this message.\n";
    }
}

int main(int argc, char** argv)
{
    std::filesystem::path inputPath;
    std::filesystem::path outputDir;
    std::string baseName;
    std::filesystem::path keepCnjDir;
    float unitScale = 1.0f;
    bool quiet = false;

    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--help") { PrintUsage(argv[0]); return 0; }
        if (arg == "--quiet") { quiet = true; continue; }
        if (arg == "--unit-scale" && i + 1 < argc)
        {
            // plans/plan_cnb.md CNBF-120: std::stof consumed a PREFIX, so "1.0abc" was 1.0 and
            // "0.01m" was 0.01; it also accepts "nan" and "inf", either of which would multiply
            // every position and bone translation in the model into a value nothing can render.
            // Zero and negatives are refused too: a zero scale collapses the model to a point and
            // a negative one mirrors it, and neither is a plausible thing to have meant.
            try
            {
                unitScale = CNA::Tools::ParseFiniteFloatArg("--unit-scale", argv[++i], 0.0f,
                                                             1000000.0f);
            }
            catch (const std::exception& e)
            {
                std::cerr << "error: " << e.what() << "\n";
                return 1;
            }
            continue;
        }
        if (arg == "--keep-cnj" && i + 1 < argc) { keepCnjDir = argv[++i]; continue; }
        if (!arg.empty() && arg.front() == '-')
        {
            std::cerr << "error: unknown option '" << arg << "'\n";
            return 1;
        }
        positional.emplace_back(arg);
    }

    if (positional.size() != 3u)
    {
        PrintUsage(argv[0]);
        return 1;
    }
    inputPath = positional[0];
    outputDir = positional[1];
    baseName = positional[2];

    // plans/plan_cnb.md CNBF-120: baseName becomes part of every output file name and of the
    // staging directory's name, so it has to be ONE filename component. A separator would write
    // outside outputDir, and "." or ".." would name a directory rather than an asset -- neither
    // was checked, and the staging directory's name was built from it before anything else ran.
    {
        const std::filesystem::path candidate(baseName);
        const bool oneComponent = !baseName.empty() &&
                                   baseName.find('/') == std::string::npos &&
                                   baseName.find('\\') == std::string::npos &&
                                   candidate.filename() == candidate &&
                                   !candidate.has_root_path();
        if (!oneComponent || baseName == "." || baseName == "..")
        {
            std::cerr << "error: <baseName> must be a single file-name component with no path "
                         "separators, and not '.' or '..'; got '"
                      << baseName << "'.\n";
            return 1;
        }
    }

    try
    {
        if (!std::filesystem::exists(inputPath))
        {
            std::cerr << "error: cannot open '" << inputPath.string() << "'.\n";
            return 1;
        }
        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);
        if (ec && !std::filesystem::is_directory(outputDir))
        {
            std::cerr << "error: cannot create '" << outputDir.string() << "': " << ec.message()
                      << ".\n";
            return 1;
        }

        StagingDirectory staging(baseName);
        {
            // The .cnj stage announces the files it writes, which for this tool are inside a
            // temporary directory the caller never asked about and cannot use. Suppressed unless
            // --keep-cnj makes those files real output; a leaked /tmp path in a build log is
            // worse than no line at all. Errors still reach stderr untouched.
            std::streambuf* const saved = std::cout.rdbuf();
            std::ostringstream swallowed;
            if (keepCnjDir.empty()) { std::cout.rdbuf(swallowed.rdbuf()); }
            try
            {
                CNA::Tools::Gltf::ConvertGltfToCnj(inputPath, staging.path(), baseName, unitScale);
            }
            catch (...)
            {
                std::cout.rdbuf(saved);
                throw;
            }
            std::cout.rdbuf(saved);
        }

        // Every .cnj the conversion produced becomes its own .cnb. A glTF file with several skins
        // yields several documents, so this compiles all of them rather than assuming one.
        std::vector<std::filesystem::path> documents;
        for (const auto& entry : std::filesystem::directory_iterator(staging.path()))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".cnj")
            {
                documents.push_back(entry.path());
            }
        }
        if (documents.empty())
        {
            std::cerr << "error: the conversion produced no .cnj document to compile.\n";
            return 1;
        }
        std::sort(documents.begin(), documents.end());

        for (const std::filesystem::path& document : documents)
        {
            const CNA::Content::Cnb::CnjToCnbResult compiled =
                CNA::Content::Cnb::CompileCnjToCnb(document.string(), staging.path().string(),
                                                    document.stem().string());
            const std::filesystem::path target =
                outputDir / (document.stem().string() + ".cnb");
            WriteFile(target, compiled.bytes);
            if (!quiet)
            {
                std::cout << target.string() << ": " << compiled.assetTypeName << ", "
                          << compiled.bytes.size() << " bytes, " << compiled.absorbedFiles.size()
                          << " file(s) absorbed\n";
            }
        }

        if (!keepCnjDir.empty())
        {
            // Every error here was discarded, so --keep-cnj reported success and exited 0 having
            // copied nothing (CNBF-120). The .cnb files are already written and correct at this
            // point, so this is reported as the failure it is rather than pretended away.
            std::filesystem::create_directories(keepCnjDir, ec);
            if (ec && !std::filesystem::is_directory(keepCnjDir))
            {
                std::cerr << "error: --keep-cnj cannot create '" << keepCnjDir.string()
                          << "': " << ec.message() << ".\n";
                return 1;
            }
            for (const auto& entry : std::filesystem::directory_iterator(staging.path()))
            {
                std::error_code copyError;
                std::filesystem::copy(entry.path(), keepCnjDir / entry.path().filename(),
                                      std::filesystem::copy_options::overwrite_existing,
                                      copyError);
                if (copyError)
                {
                    std::cerr << "error: --keep-cnj could not copy '"
                              << entry.path().filename().string() << "' into '"
                              << keepCnjDir.string() << "': " << copyError.message() << ".\n";
                    return 1;
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
