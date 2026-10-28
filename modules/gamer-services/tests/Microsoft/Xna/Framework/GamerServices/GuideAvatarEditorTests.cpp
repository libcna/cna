// SPDX-License-Identifier: MS-PL
// The avatar editor as a Guide screen, driven as a player would (categories, choices, save,
// leave) without a device.
#include <gtest/gtest.h>
#include "System/Environment.hpp"
#include <optional>
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "../../../../../src/Internal/Guide/GuideSystem.hpp"
#include "../../../../../src/Internal/Guide/GuideUi.hpp"
#include "../../../../../src/Internal/Guide/GuidePortraits.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarEditor.hpp"
#include "CNA/Internal/GamerServices/DispatcherGraphics.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "../../../../../../platform/tests/CNA/Platform/CannedGamepad.hpp"
#include "../../../../../../platform/tests/CNA/Platform/CannedMouse.hpp"
#include "../../../../../../platform/tests/CNA/Platform/PlatformTestDecorator.hpp"
#include "System/IServiceProvider.hpp"
#include <chrono>
#include <cstdio>
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

// ---- The editor driven by real devices: a keyboard, a controller and a mouse behind the platform.
namespace {
class EditorKeyboard final : public CNA::Platform::IPlatformKeyboard {
public:
    CNA::Platform::KeyboardSnapshot snapshot;
    void Update() override {}
    [[nodiscard]] const CNA::Platform::KeyboardSnapshot& GetSnapshot() const override { return snapshot; }
    [[nodiscard]] bool HasKeyboard() const override { return true; }
};
class EditorDevices final : public CNA::Platform::Testing::PlatformTestDecorator {
public:
    EditorKeyboard keyboard;
    CNA::Platform::Testing::CannedGamepad gamepad;
    CNA::Platform::Testing::CannedMouse mouse;
    [[nodiscard]] CNA::Platform::IPlatformKeyboard* GetKeyboard() override { return &keyboard; }
    [[nodiscard]] CNA::Platform::IPlatformGamepad* GetGamepad() override { return &gamepad; }
    [[nodiscard]] CNA::Platform::IPlatformMouse* GetMouse() override { return &mouse; }
};

class GuideAvatarEditorDeviceTest : public GuideAvatarEditorTest {
protected:
    // One Guide frame as a game draws it: the devices are read, the editor handles them and draws.
    void Frame() {
        Ui::draw(device_);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    void Keys(std::vector<CNA::Platform::KeyCode> held) {
        devices_.keyboard.snapshot.pressedKeys = std::move(held);
        Frame();
    }
    void Press(CNA::Platform::KeyCode key) { Keys({key}); Keys({}); }
    void Pad(std::uint32_t buttons, float rightStickX = 0.0f, float rightTrigger = 0.0f) {
        CNA::Platform::GamepadSnapshot snapshot;
        snapshot.connected = true;
        snapshot.buttons = buttons;
        snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::RightThumbstickX)] = rightStickX;
        snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::RightTrigger)] = rightTrigger;
        devices_.gamepad.SetPendingSnapshot(0, snapshot);
        devices_.gamepad.Update();
        Frame();
    }
    void Button(Microsoft::Xna::Framework::Input::Buttons button) { Pad(static_cast<std::uint32_t>(button)); Pad(0); }
    void Mouse(Microsoft::Xna::Framework::Vector2 at, bool down, int wheel = 0) {
        CNA::Platform::MouseSnapshot snapshot;
        snapshot.x = static_cast<int>(at.X);
        snapshot.y = static_cast<int>(at.Y);
        snapshot.buttons = down ? std::uint8_t{1} : std::uint8_t{0};
        snapshot.scrollY = wheel;
        devices_.mouse.SetPending(snapshot);
        devices_.mouse.Update();
        Frame();
    }
    void Click(Microsoft::Xna::Framework::Vector2 at) { Mouse(at, false); Mouse(at, true); Mouse(at, false); }
    static Microsoft::Xna::Framework::Vector2 Target(const std::string& name) {
        for (const auto& [target, centre] : Ui::targetsForTesting())
            if (target == name) return centre;
        ADD_FAILURE() << "no mouse target " << name;
        return {};
    }
    // The yaw and zoom of the editor's camera.
    static std::pair<float, float> View() {
        float yaw = 0, zoom = 0;
        EXPECT_EQ(2, std::sscanf(Ui::stateForTesting().c_str(), "yaw=%f zoom=%f", &yaw, &zoom)) << Ui::stateForTesting();
        return {yaw, zoom};
    }
    // The stored avatar's hairstyle as the Hair category's first row names it.
    std::string WornHair() {
        const auto stored = Stored();
        const auto descriptor = Avatars::decode(std::vector<std::uint8_t>(stored.begin(), stored.end()));
        const auto manifest = Avatars::embeddedManifest(descriptor->catalogVersion);
        return "Hairstyle: " + manifest->item(descriptor->items[0], Avatars::AvatarItemSlot::Hair)->label;
    }

    // The device the editor's avatars draw on: theirs once ready, which a warm model cache makes
    // happen within the test.
    struct DispatcherDevice {
        explicit DispatcherDevice(Microsoft::Xna::Framework::Graphics::GraphicsDevice* device) { Service::setDispatcherGraphicsDeviceForTesting(device); }
        ~DispatcherDevice() { Service::setDispatcherGraphicsDeviceForTesting(nullptr); }
    };
    EditorDevices devices_;
    CNA::Platform::Testing::ScopedCurrentPlatform scope_{devices_};
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device_;
    DispatcherDevice dispatcher_{&device_};
};
}

