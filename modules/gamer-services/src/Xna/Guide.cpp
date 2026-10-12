// SPDX-License-Identifier: MS-PL
// Guide's own state: trial mode, notification position and the screen saver.
//
// Link boundary: the rest of the Guide is split by what it needs -- GuideDialogs.cpp (message box
// and keyboard, drawn by the Guide's frame), GuideMarketplace.cpp, GuideSignInFlow.cpp and
// GuideSocialCalls.cpp (the service-backed panels), and the system Guide that
// GamerServicesDispatcher::Initialize installs. Reading IsTrialMode must not link any of them,
// let alone the avatar system the full Guide draws (docs/link-size-architecture.md).
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    bool Guide::simulateTrialMode_ = false;
    bool Guide::isTrialMode_ = true;
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

    // XNA IL: Guide.isTrialMode starts true and GamerServicesDispatcher.Update latches it from the
    // kernel's GuideState (fed SimulateTrialMode); the setter is internal.
    bool Guide::getIsTrialModeProperty()          { return isTrialMode_; }

    NotificationPosition Guide::getNotificationPositionProperty() { return position_; }

    void Guide::setNotificationPositionProperty(NotificationPosition value)
    {
        if (value != position_)
            position_ = value;
    }

    bool Guide::getSimulateTrialModeProperty()          { return simulateTrialMode_; }
    void Guide::setSimulateTrialModeProperty(bool value) { simulateTrialMode_ = value; }
}
