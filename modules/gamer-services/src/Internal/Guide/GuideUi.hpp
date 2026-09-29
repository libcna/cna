// SPDX-License-Identifier: MS-PL
#pragma once
// The CNA system UI: a stack of Guide screens drawn over the game in one visual language (panel
// shell with an identity rail, dialogs, toasts). The XNA Guide calls open screens here; every
// screen reads the service asynchronously, so the game never waits on the network to draw.
#include "Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
class GraphicsDevice;
class SpriteBatch;
}

namespace CNA::Internal::GamerServices::GuideUi {
/** @brief A navigation command, whatever the device (keyboard, controller, mouse). */
enum class Command : std::uint8_t { Up, Down, Left, Right, Accept, Back, X, Y, Previous, Next };

/** @brief A system screen (implemented in GuideScreens.cpp). */
class Screen;

/** @brief Replaces whatever Guide screens are up with one, for a player (the Guide.Show* calls).
 * @param screen Screen. @param player Player the Guide belongs to. */
void open(std::shared_ptr<Screen> screen,Microsoft::Xna::Framework::PlayerIndex player);
/** @brief Whether any Guide screen is up. @return Visible. */
bool visible();
/** @brief Closes every Guide screen. */
void closeAll();
/** @brief Draws the system UI over the game: avatar views, dim, screens, the game's message box
 * and keyboard, notifications. Handles their input. Call once per frame after the game's Draw.
 * @param device Device. */
void draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
/** @brief Draws only the game's message box and keyboard (a game that draws the Guide itself).
 * @param device Device. @param batch A begun SpriteBatch. */
void drawGameDialogs(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,Microsoft::Xna::Framework::Graphics::SpriteBatch& batch);
/** @brief Releases device resources (device lost or reset). */
void releaseDeviceResources();

/** @brief Screen factories used by the XNA Guide calls. @param player Player. @return Screen. */
std::shared_ptr<Screen> homeScreen(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief The friends list. @param player Player. @return Screen. */
std::shared_ptr<Screen> friendsScreen(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief A gamer card. @param player Player. @param gamertag Whose. @return Screen. */
std::shared_ptr<Screen> gamerCardScreen(Microsoft::Xna::Framework::PlayerIndex player,std::string gamertag);
/** @brief The inbox. @param player Player. @return Screen. */
std::shared_ptr<Screen> messagesScreen(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief This title's achievements. @param player Player. @return Screen. */
std::shared_ptr<Screen> achievementsScreen(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief Recently met players. @param player Player. @return Screen. */
std::shared_ptr<Screen> playersScreen(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief A player review. @param player Player. @param gamertag Whom. @return Screen. */
std::shared_ptr<Screen> reviewScreen(Microsoft::Xna::Framework::PlayerIndex player,std::string gamertag);
/** @brief Invite friends to the current online session. @param player Player.
 * @param preselected Gamertags already chosen. @return Screen. */
std::shared_ptr<Screen> inviteScreen(Microsoft::Xna::Framework::PlayerIndex player,std::vector<std::string> preselected);
/** @brief Online status and gamer zone. @param player Player. @return Screen. */
std::shared_ptr<Screen> settingsScreen(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief The party. @param player Player. @return Screen. */
std::shared_ptr<Screen> partyScreen(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief This title's content, updates and avatar catalogs. @param player Player. @return Screen. */
std::shared_ptr<Screen> contentScreen(Microsoft::Xna::Framework::PlayerIndex player);
/** @brief A received game invitation. @param player Recipient's player. @param sender Sender gamertag.
 * @param senderId Sender service identity. @param detail Session description.
 * @param answer Called once: true (accept), false (decline), or empty (closed unanswered).
 * @return Screen. */
std::shared_ptr<Screen> invitationScreen(Microsoft::Xna::Framework::PlayerIndex player,std::string sender,std::string senderId,
    std::string detail,std::function<void(std::optional<bool>)> answer);

/** @brief What one player's sign-in may choose from. */
struct SignInRequest {
    /** @brief The slot being signed in (0..3). */
    int slot=0;
    /** @brief Slots the game asked for (1, 2 or 4). */
    int panes=1;
    /** @brief Offline: local profiles only. */
    bool local=false;
    /** @brief The account a guest would join as (online-only sign-in), or empty. */
    std::string guestHost;
    /** @brief Local profiles not yet signed in. */
    std::vector<std::string> profiles;
};
/** @brief What a sign-in choice does (the XNA Guide's sign-in state). */
struct SignInHandlers {
    /** @brief Sign in (or create) a local profile by name. */
    std::function<void(const std::string&)> local;
    /** @brief Sign in to an account. */
    std::function<void(const std::string&,std::string)> account;
    /** @brief Sign in as a guest of guestHost. */
    std::function<void()> guest;
    /** @brief The player cancelled. */
    std::function<void()> cancel;
};
/** @brief The sign-in picker for one slot. @param player Slot's player. @param request Choices.
 * @param handlers What they do. @return Screen. */
std::shared_ptr<Screen> signInScreen(Microsoft::Xna::Framework::PlayerIndex player,SignInRequest request,SignInHandlers handlers);

/** @brief A system notification. */
struct Notification {
    /** @brief What it is about, which picks its icon. */
    enum class Kind : std::uint8_t { Info, SignIn, SignOut, FriendOnline, FriendRequest, Invitation, Party, Achievement, Message, Service };
    /** @brief Kind. */
    Kind kind=Kind::Info;
    /** @brief First line. */
    std::string title;
    /** @brief Second line, may be empty. */
    std::string text;
    /** @brief An avatar to show instead of the icon (1021 bytes), or empty. */
    std::vector<unsigned char> avatar;
};
/** @brief Queues a notification (any thread). @param notification Notification. */
void notify(Notification notification);

/** @brief Tests: the name of the top screen ("friends", "gamerCard", ...), or empty. @return Name. */
std::string currentScreenForTesting();
/** @brief Tests: the top screen's items as shown. @return Labels. */
std::vector<std::string> labelsForTesting();
/** @brief Tests: the focused item of the top screen, or -1. @return Index. */
int focusForTesting();
/** @brief Tests: acts as if a command was pressed once. @param command Command. */
void sendForTesting(Command command);
/** @brief Tests: acts as if an item was clicked. @param index Item. */
void clickForTesting(int index);
}
