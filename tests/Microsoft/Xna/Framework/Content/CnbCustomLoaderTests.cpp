// SPDX-License-Identifier: MS-PL
//
// plan_cnb.md CNB-26/CNB-27: tests for RegisterCnbLoader<T> (CNB-24/CNB-25) -- a game-registered,
// .cnb "type"-string-keyed loader table, for asset types with no dedicated ContentTypeReader<T>.
// Matches cnb.md's "Custom loaders" section worked example ("EnemyDefinition"/"LootTable" both
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
                   / ("cna_cnb_custom_loader_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    // A game-defined type with no CNA-provided reader -- exactly cnb.md's "Custom loaders"
    // example: several distinct .cnb "type" strings can all deserialize into this one struct.
    struct GameData
    {
        std::string kind;
        int iconWidth = 0;
    };
}

class CnbCustomLoaderTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnbCustomLoaderTest, TwoDifferentTypeNamesProduceSameTViaDifferentFactories)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "goblin.cnb", R"({"cnbVersion": 1, "type": "EnemyDefinition"})");
    WriteFile(root.path() / "chest.cnb", R"({"cnbVersion": 1, "type": "LootTable"})");

    ContentManager cm(nullptr, root.path().string());

    cm.RegisterCnbLoader<GameData>("EnemyDefinition",
        [](const std::string&, ContentManager&) { GameData d; d.kind = "Enemy"; return d; });
    cm.RegisterCnbLoader<GameData>("LootTable",
        [](const std::string&, ContentManager&) { GameData d; d.kind = "Loot"; return d; });

    GameData enemy = cm.Load<GameData>("goblin");
    GameData loot = cm.Load<GameData>("chest");

    EXPECT_EQ(enemy.kind, "Enemy");
    EXPECT_EQ(loot.kind, "Loot");
}

TEST_F(CnbCustomLoaderTest, UnregisteredTypeNameThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "mystery.cnb", R"({"cnbVersion": 1, "type": "Unknown"})");

    ContentManager cm(nullptr, root.path().string());
    cm.RegisterCnbLoader<GameData>("EnemyDefinition",
        [](const std::string&, ContentManager&) { GameData d; d.kind = "Enemy"; return d; });

    EXPECT_THROW(cm.Load<GameData>("mystery"), ContentLoadException);
}

TEST_F(CnbCustomLoaderTest, MissingCnbVersionThrowsEvenWithRegisteredType)
{
    // GenericCnbTypeReader must enforce the same "cnbVersion" requirement CNB-2's
    // ValidateCnbEnvelope enforces for every other reader -- a .cnb with a recognized "type"
    // but no "cnbVersion" must still be rejected, not silently dispatched to the factory.
    ScratchContentRoot root;
    WriteFile(root.path() / "noversion.cnb", R"({"type": "EnemyDefinition"})");

    ContentManager cm(nullptr, root.path().string());
    bool factoryInvoked = false;
    cm.RegisterCnbLoader<GameData>("EnemyDefinition",
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

TEST_F(CnbCustomLoaderTest, FactoryCanRecursivelyLoadReferencedTexture)
{
    ScratchContentRoot root;

    Texture2D source(gd, 2, 2);
    std::vector<Color> pixels(4, Color(0, 255, 0, 255));
    source.SetData(pixels.data(), 4);
    source.SaveAsPng((root.path() / "icon.png").string());

    WriteFile(root.path() / "quest.cnb", R"({"cnbVersion": 1, "type": "Quest", "icon": "icon.png"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    cm.RegisterCnbLoader<GameData>("Quest",
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

TEST_F(CnbCustomLoaderTest, RegisteringForAlreadyOwnedTypeThrowsLogicError)
{
    ContentManager cm(nullptr);

    EXPECT_THROW(
        cm.RegisterCnbLoader<Texture2D>(
            "Weird",
            [](const std::string&, ContentManager&) -> Texture2D { throw std::runtime_error("unreachable"); }),
        std::logic_error);
}
