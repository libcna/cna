// SPDX-License-Identifier: MS-PL
#pragma once
// Shared by the networking demos: networking needs a signed-in gamer, and an XNA game gets one
// from the Guide. Without a CNA account service the Guide signs in local offline profiles;
// CNA_GAMER_SERVICES_AUTO_SIGN_IN signs them in at startup for unattended runs.

#include <chrono>
#include <thread>

#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"

namespace CNAExamplesEXT
{
    /** @brief For a game's Update: the first signed-in gamer, or null after asking the Guide for one. */
    inline Microsoft::Xna::Framework::GamerServices::SignedInGamer* SignedInGamerOrShowSignInEXT()
    {
        using namespace Microsoft::Xna::Framework::GamerServices;
        auto* gamers = Gamer::getSignedInGamersProperty();
        if (gamers->getCountProperty() > 0)
        {
            return (*gamers)[0];
        }
        if (!Guide::getIsVisibleProperty())
        {
            Guide::ShowSignIn(1, false);
        }
        return nullptr;
    }

    /** @brief For a console program, which cannot show the Guide: waits for automatic sign-in. */
    inline Microsoft::Xna::Framework::GamerServices::SignedInGamer* WaitForSignedInGamerEXT()
    {
        using namespace Microsoft::Xna::Framework::GamerServices;
        auto* gamers = Gamer::getSignedInGamersProperty();
        for (int attempt = 0; attempt < 200 && gamers->getCountProperty() == 0; ++attempt)
        {
            GamerServicesDispatcher::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return gamers->getCountProperty() > 0 ? (*gamers)[0] : nullptr;
    }

    /** @brief The message a demo prints when no gamer signed in. */
    inline constexpr const char* kNoSignedInGamerEXT =
        "Nobody is signed in. Sign in through the Guide, or set CNA_GAMER_SERVICES_AUTO_SIGN_IN=<profile name>.";
}
