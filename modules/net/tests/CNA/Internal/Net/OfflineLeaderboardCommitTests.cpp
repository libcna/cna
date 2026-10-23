// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "System/ArgumentException.hpp"
#include <memory>

namespace {
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Net;
namespace Service = CNA::Internal::GamerServices;

class OfflineLeaderboardCommitTest : public ::testing::Test {
protected:
    void SetUp() override {
        previousBackend = Service::backend();
        CNA::GamerServices::setConfigurationOverride(CNA::GamerServices::Configuration{});
        Service::setBackendForTesting({});
        ASSERT_FALSE(Service::backend()->serviceEnabled());
        Microsoft::Xna::Framework::Storage::StorageDevice::SetAppNameEXT("CnaTestsOfflineLeaderboardCommit");
        Service::ResetStoreForTestingEXT();
        previousGamers = *Gamer::getSignedInGamersProperty();
        Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&gamer})));
        session.reset(NetworkSession::Create(NetworkSessionType::LocalWithLeaderboards,
            std::vector<SignedInGamer*>{&gamer}, 4, 0, {}));
    }
    void TearDown() override {
        if (session && !session->getIsDisposedProperty()) session->Dispose();
        session.reset();
        Gamer::setSignedInGamersProperty(new SignedInGamerCollection(previousGamers));
        Service::ResetStoreForTestingEXT();
        Service::setBackendForTesting(previousBackend);
        CNA::GamerServices::setConfigurationOverride({});
    }
    void Start() { session->StartGame(); session->Update(); }
    void End() { session->EndGame(); session->Update(); }
    LeaderboardEntry Read() {
        auto reader = LeaderboardReader::Read(board, 0, 10);
        const auto entries = reader.getEntriesProperty();
        EXPECT_EQ(1, entries.getCountProperty());
        return entries[0];
    }
    LeaderboardEntry* SignedEntry() { return gamer.getLeaderboardWriterProperty().GetLeaderboard(board); }
    LeaderboardEntry* LocalEntry() { return session->getLocalGamersProperty()[0]->getLeaderboardWriterProperty().GetLeaderboard(board); }

    SignedInGamer gamer = SignedInGamer::CreateInternal("offline_commit_tag");
    LeaderboardIdentity board = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::unique_ptr<NetworkSession> session;
    SignedInGamerCollection previousGamers = SignedInGamerCollection::CreateInternal({});
    std::shared_ptr<Service::IGamerServicesBackend> previousBackend;
};
}

TEST_F(OfflineLeaderboardCommitTest, RatingThenColumnsArePersistedBeforeGameEnded) {
    Start();
    auto* entry = SignedEntry();
    entry->setRatingProperty(4200);
    entry->getColumnsProperty().SetValue("Result", LeaderboardOutcome::Win);
    bool ended = false;
    session->GameEnded += [&](auto*, const GameEndedEventArgs&) {
        ended = true;
        auto saved = Read();
        EXPECT_EQ(4200, saved.getRatingProperty());
        EXPECT_EQ(LeaderboardOutcome::Win, saved.getColumnsProperty().GetValueOutcome("Result"));
    };
    End();
    EXPECT_TRUE(ended);
    EXPECT_EQ(NetworkSessionState::Lobby, session->getSessionStateProperty());
}

TEST_F(OfflineLeaderboardCommitTest, ReferencesRetainedBeforeStartCommitTheirFinalValues) {
    auto* entry = SignedEntry();
    auto& columns = entry->getColumnsProperty();
    columns.SetValue("Round", 1);
    auto* stream = columns.GetValueStream("Replay");
    stream->WriteByte(1);
    entry->setRatingProperty(42);
    auto& retained = columns["Round"];
    Start();
    retained = 2;
    stream->setPositionProperty(0);
    stream->WriteByte(9);
    End();
    auto saved = Read();
    EXPECT_EQ(2, saved.getColumnsProperty().GetValueInt32("Round"));
    EXPECT_EQ(9, saved.getColumnsProperty().GetValueStream("Replay")->ReadByte());
}

TEST_F(OfflineLeaderboardCommitTest, LocalNetworkGamerWriterAlsoCommitsPostRatingColumns) {
    Start();
    auto* entry = LocalEntry();
    entry->setRatingProperty(4200);
    entry->getColumnsProperty().SetValue("Result", LeaderboardOutcome::Win);
    End();
    auto saved = Read();
    EXPECT_EQ(4200, saved.getRatingProperty());
    EXPECT_EQ(LeaderboardOutcome::Win, saved.getColumnsProperty().GetValueOutcome("Result"));
}

TEST_F(OfflineLeaderboardCommitTest, UnchangedSignedInCacheCannotOverwriteLocalWriter) {
    (void)SignedEntry();
    Start();
    auto* entry = LocalEntry();
    entry->getColumnsProperty().SetValue("Result", LeaderboardOutcome::Win);
    entry->setRatingProperty(42);
    End();
    auto saved = Read();
    EXPECT_EQ(42, saved.getRatingProperty());
    EXPECT_EQ(LeaderboardOutcome::Win, saved.getColumnsProperty().GetValueOutcome("Result"));
}

TEST_F(OfflineLeaderboardCommitTest, UnchangedLocalCacheCannotOverwriteSignedInWriter) {
    (void)LocalEntry();
    Start();
    auto* entry = SignedEntry();
    entry->getColumnsProperty().SetValue("Result", LeaderboardOutcome::Win);
    entry->setRatingProperty(42);
    End();
    auto saved = Read();
    EXPECT_EQ(42, saved.getRatingProperty());
    EXPECT_EQ(LeaderboardOutcome::Win, saved.getColumnsProperty().GetValueOutcome("Result"));
}

TEST_F(OfflineLeaderboardCommitTest, MerelyOpeningWriterEntriesDoesNotCreateRows) {
    Start();
    (void)SignedEntry();
    (void)LocalEntry();
    End();
    auto reader = LeaderboardReader::Read(board, 0, 10);
    EXPECT_EQ(0, reader.getEntriesProperty().getCountProperty());
}

TEST_F(OfflineLeaderboardCommitTest, FailedFinalStreamWritePreservesStoreAndAllowsExplicitRetry) {
    Start();
    auto* entry = SignedEntry();
    auto* stream = entry->getColumnsProperty().GetValueStream("Replay");
    stream->WriteByte(7);
    entry->setRatingProperty(42);
    stream->SetLength(257);
    bool ended = false;
    session->GameEnded += [&](auto*, const GameEndedEventArgs&) { ended = true; };
    session->EndGame();
    EXPECT_THROW(session->Update(), System::ArgumentException);
    EXPECT_EQ(NetworkSessionState::Playing, session->getSessionStateProperty());
    EXPECT_FALSE(ended);
    auto previous = Read();
    EXPECT_EQ(1, previous.getColumnsProperty().GetValueStream("Replay")->getLengthProperty());
    stream->SetLength(2);
    End();
    EXPECT_TRUE(ended);
    auto saved = Read();
    EXPECT_EQ(2, saved.getColumnsProperty().GetValueStream("Replay")->getLengthProperty());
}
