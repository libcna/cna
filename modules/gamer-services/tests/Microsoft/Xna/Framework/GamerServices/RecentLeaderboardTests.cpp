// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
#include <array>
#include <exception>

namespace {
using namespace Microsoft::Xna::Framework::GamerServices;
namespace Local = CNA::Internal::GamerServices;
using Json = nlohmann::json;
LeaderboardEntry First(const LeaderboardReader& reader) {
    const auto rows = reader.getEntriesProperty();
    return rows[0];
}
// Oracle throughout: owner-approved CNA GAP-18 policy (2026-11-06), not Xbox LIVE.
// 7*24*60*60 = 604800 seconds. T=2000000000 => cutoff exactly 2000604800.
class RecentLeaderboardTest : public ::testing::Test {
protected:
    static constexpr long long T = 2000000000LL;
    long long now = T;
    SignedInGamer a = SignedInGamer::CreateInternal("a");
    SignedInGamer b = SignedInGamer::CreateInternal("b");
    SignedInGamer c = SignedInGamer::CreateInternal("c");
    SignedInGamer d = SignedInGamer::CreateInternal("d");
    SignedInGamerCollection previous = SignedInGamerCollection::CreateInternal({});
    std::shared_ptr<Local::IGamerServicesBackend> previousBackend;
    void SetUp() override {
        previousBackend = Local::backend();
        CNA::GamerServices::setConfigurationOverride(CNA::GamerServices::Configuration{});
        Local::setBackendForTesting({});
        ASSERT_FALSE(Local::backend()->serviceEnabled());
        Microsoft::Xna::Framework::Storage::StorageDevice::SetAppNameEXT("CnaTestsGap18Recent");
        Local::ResetStoreForTestingEXT();
        Local::SetLeaderboardClockForTestingEXT([this] { return now; });
        previous = *Gamer::getSignedInGamersProperty();
        Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
            SignedInGamerCollection::CreateInternal({&a,&b,&c,&d})));
    }
    void TearDown() override {
        Local::SetLeaderboardClockForTestingEXT({});
        Gamer::setSignedInGamersProperty(new SignedInGamerCollection(previous));
        Local::ResetStoreForTestingEXT();
        Local::setBackendForTesting(previousBackend);
        CNA::GamerServices::setConfigurationOverride({});
    }
    std::filesystem::path path(const std::string& key) {
        return std::filesystem::path(Local::GetGamerServicesStoreRootEXT()) / "leaderboards" / (key+".json");
    }
    Json disk(const std::string& key) { std::ifstream file(path(key)); return Json::parse(file); }
    void raw(const std::string& key,const Json& root) {
        std::filesystem::create_directories(path(key).parent_path());
        std::ofstream file(path(key));file << root.dump();file.close();ASSERT_TRUE(file.good());
    }
    void save(const std::string& key,const std::string& tag,long long rating,int marker=1) {
        auto columns=PropertyDictionary::CreateInternal({});columns.SetValue("marker",marker);
        Local::SaveLeaderboardEntryEXT(key,{tag,rating},&columns);
    }
};
}

