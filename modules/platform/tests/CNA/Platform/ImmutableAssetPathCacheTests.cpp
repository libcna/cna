// SPDX-License-Identifier: MS-PL

#include "../../../src/Common/ImmutableAssetPathCache.hpp"

#include <gtest/gtest.h>

#include <array>
#include <barrier>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

using CNA::Platform::Common::ImmutableAssetPathCache;
using Names = std::vector<std::string>;

// Oracle for all fixtures: literal package tree, canonical spelling and unique ASCII matching
// from IPlatformFileSystem::TryLoadFileIgnoringCase; counts derive from distinct parent directories.
struct Package
{
    std::map<std::string, Names> directories{
        {"", {"Content", "Empty"}},
        {"Content", {"Textures", "Audio"}},
        {"Content/Textures", {"CarPaint.PNG", "Other.png"}},
        {"Content/Audio", {"Theme.OGG"}},
        {"Empty", {}}
    };
    std::map<std::string, int> calls;
    std::mutex mutex;

    ImmutableAssetPathCache::Listing List(const std::string& directory)
    {
        const std::lock_guard lock(mutex);
        ++calls[directory];
        const auto found = directories.find(directory);
        if (found == directories.end()) return std::nullopt;
        return found->second;
    }

    ImmutableAssetPathCache::ListDirectory Lister()
    {
        return [this](const std::string& directory) { return List(directory); };
    }
};

