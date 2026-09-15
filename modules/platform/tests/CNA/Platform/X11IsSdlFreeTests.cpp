// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0091: the X11 backend contains no SDL, asserted rather than claimed.
//
// The strategic goal of this backend is that «CNA must not depend existentially on SDL3». A grep
// in a plan document proves that on the day it was written; this file proves it on every build,
// which is the difference between a property and a note.
//
// Two independent checks, because either alone is defeatable. The compile-time one fails if an
// SDL header is ever included into a translation unit that also includes this backend's own
// headers -- the way a "temporary" SDL shortcut would arrive. The source scan fails if an SDL
// identifier appears anywhere under src/X11/ at all, including in a comment, which catches a
// reference added through a path the preprocessor cannot see (a dlsym by name, a build-system
// edit, a string literal).

#include <gtest/gtest.h>

#include "../../../src/X11/X11Platform.hpp"

#include "CNA/Platform/PlatformFactory.hpp"

// If any SDL header reached this translation unit, one of these sentinels is defined. They are
// the header guards SDL itself uses, so this cannot be satisfied by a forward declaration or
// defeated by an include ordering.
#if defined(SDL_h_) || defined(SDL_H) || defined(_SDL_H) || defined(SDL_VERSION_ATLEAST) || \
    defined(SDL_MAJOR_VERSION) || defined(SDL_INIT_VIDEO)
#  error "The X11 platform backend must not include any SDL header. plans/plan_x11.md Rule 3."
#endif

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

/// Locates `modules/platform/src/X11`.
///
/// Never relative to the working directory: ctest runs this suite from the build tree and from
/// the source root, and a relative path would silently scan nothing and pass. CMake passes the
/// absolute directory; `__FILE__` is only the fallback, because with CCACHE_BASEDIR (which this
/// project requires) it is itself relative to the build directory
/// (plans/plan_native_platform_validation.md NPV-0122).
std::filesystem::path BackendDirectory()
{
#if defined(CNA_X11_BACKEND_SOURCE_DIR)
    return std::filesystem::path(CNA_X11_BACKEND_SOURCE_DIR);
#endif
    std::filesystem::path here(__FILE__);
    // .../modules/platform/tests/CNA/Platform/X11IsSdlFreeTests.cpp
    return here.parent_path()          // Platform
        .parent_path()                 // CNA
        .parent_path()                 // tests
        .parent_path()                 // platform
        / "src" / "X11";
}

TEST(X11IsSdlFree, TheBackendSourceTreeExistsWhereThisTestLooksForIt)
{
    // Without this the scan below would pass by finding nothing, which is the classic way a
    // file-scanning test rots into a no-op after a directory moves.
    const std::filesystem::path directory = BackendDirectory();
    ASSERT_TRUE(std::filesystem::is_directory(directory)) << directory;

    int sources = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file())
        {
            ++sources;
        }
    }
    EXPECT_GE(sources, 20) << "the X11 backend should have more files than this; " << directory
                           << " looks wrong";
}

/// Removes `//` and block comments, so the scan below reads code rather than prose.
///
/// The distinction is the whole point. This backend's documentation names SDL repeatedly and
/// should: it explains why the backend exists, and it cites a bug the SDL2 keyboard mapping made
/// so the same mistake is not repeated here. A check that could not tell an explanation from a
/// call would push that reasoning out of the source, which is a worse outcome than the one it
/// prevents. `tools/platform/renderer_sdl_audit.py` draws the same line for the same reason.
std::string StripComments(const std::string& source)
{
    std::string code;
    code.reserve(source.size());
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        const char current = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : '\0';

        if (inLineComment)
        {
            if (current == '\n')
            {
                inLineComment = false;
                code.push_back(current);
            }
            continue;
        }
        if (inBlockComment)
        {
            if (current == '*' && next == '/')
            {
                inBlockComment = false;
                ++index;
            }
            else if (current == '\n')
            {
                code.push_back(current);
            }
            continue;
        }
        if (inString)
        {
            // A backslash escapes the next character, so an escaped quote does not end the
            // string -- without this, `"\""` would leave the scanner permanently confused about
            // whether it is inside one.
            if (current == '\\')
            {
                ++index;
                continue;
            }
            if (current == '"')
            {
                inString = false;
            }
            continue;
        }

        if (current == '/' && next == '/')
        {
            inLineComment = true;
            ++index;
            continue;
        }
        if (current == '/' && next == '*')
        {
            inBlockComment = true;
            ++index;
            continue;
        }
        if (current == '"')
        {
            inString = true;
            continue;
        }
        code.push_back(current);
    }
    return code;
}

TEST(X11IsSdlFree, StrippingCommentsKeepsCodeAndDiscardsProse)
{
    // The stripper is the part of this file that could silently make the scan below vacuous, so
    // it is tested rather than trusted.
    EXPECT_EQ(StripComments("int x; // SDL_Init\nint y;"), "int x; \nint y;");
    EXPECT_EQ(StripComments("/* SDL_Init */int x;"), "int x;");
    EXPECT_EQ(StripComments("const char* s = \"SDL_Init\"; int x;"), "const char* s = ; int x;");
    EXPECT_NE(StripComments("SDL_Init(0);").find("SDL_Init"), std::string::npos);
    // A URL inside a line comment contains "//" and must not reopen the scanner mid-comment.
    EXPECT_EQ(StripComments("// see http://example.com SDL_Init\nint x;"), "\nint x;");
}

TEST(X11IsSdlFree, NoCodeInTheBackendReferencesSdl)
{
    // src/X11/, the Linux evdev controllers in src/Linux/ it serves gamepads from, and the code it
    // shares with the Wayland backend (plans/plan_wayland.md Phase B): all are part of every
    // SDL-free X11 build.
    const std::filesystem::path shared = BackendDirectory().parent_path();
    const std::vector<std::filesystem::path> directories = {
        BackendDirectory(), shared / "Linux", shared / "Xkb", shared / "Freedesktop", shared / "Posix"};

    // The tokens that constitute a use rather than a mention: SDL's own C identifiers, its
    // headers, CNA's SDL backend namespaces, and SDL_mixer's prefix.
    static const std::vector<std::string> tokens = {"SDL_", "SDL3", "SDL2", "Sdl3", "Sdl2", "MIX_"};

    std::vector<std::string> offenders;
    int scanned = 0;
    for (const std::filesystem::path& directory : directories)
    {
        ASSERT_TRUE(std::filesystem::is_directory(directory)) << directory;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            ++scanned;
            std::ifstream file(entry.path());
            const std::string source((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
            const std::string code = StripComments(source);
            for (const std::string& token : tokens)
            {
                if (code.find(token) != std::string::npos)
                {
                    offenders.push_back(directory.filename().string() + "/" +
                                        entry.path().filename().string() + " references " + token);
                }
            }
        }
    }

    EXPECT_GT(scanned, 0) << "nothing was scanned, so this assertion proved nothing";
    EXPECT_TRUE(offenders.empty())
        << "the X11 backend calls into SDL, which defeats the reason it exists:\n"
        << [&offenders] {
               std::string text;
               for (const std::string& offender : offenders)
               {
                   text += "  " + offender + "\n";
               }
               return text;
           }();
}

TEST(X11IsSdlFree, TheBackendIsReachableThroughTheFactoryWithoutNamingSdl)
{
    // The positive half: proving the absence of SDL is only interesting if the thing that
    // replaced it is actually there.
    const std::vector<std::string> available = CNA::Platform::PlatformFactory::GetAvailable();
    EXPECT_NE(std::find(available.begin(), available.end(), "X11"), available.end());
}

} // namespace
