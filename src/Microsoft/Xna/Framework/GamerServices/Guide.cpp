// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include <algorithm>
#include <SDL3/SDL.h>

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
        return SDL_ScreenSaverEnabled();
    }

    void Guide::setIsScreenSaverEnabledProperty(bool value)
    {
        if (value)
            SDL_EnableScreenSaver();
        else
            SDL_DisableScreenSaver();
    }

    bool Guide::getIsTrialModeProperty()          { return isTrialMode_; }
    void Guide::setIsTrialModeProperty(bool value) { isTrialMode_ = value; }

    bool Guide::getIsVisibleProperty()             { return false; }
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
        const std::string& /*title*/,
        const std::string& /*description*/,
        const std::string& /*defaultText*/,
        System::AsyncCallback callback,
        std::any state,
        bool /*usePasswordMode*/
    ) {
        Microsoft::Xna::Framework::Input::TextInputEXT::StartTextInput();
        auto* action = new GuideAction(std::move(state), std::move(callback));
        action->setIsCompletedProperty(true);
        // audit_net.md High finding: the callback used to only be stored, never invoked, despite
        // this action already completing synchronously right above - matching
        // AvatarDescription::BeginGetFromGamer's existing invoke-after-complete pattern.
        if (action->Callback)
        {
            action->Callback(*action);
        }
        return action;
    }

    std::string Guide::EndShowKeyboardInput(System::IAsyncResult* /*result*/)
    {
        Microsoft::Xna::Framework::Input::TextInputEXT::StopTextInput();
        return "";
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
