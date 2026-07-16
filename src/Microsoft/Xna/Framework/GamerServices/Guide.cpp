// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "System/NotSupportedException.hpp"
#include "System/Threading/EventWaitHandle.hpp"
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
        const std::string& /*title*/,
        const std::string& /*text*/,
        const std::vector<std::string>& /*buttons*/,
        int /*focusButton*/,
        MessageBoxIcon /*icon*/,
        System::AsyncCallback /*callback*/,
        std::any /*state*/
    ) {
        throw System::NotSupportedException();
    }

    System::IAsyncResult* Guide::BeginShowMessageBox(
        Microsoft::Xna::Framework::PlayerIndex /*player*/,
        const std::string& /*title*/,
        const std::string& /*text*/,
        const std::vector<std::string>& /*buttons*/,
        int /*focusButton*/,
        MessageBoxIcon /*icon*/,
        System::AsyncCallback /*callback*/,
        std::any /*state*/
    ) {
        throw System::NotSupportedException();
    }

    std::optional<int> Guide::EndShowMessageBox(System::IAsyncResult* /*result*/)
    {
        throw System::NotSupportedException();
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
