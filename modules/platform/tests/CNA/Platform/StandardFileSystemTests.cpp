// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "System/Environment.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class ScopedEnvironment final
    {
    public:
        ScopedEnvironment(std::string name, const std::string& value)
            : name_(std::move(name))
        {
            if (const char* previous = std::getenv(name_.c_str()))
            {
                hadPrevious_ = true;
                previous_ = previous;
            }
            System::Environment::SetEnvironmentVariable(name_, value);
        }

        ~ScopedEnvironment()
        {
            System::Environment::SetEnvironmentVariable(
                name_, hadPrevious_ ? previous_ : std::string{});
        }

        ScopedEnvironment(const ScopedEnvironment&) = delete;
        ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    private:
        std::string name_;
        std::string previous_;
        bool hadPrevious_ = false;
    };

    class ScopedTestDirectory final
    {
    public:
        ScopedTestDirectory()
            : path_(std::filesystem::temp_directory_path()
                    / ("cna-standard-filesystem-"
                       + std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch().count())))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScopedTestDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] const std::filesystem::path& GetPath() const { return path_; }

    private:
        std::filesystem::path path_;
    };
}

TEST(StandardFileSystemTests, HeadlessAndTerminalReadXdgUserDirectories)
{
    const ScopedTestDirectory configRoot;
    const std::filesystem::path home = configRoot.GetPath() / "home";
    std::filesystem::create_directories(home);
    {
        std::ofstream config(configRoot.GetPath() / "user-dirs.dirs");
        ASSERT_TRUE(config.good());
        config << "XDG_MUSIC_DIR=\"$HOME/My Music\"\n"
                  "XDG_PICTURES_DIR=\"/srv/shared/pictures\"\n";
    }

    const ScopedEnvironment homeEnvironment("HOME", home.string());
    const ScopedEnvironment configEnvironment("XDG_CONFIG_HOME", configRoot.GetPath().string());
    const std::vector<std::string> available = CNA::Platform::PlatformFactory::GetAvailable();

    for (const std::string implementation : {"Headless", "Terminal"})
    {
        if (std::find(available.begin(), available.end(), implementation) == available.end())
            continue;

        const std::unique_ptr<CNA::Platform::IPlatform> platform =
            CNA::Platform::PlatformFactory::Create(implementation);
        ASSERT_NE(platform, nullptr);
        ASSERT_NE(platform->GetFileSystem(), nullptr);
        EXPECT_EQ(platform->GetFileSystem()->GetUserFolder(CNA::Platform::UserFolder::Music),
                  (home / "My Music").string() + "/");
        EXPECT_EQ(platform->GetFileSystem()->GetUserFolder(CNA::Platform::UserFolder::Pictures),
                  "/srv/shared/pictures/");
    }
}

TEST(StandardFileSystemTests, MissingXdgConfigurationIsAGracefulEmptyResult)
{
    const ScopedTestDirectory configRoot;
    const ScopedEnvironment configEnvironment("XDG_CONFIG_HOME", configRoot.GetPath().string());
    const std::unique_ptr<CNA::Platform::IPlatform> platform =
        CNA::Platform::PlatformFactory::Create("Headless");

    ASSERT_NE(platform, nullptr);
    EXPECT_TRUE(platform->GetFileSystem()->GetUserFolder(
        CNA::Platform::UserFolder::Music).empty());
    EXPECT_TRUE(platform->GetFileSystem()->GetUserFolder(
        CNA::Platform::UserFolder::Pictures).empty());
}
