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

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <nlohmann/json.hpp>
#include <thread>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

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

// A catalog one version past everything compiled in: v1 plus one hat the library does not have.
struct NewerCatalog {
    nlohmann::json json;
    std::map<std::string, std::vector<unsigned char>> assets;
    // Lists a file under a name (replacing an entry of that name) and makes it servable.
    void put(const std::string& name, const std::vector<unsigned char>& bytes) {
        auto& list = json["assets"];
        for (auto entry = list.begin(); entry != list.end(); ++entry) {
            if ((*entry)["name"] == name) {
                list.erase(entry);
                break;
            }
        }
        const auto hash = Avatars::sha256Hex(bytes);
        list.push_back({{"name", name}, {"sha256", hash}, {"size", bytes.size()}});
        assets[hash] = bytes;
    }
    std::string manifest() const { return json.dump(); }
};

std::vector<unsigned char> EmbeddedV1File(const std::string& name) {
    const auto embedded = Avatars::embeddedManifest(Avatars::BaseCatalogVersion);
    const auto bytes = Avatars::resolveAsset(*embedded, name);
    return {bytes->view.begin(), bytes->view.end()};
}

// Same-length text replacement inside a GLB's JSON chunk, so the container stays valid.
std::vector<unsigned char> Renamed(std::vector<unsigned char> bytes, const std::string& from, const std::string& to) {
    std::string text(bytes.begin(), bytes.end());
    for (auto at = text.find(from); at != std::string::npos; at = text.find(from, at)) {
        text.replace(at, from.size(), to);
    }
    return {text.begin(), text.end()};
}

