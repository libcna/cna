// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Input/GestureDetector.hpp"

#include "Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "System/TimeSpan.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Input::Touch::GestureType;
    using Microsoft::Xna::Framework::Input::Touch::GestureSample;
    using Microsoft::Xna::Framework::Input::Touch::TouchPanel;
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    constexpr int   MOVE_THRESHOLD    = 35;
    constexpr float MIN_FLICK_VELOCITY = 100.0f;

    enum class GestureState
    {
        NONE,
        HOLDING,
        HELD,
        JUST_TAPPED,
        DRAGGING_FREE,
        DRAGGING_H,
        DRAGGING_V,
        PINCHING
    };

    // --- state ---

    int         activeFingerId        = TouchPanel::NO_FINGER;
    Vector2     activeFingerPosition  = Vector2::Zero;
    bool        callBelatedPinchComplete = false;
    TimePoint   eventTimestamp        = {};
    std::vector<int> fingerIds;
    bool        justDoubleTapped      = false;
    Vector2     lastUpdatePosition    = Vector2::Zero;
    Vector2     pressPosition         = Vector2::Zero;
    int         secondFingerId        = TouchPanel::NO_FINGER;
    Vector2     secondFingerPosition  = Vector2::Zero;
    GestureState state                = GestureState::NONE;
    TimePoint   updateTimestamp       = {};
    Vector2     velocity              = Vector2::Zero;

    // --- helpers ---

    System::TimeSpan GetGestureTimestamp()
    {
        auto now = Clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
        return System::TimeSpan::FromTicks(ms * System::TimeSpan::TicksPerMillisecond);
    }

    bool IsGestureEnabled(GestureType g)
    {
        return (TouchPanel::getEnabledGesturesProperty() & g) != GestureType::None;
    }

    // --- pinch helpers ---

    void OnReleased_Pinch(int fingerId, Vector2 touchPosition)
    {
        if (fingerId != activeFingerId && fingerId != secondFingerId)
            return;

        if (IsGestureEnabled(GestureType::PinchComplete))
        {
            TouchPanel::EnqueueGesture(GestureSample(
                GestureType::PinchComplete,
                GetGestureTimestamp(),
                Vector2::Zero, Vector2::Zero,
                Vector2::Zero, Vector2::Zero,
                activeFingerId, secondFingerId
            ));
        }

        if (fingerId == activeFingerId)
        {
            activeFingerId       = secondFingerId;
            activeFingerPosition = secondFingerPosition;
        }

        secondFingerId = TouchPanel::NO_FINGER;

        bool replaced = false;
        for (int id : fingerIds)
        {
            if (id != activeFingerId)
            {
                secondFingerId = id;
                replaced = true;
                break;
            }
        }

        if (!replaced)
            state = GestureState::HELD;
    }

    void OnMoved_Pinch(int fingerId, Vector2 touchPosition, Vector2 delta)
    {
        if (fingerId != activeFingerId && fingerId != secondFingerId)
            return;

        if (fingerId == activeFingerId)
        {
            activeFingerPosition = touchPosition;
            TouchPanel::EnqueueGesture(GestureSample(
                GestureType::Pinch,
                GetGestureTimestamp(),
                activeFingerPosition, secondFingerPosition,
                delta, Vector2::Zero,
                activeFingerId, secondFingerId
            ));
        }
        else
        {
            secondFingerPosition = touchPosition;
            TouchPanel::EnqueueGesture(GestureSample(
                GestureType::Pinch,
                GetGestureTimestamp(),
                activeFingerPosition, secondFingerPosition,
                Vector2::Zero, delta,
                activeFingerId, secondFingerId
            ));
        }
    }
}