TEST(PlatformImmutableAssetPathCacheTest, ResolvesCanonicalComponentsAndListsEachParentOnce)
{
    Package package;
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    EXPECT_EQ(cache.Resolve("content/textures/carpaint.png", list), "Content/Textures/CarPaint.PNG");
    EXPECT_EQ(cache.Resolve("CONTENT/TEXTURES/OTHER.PNG", list), "Content/Textures/Other.png");
    EXPECT_EQ(cache.Resolve("Content/Textures/CarPaint.PNG", list), "Content/Textures/CarPaint.PNG");
    // Three parents: root, Content, Content/Textures. Request casing is never a cache key.
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Textures", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, SixHundredTwentyMissingXnbAndImagePairsNeedOnlyThreeListings)
{
    Package package;
    auto& textures = package.directories.at("Content/Textures");
    textures.clear();
    for (int index = 0; index < 620; ++index)
        textures.push_back("Texture" + std::to_string(index) + ".PNG");
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    for (int index = 0; index < 620; ++index)
    {
        const std::string base = "content/textures/texture" + std::to_string(index);
        EXPECT_EQ(cache.Resolve(base + ".xnb", list), "");
        EXPECT_EQ(cache.Resolve(base + ".png", list),
            "Content/Textures/Texture" + std::to_string(index) + ".PNG");
    }
    // 620 * 2 requests, three distinct parents => exactly three list calls, not 620 * 2 * 3.
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Textures", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, MissingComponentsAndEmptyDirectoriesStayCached)
{
    Package package;
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    for (int index = 0; index < 1000; ++index)
    {
        const std::string name = "missing" + std::to_string(index);
        EXPECT_EQ(cache.Resolve(name + "/anything.png", list), "");
        EXPECT_EQ(cache.Resolve("content/" + name + "/anything.png", list), "");
        EXPECT_EQ(cache.Resolve("empty/" + name, list), "");
    }
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Empty", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, SharedAncestorsAndEqualBasenamesRemainDirectoryScoped)
{
    Package package;
    package.directories.at("Content/Audio").push_back("CarPaint.png");
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    EXPECT_EQ(cache.Resolve("content/textures/carpaint.png", list), "Content/Textures/CarPaint.PNG");
    EXPECT_EQ(cache.Resolve("content/audio/carpaint.png", list), "Content/Audio/CarPaint.png");
    EXPECT_EQ(cache.Resolve("CONTENT/AUDIO/THEME.ogg", list), "Content/Audio/Theme.OGG");
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Audio", 1}, {"Content/Textures", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, AmbiguousLeafFailsForEverySpellingAndKeepsOtherEntriesUsable)
{
    Package package;
    package.directories.at("Content/Textures").push_back("CARPAINT.png");
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    for (const std::string name : {"carpaint.png", "CarPaint.PNG", "CARPAINT.png"})
        EXPECT_EQ(cache.Resolve("content/textures/" + name, list), "");
    // Exact reads occur before this fallback; an exact component does not break fallback ties.
    EXPECT_EQ(cache.Resolve("content/textures/other.png", list), "Content/Textures/Other.png");
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Textures", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, AmbiguousDirectoryNeverListsEitherCandidate)
{
    Package package;
    package.directories.at("Content").push_back("TEXTURES");
    package.directories.emplace("Content/TEXTURES", Names{"Other.png"});
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    EXPECT_EQ(cache.Resolve("content/textures/other.png", list), "");
    EXPECT_EQ(cache.Resolve("Content/Textures/Other.png", list), "");
    EXPECT_EQ(cache.Resolve("content/audio/theme.ogg", list), "Content/Audio/Theme.OGG");
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Audio", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, DotAndRepeatedSeparatorsReuseCanonicalKeys)
{
    Package package;
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    EXPECT_EQ(cache.Resolve("./content//./textures/carpaint.png/", list),
        "Content/Textures/CarPaint.PNG");
    EXPECT_EQ(cache.Resolve("content/textures/./OTHER.png", list), "Content/Textures/Other.png");
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Textures", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, EmptyAbsoluteAndParentPathsAreRefused)
{
    Package package;
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    for (const std::string path : {"", "/content/textures/carpaint.png", ".", "./", "../Content"})
        EXPECT_EQ(cache.Resolve(path, list), "");
    EXPECT_TRUE(package.calls.empty());
    EXPECT_EQ(cache.Resolve("content/../Empty/file", list), "");
    // The old Android walker lists the root before encountering a later parent component.
    EXPECT_EQ(package.calls, (std::map<std::string, int>{{"", 1}}));
    EXPECT_EQ(cache.Resolve("content/audio/theme.ogg", list), "Content/Audio/Theme.OGG");
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Audio", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, FailedListingRetriesWithoutDiscardingSuccessfulAncestors)
{
    Package package;
    ImmutableAssetPathCache cache;
    int attempts = 0;
    const ImmutableAssetPathCache::ListDirectory list = [&](const std::string& directory)
        -> ImmutableAssetPathCache::Listing
    {
        if (directory == "Content/Textures" && ++attempts == 1) return std::nullopt;
        return package.List(directory);
    };
    EXPECT_EQ(cache.Resolve("content/textures/carpaint.png", list), "");
    EXPECT_EQ(cache.Resolve("CONTENT/TEXTURES/CARPAINT.PNG", list), "Content/Textures/CarPaint.PNG");
    EXPECT_EQ(cache.Resolve("content/textures/other.png", list), "Content/Textures/Other.png");
    EXPECT_EQ(attempts, 2);
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Textures", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, ThrowingListerLeavesCacheUnlockedAndRetryable)
{
    Package package;
    ImmutableAssetPathCache cache;
    int attempts = 0;
    const ImmutableAssetPathCache::ListDirectory list = [&](const std::string& directory)
        -> ImmutableAssetPathCache::Listing
    {
        if (++attempts == 1) throw std::runtime_error("fixture listing failure");
        return package.List(directory);
    };
    EXPECT_THROW((void)cache.Resolve("content/audio/theme.ogg", list), std::runtime_error);
    EXPECT_EQ(cache.Resolve("content/audio/theme.ogg", list), "Content/Audio/Theme.OGG");
    EXPECT_EQ(attempts, 4); // one refused root + root + Content + Content/Audio
}

TEST(PlatformImmutableAssetPathCacheTest, IndependentCachesDoNotSharePackageNamesOrLifetime)
{
    Package package;
    {
        ImmutableAssetPathCache cache;
        EXPECT_EQ(cache.Resolve("content/textures/carpaint.png", package.Lister()),
            "Content/Textures/CarPaint.PNG");
    }
    // Replacing a package requires a new service/cache, never invalidation of a live APK cache.
    package.directories.at("Content/Textures") = {"CARPAINT.png"};
    ImmutableAssetPathCache replacement;
    EXPECT_EQ(replacement.Resolve("content/textures/carpaint.png", package.Lister()),
        "Content/Textures/CARPAINT.png");
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 2}, {"Content", 2}, {"Content/Textures", 2}}));
}

TEST(PlatformImmutableAssetPathCacheTest, NonAsciiBytesArePreservedAndOnlyAsciiCaseFolds)
{
    Package package;
    package.directories.at("Content/Textures") = {"\xC3\x89" "clair.PNG"};
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    EXPECT_EQ(cache.Resolve("content/textures/\xC3\x89" "CLAIR.png", list),
        "Content/Textures/\xC3\x89" "clair.PNG");
    EXPECT_EQ(cache.Resolve("content/textures/\xC3\xA9" "clair.png", list), "");
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Textures", 1}}));
}

TEST(PlatformImmutableAssetPathCacheTest, ConcurrentColdLookupsPublishEachDirectoryOnce)
{
    Package package;
    ImmutableAssetPathCache cache;
    const auto list = package.Lister();
    std::barrier start(8);
    std::array<bool, 8> correct{};
    std::vector<std::jthread> workers;
    for (std::size_t index = 0; index < correct.size(); ++index)
    {
        workers.emplace_back([&, index]
        {
            start.arrive_and_wait();
            bool ok = true;
            for (int repeat = 0; repeat < 16; ++repeat)
            {
                ok &= cache.Resolve("content/textures/carpaint.png", list) ==
                    "Content/Textures/CarPaint.PNG";
                ok &= cache.Resolve("CONTENT/TEXTURES/MISSING.XNB", list).empty();
            }
            correct[index] = ok;
        });
    }
    workers.clear(); // join before inspecting counters or per-worker results
    for (bool ok : correct) EXPECT_TRUE(ok);
    EXPECT_EQ(package.calls, (std::map<std::string, int>{
        {"", 1}, {"Content", 1}, {"Content/Textures", 1}}));
}

} // namespace
