// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/IServiceProvider.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include <string>
namespace CNA::Internal::GamerServices {
/** @brief Installs automatic system Guide presentation in a standard Game service container.
 * @param provider Dispatcher service provider. */
void installGuideOverlay(System::IServiceProvider& provider);
/** @brief Gets non-sensitive sign-in progress text. @return Progress label, or empty. */
std::string guideSignInStatus();
/** @brief Reports whether the Guide draws in a game (a graphics device service was found).
 * @return True when installed in a game with a graphics device service. */
bool guideOverlayAttached();
/** @brief Opens the system Guide menu for a player unless the Guide is already visible.
 * @param player Player whose Guide button was pressed. */
void openSystemGuide(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief Opens the system Guide on a new Home key or Guide-button press
 * (`CNA_GAMER_SERVICES_GUIDE_BUTTON=0` disables it). */
void pollSystemGuideButton();
}
