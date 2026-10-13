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

namespace Microsoft::Xna::Framework::GamerServices
{
    void Guide::ShowMarketplace(PlayerIndex player)
    {
        // Reference: a signed-in LIVE profile with the purchase privilege, else GamerPrivilegeException.
        SignedInGamer* gamer=nullptr;
        for(auto* candidate:*Gamer::getSignedInGamersProperty())if(candidate->getPlayerIndexProperty()==player)gamer=candidate;
        if(!gamer||!gamer->getIsSignedInToLiveProperty())throw GamerPrivilegeException("The profile is not signed in.");
        if(!gamer->getPrivilegesProperty().getAllowPurchaseContentProperty())throw GamerPrivilegeException("The profile may not purchase content.");
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::contentScreen(player),player);
        // A game simulating trial mode gets XNA's purchase emulation straight away.
        if(simulateTrialMode_)CNA::Internal::GamerServices::GuideUi::push(CNA::Internal::GamerServices::GuideUi::testPurchaseScreen(player));
    }
}
