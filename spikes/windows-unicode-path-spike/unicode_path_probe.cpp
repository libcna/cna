// SPDX-License-Identifier: MIT
//
// windows-unicode-path-spike — what actually breaks when a filesystem path is not
// representable in the process ANSI code page.
//
// This is the existence gate for `plans/plan_windows_portability.md` WINPORT-0003. It answers
// one question per line of output: given a real file on disk whose name contains characters
// outside the active ANSI code page, which of the conversions and open calls CNA actually
// performs still find it?
//
// It deliberately makes no claim from a string it printed. Every PASS is a byte-for-byte read
// of the file's known content, because a terminal that cannot render a name is not evidence
// that the name is wrong (WINNATIVE-F7 and this workstream's Phase 38 rule).
//
// Build (Windows, MSVC):   cl /std:c++20 /EHsc /utf-8 unicode_path_probe.cpp
// Build (Linux, GCC):      g++ -std=c++20 -o unicode_path_probe unicode_path_probe.cpp
//
// Exit code is 0 when the probe ran; it is not a pass/fail gate. Read the table.

#include <algorithm>
#include <cstdio>
#include <cstring>
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
    // The payload every case looks for. Short, ASCII, and unique, so that finding it proves the
    // right file was opened and not merely that *a* file was.
    constexpr std::string_view kPayload = "CNA-UNICODE-PROBE-OK";

    int gPass = 0;
    int gFail = 0;

    void Report(std::string_view name, bool ok, std::string_view detail = {})
    {
        (ok ? gPass : gFail)++;
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << name;
        if (!detail.empty())
        {
            std::cout << "   [" << detail << "]";
        }
        std::cout << '\n';
    }

    bool ContentMatches(std::istream& in)
    {
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return text.find(kPayload) != std::string::npos;
    }

    std::string Utf8Of(const fs::path& p)
    {
        const std::u8string s = p.u8string();
        return {reinterpret_cast<const char*>(s.data()), s.size()};
    }

    fs::path FromUtf8(std::string_view s)
    {
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
    }

    // The interesting names. Each is a directory component and a file stem, so both halves of a
    // path are exercised; together they cover the scripts a real user's content actually has.
    struct Sample
    {
        const char* label;
        const char* utf8;   // written as escapes so the source file's own encoding cannot lie
    };

    const std::vector<Sample>& Samples()
    {
        static const std::vector<Sample> samples = {
            {"ascii",        "plain"},
            {"spaces",       "CNA test dir"},
            {"latin1",       "caf\xc3\xa9"},                                  // café  (IS in CP1252)
            {"czech",        "\xc5\xbe" "lu\xc5\xa5" "ou\xc4\x8d" "k\xc3\xbd"},  // žluťoučký (NOT in CP1252)
            {"combining",    "e\xcc\x81" "tude"},                             // e + U+0301
            {"precomposed",  "\xc3\xa9" "tude"},                              // é
            {"cyrillic",     "\xd0\xba\xd0\xb8\xd1\x80\xd0\xb8\xd0\xbb\xd0\xbb\xd0\xb8\xd1\x86\xd0\xb0"},
            {"cjk-jp",       "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"},         // 日本語
            {"cjk-zh",       "\xe4\xb8\xad\xe6\x96\x87"},                     // 中文
            {"emoji",        "emoji-\xf0\x9f\x98\x80"},                       // 😀 U+1F600, surrogate pair
            {"mixed",        "m\xc3\xad" "ch-\xc4\x8d" "esky-\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e-\xf0\x9f\x98\x80"},
        };
        return samples;
    }

#ifdef _WIN32
    // Does this UTF-8 text survive a round trip through the active ANSI code page? When it does
    // not, `path::string()` is not merely lossy, it is documented to fail — and that is the whole
    // of WINNATIVE-F30.
    bool RepresentableInAcp(const fs::path& p)
    {
        const std::wstring& w = p.native();
        BOOL used = FALSE;
        const int n = ::WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(), -1,
                                            nullptr, 0, nullptr, &used);
        return n > 0 && !used;
    }
#endif
}

