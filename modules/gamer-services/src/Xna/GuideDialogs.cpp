// SPDX-License-Identifier: MS-PL
// The Guide's game dialogs: Guide.BeginShowMessageBox and Guide.BeginShowKeyboardInput, their
// End calls and the visibility rules every Guide pane shares. Linked by a game that shows a
// message box or keyboard; it draws through the Guide's frame (GuideScreens.cpp), never through
// the service-backed panels or the avatar system.
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include <memory>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "CNA/Internal/Input/SystemInput.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/NotSupportedException.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "../Internal/GuideOverlay.hpp"
#include "../Internal/Guide/GuideScreen.hpp"
#include "../Internal/Guide/GuideSystem.hpp"
#include "CNA/Internal/Runtime/IModalFrames.hpp"
#include "../Internal/ServiceAsyncResult.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include <algorithm>
#include <functional>
#include <cstdint>
#include <cstdlib>
#include <array>
#include <atomic>
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include <cctype>

namespace Microsoft::Xna::Framework::GamerServices
{
    namespace
    {
        // C# `internal class GuideAction : IAsyncResult`, only ever used within Guide's own
        // Begin*/End* pairs — kept as a translation-unit-private type rather than a nested
        // class, since (unlike Gamer::GamerAction) nothing outside Guide needs to name it.
        class GuideAction : public System::IAsyncResult
        {
        public:
            GuideAction(std::any state, System::AsyncCallback callback)
                : Callback(std::move(callback))
                , asyncState_(std::move(state))
                , asyncWaitHandle_(true, System::Threading::EventResetMode::ManualReset)
            {
            }

            [[nodiscard]] const std::any& getAsyncStateProperty() const override { return asyncState_; }
            [[nodiscard]] bool getCompletedSynchronouslyProperty() const override { return false; }
            [[nodiscard]] bool getIsCompletedProperty() const override { return isCompleted_; }
            void setIsCompletedProperty(bool value)
            {
                isCompleted_ = value;
                if (value) asyncWaitHandle_.Set();
            }

            [[nodiscard]] System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override
            {
                return asyncWaitHandle_;
            }

            const System::AsyncCallback Callback;
            // Apple M4 AM4-090: identifies this request to the Guide's input reader,
            // which primes when something new appears on top; text or an address could repeat.
            const std::uint64_t Serial = NextSerial();
            // Reference XOverlappedAsyncResult.endHasBeenCalled.
            bool EndCalled{false};

        private:
            static std::uint64_t NextSerial()
            {
                static std::atomic<std::uint64_t> next{0};
                return ++next;
            }

            std::any asyncState_;
            bool isCompleted_{false};

            // Mutable: IAsyncResult::getAsyncWaitHandleProperty() is const but returns a
            // non-const WaitHandle&, so the handle exposed through it must be mutable.
            mutable System::Threading::EventWaitHandle asyncWaitHandle_;
        };

        // Task 3.1: a real message box needs a real user response, so - unlike GuideAction's
        // other uses (BeginShowKeyboardInput, which completes synchronously) - this one carries
        // its own request/response state (title/text/buttons/focusButton/icon in, SelectedButton
        // out) instead of completing at construction time. The same object serves as both the
        // returned IAsyncResult* and (via pendingMessageBox_ below) the one currently-rendering
        // message box - matching NetworkSessionAction's own established pattern of the action
        // object carrying its own request data as public const fields.
        class GuideMessageBoxAction : public GuideAction
        {
        public:
            GuideMessageBoxAction(
                std::any state,
                System::AsyncCallback callback,
                std::string title,
                std::string text,
                std::vector<std::string> buttons,
                int focusButton,
                MessageBoxIcon icon
            )
                : GuideAction(std::move(state), std::move(callback))
                , Title(std::move(title))
                , Text(std::move(text))
                , Buttons(std::move(buttons))
                , FocusButton(focusButton)
                , Icon(icon)
            {
            }

            const std::string Title;
            const std::string Text;
            const std::vector<std::string> Buttons;
            // Moves with keyboard and gamepad navigation.
            int FocusButton;
            const MessageBoxIcon Icon;
            std::optional<int> SelectedButton;

