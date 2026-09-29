// SPDX-License-Identifier: MS-PL
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
#include "CNA/Internal/Runtime/IModalFrames.hpp"
#include "../Internal/ServiceAsyncResult.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include <algorithm>
#include <functional>
#include <cstdlib>
#include <array>
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
            // Reference XOverlappedAsyncResult.endHasBeenCalled.
            bool EndCalled{false};

        private:
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

    namespace {
        bool signInActive = false;
        bool socialPending = false;
        std::unique_ptr<System::IAsyncResult> socialAction;
        int signInPaneCount = 0;
        int signInSlot = 0;
        bool signInLocal = false;
        std::string signInUsername;
        std::string Folded(std::string value) {
            for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }
        bool ProfileSignedIn(const std::string& gamertag) {
            for(auto* gamer:*Gamer::getSignedInGamersProperty())
                if(Folded(gamer->getGamertagProperty())==Folded(gamertag))return true;
            return false;
        }
        void EndLocalSignIn(const std::string& message) {
            signInActive = false; SyncTouchInputSuppression();
            (void)CNA::Internal::GamerServices::showGuideMessageBox(static_cast<PlayerIndex>(signInSlot), "Sign in", message, {"OK"}, 0, MessageBoxIcon::Error,
                [](System::IAsyncResult& result) { std::unique_ptr<System::IAsyncResult> owned(&result); (void)Guide::EndShowMessageBox(&result); }, {});
        }
        // Without a service, a pane signs in a local offline profile, creating it on first use.
        void StartLocalSignInPane() {
            namespace Service = CNA::Internal::GamerServices;
            std::string known, suggestion; int listed = 0;
            for (const auto& profile : Service::loadLocalProfiles()) {
                if (ProfileSignedIn(profile.gamertag)) continue;
                if (suggestion.empty()) suggestion = profile.gamertag;
                if (listed++ < 8) known += (known.empty() ? "" : ", ") + profile.gamertag;
            }
            const auto player = "Profile for player " + std::to_string(signInSlot + 1);
            (void)CNA::Internal::GamerServices::showGuideKeyboardInput(static_cast<PlayerIndex>(signInSlot), "Sign in",
                known.empty() ? player + ". Enter a name to create a profile." : player + ": " + known + ", or a new name.",
                suggestion, [](System::IAsyncResult& input) {
                    std::unique_ptr<System::IAsyncResult> owned(&input);
                    if (Guide::WasKeyboardInputCanceledEXT(&input)) { signInActive = false; SyncTouchInputSuppression(); return; }
                    auto name = Guide::EndShowKeyboardInput(&input);
                    const auto first = name.find_first_not_of(' '), last = name.find_last_not_of(' ');
                    name = first == std::string::npos ? std::string{} : name.substr(first, last - first + 1);
                    if (!Service::isValidLocalGamertag(name)) {
                        EndLocalSignIn("Profile names are 1 to 15 letters, digits and single spaces, starting with a letter."); return;
                    }
                    if (ProfileSignedIn(name)) { EndLocalSignIn("That profile is already signed in."); return; }
                    const auto profile = Service::openLocalProfile(name);
                    try { Service::backend()->signInLocal(signInSlot, profile.gamertag); }
                    catch (...) { signInActive = false; SyncTouchInputSuppression(); throw; }
                    SyncTouchInputSuppression();
                }, {});
        }
        void StartSignInPane() {
            auto occupied=[](int slot) {
                for(auto* gamer:*Gamer::getSignedInGamersProperty())
                    if(gamer->getPlayerIndexProperty()==static_cast<PlayerIndex>(slot))return true;
                return false;
            };
            while (signInSlot < signInPaneCount && occupied(signInSlot)) ++signInSlot;
            if (signInSlot >= signInPaneCount) { signInActive = false; SyncTouchInputSuppression(); return; }
            if (signInLocal) { StartLocalSignInPane(); return; }
            (void)CNA::Internal::GamerServices::showGuideKeyboardInput(static_cast<PlayerIndex>(signInSlot), "CNA Gamer Services sign-in",
                "Username for player " + std::to_string(signInSlot + 1), "", [](System::IAsyncResult& usernameResult) {
                    std::unique_ptr<System::IAsyncResult> owned(&usernameResult);
                    if (Guide::WasKeyboardInputCanceledEXT(&usernameResult)) { signInActive = false; SyncTouchInputSuppression(); return; }
                    signInUsername = Guide::EndShowKeyboardInput(&usernameResult);
                    if(signInUsername.empty()||signInUsername.size()>64){signInActive=false;signInUsername.clear();SyncTouchInputSuppression();return;}
                    (void)CNA::Internal::GamerServices::showGuideKeyboardInput(static_cast<PlayerIndex>(signInSlot), "CNA Gamer Services sign-in", "Password", "",
                        [](System::IAsyncResult& passwordResult) {
                            std::unique_ptr<System::IAsyncResult> passwordOwned(&passwordResult);
                            if (Guide::WasKeyboardInputCanceledEXT(&passwordResult)) { signInActive = false; SyncTouchInputSuppression(); return; }
                            auto password = Guide::EndShowKeyboardInput(&passwordResult);
                            if(password.size()>256){std::fill(password.begin(),password.end(),'\0');signInActive=false;signInUsername.clear();SyncTouchInputSuppression();return;}
                            try {CNA::Internal::GamerServices::backend()->signIn(signInSlot, signInUsername, std::move(password));}
                            catch(...) {signInActive=false;signInUsername.clear();SyncTouchInputSuppression();throw;}
                            signInUsername.clear(); SyncTouchInputSuppression();
                        }, {}, true);
                }, {});
        }
    }

    bool Guide::simulateTrialMode_ = false;
    NotificationPosition Guide::position_ = NotificationPosition::BottomCenter;

    bool Guide::getIsScreenSaverEnabledProperty()
    {
        CNA::Platform::IPlatformDisplays* displays =
            CNA::Platform::GetCurrentPlatform().GetDisplays();
        return displays == nullptr || displays->IsScreenSaverEnabled();
    }

    void Guide::setIsScreenSaverEnabledProperty(bool value)
    {
        if (CNA::Platform::IPlatformDisplays* displays =
                CNA::Platform::GetCurrentPlatform().GetDisplays();
            displays != nullptr)
        {
            displays->SetScreenSaverEnabled(value);
        }
    }

    // Reference: SimulateTrialMode forces IsTrialMode to report true.
    // CNA titles are fully licensed; only simulating trial mode makes a trial.
    bool Guide::getIsTrialModeProperty()          { return simulateTrialMode_; }

    bool Guide::getIsVisibleProperty()
    {
        // Reference Guide.IsVisible: reading it before gamer services are initialized is an error.
        if (!GamerServicesDispatcher::getIsInitializedProperty())
            throw System::InvalidOperationException("Gamer services are not initialized.");
        return CNA::Internal::GamerServices::guideIsVisible();
    }

    NotificationPosition Guide::getNotificationPositionProperty() { return position_; }

    void Guide::setNotificationPositionProperty(NotificationPosition value)
    {
        if (value != position_)
            position_ = value;
    }

    bool Guide::getSimulateTrialModeProperty()          { return simulateTrialMode_; }
    void Guide::setSimulateTrialModeProperty(bool value) { simulateTrialMode_ = value; }

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
            [](Input::charcs c)
            {
                if (pendingKeyboardInput_ == nullptr)
                {
                    return;
                }
                // Enter/Return - FNA/CNA's SDL bridge synthesizes this as char code 13 on
                // KEY_DOWN (SDL doesn't deliver a real TEXT_INPUT event for it) - the most
                // faithful analog to a real Xbox 360 on-screen keyboard's "confirm" action.
                if (c == u'\r' || c == u'\n')
                {
                    CompletePendingKeyboardInput(/*canceled=*/false);
                    return;
                }
                // Backspace - synthesized the same way (char code 8). Needed for genuinely usable
                // real capture (fixing a typo before confirming); everything else the SDL bridge
                // can synthesize this way (Home=2, End=3, Tab=9, Delete=127, Ctrl+V paste=22) is
                // deliberately not handled - cursor repositioning/clipboard paste are out of scope
                // for this minimal, append/backspace-at-end capture model, and appending their
                // raw control-character codes as literal text would be worse than ignoring them.
                if (c == u'\b')
                {
                    RemoveLastCodeUnit(pendingKeyboardInput_->Buffer);
                    return;
                }
                if (c == 0x02 || c == 0x03 || c == 0x09 || c == 0x16 || c == 0x7F)
                {
                    return;
                }
                if (!signInActive || pendingKeyboardInput_->Buffer.size() < 256)
                    pendingKeyboardInput_->Buffer.push_back(c);
            }
        );
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
        Graphics::SpriteFont& font,
        Graphics::Texture2D& whitePixel
    ) {
        if (pendingKeyboardInput_ == nullptr)
        {
            return;
        }

        // Visual language matches the message box overlay (decision 5d): translucent white
        // rectangle, black text.
        const Color boxColor(255, 255, 255, 220);
        const Color textColor(0, 0, 0, 255);
        const Color hintColor(90, 90, 90, 255);

        const auto& viewport = device.getViewportProperty();
        const float viewportWidth = static_cast<float>(viewport.getWidthProperty());
        const float viewportHeight = static_cast<float>(viewport.getHeightProperty());

        const float padding = 16.0f;
        const float spacing = 8.0f;

        // usePasswordMode masks the on-screen display only - one '*' per typed UTF-16 code unit
        // (a coarse but standard on-screen-keyboard convention; doesn't attempt to collapse a
        // surrogate pair into a single mask character). EndShowKeyboardInput's own returned text
        // is always the real typed characters, matching real XNA - only the on-screen rendering
        // differs between the two overloads. See ComputeDisplayText's own comment - this is the
        // same decision GetPendingKeyboardInputDisplayTextForTestingEXT exposes for testing.
        std::string displayText = ComputeDisplayText(pendingKeyboardInput_);
        if (displayText.empty())
        {
            displayText = " ";
        }

        const std::string title = pendingKeyboardInput_->Title.empty() ? std::string(" ") : pendingKeyboardInput_->Title;
        const std::string description =
            pendingKeyboardInput_->Description.empty() ? std::string(" ") : pendingKeyboardInput_->Description;
        constexpr const char* hint = "Enter: confirm    Esc: cancel";

        const Vector2 titleSize = font.MeasureString(title);
        const Vector2 descriptionSize = font.MeasureString(description);
        const Vector2 textSize = font.MeasureString(displayText);
        const Vector2 hintSize = font.MeasureString(hint);

        // Not implemented: real word-wrap for long title/description/typed text - same minimal,
        // single-line-per-field scope as RenderPendingMessageBoxEXT.
        const float contentWidth = std::max(std::max(titleSize.X, descriptionSize.X), std::max(textSize.X, hintSize.X));
        const float boxWidth = std::min(viewportWidth - 2.0f * padding, std::max(360.0f, contentWidth + 2.0f * padding));
        const float boxHeight = padding * 2.0f + titleSize.Y + spacing + descriptionSize.Y + spacing
                                 + textSize.Y + spacing + hintSize.Y;

        const float boxX = (viewportWidth - boxWidth) * 0.5f;
        const float boxY = (viewportHeight - boxHeight) * 0.5f;

        spriteBatch.Draw(whitePixel,
                          Rectangle(static_cast<int>(boxX), static_cast<int>(boxY),
                                    static_cast<int>(boxWidth), static_cast<int>(boxHeight)),
                          std::nullopt, boxColor);

        float y = boxY + padding;
        spriteBatch.DrawString(font, title, Vector2(boxX + padding, y), textColor);
        y += titleSize.Y + spacing;
        spriteBatch.DrawString(font, description, Vector2(boxX + padding, y), textColor);
        y += descriptionSize.Y + spacing;
        spriteBatch.DrawString(font, displayText, Vector2(boxX + padding, y), textColor);
        y += textSize.Y + spacing;
        spriteBatch.DrawString(font, hint, Vector2(boxX + padding, y), hintColor);

        // Real Escape-key handling: cancel on the down-edge (not held/every frame), matching
        // RenderPendingMessageBoxEXT's own real-mouse-click edge detection (Input::Mouse there,
        // Input::Keyboard here). Escape is not part of TextInputEXT's own FNA-faithful control-
        // character synthesis table (Home/End/Backspace/Tab/Enter/Delete/Ctrl+V only, a byte-exact
        // port of FNA's own FNAPlatform.cs list) - adding it there would be a shared, FNA-fidelity
        // -sensitive change affecting every TextInputEXT consumer project-wide, not just this
        // Guide-local cancel feature, so this polls real keyboard state directly instead, exactly
        // like the message box already does for its own click detection.
        const Input::KeyboardState kb = Input::Keyboard::GetState();
        const bool escapeDown = kb.IsKeyDown(Input::Keys::Escape);
        const bool escapeEdge = escapeDown && !pendingKeyboardInput_->WasEscapeDown;
        pendingKeyboardInput_->WasEscapeDown = escapeDown;

        if (escapeEdge)
        {
            CompletePendingKeyboardInput(/*canceled=*/true);
        }
    }

    void Guide::SimulateKeyboardInputCancelEXT()
    {
        if (pendingKeyboardInput_ == nullptr)
        {
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
        Graphics::SpriteFont& font,
        Graphics::Texture2D& whitePixel
    ) {
        if (suppressTouchUntilMouseRelease_ &&
            Input::Mouse::GetState().getLeftButtonProperty() != Input::ButtonState::Pressed)
        {
            suppressTouchUntilMouseRelease_ = false;
            SyncTouchInputSuppression();
        }

        if (pendingMessageBox_ == nullptr)
        {
            return;
        }

        // Visual language matches the F1 help overlay (decision 5d): translucent white
        // rectangle, black text.
        const Color boxColor(255, 255, 255, 220);
        const Color textColor(0, 0, 0, 255);
        const Color buttonColor(210, 210, 210, 255);
        const Color buttonFocusColor(160, 200, 255, 255);

        const auto& viewport = device.getViewportProperty();
        const float viewportWidth = static_cast<float>(viewport.getWidthProperty());
        const float viewportHeight = static_cast<float>(viewport.getHeightProperty());

        const float padding = 16.0f;
        const float spacing = 12.0f;
        const float buttonPaddingX = 14.0f;
        const float buttonHeight = 32.0f;
        const float buttonGap = 12.0f;

        const Vector2 titleSize = font.MeasureString(pendingMessageBox_->Title.empty() ? " " : pendingMessageBox_->Title);
        const Vector2 textSize = font.MeasureString(pendingMessageBox_->Text.empty() ? " " : pendingMessageBox_->Text);

        // Button widths come first: the box must be wide enough for the whole button row.
        std::vector<Rectangle> buttonRects;
        buttonRects.reserve(pendingMessageBox_->Buttons.size());
        float totalButtonsWidth = 0.0f;
        std::vector<float> buttonWidths;
        buttonWidths.reserve(pendingMessageBox_->Buttons.size());
        for (const std::string& label : pendingMessageBox_->Buttons)
        {
            const float w = font.MeasureString(label.empty() ? " " : label).X + 2.0f * buttonPaddingX;
            buttonWidths.push_back(w);
            totalButtonsWidth += w;
        }
        totalButtonsWidth += buttonGap * static_cast<float>(pendingMessageBox_->Buttons.size() - 1);

        // Not implemented: real word-wrap for long body text - this is a minimal, single-line
        // overlay (matching this task's own "minimal" scope); a body string wider than the box
        // simply overflows past its edges rather than wrapping.
        const float contentWidth = std::max({titleSize.X, textSize.X, totalButtonsWidth});
        const float boxWidth = std::min(viewportWidth - 2.0f * padding, std::max(360.0f, contentWidth + 2.0f * padding));
        const float boxHeight = padding * 2.0f + titleSize.Y + spacing + textSize.Y
                                 + spacing + buttonHeight;

        const float boxX = (viewportWidth - boxWidth) * 0.5f;
        const float boxY = (viewportHeight - boxHeight) * 0.5f;

        spriteBatch.Draw(whitePixel,
                          Rectangle(static_cast<int>(boxX), static_cast<int>(boxY),
                                    static_cast<int>(boxWidth), static_cast<int>(boxHeight)),
                          std::nullopt, boxColor);

        spriteBatch.DrawString(font, pendingMessageBox_->Title, Vector2(boxX + padding, boxY + padding), textColor);
        spriteBatch.DrawString(font, pendingMessageBox_->Text,
                                Vector2(boxX + padding, boxY + padding + titleSize.Y + spacing), textColor);

        // Lay out button rectangles left-to-right, centered as a group within the box.
        float buttonX = boxX + (boxWidth - totalButtonsWidth) * 0.5f;
        const float buttonY = boxY + boxHeight - padding - buttonHeight;
        for (std::size_t i = 0; i < pendingMessageBox_->Buttons.size(); ++i)
        {
            const Rectangle rect(static_cast<int>(buttonX), static_cast<int>(buttonY),
                                  static_cast<int>(buttonWidths[i]), static_cast<int>(buttonHeight));
            buttonRects.push_back(rect);

            const bool isFocused = static_cast<int>(i) == pendingMessageBox_->FocusButton;
            spriteBatch.Draw(whitePixel, rect, std::nullopt, isFocused ? buttonFocusColor : buttonColor);
            const Vector2 labelSize = font.MeasureString(pendingMessageBox_->Buttons[i]);
            const Vector2 labelPos(
                buttonX + (buttonWidths[i] - labelSize.X) * 0.5f,
                buttonY + (buttonHeight - labelSize.Y) * 0.5f
            );
            spriteBatch.DrawString(font, pendingMessageBox_->Buttons[i], labelPos, textColor);

            buttonX += buttonWidths[i] + buttonGap;
        }

        // Real mouse-click handling: select on the down-edge of the left button (not held/every
        // frame), matching ordinary UI button semantics.
        const Input::MouseState mouse = Input::Mouse::GetState();
        const bool leftDown = mouse.getLeftButtonProperty() == Input::ButtonState::Pressed;
        const bool clickEdge = leftDown && !pendingMessageBox_->WasLeftMouseDown;
        pendingMessageBox_->WasLeftMouseDown = leftDown;

        if (clickEdge)
        {
            for (std::size_t i = 0; i < buttonRects.size(); ++i)
            {
                if (buttonRects[i].Contains(mouse.getXProperty(), mouse.getYProperty()))
                {
                    suppressTouchUntilMouseRelease_ = true;
                    CompletePendingMessageBox(static_cast<int>(i));
                    return;
                }
            }
        }
        // Keyboard and gamepad answer the box too, as the console Guide is answered: arrows,
        // Tab, the D-pad or the left stick move the focus, Enter/Space/A choose, Escape/B/Back
        // cancel (EndShowMessageBox then returns no button).
        const Input::KeyboardState keys = Input::Keyboard::GetState();
        bool previous = keys.IsKeyDown(Input::Keys::Left) || keys.IsKeyDown(Input::Keys::Up);
        bool next = keys.IsKeyDown(Input::Keys::Right) || keys.IsKeyDown(Input::Keys::Down) || keys.IsKeyDown(Input::Keys::Tab);
        bool select = keys.IsKeyDown(Input::Keys::Enter) || keys.IsKeyDown(Input::Keys::Space);
        bool cancel = keys.IsKeyDown(Input::Keys::Escape);
        for (int index = 0; index < 4; ++index)
        {
            const Input::GamePadState pad = Input::GamePad::GetState(static_cast<PlayerIndex>(index));
            const Vector2 stick = pad.getThumbSticksProperty().getLeftProperty();
            previous = previous || pad.IsButtonDown(Input::Buttons::DPadLeft) || pad.IsButtonDown(Input::Buttons::DPadUp) ||
                       stick.X < -0.5f || stick.Y > 0.5f;
            next = next || pad.IsButtonDown(Input::Buttons::DPadRight) || pad.IsButtonDown(Input::Buttons::DPadDown) ||
                   stick.X > 0.5f || stick.Y < -0.5f;
            select = select || pad.IsButtonDown(Input::Buttons::A);
            cancel = cancel || pad.IsButtonDown(Input::Buttons::B) || pad.IsButtonDown(Input::Buttons::Back);
        }
        auto edge = [](bool down, bool& was) { const bool pressed = down && !was; was = down; return pressed; };
        const int count = static_cast<int>(pendingMessageBox_->Buttons.size());
        if (edge(previous, pendingMessageBox_->WasPreviousDown))
        {
            pendingMessageBox_->FocusButton = (pendingMessageBox_->FocusButton + count - 1) % count;
        }
        if (edge(next, pendingMessageBox_->WasNextDown))
        {
            pendingMessageBox_->FocusButton = (pendingMessageBox_->FocusButton + 1) % count;
        }
        if (edge(select, pendingMessageBox_->WasSelectDown))
        {
            CompletePendingMessageBox(pendingMessageBox_->FocusButton);
            return;
        }
        if (edge(cancel, pendingMessageBox_->WasCancelDown))
        {
            CompletePendingMessageBox(std::nullopt);
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

    void Guide::DelayNotifications(System::TimeSpan delay)
    {
        // Defers the Guide's own notifications (game invitations); at most 120 s, an active delay stays.
        CNA::Internal::GamerServices::delayNotifications(static_cast<long long>(delay.getTotalMillisecondsProperty()));
    }

    namespace {
        namespace Service = CNA::Internal::GamerServices;
        SignedInGamer* SocialActor(PlayerIndex player) {
            const auto index=static_cast<int>(player);
            if(index<0||index>3)throw System::ArgumentOutOfRangeException("player");
            if(!Service::backend()->serviceEnabled())throw GamerServicesNotAvailableException("No CNA account service is configured.");
            for(auto* gamer:*Gamer::getSignedInGamersProperty())
                if(gamer->getPlayerIndexProperty()==player&&gamer->getIsSignedInToLiveProperty())return gamer;
            throw GamerServicesNotAvailableException("The requested player is not signed in.");
        }
        void SocialMessage(PlayerIndex player,const std::string& title,const std::string& text) {
            (void)CNA::Internal::GamerServices::showGuideMessageBox(player,title,text,{"OK"},0,MessageBoxIcon::None,[](System::IAsyncResult& result){
                std::unique_ptr<System::IAsyncResult> owned(&result);(void)Guide::EndShowMessageBox(&result);
            },{});
        }
        void ValidateRecipients(const std::vector<Gamer*>& recipients) {
            // Reference Gamer.ValidateGamerList: at most 100 gamers, none null or disposed.
            if(recipients.size()>100)throw System::ArgumentException("Too many gamers.","recipients");
            for(auto* gamer:recipients) {
                if(!gamer)throw System::ArgumentException("Gamer is null.","recipients");
                if(gamer->getIsDisposedProperty())throw System::ObjectDisposedException("recipients");
            }
        }
        void ValidateGamer(Gamer* gamer) {
            if(!gamer)throw System::ArgumentNullException("gamer");
            if(gamer->getIsDisposedProperty())throw System::ObjectDisposedException("gamer");
        }
        // Runs one service call on the backend executor; the outcome is reported at Update.
        void SocialCall(PlayerIndex player,std::function<void(Service::IGamerServicesBackend&)> work,std::string failure) {
            auto service=Service::backend();auto* executor=service.get();auto failed=std::make_shared<bool>(false);
            service->submit([executor,work=std::move(work),failed]{try{work(*executor);}catch(...){*failed=true;}},
                [player,failed,failure=std::move(failure)]{if(*failed)SocialMessage(player,"CNA Gamer Services",failure);});
        }
        void SendMessage(PlayerIndex player,const std::string& user,const std::vector<std::string>& tags,const std::string& text) {
            SocialCall(player,[user,tags,text](auto& executor){executor.sendMessage(user,tags,text);},"The message could not be sent.");
        }
        void Compose(PlayerIndex player,const std::string& user,const std::vector<std::string>& tags,const std::string& text) {
            std::string names;for(const auto& tag:tags)names+=(names.empty()?"":", ")+tag;
            (void)CNA::Internal::GamerServices::showGuideKeyboardInput(player,"Compose message",names.empty()?"Message":"To: "+names,text,
                [player,user,tags](System::IAsyncResult& input) {
                    std::unique_ptr<System::IAsyncResult> owned(&input);
                    if(Guide::WasKeyboardInputCanceledEXT(&input))return;
                    const auto body=Guide::EndShowKeyboardInput(&input);
                    if(!tags.empty()){SendMessage(player,user,tags,body);return;}
                    (void)CNA::Internal::GamerServices::showGuideKeyboardInput(player,"Compose message","Recipient gamertag","",[player,user,body](System::IAsyncResult& recipient) {
                        std::unique_ptr<System::IAsyncResult> ownedRecipient(&recipient);
                        if(Guide::WasKeyboardInputCanceledEXT(&recipient))return;
                        const auto tag=Guide::EndShowKeyboardInput(&recipient);
                        if(!tag.empty())SendMessage(player,user,{tag},body);
                    },{});
                },{});
        }
        void Inbox(PlayerIndex player,const std::string& user,int index) {
            const auto page=Service::backend()->messages(user,index,1);
            if(page.messages.empty()) {
                SocialMessage(player,"Messages",index==0?"You have no messages.":"No more messages.");return;
            }
            const auto message=page.messages.front();
            if(!message.read)try{Service::backend()->updateMessage(user,message.id,false);}catch(...){}
            (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Messages ("+std::to_string(index+1)+"/"+std::to_string(page.total)+")",
                "From "+message.sender+":\n"+message.text,{"Next","Reply","Delete","Close"},0,MessageBoxIcon::None,
                [player,user,index,message](System::IAsyncResult& result) {
                    std::unique_ptr<System::IAsyncResult> owned(&result);const auto answer=Guide::EndShowMessageBox(&result);
                    if(!answer||*answer==3)return;
                    try {
                        if(*answer==0)Inbox(player,user,index+1);
                        else if(*answer==1)Compose(player,user,{message.sender},"");
                        else {Service::backend()->updateMessage(user,message.id,true);Inbox(player,user,index);}
                    }catch(...){SocialMessage(player,"Messages","The messages could not be read.");}
                },{});
        }
        void Information(PlayerIndex player,const std::string& title,const std::string& text) {
            (void)SocialActor(player);SocialMessage(player,title,text);
        }
        void ChangeFriend(PlayerIndex player,const std::string& target,const std::string& action) {
            if(socialPending)throw System::InvalidOperationException("A social operation is already pending.");
            auto service=Service::backend();const auto user=service->profile(SocialActor(player)->getGamertagProperty()).userId;
            socialPending=true;
            try {
                socialAction.reset(Service::ServiceAsyncResult::begin("guide.friend",nullptr,[user,target,action](auto& executor)->std::any{
                    executor.changeFriend(user,target,action);return {};
                },[player](System::IAsyncResult& result){
                    auto owned=std::move(socialAction);socialPending=false;
                    try{(void)Service::ServiceAsyncResult::end(&result,"guide.friend",nullptr);Guide::ShowFriends(player);}
                    catch(...){SocialMessage(player,"CNA Gamer Services","The friendship change could not be completed.");}
                },{},std::move(service)));
            }catch(...){socialPending=false;throw;}
        }
        void SendInvitations(PlayerIndex player,const std::string& user,const std::vector<std::string>& tags) {
            try {
                Service::sendInvitations(user,tags,[player,count=tags.size()](int failures) {
                    if(failures)SocialMessage(player,"Game invitation",failures==static_cast<int>(count)
                        ?"The invitation could not be sent.":"Some invitations could not be sent.");
                });
            }catch(...){SocialMessage(player,"Game invitation","There is no online game to invite gamers to.");}
        }
        void ConfirmInvitations(PlayerIndex player,const std::string& user,const std::vector<std::string>& tags) {
            std::string names;for(const auto& tag:tags)names+=(names.empty()?"":", ")+tag;
            (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Game invitation","Invite "+names+" to join your game?",{"Send invitation","Cancel"},0,
                MessageBoxIcon::None,[player,user,tags](System::IAsyncResult& result) {
                    std::unique_ptr<System::IAsyncResult> owned(&result);const auto answer=Guide::EndShowMessageBox(&result);
                    if(answer&&*answer==0)SendInvitations(player,user,tags);
                },{});
        }
        void ProfileCard(PlayerIndex player,const std::string& tag) {
            auto* actor=SocialActor(player);auto service=Service::backend();const auto person=service->profile(tag);
            const auto user=service->profile(actor->getGamertagProperty()).userId;
            std::string action="add",label="Request friendship";
            for(const auto& entry:service->friends(user))if(entry.gamertag==person.gamertag) {
                if(entry.accepted){action="remove";label="Remove friend";}
                else if(entry.requestReceived){action="accept";label="Accept request";}
                else if(entry.requestSent){action="remove";label="Cancel request";}
            }
            const auto text=person.displayName+"\n"+person.motto+"\nGamer score: "+std::to_string(person.gamerScore)+
                "    Achievements: "+std::to_string(person.totalAchievements)+"\nRegion: "+person.region;
            const bool self=person.userId==user;
            const bool invite=!self&&Service::activeOnlineSession().has_value();
            std::vector<std::string> buttons;
            if(!self)buttons.push_back(label);
            if(invite)buttons.push_back("Invite to game");
            buttons.push_back("Friends");buttons.push_back("Close");
            (void)CNA::Internal::GamerServices::showGuideMessageBox(player,person.gamertag,text,buttons,0,MessageBoxIcon::None,
                [player,tag=person.gamertag,action,self,invite,user](System::IAsyncResult& result){
                    std::unique_ptr<System::IAsyncResult> owned(&result);const auto answer=Guide::EndShowMessageBox(&result);
                    if(!answer)return;
                    int index=*answer;
                    if(!self&&index==0){ChangeFriend(player,tag,action);return;}
                    if(!self)--index;
                    if(invite&&index==0){SendInvitations(player,user,{tag});return;}
                    if(invite)--index;
                    if(index==0)Guide::ShowFriends(player);
                },{});
        }
        void FriendsPage(PlayerIndex player,std::size_t offset) {
            auto* actor=SocialActor(player);const auto friends=actor->GetFriends();std::string text;
            const auto count=static_cast<std::size_t>(friends.getCountProperty());
            for(std::size_t i=offset;i<std::min(offset+8,count);++i) {
                auto* entry=friends[static_cast<int>(i)];text+=entry->getGamertagProperty();
                if(entry->getFriendRequestReceivedFromProperty())text+=" - incoming request";
                else if(entry->getFriendRequestSentToProperty())text+=" - sent request";
                else text+=entry->getIsOnlineProperty()?" - online":" - offline";
                if(!entry->getPresenceProperty().empty())text+=" - "+entry->getPresenceProperty();text+="\n";
            }
            if(text.empty())text="No friends or pending requests.";
            (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"CNA Friends",text,{"Find gamer","More","Close"},0,MessageBoxIcon::None,
                [player,offset,count](System::IAsyncResult& result){
                    std::unique_ptr<System::IAsyncResult> owned(&result);const auto answer=Guide::EndShowMessageBox(&result);
                    if(answer&&*answer==1)FriendsPage(player,offset+8<count?offset+8:0);
                    else if(answer&&*answer==0) {
                        (void)CNA::Internal::GamerServices::showGuideKeyboardInput(player,"CNA Gamer Card","Gamertag","",[player](System::IAsyncResult& input){
                            std::unique_ptr<System::IAsyncResult> ownedInput(&input);
                            if(Guide::WasKeyboardInputCanceledEXT(&input))return;
                            const auto tag=Guide::EndShowKeyboardInput(&input);
                            try{ProfileCard(player,tag);}catch(...){SocialMessage(player,"CNA Gamer Card","The gamer could not be found.");}
                        },{});
                    }
                },{});
        }
    }
    void Guide::ShowComposeMessage(PlayerIndex player,const std::string& text,const std::vector<Gamer*>& recipients) {
        // Reference: text shorter than 256 characters, then the recipient list, then the Guide.
        if(text.size()>=256)throw System::ArgumentException("Text is too long.","text");
        ValidateRecipients(recipients);
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        auto* actor=SocialActor(player);std::vector<std::string> tags;
        for(auto* gamer:recipients)tags.push_back(gamer->getGamertagProperty());
        Compose(player,Service::GamerAccess::userId(*actor),tags,text);
    }
    void Guide::ShowFriendRequest(PlayerIndex player,Gamer* gamer) {
        ValidateGamer(gamer);
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        const auto tag=gamer->getGamertagProperty();
        (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Friend request","Send a friendship request to "+tag+"?",{"Send request","Cancel"},0,MessageBoxIcon::None,
            [player,tag](System::IAsyncResult& result){std::unique_ptr<System::IAsyncResult> owned(&result);const auto answer=EndShowMessageBox(&result);if(answer&&*answer==0)ChangeFriend(player,tag,"add");},{});
    }
    void Guide::ShowFriends(PlayerIndex player) {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        FriendsPage(player,0);
    }

    void Guide::ShowGameInvite(PlayerIndex player,const std::vector<Gamer*>& recipients) {
        // Reference ShowGameInvite validates the recipient list (at most 100, no null/disposed
        // gamers) before showing the Guide; an empty list prompts for the recipient.
        if(recipients.size()>100)throw System::ArgumentException("Too many gamers.","recipients");
        for(auto* gamer:recipients) {
            if(!gamer)throw System::ArgumentException("Gamer is null.","recipients");
            if(gamer->getIsDisposedProperty())throw System::ObjectDisposedException("recipients");
        }
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        auto* actor=SocialActor(player);
        if(!Service::activeOnlineSession())
            throw System::InvalidOperationException("There is no online network session to invite gamers to.");
        const auto user=Service::GamerAccess::userId(*actor);
        std::vector<std::string> tags;for(auto* gamer:recipients)tags.push_back(gamer->getGamertagProperty());
        if(!tags.empty()){ConfirmInvitations(player,user,tags);return;}
        (void)CNA::Internal::GamerServices::showGuideKeyboardInput(player,"Game invitation","Gamertag to invite","",[player,user](System::IAsyncResult& input) {
            std::unique_ptr<System::IAsyncResult> owned(&input);
            if(Guide::WasKeyboardInputCanceledEXT(&input))return;
            const auto tag=Guide::EndShowKeyboardInput(&input);
            if(!tag.empty())ConfirmInvitations(player,user,{tag});
        },{});
    }

    void Guide::ShowGameInvite(const std::string& /*sessionId*/)
    {
        // The reference assembly supports this overload only for Windows Phone LIVE titles.
        throw System::NotSupportedException("ShowGameInvite(sessionId) is supported only on Windows Phone.");
    }

    void Guide::ShowGamerCard(PlayerIndex player,Gamer* gamer) {
        ValidateGamer(gamer);
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        ProfileCard(player,gamer->getGamertagProperty());
    }

    void Guide::ShowMarketplace(PlayerIndex player)
    {
        // Reference: a signed-in LIVE profile with the purchase privilege, else GamerPrivilegeException.
        SignedInGamer* gamer=nullptr;
        for(auto* candidate:*Gamer::getSignedInGamersProperty())if(candidate->getPlayerIndexProperty()==player)gamer=candidate;
        if(!gamer||!gamer->getIsSignedInToLiveProperty())throw GamerPrivilegeException("The profile is not signed in.");
        if(!gamer->getPrivilegesProperty().getAllowPurchaseContentProperty())throw GamerPrivilegeException("The profile may not purchase content.");
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        // CNA runs no store or payment service; titles are fully licensed (IsTrialMode stays false).
        Information(player,"Marketplace","CNA Gamer Services has no marketplace. This title is fully licensed; there is nothing to purchase.");
    }

    void Guide::ShowMessages(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        auto* actor=SocialActor(player);
        try{Inbox(player,Service::GamerAccess::userId(*actor),0);}
        catch(const GuideAlreadyVisibleException&){throw;}
        catch(...){SocialMessage(player,"Messages","The messages could not be read.");}
    }

    void Guide::ShowParty(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        // There is no cross-title voice/party service in CNA; the Guide says so instead of doing nothing.
        Information(player,"Party","CNA Gamer Services has no party service. Use game invitations to play with friends.");
    }

    void Guide::ShowPartySessions(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        Information(player,"Party sessions","CNA Gamer Services has no party service, so there are no party sessions to join.");
    }

    void Guide::ShowPlayerReview(PlayerIndex player, Gamer* gamer)
    {
        ValidateGamer(gamer);
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        auto* actor=SocialActor(player);const auto user=Service::GamerAccess::userId(*actor);const auto tag=gamer->getGamertagProperty();
        (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Player review","How was playing with "+tag+"?\nAvoided players' games are not offered to you.",
            {"Prefer","Avoid","Clear review","Cancel"},0,MessageBoxIcon::None,[player,user,tag](System::IAsyncResult& result) {
                std::unique_ptr<System::IAsyncResult> owned(&result);const auto answer=EndShowMessageBox(&result);
                if(!answer||*answer==3)return;
                const std::string rating=*answer==0?"prefer":*answer==1?"avoid":"clear";
                SocialCall(player,[user,tag,rating](auto& executor){executor.reviewPlayer(user,tag,rating);},"The review could not be recorded.");
            },{});
    }

    void Guide::ShowPlayers(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        std::string text;int shown=0;
        for(const auto& tag:Service::recentPlayers()){if(shown++==8)break;text+=tag+"\n";}
        if(text.empty())text="You have not played with anyone yet.";
        (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Recent players",text,{"Gamer card","Close"},0,MessageBoxIcon::None,[player](System::IAsyncResult& result) {
            std::unique_ptr<System::IAsyncResult> owned(&result);const auto answer=EndShowMessageBox(&result);
            if(!answer||*answer!=0)return;
            (void)CNA::Internal::GamerServices::showGuideKeyboardInput(player,"Recent players","Gamertag","",[player](System::IAsyncResult& input) {
                std::unique_ptr<System::IAsyncResult> ownedInput(&input);
                if(WasKeyboardInputCanceledEXT(&input))return;
                const auto tag=EndShowKeyboardInput(&input);
                try{ProfileCard(player,tag);}catch(...){SocialMessage(player,"Recent players","The gamer could not be found.");}
            },{});
        },{});
    }

    void Guide::ShowSignIn(int paneCount, bool onlineOnly) {
        // Xbox 360 documentation accepts 1, 2, 4; Windows stub IL is not the target.
        if (paneCount != 1 && paneCount != 2 && paneCount != 4) throw System::ArgumentException("paneCount must be 1, 2 or 4.", "paneCount");
        if (CNA::Internal::GamerServices::guideIsVisible()) throw GuideAlreadyVisibleException();
        if (!GamerServicesDispatcher::getIsInitializedProperty())
            throw GamerServicesNotAvailableException("Gamer services are not initialized.");
        // Without a service only local offline profiles exist, which an online-only sign-in excludes.
        const bool service = CNA::Internal::GamerServices::backend()->serviceEnabled();
        if (!service && onlineOnly) throw GamerServicesNotAvailableException("No CNA account service is configured.");
        signInPaneCount = paneCount; signInSlot = 0; signInActive = true; signInLocal = !service;
        try { StartSignInPane(); } catch (...) { signInActive = false; SyncTouchInputSuppression(); throw; }
    }
    void Guide::OnSignInResult(int slot, bool success) {
        if (!signInActive || slot != signInSlot) return;
        if (signInLocal && !success) return;
        if (success) { ++signInSlot; StartSignInPane(); }
        else {
            signInActive = false;
            (void)CNA::Internal::GamerServices::showGuideMessageBox(static_cast<PlayerIndex>(slot), "CNA Gamer Services", "Sign-in failed. Check the account and service connection.",
                {"OK"}, 0, MessageBoxIcon::Error, [](System::IAsyncResult& result) {
                    std::unique_ptr<System::IAsyncResult> owned(&result); (void)Guide::EndShowMessageBox(&result);
                }, {});
        }
        SyncTouchInputSuppression();
    }

    void Guide::ShowAchievementsEXT(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        auto* actor=SocialActor(player);std::string text;int earned=0,shown=0;
        const auto achievements=Service::backend()->achievements(Service::GamerAccess::userId(*actor));
        for(const auto& achievement:achievements) {
            if(achievement.earnedTicks)++earned;
            if(shown++<10)text+=(achievement.earnedTicks?"[x] ":"[ ] ")+achievement.name+"\n";
        }
        if(achievements.empty())text="This title has no achievements.";
        SocialMessage(player,"Achievements ("+std::to_string(earned)+"/"+std::to_string(achievements.size())+")",text);
    }
}

namespace CNA::Internal::GamerServices {
System::IAsyncResult* showGuideKeyboardInput(Microsoft::Xna::Framework::PlayerIndex,const std::string& title,const std::string& description,
    const std::string& defaultText,System::AsyncCallback callback,std::any state,bool usePasswordMode) {
    return Microsoft::Xna::Framework::GamerServices::OpenKeyboardInputInternal(
        title,description,defaultText,std::move(callback),std::move(state),usePasswordMode);
}
bool guideIsVisible() {
    namespace Xna=Microsoft::Xna::Framework::GamerServices;
    return Xna::pendingMessageBox_!=nullptr||Xna::pendingKeyboardInput_!=nullptr||Xna::signInActive||Xna::socialPending;
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

namespace {
void systemGuideAction(Microsoft::Xna::Framework::PlayerIndex player,const std::function<void()>& action) {
    using namespace Microsoft::Xna::Framework::GamerServices;
    try {action();}
    catch(const std::exception& error) {
        (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Guide",error.what(),{"OK"},0,MessageBoxIcon::Error,[](System::IAsyncResult& result) {
            std::unique_ptr<System::IAsyncResult> owned(&result);(void)Guide::EndShowMessageBox(&result);
        },{});
    }
}
}

void openSystemGuide(Microsoft::Xna::Framework::PlayerIndex player) {
    using namespace Microsoft::Xna::Framework::GamerServices;
    using Microsoft::Xna::Framework::PlayerIndex;
    const int index=static_cast<int>(player);
    if(index<0||index>3||!GamerServicesDispatcher::getIsInitializedProperty()||guideIsVisible())return;
    SignedInGamer* gamer=nullptr;
    for(auto* candidate:*Gamer::getSignedInGamersProperty())if(candidate->getPlayerIndexProperty()==player)gamer=candidate;
    auto close=[](System::IAsyncResult& result){std::unique_ptr<System::IAsyncResult> owned(&result);return Guide::EndShowMessageBox(&result);};
    if(!gamer) {
        // Sign-in panes cover this player's slot: 1, 2 or 4 of them.
        const int panes=index==0?1:index==1?2:4;
        (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Guide","No profile is signed in for player "+std::to_string(index+1)+".",
            {"Sign in","Close"},0,MessageBoxIcon::None,[player,panes,close](System::IAsyncResult& result) {
                if(close(result)==0)systemGuideAction(player,[panes]{Guide::ShowSignIn(panes,false);});
            },{});
        return;
    }
    if(!gamer->getIsSignedInToLiveProperty()) {
        (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Guide","Signed in to the local profile "+gamer->getGamertagProperty()+
            ". Online features need a CNA account service.",{"Sign out","Close"},1,MessageBoxIcon::None,[player,index,close](System::IAsyncResult& result) {
                if(close(result)==0)systemGuideAction(player,[index]{backend()->signOut(index);});
            },{});
        return;
    }
    const auto user=CNA::Internal::GamerServices::GamerAccess::userId(*gamer);
    (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Guide","Signed in as "+gamer->getGamertagProperty()+".",
        {"Friends","Invite to game","Messages","Online status","Sign out"},0,MessageBoxIcon::None,[player,index,user,close](System::IAsyncResult& result) {
            const auto choice=close(result);
            if(!choice)return;
            switch(*choice) {
            case 0:systemGuideAction(player,[player]{Guide::ShowFriends(player);});break;
            case 1:systemGuideAction(player,[player]{Guide::ShowGameInvite(player,std::vector<Gamer*>{});});break;
            case 2:systemGuideAction(player,[player]{Guide::ShowMessages(player);});break;
            case 3:
                // What friends see as FriendGamer.IsAway/IsBusy: only ever what the player chose.
                (void)CNA::Internal::GamerServices::showGuideMessageBox(player,"Online status","Choose how your friends see you.",
                    {"Online","Away","Busy"},0,MessageBoxIcon::None,[player,user,close](System::IAsyncResult& status) {
                        const auto picked=close(status);
                        if(!picked)return;
                        static const char* const names[]={"online","away","busy"};
                        const std::string name=names[std::clamp(*picked,0,2)];
                        SocialCall(player,[user,name](auto& executor){executor.setPresenceStatus(user,name);},
                            "The online status could not be changed.");
                    },{});
                break;
            default:systemGuideAction(player,[index]{backend()->signOut(index);});break;
            }
        },{});
}

void pollSystemGuideButton() {
    using namespace Microsoft::Xna::Framework::Input;
    using Microsoft::Xna::Framework::PlayerIndex;
    static const bool disabled=[]{const auto* value=std::getenv("CNA_GAMER_SERVICES_GUIDE_BUTTON");return value&&std::string(value)=="0";}();
    static std::array<bool,5> previous{};
    if(disabled)return;
    // Home opens the Guide from a keyboard, as in Games for Windows LIVE; the Guide button from a pad.
    const bool home=Keyboard::GetState().IsKeyDown(Keys::Home);
    if(home&&!previous[4])openSystemGuide(PlayerIndex::One);
    previous[4]=home;
    for(int index=0;index<4;++index) {
        const bool pressed=GamePad::GetState(static_cast<PlayerIndex>(index)).IsButtonDown(Buttons::BigButton);
        if(pressed&&!previous[index])openSystemGuide(static_cast<PlayerIndex>(index));
        previous[index]=pressed;
    }
}
}
