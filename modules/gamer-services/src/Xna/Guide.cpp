// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
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
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include <algorithm>

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
            void setIsCompletedProperty(bool value) { isCompleted_ = value; }

            [[nodiscard]] System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override
            {
                return asyncWaitHandle_;
            }

            const System::AsyncCallback Callback;

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
            const int FocusButton;
            const MessageBoxIcon Icon;
            std::optional<int> SelectedButton;

            // Edge-detection state for RenderPendingMessageBoxEXT's real mouse-click handling -
            // a button selects on the down-edge of the left mouse button, not every frame it's
            // held, so this must persist across calls for as long as this box is pending.
            bool WasLeftMouseDown = false;
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
        void CompletePendingMessageBox(int buttonIndex)
        {
            GuideMessageBoxAction* action = pendingMessageBox_;
            action->SelectedButton = buttonIndex;
            action->setIsCompletedProperty(true);
            pendingMessageBox_ = nullptr;
            SyncTouchInputSuppression();
            if (action->Callback)
            {
                action->Callback(*action);
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
                pendingMessageBox_ != nullptr || pendingKeyboardInput_ != nullptr ||
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
                action->Callback(*action);
            }
        }
    }

    bool Guide::isTrialMode_ = false;
    bool Guide::simulateTrialMode_ = false;
    NotificationPosition Guide::position_ = NotificationPosition::BottomRight;

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

    bool Guide::getIsTrialModeProperty()          { return isTrialMode_; }
    void Guide::setIsTrialModeProperty(bool value) { isTrialMode_ = value; }

    bool Guide::getIsVisibleProperty()
    {
        return pendingMessageBox_ != nullptr || pendingKeyboardInput_ != nullptr;
    }
    void Guide::setIsVisibleProperty(bool /*value*/) { }

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
        Microsoft::Xna::Framework::PlayerIndex /*player*/,
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
                pendingKeyboardInput_->Buffer.push_back(c);
            }
        );
        return action;
    }

    std::string Guide::EndShowKeyboardInput(System::IAsyncResult* result)
    {
        auto* action = dynamic_cast<GuideKeyboardInputAction*>(result);
        if (action == nullptr)
        {
            throw System::ArgumentException("result was not returned by a call to BeginShowKeyboardInput.", "result");
        }
        if (!action->getIsCompletedProperty())
        {
            throw System::InvalidOperationException(
                "The keyboard input has not been confirmed yet - it completes when the user "
                "presses Enter (or via a test's simulated TextInputEXT::INTERNAL_OnTextInput(u'\\r'))."
            );
        }
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
        // No FNA reference behavior exists for this validation (FNA's own BeginShowMessageBox is
        // a permanent NotSupportedException stub, "FIXME: Surely they don't want us doing this");
        // this is a CNA-original, conservative default for a real implementation - an empty
        // button list has no button to ever select.
        if (buttons.empty())
        {
            throw System::ArgumentException("buttons must contain at least one entry.", "buttons");
        }
        if (pendingMessageBox_ != nullptr)
        {
            throw System::InvalidOperationException("A message box is already pending.");
        }

        auto* action = new GuideMessageBoxAction(
            std::move(state), std::move(callback), title, text, buttons, focusButton, icon
        );
        pendingMessageBox_ = action;
        SyncTouchInputSuppression();
        return action;
    }

    System::IAsyncResult* Guide::BeginShowMessageBox(
        Microsoft::Xna::Framework::PlayerIndex /*player*/,
        const std::string& title,
        const std::string& text,
        const std::vector<std::string>& buttons,
        int focusButton,
        MessageBoxIcon icon,
        System::AsyncCallback callback,
        std::any state
    ) {
        return BeginShowMessageBox(title, text, buttons, focusButton, icon, std::move(callback), std::move(state));
    }

    std::optional<int> Guide::EndShowMessageBox(System::IAsyncResult* result)
    {
        auto* action = dynamic_cast<GuideMessageBoxAction*>(result);
        if (action == nullptr)
        {
            throw System::ArgumentException("result was not returned by a call to BeginShowMessageBox.", "result");
        }
        if (!action->getIsCompletedProperty())
        {
            throw System::InvalidOperationException(
                "The message box has not been answered yet - render it via RenderPendingMessageBoxEXT "
                "(or resolve it via SimulateMessageBoxClickEXT) before calling EndShowMessageBox."
            );
        }
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

        // Not implemented: real word-wrap for long body text - this is a minimal, single-line
        // overlay (matching this task's own "minimal" scope); a body string wider than the box
        // simply overflows past its edges rather than wrapping.
        const float contentWidth = std::max(titleSize.X, textSize.X);
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

    void Guide::DelayNotifications(System::TimeSpan /*delay*/)
    {
    }

    void Guide::ShowComposeMessage(
        Microsoft::Xna::Framework::PlayerIndex /*player*/,
        const std::string& /*text*/,
        const std::vector<Gamer*>& /*recipients*/
    ) {
    }

    void Guide::ShowFriendRequest(Microsoft::Xna::Framework::PlayerIndex /*player*/, Gamer* /*gamer*/)
    {
    }

    void Guide::ShowFriends(Microsoft::Xna::Framework::PlayerIndex /*player*/)
    {
    }

    void Guide::ShowGameInvite(
        Microsoft::Xna::Framework::PlayerIndex /*player*/,
        const std::vector<Gamer*>& /*recipients*/
    ) {
    }

    void Guide::ShowGameInvite(const std::string& /*sessionId*/)
    {
    }

    void Guide::ShowGamerCard(Microsoft::Xna::Framework::PlayerIndex /*player*/, Gamer* /*gamer*/)
    {
    }

    void Guide::ShowMarketplace(Microsoft::Xna::Framework::PlayerIndex /*player*/)
    {
    }

    void Guide::ShowMessages(Microsoft::Xna::Framework::PlayerIndex /*player*/)
    {
    }

    void Guide::ShowParty(Microsoft::Xna::Framework::PlayerIndex /*player*/)
    {
    }

    void Guide::ShowPartySessions(Microsoft::Xna::Framework::PlayerIndex /*player*/)
    {
    }

    void Guide::ShowPlayerReview(Microsoft::Xna::Framework::PlayerIndex /*player*/, Gamer* /*gamer*/)
    {
    }

    void Guide::ShowPlayers(Microsoft::Xna::Framework::PlayerIndex /*player*/)
    {
    }

    void Guide::ShowSignIn(int /*paneCount*/, bool /*onlineOnly*/)
    {
    }

    void Guide::ShowAchievementsEXT(Microsoft::Xna::Framework::PlayerIndex /*player*/)
    {
    }
}
