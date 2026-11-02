// SPDX-License-Identifier: MS-PL
#include "CNA/C/cna.h"
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "System/IServiceProvider.hpp"
#include <cstring>
#include <thread>
#include <limits>
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
namespace Service = CNA::Internal::GamerServices;
static std::shared_ptr<Service::IGamerServicesBackend> previous;
extern "C" void cna_test_service_begin(void)
{
    previous = Service::backend();
    Service::ServiceIdentity person;
    person.userId = "fixture"; person.gamertag = "Fixture"; person.displayName = "Fixture Name";
    Service::ServiceLeaderboardFixture board;
    board.key = "BestScoreLifeTime";
    Service::ServiceLeaderboardEntry entry;
    entry.userId = person.userId; entry.gamertag = person.gamertag; entry.rating = 42; entry.rank = 1;
    entry.columns["stream"] = {"stream", std::string("00ff7f")};
    board.entries.push_back(entry);
    Service::setBackendForTesting(Service::makeFakeBackend({person}, {}, {board}));
}
extern "C" void cna_test_service_end(void)
{
    if (previous) {
        Service::backend()->signOut(0);
        GamerServicesDispatcher::Update();
        Service::setBackendForTesting(std::move(previous));
    }
}
extern "C" int cna_test_service_worker(int (*test)(void))
{
    int result = 1;
    std::thread worker([&] { result = test(); }); worker.join(); return result;
}
extern "C" int cna_test_service_name(unsigned kind, const char* text, uint64_t count)
{
    std::string name;
    switch (kind) {
    case 0: { std::unique_ptr<Gamer> gamer(Gamer::GetFromGamertag("Fixture")); name = gamer->GetTypeName(); break; }
    case 1: name = FriendGamer::CreateInternal("F", "F", false, false, false, false, false, false).GetTypeName(); break;
    case 2: name = SignedInGamer::CreateInternal("S", false, false, PlayerIndex::One).GetTypeName(); break;
    case 3: name = Net::NetworkGamer::CreateInternal(nullptr).GetTypeName(); break;
    default: name = Net::LocalNetworkGamer::CreateInternal(nullptr, nullptr).GetTypeName(); break;
    }
    return count == name.size() && std::memcmp(text, name.data(), name.size()) == 0;
}
extern "C" int cna_test_service_config(const CNA_GamerServicesConfiguration* value)
{
    const auto native = CNA::GamerServices::resolveConfiguration();
    return native.endpoint == value->endpoint && native.gameId == value->game_id &&
        native.caBundle == value->ca_bundle && native.titleVersion == value->title_version &&
        native.insecureLoopback == (value->insecure_loopback != 0) &&
        native.avatarCatalogUpdates == (value->avatar_catalog_updates != 0) &&
        native.maxAvatarCatalogBytes == value->max_avatar_catalog_bytes;
}
extern "C" void cna_test_service_initialize(void)
{
    struct Provider final : System::IServiceProvider {
        void* GetService(const std::type_info&) const override { return nullptr; }
    };
    static Provider provider;
    GamerServicesDispatcher::Initialize(provider);
    Service::backend()->signIn(0, "Fixture", "fixture");
    GamerServicesDispatcher::Update();
}
extern "C" CNA_Bool cna_test_service_nan_pair(void)
{
    auto dictionary = PropertyDictionary::CreateInternal({});
    const System::Collections::Generic::KeyValuePair<std::string, std::any> pair(
        "nan", std::numeric_limits<double>::quiet_NaN());
    dictionary.Add(pair);
    return dictionary.Contains(pair) && dictionary.Remove(pair) ? CNA_TRUE : CNA_FALSE;
}
extern "C" int cna_test_service_endpoint_supported(void)
{
    CNA::GamerServices::Configuration value;
    value.endpoint = "https://example.invalid/cna/v1";
    value.gameId = "fixture";
    try { CNA::GamerServices::validateConfiguration(value); return 1; }
    catch (const std::exception&) { return 0; }
}
extern "C" void cna_test_service_oversized_configuration(void)
{
    CNA::GamerServices::Configuration value;
    value.caBundle.assign(4097, 'x');
    CNA::GamerServices::setConfigurationOverride(value);
}
extern "C" void cna_test_service_nul_configuration(void)
{
    CNA::GamerServices::Configuration value;
    value.gameId = std::string("a\0b", 3);
    CNA::GamerServices::setConfigurationOverride(value);
}
