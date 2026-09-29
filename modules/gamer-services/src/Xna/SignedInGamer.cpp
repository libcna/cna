// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "../Internal/GuideOverlay.hpp"
#include "../Internal/Guide/GuideUi.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "System/DateTime.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/ArgumentNullException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/TimeZone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "../Internal/ServiceAsyncResult.hpp"
#include <algorithm>

namespace Microsoft::Xna::Framework::GamerServices
{
    GetTypeNameCPP(SignedInGamer, "Microsoft.Xna.Framework.GamerServices.SignedInGamer")

    System::EventHandler<SignedInEventArgs> SignedInGamer::SignedIn;
    System::EventHandler<SignedOutEventArgs> SignedInGamer::SignedOut;

    namespace
    {
        // Reference SignedIn add accessor: a new handler is told about every gamer already signed in.
        const bool signedInReplay = [] {
            SignedInGamer::SignedIn.SetReplayHook([](const System::EventHandler<SignedInEventArgs>::HandlerType& handler) {
                for (SignedInGamer* gamer : *Gamer::getSignedInGamersProperty())
                    handler(nullptr, SignedInEventArgs(gamer));
            });
            return true;
        }();
    }

    SignedInGamer::SignedInGamer(
        const std::string& gamertag,
        bool isSignedInToLive,
        bool isGuest,
        Microsoft::Xna::Framework::PlayerIndex playerIndex
    )
        : Gamer(gamertag, gamertag)
        , isGuest_(isGuest)
        , isSignedInToLive_(isSignedInToLive)
        , playerIndex_(playerIndex)
        , gameDefaults_(GameDefaults::CreateInternal())
        , presence_(GamerPresence::CreateInternal())
        , privileges_(GamerPrivileges::CreateInternal())
        , partySize_(1)
    {
    }

    SignedInGamer SignedInGamer::CreateInternal(
        const std::string& gamertag,
        bool isSignedInToLive,
        bool isGuest,
        Microsoft::Xna::Framework::PlayerIndex playerIndex
    ) {
        return SignedInGamer(gamertag, isSignedInToLive, isGuest, playerIndex);
    }

    const GameDefaults& SignedInGamer::getGameDefaultsProperty() const     { return gameDefaults_; }
    bool SignedInGamer::getIsGuestProperty() const                        { return isGuest_; }
    bool SignedInGamer::getIsSignedInToLiveProperty() const               { return isSignedInToLive_; }
    int SignedInGamer::getPartySizeProperty() const                       { return partySize_; }
    void SignedInGamer::setPartySizeProperty(int value)                  { partySize_ = value; }

    Microsoft::Xna::Framework::PlayerIndex SignedInGamer::getPlayerIndexProperty() const
    {
        return playerIndex_;
    }

    const GamerPresence& SignedInGamer::getPresenceProperty() const       { return presence_; }
    GamerPresence& SignedInGamer::getPresenceProperty()                   { return presence_; }
    const GamerPrivileges& SignedInGamer::getPrivilegesProperty() const   { return privileges_; }

    bool SignedInGamer::IsFriend(Gamer* gamer) const
    {
        // Reference SignedInGamer.IsFriend, in its validation order.
        if (getIsDisposedProperty()) throw System::ObjectDisposedException("SignedInGamer");
        if (!isSignedInToLive_) throw GamerPrivilegeException("The profile is not signed in to an online account.");
        if (gamer == nullptr) throw System::ArgumentNullException("gamer");
        if (gamer->getIsDisposedProperty()) throw System::ObjectDisposedException("gamer");
        if (!serviceUserId_.empty()) {
            for (const auto& entry : CNA::Internal::GamerServices::backend()->friends(serviceUserId_))
                if (entry.accepted && entry.gamertag == gamer->getGamertagProperty()) return true;
        }
        return false;
    }

    bool SignedInGamer::IsHeadset(const Microsoft::Xna::Framework::Audio::Microphone& microphone) const
    {
        return microphone.getIsHeadsetProperty();
    }

    FriendCollection SignedInGamer::GetFriends() const
    {
        if (getIsDisposedProperty()) throw System::ObjectDisposedException("SignedInGamer");
        if (!isSignedInToLive_) throw GamerPrivilegeException("The profile is not signed in to an online account.");
        if (!serviceUserId_.empty()) {
            std::vector<std::shared_ptr<FriendGamer>> owned;
            std::vector<FriendGamer*> friends;
            for (const auto& entry : CNA::Internal::GamerServices::backend()->friends(serviceUserId_)) {
                // Away/busy is the status the friend chose (the Guide's Online status); an online
                // CNA account is always in a game, so it is playing.
                auto friendGamer = std::shared_ptr<FriendGamer>(new FriendGamer(entry.gamertag, entry.gamertag, entry.online, entry.online, entry.away, entry.busy, entry.requestSent, entry.requestReceived));
                friendGamer->presence_ = entry.presence;
                friendGamer->isJoinable_ = entry.joinable;
                friendGamer->inviteReceivedFrom_ = entry.inviteReceivedFrom;
                friendGamer->inviteSentTo_ = entry.inviteSentTo;
                friendGamer->inviteAccepted_ = entry.inviteAccepted;
                friendGamer->inviteRejected_ = entry.inviteRejected;
                friends.push_back(friendGamer.get()); owned.push_back(std::move(friendGamer));
            }
            auto collection = FriendCollection::CreateInternal(std::move(friends));
            collection.ownedFriends_ = std::move(owned); return collection;
        }
        return FriendCollection::CreateInternal({});
    }

