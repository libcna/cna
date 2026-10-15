// SPDX-License-Identifier: MS-PL
// The avatar editor as a Guide screen, driven as a player would (categories, choices, save,
// leave) without a device.
#include <gtest/gtest.h>
#include "System/Environment.hpp"
#include <optional>
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "../../../../../src/Internal/Guide/GuideSystem.hpp"
#include "../../../../../src/Internal/Guide/GuideUi.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarEditor.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/IServiceProvider.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <thread>
#include <unistd.h>

namespace Service = CNA::Internal::GamerServices;
namespace Avatars = CNA::Internal::GamerServices::Avatars;
namespace Ui = CNA::Internal::GamerServices::GuideUi;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::PlayerIndex;

namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};

std::string CatalogText(int version) {
    const auto bytes = Avatars::embeddedCatalogFile("v" + std::to_string(version) + "/catalog.json");
    return {bytes.begin(), bytes.end()};
}

std::vector<unsigned char> Avatar(std::uint8_t body, std::uint32_t seed) {
    std::mt19937 random(seed);
    auto descriptor = Avatars::randomDescriptor(*Avatars::embeddedCatalogs().back(), body, random);
    const auto bytes = Avatars::encode(descriptor);
    return {bytes.begin(), bytes.end()};
}

std::vector<std::string> CategoryNames() {
    return {"Body", "Skin", "Face", "Eyes", "Eyebrows", "Ears", "Nose", "Mouth", "Hair", "Facial hair", "Makeup", "Outfits", "Tops",
            "Bottoms", "Shoes", "Headwear", "Glasses", "Earrings", "Wristwear", "Gloves", "Rings"};
}

class GuideAvatarEditorTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Service::backend();
        profiles_ = std::filesystem::temp_directory_path() / ("cna-editor-profiles-" + std::to_string(System::Environment::getProcessIdProperty()));
        std::filesystem::remove_all(profiles_);
        const auto* current = std::getenv("CNA_GAMER_SERVICES_PROFILES_DIR");
        savedProfiles_ = current ? std::optional<std::string>(current) : std::nullopt;
        System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_PROFILES_DIR", profiles_.string());
        Service::ServiceIdentity alice;
        alice.userId = "a";
        alice.gamertag = "Alice";
        alice.allowOnlineSessions = true;
        alice.avatar = Avatar(0, 3);
        service_ = Service::makeFakeBackend({alice});
        Service::setFakeAvatarCatalog(*service_, CatalogText(Avatars::embeddedCatalogs().back()->version), {});
        Service::setBackendForTesting(service_);
        if (!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider_);
        service_->signIn(0, "Alice", "fixture");
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 1; });
    }
    void TearDown() override {
        Ui::closeAll();
        Service::setFakeAvatarsUnreachable(*service_, false);
        service_->signOut(0);
        service_->signOut(1);
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 0; });
        Service::setBackendForTesting(previous_);
        if (savedProfiles_) System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_PROFILES_DIR", *savedProfiles_);
        else System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_PROFILES_DIR", std::nullopt);
        std::filesystem::remove_all(profiles_);
    }
    template <typename Condition>
    static bool Settle(Condition condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!condition()) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            GamerServicesDispatcher::Update();
            Ui::frameForTesting();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    // Opens the editor for Alice and waits for it to finish loading.
    bool Open(PlayerIndex player = PlayerIndex::One, Ui::AvatarEditorOptions options = {}) {
        if (!options.seed) options.seed = 5;
        Ui::open(Ui::avatarEditorScreen(player, std::move(options)), player);
        return Settle([] { return Ui::labelsForTesting() != std::vector<std::string>{"Loading"}; });
    }
    std::vector<unsigned char> Stored() { return service_->avatars({"a"}).front().description; }
    static std::string Label(std::size_t index) {
        const auto labels = Ui::labelsForTesting();
        return index < labels.size() ? labels[index] : std::string();
    }
    static void Category(int index) {
        while (Ui::focusForTesting() != index) Ui::sendForTesting(Ui::Command::Down);
    }

    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_, service_;
    std::filesystem::path profiles_;
    std::optional<std::string> savedProfiles_;
};
}

