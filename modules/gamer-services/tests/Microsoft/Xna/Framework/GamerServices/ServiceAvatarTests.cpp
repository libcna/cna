// SPDX-License-Identifier: MS-PL
// Named Service* like the other suites that initialize the process-wide dispatcher, so it runs
// after GamerServicesDispatcherTest.IsInitializedDefaultsFalse.
#include <gtest/gtest.h>

#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/IServiceProvider.hpp"

#include <chrono>
#include <future>
#include <nlohmann/json.hpp>
#include <thread>

namespace Service = CNA::Internal::GamerServices;
namespace Avatars = CNA::Internal::GamerServices::Avatars;
using namespace Microsoft::Xna::Framework::GamerServices;

namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};

std::vector<unsigned char> Encoded(std::uint8_t body, std::uint16_t height, std::uint16_t catalog = Avatars::BaseCatalogVersion,
                                   std::uint16_t hat = 0) {
    Avatars::AvatarDescriptor descriptor;
    descriptor.bodyType = body;
    descriptor.heightMillimeters = height;
    descriptor.catalogVersion = catalog;
    descriptor.items = {1, 20, 40, 60, 0, hat};
    const auto bytes = Avatars::encode(descriptor);
    return {bytes.begin(), bytes.end()};
}

// A catalog one version past everything compiled in: v1 plus one hat the library does not have.
const std::uint16_t Newer = static_cast<std::uint16_t>(Avatars::newestEmbeddedManifest().version + 1);

struct NewerCatalog {
    std::string manifest;
    std::map<std::string, std::vector<unsigned char>> assets;
};

NewerCatalog MakeNewerCatalog() {
    const auto embedded = Avatars::embeddedManifest(Avatars::BaseCatalogVersion);
    const auto& files = Avatars::embeddedCatalogFiles();
    auto manifestFile = std::find_if(files.begin(), files.end(), [](const auto& file) { return std::string_view(file.name) == "v1/catalog.json"; });
    auto json = nlohmann::json::parse(std::string(reinterpret_cast<const char*>(manifestFile->data), manifestFile->size));
    json["catalogVersion"] = Newer;
    NewerCatalog catalog;
    for (const char* body : {"female", "male"}) {
        // A distinct file: the cap renamed inside its JSON chunk, so its contents and hash are new.
        const auto base = Avatars::resolveAsset(*embedded, std::string("hat_cap.") + body + ".glb");
        std::string text(base->view.begin(), base->view.end());
        for (auto at = text.find("hat_cap"); at != std::string::npos; at = text.find("hat_cap", at)) {
            text.replace(at, 7, "hat_crn");
        }
        std::vector<unsigned char> bytes(text.begin(), text.end());
        const auto name = std::string("hat_crown.") + body + ".glb";
        const auto hash = Avatars::sha256Hex(bytes);
        json["assets"].push_back({{"name", name}, {"sha256", hash}, {"size", bytes.size()}});
        catalog.assets[hash] = bytes;
    }
    json["items"].push_back({{"id", 102}, {"slot", "hat"}, {"name", "hat_crown"},
                             {"assets", {{"female", "hat_crown.female.glb"}, {"male", "hat_crown.male.glb"}}}});
    catalog.manifest = json.dump();
    return catalog;
}

// Service work runs on the backend executor, which the dispatcher pumps; the avatar resolver
// waits for it from a background thread, as the renderer's loader thread does in a game.
template <typename F>
auto WhilePumping(F work) {
    auto future = std::async(std::launch::async, std::move(work));
    while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
        GamerServicesDispatcher::Update();
    }
    return future.get();
}

class AvatarServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Service::backend();
        std::vector<Service::ServiceIdentity> identities;
        for (auto [id, tag] : {std::pair{"a", "Alice"}, {"b", "Bob"}}) {
            Service::ServiceIdentity identity;
            identity.userId = id;
            identity.gamertag = tag;
            identities.push_back(identity);
        }
        identities[0].avatar = Encoded(1, 1830);
        service_ = Service::makeFakeBackend(std::move(identities));
        Service::setBackendForTesting(service_);
        if (!GamerServicesDispatcher::getIsInitializedProperty()) {
            GamerServicesDispatcher::Initialize(provider_);
        }
        service_->signIn(0, "Alice", "fixture");
        service_->signIn(1, "Bob", "fixture");
        GamerServicesDispatcher::Update();
        ASSERT_EQ(2, Gamer::getSignedInGamersProperty()->getCountProperty());
    }
    void TearDown() override {
        service_->signOut(0);
        service_->signOut(1);
        GamerServicesDispatcher::Update();
        Service::setBackendForTesting(previous_);
    }
    SignedInGamer* gamer(int slot) { return (*Gamer::getSignedInGamersProperty())[slot]; }

    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_, service_;
};
}

