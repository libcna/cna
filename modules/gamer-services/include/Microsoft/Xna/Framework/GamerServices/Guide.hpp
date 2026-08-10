// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NotificationPosition.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/AsyncCallback.hpp"
#include "System/IAsyncResult.hpp"
#include "System/TimeSpan.hpp"
#include <any>
#include <optional>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class SpriteBatch;
    class SpriteFont;
    class Texture2D;
}

namespace Microsoft::Xna::Framework::GamerServices
{
    class Gamer;

    /**
     * @brief Provides access to the in-game Guide overlay (system UI, keyboard input, messaging, etc.).
     */
    class Guide
    {
    public:
        /** @brief Deleted; all members are static. */
        Guide() = delete;

        /**
         * @brief Gets whether the platform's screen saver is currently enabled.
         *
         * @return true if the screen saver is enabled.
         */
        [[nodiscard]] static bool getIsScreenSaverEnabledProperty();

        /**
         * @brief Enables or disables the platform's screen saver.
         *
         * @param value true to enable the screen saver; false to disable it.
         */
        static void setIsScreenSaverEnabledProperty(bool value);

        /**
         * @brief Gets whether the game is running in trial mode.
         *
         * @return true if running in trial mode.
         */
        [[nodiscard]] static bool getIsTrialModeProperty();

        /**
         * @brief Sets whether the game is running in trial mode.
         *
         * @param value true to run in trial mode.
         */
        static void setIsTrialModeProperty(bool value);

        /**
         * @brief Gets whether the Guide overlay is currently visible.
         *
         * Unlike real XNA off-Xbox (and FNA, which always returns false - there is no system
         * guide overlay to track), this reflects CNA's own real message-box/keyboard-input
         * overlays: true whenever BeginShowMessageBox or BeginShowKeyboardInput has an operation
         * pending. Matches this project's decision 1a reasoning (real observable behavior over a
         * PC no-op stub) now that those two overlays are genuinely real, not stubs.
         *
         * @return true if a message box or keyboard input overlay is currently pending.
         */
        [[nodiscard]] static bool getIsVisibleProperty();

        /**
         * @brief No-op, matching real XNA: there is no way to programmatically force the Guide
         * overlay to show/hide independently of an actual pending BeginShowMessageBox/
         * BeginShowKeyboardInput operation.
         *
         * @param value Ignored.
         */
        static void setIsVisibleProperty(bool value);

        /**
         * @brief Gets the screen position used for gamer notification toasts.
         *
         * @return The current NotificationPosition.
         */
        [[nodiscard]] static NotificationPosition getNotificationPositionProperty();

        /**
         * @brief Sets the screen position used for gamer notification toasts.
         *
         * @param value The new NotificationPosition.
         */
        static void setNotificationPositionProperty(NotificationPosition value);

        /**
         * @brief Gets whether trial mode is being simulated for testing.
         *
         * @return true if trial mode is being simulated.
         */
        [[nodiscard]] static bool getSimulateTrialModeProperty();

        /**
         * @brief Sets whether trial mode is being simulated for testing.
         *
         * @param value true to simulate trial mode.
         */
        static void setSimulateTrialModeProperty(bool value);

        /**
         * @brief Begins showing the on-screen keyboard for text input.
         *
         * Task 3.2: real captured text - accumulates each UTF-16 code unit
         * Microsoft::Xna::Framework::Input::TextInputEXT::TextInput fires (surrogate-pair safe)
         * into defaultText, and completes on Enter, matching real XNA/Xbox 360 on-screen keyboard
         * semantics. Does **not** complete synchronously, unlike this class's other Begin* methods.
         * title/description are stored and rendered by RenderPendingKeyboardInputEXT() below - a
         * game's own Draw() must call it once per frame (after drawing its own scene) for the
         * player to see the prompt/typed text at all; nothing renders automatically.
         *
         * @param player      The player requesting input.
         * @param title       The input dialog title.
         * @param description The input dialog description.
         * @param defaultText The initial text value; pre-seeds the buffer, so pressing Enter
         *        immediately returns this text unchanged.
         * @param callback    Invoked when the operation completes.
         * @param state       User-defined state passed through to the callback.
         * @return An IAsyncResult that completes on Enter (or is canceled via Escape/
         *         SimulateKeyboardInputCancelEXT - check WasKeyboardInputCanceledEXT); caller
         *         owns it and must delete it after EndShowKeyboardInput.
         * @throws System::InvalidOperationException if a keyboard input request is already
         *         pending.
         */
        [[nodiscard]] static System::IAsyncResult* BeginShowKeyboardInput(
            Microsoft::Xna::Framework::PlayerIndex player,
            const std::string& title,
            const std::string& description,
            const std::string& defaultText,
            System::AsyncCallback callback,
            std::any state
        );

