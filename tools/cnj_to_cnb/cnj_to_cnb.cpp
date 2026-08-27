// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-063 (Phase D): the offline `.cnj` -> `.cnb` content compiler.
//
// Deliberately a thin shell over CNA::Content::Cnb::CompileCnjToCnb: everything worth testing
// lives in the library, where a unit test can reach it without spawning a process, and this file
// only turns argv into a call and a result into a report. It links CNA the same way
// cna_tool_gltf_to_cnj does and, like that tool, never constructs a GraphicsDevice -- compiling
// content must not need a window, a GPU or a renderer.

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnjToCnb.hpp"

namespace
{
    int Usage(const char* argv0)
    {
        std::cerr
            << "Usage: " << argv0 << " <input.cnj> [output.cnb] [options]\n"
            << "\n"
            << "Compiles a CNA .cnj content document, and the binary sidecar files it names,\n"
            << "into a single compiled .cnb asset.\n"
            << "\n"
            << "Options:\n"
            << "  --content-root <dir>  Directory sidecar references resolve against\n"
            << "                        (default: the input file's own directory).\n"
            << "  --name <logical>      Logical asset name recorded in the file's debug\n"
            << "                        metadata (default: the input file's stem).\n"
            << "  --quiet               Print nothing on success.\n"
            << "  --help                Show this message.\n";
        return 2;
    }
}

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;

    std::string input;
    std::string output;
    std::string contentRoot;
    std::string logicalName;
    bool quiet = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { return Usage(argv[0]); }
        if (arg == "--quiet") { quiet = true; continue; }
        if (arg == "--content-root")
        {
            if (++i >= argc) { return Usage(argv[0]); }
            contentRoot = argv[i];
            continue;
        }
        if (arg == "--name")
        {
            if (++i >= argc) { return Usage(argv[0]); }
            logicalName = argv[i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') { return Usage(argv[0]); }
        if (input.empty()) { input = arg; }
        else if (output.empty()) { output = arg; }
        else { return Usage(argv[0]); }
    }

    if (input.empty()) { return Usage(argv[0]); }
    if (output.empty())
    {
        output = fs::path(input).replace_extension(".cnb").string();
    }

    try
    {
        const CNA::Content::Cnb::CnjToCnbResult result =
            CNA::Content::Cnb::CompileCnjToCnb(input, contentRoot, logicalName);

        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            std::cerr << "cnj_to_cnb: cannot open '" << output << "' for writing.\n";
            return 1;
        }
        file.write(reinterpret_cast<const char*>(result.bytes.data()),
                   static_cast<std::streamsize>(result.bytes.size()));
        if (!file)
        {
            std::cerr << "cnj_to_cnb: failed while writing '" << output << "'.\n";
            return 1;
        }
        file.close();

        if (!quiet)
        {
            std::cout << "Wrote " << output << " (" << result.assetTypeName << ", "
                      << result.bytes.size() << " bytes)\n";
            std::cout << "  absorbed " << result.absorbedFiles.size() << " source file(s):\n";
            for (const std::string& absorbed : result.absorbedFiles)
            {
                std::cout << "    " << absorbed << "\n";
            }
            if (!result.externalReferences.empty())
            {
                std::cout << "  " << result.externalReferences.size()
                          << " external reference(s) kept out of line:\n";
                for (const std::string& reference : result.externalReferences)
                {
                    std::cout << "    " << reference << "\n";
                }
            }
        }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "cnj_to_cnb: " << e.what() << "\n";
        return 1;
    }
}
