// SPDX-License-Identifier: MS-PL
//
// INPUT-TEST-009: deterministic fuzz of PlatformInputBridge::ProcessEvent. Feeds many pseudo-random
// but well-typed platform events (keyboard / mouse / text / touch) through the real bridge and
// asserts it never crashes and the public input-state snapshots stay readable. The stream is
// deterministic (fixed seed; no wall clock, no std::random device) so it is reproducible and
// order-independent. Under the ASan+UBSan CI job this doubles as a memory-error / UB net over the
// whole PlatformEvent visitor on edge-case field values.
//
// Gamepad events are intentionally excluded: snapshots and device operations are exercised through
// the canned IPlatformGamepad service instead.

#include <gtest/gtest.h>

#include <cstdint>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::PlatformInputBridge;
using CNA::Internal::Input::SdlInputBridge;
using namespace CNA::Platform;
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

    struct PlatformInputBridgeFuzzTest : ::testing::Test
    {
        void SetUp() override
        {
            InputManager::ResetAllForTests();
            SdlInputBridge::SetScancodeModeForTests(false);
            Touch::TouchPanel::setDisplayWidthProperty(1000);
            Touch::TouchPanel::setDisplayHeightProperty(1000);
            Touch::TouchPanel::setEnabledGesturesProperty(
                Touch::GestureType::Tap | Touch::GestureType::FreeDrag | Touch::GestureType::Flick);
        }
        void TearDown() override { InputManager::ResetAllForTests(); }
    };

    PlatformEvent randomEvent(Rng& rng)
    {
        switch (rng.below(9))
        {
        case 0:
            return KeyEvent{0, static_cast<Scancode>(rng.below(512)),
                            static_cast<KeyCode>(rng.below(256)), 0, true,
                            rng.below(2) != 0};
        case 1:
            return KeyEvent{0, static_cast<Scancode>(rng.below(512)),
                            static_cast<KeyCode>(rng.below(256)), 0, false, false};
        case 2:
            return MouseMotionEvent{0, rng.span(-100.0f, 2000.0f),
                                    rng.span(-100.0f, 2000.0f),
                                    rng.span(-40.0f, 40.0f), rng.span(-40.0f, 40.0f)};
        case 3:
            return MouseButtonEvent{0, static_cast<std::uint8_t>(1 + rng.below(7)),
                                    true, 1, rng.span(-100.0f, 2000.0f),
                                    rng.span(-100.0f, 2000.0f)};
        case 4:
            return MouseButtonEvent{0, static_cast<std::uint8_t>(1 + rng.below(7)),
                                    false, 1, rng.span(-100.0f, 2000.0f),
                                    rng.span(-100.0f, 2000.0f)};
        case 5:
            return MouseWheelEvent{0, rng.span(-4.0f, 4.0f), rng.span(-4.0f, 4.0f)};
        case 6:
        {
            // A mix of valid, multi-byte, astral, and malformed UTF-8, plus empty.
            static const char* const texts[] = {"a", "\xC3\xA9", "\xF0\x9F\x98\x80", "\xFF", "x\xC3", ""};
            return TextInputEvent{0, texts[rng.below(6)]};
        }
        case 7:
        {
            static constexpr TouchEventKind kinds[] = {
                TouchEventKind::Down, TouchEventKind::Motion,
                TouchEventKind::Up, TouchEventKind::Cancelled};
            return TouchEvent{0, rng.below(12), kinds[rng.below(4)], rng.unit(), rng.unit(),
                              rng.span(-1.0f, 1.0f), rng.span(-1.0f, 1.0f), rng.unit()};
        }
        default:
            return TextEditingEvent{0, "draft", static_cast<int>(rng.below(6)),
                                    static_cast<int>(rng.below(6))};
        }
    }
}

TEST_F(PlatformInputBridgeFuzzTest, RandomEventStreamNeverCrashesAndStateStaysReadable)
{
    Rng rng(0x00C0FFEEULL);

    for (int i = 0; i < 5000; ++i)
    {
        const PlatformEvent event = randomEvent(rng);
        ASSERT_NO_THROW(PlatformInputBridge::ProcessEvent(event)) << "event #" << i;

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