        /**
         * @brief Begins showing the on-screen keyboard for text input.
         *
         * See the non-password overload above. usePasswordMode masks each typed character as '*'
         * in RenderPendingKeyboardInputEXT()'s own rendering (not the returned text itself, which
         * is always the real typed characters) - matching real XNA: both overloads complete on
         * Enter identically, only the on-screen display differs.
         *
         * @param player          The player requesting input.
         * @param title           The input dialog title.
         * @param description     The input dialog description.
         * @param defaultText     The initial text value; pre-seeds the buffer.
         * @param callback        Invoked when the operation completes.
         * @param state           User-defined state passed through to the callback.
         * @param usePasswordMode Whether entered characters are masked on-screen.
         * @return An IAsyncResult that completes on Enter (or is canceled via Escape/
         *         SimulateKeyboardInputCancelEXT - check WasKeyboardInputCanceledEXT); caller
         *         owns it and must delete it after EndShowKeyboardInput.
         * @throws System::InvalidOperationException if a keyboard input request is already
         *         pending.
         */
        [[nodiscard]] static System::IAsyncResult* BeginShowKeyboardInput(
            Microsoft::Xna::Framework::PlayerIndex player,
            const std::string& title,
            const std::string& description,
            const std::string& defaultText,
            System::AsyncCallback callback,
            std::any state,
            bool usePasswordMode
        );

        /**
         * @brief Completes an asynchronous BeginShowKeyboardInput request.
         *
         * @param result The result returned by BeginShowKeyboardInput.
         * @return The text the user actually typed (or defaultText, if Enter was pressed
         *         immediately with no further input). Empty if the operation was canceled -
         *         check WasKeyboardInputCanceledEXT() to distinguish "canceled" from "confirmed
         *         with nothing typed," since both otherwise return the same empty string (real
         *         XNA's own documented behavior returns null on cancel, which this C++ port's
         *         non-nullable std::string return type cannot represent directly).
         * @throws System::ArgumentException if result was not returned by BeginShowKeyboardInput.
         * @throws System::InvalidOperationException if the operation has not completed yet (the
         *         user has not pressed Enter or canceled).
         */
        [[nodiscard]] static std::string EndShowKeyboardInput(System::IAsyncResult* result);

        /**
         * @brief CNAEXT/test-only: gets the title the currently pending keyboard input request was
         * created with, without needing pixel readback of RenderPendingKeyboardInputEXT's own
         * rendering to confirm it round-tripped correctly (mirrors
         * GetPendingMessageBoxFocusButtonForTestingEXT's own established pattern).
         *
         * @return The pending request's title.
         * @throws System::InvalidOperationException if no keyboard input is currently pending.
         */
        CNAEXT [[nodiscard]] static const std::string& GetPendingKeyboardInputTitleForTestingEXT();

        /**
         * @brief CNAEXT/test-only: gets the description the currently pending keyboard input
         * request was created with. See GetPendingKeyboardInputTitleForTestingEXT above.
         *
         * @return The pending request's description.
         * @throws System::InvalidOperationException if no keyboard input is currently pending.
         */
        CNAEXT [[nodiscard]] static const std::string& GetPendingKeyboardInputDescriptionForTestingEXT();

        /**
         * @brief CNAEXT/test-only: gets the exact display text `RenderPendingKeyboardInputEXT`
         * would currently draw for the typed buffer - one `'*'` per typed UTF-16 code unit when
         * the pending request's `usePasswordMode` is true, the real typed text otherwise. Sourced
         * from the same masking logic `RenderPendingKeyboardInputEXT` itself calls (not a
         * reimplementation), so this genuinely fails if that masking branch is ever removed or
         * broken - unlike `EndShowKeyboardInput`, which always returns the real typed text by
         * design and so cannot itself prove masking happened on screen.
         *
         * @return The current display text, masked if password mode is on.
         * @throws System::InvalidOperationException if no keyboard input is currently pending.
         */
        CNAEXT [[nodiscard]] static std::string GetPendingKeyboardInputDisplayTextForTestingEXT();