namespace CNA::Internal::Input
{
    void GestureDetector::OnPressed(int fingerId, Vector2 touchPosition)
    {
        fingerIds.push_back(fingerId);

        if (state == GestureState::PINCHING)
            return;

        if (activeFingerId == TouchPanel::NO_FINGER)
        {
            activeFingerId       = fingerId;
            activeFingerPosition = touchPosition;
        }
        else
        {
            if (IsGestureEnabled(GestureType::Pinch))
            {
                secondFingerId       = fingerId;
                secondFingerPosition = touchPosition;
                state                = GestureState::PINCHING;
            }
            return;
        }

        if (state == GestureState::JUST_TAPPED)
        {
            if (IsGestureEnabled(GestureType::DoubleTap))
            {
                auto elapsed = Clock::now() - eventTimestamp;
                if (elapsed <= std::chrono::milliseconds(300))
                {
                    float dist = (touchPosition - pressPosition).Length();
                    if (dist <= MOVE_THRESHOLD)
                    {
                        TouchPanel::EnqueueGesture(GestureSample(
                            GestureType::DoubleTap,
                            GetGestureTimestamp(),
                            touchPosition, Vector2::Zero,
                            Vector2::Zero, Vector2::Zero,
                            fingerId, TouchPanel::NO_FINGER
                        ));
                        justDoubleTapped = true;
                    }
                }
            }
        }

        state          = GestureState::HOLDING;
        pressPosition  = touchPosition;
        eventTimestamp = Clock::now();
    }

    void GestureDetector::OnReleased(int fingerId, Vector2 touchPosition)
    {
        fingerIds.erase(std::remove(fingerIds.begin(), fingerIds.end(), fingerId), fingerIds.end());

        if (state == GestureState::PINCHING)
        {
            OnReleased_Pinch(fingerId, touchPosition);
            return;
        }

        if (fingerId == activeFingerId)
            activeFingerId = TouchPanel::NO_FINGER;

        if (!fingerIds.empty())
            return;

        // Tap detection
        if (state == GestureState::HOLDING)
        {
            bool tapEnabled  = IsGestureEnabled(GestureType::Tap);
            bool dtapEnabled = IsGestureEnabled(GestureType::DoubleTap);

            if (tapEnabled || dtapEnabled)
            {
                auto held = Clock::now() - eventTimestamp;
                if (held < std::chrono::seconds(1))
                {
                    if (!justDoubleTapped)
                    {
                        if (tapEnabled)
                        {
                            TouchPanel::EnqueueGesture(GestureSample(
                                GestureType::Tap,
                                GetGestureTimestamp(),
                                touchPosition, Vector2::Zero,
                                Vector2::Zero, Vector2::Zero,
                                fingerId, TouchPanel::NO_FINGER
                            ));
                        }
                        state = GestureState::JUST_TAPPED;
                    }
                }
            }
        }

        justDoubleTapped = false;

        // Flick detection
        if (IsGestureEnabled(GestureType::Flick))
        {
            float distFromPress = (touchPosition - pressPosition).Length();
            if (distFromPress > MOVE_THRESHOLD && velocity.Length() >= MIN_FLICK_VELOCITY)
            {
                TouchPanel::EnqueueGesture(GestureSample(
                    GestureType::Flick,
                    GetGestureTimestamp(),
                    Vector2::Zero, Vector2::Zero,
                    velocity, Vector2::Zero,
                    fingerId, TouchPanel::NO_FINGER
                ));
            }
            velocity             = Vector2::Zero;
            lastUpdatePosition   = Vector2::Zero;
            updateTimestamp      = TimePoint{};
        }

        // DragComplete detection
        if (IsGestureEnabled(GestureType::DragComplete))
        {
            bool wasDragging = (state == GestureState::DRAGGING_H  ||
                                state == GestureState::DRAGGING_V  ||
                                state == GestureState::DRAGGING_FREE);
            if (wasDragging)
            {
                TouchPanel::EnqueueGesture(GestureSample(
                    GestureType::DragComplete,
                    GetGestureTimestamp(),
                    Vector2::Zero, Vector2::Zero,
                    Vector2::Zero, Vector2::Zero,
                    fingerId, TouchPanel::NO_FINGER
                ));
            }
        }

        // Belated PinchComplete
        if (callBelatedPinchComplete && IsGestureEnabled(GestureType::PinchComplete))
        {
            TouchPanel::EnqueueGesture(GestureSample(
                GestureType::PinchComplete,
                GetGestureTimestamp(),
                Vector2::Zero, Vector2::Zero,
                Vector2::Zero, Vector2::Zero,
                TouchPanel::NO_FINGER, TouchPanel::NO_FINGER
            ));
        }
        callBelatedPinchComplete = false;

        if (state != GestureState::JUST_TAPPED)
            state = GestureState::NONE;

        eventTimestamp = Clock::now();
    }

