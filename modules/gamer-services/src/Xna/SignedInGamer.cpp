// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "../Internal/GuideOverlay.hpp"
#include "../Internal/Guide/GuideUi.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
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

    const GameDefaults& SignedInGamer::getGameDefaultsProperty() const     { return gameDefaults_; }
    bool SignedInGamer::getIsGuestProperty() const                        { return isGuest_; }
    bool SignedInGamer::getIsSignedInToLiveProperty() const               { return isSignedInToLive_; }
    int SignedInGamer::getPartySizeProperty() const                       { return partySize_; }

    Microsoft::Xna::Framework::PlayerIndex SignedInGamer::getPlayerIndexProperty() const
    {
        return playerIndex_;
    }

    const GamerPresence& SignedInGamer::getPresenceProperty() const       { return presence_; }
    GamerPresence& SignedInGamer::getPresenceProperty()                   { return presence_; }
    const GamerPrivileges& SignedInGamer::getPrivilegesProperty() const   { return privileges_; }

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
