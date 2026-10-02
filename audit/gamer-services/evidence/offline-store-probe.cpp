#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
#include <iostream>
#include <filesystem>
int main() {
 using namespace CNA::Internal::GamerServices;
 using namespace Microsoft::Xna::Framework;
 Storage::StorageDevice::SetAppNameEXT("CnaAuditProbe");
 auto columns=GamerServices::PropertyDictionary::CreateInternal({});
 constexpr long long input=9007199254740993LL;
 columns.SetValue("precise",input);
 SaveLeaderboardEntryEXT("probe",{"Audit",input},&columns);
 auto rows=LoadLeaderboardEntriesEXT("probe");
 auto loaded=GamerServices::PropertyDictionary::CreateInternal({});
 LoadLeaderboardEntryColumnsEXT("probe","Audit",loaded);
 std::cout<<"input="<<input<<" rating="<<rows.at(0).Rating<<" column="<<loaded.GetValueInt64("precise")<<'\n';
 const auto path=std::filesystem::path(GetGamerServicesStoreRootEXT())/"leaderboards"/"blocked.json";
 std::filesystem::create_directory(path);
 bool threw=false;try {SaveLeaderboardEntryEXT("blocked",{"Audit",42},nullptr);}catch(...){threw=true;}
 std::cout<<"destination-is-directory threw="<<threw<<" loaded="<<LoadLeaderboardEntriesEXT("blocked").size()<<'\n';
}
