// SPDX-License-Identifier: MS-PL
#pragma once
// What the Guide UI reads from the rest of GamerServices: the rail's identity, the game's pending
// message box and keyboard (owned by the XNA Guide), and the notification queue.
#include "GuideUi.hpp"
#include "Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include <optional>
#include <string>
#include <vector>

namespace CNA::Internal::GamerServices::GuideUi {
/** @brief The signed-in player the rail shows. */
struct Identity {
    /** @brief Service identity (empty for a local profile). */
    std::string userId;
    /** @brief Gamertag (empty when nobody is signed in). */
    std::string gamertag;
    /** @brief Avatar description (empty until read, or none). */
    std::vector<unsigned char> avatar;
    /** @brief Gamerscore. */
    int gamerScore=0;
    /** @brief Signed in to the service. */
    bool online=false;
    /** @brief A guest of a signed-in account. */
    bool guest=false;
    /** @brief "online", "away" or "busy". */
    std::string status="online";
    /** @brief Unread messages. */
    int unread=0;
};
/** @brief A player's rail identity. @param player Player. @return Identity. */
Identity& identity(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief Reads a player's identity again (gamertag now; score, avatar, unread count from the
 * service or local profile at the next updates). @param player Player. */
void refreshIdentity(Microsoft::Xna::Framework::PlayerIndex player);

/** @brief The game's pending message box. */
struct MessageBoxView {
    /** @brief Title. */
    std::string title;
    /** @brief Body. */
    std::string text;
    /** @brief Button captions. */
    std::vector<std::string> buttons;
    /** @brief Focused button. */
    int focus=0;
    /** @brief Icon. */
    Microsoft::Xna::Framework::GamerServices::MessageBoxIcon icon=Microsoft::Xna::Framework::GamerServices::MessageBoxIcon::None;
};
/** @brief The pending message box, if any. @return View. */
std::optional<MessageBoxView> pendingMessageBox();
/** @brief Moves its focus. @param button Button. */
void focusMessageBox(int button);
/** @brief Answers it (no button: cancelled). @param button Button. */
void answerMessageBox(std::optional<int> button);

/** @brief The game's pending keyboard input. */
struct KeyboardView {
    /** @brief Title. */
    std::string title;
    /** @brief Description. */
    std::string description;
    /** @brief Text as shown (masked in password mode). */
    std::string text;
};
/** @brief Lifts the touch withhold once the click that answered a message box is released. */
void releaseTouchSuppression();

/** @brief The pending keyboard input, if any. @return View. */
std::optional<KeyboardView> pendingKeyboard();
/** @brief Types one character into it as a real key would ('\b' deletes, '\r' commits).
 * @param character Character. */
void typeKeyboard(char16_t character);
/** @brief Cancels it. */
void cancelKeyboard();

/** @brief The notification on screen. */
struct GuideToast {
    /** @brief What it says. */
    Notification notification;
    /** @brief Seconds it has been showing. */
    double age=0;
};
/** @brief The notification on screen now, if any (advances the queue). @return Toast. */
std::optional<GuideToast> currentGuideToast();
}
