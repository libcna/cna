// SPDX-License-Identifier: MS-PL
// Guide.ShowSignIn and the sign-in state the dispatcher reports back to (OnSignInResult).
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "../Internal/GuideOverlay.hpp"
#include "../Internal/Guide/GuideScreen.hpp"
#include "System/ArgumentException.hpp"
#include <cctype>
#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    namespace {
        int signInPaneCount = 0;
        int signInSlot = 0;
        bool signInLocal = false;
        // ShowSignIn(onlineOnly: true): further players may sign in as guests of a signed-in account.
        bool signInOnlineOnly = false;
        // The account a guest joins as (the lowest-numbered signed-in account), or null.
        SignedInGamer* GuestHost() {
            SignedInGamer* host=nullptr;
            for(auto* gamer:*Gamer::getSignedInGamersProperty())
                if(gamer->getIsSignedInToLiveProperty()&&!gamer->getIsGuestProperty()&&
                   (!host||gamer->getPlayerIndexProperty()<host->getPlayerIndexProperty()))host=gamer;
            return host;
        }
        // Xbox names a guest after its account: "Alice (1)", "Alice (2)".
        std::string GuestName(const SignedInGamer& host) {
            for(int number=1;;++number) {
                const auto name=host.getGamertagProperty()+" ("+std::to_string(number)+")";
                bool taken=false;
                for(auto* gamer:*Gamer::getSignedInGamersProperty())if(gamer->getGamertagProperty()==name)taken=true;
                if(!taken)return name;
            }
        }
        std::string Folded(std::string value) {
            for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }
        bool ProfileSignedIn(const std::string& gamertag) {
            for(auto* gamer:*Gamer::getSignedInGamersProperty())
                if(Folded(gamer->getGamertagProperty())==Folded(gamertag))return true;
            return false;
        }
        void EndLocalSignIn(const std::string& message) {
            CNA::Internal::GamerServices::setGuideSignInActive(false); CNA::Internal::GamerServices::GuideUi::closeAll(); CNA::Internal::GamerServices::syncGuideTouchInput();
            (void)CNA::Internal::GamerServices::showGuideMessageBox(static_cast<PlayerIndex>(signInSlot), "Sign in", message, {"OK"}, 0, MessageBoxIcon::Error,
                [](System::IAsyncResult& result) { std::unique_ptr<System::IAsyncResult> owned(&result); (void)Guide::EndShowMessageBox(&result); }, {});
        }
        // Without a service, a local offline profile, created on first use.
        void SignInLocalProfile(std::string name) {
            namespace Service = CNA::Internal::GamerServices;
            const auto first = name.find_first_not_of(' '), last = name.find_last_not_of(' ');
            name = first == std::string::npos ? std::string{} : name.substr(first, last - first + 1);
            if (!Service::isValidLocalGamertag(name)) {
                EndLocalSignIn("Profile names are 1 to 15 letters, digits and single spaces, starting with a letter."); return;
            }
            if (ProfileSignedIn(name)) { EndLocalSignIn("That profile is already signed in."); return; }
            const auto profile = Service::openLocalProfile(name);
            try { Service::backend()->signInLocal(signInSlot, profile.gamertag); }
            catch (...) { CNA::Internal::GamerServices::setGuideSignInActive(false); Service::GuideUi::closeAll(); CNA::Internal::GamerServices::syncGuideTouchInput(); throw; }
            CNA::Internal::GamerServices::syncGuideTouchInput();
        }
        void StartSignInPane() {
            namespace Service = CNA::Internal::GamerServices;
            auto occupied=[](int slot) {
                for(auto* gamer:*Gamer::getSignedInGamersProperty())
                    if(gamer->getPlayerIndexProperty()==static_cast<PlayerIndex>(slot))return true;
                return false;
            };
            while (signInSlot < signInPaneCount && occupied(signInSlot)) ++signInSlot;
            if (signInSlot >= signInPaneCount) { CNA::Internal::GamerServices::setGuideSignInActive(false); Service::GuideUi::closeAll(); CNA::Internal::GamerServices::syncGuideTouchInput(); return; }
            // The console's sign-in: the player slots and what this player may sign in as.
            Service::GuideUi::SignInRequest request;
            request.slot = signInSlot; request.panes = signInPaneCount; request.local = signInLocal;
            if (signInLocal)
                for (const auto& profile : Service::loadLocalProfiles())
                    if (!ProfileSignedIn(profile.gamertag)) request.profiles.push_back(profile.gamertag);
            if (auto* host = signInOnlineOnly ? GuestHost() : nullptr) request.guestHost = host->getGamertagProperty();
            Service::GuideUi::SignInHandlers handlers;
            handlers.local = [](const std::string& name) { SignInLocalProfile(name); };
            handlers.account = [](const std::string& username, std::string password) {
                try { Service::backend()->signIn(signInSlot, username, std::move(password)); }
                catch (...) { CNA::Internal::GamerServices::setGuideSignInActive(false); Service::GuideUi::closeAll(); CNA::Internal::GamerServices::syncGuideTouchInput(); throw; }
            };
            handlers.guest = [] {
                // A guest needs no password: it plays on its account's sign-in.
                if (auto* host = GuestHost())
                    Service::backend()->signInGuest(signInSlot, GuestName(*host), static_cast<int>(host->getPlayerIndexProperty()));
            };
            handlers.cancel = [] { CNA::Internal::GamerServices::setGuideSignInActive(false); CNA::Internal::GamerServices::syncGuideTouchInput(); };
            Service::GuideUi::open(Service::GuideUi::signInScreen(static_cast<PlayerIndex>(signInSlot), std::move(request), std::move(handlers)),
                static_cast<PlayerIndex>(signInSlot));
        }
    }

    void Guide::ShowSignIn(int paneCount, bool onlineOnly) {
        // Xbox 360 documentation accepts 1, 2, 4; Windows stub IL is not the target.
        if (paneCount != 1 && paneCount != 2 && paneCount != 4) throw System::ArgumentException("paneCount must be 1, 2 or 4.", "paneCount");
        if (CNA::Internal::GamerServices::guideIsVisible()) throw GuideAlreadyVisibleException();
        if (!GamerServicesDispatcher::getIsInitializedProperty())
            throw GamerServicesNotAvailableException("Gamer services are not initialized.");
        // Without a service only local offline profiles exist, which an online-only sign-in excludes.
        const bool service = CNA::Internal::GamerServices::backend()->serviceEnabled();
        if (!service && onlineOnly) throw GamerServicesNotAvailableException("No CNA account service is configured.");
        signInPaneCount = paneCount; signInSlot = 0; CNA::Internal::GamerServices::setGuideSignInActive(true); signInLocal = !service; signInOnlineOnly = onlineOnly;
        try { StartSignInPane(); } catch (...) { CNA::Internal::GamerServices::setGuideSignInActive(false); CNA::Internal::GamerServices::syncGuideTouchInput(); throw; }
    }
    void Guide::OnSignInResult(int slot, bool success, const std::string& reason) {
        if (!CNA::Internal::GamerServices::guideSignInActive() || slot != signInSlot) return;
        if (signInLocal && !success) return;
        if (success) { ++signInSlot; StartSignInPane(); }
        else {
            CNA::Internal::GamerServices::setGuideSignInActive(false);
            CNA::Internal::GamerServices::GuideUi::closeAll();
            // XNA's LIVEnTitleUpdateRequired, in CNA's words.
            const char* text = reason == "UPDATE_REQUIRED"
                ? "This version of the game is no longer supported by CNA Gamer Services. Install the latest version to sign in."
                : "Sign-in failed. Check the account and service connection.";
            (void)CNA::Internal::GamerServices::showGuideMessageBox(static_cast<PlayerIndex>(slot), "CNA Gamer Services", text,
                {"OK"}, 0, MessageBoxIcon::Error, [](System::IAsyncResult& result) {
                    std::unique_ptr<System::IAsyncResult> owned(&result); (void)Guide::EndShowMessageBox(&result);
                }, {});
        }
        CNA::Internal::GamerServices::syncGuideTouchInput();
    }
}
