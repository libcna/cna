// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-36: ContentManager's general loadedAssets_ cache must be keyed by (type, name),
// not name alone -- otherwise Load<T2>() for a logical name a different T1 is already cached
// under throws std::bad_any_cast (an unrelated, undocumented exception type) instead of reaching
// normal .cnj envelope validation and a clear ContentLoadException.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace
{
    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors ContentManagerSkinnedModelTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_cache_type_safety_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    struct GameDataA { std::string kind; };
    struct GameDataB { int value = 0; };
}

TEST(CnjAssetCacheTypeSafetyTest, DifferentTypeSameNameThrowsContentLoadExceptionNotBadAnyCast)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "goblin.cnj", R"({"cnjVersion": 1, "type": "A"})");

    ContentManager cm(nullptr, root.path().string());
    cm.RegisterCnjLoader<GameDataA>("A",
        [](const std::string&, ContentManager&) { return GameDataA{"enemy"}; });
    cm.RegisterCnjLoader<GameDataB>("B",
        [](const std::string&, ContentManager&) { return GameDataB{42}; });

    GameDataA a = cm.Load<GameDataA>("goblin");
    EXPECT_EQ(a.kind, "enemy");

    // Same logical name "goblin", but a different T, whose .cnj "type" ("A") doesn't match what
    // GameDataB's factory is registered under ("B") -- this must reach normal envelope
    // validation and throw a clean ContentLoadException, not std::bad_any_cast.
    EXPECT_THROW(cm.Load<GameDataB>("goblin"), ContentLoadException);
}

TEST(CnjAssetCacheTypeSafetyTest, SameTypeSameNameStillReturnsCachedInstance)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "goblin.cnj", R"({"cnjVersion": 1, "type": "A"})");

    ContentManager cm(nullptr, root.path().string());
    int callCount = 0;
    cm.RegisterCnjLoader<GameDataA>("A",
        [&callCount](const std::string&, ContentManager&)
        {
            ++callCount;
            return GameDataA{"enemy"};
        });

    GameDataA first = cm.Load<GameDataA>("goblin");
    GameDataA second = cm.Load<GameDataA>("goblin");

    EXPECT_EQ(first.kind, "enemy");
    EXPECT_EQ(second.kind, "enemy");
    EXPECT_EQ(callCount, 1) << "second Load<T>() for the same (type, name) must hit the cache";
}