            // Edge-detection state for RenderPendingMessageBoxEXT's real mouse-click handling -
            // a button selects on the down-edge of the left mouse button, not every frame it's
            // held, so this must persist across calls for as long as this box is pending.
            bool WasLeftMouseDown = false;
            // Keyboard/gamepad navigation edges. They start as held so the press that opened the
            // box (Enter confirming a previous prompt, A choosing a menu item) cannot answer it.
            bool WasPreviousDown = true;
            bool WasNextDown = true;
            bool WasSelectDown = true;
            bool WasCancelDown = true;
        };

        // At most one message box is pending at a time (matches this platform's single-active-
        // action model used throughout GamerServices/Net - e.g. NetworkSession::activeAction_,
        // SignedInGamer::statReceiveAction_). Points at the same object returned to the caller as
        // an IAsyncResult*; caller still owns and must delete it, matching GuideAction's existing
        // ownership contract - this pointer only tracks which one (if any) is still awaiting a
        // response, never owns/frees it itself.
        GuideMessageBoxAction* pendingMessageBox_ = nullptr;

        // The click that answers a message box is a press and a release. Lifting the touch
        // withhold on the press would let the release arrive as a tap on whatever the box was
        // covering -- on a phone the shell owns the whole gesture, not just its first half -- so
        // the withhold outlives the answer until that button comes back up.
        bool suppressTouchUntilMouseRelease_ = false;

        // Defined below, once both pending pointers are in scope.
        void SyncTouchInputSuppression();

        // Shared completion path for both the real mouse-driven click (RenderPendingMessageBoxEXT)
        // and the headless/test-only SimulateMessageBoxClickEXT. Captures the action pointer and
        // clears pendingMessageBox_ *before* invoking the callback, not after - a re-entrant
        // callback that immediately calls BeginShowMessageBox again (or EndShowMessageBox on this
        // same result) must see consistent, already-updated state, matching the same reentrancy
        // fix applied to NetworkSession's Begin*/audit_net.md High finding.
        void CompletePendingMessageBox(std::optional<int> buttonIndex)
        {
            GuideMessageBoxAction* action = pendingMessageBox_;
            action->SelectedButton = buttonIndex;
            action->setIsCompletedProperty(true);
            pendingMessageBox_ = nullptr;
            SyncTouchInputSuppression();
            if (action->Callback)
            {
                auto callback = action->Callback; callback(*action);
            }
        }

        // Task 3.2: mirrors CNA::Internal::Input's own file-local decode_utf8_to_utf16 in
        // SdlInputBridge.cpp (same reasoning documented there: UTF-16-code-unit granularity,
        // which sharp-runtime's byte-oriented Encoding classes don't directly expose - this is
        // internal Guide plumbing tied directly to TextInputEXT's charcs stream, the same
        // category of file-local translation helper). Malformed sequences substitute U+FFFD,
        // matching Encoding.UTF8's own behavior (same choice SdlInputBridge.cpp makes).
        std::u16string DecodeUtf8ToUtf16(const std::string& text)
        {
            std::u16string result;
            const auto* s = reinterpret_cast<const unsigned char*>(text.c_str());
            while (*s != 0)
            {
                const unsigned char b0 = s[0];
                std::uint32_t cp;
                int len;
                std::uint32_t minCp;
                if (b0 < 0x80)                { cp = b0;        len = 1; minCp = 0x0; }
                else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; len = 2; minCp = 0x80; }
                else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; len = 3; minCp = 0x800; }
                else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; len = 4; minCp = 0x10000; }
                else
                {
                    result.push_back(u'�');
                    ++s;
                    continue;
                }

                int i = 1;
                for (; i < len; ++i)
                {
                    if ((s[i] & 0xC0) != 0x80) break;
                    cp = (cp << 6) | (s[i] & 0x3F);
                }
                if (i != len)
                {
                    result.push_back(u'�');
                    s += i;
                    continue;
                }
                s += len;

