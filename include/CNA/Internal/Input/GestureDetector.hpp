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
    };
}
