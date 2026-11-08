// SPDX-License-Identifier: MS-PL
// CNA-FIX-L27: compiled into both focused content suites. The Wine qualification
// runner must see this test pass in each of its ASCII and representable non-ASCII runs.
#include "CNA/Internal/PathUtf8.hpp"
#include <gtest/gtest.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::wstring Environment(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::vector<wchar_t> buffer(size);
    if (GetEnvironmentVariableW(name, buffer.data(), size) != size - 1) return {};
    return buffer.data();
}

std::filesystem::path DirectoryPath(const std::filesystem::path& path)
{
    auto directory = std::filesystem::canonical(path);
    // GetTempPath supplies a trailing separator; parent_path on that spelling
    // names the directory itself, not its owner. Preserve drive roots.
    while (directory.has_relative_path() && directory.filename().empty())
        directory = directory.parent_path();
    return directory;
}
}

TEST(WindowsTempQualification, RequestedRootReachesTheSuiteAndRoundTripsThroughAcp)
{
    const auto expected = Environment(L"CNA_L27_TEMP_ROOT");
    if (expected.empty())
        GTEST_SKIP() << "Run scripts/run-wine-unicode-temp.py for paired TEMP qualification";

    // Oracle: the runner's independently supplied wide root must own the process
    // scratch path. Compare native paths and CP_ACP round trips, never CNA output
    // copied into a golden. UseProcessTempRoot redirects TEMP to its child.
    const auto root = DirectoryPath(expected);
    const auto actual = DirectoryPath(std::filesystem::temp_directory_path());
    SCOPED_TRACE("requested=" + CNA::Internal::PathToUtf8(root) +
        " effective=" + CNA::Internal::PathToUtf8(actual) +
        " TEMP=" + CNA::Internal::PathToUtf8(Environment(L"TEMP")) +
        " TMP=" + CNA::Internal::PathToUtf8(Environment(L"TMP")));
    // Both directory spellings must identify the same owned root; no permission
    // to accept a Windows-default TEMP outside the runner's requested directory.
    EXPECT_EQ(DirectoryPath(actual / ""), actual);
    ASSERT_TRUE(std::filesystem::equivalent(actual.parent_path(), root));
    ASSERT_TRUE(std::filesystem::equivalent(DirectoryPath(Environment(L"TEMP")), actual));
    ASSERT_TRUE(std::filesystem::equivalent(DirectoryPath(Environment(L"TMP")), actual));
    const bool nonAscii = Environment(L"CNA_L27_REQUIRE_NONASCII") == L"1";
    bool hasNonAscii = false;
    for (const wchar_t value : root.native()) hasNonAscii |= value > 127;
    EXPECT_EQ(hasNonAscii, nonAscii);

    const UINT acp = GetACP();
    BOOL substituted = FALSE;
    const bool unicodeAcp = acp == CP_UTF8;
    const DWORD flags = unicodeAcp ? 0 : WC_NO_BEST_FIT_CHARS;
    const auto& wide = actual.native();
    const int count = WideCharToMultiByte(CP_ACP, flags, wide.c_str(), -1,
        nullptr, 0, nullptr, unicodeAcp ? nullptr : &substituted);
    ASSERT_GT(count, 0);
    ASSERT_FALSE(substituted) << "qualification root is outside this Windows ANSI code page";
    std::vector<char> ansi(count);
    ASSERT_EQ(WideCharToMultiByte(CP_ACP, flags, wide.c_str(), -1,
        ansi.data(), count, nullptr, unicodeAcp ? nullptr : &substituted), count);
    ASSERT_FALSE(substituted);
    std::vector<wchar_t> roundTrip(wide.size() + 1);
    ASSERT_EQ(MultiByteToWideChar(CP_ACP, 0, ansi.data(), -1,
        roundTrip.data(), static_cast<int>(roundTrip.size())), static_cast<int>(wide.size() + 1));
    EXPECT_EQ(std::wstring(roundTrip.data()), wide);

    const auto file = actual / L"l27-temp-entrypoint.txt";
    { std::ofstream output(file, std::ios::binary); output << "l27-wide-temp"; ASSERT_TRUE(output.good()); }
    std::ifstream input(file, std::ios::binary);
    std::string text;
    input >> text;
    EXPECT_EQ(text, "l27-wide-temp");
    input.close();
    EXPECT_TRUE(std::filesystem::remove(file));
    std::cout << "[L27 TEMP] ACP=" << acp << " root=" << CNA::Internal::PathToUtf8(root)
              << " effective=" << CNA::Internal::PathToUtf8(actual) << '\n';
}