                if (cp < minCp || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
                {
                    result.push_back(u'�');
                    continue;
                }

                if (cp <= 0xFFFF)
                {
                    result.push_back(static_cast<char16_t>(cp));
                }
                else
                {
                    cp -= 0x10000;
                    result.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                    result.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
                }
            }
            return result;
        }

        // Reference Guide.ValidateShowMessageBoxArgs. Lengths count UTF-16 code units, as .NET's
        // string.Length does. The Windows-only rule that the player be PlayerIndex.One is not taken:
        // the Xbox 360 Guide shows a box for any player.
        void ValidateShowMessageBoxArgs(PlayerIndex player, const std::string& title, const std::string& text,
                                        const std::vector<std::string>& buttons, int focusButton)
        {
            if (static_cast<int>(player) < 0 || static_cast<int>(player) > 3)
                throw System::ArgumentOutOfRangeException("player");
            const auto invalid = [](const std::string& value) { return value.empty() || DecodeUtf8ToUtf16(value).size() >= 256; };
            if (invalid(title))
                throw System::ArgumentException("The title must be non-empty and shorter than 256 characters.", "title");
            if (invalid(text))
                throw System::ArgumentException("The text must be non-empty and shorter than 256 characters.", "text");
            if (buttons.empty() || buttons.size() > 3 || std::any_of(buttons.begin(), buttons.end(), invalid))
                throw System::ArgumentException(
                    "There must be one to three buttons, each non-empty and shorter than 256 characters.", "buttons");
            if (focusButton < 0 || focusButton >= static_cast<int>(buttons.size()))
                throw System::ArgumentOutOfRangeException("focusButton");
        }

        // Inverse of DecodeUtf8ToUtf16 above - reassembles a well-formed surrogate pair into one
        // 4-byte UTF-8 sequence rather than encoding each half independently.
        std::string EncodeUtf16ToUtf8(const std::u16string& text)
        {
            std::string result;
            for (std::size_t i = 0; i < text.size();)
            {
                std::uint32_t cp = text[i];
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()
                    && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
                {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<std::uint32_t>(text[i + 1]) - 0xDC00);
                    i += 2;
                }
                else
                {
                    ++i;
                }

                if (cp <= 0x7F)
                {
                    result.push_back(static_cast<char>(cp));
                }
                else if (cp <= 0x7FF)
                {
                    result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else if (cp <= 0xFFFF)
                {
                    result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else
                {
                    result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            }
            return result;
        }

        // Task 3.2: BeginShowKeyboardInput's completion can't be known synchronously - unlike
        // GuideAction's other use (the message box, which needs an explicit per-frame render/pump
        // call since Guide has no automatic hook into a game's draw loop), real typed text arrives
        // through TextInputEXT::TextInput, which already fires automatically via the engine's own
        // event pump (Game::PollEvents(), per that class's own threading note) - so this needs no
        // separate pump entry point of its own, just a subscription for as long as one request is
        // pending.
        class GuideKeyboardInputAction : public GuideAction
        {
        public:
            GuideKeyboardInputAction(std::any state, System::AsyncCallback callback, std::string title,
                                      std::string description, bool usePasswordMode)
                : GuideAction(std::move(state), std::move(callback))
                , Title(std::move(title))
                , Description(std::move(description))
                , UsePasswordMode(usePasswordMode)
            {
            }

            const std::string Title;
            const std::string Description;
            const bool UsePasswordMode;
            std::u16string Buffer;
            bool Canceled = false;
            System::MulticastAction<Input::charcs>::Token SubscriptionToken =
                System::MulticastAction<Input::charcs>::InvalidToken;

            // Edge-detection state for RenderPendingKeyboardInputEXT's own real Escape-to-cancel
            // handling - mirrors GuideMessageBoxAction::WasLeftMouseDown's same reasoning
            // (cancel on the down-edge, not every frame Escape is held).
            bool WasEscapeDown = false;
        };

        GuideKeyboardInputAction* pendingKeyboardInput_ = nullptr;

        // A visible Guide is drawn and driven by the system shell on every real XNA platform, so
        // it owns the screen and the game's own touch panel reports nothing behind it. CNA draws
        // its overlay inside the game's own Draw(), so that ownership is stated explicitly here:
        // touch input is withheld for exactly as long as getIsVisibleProperty() is true. Called
        // wherever either pending pointer changes.
        void SyncTouchInputSuppression()
        {
            Input::Touch::TouchPanel::setInputSuppressedEXT(
                CNA::Internal::GamerServices::guideIsVisible() ||
                suppressTouchUntilMouseRelease_);
            CNA::Internal::GamerServices::syncSystemInputOwnership();
        }

        // audit_net.md remediation (2026-07-18): the actual masking decision, shared by
        // RenderPendingKeyboardInputEXT (the real on-screen draw) and
        // GetPendingKeyboardInputDisplayTextForTestingEXT (the test-only accessor exposing that
        // same decision without needing pixel readback) - one '*' per typed UTF-16 code unit
        // when UsePasswordMode is set, the real typed text otherwise. A single source of truth so
        // the test accessor cannot silently drift from what actually gets drawn.
        std::string ComputeDisplayText(const GuideKeyboardInputAction* action)
        {
            if (action->UsePasswordMode)
            {
                return std::string(action->Buffer.size(), '*');
            }
            return EncodeUtf16ToUtf8(action->Buffer);
        }

        // Removes the last-typed code unit for Backspace, respecting surrogate pairs (removing a
        // low surrogate also removes its preceding high surrogate, so a single Backspace after
        // typing an emoji deletes the whole code point, not just half of it).
        void RemoveLastCodeUnit(std::u16string& buffer)
        {
            if (buffer.empty())
            {
                return;
            }
            const char16_t last = buffer.back();
            buffer.pop_back();
            if (last >= 0xDC00 && last <= 0xDFFF && !buffer.empty())
            {
                const char16_t prev = buffer.back();
                if (prev >= 0xD800 && prev <= 0xDBFF)
                {
                    buffer.pop_back();
                }
            }
        }

        // Same reentrancy-safe shape as CompletePendingMessageBox: captured and cleared *before*
        // invoking the callback, and unsubscribes from TextInputEXT::TextInput here (not in
        // EndShowKeyboardInput) so real OS-level text capture stops the instant Enter is pressed
        // or the operation is canceled, not whenever the game later gets around to calling End*.
        // `canceled` clears the buffer (matching a real on-screen keyboard's own cancel-discards-
        // the-edit semantics) and marks the action so WasKeyboardInputCanceledEXT can report it.
        // Reference XOverlappedAsyncResult.PrepareForEndFunction: End may be called once, and waits
        // for the answer. CNA draws the Guide inside the game, so the wait runs the game's modal
        // frames (input and the Guide over a cleared screen, the game's Update and Draw frozen), as
        // a console keeps its Guide running while the game thread waits. Without a running game to
        // present it nothing could answer, so the wait refuses instead of hanging.
        void PrepareForEnd(GuideAction& action, const char* operation)
        {
            if (action.EndCalled) throw System::InvalidOperationException(std::string(operation) + " was already called for this result.");
            action.EndCalled = true;
            while (!action.getIsCompletedProperty())
            {
                auto* frames = CNA::Internal::GamerServices::guideModalFrames();
                if (frames == nullptr || !frames->runModalFrame())
                {
                    action.EndCalled = false;
                    throw System::InvalidOperationException(std::string(operation) +
                        " cannot wait for the answer: no running game is presenting the Guide.");
                }
            }
        }

        void CompletePendingKeyboardInput(bool canceled)
        {
            GuideKeyboardInputAction* action = pendingKeyboardInput_;
            Input::TextInputEXT::TextInput.Remove(action->SubscriptionToken);
            Microsoft::Xna::Framework::Input::TextInputEXT::StopTextInput();
            pendingKeyboardInput_ = nullptr;
            SyncTouchInputSuppression();
            if (canceled)
            {
                action->Canceled = true;
                action->Buffer.clear();
            }
            action->setIsCompletedProperty(true);
            if (action->Callback)
            {
                auto callback = action->Callback; callback(*action);
            }
        }
    }

    bool Guide::getIsVisibleProperty()
    {
        // Reference Guide.IsVisible: reading it before gamer services are initialized is an error.
        if (!GamerServicesDispatcher::getIsInitializedProperty())
            throw System::InvalidOperationException("Gamer services are not initialized.");
        return CNA::Internal::GamerServices::guideIsVisible();
    }

    System::IAsyncResult* Guide::BeginShowKeyboardInput(
        Microsoft::Xna::Framework::PlayerIndex player,
        const std::string& title,
        const std::string& description,
        const std::string& defaultText,
        System::AsyncCallback callback,
        std::any state
    ) {
        return BeginShowKeyboardInput(
            player, title, description, defaultText, std::move(callback), std::move(state), false
        );
    }

    System::IAsyncResult* Guide::BeginShowKeyboardInput(
        Microsoft::Xna::Framework::PlayerIndex player,
        const std::string& title,
        const std::string& description,
        const std::string& defaultText,
        System::AsyncCallback callback,
        std::any state,
        bool usePasswordMode
    ) {
        // Reference BeginShowKeyboardInput: every text under 256 characters (UTF-16 units); the
        // Windows-only "player must be One" rule is not taken. Then the kernel's refusal while the
        // Guide is visible.
        if (static_cast<int>(player) < 0 || static_cast<int>(player) > 3)
            throw System::ArgumentOutOfRangeException("player");
        if (DecodeUtf8ToUtf16(title).size() >= 256)
            throw System::ArgumentException("The title must be shorter than 256 characters.", "title");
        if (DecodeUtf8ToUtf16(description).size() >= 256)
            throw System::ArgumentException("The description must be shorter than 256 characters.", "description");
        if (DecodeUtf8ToUtf16(defaultText).size() >= 256)
            throw System::ArgumentException("The default text must be shorter than 256 characters.", "defaultText");
        if (CNA::Internal::GamerServices::guideIsVisible())
            throw GuideAlreadyVisibleException();
        return CNA::Internal::GamerServices::showGuideKeyboardInput(
            player, title, description, defaultText, std::move(callback), std::move(state), usePasswordMode);
    }

    namespace
    {
    // One character of keyboard input, from a real key (TextInputEXT) or the Guide's on-screen
    // keyboard. Enter commits, Backspace deletes, other control characters are ignored.
    void HandleKeyboardCharacter(Input::charcs c)
    {
        if (pendingKeyboardInput_ == nullptr)
        {
            return;
        }
        // Enter/Return - FNA/CNA's SDL bridge synthesizes this as char code 13 on KEY_DOWN (SDL
        // doesn't deliver a real TEXT_INPUT event for it) - the most faithful analog to a real
        // Xbox 360 on-screen keyboard's "confirm" action.
        if (c == u'\r' || c == u'\n')
        {
            CompletePendingKeyboardInput(/*canceled=*/false);
            return;
        }
        if (c == u'\b')
        {
            RemoveLastCodeUnit(pendingKeyboardInput_->Buffer);
            return;
        }
        // Home, End, Tab, Ctrl+V and Delete: no cursor or clipboard in this append-only field.
        if (c == 0x02 || c == 0x03 || c == 0x09 || c == 0x16 || c == 0x7F)
        {
            return;
        }
        if (!CNA::Internal::GamerServices::guideSignInActive() || pendingKeyboardInput_->Buffer.size() < 256)
            pendingKeyboardInput_->Buffer.push_back(c);
    }

    // The keyboard pane itself, shared by the validated public call and the Guide's own panes.
    System::IAsyncResult* OpenKeyboardInputInternal(
        const std::string& title,
        const std::string& description,
        const std::string& defaultText,
        System::AsyncCallback callback,
        std::any state,
        bool usePasswordMode
    ) {
        if (pendingKeyboardInput_ != nullptr)
        {
            throw System::InvalidOperationException("A keyboard input request is already pending.");
        }

        Microsoft::Xna::Framework::Input::TextInputEXT::StartTextInput();

        auto* action = new GuideKeyboardInputAction(std::move(state), std::move(callback), title,
                                                      description, usePasswordMode);
        action->Buffer = DecodeUtf8ToUtf16(defaultText);
        pendingKeyboardInput_ = action;
        SyncTouchInputSuppression();

        action->SubscriptionToken = Microsoft::Xna::Framework::Input::TextInputEXT::TextInput.Add(
            [](Input::charcs c) { HandleKeyboardCharacter(c); });
        return action;
    }
    }

    std::string Guide::EndShowKeyboardInput(System::IAsyncResult* result)
    {
        if (result == nullptr) throw System::ArgumentNullException("result");
        auto* action = dynamic_cast<GuideKeyboardInputAction*>(result);
        if (action == nullptr)
        {
            throw System::ArgumentException("result was not returned by a call to BeginShowKeyboardInput.", "result");
        }
        PrepareForEnd(*action, "EndShowKeyboardInput");
        return EncodeUtf16ToUtf8(action->Buffer);
    }

    const std::string& Guide::GetPendingKeyboardInputTitleForTestingEXT()
    {
        if (pendingKeyboardInput_ == nullptr)
        {
            throw System::InvalidOperationException("No keyboard input is currently pending.");
        }
        return pendingKeyboardInput_->Title;
    }

    const std::string& Guide::GetPendingKeyboardInputDescriptionForTestingEXT()
    {
        if (pendingKeyboardInput_ == nullptr)
        {
            throw System::InvalidOperationException("No keyboard input is currently pending.");
        }
        return pendingKeyboardInput_->Description;
    }

    std::string Guide::GetPendingKeyboardInputDisplayTextForTestingEXT()
    {
        if (pendingKeyboardInput_ == nullptr)
        {
            throw System::InvalidOperationException("No keyboard input is currently pending.");
        }
        return ComputeDisplayText(pendingKeyboardInput_);
    }

    bool Guide::WasKeyboardInputCanceledEXT(System::IAsyncResult* result)
    {
        auto* action = dynamic_cast<GuideKeyboardInputAction*>(result);
        if (action == nullptr)
        {
            throw System::ArgumentException("result was not returned by a call to BeginShowKeyboardInput.", "result");
        }
        return action->Canceled;
    }

    bool Guide::getHasPendingKeyboardInputEXTProperty()
    {
        return pendingKeyboardInput_ != nullptr;
    }

    void Guide::RenderPendingKeyboardInputEXT(
        Graphics::GraphicsDevice& device,
        Graphics::SpriteBatch& spriteBatch,
        Graphics::SpriteFont&,
        Graphics::Texture2D&
    ) {
        // Drawn and answered as the CNA system dialog, in the caller's begun batch.
        if (pendingKeyboardInput_ != nullptr)
        {
            CNA::Internal::GamerServices::GuideUi::drawGameDialogs(device, spriteBatch);
        }
    }

    void Guide::SimulateKeyboardInputCancelEXT()
    {
        if (pendingKeyboardInput_ == nullptr)
        {
            // Escape on the sign-in screen, before any name is typed, ends the sign-in.
            if (CNA::Internal::GamerServices::GuideUi::currentScreenForTesting() == "signIn")
            {
                CNA::Internal::GamerServices::GuideUi::sendForTesting(CNA::Internal::GamerServices::GuideUi::Command::Back);
                return;
            }
            throw System::InvalidOperationException("No keyboard input is currently pending.");
        }
        CompletePendingKeyboardInput(/*canceled=*/true);
    }

    void Guide::ResetPendingKeyboardInputForTestingEXT()
    {
        if (pendingKeyboardInput_ != nullptr)
        {
            Microsoft::Xna::Framework::Input::TextInputEXT::TextInput.Remove(pendingKeyboardInput_->SubscriptionToken);
            pendingKeyboardInput_ = nullptr;
            SyncTouchInputSuppression();
        }
    }

    System::IAsyncResult* Guide::BeginShowMessageBox(
        const std::string& title,
        const std::string& text,
        const std::vector<std::string>& buttons,
        int focusButton,
        MessageBoxIcon icon,
        System::AsyncCallback callback,
        std::any state
    ) {
        // Reference: the overload without a player shows the box for player one.
        return BeginShowMessageBox(PlayerIndex::One, title, text, buttons, focusButton, icon, std::move(callback), std::move(state));
    }

    System::IAsyncResult* Guide::BeginShowMessageBox(
        Microsoft::Xna::Framework::PlayerIndex player,
        const std::string& title,
        const std::string& text,
        const std::vector<std::string>& buttons,
        int focusButton,
        MessageBoxIcon icon,
        System::AsyncCallback callback,
        std::any state
    ) {
        ValidateShowMessageBoxArgs(player, title, text, buttons, focusButton);
        if (CNA::Internal::GamerServices::guideIsVisible())
            throw GuideAlreadyVisibleException();
        return CNA::Internal::GamerServices::showGuideMessageBox(
            player, title, text, buttons, focusButton, icon, std::move(callback), std::move(state));
    }

    std::optional<int> Guide::EndShowMessageBox(System::IAsyncResult* result)
    {
        if (result == nullptr) throw System::ArgumentNullException("result");
        auto* action = dynamic_cast<GuideMessageBoxAction*>(result);
        if (action == nullptr)
        {
            throw System::ArgumentException("result was not returned by a call to BeginShowMessageBox.", "result");
        }
        PrepareForEnd(*action, "EndShowMessageBox");
        return action->SelectedButton;
    }

    bool Guide::getHasPendingMessageBoxEXTProperty()
    {
        return pendingMessageBox_ != nullptr;
    }

    void Guide::RenderPendingMessageBoxEXT(
        Graphics::GraphicsDevice& device,
        Graphics::SpriteBatch& spriteBatch,
        Graphics::SpriteFont&,
        Graphics::Texture2D&
    ) {
        if (suppressTouchUntilMouseRelease_ &&
            CNA::Internal::Input::systemMouseState().getLeftButtonProperty() != Input::ButtonState::Pressed)
        {
            suppressTouchUntilMouseRelease_ = false;
            SyncTouchInputSuppression();
        }
        if (pendingMessageBox_ != nullptr)
        {
            CNA::Internal::GamerServices::GuideUi::drawGameDialogs(device, spriteBatch);
        }
    }

    void Guide::SimulateMessageBoxClickEXT(int buttonIndex)
    {
        if (pendingMessageBox_ == nullptr)
        {
            throw System::InvalidOperationException("No message box is currently pending.");
        }
        System::ArgumentOutOfRangeException::ThrowIfNegative(buttonIndex, "buttonIndex");
        System::ArgumentOutOfRangeException::ThrowIfGreaterThanOrEqual(
            buttonIndex, static_cast<int>(pendingMessageBox_->Buttons.size()), "buttonIndex"
        );
        CompletePendingMessageBox(buttonIndex);
    }

    void Guide::ResetPendingMessageBoxForTestingEXT()
    {
        pendingMessageBox_ = nullptr;
        suppressTouchUntilMouseRelease_ = false;
        SyncTouchInputSuppression();
    }

    int Guide::GetPendingMessageBoxFocusButtonForTestingEXT()
    {
        if (pendingMessageBox_ == nullptr)
        {
            throw System::InvalidOperationException("No message box is currently pending.");
        }
        return pendingMessageBox_->FocusButton;
    }
}

