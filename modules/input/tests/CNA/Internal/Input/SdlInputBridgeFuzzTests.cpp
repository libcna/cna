// SPDX-License-Identifier: MS-PL
//
// INPUT-TEST-009: deterministic fuzz of SdlInputBridge::ProcessEvent. Feeds many pseudo-random but
// well-typed SDL events (keyboard / mouse / text / touch) through the real bridge entry point and
// asserts it never crashes and the public input-state snapshots stay readable. The stream is
// deterministic (fixed seed; no wall clock, no std::random device) so it is reproducible and
// order-independent. Under the ASan+UBSan CI job this doubles as a memory-error / UB net over the
// whole ProcessEvent switch on edge-case field values.
//
// Gamepad events are intentionally excluded: they drive the real SDL gamepad subsystem (device open /
// sensor / rumble), which is exercised separately through the injectable fake backend.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <cstdint>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::SdlInputBridge;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    // Small deterministic LCG — no std::random, no clock seeding, so runs are reproducible.
    struct Rng
    {
        std::uint64_t state;
        explicit Rng(std::uint64_t seed) : state(seed) {}
        std::uint32_t next()
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<std::uint32_t>(state >> 33);
        }
        std::uint32_t below(std::uint32_t n) { return next() % n; }
        float unit() { return static_cast<float>(next() % 10001u) / 10000.0f; } // [0,1]
        float span(float lo, float hi) { return lo + unit() * (hi - lo); }
    };

    struct SdlInputBridgeFuzzTest : ::testing::Test
    {
        void SetUp() override
        {
            InputManager::ResetAllForTests();
            Touch::TouchPanel::setDisplayWidthProperty(1000);
            Touch::TouchPanel::setDisplayHeightProperty(1000);
            Touch::TouchPanel::setEnabledGesturesProperty(
                Touch::GestureType::Tap | Touch::GestureType::FreeDrag | Touch::GestureType::Flick);
        }
        void TearDown() override { InputManager::ResetAllForTests(); }
    };

    SDL_Event randomEvent(Rng& rng)
    {
        SDL_Event e{};
        switch (rng.below(9))
        {
        case 0:
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = static_cast<SDL_Keycode>(rng.next());
            e.key.scancode = static_cast<SDL_Scancode>(rng.below(512));
            e.key.repeat = rng.below(2) != 0;
            break;
        case 1:
            e.type = SDL_EVENT_KEY_UP;
            e.key.key = static_cast<SDL_Keycode>(rng.next());
            e.key.scancode = static_cast<SDL_Scancode>(rng.below(512));
            break;
        case 2:
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = rng.span(-100.0f, 2000.0f);
            e.motion.y = rng.span(-100.0f, 2000.0f);
            e.motion.xrel = rng.span(-40.0f, 40.0f);
            e.motion.yrel = rng.span(-40.0f, 40.0f);
            break;
        case 3:
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = static_cast<Uint8>(1 + rng.below(7)); // 1..7 (incl. unmapped)
            break;
        case 4:
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = static_cast<Uint8>(1 + rng.below(7));
            break;
        case 5:
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.x = rng.span(-4.0f, 4.0f);
            e.wheel.y = rng.span(-4.0f, 4.0f);
            break;
        case 6:
        {
            // A mix of valid, multi-byte, astral, and malformed UTF-8, plus empty.
            static const char* const texts[] = {"a", "\xC3\xA9", "\xF0\x9F\x98\x80", "\xFF", "x\xC3", ""};
            e.type = SDL_EVENT_TEXT_INPUT;
            e.text.text = texts[rng.below(6)];
            break;
        }
        case 7:
        {
            static const Uint32 fingerTypes[] = {SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_MOTION,
                                                 SDL_EVENT_FINGER_UP, SDL_EVENT_FINGER_CANCELED};
            e.type = fingerTypes[rng.below(4)];
            e.tfinger.fingerID = rng.below(12); // reuse a small id pool to exercise slot maps
            e.tfinger.x = rng.unit();
            e.tfinger.y = rng.unit();
            e.tfinger.dx = rng.span(-1.0f, 1.0f);
            e.tfinger.dy = rng.span(-1.0f, 1.0f);
            break;
        }
        default:
            e.type = SDL_EVENT_TEXT_EDITING;
            e.edit.text = "draft";
            e.edit.start = static_cast<int>(rng.below(6));
            e.edit.length = static_cast<int>(rng.below(6));
            break;
        }
        return e;
    }
}

TEST_F(SdlInputBridgeFuzzTest, RandomEventStreamNeverCrashesAndStateStaysReadable)
{
    Rng rng(0x00C0FFEEULL);

    for (int i = 0; i < 5000; ++i)
    {
        const SDL_Event e = randomEvent(rng);
        ASSERT_NO_THROW(SdlInputBridge::ProcessEvent(e)) << "event #" << i << " type=" << e.type;

        // Every public snapshot must remain readable regardless of the event stream.
        ASSERT_NO_THROW((void)Keyboard::GetState());
        ASSERT_NO_THROW((void)InputManager::GetMouseState());
        ASSERT_NO_THROW((void)Touch::TouchPanel::GetState());
        ASSERT_NO_THROW((void)GamePad::GetState(PlayerIndex::One));

        if ((i & 0x1F) == 0)
        {
            ASSERT_NO_THROW(Touch::TouchPanel::Update());
        }
        while (Touch::TouchPanel::getIsGestureAvailableProperty())
        {
            ASSERT_NO_THROW((void)Touch::TouchPanel::ReadGesture());
        }
    }
}
