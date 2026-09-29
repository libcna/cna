// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/AsyncCallback.hpp"
#include "CNA/Internal/Runtime/IModalFrames.hpp"
#include "System/IAsyncResult.hpp"
#include "System/IServiceProvider.hpp"
#include "Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NotificationPosition.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include <any>
#include <chrono>
#include <functional>
#include <string>
#include <vector>
namespace CNA::Internal::GamerServices {
/** @brief Installs automatic system Guide presentation in a standard Game service container.
 * @param provider Dispatcher service provider. */
void installGuideOverlay(System::IServiceProvider& provider);
/** @brief How long one Guide notification stays on screen. */
inline constexpr std::chrono::seconds GuideNotificationDuration{4};
/** @brief Queues a Guide notification toast ("Alice signed in", "Achievement unlocked"), drawn over
 * the game at Guide.NotificationPosition without taking input; any thread. @param text Message. */
void postGuideNotification(std::string text);
/** @brief Gets the notifications showing or waiting, oldest (the one on screen) first.
 * @return Texts. */
std::vector<std::string> guideNotifications();
/** @brief Where a notification box sits for a position, inside a five-percent title-safe margin.
 * @param position Guide.NotificationPosition. @param screenWidth Back buffer width.
 * @param screenHeight Back buffer height. @param width Box width. @param height Box height.
 * @return Top-left corner. */
Microsoft::Xna::Framework::Point guideNotificationOrigin(Microsoft::Xna::Framework::GamerServices::NotificationPosition position,
    int screenWidth,int screenHeight,int width,int height);
/** @brief Replaces the notification clock for tests. @param clock Clock, or empty for steady_clock. */
void setGuideNotificationClockForTesting(std::function<std::chrono::steady_clock::time_point()> clock);
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
/** @brief Gets the frames a Guide End call runs while it waits for an answer: the live game's,
 * unless a test installed its own. @return The frames, or null without a game. */
CNA::Internal::Runtime::IModalFrames* guideModalFrames();
/** @brief Replaces the live game's modal frames for tests. @param frames Frames, or null to restore. */
void setGuideModalFramesForTesting(CNA::Internal::Runtime::IModalFrames* frames);
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
