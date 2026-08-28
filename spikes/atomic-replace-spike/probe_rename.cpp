// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-123: what does std::filesystem::rename ACTUALLY do on Windows when the
// destination already exists?
//
// tools/common/CnaToolAtomicWrite.hpp published its output with std::filesystem::rename and a
// comment asserting that this "on every platform CNA targets replaces the existing file in one
// step". POSIX rename(2) does. The C runtime's rename() does NOT -- C says the behaviour is
// implementation-defined when the new name exists, and the Windows CRT returns EEXIST. Which of
// those a Windows std::filesystem::rename is depends entirely on the standard library, so it has
// to be measured rather than reasoned about.
//
// Build (cross, from the repository root):
//   x86_64-w64-mingw32-g++ -std=c++23 -static -O0 \
//       spikes/atomic-replace-spike/probe_rename.cpp -o spikes/atomic-replace-spike/probe_rename.exe
// Run:
//   WINEDEBUG=-all wine spikes/atomic-replace-spike/probe_rename.exe

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
    void Write(const fs::path& p, const std::string& text)
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << text;
    }

    std::string Read(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / "cna_rename_probe";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // Case 1: destination ABSENT. Every implementation must manage this one.
    {
        const fs::path from = dir / "a.tmp";
        const fs::path to = dir / "a.out";
        Write(from, "NEW");
        ec.clear();
        fs::rename(from, to, ec);
        std::printf("absent-destination : ec=%d (%s) exists=%d contents=%s\n", ec.value(),
                    ec ? ec.message().c_str() : "ok", static_cast<int>(fs::exists(to)),
                    Read(to).c_str());
    }

    // Case 2: destination EXISTS. This is the one the atomic-write helper depends on.
    {
        const fs::path from = dir / "b.tmp";
        const fs::path to = dir / "b.out";
        Write(to, "OLD-OLD-OLD");
        Write(from, "NEW");
        ec.clear();
        fs::rename(from, to, ec);
        std::printf("existing-destination: ec=%d (%s) dest=%s tempStillThere=%d\n", ec.value(),
                    ec ? ec.message().c_str() : "ok", Read(to).c_str(),
                    static_cast<int>(fs::exists(from)));
    }

    // Case 3: the same through the CRT's own rename(), for comparison -- this is what a
    // std::filesystem::rename implemented as a thin wrapper over the CRT would give.
    {
        const fs::path from = dir / "c.tmp";
        const fs::path to = dir / "c.out";
        Write(to, "OLD");
        Write(from, "NEW");
        const int rc = std::rename(from.string().c_str(), to.string().c_str());
        std::printf("crt-rename          : rc=%d dest=%s\n", rc, Read(to).c_str());
    }

    fs::remove_all(dir, ec);
    return 0;
}