TEST_F(GuideAvatarEditorTest, OpensFromHomeOnTheCategories) {
    Service::openSystemGuide(PlayerIndex::One);
    const auto home = Ui::labelsForTesting();
    const auto edit = std::find(home.begin(), home.end(), "Edit avatar");
    ASSERT_NE(edit, home.end());
    Ui::clickForTesting(static_cast<int>(edit - home.begin()));
    ASSERT_EQ("avatarEditor", Ui::currentScreenForTesting());
    ASSERT_TRUE(Settle([] { return Ui::labelsForTesting() == CategoryNames(); }));
    EXPECT_EQ(0, Ui::focusForTesting());
    // Nothing changed: Back leaves at once, to Home.
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_EQ("home", Ui::currentScreenForTesting());
}

TEST_F(GuideAvatarEditorTest, CategoriesWrapAndChoicesAreTriedOnBeforeTheyAreKept) {
    ASSERT_TRUE(Open());
    Ui::sendForTesting(Ui::Command::Up);
    EXPECT_EQ(20, Ui::focusForTesting());
    Ui::sendForTesting(Ui::Command::Next);
    EXPECT_EQ(0, Ui::focusForTesting());
    Category(8);
    Ui::sendForTesting(Ui::Command::Accept);
    // Hair: the style row is focused on the style worn now.
    ASSERT_EQ(2u, Ui::labelsForTesting().size());
    const auto worn = Label(0);
    EXPECT_TRUE(worn.starts_with("Hairstyle: ")) << worn;
    EXPECT_EQ(0, Ui::focusForTesting());
    // The next style (the previous one when the worn style is the last).
    Ui::sendForTesting(Ui::Command::Right);
    const bool last = Label(0).find("(trying ") == std::string::npos;
    if (last) Ui::sendForTesting(Ui::Command::Left);
    ASSERT_NE(Label(0).find("(trying "), std::string::npos) << Label(0);
    // Leaving the row drops what was only tried.
    Ui::sendForTesting(Ui::Command::Down);
    EXPECT_EQ(worn, Label(0));
    Ui::sendForTesting(Ui::Command::Up);
    Ui::sendForTesting(last ? Ui::Command::Left : Ui::Command::Right);
    const auto tried = Label(0).substr(Label(0).find("(trying ") + 8);
    Ui::sendForTesting(Ui::Command::Accept);
    EXPECT_EQ("Hairstyle: " + tried.substr(0, tried.size() - 1), Label(0));
    // Back returns to the categories, the choice kept.
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_EQ(CategoryNames(), Ui::labelsForTesting());
    EXPECT_EQ("avatarEditor", Ui::currentScreenForTesting());
}

TEST_F(GuideAvatarEditorTest, SlidersMoveWithLeftAndRight) {
    ASSERT_TRUE(Open());
    Ui::sendForTesting(Ui::Command::Accept);
    Ui::sendForTesting(Ui::Command::Down);
    Ui::sendForTesting(Ui::Command::Down);
    ASSERT_TRUE(Label(2).starts_with("Height: ")) << Label(2);
    // Alice's stored height is drawn by std::uniform_int_distribution, whose output for a seed the
    // standard library decides: libstdc++ draws 1691 mm here, libc++ 1676 mm. Off the slider's
    // 10 mm grid a first step only snaps onto it, and from 1676 mm that leaves the label at
    // "1.68 m", so the test starts from the grid.
    Ui::sendForTesting(Ui::Command::Right);
    const auto before = Label(2);
    Ui::sendForTesting(Ui::Command::Right);
    EXPECT_NE(before, Label(2));
    Ui::sendForTesting(Ui::Command::Left);
    EXPECT_EQ(before, Label(2));
}

TEST_F(GuideAvatarEditorTest, ResetAboveTheFirstRowPutsTheCategoryBack) {
    ASSERT_TRUE(Open());
    Category(8);
    Ui::sendForTesting(Ui::Command::Accept);
    const auto worn = Label(0);
    // Try on and keep the next hairstyle, then reset the category from the button above the rows.
    Ui::sendForTesting(Ui::Command::Right);
    Ui::sendForTesting(Ui::Command::Accept);
    ASSERT_NE(worn, Label(0));
    Ui::sendForTesting(Ui::Command::Up);
    EXPECT_EQ("Reset Hair", Label(0));
    Ui::sendForTesting(Ui::Command::Accept);
    Ui::sendForTesting(Ui::Command::Down);
    EXPECT_EQ(worn, Label(0));
}

