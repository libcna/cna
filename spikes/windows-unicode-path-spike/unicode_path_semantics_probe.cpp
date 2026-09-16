// SPDX-License-Identifier: MIT
//
// windows-unicode-path-spike, part two — the questions the migration has to answer before it
// can pick a policy, rather than after.
//
// Part one (unicode_path_probe.cpp) established that a native `std::filesystem::path` survives
// every operation and a narrowed one does not. This part asks what the conversion layer should
// promise at its edges:
//
//   Phase 20 — what does the toolchain do with malformed UTF-8 handed to `path(std::u8string)`?
//   Phase 22 — what happens past legacy MAX_PATH with LongPathsEnabled=0?
//   Phase 30 — how do the Windows path *semantics* differ from POSIX, for the properties CNA
//              actually asks about (is_absolute, root_name, relative, lexically_normal)?
//
// Build (Windows, MSVC):   cl /std:c++20 /EHsc /utf-8 unicode_path_semantics_probe.cpp
// Build (Linux, GCC):      g++ -std=c++20 -o unicode_path_semantics_probe unicode_path_semantics_probe.cpp

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
    std::string Hex(std::string_view bytes)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (const unsigned char b : bytes)
        {
            out += digits[b >> 4];
            out += digits[b & 0x0f];
            out += ' ';
        }
        if (!out.empty()) { out.pop_back(); }
        return out;
    }

    std::string Utf8Of(const fs::path& p)
    {
        try
        {
            const std::u8string s = p.u8string();
            return {reinterpret_cast<const char*>(s.data()), s.size()};
        }
        catch (const std::exception& e)
        {
            return std::string("<u8string() threw: ") + e.what() + ">";
        }
    }

    fs::path FromUtf8(std::string_view s)
    {
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
    }
}

int main()
{
    std::cout << "=== CNA path semantics probe ===\n";
#ifdef _WIN32
    std::cout << "platform : Windows, ACP " << ::GetACP() << "\n";
#else
    std::cout << "platform : POSIX\n";
#endif
    std::cout << "\n";

    // ---------------------------------------------------------------------------------------
    // Phase 20 — malformed UTF-8 at the conversion boundary.
    // ---------------------------------------------------------------------------------------
    std::cout << "### Phase 20: malformed UTF-8 -> path(std::u8string) -> u8string()\n";
    struct Malformed { const char* label; std::string bytes; };
    const std::vector<Malformed> malformed = {
        {"valid ascii",           std::string("ok")},
        {"valid 2-byte",          std::string("\xc3\xa9")},
        {"lone continuation",     std::string("\x80")},
        {"truncated 2-byte",      std::string("\xc3")},
        {"truncated 3-byte",      std::string("\xe6\x97")},
        {"overlong slash",        std::string("\xc0\xaf")},
        {"lone high surrogate",   std::string("\xed\xa0\x80")},   // U+D800 as CESU-8/WTF-8
        {"lone low surrogate",    std::string("\xed\xb0\x80")},   // U+DC00
        {"beyond U+10FFFF",       std::string("\xf5\x80\x80\x80")},
        {"embedded NUL",          std::string("a\0b", 3)},
        {"latin1 e-acute (0xe9)", std::string("\xe9")},           // valid CP1252, invalid UTF-8
    };

    for (const Malformed& m : malformed)
    {
        std::cout << "  " << m.label << "  in=[" << Hex(m.bytes) << "]  -> ";
        try
        {
            const fs::path p = FromUtf8(m.bytes);
            const std::string back = Utf8Of(p);
            std::cout << "path built, u8string()=[" << Hex(back) << "]"
                      << (back == m.bytes ? "  ROUND-TRIP" : "  ALTERED");
        }
        catch (const std::exception& e)
        {
            std::cout << "THREW: " << e.what();
        }
        std::cout << "\n";
    }
    std::cout << "\n";

    // ---------------------------------------------------------------------------------------
    // Phase 30 — Windows path semantics, for exactly the properties CNA queries.
    // ---------------------------------------------------------------------------------------
    std::cout << "### Phase 30: path semantics\n";
    const std::vector<std::string> semantic = {
        "/usr/share/cna",
        "C:\\CNA test\\asset.png",
        "C:/CNA test/asset.png",
        "C:asset.png",              // drive-relative: root_name but no root_directory
        "\\\\server\\share\\a.png", // UNC
        "\\absolute-no-drive",      // rooted, no drive
        "relative/asset.png",
        "./a/../b",
        "trailing/",
        "a/b/../../../escape",
        "CON",
        "asset.png.",               // trailing dot
        "asset.png ",               // trailing space
    };
    std::cout << "  | path | is_absolute | root_name | root_dir | lexically_normal |\n";
    for (const std::string& s : semantic)
    {
        const fs::path p(s);
        std::cout << "  | " << s
                  << " | " << (p.is_absolute() ? "ABS" : "rel")
                  << " | '" << Utf8Of(p.root_name()) << "'"
                  << " | '" << Utf8Of(p.root_directory()) << "'"
                  << " | " << Utf8Of(p.lexically_normal())
                  << " |\n";
    }
    std::cout << "\n";

    // The containment question PathContainment.hpp answers, asked as a path question.
    std::cout << "### Phase 30b: lexically_relative for containment\n";
    const fs::path base("C:/content root");
    for (const char* c : {"C:/content root/tex/a.png", "C:/content root/../evil.png",
                          "C:/other/a.png", "C:/content root/..config/a.png"})
    {
        const fs::path rel = fs::path(c).lexically_normal().lexically_relative(base.lexically_normal());
        std::cout << "  " << c << "  -> rel='" << Utf8Of(rel) << "'"
                  << "  escapes=" << ((rel.empty() || *rel.begin() == "..") ? "YES" : "no") << "\n";
    }
    std::cout << "\n";

    // ---------------------------------------------------------------------------------------
    // Phase 22 — long paths, with whatever the machine's policy currently is.
    // ---------------------------------------------------------------------------------------
    std::cout << "### Phase 22: long paths\n";
#ifdef _WIN32
    {
        HKEY key{};
        DWORD value = 0, size = sizeof(value), type = 0;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
                            0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            ::RegQueryValueExW(key, L"LongPathsEnabled", nullptr, &type,
                               reinterpret_cast<LPBYTE>(&value), &size);
            ::RegCloseKey(key);
        }
        std::cout << "  LongPathsEnabled = " << value << "\n";
        std::cout << "  MAX_PATH         = " << MAX_PATH << "\n";
    }