namespace CNA::Internal::GamerServices {
namespace {
// A sign-in in progress keeps the Guide visible; set by GuideSignInFlow.cpp.
bool signInActive=false;
}
bool guideSignInActive() {return signInActive;}
void setGuideSignInActive(bool active) {signInActive=active;}
void syncGuideTouchInput() {Microsoft::Xna::Framework::GamerServices::SyncTouchInputSuppression();}
System::IAsyncResult* showGuideKeyboardInput(Microsoft::Xna::Framework::PlayerIndex,const std::string& title,const std::string& description,
    const std::string& defaultText,System::AsyncCallback callback,std::any state,bool usePasswordMode) {
    return Microsoft::Xna::Framework::GamerServices::OpenKeyboardInputInternal(
        title,description,defaultText,std::move(callback),std::move(state),usePasswordMode);
}
bool guideIsVisible() {
    namespace Xna=Microsoft::Xna::Framework::GamerServices;
    return Xna::pendingMessageBox_!=nullptr||Xna::pendingKeyboardInput_!=nullptr||guideSignInActive()||GuideUi::visible();
}
System::IAsyncResult* showGuideMessageBox(Microsoft::Xna::Framework::PlayerIndex,const std::string& title,const std::string& text,
    const std::vector<std::string>& buttons,int focusButton,Microsoft::Xna::Framework::GamerServices::MessageBoxIcon icon,
    System::AsyncCallback callback,std::any state) {
    namespace Xna=Microsoft::Xna::Framework::GamerServices;
    if(buttons.empty())throw System::ArgumentException("buttons must contain at least one entry.","buttons");
    if(Xna::pendingMessageBox_!=nullptr)throw System::InvalidOperationException("A message box is already pending.");
    auto* action=new Xna::GuideMessageBoxAction(std::move(state),std::move(callback),title,text,buttons,focusButton,icon);
    Xna::pendingMessageBox_=action;
    Xna::SyncTouchInputSuppression();
    return action;
}
std::string guideSignInStatus() {
    return guideIsVisible() ? "CNA Gamer Services: signing in..." : "";
}
void syncSystemInputOwnership() {
    // The Guide owns the keyboard, the pads and the mouse buttons while it is up, as the console's
    // Guide took the controller; games read neutral input meanwhile (see SystemInput.hpp).
    CNA::Internal::Input::setSystemOwnsInput(guideIsVisible());
}
}

