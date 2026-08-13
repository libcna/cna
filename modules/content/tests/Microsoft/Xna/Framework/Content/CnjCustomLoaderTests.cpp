// SPDX-License-Identifier: MS-PL
//
// plan_cnj.md CNB-26/CNB-27: tests for RegisterCnjLoader<T> (CNB-24/CNB-25) -- a game-registered,
// .cnj "type"-string-keyed loader table, for asset types with no dedicated ContentTypeReader<T>.
// Matches cnj.md's "Custom loaders" section worked example ("EnemyDefinition"/"LootTable" both
// producing the same generic data struct via two different factories).

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors ContentManagerSkinnedModelTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_custom_loader_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    // A game-defined type with no CNA-provided reader -- exactly cnj.md's "Custom loaders"
    // example: several distinct .cnj "type" strings can all deserialize into this one struct.
    struct GameData
    {
        std::string kind;
        int iconWidth = 0;
    };
}

// plan_cnj.md CNB-38: no GraphicsDevice here -- none of this fixture's tests touch graphics, so
// none of them need to pay for real window/SDL-video-subsystem creation (which fails outright
// under a genuinely headless environment, e.g. CNA_GRAPHICS_RENDERER=HEADLESS's whole point, or
// a dummy video driver under EasyGL). Only FactoryCanRecursivelyLoadReferencedTexture actually
// needs one, via CnjCustomLoaderGraphicsTest below.
class CnjCustomLoaderTest : public ::testing::Test
{
};

// The one test in this file that genuinely needs a real GraphicsDevice (it loads a Texture2D).
// Kept as a separate fixture so the other 8, graphics-independent tests above don't pay for
// window/SDL-video creation they never use.
class CnjCustomLoaderGraphicsTest : public CnjCustomLoaderTest
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnjCustomLoaderTest, TwoDifferentTypeNamesProduceSameTViaDifferentFactories)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "goblin.cnj", R"({"cnjVersion": 1, "type": "EnemyDefinition"})");
    WriteFile(root.path() / "chest.cnj", R"({"cnjVersion": 1, "type": "LootTable"})");

    ContentManager cm(nullptr, root.path().string());

    cm.RegisterCnjLoader<GameData>("EnemyDefinition",
        [](const std::string&, ContentManager&) { GameData d; d.kind = "Enemy"; return d; });
    cm.RegisterCnjLoader<GameData>("LootTable",
        [](const std::string&, ContentManager&) { GameData d; d.kind = "Loot"; return d; });

    GameData enemy = cm.Load<GameData>("goblin");
    GameData loot = cm.Load<GameData>("chest");

    EXPECT_EQ(enemy.kind, "Enemy");
    EXPECT_EQ(loot.kind, "Loot");
}

TEST_F(CnjCustomLoaderTest, UnregisteredTypeNameThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "mystery.cnj", R"({"cnjVersion": 1, "type": "Unknown"})");

    ContentManager cm(nullptr, root.path().string());
    cm.RegisterCnjLoader<GameData>("EnemyDefinition",
        [](const std::string&, ContentManager&) { GameData d; d.kind = "Enemy"; return d; });

    EXPECT_THROW(cm.Load<GameData>("mystery"), ContentLoadException);
}

TEST_F(CnjCustomLoaderTest, MissingCnjVersionThrowsEvenWithRegisteredType)
{
    // GenericCnjTypeReader must enforce the same "cnjVersion" requirement CNB-2's
    // ValidateCnjEnvelope enforces for every other reader -- a .cnj with a recognized "type"
    // but no "cnjVersion" must still be rejected, not silently dispatched to the factory.
    ScratchContentRoot root;
    WriteFile(root.path() / "noversion.cnj", R"({"type": "EnemyDefinition"})");

    ContentManager cm(nullptr, root.path().string());
    bool factoryInvoked = false;
    cm.RegisterCnjLoader<GameData>("EnemyDefinition",
        [&factoryInvoked](const std::string&, ContentManager&)
        {
            factoryInvoked = true;
            GameData d;
            d.kind = "Enemy";
            return d;
        });

    EXPECT_THROW(cm.Load<GameData>("noversion"), ContentLoadException);
    EXPECT_FALSE(factoryInvoked);
}