#endif
    {
        std::error_code ec;
        const fs::path root = fs::temp_directory_path() / "cna-longpath-probe";
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);

        // Build nesting until the total exceeds MAX_PATH, one 40-character component at a time.
        fs::path deep = root;
        const std::string component(40, 'd');
        int created = 0;
        for (int i = 0; i < 20; ++i)
        {
            deep /= component;
            fs::create_directories(deep, ec);
            if (ec)
            {
                std::cout << "  create_directories failed at depth " << (i + 1)
                          << ", total length " << Utf8Of(deep).size()
                          << ": " << ec.message() << "\n";
                break;
            }
            created = i + 1;
            if (Utf8Of(deep).size() > 300) { break; }
        }
        std::cout << "  deepest created: depth " << created
                  << ", length " << Utf8Of(deep).size() << "\n";

        const fs::path file = deep / "payload.txt";
        {
            std::ofstream out(file, std::ios::binary);
            std::cout << "  ofstream(path) at length " << Utf8Of(file).size()
                      << ": " << (out.is_open() ? "open" : "FAILED") << "\n";
            if (out.is_open()) { out << "long"; }
        }
        std::cout << "  exists(): " << (fs::exists(file, ec) ? "yes" : "NO") << "\n";

#ifdef _WIN32
        // The explicit extended-length form, which bypasses the MAX_PATH parse entirely.
        {
            std::wstring ext = L"\\\\?\\" + file.wstring();
            const DWORD a = ::GetFileAttributesW(ext.c_str());
            std::cout << "  GetFileAttributesW with \\\\?\\ prefix: "
                      << (a != INVALID_FILE_ATTRIBUTES ? "found" : "NOT found") << "\n";
            const DWORD a2 = ::GetFileAttributesW(file.c_str());
            std::cout << "  GetFileAttributesW bare:               "
                      << (a2 != INVALID_FILE_ATTRIBUTES ? "found" : "NOT found") << "\n";
        }
#endif
        fs::remove_all(root, ec);
    }

    std::cout << "\n=== done ===\n";
    return 0;
}
