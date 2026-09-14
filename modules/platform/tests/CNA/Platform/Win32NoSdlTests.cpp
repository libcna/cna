// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0061: the Win32 backend contains no SDL.
//
// The point of this whole backend is that SDL is a *backend* of CNA rather than CNA's substrate,
// and the way that claim decays is not a deliberate decision -- it is one "temporary" SDL call
// added for a feature that was awkward to write natively. A grep in a CI script catches that only
// if someone runs the script; a test catches it on every build that runs the suite.
//
// Scanning the sources rather than the binary is deliberate: a link-level check would pass a file
// that includes an SDL header for a type alias, which is exactly the shape the first regression
// would take.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

#if defined(CNA_WIN32_SOURCE_ROOT)
constexpr const char* kWin32SourceRoot = CNA_WIN32_SOURCE_ROOT;
#else
constexpr const char* kWin32SourceRoot = nullptr;
#endif

std::string ToLowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char character) {
        return static_cast<char>(character >= 'A' && character <= 'Z' ? character + 32 : character);
    });
    return text;
}

std::vector<std::filesystem::path> Win32Sources()
{
    std::vector<std::filesystem::path> sources;
    if (kWin32SourceRoot == nullptr)
        return sources;

    std::error_code error;
    const std::filesystem::path root(kWin32SourceRoot);
    if (!std::filesystem::is_directory(root, error))
        return sources;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(root, error))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string extension = entry.path().extension().string();
        if (extension == ".cpp" || extension == ".hpp")
            sources.push_back(entry.path());
    }
    return sources;
}

class Win32SourceAudit : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sources_ = Win32Sources();
        if (sources_.empty())
        {
            GTEST_SKIP() << "the Win32 source tree is not reachable from this test binary; the "
                            "mechanical gates (tools/platform/sdl_inventory.py) still cover it";
        }
    }

    std::vector<std::filesystem::path> sources_;
};

TEST_F(Win32SourceAudit, TheBackendIsBuiltFromMoreThanAHandfulOfFiles)
{
    // Guards the audit itself: if the glob stopped finding anything, every assertion below would
    // pass vacuously and report an SDL-free tree that was simply not read.
    EXPECT_GE(sources_.size(), 20u) << "the source scan found suspiciously little to audit";
}

/// Strips comments so the audit reads code rather than prose.
///
/// The distinction matters more here than it looks. This backend's comments explain *why* there is
/// no SDL in it -- that the wheel's sign convention is chosen to match the SDL3 backend, that
/// neither SDL3 nor Headless imposes a precondition this one invented -- and that reasoning is
/// exactly what a future reader needs. Banning the word outright would delete the explanation
/// along with the thing it explains, and a rule that punishes documenting a boundary is a rule
/// people route around.
std::string CodeWithoutComments(const std::string& line, bool& insideBlockComment)
{
    std::string code;
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        if (insideBlockComment)
        {
            if (line.compare(index, 2, "*/") == 0)
            {
                insideBlockComment = false;
                ++index;
            }
            continue;
        }
        if (line.compare(index, 2, "//") == 0)
            break;
        if (line.compare(index, 2, "/*") == 0)
        {
            insideBlockComment = true;
            ++index;
            continue;
        }
        code.push_back(line[index]);
    }
    return code;
}

TEST_F(Win32SourceAudit, NoSourceCodeReferencesSdl)
{
    // The rule the whole backend exists to keep: not one include, call, type or link edge. Any
    // spelling counts -- <SDL3/SDL.h>, SDL_Init, CNA::Platform::Sdl3::anything -- because the way
    // this claim decays is a single "temporary" use of a feature that was awkward to write
    // natively, not a deliberate decision anyone would announce.
    for (const std::filesystem::path& source : sources_)
    {
        std::ifstream stream(source);
        ASSERT_TRUE(stream.is_open()) << source.string();

        std::string line;
        int number = 0;
        bool insideBlockComment = false;
        while (std::getline(stream, line))
        {
            ++number;
            const std::string code = ToLowerAscii(CodeWithoutComments(line, insideBlockComment));
            EXPECT_EQ(code.find("sdl"), std::string::npos)
                << source.filename().string() << ":" << number << ": " << line;
        }
    }
}

TEST_F(Win32SourceAudit, NoSourceIncludesAnSdlHeaderEvenInsideAComment)
{
    // The one SDL spelling that is not allowed even as prose. A commented-out include is a
    // proposal to add one back, and it is the shape a regression takes while it is being written.
    for (const std::filesystem::path& source : sources_)
    {
        std::ifstream stream(source);
        ASSERT_TRUE(stream.is_open()) << source.string();

        std::string line;
        int number = 0;
        while (std::getline(stream, line))
        {
            ++number;
            const std::string lowered = ToLowerAscii(line);
            if (lowered.find("include") == std::string::npos)
                continue;
            EXPECT_EQ(lowered.find("sdl"), std::string::npos)
                << source.filename().string() << ":" << number << ": " << line;
        }
    }
}

TEST_F(Win32SourceAudit, EverySourceCarriesTheProjectLicenceHeader)
{
    for (const std::filesystem::path& source : sources_)
    {
        std::ifstream stream(source);
        ASSERT_TRUE(stream.is_open()) << source.string();
        std::string first;
        std::getline(stream, first);
        EXPECT_EQ(first, "// SPDX-License-Identifier: MS-PL") << source.filename().string();
    }
}

TEST_F(Win32SourceAudit, NoSourceLeavesAnUnrecordedTodo)
{
    // Unfinished work in this backend belongs in plans/plan_win32.md section 15, where it is
    // paired with the capability that stays false because of it -- not in a comment nobody
    // revisits.
    for (const std::filesystem::path& source : sources_)
    {
        std::ifstream stream(source);
        ASSERT_TRUE(stream.is_open()) << source.string();

        std::string line;
        int number = 0;
        while (std::getline(stream, line))
        {
            ++number;
            const std::string lowered = ToLowerAscii(line);
            EXPECT_EQ(lowered.find("todo"), std::string::npos)
                << source.filename().string() << ":" << number << ": " << line;
            EXPECT_EQ(lowered.find("fixme"), std::string::npos)
                << source.filename().string() << ":" << number << ": " << line;
            EXPECT_EQ(lowered.find("hack"), std::string::npos)
                << source.filename().string() << ":" << number << ": " << line;
        }
    }
}

TEST_F(Win32SourceAudit, WindowsIsIncludedOnlyThroughTheModulesOneGuardedEntryPoint)
{
    // Win32Common.hpp is where NOMINMAX, WIN32_LEAN_AND_MEAN, UNICODE and the four macro undefs
    // live. A source that included <windows.h> directly would compile because some other header
    // had already set them -- until it was the first one, at which point `min`/`max` break
    // <algorithm> and `CreateDirectory` silently renames a contract method.
    for (const std::filesystem::path& source : sources_)
    {
        if (source.filename() == "Win32Common.hpp")
            continue;

        std::ifstream stream(source);
        ASSERT_TRUE(stream.is_open()) << source.string();

        std::string line;
        int number = 0;
        while (std::getline(stream, line))
        {
            ++number;
            EXPECT_EQ(ToLowerAscii(line).find("include <windows.h>"), std::string::npos)
                << source.filename().string() << ":" << number
                << ": include \"Win32Common.hpp\" instead";
        }
    }
}

} // namespace