TEST_F(RecentLeaderboardTest, ExactExpiryPrunesOnlyCanonicalRecentKeysAndKeepsModesSeparate) {
    for(const auto* key:{"BestScoreRecent_0","BestTimeRecent_-7","BestScoreLifeTime_0","BestTimeLifeTime_0","MyRecent_0","BestScoreRecentExtra_0"})
        save(key,"a",100);
    now = T+604799;
    EXPECT_EQ(1u,Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0").size());
    EXPECT_EQ(1u,Local::LoadLeaderboardEntriesEXT("BestTimeRecent_-7").size());
    now = T+604800;
    for(const auto* key:{"BestScoreRecent_0","BestTimeRecent_-7"}) {
        EXPECT_TRUE(Local::LoadLeaderboardEntriesEXT(key).empty());
        EXPECT_TRUE(disk(key)["entries"].empty());
        auto columns=PropertyDictionary::CreateInternal({});Local::LoadLeaderboardEntryColumnsEXT(key,"a",columns);
        EXPECT_FALSE(columns.ContainsKey("marker"));
    }
    for(const auto* key:{"BestScoreLifeTime_0","BestTimeLifeTime_0","MyRecent_0","BestScoreRecentExtra_0"})
        EXPECT_EQ(1u,Local::LoadLeaderboardEntriesEXT(key).size());
    save("BestScoreRecent_1","a",7);
    EXPECT_TRUE(Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0").empty());
    ASSERT_EQ(1u,Local::LoadLeaderboardEntriesEXT("BestScoreRecent_1").size());
    EXPECT_EQ(7,Local::LoadLeaderboardEntriesEXT("BestScoreRecent_1")[0].Rating);
}

TEST_F(RecentLeaderboardTest, BetterAndEqualWritesRefreshButWorseWritesDoNot) {
    struct Case {const char* key;long long better,worse;};
    for(const auto test:{Case{"BestScoreRecent_0",110,90},Case{"BestTimeRecent_0",90,110}}) {
        now=T;save(test.key,"a",100);
        now=T+10;save(test.key,"a",test.worse,2);
        auto row=disk(test.key)["entries"][0];
        EXPECT_EQ(100,row["rating"]);EXPECT_EQ(T,row["writtenUtcSeconds"]);
        now=T+20;save(test.key,"a",test.better,3);
        row=disk(test.key)["entries"][0];
        EXPECT_EQ(test.better,row["rating"]);EXPECT_EQ(T+20,row["writtenUtcSeconds"]);
        now=T+30;save(test.key,"a",test.better,4);
        row=disk(test.key)["entries"][0];
        EXPECT_EQ(T+30,row["writtenUtcSeconds"]);
        auto columns=PropertyDictionary::CreateInternal({});Local::LoadLeaderboardEntryColumnsEXT(test.key,"a",columns);
        EXPECT_EQ(4,columns.GetValueInt32("marker"));
        now=T+604829;EXPECT_EQ(1u,Local::LoadLeaderboardEntriesEXT(test.key).size());
        now=T+604830;EXPECT_TRUE(Local::LoadLeaderboardEntriesEXT(test.key).empty());
    }
}

TEST_F(RecentLeaderboardTest, WorseCandidateReplacesAnExpiredBestWithoutAnyPriorRead) {
    save("BestScoreRecent_0","a",100);save("BestTimeRecent_0","a",100);
    now=T+604800;
    save("BestScoreRecent_0","a",80,2);save("BestTimeRecent_0","a",120,2);
    EXPECT_EQ(80,Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0").at(0).Rating);
    EXPECT_EQ(120,Local::LoadLeaderboardEntriesEXT("BestTimeRecent_0").at(0).Rating);
    EXPECT_EQ(T+604800,disk("BestScoreRecent_0")["entries"][0]["writtenUtcSeconds"]);
}

TEST_F(RecentLeaderboardTest, LegacyMigrationIsPersistedOnceAndDoesNotRegrantOnReopen) {
    const std::string key="BestScoreRecent_0";
    raw(key,Json{{"entries",Json::array({Json{{"gamertag","a"},{"rating",9007199254740993LL},
        {"columns",Json::array({Json{{"key","marker"},{"type","int32"},{"value",17}}})}}})}});
    auto rows=Local::LoadLeaderboardEntriesEXT(key);
    ASSERT_EQ(1u,rows.size());EXPECT_EQ(9007199254740993LL,rows[0].Rating);
    EXPECT_EQ(1,disk(key)["recentPolicyVersion"]);
    EXPECT_EQ(T,disk(key)["entries"][0]["writtenUtcSeconds"]);
    auto columns=PropertyDictionary::CreateInternal({});Local::LoadLeaderboardEntryColumnsEXT(key,"a",columns);
    EXPECT_EQ(17,columns.GetValueInt32("marker"));
    // Fresh objects/read calls simulate reopening the on-disk store, with no cached timestamp.
    now=T+123;EXPECT_EQ(1u,Local::LoadLeaderboardEntriesEXT(key).size());
    EXPECT_EQ(T,disk(key)["entries"][0]["writtenUtcSeconds"]);
    now=T+604800;EXPECT_TRUE(Local::LoadLeaderboardEntriesEXT(key).empty());
    now=T+604801;EXPECT_TRUE(Local::LoadLeaderboardEntriesEXT(key).empty());
}

TEST_F(RecentLeaderboardTest, VersionedMissingOrInvalidTimestampsAreNeverMigratedAgain) {
    for(const Json& stamp:{Json(nullptr),Json("yesterday"),Json(-1),Json(1.5),Json(18446744073709551615ULL)}) {
        raw("BestScoreRecent_0",{{"recentPolicyVersion",1},{"entries",Json::array({
            Json{{"gamertag","a"},{"rating",999},{"writtenUtcSeconds",stamp}},
            Json{{"gamertag","b"},{"rating",7},{"writtenUtcSeconds",T}}})}});
        auto rows=Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0");
        ASSERT_EQ(1u,rows.size());EXPECT_EQ("b",rows[0].Gamertag);EXPECT_EQ(7,rows[0].Rating);
    }
    raw("BestScoreRecent_0",{{"recentPolicyVersion",1},{"entries",Json::array({Json{{"gamertag","a"},{"rating",999}}})}});
    EXPECT_TRUE(Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0").empty());
}

TEST_F(RecentLeaderboardTest, ExpiryPrecedesRanksPagingCenteringAndFriendRestriction) {
    const auto id=LeaderboardIdentity::Create(LeaderboardKey::BestScoreRecent);
    save("BestScoreRecent_0","a",100);
    now=T+1;save("BestScoreRecent_0","b",90);save("BestScoreRecent_0","c",80);save("BestScoreRecent_0","d",70);
    now=T+604800;
    auto page=LeaderboardReader::Read(id,0,1);
    EXPECT_EQ(3,page.getTotalLeaderboardSizeProperty());
    ASSERT_EQ(1,page.getEntriesProperty().getCountProperty());
    EXPECT_EQ(&b,First(page).getGamerProperty());
    EXPECT_EQ(1,First(page).getRankingEXTProperty());
    page.PageDown();EXPECT_EQ(&c,First(page).getGamerProperty());
    EXPECT_EQ(2,First(page).getRankingEXTProperty());
    page.PageDown();EXPECT_EQ(&d,First(page).getGamerProperty());
    EXPECT_FALSE(page.getCanPageDownProperty());page.PageUp();
    EXPECT_EQ(&c,First(page).getGamerProperty());
    auto offset=LeaderboardReader::Read(id,1,1);
    ASSERT_EQ(1,offset.getEntriesProperty().getCountProperty());EXPECT_EQ(&c,First(offset).getGamerProperty());
    auto centered=LeaderboardReader::Read(id,&c,1);
    EXPECT_EQ(1,centered.getPageStartProperty());EXPECT_EQ(&c,First(centered).getGamerProperty());
    auto friends=LeaderboardReader::Read(id,std::vector<Gamer*>{&a,&c,&d},&c,1);
    EXPECT_EQ(2,friends.getTotalLeaderboardSizeProperty());
    ASSERT_EQ(1,friends.getEntriesProperty().getCountProperty());
    EXPECT_EQ(&c,First(friends).getGamerProperty());
    EXPECT_EQ(2,First(friends).getRankingEXTProperty());
    friends.PageDown();EXPECT_EQ(&d,First(friends).getGamerProperty());
}

TEST_F(RecentLeaderboardTest, BestTimeRecentSortsAscendingAfterExpiry) {
    save("BestTimeRecent_0","a",1);
    now=T+1;save("BestTimeRecent_0","b",90);save("BestTimeRecent_0","c",80);
    now=T+604800;
    auto reader=LeaderboardReader::Read(LeaderboardIdentity::Create(LeaderboardKey::BestTimeRecent),0,10);
    const auto rows=reader.getEntriesProperty();ASSERT_EQ(2,rows.getCountProperty());
    EXPECT_EQ(&c,rows[0].getGamerProperty());EXPECT_EQ(80,rows[0].getRatingProperty());
    EXPECT_EQ(1,rows[0].getRankingEXTProperty());EXPECT_EQ(&b,rows[1].getGamerProperty());
}

TEST_F(RecentLeaderboardTest, ClockCorrectionsDoNotRewriteStampsOrResurrectPrunedRows) {
    save("BestScoreRecent_0","a",100);
    now=T-100;EXPECT_EQ(1u,Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0").size());
    EXPECT_EQ(T,disk("BestScoreRecent_0")["entries"][0]["writtenUtcSeconds"]);
    now=T+604800;EXPECT_TRUE(Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0").empty());
    now=T;EXPECT_TRUE(Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0").empty());
}

TEST_F(RecentLeaderboardTest, FailedMigrationCannotGrantAnUnpersistedFreshWindow) {
    raw("BestScoreRecent_0",{{"entries",Json::array({Json{{"gamertag","a"},{"rating",42}}})}});
    const auto before=disk("BestScoreRecent_0");
    std::filesystem::create_directory(path("BestScoreRecent_0").string()+".tmp");
    EXPECT_THROW((void)Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0"),std::exception);
    EXPECT_EQ(before,disk("BestScoreRecent_0"));
    std::filesystem::remove(path("BestScoreRecent_0").string()+".tmp");
    now=T+10;ASSERT_EQ(1u,Local::LoadLeaderboardEntriesEXT("BestScoreRecent_0").size());
    EXPECT_EQ(T+10,disk("BestScoreRecent_0")["entries"][0]["writtenUtcSeconds"]);
}

TEST_F(RecentLeaderboardTest, LegacyStampedRowsKeepTheirAgeAndFutureFormatsArePreserved) {
    const std::string key="BestScoreRecent_0";
    raw(key,{{"entries",Json::array({
        Json{{"gamertag","a"},{"rating",100},{"writtenUtcSeconds",T-604800}},
        Json{{"gamertag","b"},{"rating",90}}})}});
    auto rows=Local::LoadLeaderboardEntriesEXT(key);
    ASSERT_EQ(1u,rows.size());EXPECT_EQ("b",rows[0].Gamertag);
    EXPECT_EQ(T,disk(key)["entries"][0]["writtenUtcSeconds"]);
    raw(key,{{"recentPolicyVersion",2},{"entries",Json::array({Json{{"gamertag","a"},{"rating",100}}})}});
    const auto before=disk(key);
    EXPECT_THROW((void)Local::LoadLeaderboardEntriesEXT(key),std::exception);
    EXPECT_THROW(save(key,"a",101),std::exception);
    EXPECT_EQ(before,disk(key));
}

TEST_F(RecentLeaderboardTest, ConcurrentMigrationPersistsOneSharedTimestamp) {
    const std::string key="BestScoreRecent_0";
    raw(key,{{"entries",Json::array({Json{{"gamertag","a"},{"rating",100}},Json{{"gamertag","b"},{"rating",90}}})}});
    long long captures=0;
    Local::SetLeaderboardClockForTestingEXT([&] { return T+captures++; });
    std::array<std::exception_ptr,2> failures{};
    std::array<std::size_t,2> sizes{};
    std::array<std::thread,2> readers;
    for(int i=0;i<2;++i)readers[i]=std::thread([&,i] {
        try{sizes[i]=Local::LoadLeaderboardEntriesEXT(key).size();}catch(...){failures[i]=std::current_exception();}
    });
    for(auto& thread:readers)thread.join();
    Local::SetLeaderboardClockForTestingEXT([this] { return now; });
    EXPECT_FALSE(failures[0]);EXPECT_FALSE(failures[1]);
    EXPECT_EQ(2u,sizes[0]);EXPECT_EQ(2u,sizes[1]);
    const auto rows=disk(key)["entries"];ASSERT_EQ(2u,rows.size());
    ASSERT_TRUE(rows[0].contains("writtenUtcSeconds"));ASSERT_TRUE(rows[1].contains("writtenUtcSeconds"));
    EXPECT_EQ(T,rows[0].at("writtenUtcSeconds"));EXPECT_EQ(T,rows[1].at("writtenUtcSeconds"));
}

TEST_F(RecentLeaderboardTest, OneReadCapturesRatingAndColumnsBeforeTheCutoffTogether) {
    save("BestScoreRecent_0","a",42,17);
    int captures=0;
    Local::SetLeaderboardClockForTestingEXT([&] { return T+604799+captures++; });
    auto reader=LeaderboardReader::Read(LeaderboardIdentity::Create(LeaderboardKey::BestScoreRecent),0,10);
    const auto rows=reader.getEntriesProperty();
    EXPECT_EQ(1,captures);ASSERT_EQ(1,rows.getCountProperty());
    EXPECT_EQ(42,rows[0].getRatingProperty());
    EXPECT_EQ(17,rows[0].getColumnsProperty().GetValueInt32("marker"));
    Local::SetLeaderboardClockForTestingEXT([this] { return now; });
}