        /**
         * @brief CNAEXT/EXT: gets whether the given completed BeginShowKeyboardInput operation was
         * canceled (Escape, or SimulateKeyboardInputCancelEXT) rather than confirmed with Enter.
         * Not part of the real XNA 4.0 Guide API - real XNA distinguishes this case via a null
         * EndShowKeyboardInput return, which this port's non-nullable std::string return type
         * cannot represent directly.
         *
         * @param result The result returned by BeginShowKeyboardInput.
         * @return true if the operation was canceled rather than confirmed.
         * @throws System::ArgumentException if result was not returned by BeginShowKeyboardInput.
         */
        CNAEXT [[nodiscard]] static bool WasKeyboardInputCanceledEXT(System::IAsyncResult* result);

        /**
         * @brief CNAEXT/EXT: gets whether a keyboard input request is currently pending (shown via
         * BeginShowKeyboardInput and not yet completed). Not part of the real XNA 4.0 Guide API.
         *
         * @return true if a keyboard input request is currently awaiting Enter/cancel.
         */
        CNAEXT [[nodiscard]] static bool getHasPendingKeyboardInputEXTProperty();

        /**
         * @brief CNAEXT/EXT: renders the currently pending keyboard input overlay (if any) -
         * title, description, and the text typed so far (masked as '*' per character if
         * usePasswordMode was set) - and checks real keyboard input for an Escape press,
         * canceling the pending operation when detected. A game's own Draw() calls this once per
         * frame, after drawing its own scene, so the overlay renders on top (same convention as
         * RenderPendingMessageBoxEXT). No-op if no keyboard input is pending.
         *
         * Not part of the real XNA 4.0 Guide API - Guide has no access to a game's own
         * GraphicsDevice/SpriteBatch on any real platform, so this needs an explicit pump/render
         * entry point instead of an automatic hook (same reasoning as RenderPendingMessageBoxEXT).
         *
         * @param device      The graphics device backing spriteBatch, used to size/center the box.
         * @param spriteBatch An open-and-ready-to-draw-into SpriteBatch (between Begin()/End()).
         * @param font        The font used to render the title/description/typed-text.
         * @param whitePixel  A caller-owned opaque white 1x1 (or larger) Texture2D used to draw
         *        the box background rectangle.
         */
        CNAEXT static void RenderPendingKeyboardInputEXT(
            Graphics::GraphicsDevice& device,
            Graphics::SpriteBatch& spriteBatch,
            Graphics::SpriteFont& font,
            Graphics::Texture2D& whitePixel
        );

        /**
         * @brief CNAEXT/EXT: cancels the currently pending keyboard input as if the user pressed
         * Escape, without requiring real keyboard input. Intended for headless demos/tests that
         * cannot drive a real window's keyboard input - mirrors SimulateMessageBoxClickEXT's own
         * established pattern for the message box overlay.
         *
         * @throws System::InvalidOperationException if no keyboard input is currently pending.
         */
        CNAEXT static void SimulateKeyboardInputCancelEXT();

        /**
         * @brief CNAEXT/test-only: clears any currently-pending keyboard input request without
         * completing it or invoking its callback, and unsubscribes from
         * Microsoft::Xna::Framework::Input::TextInputEXT::TextInput - so a test/demo that creates
         * one and doesn't resolve it cannot strand the single-pending-request guard, or leave a
         * dangling subscriber reacting to unrelated future TextInputEXT activity, for every later
         * test in the same process. Does not delete the pending action object - the caller who
         * received it from BeginShowKeyboardInput still owns and must delete it.
         */
        CNAEXT static void ResetPendingKeyboardInputForTestingEXT();

        /**
         * @brief Begins showing a system message box.
         *
         * Task 3.1: unlike this class's other Begin* methods, this does **not** complete
         * synchronously — a message box needs a real user response. Completes once the game's
         * own Draw() loop calls RenderPendingMessageBoxEXT() and the user selects a button (or
         * a test/headless caller calls SimulateMessageBoxClickEXT()).
         *
         * @param title       The message box title.
         * @param text        The message box body text.
         * @param buttons     The button labels to display; must contain at least one entry.
         * @param focusButton The index of the initially focused button.
         * @param icon        The icon to display.
         * @param callback    Invoked when the operation completes.
         * @param state       User-defined state passed through to the callback.
         * @return An IAsyncResult that completes once a button is selected; caller owns it and
         *         must delete it after EndShowMessageBox.
         * @throws System::ArgumentException if buttons is empty.
         * @throws System::InvalidOperationException if another message box is already pending.
         */
        [[nodiscard]] static System::IAsyncResult* BeginShowMessageBox(
            const std::string& title,
            const std::string& text,
            const std::vector<std::string>& buttons,
            int focusButton,
            MessageBoxIcon icon,
            System::AsyncCallback callback,
            std::any state
        );

