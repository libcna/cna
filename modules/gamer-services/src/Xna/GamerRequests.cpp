// SPDX-License-Identifier: MS-PL
// Gamer's service requests: profiles, lookups by gamertag, partner tokens.
//
// Link boundary: Gamer's identity and the signed-in collection (Gamer.cpp) are read by the Guide's
// license and marketplace checks; these reach the service client and are linked only by a game
// that makes them.
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/NotSupportedException.hpp"
#include "../Internal/ServiceAsyncResult.hpp"

#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GamerProfile* Gamer::GetProfile()
    {
        System::IAsyncResult* result = BeginGetProfile(System::AsyncCallback{}, std::any{});
        // Downcast: sharp-runtime's IAsyncResult lacks AsyncWaitHandle (unlike real .NET);
        // Gamer always constructs the concrete GamerAction below, which does expose it.
        if (dynamic_cast<CNA::Internal::GamerServices::ServiceAsyncResult*>(result) == nullptr)
            static_cast<GamerAction*>(result)->getAsyncWaitHandleProperty().WaitOne();
        std::unique_ptr<System::IAsyncResult> owned(result);
        return EndGetProfile(result);
    }

    System::IAsyncResult* Gamer::BeginGetProfile(System::AsyncCallback callback, std::any asyncState)
    {
        if (isDisposed_) throw System::ObjectDisposedException("Gamer");
        if (CNA::Internal::GamerServices::backend()->serviceEnabled()) {
            auto service = CNA::Internal::GamerServices::backend();
            const auto tag = gamertag_;
            return CNA::Internal::GamerServices::ServiceAsyncResult::begin("profile", this,
                [tag](auto& executor) -> std::any { return executor.profile(tag); }, std::move(callback), std::move(asyncState),std::move(service));
        }
        auto* action = new GamerAction(std::move(asyncState), std::move(callback));
        action->setIsCompletedProperty(true);
        // audit_net.md High finding: the callback used to only be stored, never invoked, despite
        // this action already completing synchronously right above.
        if (action->Callback)
        {
            action->Callback(*action);
        }
        return action;
    }

    GamerProfile* Gamer::EndGetProfile(System::IAsyncResult* result)
    {
        if (dynamic_cast<CNA::Internal::GamerServices::ServiceAsyncResult*>(result)) {
            CNA::Internal::GamerServices::ServiceIdentity person;
            try {
                person = std::any_cast<CNA::Internal::GamerServices::ServiceIdentity>(
                    CNA::Internal::GamerServices::ServiceAsyncResult::end(result, "profile", this));
            } catch (const CNA::Internal::GamerServices::ServiceOperationError& error) {
                // XNA ErrorHandler maps ProfileNotSignedIn to GamerPrivilegeException;
                // a queued read can lose authentication before the worker executes it.
                if (error.code == "UNAUTHENTICATED")
                    throw GamerPrivilegeException("The profile is not signed in to an online account.");
                throw;
            }
            auto profile = std::make_unique<GamerProfile>(GamerProfile::CreateInternal());
            profile->pictureHash_=person.picture;profile->motto_ = person.motto; profile->gamerScore_ = person.gamerScore;
            profile->totalAchievements_ = person.totalAchievements;
            profile->titlesPlayed_ = person.titlesPlayed;
            profile->region_ = System::Globalization::RegionInfo(person.region);
            profile->gamerZone_ = static_cast<GamerZone>(person.gamerZone);
            // No reviews, no reputation: XNA's unset 0, never an invented rating.
            profile->reputation_ = person.reputation.value_or(0.0f);
            return profile.release();
        }
        auto profile = std::make_unique<GamerProfile>(GamerProfile::CreateInternal());
        // An offline profile's totals come from this title's local store; a remote gamer's progress
        // is not on this machine.
        if (dynamic_cast<SignedInGamer*>(this) != nullptr)
        {
            const auto earned = CNA::Internal::GamerServices::LoadEarnedAchievementsEXT(gamertag_);
            const auto& catalog = CNA::Internal::GamerServices::LoadOfflineAchievementCatalogEXT();
            for (const auto& record : earned)
            {
                if (!catalog) { ++profile->totalAchievements_; continue; }
                for (const auto& definition : *catalog)
                    if (definition.Key == record.Key) { ++profile->totalAchievements_; profile->gamerScore_ += definition.Score; }
            }
            profile->titlesPlayed_ = earned.empty() ? 0 : 1;
        }
        return profile.release();
    }

    Gamer* Gamer::GetFromGamertag(const std::string& gamertag) {
        std::unique_ptr<System::IAsyncResult> result(BeginGetFromGamertag(gamertag, {}, {}));
        return EndGetFromGamertag(result.get());
    }
    System::IAsyncResult* Gamer::BeginGetFromGamertag(const std::string& gamertag,System::AsyncCallback callback,std::any asyncState) {
        if (!CNA::Internal::GamerServices::backend()->serviceEnabled()) throw System::NotSupportedException();
        if (gamertag.empty() || gamertag.size() > 32) throw System::ArgumentException("Invalid gamertag.", "gamertag");
        auto service = CNA::Internal::GamerServices::backend();
        return CNA::Internal::GamerServices::ServiceAsyncResult::begin("lookup", nullptr,
            [gamertag](auto& executor) -> std::any { return executor.profile(gamertag); }, std::move(callback), std::move(asyncState),std::move(service));
    }
    Gamer* Gamer::EndGetFromGamertag(System::IAsyncResult* result) {
        if (!CNA::Internal::GamerServices::backend()->serviceEnabled()) throw System::NotSupportedException();
        const auto person = std::any_cast<CNA::Internal::GamerServices::ServiceIdentity>(
            CNA::Internal::GamerServices::ServiceAsyncResult::end(result, "lookup", nullptr));
        auto gamer = std::unique_ptr<Gamer>(new Gamer(person.gamertag, person.displayName));
        gamer->serviceUserId_ = person.userId;
        return gamer.release();
    }

    std::string Gamer::GetPartnerToken(const std::string& /*audienceUri*/)
    {
        throw System::NotSupportedException();
    }

    System::IAsyncResult* Gamer::BeginGetPartnerToken(
        const std::string& /*audienceUri*/,
        System::AsyncCallback /*callback*/,
        std::any /*asyncState*/
    ) {
        throw System::NotSupportedException();
    }

    std::string Gamer::EndGetPartnerToken(System::IAsyncResult* /*result*/)
    {
        throw System::NotSupportedException();
    }

    Gamer::GamerAction::GamerAction(std::any state, System::AsyncCallback callback)
        : Callback(std::move(callback))
        , asyncState_(std::move(state))
        , asyncWaitHandle_(true, System::Threading::EventResetMode::ManualReset)
    {
    }

    const std::any& Gamer::GamerAction::getAsyncStateProperty() const  { return asyncState_; }
    bool Gamer::GamerAction::getCompletedSynchronouslyProperty() const  { return completedSynchronously_; }
    bool Gamer::GamerAction::getIsCompletedProperty() const             { return isCompleted_; }
    void Gamer::GamerAction::setIsCompletedProperty(bool value)
    {
        // Every GamerAction is completed by the Begin call that created it (the offline store has no
        // deferred work), so completing it is completing it synchronously.
        if (value && !isCompleted_) completedSynchronously_ = true;
        isCompleted_ = value;
    }

    System::Threading::WaitHandle& Gamer::GamerAction::getAsyncWaitHandleProperty() const
    {
        return asyncWaitHandle_;
    }
}