namespace CNA::Internal::GamerServices::GuideUi {
std::optional<MessageBoxView> pendingMessageBox() {
    namespace Xna=Microsoft::Xna::Framework::GamerServices;
    const auto* box=Xna::pendingMessageBox_;
    if(!box)return std::nullopt;
    return MessageBoxView{box->Title,box->Text,box->Buttons,box->FocusButton,box->Icon,box->Serial};
}
void focusMessageBox(int button) {
    namespace Xna=Microsoft::Xna::Framework::GamerServices;
    if(Xna::pendingMessageBox_&&button>=0&&button<static_cast<int>(Xna::pendingMessageBox_->Buttons.size()))Xna::pendingMessageBox_->FocusButton=button;
}
void answerMessageBox(std::optional<int> button) {
    namespace Xna=Microsoft::Xna::Framework::GamerServices;
    if(!Xna::pendingMessageBox_)return;
    // A click answers on the press; its release must not reach whatever the box was covering.
    if(CNA::Internal::Input::systemMouseState().getLeftButtonProperty()==Microsoft::Xna::Framework::Input::ButtonState::Pressed)
        Xna::suppressTouchUntilMouseRelease_=true;
    Xna::CompletePendingMessageBox(button);
}
std::optional<KeyboardView> pendingKeyboard() {
    namespace Xna=Microsoft::Xna::Framework::GamerServices;
    const auto* keys=Xna::pendingKeyboardInput_;
    if(!keys)return std::nullopt;
    return KeyboardView{keys->Title,keys->Description,Xna::ComputeDisplayText(keys),keys->Serial};
}
void typeKeyboard(char16_t character){Microsoft::Xna::Framework::GamerServices::HandleKeyboardCharacter(character);}
void cancelKeyboard() {
    namespace Xna=Microsoft::Xna::Framework::GamerServices;
    if(Xna::pendingKeyboardInput_)Xna::CompletePendingKeyboardInput(/*canceled=*/true);
}
void releaseTouchSuppression() {
    namespace Xna=Microsoft::Xna::Framework::GamerServices;
    if(Xna::suppressTouchUntilMouseRelease_&&
       CNA::Internal::Input::systemMouseState().getLeftButtonProperty()!=Microsoft::Xna::Framework::Input::ButtonState::Pressed) {
        Xna::suppressTouchUntilMouseRelease_=false;
        Xna::SyncTouchInputSuppression();
    }
}
}