    void SignedInGamer::AwardAchievement(const std::string& achievementKey)
    {
        // A guest has no profile of its own to keep achievements in.
        if (isGuest_) throw GamerPrivilegeException("A guest cannot earn achievements.");
        if (!serviceUserId_.empty()) {
            const auto name = CNA::Internal::GamerServices::backend()->award(serviceUserId_, achievementKey);
            if (!name.empty()) CNA::Internal::GamerServices::GuideUi::notify({CNA::Internal::GamerServices::GuideUi::Notification::Kind::Achievement, "Achievement unlocked", name, {}});
            return;
        }
        // Offline, a title that ships an achievement catalog awards only what it defines, as the
        // service does; awarding one already earned keeps its first date.
        const auto& catalog = CNA::Internal::GamerServices::LoadOfflineAchievementCatalogEXT();
        if (catalog && std::none_of(catalog->begin(), catalog->end(), [&](const auto& entry) { return entry.Key == achievementKey; }))
            throw GamerServicesNotAvailableException("The title defines no achievement \"" + achievementKey + "\".");
        for (const auto& record : CNA::Internal::GamerServices::LoadEarnedAchievementsEXT(getGamertagProperty()))
            if (record.Key == achievementKey) return;
        CNA::Internal::GamerServices::SaveEarnedAchievementEXT(
            getGamertagProperty(), achievementKey, System::DateTime::getNowProperty().getTicksProperty()
        );
        std::string name = achievementKey;
        if (catalog)
            for (const auto& entry : *catalog) if (entry.Key == achievementKey && !entry.Name.empty()) name = entry.Name;
        CNA::Internal::GamerServices::GuideUi::notify({CNA::Internal::GamerServices::GuideUi::Notification::Kind::Achievement, "Achievement unlocked", name, {}});
    }

    System::IAsyncResult* SignedInGamer::BeginAwardAchievement(
        const std::string& achievementKey,
        System::AsyncCallback callback,
        std::any state
    ) {
        if (!serviceUserId_.empty()) {
            auto service = CNA::Internal::GamerServices::backend(); const auto user = serviceUserId_;
            return CNA::Internal::GamerServices::ServiceAsyncResult::begin("award", this,
                [user, achievementKey](auto& executor) -> std::any {
                    const auto name = executor.award(user, achievementKey);
                    if (!name.empty()) CNA::Internal::GamerServices::GuideUi::notify({CNA::Internal::GamerServices::GuideUi::Notification::Kind::Achievement, "Achievement unlocked", name, {}});
                    return {};
                }, std::move(callback), std::move(state),std::move(service));
        }
        AwardAchievement(achievementKey);
        // FNA: the overlap check on statStoreAction is intentionally a no-op — the
        // reference source's overlap guard is commented out ("FIXME: Pray...").
        statStoreAction_ = new GamerAction(std::move(state), std::move(callback));
        statStoreAction_->setIsCompletedProperty(true);
        // audit_net.md High finding: the callback used to only be stored, never invoked, despite
        // this action already completing synchronously right above. A re-entrant callback that
        // itself calls EndAwardAchievement() (which nulls statStoreAction_) must not make this
        // method return a stale null pointer, so capture the pointer to return before invoking
        // the callback, not after.
        GamerAction* action = statStoreAction_;
        if (action->Callback)
        {
            action->Callback(*action);
        }
        return action;
    }

    void SignedInGamer::EndAwardAchievement(System::IAsyncResult* result)
    {
        if (dynamic_cast<CNA::Internal::GamerServices::ServiceAsyncResult*>(result)) {
            (void)CNA::Internal::GamerServices::ServiceAsyncResult::end(result, "award", this); return;
        }
        statStoreAction_ = nullptr;
    }