TEST_F(AvatarServiceTest, ASignedInGamersAvatarComesFromTheService) {
    int calls = 0;
    System::IAsyncResult* seen = nullptr;
    auto* result = AvatarDescription::BeginGetFromGamer(gamer(0), [&](System::IAsyncResult& done) {
        ++calls;
        seen = &done;
    }, std::any(7));
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->getCompletedSynchronouslyProperty());
    EXPECT_EQ(std::any_cast<int>(result->getAsyncStateProperty()), 7);
    const auto description = AvatarDescription::EndGetFromGamer(result);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen, result);
    ASSERT_TRUE(description.getIsValidProperty());
    EXPECT_EQ(description.getBodyTypeProperty(), AvatarBodyType::Male);
    EXPECT_FLOAT_EQ(description.getHeightProperty(), 1.83f);
    const auto expected = Encoded(1, 1830);
    EXPECT_EQ(description.getDescriptionProperty(), std::vector<SharpRuntime::bytecs>(expected.begin(), expected.end()));
    EXPECT_THROW((void)AvatarDescription::EndGetFromGamer(result), System::InvalidOperationException);
    delete result;
}

TEST_F(AvatarServiceTest, AGamerWithoutAnAvatarGetsAnInvalidDescription) {
    auto* result = AvatarDescription::BeginGetFromGamer(gamer(1), {}, {});
    const auto description = AvatarDescription::EndGetFromGamer(result);
    EXPECT_FALSE(description.getIsValidProperty());
    EXPECT_FLOAT_EQ(description.getHeightProperty(), 0.0f);
    delete result;
}

TEST_F(AvatarServiceTest, AGamerLookedUpByGamertagHasTheirAvatar) {
    std::unique_ptr<Gamer> alice(Gamer::GetFromGamertag("Alice"));
    std::unique_ptr<System::IAsyncResult> result(AvatarDescription::BeginGetFromGamer(alice.get(), {}, {}));
    EXPECT_TRUE(AvatarDescription::EndGetFromGamer(result.get()).getIsValidProperty());
}

TEST_F(AvatarServiceTest, TheServiceAvatarRendersThroughTheStandardApi) {
    std::unique_ptr<System::IAsyncResult> result(AvatarDescription::BeginGetFromGamer(gamer(0), {}, {}));
    auto description = AvatarDescription::EndGetFromGamer(result.get());
    AvatarRenderer renderer(&description);
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (renderer.getStateProperty() == AvatarRendererState::Loading && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(renderer.getStateProperty(), AvatarRendererState::Ready);
}

TEST_F(AvatarServiceTest, ANewerCatalogItemIsFetchedFromTheServiceByHash) {
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest, catalog.assets);
    const auto manifest = WhilePumping([] { return Avatars::catalogManifest(Newer); });
    ASSERT_EQ(manifest->version, Newer);
    ASSERT_NE(manifest->item(102), nullptr);
    const auto bytes = Encoded(0, 1650, Newer, 102);
    const auto descriptor = Avatars::decode(bytes);
    ASSERT_TRUE(descriptor.has_value());
    const auto model = WhilePumping([&] { return Avatars::buildAvatarModel(*descriptor); });
    EXPECT_TRUE(model->substitutedItems.empty());
    // The embedded body and items are still used; only the new hat came over the wire.
    EXPECT_TRUE(Avatars::resolveAsset(*manifest, manifest->bodies[0])->owned == nullptr);
    const auto hat = WhilePumping([&] { return Avatars::resolveAsset(*manifest, "hat_crown.female.glb"); });
    ASSERT_TRUE(hat.has_value());
    EXPECT_TRUE(hat->owned != nullptr);
}

TEST_F(AvatarServiceTest, AnItemTheServiceCannotProvideIsLeftOut) {
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest, {});
    ASSERT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); })->version, Newer);
    const auto descriptor = Avatars::decode(Encoded(1, 1800, Newer, 102));
    const auto model = WhilePumping([&] { return Avatars::buildAvatarModel(*descriptor); });
    ASSERT_EQ(model->substitutedItems.size(), 1u);
    EXPECT_EQ(model->substitutedItems[0], 102);
}

TEST_F(AvatarServiceTest, AnAssetWhoseBytesDoNotMatchTheManifestIsRefused) {
    auto catalog = MakeNewerCatalog();
    for (auto& [hash, bytes] : catalog.assets) {
        bytes[bytes.size() / 2] ^= 0x5a;
    }
    Service::setFakeAvatarCatalog(*service_, catalog.manifest, catalog.assets);
    const auto manifest = WhilePumping([] { return Avatars::catalogManifest(Newer); });
    ASSERT_EQ(manifest->version, Newer);
    EXPECT_FALSE(WhilePumping([&] { return Avatars::resolveAsset(*manifest, "hat_crown.male.glb"); }).has_value());
}