TEST_F(GuideAvatarEditorDeviceTest, AKeyboardReachesEveryPartOfTheEditor) {
    using CNA::Platform::KeyCode;
    ASSERT_TRUE(Open());
    Keys({});
    for (int k = 0; k < 8; ++k) Press(KeyCode::Down);
    ASSERT_EQ(8, Ui::focusForTesting());
    // Enter opens Hair; Right tries on another style, Enter keeps it.
    Press(KeyCode::Enter);
    const auto worn = WornHair();
    ASSERT_EQ(worn, Label(0));
    Press(KeyCode::Right);
    if (Label(0).find("(trying ") == std::string::npos) Press(KeyCode::Left);
    ASSERT_NE(Label(0).find("(trying "), std::string::npos) << Label(0);
    Press(KeyCode::Enter);
    EXPECT_NE(worn, Label(0));
    // Up from the first row is the category's Reset.
    Press(KeyCode::Up);
    ASSERT_EQ("Reset Hair", Label(0));
    Press(KeyCode::Enter);
    Press(KeyCode::Down);
    EXPECT_EQ(worn, Label(0));
    // C turns the avatar and + zooms in while held.
    const auto [yaw, zoom] = View();
    for (int frame = 0; frame < 12; ++frame) Keys({KeyCode::C, KeyCode::OemPlus});
    Keys({});
    EXPECT_GT(View().first, yaw);
    EXPECT_LT(View().second, zoom);
    // Escape back to the categories; X shuffles the category shown (its style and colour); Y
    // randomizes; Escape asks to save.
    const auto hair = Ui::labelsForTesting();
    Press(KeyCode::Escape);
    ASSERT_EQ(CategoryNames(), Ui::labelsForTesting());
    Press(KeyCode::X);
    Press(KeyCode::Enter);
    EXPECT_NE(hair, Ui::labelsForTesting()) << "X shuffles the hair";
    Press(KeyCode::Escape);
    Press(KeyCode::Y);
    Press(KeyCode::Escape);
    EXPECT_EQ("avatarEditorLeave", Ui::currentScreenForTesting());
}

TEST_F(GuideAvatarEditorDeviceTest, AControllerReachesEveryPartOfTheEditor) {
    using Microsoft::Xna::Framework::Input::Buttons;
    ASSERT_TRUE(Open());
    Pad(0);
    for (int k = 0; k < 8; ++k) Button(Buttons::DPadDown);
    ASSERT_EQ(8, Ui::focusForTesting());
    Button(Buttons::A);
    const auto worn = WornHair();
    ASSERT_EQ(worn, Label(0));
    Button(Buttons::DPadRight);
    if (Label(0).find("(trying ") == std::string::npos) Button(Buttons::DPadLeft);
    ASSERT_NE(Label(0).find("(trying "), std::string::npos) << Label(0);
    Button(Buttons::A);
    EXPECT_NE(worn, Label(0));
    Button(Buttons::DPadUp);
    ASSERT_EQ("Reset Hair", Label(0));
    Button(Buttons::A);
    Button(Buttons::DPadDown);
    EXPECT_EQ(worn, Label(0));
    // The right stick turns the avatar, the right trigger zooms in.
    const auto [yaw, zoom] = View();
    for (int frame = 0; frame < 12; ++frame) Pad(0, 1.0f, 1.0f);
    Pad(0);
    EXPECT_GT(View().first, yaw);
    EXPECT_LT(View().second, zoom);
    // RB turns the category page; B goes back; Y randomizes; B asks to save.
    Button(Buttons::B);
    ASSERT_EQ(CategoryNames(), Ui::labelsForTesting());
    Button(Buttons::RightShoulder);
    EXPECT_EQ(9, Ui::focusForTesting());
    Button(Buttons::Y);
    Button(Buttons::B);
    EXPECT_EQ("avatarEditorLeave", Ui::currentScreenForTesting());
}