// plan_cnj.md CNB-35: the strict envelope/version policy applies equally to the
// RegisterCnjLoader<T> dispatch path, not just built-in readers.
TEST_F(CnjCustomLoaderTest, UnsupportedCnjVersionThrowsEvenWithRegisteredType)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "future.cnj", R"({"cnjVersion": 2, "type": "EnemyDefinition"})");

    ContentManager cm(nullptr, root.path().string());
    bool factoryInvoked = false;
    cm.RegisterCnjLoader<GameData>("EnemyDefinition",
        [&factoryInvoked](const std::string&, ContentManager&)
        {
            factoryInvoked = true;
            GameData d;
            d.kind = "Enemy";
            return d;
        });

    EXPECT_THROW(cm.Load<GameData>("future"), ContentLoadException);
    EXPECT_FALSE(factoryInvoked);
}

TEST_F(CnjCustomLoaderGraphicsTest, FactoryCanRecursivelyLoadReferencedTexture)
{
    ScratchContentRoot root;

    Texture2D source(gd, 2, 2);
    std::vector<Color> pixels(4, Color(0, 255, 0, 255));
    source.SetData(pixels.data(), 4);
    source.SaveAsPng((root.path() / "icon.png").string());

    WriteFile(root.path() / "quest.cnj", R"({"cnjVersion": 1, "type": "Quest", "icon": "icon.png"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    cm.RegisterCnjLoader<GameData>("Quest",
        [](const std::string&, ContentManager& cmRef)
        {
            Texture2D icon = cmRef.Load<Texture2D>("icon");
            GameData d;
            d.kind = "Quest";
            d.iconWidth = icon.getWidthProperty();
            return d;
        });

    GameData quest = cm.Load<GameData>("quest");

    EXPECT_EQ(quest.kind, "Quest");
    EXPECT_EQ(quest.iconWidth, 2);
}

TEST_F(CnjCustomLoaderTest, RegisteringForAlreadyOwnedTypeThrowsLogicError)
{
    ContentManager cm(nullptr);

    EXPECT_THROW(
        cm.RegisterCnjLoader<Texture2D>(
            "Weird",
            [](const std::string&, ContentManager&) -> Texture2D { throw std::runtime_error("unreachable"); }),
        std::logic_error);
}

// plan_cnj.md CNB-37: deterministic, fail-fast registration guards.

TEST_F(CnjCustomLoaderTest, EmptyTypeNameThrowsInvalidArgument)
{
    ContentManager cm(nullptr);

    EXPECT_THROW(
        cm.RegisterCnjLoader<GameData>(
            "", [](const std::string&, ContentManager&) { return GameData{}; }),
        std::invalid_argument);
}

TEST_F(CnjCustomLoaderTest, EmptyFactoryThrowsInvalidArgument)
{
    ContentManager cm(nullptr);

    ContentManager::CnjLoaderFn<GameData> emptyFactory;
    EXPECT_FALSE(static_cast<bool>(emptyFactory));

    EXPECT_THROW(cm.RegisterCnjLoader<GameData>("Empty", emptyFactory), std::invalid_argument);
}

TEST_F(CnjCustomLoaderTest, DuplicateTypeNameForSameTThrowsLogicErrorNotSilentReplace)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "goblin.cnj", R"({"cnjVersion": 1, "type": "EnemyDefinition"})");

    ContentManager cm(nullptr, root.path().string());

    bool firstInvoked = false;
    bool secondInvoked = false;

    cm.RegisterCnjLoader<GameData>("EnemyDefinition",
        [&firstInvoked](const std::string&, ContentManager&)
        {
            firstInvoked = true;
            GameData d;
            d.kind = "First";
            return d;
        });

    EXPECT_THROW(
        cm.RegisterCnjLoader<GameData>("EnemyDefinition",
            [&secondInvoked](const std::string&, ContentManager&)
            {
                secondInvoked = true;
                GameData d;
                d.kind = "Second";
                return d;
            }),
        std::logic_error);

    // The first factory must still be the one actually registered -- not silently replaced.
    GameData loaded = cm.Load<GameData>("goblin");
    EXPECT_EQ(loaded.kind, "First");
    EXPECT_TRUE(firstInvoked);
    EXPECT_FALSE(secondInvoked);
}
