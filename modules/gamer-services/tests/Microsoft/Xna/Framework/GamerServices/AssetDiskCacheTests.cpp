// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "System/Environment.hpp"
#include <optional>

#include "CNA/Internal/GamerServices/AssetDiskCache.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/PathUtf8.hpp"

#include <filesystem>
#include <algorithm>
#include <cerrno>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#endif

using CNA::Internal::GamerServices::AssetDiskCache;
namespace Avatars = CNA::Internal::GamerServices::Avatars;

namespace {
std::vector<unsigned char> bytesOf(char fill, std::size_t size) {return std::vector<unsigned char>(size, static_cast<unsigned char>(fill));}
std::string hashOf(const std::vector<unsigned char>& bytes) {return Avatars::sha256Hex(std::span<const std::uint8_t>(bytes.data(), bytes.size()));}

class AssetDiskCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / ("cna-asset-cache-" + std::to_string(System::Environment::getProcessIdProperty()) + "-" + std::to_string(++counter_));
        std::filesystem::remove_all(root_);
    }
    void TearDown() override {std::filesystem::remove_all(root_);}
    void age(const std::filesystem::path& path, std::chrono::minutes by) {
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() - by);
    }
    std::filesystem::path root_;
    static inline int counter_ = 0;
};
}

TEST_F(AssetDiskCacheTest, StoresAndReadsAnEntryByItsHash) {
    const AssetDiskCache cache(root_, "writer");
    const auto bytes = bytesOf('a', 1000);
    const auto hash = hashOf(bytes);
    EXPECT_FALSE(cache.read(hash).has_value());
    EXPECT_TRUE(cache.write(hash, bytes));
    EXPECT_EQ(cache.read(hash), bytes);
    EXPECT_TRUE(std::filesystem::is_regular_file(root_ / hash));
    // The temporary became the entry; native writers also keep a stable capacity lock inode.
    for (const auto& entry : std::filesystem::directory_iterator(root_)) {
        const auto name = entry.path().filename().string();
        EXPECT_TRUE(name == hash || name == ".capacity.lock");
    }
}

TEST_F(AssetDiskCacheTest, ADamagedEntryIsAMissAndIsRemoved) {
    const AssetDiskCache cache(root_, "writer");
    const auto bytes = bytesOf('b', 500);
    const auto hash = hashOf(bytes);
    ASSERT_TRUE(cache.write(hash, bytes));
    {std::ofstream damage(root_ / hash, std::ios::binary | std::ios::in | std::ios::out); damage.seekp(10); damage.put('X');}
    EXPECT_FALSE(cache.read(hash).has_value());
    EXPECT_FALSE(std::filesystem::exists(root_ / hash));
    // The next download stores a good copy again.
    EXPECT_TRUE(cache.write(hash, bytes));
    EXPECT_EQ(cache.read(hash), bytes);
}

TEST_F(AssetDiskCacheTest, ATruncatedEntryFromAnotherWriterIsAMiss) {
    const AssetDiskCache cache(root_, "writer");
    const auto bytes = bytesOf('c', 800);
    const auto hash = hashOf(bytes);
    std::filesystem::create_directories(root_);
    {std::ofstream partial(root_ / hash, std::ios::binary); partial.write(reinterpret_cast<const char*>(bytes.data()), 400);}
    EXPECT_FALSE(cache.read(hash).has_value());
    {std::ofstream empty(root_ / hash, std::ios::binary);}
    EXPECT_FALSE(cache.read(hash).has_value());
}

TEST_F(AssetDiskCacheTest, InterruptedWritesLeaveNoEntryAndOldTemporariesAreRemoved) {
    const AssetDiskCache cache(root_, "writer");
    const auto bytes = bytesOf('d', 300);
    const auto hash = hashOf(bytes);
    std::filesystem::create_directories(root_);
    // A crash between writing and renaming leaves only a temporary.
    const auto stale = root_ / (hash + ".deadprocess.tmp");
    const auto live = root_ / (hashOf(bytesOf('e', 10)) + ".otherprocess.tmp");
    {std::ofstream(stale, std::ios::binary) << "partial";}
    {std::ofstream(live, std::ios::binary) << "in progress";}
    age(stale, std::chrono::minutes(30));
    EXPECT_FALSE(cache.read(hash).has_value());
    ASSERT_TRUE(cache.write(hashOf(bytesOf('f', 20)), bytesOf('f', 20)));
    EXPECT_FALSE(std::filesystem::exists(stale));
    // Another process's fresh temporary is left alone.
    EXPECT_TRUE(std::filesystem::exists(live));
}