TEST_F(GuideAvatarEditorDeviceTest, AMouseReachesEveryPartOfTheEditor) {
    ASSERT_TRUE(Open());
    Mouse({1, 1}, false);
    // A category on the rail, then cards of its first row.
    Click(Target("category 8"));
    ASSERT_EQ(8, Ui::focusForTesting());
    Frame();
    // Two cards of the style strip (it shows the worn style's neighbourhood).
    std::vector<std::string> cards;
    for (const auto& [name, centre] : Ui::targetsForTesting())
        if (name.starts_with("option 0 ")) cards.push_back(name);
    ASSERT_GE(cards.size(), 2u);
    Click(Target(cards[0]));
    const auto first = Label(0);
    ASSERT_TRUE(first.starts_with("Hairstyle: ")) << first;
    Frame();
    Click(Target(cards[1]));
    EXPECT_NE(first, Label(0));
    // The category's Reset button.
    Frame();
    Click(Target("reset"));
    EXPECT_EQ(WornHair(), Label(0));
    // A slider, clicked left of its middle.
    Frame();
    Click(Target("category 0"));
    Frame();
    std::string slider;
    for (const auto& [name, centre] : Ui::targetsForTesting())
        if (name.starts_with("slider ") && slider.empty()) slider = name;
    ASSERT_FALSE(slider.empty());
    const auto row = static_cast<std::size_t>(std::stoi(slider.substr(7)));
    const auto at = Target(slider);
    Click({at.X + 30.0f, at.Y});
    const auto right = Label(row);
    Frame();
    Click({at.X - 60.0f, at.Y});
    EXPECT_NE(right, Label(row));
    // The stage: the wheel zooms, a drag turns the avatar.
    const auto stage = Target("stage");
    const auto [yaw, zoom] = View();
    Mouse(stage, false, 120);
    EXPECT_LT(View().second, zoom);
    Mouse(stage, true, 120);
    Mouse({stage.X + 80.0f, stage.Y}, true, 120);
    Mouse({stage.X + 80.0f, stage.Y}, false, 120);
    EXPECT_NE(View().first, yaw);
}

// Avatar acceptance AVA-02: the stage and the portraits are drawn into render targets.
// A renderer without them (STUB) used to throw out of the Guide's draw once an avatar was ready,
// which only the device tests whose avatar happened to finish loading in time reached. These wait
// for the avatar, so every renderer reaches the draw: one with render targets draws, one without
// shows the editor without the picture.
TEST_F(GuideAvatarEditorDeviceTest, TheStageIsDrawnOrLeftOutOnEveryRenderer) {
    ASSERT_TRUE(Open());
    const auto stage = [] { const auto state = Ui::stateForTesting(); return state.substr(state.find("stage=") + 6); };
    for (int frame = 0; frame < 2500 && stage() == "none"; ++frame) ASSERT_NO_THROW(Keys({}));
    for (int frame = 0; frame < 5; ++frame) ASSERT_NO_THROW(Keys({}));
    Microsoft::Xna::Framework::Graphics::RenderTarget2D probe(device_, 4, 4);
    EXPECT_EQ(probe.GetRenderTargetRenderer() ? "drawn" : "refused", stage());
}

