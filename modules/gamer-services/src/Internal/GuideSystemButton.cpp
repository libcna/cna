// SPDX-License-Identifier: MS-PL
// The Guide button (Home on a keyboard, the Guide button on a pad): opens the system Guide's
// home. Polled by GamerServicesDispatcher::Update, so only an initialized game links it.
#include "GuideOverlay.hpp"
#include "Guide/GuideScreen.hpp"
#include "CNA/Internal/Input/SystemInput.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include <array>
#include <cstdlib>
#include <string>

namespace CNA::Internal::GamerServices {
void openSystemGuide(Microsoft::Xna::Framework::PlayerIndex player) {
    using namespace Microsoft::Xna::Framework::GamerServices;
    const int index=static_cast<int>(player);
    if(index<0||index>3||!GamerServicesDispatcher::getIsInitializedProperty()||guideIsVisible())return;
    installSystemGuide();
    GuideUi::open(GuideUi::homeScreen(player),player);
}

void systemGuideButton(Microsoft::Xna::Framework::PlayerIndex player) {
    // As on the console, the button closes the Guide it opened; a game's own message box or
    // keyboard, and a sign-in in progress, stay until they are answered.
    if(GuideUi::visible()&&!GuideUi::pendingMessageBox()&&!GuideUi::pendingKeyboard()&&!guideSignInActive()) {
        GuideUi::closeAll();
        return;
    }
    openSystemGuide(player);
}

void pollSystemGuideButton() {
    using namespace Microsoft::Xna::Framework::Input;
    using Microsoft::Xna::Framework::PlayerIndex;
    static const bool disabled=[]{const auto* value=std::getenv("CNA_GAMER_SERVICES_GUIDE_BUTTON");return value&&std::string(value)=="0";}();
    static std::array<bool,5> previous{};
    if(disabled)return;
    // Home opens the Guide from a keyboard, as in Games for Windows LIVE; the Guide button from a pad.
    const bool home=CNA::Internal::Input::systemKeyboardState().IsKeyDown(Keys::Home);
    if(home&&!previous[4])systemGuideButton(PlayerIndex::One);
    previous[4]=home;
    for(int index=0;index<4;++index) {
        const bool pressed=CNA::Internal::Input::systemGamePadState(static_cast<PlayerIndex>(index)).IsButtonDown(Buttons::BigButton);
        if(pressed&&!previous[index])systemGuideButton(static_cast<PlayerIndex>(index));
        previous[index]=pressed;
    }
}
}
