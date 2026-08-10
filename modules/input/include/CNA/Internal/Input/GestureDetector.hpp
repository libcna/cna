// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace CNA::Internal::Input
{
    /**
     * @brief Internal gesture recognition state machine.
     *
     * Detects Tap, DoubleTap, Hold, HorizontalDrag, VerticalDrag, FreeDrag,
     * Flick, DragComplete, Pinch, and PinchComplete gestures.
     *
     * Call OnPressed/OnMoved/OnReleased from touch events and OnUpdate each frame.
     * Recognized gestures are enqueued in TouchPanel via TouchPanel::EnqueueGesture.
     */
    class GestureDetector
    {
    public:
        GestureDetector() = delete;

        static void OnPressed(int fingerId, Microsoft::Xna::Framework::Vector2 touchPosition);
        static void OnMoved(int fingerId, Microsoft::Xna::Framework::Vector2 touchPosition,
                            Microsoft::Xna::Framework::Vector2 delta);
        static void OnReleased(int fingerId, Microsoft::Xna::Framework::Vector2 touchPosition);
        static void OnUpdate();

        /**
         * @brief Test-only: resets all gesture-recognition state to its initial (idle) values.
         *
         * The detector's state machine lives in process-wide file-static variables with no
         * runtime reset hook, so tests must reset it to avoid becoming order-dependent.
         */
        static void ResetForTests();

        /**
         * @brief Test-only: switches the detector to a manually-advanced clock so timing gestures
         *        (Hold/Flick/DoubleTap) can be tested deterministically without real sleeps.
         *
         * The clock starts at zero; advance it with AdvanceTestClockMilliseconds().
         */
        static void EnableTestClock();

        /** @brief Test-only: reverts the detector to the real monotonic clock. */
        static void DisableTestClock();

        /**
         * @brief Test-only: advances the manual test clock (see EnableTestClock).
         * @param milliseconds How many milliseconds to advance "now" by.
         */
        static void AdvanceTestClockMilliseconds(long milliseconds);
    };
}