TEST_F(AssetDiskCacheTest, AFullCacheEvictsTheLeastRecentlyUsedEntries) {
    const AssetDiskCache cache(root_, "writer", 3000);
    const auto first = bytesOf('1', 1000), second = bytesOf('2', 1000), third = bytesOf('3', 1000), fourth = bytesOf('4', 1000);
    ASSERT_TRUE(cache.write(hashOf(first), first));
    ASSERT_TRUE(cache.write(hashOf(second), second));
    ASSERT_TRUE(cache.write(hashOf(third), third));
    age(root_ / hashOf(first), std::chrono::minutes(30));
    age(root_ / hashOf(second), std::chrono::minutes(20));
    age(root_ / hashOf(third), std::chrono::minutes(10));
    // Reading the oldest makes it the most recently used.
    ASSERT_TRUE(cache.read(hashOf(first)).has_value());
    ASSERT_TRUE(cache.write(hashOf(fourth), fourth));
    EXPECT_TRUE(cache.read(hashOf(first)).has_value());
    EXPECT_FALSE(cache.read(hashOf(second)).has_value());
    EXPECT_TRUE(cache.read(hashOf(third)).has_value());
    EXPECT_TRUE(cache.read(hashOf(fourth)).has_value());
}

TEST_F(AssetDiskCacheTest, EntriesLargerThanTheCacheAreNotStored) {
    const AssetDiskCache small(root_, "writer", 100);
    const auto bytes = bytesOf('g', 101);
    EXPECT_FALSE(small.write(hashOf(bytes), bytes));
    EXPECT_FALSE(small.read(hashOf(bytes)).has_value());
    const AssetDiskCache cache(root_, "writer");
    EXPECT_FALSE(cache.write(hashOf(bytes), {}));
    EXPECT_FALSE(cache.write("not-a-hash", bytes));
}

TEST_F(AssetDiskCacheTest, NoRootMeansNoCache) {
    const AssetDiskCache cache({}, "writer");
    const auto bytes = bytesOf('h', 10);
    EXPECT_FALSE(cache.write(hashOf(bytes), bytes));
    EXPECT_FALSE(cache.read(hashOf(bytes)).has_value());
}

TEST_F(AssetDiskCacheTest, ALinkIsNeverFollowed) {
    const AssetDiskCache cache(root_, "writer");
    const auto bytes = bytesOf('i', 64);
    const auto hash = hashOf(bytes);
    std::filesystem::create_directories(root_);
    {std::ofstream target(root_ / "elsewhere", std::ios::binary); target.write(reinterpret_cast<const char*>(bytes.data()), 64);}
    try {
        std::filesystem::create_symlink(root_ / "elsewhere", root_ / hash);
    } catch (const std::filesystem::filesystem_error& error) {
        GTEST_SKIP() << "this filesystem creates no symbolic links: " << error.what();
    }
    EXPECT_FALSE(cache.read(hash).has_value());
}

TEST_F(AssetDiskCacheTest, ConcurrentWritersOfOneEntryLeaveItWhole) {
    const auto bytes = bytesOf('j', 200000);
    const auto hash = hashOf(bytes);
    std::vector<std::thread> writers;
    for (int k = 0; k < 6; ++k) {
        writers.emplace_back([&, k] {
            const AssetDiskCache cache(root_, "writer" + std::to_string(k));
            for (int round = 0; round < 5; ++round) {
                cache.write(hash, bytes);
                if (auto read = cache.read(hash)) EXPECT_EQ(*read, bytes);
            }
        });
    }
    for (auto& writer : writers) writer.join();
    EXPECT_EQ(AssetDiskCache(root_, "reader").read(hash), bytes);
    for (const auto& entry : std::filesystem::directory_iterator(root_)) {
        const auto name = entry.path().filename().string();
        EXPECT_TRUE(name == hash || name == ".capacity.lock");
    }
}

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
TEST_F(AssetDiskCacheTest, AFailedCapacityLockNeverPublishesAnUnlockedEntry) {
    std::filesystem::create_directories(root_ / ".capacity.lock");
    const auto bytes = bytesOf('k', 1000);
    const auto hash = hashOf(bytes);
    EXPECT_FALSE(AssetDiskCache(root_, "writer").write(hash, bytes));
    EXPECT_FALSE(std::filesystem::exists(root_ / hash));
}

