// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/NotSupportedException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    SignedInGamerCollection* Gamer::signedInGamers_ = nullptr;

    Gamer::Gamer(const std::string& gamertag, std::optional<std::string> displayName)
        // Task 10.1: matches FNA's `displayName ?? gamertag` exactly - substitutes only when
        // displayName was never supplied, never for an explicitly-passed empty string.
        : displayName_(displayName.has_value() ? std::move(*displayName) : gamertag)
        , gamertag_(gamertag)
        // Task 4.3 (plans/plan_net.md Phase 4): LeaderboardWriter needs to know its owning gamer to
        // persist to the right per-gamer local store entry - `this` is a valid pointer value in a
        // member-initializer-list (LeaderboardWriter's constructor only stores it, never
        // dereferences it before Gamer itself is fully constructed).
        , leaderboardWriter_(this)
    {
    }

    const std::string& Gamer::getDisplayNameProperty() const  { return displayName_; }
    void Gamer::setDisplayNameProperty(const std::string& v)  { displayName_ = v; }
    const std::string& Gamer::getGamertagProperty() const     { return gamertag_; }
    bool Gamer::getIsDisposedProperty() const                 { return isDisposed_; }
    LeaderboardWriter& Gamer::getLeaderboardWriterProperty()  { return leaderboardWriter_; }
    std::any& Gamer::getTagProperty()                         { return tag_; }
    void Gamer::setTagProperty(const std::any& v)             { tag_ = v; }

    SignedInGamerCollection* Gamer::getSignedInGamersProperty()
    {
        if (!signedInGamers_)
            signedInGamers_ = new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}));
        return signedInGamers_;
    }

    void Gamer::setSignedInGamersProperty(SignedInGamerCollection* value)
    {
        if (signedInGamers_ != value)
        {
            delete signedInGamers_;
            signedInGamers_ = value;
        }
    }

    std::string Gamer::ToString() const
    {
        return displayName_;
    }

    GamerProfile* Gamer::GetProfile()
    {
        System::IAsyncResult* result = BeginGetProfile(System::AsyncCallback{}, std::any{});
        // Downcast: sharp-runtime's IAsyncResult lacks AsyncWaitHandle (unlike real .NET);
        // Gamer always constructs the concrete GamerAction below, which does expose it.
        static_cast<GamerAction*>(result)->getAsyncWaitHandleProperty().WaitOne();
        GamerProfile* profile = EndGetProfile(result);
        delete result; // caller-owned; FNA relies on GC, so there is no explicit dispose call
        return profile;
    }

    System::IAsyncResult* Gamer::BeginGetProfile(System::AsyncCallback callback, std::any asyncState)
    {
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

    GamerProfile* Gamer::EndGetProfile(System::IAsyncResult* /*result*/)
    {
        return new GamerProfile(GamerProfile::CreateInternal());
    }

    Gamer* Gamer::GetFromGamertag(const std::string& /*gamertag*/)
    {
        throw System::NotSupportedException();
    }

    System::IAsyncResult* Gamer::BeginGetFromGamertag(
        const std::string& /*gamertag*/,
        System::AsyncCallback /*callback*/,
        std::any /*asyncState*/
    ) {
        throw System::NotSupportedException();
    }

    Gamer* Gamer::EndGetFromGamertag(System::IAsyncResult* /*result*/)
    {
        throw System::NotSupportedException();
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
    bool Gamer::GamerAction::getCompletedSynchronouslyProperty() const  { return false; }
    bool Gamer::GamerAction::getIsCompletedProperty() const             { return isCompleted_; }
    void Gamer::GamerAction::setIsCompletedProperty(bool value)         { isCompleted_ = value; }

    System::Threading::WaitHandle& Gamer::GamerAction::getAsyncWaitHandleProperty() const
    {
        return asyncWaitHandle_;
    }
}
