// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-123: run tools/common/CnaToolAtomicWrite.hpp's OWN contract on Windows.
//
// This compiles the real header -- not a copy of it, not a description of it -- for a Windows
// target and exercises every clause of WriteFileAtomically()'s documented contract. The Windows
// branch of Detail::ReplaceFileAtomically() cannot be reached from the Linux CnaTests suite at
// all, so without this the MoveFileExW path would be implemented and never executed.
//
// Build (cross, from the repository root):
//   x86_64-w64-mingw32-g++ -std=c++23 -static -O1 -I tools/common \
//       spikes/atomic-replace-spike/probe_helper_contract.cpp \
//       -o spikes/atomic-replace-spike/probe_helper_contract.exe
// Run:
//   WINEDEBUG=-all wine spikes/atomic-replace-spike/probe_helper_contract.exe

#include "CnaToolAtomicWrite.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("%-58s %s\n", what, ok ? "ok" : "FAILED");
        if (!ok) { ++failures; }
    }

    std::vector<std::uint8_t> Bytes(const std::string& text)
    {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }

    std::string Read(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    /// Every entry of `dir`, so a stray `.cnatmp-*` shows up as itself rather than as an absence.
    std::vector<std::string> Entries(const fs::path& dir)
    {
        std::vector<std::string> names;
        for (const auto& e : fs::directory_iterator(dir))
        {
            names.push_back(e.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / "cna_atomic_contract";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // 1. A previously absent destination is created.
    {
        const fs::path out = dir / "created.cnb";
        CNA::Tools::WriteFileAtomically(out, Bytes("NEW"));
        Check(fs::exists(out) && Read(out) == "NEW", "absent destination is created");
        Check(Entries(dir) == std::vector<std::string>{"created.cnb"},
              "no temporary remains after creating");
    }

    // 2. An existing destination is REPLACED -- the clause the CRT's rename() cannot satisfy, and
    //    the whole reason the Windows branch exists.
    {
        const fs::path out = dir / "replaced.cnb";
        CNA::Tools::WriteFileAtomically(out, Bytes("OLD-AND-MUCH-LONGER-THAN-THE-NEW-ONE"));
        CNA::Tools::WriteFileAtomically(out, Bytes("NEW"));
        Check(Read(out) == "NEW", "existing destination is replaced whole");
        Check(fs::file_size(out) == 3u, "no tail of the longer previous file survives");
    }

    // 3. An empty payload is a legitimate file, not a no-op.
    {
        const fs::path out = dir / "empty.cnb";
        CNA::Tools::WriteFileAtomically(out, Bytes("SOMETHING"));
        CNA::Tools::WriteFileAtomically(out, {});
        Check(fs::exists(out) && fs::file_size(out) == 0u, "an empty payload replaces with 0 bytes");
    }

    // 4. Failure preserves the destination and leaves no temporary. A directory that does not
    //    exist is the portable way to make the temporary uncreatable.
    {
        const fs::path out = dir / "nope" / "asset.cnb";
        bool threw = false;
        try { CNA::Tools::WriteFileAtomically(out, Bytes("X")); }
        catch (const std::runtime_error&) { threw = true; }
        Check(threw, "a destination in a missing directory is refused");
        Check(!fs::exists(dir / "nope"), "the refusal created nothing");
    }

    // 5. Failure with an EXISTING destination: a destination that is a DIRECTORY cannot be
    //    replaced by a file on either platform, so the publication step fails after a temporary has
    //    already been written. That is the arm which proves the guard runs on the failure path.
    {
        const fs::path out = dir / "isadir.cnb";
        fs::create_directories(out, ec);
        bool threw = false;
        try { CNA::Tools::WriteFileAtomically(out, Bytes("X")); }
        catch (const std::runtime_error&) { threw = true; }
        Check(threw, "publishing over a directory is refused");
        Check(fs::is_directory(out), "the refused publication left the destination alone");
        bool debris = false;
        for (const auto& e : fs::directory_iterator(dir))
        {
            if (e.path().filename().string().find(".cnatmp-") != std::string::npos)
            {
                debris = true;
            }
        }
        Check(!debris, "the failed publication left no .cnatmp- file behind");
    }

    // 6. A temporary-name collision is stepped over rather than clobbered: an existing file with
    //    attempt 0's exact name must survive, and the write must still succeed.
    {
        const fs::path out = dir / "collide.cnb";
        const fs::path squatter =
            dir / (std::string("collide.cnb.cnatmp-") + CNA::Tools::Detail::ProcessTag() + "-0");
        { std::ofstream f(squatter, std::ios::binary); f << "SQUATTER"; }
        CNA::Tools::WriteFileAtomically(out, Bytes("NEW"));
        Check(Read(out) == "NEW", "a taken temporary name does not stop the write");
        Check(Read(squatter) == "SQUATTER", "the squatting file was not overwritten");
        fs::remove(squatter, ec);
    }

    fs::remove_all(dir, ec);
    std::printf("%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
