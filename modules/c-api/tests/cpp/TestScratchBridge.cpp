// SPDX-License-Identifier: MS-PL
#include "CNA/TestSupport/TestScratchEnvironment.hpp"
#include "../pure_c/CnaTestScratch.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    struct RestoreWorkingDirectory
    {
        std::filesystem::path previous = std::filesystem::current_path();
        ~RestoreWorkingDirectory()
        {
            std::error_code ignored;
            std::filesystem::current_path(previous, ignored);
        }
    };

    std::string Utf8(const std::filesystem::path& path)
    {
        const auto bytes = path.generic_u8string();
        return std::string(bytes.begin(), bytes.end());
    }
    void SetEnvironment(const char* name, const std::string& value)
    {
#if defined(_WIN32)
        if (::_putenv_s(name, value.c_str()) != 0) throw std::runtime_error("set scratch environment");
#else
        if (::setenv(name, value.c_str(), 1) != 0) throw std::runtime_error("set scratch environment");
#endif
    }
    std::string QuotedFolder(const char* value)
    {
        std::string escaped;
        for (const char c : std::string(value))
        {
            if (c == '\\' || c == '"' || c == '$' || c == '`') escaped += '\\';
            escaped += c;
        }
        return "\"" + escaped + "\"";
    }
}

extern "C" int cna_test_scratch_enter(void)
{
    try
    {
        const auto root = CNA::TestSupport::FixtureRoot();
        CNA::TestSupport::UseProcessTempRoot();
        // Construct after the scratch owner: restore cwd before its destructor removes the
        // directory. Windows refuses to delete a process's current working directory.
        static const RestoreWorkingDirectory workingDirectory;
        (void)workingDirectory;
        std::filesystem::create_directories(root / "data");
        std::filesystem::create_directories(root / "Music");
        std::filesystem::create_directories(root / "Pictures");
        std::filesystem::create_directories(root / "config");
        SetEnvironment("XDG_DATA_HOME", Utf8(root / "data"));
        SetEnvironment("LOCALAPPDATA", Utf8(root / "data"));
        SetEnvironment("XDG_CONFIG_HOME", Utf8(root / "config"));
#if defined(__APPLE__)
        SetEnvironment("CFFIXED_USER_HOME", Utf8(root));
#endif
        {
            std::ofstream config(root / "config" / "user-dirs.dirs");
            config << "XDG_MUSIC_DIR=" << QuotedFolder(cna_test_scratch_music()) << '\n'
                   << "XDG_PICTURES_DIR=" << QuotedFolder(cna_test_scratch_pictures()) << '\n';
            if (!config.good()) throw std::runtime_error("write scratch user folders");
        }
        // Preserve the original relative fixture/asset spellings and counted-string assertions.
        std::filesystem::current_path(root);
        return 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "scratch setup: " << error.what() << '\n';
        return 0;
    }
}

extern "C" const char* cna_test_scratch_root(void)
{
    // ContentSmoke preserves the original trailing-separator and byte-count contract.
    static const std::string path = Utf8(CNA::TestSupport::FixtureRoot() / "");
    return path.c_str();
}

extern "C" const char* cna_test_scratch_music(void)
{
    static const std::string path = Utf8(CNA::TestSupport::FixtureRoot() / "Music");
    return path.c_str();
}

extern "C" const char* cna_test_scratch_pictures(void)
{
    static const std::string path = Utf8(CNA::TestSupport::FixtureRoot() / "Pictures");
    return path.c_str();
}