    void GestureDetector::OnMoved(int fingerId, Vector2 touchPosition, Vector2 delta)
    {
        if (state == GestureState::PINCHING)
        {
            OnMoved_Pinch(fingerId, touchPosition, delta);
            return;
        }

        if (activeFingerId == TouchPanel::NO_FINGER)
            activeFingerId = fingerId;

        if (fingerId != activeFingerId)
            return;

        activeFingerPosition = touchPosition;

        bool hdrag = IsGestureEnabled(GestureType::HorizontalDrag);
        bool vdrag = IsGestureEnabled(GestureType::VerticalDrag);
        bool fdrag = IsGestureEnabled(GestureType::FreeDrag);

        if (state == GestureState::HOLDING || state == GestureState::HELD)
        {
            float dist = (touchPosition - pressPosition).Length();
            if (dist > MOVE_THRESHOLD)
            {
                float ax = std::abs(delta.X);
                float ay = std::abs(delta.Y);
                if (hdrag && ax > ay)
                    state = GestureState::DRAGGING_H;
                else if (vdrag && ay > ax)
                    state = GestureState::DRAGGING_V;
                else if (fdrag)
                    state = GestureState::DRAGGING_FREE;
                else
                    state = GestureState::NONE;
            }
        }

        if (state == GestureState::DRAGGING_H && hdrag)
        {
            TouchPanel::EnqueueGesture(GestureSample(
                GestureType::HorizontalDrag,
                GetGestureTimestamp(),
                touchPosition, Vector2::Zero,
                Vector2(delta.X, 0.0f), Vector2::Zero,
                fingerId, TouchPanel::NO_FINGER
            ));
        }
        else if (state == GestureState::DRAGGING_V && vdrag)
        {
            TouchPanel::EnqueueGesture(GestureSample(
                GestureType::VerticalDrag,
                GetGestureTimestamp(),
                touchPosition, Vector2::Zero,
                Vector2(0.0f, delta.Y), Vector2::Zero,
                fingerId, TouchPanel::NO_FINGER
            ));
        }
        else if (state == GestureState::DRAGGING_FREE && fdrag)
        {
            TouchPanel::EnqueueGesture(GestureSample(
                GestureType::FreeDrag,
                GetGestureTimestamp(),
                touchPosition, Vector2::Zero,
                delta, Vector2::Zero,
                fingerId, TouchPanel::NO_FINGER
            ));
        }

        if ((state == GestureState::DRAGGING_H    && !hdrag) ||
            (state == GestureState::DRAGGING_V    && !vdrag) ||
            (state == GestureState::DRAGGING_FREE && !fdrag))
        {
            state = GestureState::HELD;
        }
    }

    void GestureDetector::OnUpdate()
    {
        if (state == GestureState::PINCHING)
        {
            if (!IsGestureEnabled(GestureType::Pinch))
            {
                state          = GestureState::HELD;
                secondFingerId = TouchPanel::NO_FINGER;
                callBelatedPinchComplete = true;
            }
            return;
        }

        if (activeFingerId == TouchPanel::NO_FINGER)
            return;

        // Flick velocity calculation
        if (IsGestureEnabled(GestureType::Flick))
        {
            if (updateTimestamp != TimePoint{})
            {
                float dt = std::chrono::duration<float>(Clock::now() - updateTimestamp).count();
                Vector2 d = activeFingerPosition - lastUpdatePosition;
                Vector2 instVelocity = d * (1.0f / (0.001f + dt));
                velocity = velocity + (instVelocity - velocity) * 0.45f;
            }
            lastUpdatePosition = activeFingerPosition;
            updateTimestamp    = Clock::now();
        }

        // Hold detection
        if (IsGestureEnabled(GestureType::Hold) && state == GestureState::HOLDING)
        {
            auto held = Clock::now() - eventTimestamp;
            if (held >= std::chrono::seconds(1))
            {
                TouchPanel::EnqueueGesture(GestureSample(
                    GestureType::Hold,
                    GetGestureTimestamp(),
                    activeFingerPosition, Vector2::Zero,
                    Vector2::Zero, Vector2::Zero,
                    activeFingerId, TouchPanel::NO_FINGER
                ));
                state = GestureState::HELD;
            }
        }
    }
}