TEST_F(GuideAvatarEditorDeviceTest, PortraitsAreDrawnOrLeftOutOnEveryRenderer) {
    auto portraits = Ui::makeAvatarPortraits();
    const auto avatar = Avatar(1, 11);
    for (int frame = 0; frame < 2500; ++frame) {
        (void)portraits->request(avatar, Ui::Framing::Head, 64, 64, false);
        ASSERT_NO_THROW(portraits->render(device_, 0.0));
        if (portraits->waiting() == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    ASSERT_EQ(0, portraits->waiting()) << "the portrait's avatar never became ready";
    Microsoft::Xna::Framework::Graphics::RenderTarget2D probe(device_, 4, 4);
    EXPECT_EQ(probe.GetRenderTargetRenderer() != nullptr, portraits->request(avatar, Ui::Framing::Head, 64, 64, false) != nullptr);
}

TEST(GuideAvatarEditorFramingTest, TheHandsViewFramesTheStandingHand) {
    // The Gloves, Wristwear and Rings views aim at the right hand as the avatar stands (2026-10-15:
    // they aimed at the bind pose's wrist, wider and higher than a hanging hand, which left the hand
    // at the edge of the stage). Framed as the editor's stage frames it -- its usable band between
    // the header and the footer, the avatar turned to face the camera at the editor's starting
    // turn -- the standing avatar's hand, wrist to fingertip, lies well inside that band.
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Quaternion;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Vector4;
    const auto& library = Avatars::clipLibrary();
    const auto& parents = Avatars::parentBones();
    const auto stand = std::ranges::find(library.clips, std::string("Stand0"), &Avatars::AvatarClip::name);
    ASSERT_NE(stand, library.clips.end());
    constexpr float StartingTurn = 3.44159265f;  // GuideAvatarEditor's yaw_
    constexpr float UsableTop = 0.17f, UsableBottom = 0.91f, Aspect = 0.89f;
    for (std::uint8_t body = 0; body < 2; ++body) {
        Avatars::AvatarDescriptor descriptor;
        descriptor.catalogVersion = Avatars::BaseCatalogVersion;
        descriptor.bodyType = body;
        descriptor.heightMillimeters = body ? 1800 : 1680;
        descriptor.items = {1, 20, 40, 60, 0, 0};
        const auto model = Avatars::buildAvatarModel(descriptor);
        // The catalog's joints, and the same in XNA coordinates (AvatarRenderer.BindPose).
        std::vector<Vector3> joints(Avatars::BoneCount), xnaJoints(Avatars::BoneCount);
        for (int bone = 0; bone < Avatars::BoneCount; ++bone) {
            joints[bone] = (parents[bone] < 0 ? Vector3::Zero : joints[static_cast<std::size_t>(parents[bone])]) +
                           model->bindTranslations[bone];
            xnaJoints[bone] = Vector3(-joints[bone].X, joints[bone].Y, -joints[bone].Z);
        }
        std::array<Quaternion, Avatars::BoneCount> rotations;
        Vector3 root;
        Avatars::sampleClip(*stand, 0.0, rotations, root);
        std::array<Matrix, Avatars::BoneCount> world;
        for (int bone = 0; bone < Avatars::BoneCount; ++bone) {
            const Matrix local = Matrix::CreateFromQuaternion(rotations[bone]) *
                                 Matrix::CreateTranslation(bone == 0 ? root : model->bindTranslations[bone]);
            world[bone] = parents[bone] < 0 ? local : local * world[parents[bone]];
        }
        // GuideAvatarEditor::render's camera for a view.
        const float usable = UsableBottom - UsableTop;
        auto shot = Ui::shotFor(Ui::Framing::Hands, xnaJoints, model->height, Aspect * usable);
        shot.offset.X = 0.0f;
        shot.offset = shot.offset / usable;
        const float perUnit = 2.0f * shot.offset.Length() * std::tan(shot.fov / 2.0f);
        shot.target.Y += ((UsableTop + UsableBottom) / 2.0f - 0.5f) * perUnit;
        const Matrix view = Matrix::CreateLookAt(shot.target + shot.offset, shot.target, Vector3::Up);
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(shot.fov, Aspect, 0.05f, 30.0f);
        // Catalog space to XNA space, then the editor's turn.
        const Matrix placement = Matrix::CreateScale(-1.0f, 1.0f, -1.0f) * Matrix::CreateRotationY(StartingTurn);
        for (const char* bone : {"WristRight", "FingerMiddle3Right"}) {
            const auto p = Vector3::Transform(world[static_cast<std::size_t>(Avatars::boneIndex(bone))].getTranslationProperty(),
                                              placement);
            const auto clip = Vector4::Transform(p, view * projection);
            // Screen position, 0 at the top of the stage and 1 at the bottom.
            const float x = clip.X / clip.W, y = 0.5f - 0.5f * clip.Y / clip.W;
            EXPECT_LT(std::fabs(x), 0.6f) << bone << " body " << int(body);
            EXPECT_GT(y, UsableTop + 0.05f) << bone << " body " << int(body);
            EXPECT_LT(y, UsableBottom - 0.05f) << bone << " body " << int(body);
        }
    }
}