TEST_F(GuideAvatarEditorTest, EveryCategoryOpensAndEveryRowCanBeFocused) {
    // Controller navigation reaches every row of every category, and none is a dead end.
    ASSERT_TRUE(Open());
    for (int category = 0; category < Avatars::EditorCategoryCount; ++category) {
        Category(category);
        Ui::sendForTesting(Ui::Command::Accept);
        const auto rows = Ui::labelsForTesting().size();
        ASSERT_GT(rows, 0u) << category;
        for (std::size_t row = 1; row < rows; ++row) {
            Ui::sendForTesting(Ui::Command::Down);
            EXPECT_EQ(static_cast<int>(row), Ui::focusForTesting()) << category;
        }
        // Back to the categories with the focus where it was.
        Ui::sendForTesting(Ui::Command::Back);
        EXPECT_EQ(category, Ui::focusForTesting());
        EXPECT_EQ(CategoryNames(), Ui::labelsForTesting());
    }
}

TEST_F(GuideAvatarEditorTest, SavingStoresTheAvatarOnTheServiceAndLeaves) {
    const auto before = Stored();
    bool closed = false, saved = false;
    ASSERT_TRUE(Open(PlayerIndex::One, {0, [&](bool wasSaved) { closed = true; saved = wasSaved; }}));
    Ui::sendForTesting(Ui::Command::Y);
    Ui::sendForTesting(Ui::Command::Back);
    ASSERT_EQ("avatarEditorLeave", Ui::currentScreenForTesting());
    EXPECT_EQ((std::vector<std::string>{"Save", "Discard changes", "Keep editing"}), Ui::labelsForTesting());
    Ui::clickForTesting(0);
    ASSERT_TRUE(Settle([&] { return closed; }));
    EXPECT_TRUE(saved);
    EXPECT_FALSE(Ui::visible());
    const auto after = Stored();
    EXPECT_NE(before, after);
    ASSERT_TRUE(Avatars::decode(std::vector<std::uint8_t>(after.begin(), after.end())).has_value());
    EXPECT_EQ(after, Ui::identity(PlayerIndex::One).avatar);
}

TEST_F(GuideAvatarEditorTest, DiscardingAndKeepingEditingLeaveTheStoredAvatarAlone) {
    const auto before = Stored();
    ASSERT_TRUE(Open());
    Ui::sendForTesting(Ui::Command::X);
    Ui::sendForTesting(Ui::Command::Back);
    ASSERT_EQ("avatarEditorLeave", Ui::currentScreenForTesting());
    // Back on the question keeps editing.
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_EQ("avatarEditor", Ui::currentScreenForTesting());
    Ui::sendForTesting(Ui::Command::Back);
    Ui::clickForTesting(2);
    EXPECT_EQ("avatarEditor", Ui::currentScreenForTesting());
    Ui::sendForTesting(Ui::Command::Back);
    Ui::clickForTesting(1);
    EXPECT_FALSE(Ui::visible());
    for (int i = 0; i < 20; ++i) GamerServicesDispatcher::Update();
    EXPECT_EQ(before, Stored());
}

TEST_F(GuideAvatarEditorTest, AFailedSaveSaysWhyAndKeepsTheEdits) {
    ASSERT_TRUE(Open());
    Ui::sendForTesting(Ui::Command::Y);
    Service::setFakeAvatarsUnreachable(*service_, true);
    Ui::sendForTesting(Ui::Command::Back);
    Ui::clickForTesting(0);
    ASSERT_TRUE(Settle([] { return Ui::currentScreenForTesting() == "info"; }));
    ASSERT_EQ(1u, Ui::labelsForTesting().size());
    EXPECT_NE(Ui::labelsForTesting()[0].find("could not be reached"), std::string::npos);
    Ui::sendForTesting(Ui::Command::Accept);
    EXPECT_EQ("avatarEditor", Ui::currentScreenForTesting());
    // Still unsaved: leaving asks again, and the save goes through once the service is back.
    Service::setFakeAvatarsUnreachable(*service_, false);
    Ui::sendForTesting(Ui::Command::Back);
    ASSERT_EQ("avatarEditorLeave", Ui::currentScreenForTesting());
    Ui::clickForTesting(0);
    ASSERT_TRUE(Settle([] { return !Ui::visible(); }));
}