int main()
{
    std::cout << "=== CNA Windows Unicode path probe ===\n";
#ifdef _WIN32
    std::cout << "platform    : Windows\n";
    std::cout << "ANSI CP     : " << ::GetACP() << "\n";
    std::cout << "OEM CP      : " << ::GetOEMCP() << "\n";
#else
    std::cout << "platform    : POSIX\n";
#endif
    std::cout << "sizeof(path::value_type) = " << sizeof(fs::path::value_type) << "\n\n";

    const fs::path root = fs::temp_directory_path() / "cna-unicode-probe";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec)
    {
        std::cerr << "cannot create " << Utf8Of(root) << ": " << ec.message() << "\n";
        return 2;
    }
    std::cout << "root (utf8) : " << Utf8Of(root) << "\n\n";

    for (const Sample& sample : Samples())
    {
        const fs::path dir = root / FromUtf8(sample.utf8);
        const fs::path file = dir / FromUtf8(std::string(sample.utf8) + ".txt");

        std::cout << "--- " << sample.label << "  (utf8: " << sample.utf8 << ")\n";

#ifdef _WIN32
        std::cout << "    representable in CP" << ::GetACP() << ": "
                  << (RepresentableInAcp(file) ? "yes" : "NO") << "\n";
#endif

        // ---- 1. create the directory and write the file, staying native throughout -----------
        fs::create_directories(dir, ec);
        Report("create_directories(path)", !ec, ec ? ec.message() : "");
        if (ec) { continue; }

        {
            std::ofstream out(file, std::ios::binary);
            const bool opened = out.is_open();
            if (opened) { out << kPayload << '\n'; }
            Report("ofstream(path) write", opened);
            if (!opened) { continue; }
        }

        Report("fs::exists(path)", fs::exists(file, ec) && !ec);
        Report("fs::file_size(path) > 0", fs::file_size(file, ec) > 0 && !ec);

        // ---- 2. read it back through a native path ------------------------------------------
        {
            std::ifstream in(file, std::ios::binary);
            Report("ifstream(path) read", in.is_open() && ContentMatches(in));
        }

        // ---- 3. the conversions CNA actually performs ----------------------------------------
        // path -> narrow std::string. This is the ~210-site pattern under audit.
        std::string narrow;
        bool narrowOk = false;
        try
        {
            narrow = file.string();
            narrowOk = true;
            Report("path::string() did not throw", true);
        }
        catch (const std::exception& e)
        {
            Report("path::string() did not throw", false, e.what());
        }

        std::string generic;
        try
        {
            generic = file.generic_string();
            Report("path::generic_string() did not throw", true);
        }
        catch (const std::exception& e)
        {
            Report("path::generic_string() did not throw", false, e.what());
        }

        std::string utf8;
        try
        {
            utf8 = Utf8Of(file);
            Report("path::u8string() did not throw", true);
        }
        catch (const std::exception& e)
        {
            Report("path::u8string() did not throw", false, e.what());
        }

        // ---- 4. the round trips ---------------------------------------------------------------
        // The one CNA does today: path -> string() -> path. On Linux a byte copy; on Windows a
        // trip through the ANSI code page and back.
        if (narrowOk)
        {
            const fs::path back = fs::path(narrow);
            Report("round trip via string() still exists", fs::exists(back, ec) && !ec);

            std::ifstream in(narrow.c_str(), std::ios::binary);
            Report("ifstream(const char* from string())", in.is_open() && ContentMatches(in));

            std::FILE* f = std::fopen(narrow.c_str(), "rb");
            Report("fopen(const char* from string())", f != nullptr);
            if (f) { std::fclose(f); }
        }
        else
        {
            Report("round trip via string() still exists", false, "string() threw");
            Report("ifstream(const char* from string())", false, "string() threw");
            Report("fopen(const char* from string())", false, "string() threw");
        }

        // The one the prescription says to use: path -> u8string() -> path.
        {
            const fs::path back = FromUtf8(utf8);
            Report("round trip via u8string() still exists", fs::exists(back, ec) && !ec);

            std::ifstream in(back, std::ios::binary);
            Report("ifstream(path from u8string())", in.is_open() && ContentMatches(in));
        }

        // And what a narrow C API does with UTF-8 bytes it was never promised.
        {
            std::FILE* f = std::fopen(utf8.c_str(), "rb");
            Report("fopen(const char* holding UTF-8)", f != nullptr);
            if (f) { std::fclose(f); }
        }

        // ---- 5. enumeration -------------------------------------------------------------------
        {
            bool found = false;
            for (const fs::directory_entry& e2 : fs::directory_iterator(dir, ec))
            {
                if (e2.path().filename() == file.filename()) { found = true; }
            }
            Report("directory_iterator finds it", found && !ec);
        }

        // ---- 6. the wide Win32 API, which is the ground truth on Windows -----------------------
#ifdef _WIN32
        {
            const DWORD attrs = ::GetFileAttributesW(file.c_str());
            Report("GetFileAttributesW", attrs != INVALID_FILE_ATTRIBUTES);

            HANDLE h = ::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Report("CreateFileW", h != INVALID_HANDLE_VALUE);
            if (h != INVALID_HANDLE_VALUE) { ::CloseHandle(h); }

            // The ANSI twin of the same call, given the same path narrowed the way CNA narrows it.
            if (narrowOk)
            {
                const DWORD a2 = ::GetFileAttributesA(narrow.c_str());
                Report("GetFileAttributesA(from string())", a2 != INVALID_FILE_ATTRIBUTES);
            }
            else
            {
                Report("GetFileAttributesA(from string())", false, "string() threw");
            }

            std::FILE* f = ::_wfopen(file.c_str(), L"rb");
            Report("_wfopen(wide)", f != nullptr);
            if (f) { std::fclose(f); }
        }
#endif
        std::cout << '\n';
    }

    std::cout << "=== probe totals: " << gPass << " pass, " << gFail << " fail ===\n";

    fs::remove_all(root, ec);
    return 0;
}