NewerCatalog MakeNewerCatalog() {
    const auto& files = Avatars::embeddedCatalogFiles();
    auto manifestFile = std::find_if(files.begin(), files.end(), [](const auto& file) { return std::string_view(file.name) == "v1/catalog.json"; });
    NewerCatalog catalog;
    catalog.json = nlohmann::json::parse(std::string(reinterpret_cast<const char*>(manifestFile->data), manifestFile->size));
    catalog.json["catalogVersion"] = Newer;
    for (const char* body : {"female", "male"}) {
        // A distinct file: the cap renamed inside its JSON chunk, so its contents and hash are new.
        catalog.put(std::string("hat_crown.") + body + ".glb", Renamed(EmbeddedV1File(std::string("hat_cap.") + body + ".glb"), "hat_cap", "hat_crn"));
    }
    catalog.json["items"].push_back({{"id", 102}, {"slot", "hat"}, {"name", "hat_crown"},
                                     {"assets", {{"female", "hat_crown.female.glb"}, {"male", "hat_crown.male.glb"}}}});
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
        // Installed catalog packs go to a directory of this test's own, never the user's.
        static std::atomic<int> sequence{0};
        root_ = std::filesystem::temp_directory_path() /
                ("cna-avatar-catalogs-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                 std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "-" + std::to_string(++sequence));
        std::filesystem::remove_all(root_);
        Avatars::setInstalledCatalogRootForTesting(root_);
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
        Avatars::setInstalledCatalogRootForTesting({});
        std::filesystem::remove_all(root_);
    }
    SignedInGamer* gamer(int slot) { return (*Gamer::getSignedInGamersProperty())[slot]; }

    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_, service_;
    std::filesystem::path root_;
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

namespace {
bool Installed(const std::filesystem::path& root) {
    return std::filesystem::exists(root / ("v" + std::to_string(Newer)) / "pack.json");
}

bool Staged(const std::filesystem::path& root) {
    std::error_code error;
    for (std::filesystem::directory_iterator entry(root, error), end; !error && entry != end; entry.increment(error)) {
        if (entry->path().filename().string().starts_with(".staging-")) {
            return true;
        }
    }
    return false;
}

// What an avatar draws with, for comparing two assemblies.
std::string Digest(const Avatars::AvatarModel& model) {
    std::vector<std::uint8_t> bytes;
    auto put = [&](float value) {
        const auto q = std::llround(value * 1e5);
        for (int shift = 0; shift < 64; shift += 8) {
            bytes.push_back(static_cast<std::uint8_t>(q >> shift));
        }
    };
    for (const auto& part : model.parts) {
        for (const auto& v : part.vertices) {
            put(v.position.X), put(v.position.Y), put(v.position.Z), put(v.uv.X), put(v.uv.Y);
            bytes.insert(bytes.end(), v.joints.begin(), v.joints.end());
        }
        for (auto index : part.indices) {
            put(static_cast<float>(index));
        }
        put(part.color.X), put(part.color.Y), put(part.color.Z);
    }
    for (const auto& image : model.images) {
        bytes.insert(bytes.end(), image.rgba.begin(), image.rgba.end());
    }
    return Avatars::sha256Hex(bytes);
}

// The acceptance of a catalog that must never activate: nothing under the version's name, the
// newer avatar drawn as the default avatar, the compiled-in catalogs untouched.
void ExpectNotActivated(const std::filesystem::path& root) {
    EXPECT_FALSE(Installed(root));
    EXPECT_EQ(Avatars::installedManifest(Newer), nullptr);
    const auto descriptor = Avatars::decode(Encoded(0, 1650, Newer, 102));
    const auto model = WhilePumping([&] { return Avatars::buildAvatarModel(*descriptor); });
    EXPECT_TRUE(model->catalogUnavailable);
    const auto v1 = Avatars::decode(Encoded(1, 1800));
    EXPECT_FALSE(Avatars::buildAvatarModel(*v1)->catalogUnavailable);
}
}

TEST_F(AvatarServiceTest, AWarmInstallationDrawsAServiceAvatarWithTheFileEndpointOff) {
    // The standard path: the service hands over the description only; the compiled-in catalog
    // draws it. With the file endpoint off, nothing else is asked for.
    Service::setFakeCatalogFileFailures(*service_, 0);
    std::unique_ptr<System::IAsyncResult> result(AvatarDescription::BeginGetFromGamer(gamer(0), {}, {}));
    auto description = AvatarDescription::EndGetFromGamer(result.get());
    ASSERT_TRUE(description.getIsValidProperty());
    AvatarRenderer renderer(&description);
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (renderer.getStateProperty() == AvatarRendererState::Loading && std::chrono::steady_clock::now() < limit) {
        GamerServicesDispatcher::Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_EQ(renderer.getStateProperty(), AvatarRendererState::Ready);
    const auto bytes = description.getDescriptionProperty();
    const auto model = Avatars::buildAvatarModel(*Avatars::decode(bytes));
    EXPECT_FALSE(model->catalogUnavailable);
    EXPECT_TRUE(model->substitutedItems.empty());
    const auto traffic = Service::fakeAvatarTraffic(*service_);
    EXPECT_EQ(traffic.avatarReads, 1);
    EXPECT_EQ(traffic.packReads, 0);
    EXPECT_EQ(traffic.fileDownloads, 0);
}

TEST_F(AvatarServiceTest, AMissingCatalogIsInstalledOnceAsOneValidatedPack) {
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    const auto manifest = WhilePumping([] { return Avatars::catalogManifest(Newer); });
    ASSERT_NE(manifest, nullptr);
    ASSERT_EQ(manifest->version, Newer);
    EXPECT_TRUE(Installed(root_));
    EXPECT_FALSE(Staged(root_));
    // Only the manifest and the two files this build lacks crossed the wire; the rest came from
    // the compiled-in catalog.
    auto traffic = Service::fakeAvatarTraffic(*service_);
    EXPECT_EQ(traffic.packReads, 1);
    EXPECT_EQ(traffic.fileDownloads, 3);
    const auto descriptor = Avatars::decode(Encoded(0, 1650, Newer, 102));
    const auto model = Avatars::buildAvatarModel(*descriptor);
    EXPECT_FALSE(model->catalogUnavailable);
    EXPECT_TRUE(model->substitutedItems.empty());
    // The pack is self-contained: its files are read from the installed pack.
    EXPECT_TRUE(Avatars::resolveAsset(*manifest, "hat_crown.female.glb")->owned != nullptr);
    EXPECT_EQ(Avatars::availableCatalogVersions().back(), Newer);

    // A later run (the process forgets what it loaded) finds the pack and asks the service nothing,
    // even with the service's catalog and file endpoint gone.
    Service::setFakeAvatarCatalog(*service_, "", {});
    Service::setFakeCatalogFileFailures(*service_, 0);
    Avatars::setInstalledCatalogRootForTesting(root_);
    ASSERT_NE(Avatars::catalogManifest(Newer), nullptr);
    EXPECT_FALSE(Avatars::buildAvatarModel(*descriptor)->catalogUnavailable);
    traffic = Service::fakeAvatarTraffic(*service_);
    EXPECT_EQ(traffic.packReads, 1);
    EXPECT_EQ(traffic.fileDownloads, 3);
}

TEST_F(AvatarServiceTest, AnInterruptedInstallActivatesNothingAndARetryResumesIt) {
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    // The manifest and the first hat arrive; the second hat does not.
    Service::setFakeCatalogFileFailures(*service_, 2);
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    EXPECT_TRUE(Staged(root_));
    ExpectNotActivated(root_);
    // A failed version is not asked for again at once.
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    EXPECT_EQ(Service::fakeAvatarTraffic(*service_).packReads, 1);

    Avatars::forgetCatalogUpdateFailures();
    Service::setFakeCatalogFileFailures(*service_, -1);
    ASSERT_NE(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    EXPECT_TRUE(Installed(root_));
    EXPECT_FALSE(Staged(root_));
    // The hat the interrupted attempt verified was reused: the retry fetched the manifest and the other hat.
    EXPECT_EQ(Service::fakeAvatarTraffic(*service_).fileDownloads, 4);
}

TEST_F(AvatarServiceTest, AMalformedManifestIsNeverActivated) {
    auto catalog = MakeNewerCatalog();
    catalog.json["items"].back()["slot"] = "wings";
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    EXPECT_EQ(Service::fakeAvatarTraffic(*service_).fileDownloads, 1);  // the manifest only
    ExpectNotActivated(root_);
}

TEST_F(AvatarServiceTest, AFileWhoseBytesDoNotMatchTheManifestIsNeverActivated) {
    auto catalog = MakeNewerCatalog();
    for (auto& [hash, bytes] : catalog.assets) {
        bytes[bytes.size() / 2] ^= 0x5a;
    }
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    ExpectNotActivated(root_);
}

TEST_F(AvatarServiceTest, AnUnreadableModelIsNeverActivated) {
    auto catalog = MakeNewerCatalog();
    std::vector<unsigned char> junk(512, 0x41);
    junk[0] = 'g', junk[1] = 'l', junk[2] = 'T', junk[3] = 'F';
    catalog.put("hat_crown.male.glb", junk);
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    ExpectNotActivated(root_);
    EXPECT_FALSE(Staged(root_));  // an invalid pack is not kept for a retry
}

TEST_F(AvatarServiceTest, AModelOnAnotherSkeletonIsNeverActivated) {
    auto catalog = MakeNewerCatalog();
    // Valid glTF, but one joint is not an XNA avatar bone.
    catalog.put("hat_crown.male.glb", Renamed(EmbeddedV1File("hat_cap.male.glb"), "ElbowLeft", "ElbowLeff"));
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    ExpectNotActivated(root_);
}

TEST_F(AvatarServiceTest, AnimationsWithoutEveryPresetAreNeverActivated) {
    auto catalog = MakeNewerCatalog();
    // The animations file replaced by a model with no clips.
    catalog.put("animations.glb", EmbeddedV1File("hat_cap.male.glb"));
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    ExpectNotActivated(root_);
}

TEST_F(AvatarServiceTest, AnOversizedCatalogIsRefusedBeforeAnyDownload) {
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    Service::setFakeAvatarCatalogPolicy(*service_, Service::AvatarCatalogPolicy{true, 1u << 20});
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    EXPECT_EQ(Service::fakeAvatarTraffic(*service_).fileDownloads, 0);
    ExpectNotActivated(root_);
    // A listed file beyond the per-file bound is a malformed manifest.
    auto huge = MakeNewerCatalog();
    huge.json["assets"].push_back({{"name", "huge.glb"}, {"sha256", std::string(64, 'b')}, {"size", 9u << 20}});
    EXPECT_THROW((void)Avatars::parseManifest(huge.manifest()), std::runtime_error);
}

TEST_F(AvatarServiceTest, AClientThatDeclinesUpdatesDrawsTheDefaultAvatarAndReadsNoIds) {
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    Service::setFakeAvatarCatalogPolicy(*service_, Service::AvatarCatalogPolicy{false, 64u << 20});
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    EXPECT_EQ(Service::fakeAvatarTraffic(*service_).packReads, 0);
    const auto descriptor = Avatars::decode(Encoded(1, 1900, Newer, 102));
    const auto first = WhilePumping([&] { return Avatars::buildAvatarModel(*descriptor); });
    const auto second = WhilePumping([&] { return Avatars::buildAvatarModel(*descriptor); });
    EXPECT_TRUE(first->catalogUnavailable);
    EXPECT_EQ(first->substitutedItems, (std::vector<std::uint16_t>{1, 20, 40, 60, 102}));
    EXPECT_FLOAT_EQ(first->height, 1.9f);
    EXPECT_EQ(Digest(*first), Digest(*second));
}

TEST_F(AvatarServiceTest, TwoClientsWithTheSameCatalogAndDescriptionBuildTheSameAvatar) {
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    const auto descriptor = Avatars::decode(Encoded(1, 1720, Newer, 102));
    ASSERT_NE(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    const auto first = Digest(*Avatars::buildAvatarModel(*descriptor));
    // A second client: its own, empty catalog directory, installing the same pack.
    const auto other = root_.string() + "-second";
    std::filesystem::remove_all(other);
    Avatars::setInstalledCatalogRootForTesting(other);
    ASSERT_NE(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    EXPECT_EQ(Digest(*Avatars::buildAvatarModel(*descriptor)), first);
    Avatars::setInstalledCatalogRootForTesting(root_);
    std::filesystem::remove_all(other);
    // And the compiled-in path is as deterministic.
    const auto v1 = Avatars::decode(Encoded(0, 1600));
    EXPECT_EQ(Digest(*Avatars::buildAvatarModel(*v1)), Digest(*Avatars::buildAvatarModel(*v1)));
}

namespace {
// Reads a signed-in gamer's description the XNA way and pumps until it completes.
AvatarDescription ReadAvatar(Gamer* gamer) {
    std::unique_ptr<System::IAsyncResult> result(AvatarDescription::BeginGetFromGamer(gamer, {}, {}));
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!result->getIsCompletedProperty() && std::chrono::steady_clock::now() < limit) {
        GamerServicesDispatcher::Update();
    }
    return AvatarDescription::EndGetFromGamer(result.get());
}

void PumpFor(std::chrono::milliseconds duration) {
    const auto limit = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < limit) {
        GamerServicesDispatcher::Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

class AvatarChangedTest : public AvatarServiceTest {
protected:
    void SetUp() override {
        AvatarServiceTest::SetUp();
        Avatars::setAvatarChangeCheckInterval(std::chrono::milliseconds(0));
    }
    void TearDown() override {
        Avatars::setAvatarChangeCheckInterval(std::chrono::milliseconds(10000));
        Service::setFakeAvatarsUnreachable(*service_, false);
        AvatarServiceTest::TearDown();
    }
};
}

TEST_F(AvatarChangedTest, TheSameAvatarRaisesNothingAndTheSlotKeepsItsDescription) {
    auto first = ReadAvatar(gamer(0));
    int calls = 0;
    first.Changed += [&](System::Object*, const System::EventArgs&) { ++calls; };
    PumpFor(std::chrono::milliseconds(80));
    EXPECT_EQ(calls, 0);
    // The slot hands out the same description (and so the same event) while nothing changes.
    auto second = ReadAvatar(gamer(0));
    EXPECT_TRUE(second.Changed.IsShared());
    EXPECT_EQ(second.Changed.Size(), 1u);
    // A new catalog on the service is not a change of anyone's avatar.
    Service::setFakeAvatarCatalog(*service_, "{}", {});
    PumpFor(std::chrono::milliseconds(80));
    EXPECT_EQ(calls, 0);
}

TEST_F(AvatarChangedTest, ANewServiceAvatarRaisesChangedOnceWithTheGamerOnTheDispatcherThread) {
    auto description = ReadAvatar(gamer(0));
    int calls = 0;
    System::Object* sender = nullptr;
    std::thread::id thread;
    auto copy = description;  // subscribing through any copy is subscribing to the one event
    copy.Changed += [&](System::Object* from, const System::EventArgs&) {
        ++calls;
        sender = from;
        thread = std::this_thread::get_id();
    };
    const auto updated = Encoded(0, 1600);
    Service::setFakeAvatar(*service_, "a", updated);
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (calls == 0 && std::chrono::steady_clock::now() < limit) {
        GamerServicesDispatcher::Update();
    }
    ASSERT_EQ(calls, 1);
    EXPECT_EQ(sender, static_cast<System::Object*>(gamer(0)));
    EXPECT_EQ(thread, std::this_thread::get_id());
    // The slot was emptied: the next read is the new avatar, and the old event never fires again.
    const auto next = ReadAvatar(gamer(0));
    EXPECT_EQ(next.getDescriptionProperty(), std::vector<SharpRuntime::bytecs>(updated.begin(), updated.end()));
    EXPECT_EQ(next.Changed.Size(), 0u);
    Service::setFakeAvatar(*service_, "a", Encoded(1, 1700));
    PumpFor(std::chrono::milliseconds(80));
    EXPECT_EQ(calls, 1);
}

TEST_F(AvatarChangedTest, OtherBytesForTheSameStoredAvatarAreNotAChange) {
    // A projection this client no longer needs (it installed the catalog since) arrives as other
    // bytes with the stored avatar's revision: the avatar did not change, so nothing is raised.
    auto description = ReadAvatar(gamer(0));
    int calls = 0;
    description.Changed += [&](System::Object*, const System::EventArgs&) { ++calls; };
    Service::setFakeAvatar(*service_, "a", Encoded(1, 1840), false);
    PumpFor(std::chrono::milliseconds(120));
    EXPECT_EQ(calls, 0);
    Service::setFakeAvatar(*service_, "a", Encoded(1, 1840), true);
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (calls == 0 && std::chrono::steady_clock::now() < limit) {
        GamerServicesDispatcher::Update();
    }
    EXPECT_EQ(calls, 1);
}

TEST_F(AvatarChangedTest, UnsubscribingThroughAnyCopyStopsTheHandler) {
    auto description = ReadAvatar(gamer(0));
    int calls = 0;
    const auto token = description.Changed.Add([&](System::Object*, const System::EventArgs&) { ++calls; });
    auto other = ReadAvatar(gamer(0));
    other.Changed.Remove(token);
    Service::setFakeAvatar(*service_, "a", Encoded(0, 1600));
    PumpFor(std::chrono::milliseconds(120));
    EXPECT_EQ(calls, 0);
}

TEST_F(AvatarChangedTest, AnUnreachableServiceRaisesNothingUntilItAnswersAgain) {
    auto description = ReadAvatar(gamer(0));
    int calls = 0;
    description.Changed += [&](System::Object*, const System::EventArgs&) { ++calls; };
    Service::setFakeAvatarsUnreachable(*service_, true);
    Service::setFakeAvatar(*service_, "a", Encoded(0, 1600));
    PumpFor(std::chrono::milliseconds(120));
    EXPECT_EQ(calls, 0);
    Service::setFakeAvatarsUnreachable(*service_, false);
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (calls == 0 && std::chrono::steady_clock::now() < limit) {
        GamerServicesDispatcher::Update();
    }
    EXPECT_EQ(calls, 1);
}

TEST_F(AvatarChangedTest, SigningOutDropsTheSlotWithoutAnEvent) {
    auto description = ReadAvatar(gamer(0));
    int calls = 0;
    description.Changed += [&](System::Object*, const System::EventArgs&) { ++calls; };
    service_->signOut(0);
    GamerServicesDispatcher::Update();
    Service::setFakeAvatar(*service_, "a", Encoded(0, 1600));
    PumpFor(std::chrono::milliseconds(120));
    EXPECT_EQ(calls, 0);
    service_->signIn(0, "Alice", "fixture");
    GamerServicesDispatcher::Update();
}

TEST_F(AvatarChangedTest, GamersWhoAreNotSignedInAreReadFreshAndNeverRaise) {
    std::unique_ptr<Gamer> alice(Gamer::GetFromGamertag("Alice"));
    auto description = ReadAvatar(alice.get());
    EXPECT_FALSE(description.Changed.IsShared());
    int calls = 0;
    description.Changed += [&](System::Object*, const System::EventArgs&) { ++calls; };
    Service::setFakeAvatar(*service_, "a", Encoded(0, 1600));
    PumpFor(std::chrono::milliseconds(80));
    EXPECT_EQ(calls, 0);
    EXPECT_FLOAT_EQ(ReadAvatar(alice.get()).getHeightProperty(), 1.6f);
}

// ---- GSH-09: adversarial catalog updates -------------------------------------------------------
namespace {
// Everything that decides how an avatar looks: every part's geometry, colour, image and layer.
std::uint64_t Fingerprint(const Avatars::AvatarModel& model) {
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&](const void* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) hash = (hash ^ static_cast<const unsigned char*>(data)[i]) * 1099511628211ull;
    };
    mix(&model.height, sizeof(model.height));
    for (const auto& part : model.parts) {
        for (const auto& vertex : part.vertices) mix(&vertex.position, sizeof(vertex.position));
        mix(part.indices.data(), part.indices.size() * sizeof(part.indices[0]));
        mix(&part.color, sizeof(part.color));
        mix(&part.image, sizeof(part.image));
        mix(&part.layer, sizeof(part.layer));
    }
    return hash;
}
}

// Two accounts' avatars need the same missing catalog at once, on several threads: it is
// installed exactly once, every caller gets it, and no staging is left behind.
TEST_F(AvatarServiceTest, ConcurrentRequestsForOnePackInstallItOnce) {
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    std::vector<std::future<std::shared_ptr<const Avatars::CatalogManifest>>> callers;
    for (int i = 0; i < 4; ++i) callers.push_back(std::async(std::launch::async, [] { return Avatars::catalogManifest(Newer); }));
    for (auto& caller : callers) {
        while (caller.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) GamerServicesDispatcher::Update();
    }
    for (auto& caller : callers) {
        const auto manifest = caller.get();
        ASSERT_NE(manifest, nullptr);
        EXPECT_EQ(manifest->version, Newer);
    }
    EXPECT_TRUE(Installed(root_));
    EXPECT_FALSE(Staged(root_));
    int versions = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root_)) versions += entry.path().filename().string().starts_with("v");
    EXPECT_EQ(1, versions);
    const auto model = Avatars::buildAvatarModel(*Avatars::decode(Encoded(0, 1650, Newer, 102)));
    EXPECT_FALSE(model->catalogUnavailable);
    EXPECT_TRUE(model->substitutedItems.empty());
}

// A client that stops in the middle of an install (its process ends) starts again: it keeps the
// files it verified, activates nothing partial, and a week-old staging directory of anything else
// is removed.
TEST_F(AvatarServiceTest, ARestartedClientResumesItsStagingAndClearsAbandonedStaging) {
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    Service::setFakeCatalogFileFailures(*service_, 2);
    EXPECT_EQ(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    ASSERT_TRUE(Staged(root_));
    const auto abandoned = root_ / ".staging-v99-0000000000000000-abandoned";
    std::filesystem::create_directories(abandoned);
    std::filesystem::last_write_time(abandoned, std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 8));
    // The restart: this process forgets every manifest it loaded; nothing partial was activated.
    Avatars::setInstalledCatalogRootForTesting(root_);
    ExpectNotActivated(root_);
    // The service answers again, and the new process has no memory of the failures.
    Service::setFakeCatalogFileFailures(*service_, -1);
    Avatars::forgetCatalogUpdateFailures();
    ASSERT_NE(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    EXPECT_TRUE(Installed(root_));
    EXPECT_FALSE(Staged(root_));
    EXPECT_FALSE(std::filesystem::exists(abandoned));
    EXPECT_EQ(Service::fakeAvatarTraffic(*service_).fileDownloads, 4);
}

// No update exists for the version an avatar names: the default avatar is drawn, ids are never
// read against another catalog, and the service is not asked again at once.
TEST_F(AvatarServiceTest, AnUnavailableUpdateDrawsTheDefaultAvatarAndIsNotAskedAgainAtOnce) {
    Service::setFakeAvatarCatalog(*service_, MakeNewerCatalog().manifest(), {});
    const auto missing = static_cast<std::uint16_t>(Newer + 1);
    EXPECT_EQ(WhilePumping([&] { return Avatars::catalogManifest(missing); }), nullptr);
    const auto model = WhilePumping([&] { return Avatars::buildAvatarModel(*Avatars::decode(Encoded(0, 1650, missing, 102))); });
    EXPECT_TRUE(model->catalogUnavailable);
    EXPECT_EQ(Service::fakeAvatarTraffic(*service_).packReads, 1);
    EXPECT_EQ(WhilePumping([&] { return Avatars::catalogManifest(missing); }), nullptr);
    EXPECT_EQ(Service::fakeAvatarTraffic(*service_).packReads, 1);
    EXPECT_FALSE(Staged(root_));
}

// A pack whose manifest is another version than its descriptor names is refused: a wrong version
// is never substituted for the one asked for.
TEST_F(AvatarServiceTest, APackClaimingAnotherVersionIsNeverSubstituted) {
    auto catalog = MakeNewerCatalog();
    catalog.json["catalogVersion"] = Newer + 1;
    const auto manifest = catalog.manifest();
    Avatars::CatalogPack pack;
    pack.version = Newer;
    pack.packFormat = 1;
    pack.reader = 1;
    pack.descriptionFormats = {1, 2};
    pack.manifestSha256 = Avatars::sha256Hex(std::vector<unsigned char>(manifest.begin(), manifest.end()));
    pack.manifestSize = manifest.size();
    for (const auto& asset : catalog.json["assets"]) pack.totalBytes += asset["size"].get<std::uint64_t>();
    EXPECT_THROW(Avatars::attachCatalogManifest(pack, manifest), std::runtime_error);
    ExpectNotActivated(root_);
}

// An installed newer catalog does not change how a description of an older, frozen catalog looks:
// v1, v2 and v3 descriptions build exactly the same avatar before and after.
TEST_F(AvatarServiceTest, InstallingANewCatalogDoesNotChangeOlderDescriptions) {
    std::vector<std::vector<unsigned char>> older;
    for (std::uint16_t version = Avatars::BaseCatalogVersion; version < Newer; ++version) {
        older.push_back(Encoded(0, 1650, version));
        older.push_back(Encoded(1, 1830, version));
    }
    ASSERT_GE(older.size(), 6u) << "catalogs v1, v2 and v3 are compiled in";
    std::vector<std::uint64_t> before;
    for (const auto& bytes : older) {
        const auto model = Avatars::buildAvatarModel(*Avatars::decode(bytes));
        ASSERT_FALSE(model->catalogUnavailable);
        before.push_back(Fingerprint(*model));
    }
    const auto catalog = MakeNewerCatalog();
    Service::setFakeAvatarCatalog(*service_, catalog.manifest(), catalog.assets);
    ASSERT_NE(WhilePumping([] { return Avatars::catalogManifest(Newer); }), nullptr);
    Avatars::setInstalledCatalogRootForTesting(root_);
    for (std::size_t i = 0; i < older.size(); ++i) {
        const auto model = Avatars::buildAvatarModel(*Avatars::decode(older[i]));
        EXPECT_FALSE(model->catalogUnavailable);
        EXPECT_EQ(before[i], Fingerprint(*model)) << "description " << i;
    }
}

// A description in a format newer than this client knows is not an avatar it can draw: XNA's
// "undefined" case, answered with height 0 and the female body, never a guess.
TEST_F(AvatarServiceTest, ADescriptionInAnUnknownFormatIsNotDrawnAsAnything) {
    auto bytes = Encoded(1, 1830);
    bytes[0] = 3;
    EXPECT_FALSE(Avatars::decode(bytes));
    const AvatarDescription description(std::vector<SharpRuntime::bytecs>(bytes.begin(), bytes.end()));
    EXPECT_FLOAT_EQ(0.0f, description.getHeightProperty());
    EXPECT_EQ(AvatarBodyType::Female, description.getBodyTypeProperty());
}

#if defined(__unix__) || defined(__APPLE__)
// Two processes install the same pack into one directory at the same moment (two games of one
// player starting together): the version is activated once, both succeed, nothing is staged.
TEST(AvatarCatalogProcessTest, TwoProcessesInstallingOnePackActivateItOnce) {
    if (const auto* child = std::getenv("CNA_TEST_CATALOG_CHILD")) {
        const auto catalog = MakeNewerCatalog();
        const auto manifest = catalog.manifest();
        Avatars::CatalogPack pack;
        pack.version = Newer;
        pack.packFormat = 1;
        pack.reader = 1;
        pack.descriptionFormats = {1, 2};
        pack.manifestSha256 = Avatars::sha256Hex(std::vector<unsigned char>(manifest.begin(), manifest.end()));
        pack.manifestSize = manifest.size();
        for (const auto& asset : catalog.json["assets"]) pack.totalBytes += asset["size"].get<std::uint64_t>();
        Avatars::attachCatalogManifest(pack, manifest);
        Avatars::setInstalledCatalogRootForTesting(std::filesystem::path(child) / "catalogs");
        const auto outcome = Avatars::installCatalogPack(pack, std::uint64_t{64} << 20, [&](const Avatars::CatalogAsset& asset) {
            // Slow enough that the two installs overlap.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return catalog.assets.at(asset.sha256);
        });
        const bool ok = outcome == Avatars::CatalogInstall::Installed || outcome == Avatars::CatalogInstall::AlreadyInstalled;
        if (ok) std::ofstream(std::filesystem::path(child) / ("done-" + std::to_string(getpid()))) << static_cast<int>(outcome);
        std::_Exit(ok ? 0 : 1);
    }
    const auto root = std::filesystem::temp_directory_path() / ("cna-catalog-processes-" + std::to_string(getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto self = std::filesystem::read_symlink("/proc/self/exe").string();
    const auto one = "CNA_TEST_CATALOG_CHILD='" + root.string() + "' '" + self + "' --gtest_filter=AvatarCatalogProcessTest.* >/dev/null 2>&1";
    EXPECT_EQ(0, std::system(("(" + one + ") & (" + one + ") & wait").c_str()));
    int finished = 0, installed = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.path().filename().string().starts_with("done-")) continue;
        ++finished;
        std::ifstream in(entry.path());
        int outcome = -1;
        in >> outcome;
        installed += outcome == static_cast<int>(Avatars::CatalogInstall::Installed);
    }
    EXPECT_EQ(2, finished) << "both processes succeed";
    EXPECT_EQ(1, installed) << "exactly one of them activates the pack";
    EXPECT_TRUE(Installed(root / "catalogs"));
    EXPECT_FALSE(Staged(root / "catalogs"));
    Avatars::setInstalledCatalogRootForTesting(root / "catalogs");
    const auto model = Avatars::buildAvatarModel(*Avatars::decode(Encoded(0, 1650, Newer, 102)));
    EXPECT_FALSE(model->catalogUnavailable);
    Avatars::setInstalledCatalogRootForTesting({});
    std::filesystem::remove_all(root);
}
#endif
