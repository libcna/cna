// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/AsyncCallback.hpp"
#include "System/IAsyncResult.hpp"
#include "System/IServiceProvider.hpp"
#include "Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include <any>
#include <string>
#include <vector>
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
/** @brief Reports whether any Guide pane is up, without the public getter's initialization check
 * (the reference's IsVisibleNoThrow). @return True while a message box, keyboard, sign-in or
 * social pane is visible. */
bool guideIsVisible();
/** @brief Opens the keyboard pane for the Guide's own screens, which the public argument and
 * visibility rules (Guide::BeginShowKeyboardInput) do not bind.
 * @param player Player the pane belongs to. @param title Title. @param description Prompt.
 * @param defaultText Initial text. @param callback Completion callback. @param state Caller state.
 * @param usePasswordMode Mask typed characters. @return Result the callback receives. */
System::IAsyncResult* showGuideKeyboardInput(Microsoft::Xna::Framework::PlayerIndex player,const std::string& title,
    const std::string& description,const std::string& defaultText,System::AsyncCallback callback,std::any state,
    bool usePasswordMode=false);
/** @brief Opens a message box for the Guide's own panes, which the public argument rules
 * (Guide::BeginShowMessageBox: one to three buttons, text under 256 characters) do not bind.
 * @param player Player the pane belongs to. @param title Title. @param text Body text.
 * @param buttons At least one caption. @param focusButton Initially focused button.
 * @param icon Icon. @param callback Completion callback. @param state Caller state.
 * @return Result the callback receives. */
System::IAsyncResult* showGuideMessageBox(Microsoft::Xna::Framework::PlayerIndex player,const std::string& title,const std::string& text,
    const std::vector<std::string>& buttons,int focusButton,Microsoft::Xna::Framework::GamerServices::MessageBoxIcon icon,
    System::AsyncCallback callback,std::any state);
}