TEST_F(AssetDiskCacheTest, ACapacityLockLinkIsNotFollowed) {
    std::filesystem::create_directories(root_);
    const auto target = root_ / "unrelated";
    { std::ofstream file(target); file << "sentinel"; }
    try {
        std::filesystem::create_symlink(target, root_ / ".capacity.lock");
    } catch (const std::filesystem::filesystem_error& error) {
        GTEST_SKIP() << "no symbolic-link support: " << error.what();
    }
    const auto bytes = bytesOf('l', 1000);
    const auto hash = hashOf(bytes);
    EXPECT_FALSE(AssetDiskCache(root_, "writer").write(hash, bytes));
    EXPECT_FALSE(std::filesystem::exists(root_ / hash));
    std::ifstream file(target);
    std::string value;
    file >> value;
    EXPECT_EQ(value, "sentinel");
}
#endif

TEST_F(AssetDiskCacheTest, TheConfiguredLocationComesFromTheEnvironment) {
    const auto* previous = std::getenv("CNA_GAMER_SERVICES_CACHE_DIR");
    const std::string saved = previous ? previous : "";
    System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CACHE_DIR", root_.string());
    EXPECT_EQ(AssetDiskCache::defaultRoot(), root_);
    if (previous) System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CACHE_DIR", saved);
    else System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CACHE_DIR", std::nullopt);
}

#if defined(__unix__) || defined(__APPLE__)
TEST_F(AssetDiskCacheTest, DifferentProcessesRespectOneCapacity) {
    constexpr auto PayloadSize = AssetDiskCache::MaximumEntry;
    constexpr const char* ChildRoot = "CNA_TEST_CACHE_CAPACITY_ROOT";
    if (const auto root = System::Environment::GetEnvironmentVariable(ChildRoot)) {
        const auto shared = CNA::Internal::PathFromUtf8(*root);
        const auto writer = std::to_string(System::Environment::getProcessIdProperty());
        auto bytes = bytesOf('a', PayloadSize);
        std::copy(writer.begin(), writer.end(), bytes.begin());
        const auto hash = hashOf(bytes);
        { std::ofstream ready(shared / ("ready-" + writer)); ready << "ready"; }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!std::filesystem::exists(shared / "go") && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ASSERT_TRUE(std::filesystem::exists(shared / "go"));
        EXPECT_TRUE(AssetDiskCache(shared, writer, PayloadSize).write(hash, bytes));
        return;
    }
    std::filesystem::create_directories(root_);
    struct RestoreEnvironment {
        const char* name;
        std::optional<std::string> previous;
        ~RestoreEnvironment() { System::Environment::SetEnvironmentVariable(name, previous); }
    } restore{ChildRoot, System::Environment::GetEnvironmentVariable(ChildRoot)};
    System::Environment::SetEnvironmentVariable(ChildRoot, CNA::Internal::PathToUtf8(root_));
    struct Children {
        std::vector<pid_t> values;
        ~Children() {
            for (const auto child : values) {
                if (child <= 0) continue;
                kill(child, SIGTERM);
                int status;
                while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            }
        }
    } children;
    const auto self = System::Environment::getProcessPathProperty();
    ASSERT_FALSE(self.empty());
    for (int index = 0; index != 8; ++index) {
        const auto child = fork();
        ASSERT_GE(child, 0);
        if (child == 0) {
            execl(self.c_str(), self.c_str(),
                  "--gtest_filter=AssetDiskCacheTest.DifferentProcessesRespectOneCapacity",
                  static_cast<char*>(nullptr));
            _exit(127);
        }
        children.values.push_back(child);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    const auto allReady = [&] {
        return std::all_of(children.values.begin(), children.values.end(), [&](const auto child) {
            return std::filesystem::exists(root_ / ("ready-" + std::to_string(child)));
        });
    };
    while (!allReady() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_TRUE(allReady());
    { std::ofstream go(root_ / "go"); go << "go"; }
    for (auto& child : children.values) {
        int status = 0;
        pid_t result;
        do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
        ASSERT_EQ(result, child);
        child = 0;
        ASSERT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
    std::uintmax_t published = 0;
    int entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root_)) {
        const auto name = entry.path().filename().string();
        if (name.size() != 64 || name.find_first_not_of("0123456789abcdef") != std::string::npos) continue;
        published += entry.file_size();
        ++entries;
        EXPECT_TRUE(AssetDiskCache(root_, "reader").read(name).has_value());
    }
    EXPECT_GT(entries, 0);
    EXPECT_LE(published, PayloadSize);
}
#endif