TEST_F(GuideAvatarEditorTest, AnAvatarChangedElsewhereWhileOpenIsShown) {
    Service::setFakeAvatar(*service_, "a", Avatar(0, 9));
    Ui::AvatarEditorOptions options;
    options.refreshSeconds = 0;
    ASSERT_TRUE(Open(PlayerIndex::One, std::move(options)));
    Ui::sendForTesting(Ui::Command::Accept);
    ASSERT_EQ("Body type: Female", Label(0));
    // Another computer saves a masculine avatar: an editor without changes shows it.
    Service::setFakeAvatar(*service_, "a", Avatar(1, 9));
    EXPECT_TRUE(Settle([] { return Label(0) == "Body type: Male"; }));
}

TEST_F(GuideAvatarEditorTest, ALocalProfileSavesToThisComputer) {
    (void)Service::openLocalProfile("Robin");
    service_->signInLocal(1, "Robin");
    ASSERT_TRUE(Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 2; }));
    const auto before = Service::localProfileAvatar("Robin");
    ASSERT_TRUE(Open(PlayerIndex::Two));
    EXPECT_EQ(CategoryNames(), Ui::labelsForTesting());
    Ui::sendForTesting(Ui::Command::Y);
    Ui::sendForTesting(Ui::Command::Back);
    Ui::clickForTesting(0);
    EXPECT_FALSE(Ui::visible());
    const auto after = Service::localProfileAvatar("Robin");
    EXPECT_NE(before, after);
    EXPECT_TRUE(Avatars::decode(std::vector<std::uint8_t>(after.begin(), after.end())).has_value());
}

TEST_F(GuideAvatarEditorTest, WithoutAProfileOrACatalogItExplains) {
    ASSERT_TRUE(Open(PlayerIndex::Three));
    EXPECT_EQ(std::vector<std::string>{"Nobody is signed in"}, Ui::labelsForTesting());
    Ui::sendForTesting(Ui::Command::Accept);
    EXPECT_FALSE(Ui::visible());

    // The service names a catalog this computer lacks and may not install.
    auto text = CatalogText(Avatars::embeddedCatalogs().back()->version);
    const auto at = text.find("\"catalogVersion\"");
    ASSERT_NE(at, std::string::npos);
    const auto end = text.find_first_of(",}", at);
    text.replace(at, end - at, "\"catalogVersion\": 99");
    Service::setFakeAvatarCatalog(*service_, text, {});
    Service::setFakeAvatarCatalogPolicy(*service_, {false, 0});
    ASSERT_TRUE(Open());
    EXPECT_EQ(std::vector<std::string>{"Avatar catalog 99 unavailable"}, Ui::labelsForTesting());
}

TEST_F(GuideAvatarEditorTest, AServiceWhoseNewestCatalogIsADevelopmentCatalogIsNotEditedAgainstIt) {
    // A development service still offering catalog 2 as its newest: the editor neither edits
    // against it nor tries to install it, and explains that the avatar is not available.
    auto text = CatalogText(Avatars::BaseCatalogVersion);
    const auto at = text.find("\"catalogVersion\"");
    ASSERT_NE(at, std::string::npos);
    const auto end = text.find_first_of(",}", at);
    text.replace(at, end - at, "\"catalogVersion\": 2");
    Service::setFakeAvatarCatalog(*service_, text, {});
    ASSERT_TRUE(Open());
    EXPECT_EQ(std::vector<std::string>{"Avatar not available"}, Ui::labelsForTesting());
    EXPECT_EQ(Service::fakeAvatarTraffic(*service_).packReads, 0);
}
