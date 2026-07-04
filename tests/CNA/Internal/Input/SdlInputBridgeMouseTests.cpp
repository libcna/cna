// SPDX-License-Identifier: MS-PL
//
// Task 804: verify Mouse::ClickedEXT button numbering end-to-end from the real SDL event entry
// point. SdlInputBridge::ProcessEvent calls Mouse::INTERNAL_onClicked(event.button.button - 1)
// on SDL_EVENT_MOUSE_BUTTON_DOWN (matching FNA's SDL3_FNAPlatform.cs:958-960). SDL button
// constants are 1-based (SDL_BUTTON_LEFT=1 .. SDL_BUTTON_X2=5), so the resulting XNA-facing
// index must be 0-based (Left=0, Middle=1, Right=2, XButton1=3, XButton2=4).

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"

using CNA::Internal::Input::SdlInputBridge;
using Microsoft::Xna::Framework::Input::Mouse;

namespace
{
    SDL_Event mouseButtonEvent(const Uint32 type, const Uint8 button)
    {
        SDL_Event e{};
        e.type = type;
        e.button.button = button;
        e.button.windowID = 0; // no window -> to_logical_position passes raw coords through
        e.button.x = 0.0f;
        e.button.y = 0.0f;
        return e;
    }
}

TEST(SdlInputBridgeMouseTest, ButtonDownFiresClickedEXTWithZeroBasedIndex)
{
    struct Case { Uint8 sdlButton; int expectedIndex; };
    const Case cases[] = {
        { SDL_BUTTON_LEFT,   0 },
        { SDL_BUTTON_MIDDLE, 1 },
        { SDL_BUTTON_RIGHT,  2 },
        { SDL_BUTTON_X1,     3 },
        { SDL_BUTTON_X2,     4 },
    };

    for (const auto& c : cases)
    {
        int fired = -999;
        Mouse::ClickedEXT = [&fired](const int button) { fired = button; };

        SDL_Event e = mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, c.sdlButton);
        SdlInputBridge::ProcessEvent(e);

        EXPECT_EQ(fired, c.expectedIndex)
            << "SDL button " << static_cast<int>(c.sdlButton)
            << " should map to XNA ClickedEXT index " << c.expectedIndex;
    }

    Mouse::ClickedEXT = nullptr;
}

TEST(SdlInputBridgeMouseTest, ButtonUpDoesNotFireClickedEXT)
{
    // FNA fires INTERNAL_onClicked only on button-down, never on button-up.
    bool fired = false;
    Mouse::ClickedEXT = [&fired](const int) { fired = true; };

    SDL_Event e = mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
    SdlInputBridge::ProcessEvent(e);

    EXPECT_FALSE(fired);

    Mouse::ClickedEXT = nullptr;
}