        /**
         * @brief Begins showing a system message box for a specific player.
         *
         * See the player-less overload above; player is accepted for API parity but not
         * otherwise used, matching this platform's single-active-message-box model.
         *
         * @param player      The player the message box is shown to.
         * @param title       The message box title.
         * @param text        The message box body text.
         * @param buttons     The button labels to display; must contain at least one entry.
         * @param focusButton The index of the initially focused button.
         * @param icon        The icon to display.
         * @param callback    Invoked when the operation completes.
         * @param state       User-defined state passed through to the callback.
         * @return An IAsyncResult that completes once a button is selected; caller owns it and
         *         must delete it after EndShowMessageBox.
         * @throws System::ArgumentException if buttons is empty.
         * @throws System::InvalidOperationException if another message box is already pending.
         */
        [[nodiscard]] static System::IAsyncResult* BeginShowMessageBox(
            Microsoft::Xna::Framework::PlayerIndex player,
            const std::string& title,
            const std::string& text,
            const std::vector<std::string>& buttons,
            int focusButton,
            MessageBoxIcon icon,
            System::AsyncCallback callback,
            std::any state
        );

        /**
         * @brief Completes an asynchronous BeginShowMessageBox request.
         *
         * @param result The result returned by BeginShowMessageBox.
         * @return The zero-based index of the selected button, or empty if the box was
         *         dismissed without a selection (never happens in this implementation - always
         *         has a value).
         * @throws System::ArgumentException if result was not returned by BeginShowMessageBox.
         * @throws System::InvalidOperationException if the operation has not completed yet.
         */
        [[nodiscard]] static std::optional<int> EndShowMessageBox(System::IAsyncResult* result);

        /**
         * @brief CNAEXT/EXT: gets whether a message box is currently pending (shown via
         * BeginShowMessageBox and not yet completed). Not part of the real XNA 4.0 Guide API,
         * which has no local rendering concept to be "pending" against.
         *
         * @return true if a message box is currently awaiting a button selection.
         */
        CNAEXT [[nodiscard]] static bool getHasPendingMessageBoxEXTProperty();

        /**
         * @brief CNAEXT/EXT: renders the currently pending message box (if any) and checks real
         * mouse input for a button click, completing the pending BeginShowMessageBox operation
         * when one is detected. A game's own Draw() calls this once per frame, after drawing its
         * own scene, so the overlay renders on top. No-op if no message box is pending.
         *
         * Not part of the real XNA 4.0 Guide API - Guide has no access to a game's own
         * GraphicsDevice/SpriteBatch on any real platform, so this needs an explicit pump/render
         * entry point instead of an automatic hook.
         *
         * @param device      The graphics device backing spriteBatch, used to size/center the box.
         * @param spriteBatch An open-and-ready-to-draw-into SpriteBatch (between Begin()/End()).
         * @param font        The font used to render the title/body/button text.
         * @param whitePixel  A caller-owned opaque white 1x1 (or larger) Texture2D used to draw
         *        the box/button background rectangles. Caller-supplied (matching font above)
         *        rather than created internally: SpriteBatch's default deferred sort mode only
         *        stores a pointer to each Draw() call's texture, resolved at End() - a texture
         *        created and destroyed within this call alone would already be a dangling
         *        pointer by the time the caller's own End() runs.
         */
        CNAEXT static void RenderPendingMessageBoxEXT(
            Graphics::GraphicsDevice& device,
            Graphics::SpriteBatch& spriteBatch,
            Graphics::SpriteFont& font,
            Graphics::Texture2D& whitePixel
        );

        /**
         * @brief CNAEXT/EXT: completes the currently pending message box as if the user clicked
         * the button at buttonIndex, without requiring real mouse input. Intended for headless
         * demos/tests that cannot drive a real window's mouse input.
         *
         * @param buttonIndex The zero-based index of the button to select.
         * @throws System::InvalidOperationException if no message box is currently pending.
         * @throws System::ArgumentOutOfRangeException if buttonIndex is out of range for the
         *         pending message box's button list.
         */
        CNAEXT static void SimulateMessageBoxClickEXT(int buttonIndex);

        /**
         * @brief CNAEXT/test-only: clears any currently-pending message box without completing
         * it or invoking its callback, so a test/demo that creates one and doesn't resolve it
         * (e.g. one that only checks BeginShowMessageBox's validation) cannot strand the
         * single-pending-message-box guard for every later Begin* call in the same process. Does
         * not delete the pending action object - the caller who received it from
         * BeginShowMessageBox still owns and must delete it, per that method's own contract.
         */
        CNAEXT static void ResetPendingMessageBoxForTestingEXT();

