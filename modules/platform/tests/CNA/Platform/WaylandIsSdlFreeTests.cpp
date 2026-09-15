// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0115: the Wayland backend contains neither SDL nor X11, asserted
// rather than claimed.
//
// The backend exists to be a genuine Wayland client: its windows, input, clipboard and graphics go
// through libwayland-client, xdg-shell, xkbcommon, EGL and VK_KHR_wayland_surface, and nothing in it
// may reach SDL or an X library -- not SDL's Wayland driver, not Xlib, not an Xwayland shortcut.
// X11IsSdlFreeTests.cpp makes the same promise for the X11 backend; this one adds X11 to the list,
// because a Wayland backend that quietly opened an X connection would still pass an SDL check.
//
// Two independent checks, as in the X11 file: a compile-time one that fails if an SDL or X11
// header reaches a translation unit that includes the backend, and a scan of the backend's
// sources and of the shared directories it compiles, with comments stripped.

#include <gtest/gtest.h>

#include "../../../src/Wayland/WaylandPlatform.hpp"

#include "CNA/Platform/PlatformFactory.hpp"

#if defined(SDL_h_) || defined(SDL_H) || defined(_SDL_H) || defined(SDL_VERSION_ATLEAST) || \
    defined(SDL_MAJOR_VERSION) || defined(SDL_INIT_VIDEO)
#  error "The Wayland platform backend must not include any SDL header. plans/plan_wayland.md."
#endif
// Xlib's and xcb's own header guards.
#if defined(_X11_XLIB_H_) || defined(_XLIB_H_) || defined(__XCB_H__) || defined(X_PROTOCOL)
#  error "The Wayland platform backend must not include any X11 header. plans/plan_wayland.md."
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::filesystem::path BackendDirectory()
{
#if defined(CNA_WAYLAND_BACKEND_SOURCE_DIR)
    return std::filesystem::path(CNA_WAYLAND_BACKEND_SOURCE_DIR);
#endif
    std::filesystem::path here(__FILE__);
    return here.parent_path().parent_path().parent_path().parent_path() / "src" / "Wayland";
}

/// Removes comments and string literals, as X11IsSdlFreeTests.cpp does and for its reason: the
/// backend's documentation names SDL and X11 where it explains a decision, and should.
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

/// Reads a file; the include lines are kept separately, because an include's path is a string
/// the stripper removes.
std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream file(path);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::vector<std::string> IncludeLines(const std::string& source)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < source.size())
    {
        std::size_t end = source.find('\n', start);
        if (end == std::string::npos)
        {
            end = source.size();
        }
        const std::string line = source.substr(start, end - start);
        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 1, "#") == 0 && line.find("include") != std::string::npos)
        {
            lines.push_back(line);
        }
        start = end + 1;
    }
    return lines;
}

TEST(WaylandIsSdlFree, TheBackendSourceTreeExistsWhereThisTestLooksForIt)
{
    const std::filesystem::path directory = BackendDirectory();
    ASSERT_TRUE(std::filesystem::is_directory(directory)) << directory;
    int sources = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        sources += entry.is_regular_file() ? 1 : 0;
    }
    EXPECT_GE(sources, 20) << directory << " looks wrong: the Wayland backend has more files than this";
}

