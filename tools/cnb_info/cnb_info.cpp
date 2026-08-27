// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-H013: `cna_tool_cnb_info`, the offline `.cnb` inspector and validator.
//
// Two jobs, and the second is the one that earns the tool its place:
//
//   * A maintainer can see what is in a compiled asset -- header, chunk table, metadata,
//     dependencies -- without attaching a debugger to a game.
//   * A build script can ask what a `.cnb` DEPENDS ON without understanding its schema, which is
//     exactly the property the container-level XREF table exists to provide. `--refs` prints one
//     logical asset name per line, so `cna_tool_cnb_info a.cnb --refs | xargs ...` is the whole
//     integration.
//
// It is also a validator: every structural invariant runs during parsing, so a non-zero exit means
// the file is malformed and the message says how. That makes it usable as a content-pipeline gate
// on its own.
//
// Deliberately a thin shell over CnbDocument with no schema knowledge whatsoever -- it can describe
// a Model, a Curve or a type this build has never heard of equally well, because everything it
// prints comes from the container. That is a design check as much as a feature: anything this tool
// cannot report is something the container failed to make self-describing.

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"

namespace
{
    int Usage(const char* argv0)
    {
        std::cerr
            << "Usage: " << argv0 << " <file.cnb> [options]\n"
            << "\n"
            << "Inspects and validates a compiled CNA .cnb asset. Every structural invariant is\n"
            << "checked while reading, so a non-zero exit means the file is malformed and the\n"
            << "message says how.\n"
            << "\n"
            << "Options:\n"
            << "  --refs      Print only the external asset names this file depends on, one per\n"
            << "              line, for use from a build script.\n"
            << "  --chunks    Print only the chunk table.\n"
            << "  --quiet     Print nothing; validate and report through the exit code alone.\n"
            << "  --help      Show this message.\n";
        return 2;
    }

    std::string Hex32(std::uint32_t value)
    {
        static constexpr char kDigits[] = "0123456789ABCDEF";
        std::string out = "0x";
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            out.push_back(kDigits[(value >> shift) & 0xFu]);
        }
        return out;
    }
}

int main(int argc, char** argv)
{
    std::string input;
    bool refsOnly = false;
    bool chunksOnly = false;
    bool quiet = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { return Usage(argv[0]); }
        if (arg == "--refs") { refsOnly = true; continue; }
        if (arg == "--chunks") { chunksOnly = true; continue; }
        if (arg == "--quiet") { quiet = true; continue; }
        if (!arg.empty() && arg[0] == '-') { return Usage(argv[0]); }
        if (input.empty()) { input = arg; }
        else { return Usage(argv[0]); }
    }
    if (input.empty()) { return Usage(argv[0]); }

    try
    {
        const CNA::Content::Cnb::CnbDocument document =
            CNA::Content::Cnb::CnbDocument::ParseFile(input);

        if (quiet) { return 0; }

        if (refsOnly)
        {
            for (const auto& reference : document.ExternalReferences())
            {
                std::cout << reference.logicalName << "\n";
            }
            return 0;
        }

        if (!chunksOnly)
        {
            std::cout << input << "\n";
            std::cout << "  container       " << document.ContainerMajor() << "."
                      << document.ContainerMinor() << "\n";
            std::cout << "  asset type      "
                      << CNA::Content::Cnb::AssetTypeIdToString(document.AssetTypeId()) << " ("
                      << Hex32(document.AssetTypeId()) << ")\n";
            std::cout << "  schema version  " << document.AssetSchemaVersion() << "\n";
            std::cout << "  chunks          " << document.ChunkCount() << "\n";
            if (document.Metadata().present)
            {
                std::cout << "  type name       " << document.Metadata().assetTypeName << "\n";
                if (!document.Metadata().contentName.empty())
                {
                    std::cout << "  content name    " << document.Metadata().contentName << "\n";
                }
            }
        }

        std::cout << "\n  " << std::left << std::setw(6) << "chunk" << std::setw(10) << "flags"
                  << std::right << std::setw(12) << "offset" << std::setw(12) << "size"
                  << std::setw(6) << "align" << "  checksum\n";
        for (std::size_t i = 0; i < document.ChunkCount(); ++i)
        {
            const auto& entry = document.ChunkAt(i);
            std::cout << "  " << std::left << std::setw(6)
                      << CNA::Content::Cnb::ChunkIdToString(entry.type) << std::setw(10)
                      << (entry.IsMandatory() ? "required" : "optional") << std::right
                      << std::setw(12) << entry.offset << std::setw(12) << entry.storedSize
                      << std::setw(6) << entry.alignment << "  " << Hex32(entry.checksum) << "\n";
        }

        if (!chunksOnly)
        {
            const auto& references = document.ExternalReferences();
            std::cout << "\n  external references: " << references.size() << "\n";
            for (const auto& reference : references)
            {
                std::cout << "    " << reference.logicalName;
                if (reference.expectedAssetTypeId != CNA::Content::Cnb::CnbAssetTypeId::Invalid)
                {
                    std::cout << "  ("
                              << CNA::Content::Cnb::AssetTypeIdToString(
                                     reference.expectedAssetTypeId)
                              << ")";
                }
                std::cout << "\n";
            }
        }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "cnb_info: " << e.what() << "\n";
        return 1;
    }
}