        /**
         * @brief CNAEXT/test-only: gets the focusButton the currently pending message box was
         * created with, without needing pixel readback to confirm it round-tripped correctly.
         *
         * @return The pending box's focusButton index.
         * @throws System::InvalidOperationException if no message box is currently pending.
         */
        CNAEXT [[nodiscard]] static int GetPendingMessageBoxFocusButtonForTestingEXT();

        /**
         * @brief Delays gamer notification toasts by the given duration.
         *
         * @param delay The delay duration. No-op in this platform's implementation.
         */
        static void DelayNotifications(System::TimeSpan delay);

        /**
         * @brief Shows the compose-message UI. No-op in this platform's implementation.
         *
         * @param player     The player composing the message.
         * @param text       The initial message text.
         * @param recipients The message recipients.
         */
        static void ShowComposeMessage(
            Microsoft::Xna::Framework::PlayerIndex player,
            const std::string& text,
            const std::vector<Gamer*>& recipients
        );

        /**
         * @brief Shows the friend-request UI. No-op in this platform's implementation.
         *
         * @param player The player sending the request.
         * @param gamer  The gamer to request friendship with.
         */
        static void ShowFriendRequest(Microsoft::Xna::Framework::PlayerIndex player, Gamer* gamer);

        /**
         * @brief Shows the friends list UI. No-op in this platform's implementation.
         *
         * @param player The player whose friends list is shown.
         */
        static void ShowFriends(Microsoft::Xna::Framework::PlayerIndex player);

        /**
         * @brief Shows the game-invite UI. No-op in this platform's implementation.
         *
         * @param player     The player sending invites.
         * @param recipients The gamers to invite.
         */
        static void ShowGameInvite(
            Microsoft::Xna::Framework::PlayerIndex player,
            const std::vector<Gamer*>& recipients
        );

        /**
         * @brief Shows the game-invite UI for a specific session. No-op in this platform's implementation.
         *
         * @param sessionId The session identifier to invite gamers to.
         */
        static void ShowGameInvite(const std::string& sessionId);

        /**
         * @brief Shows a gamer's gamer card. No-op in this platform's implementation.
         *
         * @param player The player viewing the card.
         * @param gamer  The gamer whose card is shown.
         */
        static void ShowGamerCard(Microsoft::Xna::Framework::PlayerIndex player, Gamer* gamer);

        /**
         * @brief Shows the marketplace UI. No-op in this platform's implementation.
         *
         * @param player The player viewing the marketplace.
         */
        static void ShowMarketplace(Microsoft::Xna::Framework::PlayerIndex player);

        /**
         * @brief Shows the messages UI. No-op in this platform's implementation.
         *
         * @param player The player viewing messages.
         */
        static void ShowMessages(Microsoft::Xna::Framework::PlayerIndex player);

        /**
         * @brief Shows the party UI. No-op in this platform's implementation.
         *
         * @param player The player viewing their party.
         */
        static void ShowParty(Microsoft::Xna::Framework::PlayerIndex player);

        /**
         * @brief Shows the party-sessions UI. No-op in this platform's implementation.
         *
         * @param player The player viewing party sessions.
         */
        static void ShowPartySessions(Microsoft::Xna::Framework::PlayerIndex player);

        /**
         * @brief Shows the player-review UI. No-op in this platform's implementation.
         *
         * @param player The player leaving a review.
         * @param gamer  The gamer being reviewed.
         */
        static void ShowPlayerReview(Microsoft::Xna::Framework::PlayerIndex player, Gamer* gamer);

        /**
         * @brief Shows the players list UI. No-op in this platform's implementation.
         *
         * @param player The player viewing the list.
         */
        static void ShowPlayers(Microsoft::Xna::Framework::PlayerIndex player);

        /**
         * @brief Shows the sign-in UI. No-op in this platform's implementation.
         *
         * @param paneCount  The number of sign-in panes to show.
         * @param onlineOnly Whether to restrict sign-in to online profiles.
         */
        static void ShowSignIn(int paneCount, bool onlineOnly);

        /**
         * @brief Shows the achievements UI (FNA extension). No-op in this platform's implementation.
         *
         * Task 7.11: FNA's own addition, not part of real XNA 4.0's Guide API - FNA's own
         * reference source already names it with the "EXT" suffix for exactly this reason.
         *
         * @param player The player viewing achievements.
         */
        CNAEXT static void ShowAchievementsEXT(Microsoft::Xna::Framework::PlayerIndex player);

    private:
        static bool isTrialMode_;
        static bool simulateTrialMode_;
        static NotificationPosition position_;
    };
}
