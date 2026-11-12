// SPDX-License-Identifier: MS-PL
// Guide.ShowMarketplace. Opens the game content panel when a signed-in profile may buy; a game
// that only checks the license links this and the Guide's frame, not the rest of the system Guide.
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "../Internal/GuideOverlay.hpp"
#include "../Internal/Guide/GuideScreen.hpp"

// XNA Refresh ErrorHandler.GetExceptionFromResult IL_0105..010f supplies the visible-Guide diagnostic.
namespace Microsoft::Xna::Framework::GamerServices
{
    void Guide::ShowMarketplace(PlayerIndex player)
    {
        // XNA Refresh ShowMarketplace IL_0017..0039: ProfileNotSignedIn / ProfileNotPrivileged.
        SignedInGamer* gamer=nullptr;
        for(auto* candidate:*Gamer::getSignedInGamersProperty())if(candidate->getPlayerIndexProperty()==player)gamer=candidate;
        if(!gamer||!gamer->getIsSignedInToLiveProperty())throw GamerPrivilegeException("A signed in gamer profile is required to perform this operation. A LIVE profile may also be required. There are no profiles currently signed in, or the profile is not signed in to LIVE.");
        if(!gamer->getPrivilegesProperty().getAllowPurchaseContentProperty())throw GamerPrivilegeException("The current gamer profile does not have suitable privileges to perform this operation. You may require a LIVE Gold account, or need to change your parental control settings.");
        // XNA Refresh ShowMarketplace IL_0039-003e: then the kernel packet, which needs initialized
        // gamer services (owner decision 2026-11-12).
        CNA::Internal::GamerServices::throwIfGamerServicesNotInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::contentScreen(player),player);
        // A game simulating trial mode gets XNA's purchase emulation straight away.
        if(simulateTrialMode_)CNA::Internal::GamerServices::GuideUi::push(CNA::Internal::GamerServices::GuideUi::testPurchaseScreen(player));
    }
}