TEST(WaylandIsSdlFree, NoCodeInTheBackendReferencesSdlOrX11)
{
    const std::filesystem::path shared = BackendDirectory().parent_path();
    const std::vector<std::filesystem::path> directories = {
        BackendDirectory(), shared / "Linux", shared / "Xkb", shared / "Freedesktop", shared / "Posix", shared / "Common"};

    // Code tokens: SDL's identifiers and CNA's SDL backends; Xlib's and xcb's entry points; GLX,
    // which is X11's GL binding; and CNA's own X11 namespace. Types are caught through the
    // headers that declare them, below.
    static const std::vector<std::string> codeTokens = {
        "SDL_", "SDL3", "SDL2", "Sdl3", "Sdl2", "MIX_",
        "XOpenDisplay", "XCloseDisplay", "XNextEvent", "XPending", "XFlush", "XSync(", "Xutf8", "XInternAtom",
        "xcb_", "glX", "GLXContext", "X11::"};
    // Include paths: any X11, xcb or SDL header, and any header of the X11 backend.
    static const std::vector<std::string> includeTokens = {"<X11/", "<xcb/", "<SDL", "SDL.h", "/X11/", "GL/glx"};
    // The one X header a shared file names, and why it is not a use: XkbKeyMapping.cpp checks its
    // keysym constants against a reference at compile time, xkbcommon's header first and X's
    // <X11/keysym.h> only in an #elif for an X11 build without xkbcommon. A Wayland build always
    // has xkbcommon, so that branch is never compiled in one -- asserted below, not assumed.
    const auto allowed = [](const std::string& where, const std::string& line) {
        return where == "Xkb/XkbKeyMapping.cpp" && line.find("X11/keysym.h") != std::string::npos;
    };

    std::vector<std::string> offenders;
    int scanned = 0;
    for (const std::filesystem::path& directory : directories)
    {
        if (!std::filesystem::is_directory(directory))
        {
            continue;  // src/Linux/ exists only where linux/input.h does
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            ++scanned;
            const std::string source = ReadAll(entry.path());
            const std::string code = StripComments(source);
            const std::string where = directory.filename().string() + "/" + entry.path().filename().string();
            for (const std::string& token : codeTokens)
            {
                if (code.find(token) != std::string::npos)
                {
                    offenders.push_back(where + " references " + token);
                }
            }
            for (const std::string& line : IncludeLines(source))
            {
                for (const std::string& token : includeTokens)
                {
                    if (line.find(token) != std::string::npos && !allowed(where, line))
                    {
                        offenders.push_back(where + " includes " + line);
                    }
                }
            }
        }
    }
    EXPECT_GE(scanned, 20) << "too little was scanned for this assertion to mean anything";
    EXPECT_TRUE(offenders.empty()) << [&offenders] {
        std::string text = "the Wayland backend reaches SDL or X11:\n";
        for (const std::string& offender : offenders)
        {
            text += "  " + offender + "\n";
        }
        return text;
    }();
}

TEST(WaylandIsSdlFree, TheSharedKeysymCheckPrefersXkbcommonOverX)
{
    // The allowance above holds only while X's header is the fallback: xkbcommon's is tried first,
    // and a Wayland build cannot lack it.
    const std::string source = ReadAll(BackendDirectory().parent_path() / "Xkb" / "XkbKeyMapping.cpp");
    const std::size_t xkbcommon = source.find("#if __has_include(<xkbcommon/xkbcommon-keysyms.h>)");
    const std::size_t fallback = source.find("#elif __has_include(<X11/keysym.h>)");
    ASSERT_NE(xkbcommon, std::string::npos);
    ASSERT_NE(fallback, std::string::npos);
    EXPECT_LT(xkbcommon, fallback);
#if !__has_include(<xkbcommon/xkbcommon-keysyms.h>)
    ADD_FAILURE() << "a Wayland build without xkbcommon's keysym header would compile the X fallback";
#endif
}

TEST(WaylandIsSdlFree, TheScannerSeesAnIncludeAndIgnoresProse)
{
    EXPECT_EQ(IncludeLines("#include <X11/Xlib.h>\nint x;\n").size(), 1u);
    EXPECT_EQ(IncludeLines("  #  include \"a.hpp\"\n").size(), 1u);
    EXPECT_TRUE(IncludeLines("// #include <X11/Xlib.h> is what X11 does\n").empty());
    EXPECT_EQ(StripComments("int x; // XOpenDisplay\n"), "int x; \n");
}

TEST(WaylandIsSdlFree, TheBackendIsReachableThroughTheFactory)
{
    const std::vector<std::string> available = CNA::Platform::PlatformFactory::GetAvailable();
    EXPECT_NE(std::find(available.begin(), available.end(), "Wayland"), available.end());
}

} // namespace