    AchievementCollection SignedInGamer::GetAchievements()
    {
        System::IAsyncResult* result = BeginGetAchievements(System::AsyncCallback{}, std::any{});
        if (dynamic_cast<CNA::Internal::GamerServices::ServiceAsyncResult*>(result)) {
            std::unique_ptr<System::IAsyncResult> owned(result); return EndGetAchievements(result);
        }
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServicesDispatcher::UpdateAsync())
            {
                statReceiveAction_->setIsCompletedProperty(true);
            }
        }
        AchievementCollection achievements = EndGetAchievements(result);
        delete result; // caller-owned; FNA relies on GC, so there is no explicit dispose call
        return achievements;
    }

    System::IAsyncResult* SignedInGamer::BeginGetAchievements(System::AsyncCallback callback, std::any asyncState)
    {
        if (!serviceUserId_.empty()) {
            auto service = CNA::Internal::GamerServices::backend(); const auto user = serviceUserId_;
            return CNA::Internal::GamerServices::ServiceAsyncResult::begin("achievements", this,
                [user](auto& executor) -> std::any { return executor.achievements(user); }, std::move(callback), std::move(asyncState),std::move(service));
        }
        if (statReceiveAction_ != nullptr)
        {
            throw System::InvalidOperationException();
        }
        statReceiveAction_ = new GamerAction(std::move(asyncState), std::move(callback));
        // Task 7.1: there is no real deferred work to wait on (matching BeginAwardAchievement's
        // own already-correct pattern just above) - GetAchievements()'s polling loop only ever
        // completes the action itself via GamerServicesDispatcher::UpdateAsync() returning false,
        // which never happens once a real GamerServicesComponent exists (isInitialized_ becomes
        // permanently true, matching this project's own established "no reset hook" pattern) -
        // making that loop spin forever at 100% CPU for any real game. Mark it complete
        // immediately instead.
        statReceiveAction_->setIsCompletedProperty(true);
        // audit_net.md High finding: the callback used to only be stored, never invoked, despite
        // this action already completing synchronously right above. A re-entrant callback that
        // itself calls EndGetAchievements() (which nulls statReceiveAction_) must not make this
        // method return a stale null pointer, so capture the pointer to return before invoking
        // the callback, not after.
        GamerAction* action = statReceiveAction_;
        if (action->Callback)
        {
            action->Callback(*action);
        }
        return action;
    }

    AchievementCollection SignedInGamer::EndGetAchievements(System::IAsyncResult* result)
    {
        if (dynamic_cast<CNA::Internal::GamerServices::ServiceAsyncResult*>(result)) {
            const auto records = std::any_cast<std::vector<CNA::Internal::GamerServices::ServiceAchievement>>(
                CNA::Internal::GamerServices::ServiceAsyncResult::end(result, "achievements", this));
            std::vector<Achievement> values;
            for (const auto& record : records) {
                // The service records UTC. EarnedDateTime is local time, as .NET's FromFileTime makes
                // the Xbox achievement time and as the offline store records it (DateTime.Now).
                const auto earned = record.earnedTicks != 0
                    ? System::DateTime(record.earnedTicks, System::DateTimeKind::Utc).ToLocalTime(System::TimeZone::CurrentTimeZone())
                    : System::DateTime(0);
                auto value = Achievement::CreateInternal(record.key, record.name, record.description,
                    record.displayBeforeEarned, record.earnedTicks != 0, earned);
                value.gamerScore_ = record.score; value.howToEarn_ = record.howToEarn;value.pictureHash_=record.picture;
                values.push_back(std::move(value));
            }
            return AchievementCollection::CreateInternal(std::move(values));
        }
        statReceiveAction_ = nullptr;
        std::vector<Achievement> achievements;
        const auto earned = CNA::Internal::GamerServices::LoadEarnedAchievementsEXT(getGamertagProperty());
        if (const auto& catalog = CNA::Internal::GamerServices::LoadOfflineAchievementCatalogEXT())
        {
            // The title's catalog, in its order, each earned or not.
            for (const auto& definition : *catalog)
            {
                const auto record = std::find_if(earned.begin(), earned.end(), [&](const auto& value) { return value.Key == definition.Key; });
                const bool isEarned = record != earned.end();
                auto value = Achievement::CreateInternal(definition.Key, definition.Name, definition.Description,
                    definition.DisplayBeforeEarned, isEarned, isEarned ? System::DateTime(record->EarnedTicks) : System::DateTime(0));
                value.gamerScore_ = definition.Score;
                value.howToEarn_ = definition.HowToEarn;
                value.picturePath_ = definition.Picture;
                achievements.push_back(std::move(value));
            }
            return AchievementCollection::CreateInternal(std::move(achievements));
        }
        for (const auto& record : earned)
        {
            achievements.push_back(Achievement::CreateInternal(
                record.Key, "", "", true, true, System::DateTime(record.EarnedTicks)
            ));
        }
        return AchievementCollection::CreateInternal(std::move(achievements));
    }

    void SignedInGamer::OnSignIn(SignedInGamer* gamer)
    {
        CNA::Internal::GamerServices::GuideUi::notify({CNA::Internal::GamerServices::GuideUi::Notification::Kind::SignIn, gamer->getGamertagProperty() + " signed in", {}, {}});
        if (!SignedIn.Empty())
        {
            SignedIn.Raise(nullptr, SignedInEventArgs(gamer));
        }
    }

    void SignedInGamer::OnSignOut(SignedInGamer* gamer)
    {
        CNA::Internal::GamerServices::GuideUi::notify({CNA::Internal::GamerServices::GuideUi::Notification::Kind::SignOut, gamer->getGamertagProperty() + " signed out", {}, {}});
        if (!SignedOut.Empty())
        {
            SignedOut.Raise(nullptr, SignedOutEventArgs(gamer));
        }
    }
}
