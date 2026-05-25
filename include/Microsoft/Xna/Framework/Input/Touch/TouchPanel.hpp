#pragma once

#include <array>
#include <cstdint>
#include <queue>
#include <vector>

#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    /// Provides access to touch input state and queued gesture samples.
    class TouchPanel final
    {
    public:
        using intcs = SharpRuntime::intcs;

        TouchPanel() = delete;

        /// Maximum number of simultaneous touches accepted by the XNA touch panel API.
        static constexpr intcs MAX_TOUCHES = 8;

        /// Marker used when no finger is present for a touch slot.
        static constexpr intcs NO_FINGER = -1;

        /// Gets the display width used for normalized touch coordinates.
        [[nodiscard]] static intcs getDisplayWidthProperty();

        /// Sets the display width used for normalized touch coordinates.
        static void setDisplayWidthProperty(intcs value);

        /// Gets the display height used for normalized touch coordinates.
        [[nodiscard]] static intcs getDisplayHeightProperty();

        /// Sets the display height used for normalized touch coordinates.
        static void setDisplayHeightProperty(intcs value);

        /// Gets the current display orientation.
        [[nodiscard]] static Microsoft::Xna::Framework::DisplayOrientation getDisplayOrientationProperty();

        /// Sets the current display orientation.
        static void setDisplayOrientationProperty(Microsoft::Xna::Framework::DisplayOrientation value);

        /// Gets the gesture types currently enabled for detection.
        [[nodiscard]] static GestureType getEnabledGesturesProperty();

        /// Sets the gesture types enabled for detection.
        static void setEnabledGesturesProperty(GestureType value);

        /// Gets whether a gesture sample is ready to be read.
        [[nodiscard]] static bool getIsGestureAvailableProperty();

        /// Gets the native window handle associated with the touch panel, if any.
        [[nodiscard]] static std::uintptr_t getWindowHandleProperty();

        /// Sets the native window handle associated with the touch panel.
        static void setWindowHandleProperty(std::uintptr_t value);

        /// Gets whether a touch device is currently known to exist.
        [[nodiscard]] static bool getTouchDeviceExistsProperty();

        /// Sets whether a touch device is currently known to exist.
        static void setTouchDeviceExistsProperty(bool value);

        /// Returns touch panel capabilities.
        [[nodiscard]] static TouchPanelCapabilities GetCapabilities();

        /// Returns the current touch state snapshot.
        [[nodiscard]] static TouchCollection GetState();

        /// Removes and returns the oldest queued gesture sample.
        [[nodiscard]] static GestureSample ReadGesture();

        /// Queues a gesture sample for ReadGesture.
        static void EnqueueGesture(const GestureSample& gesture);

        /// Handles a normalized platform touch event used by gesture processing.
        static void INTERNAL_onTouchEvent(
            intcs fingerId,
            TouchLocationState state,
            float x,
            float y,
            float dx,
            float dy
        );

        /// Updates one touch slot with a finger id and pixel position.
        static void SetFinger(intcs index, intcs fingerId, const Microsoft::Xna::Framework::Vector2& fingerPos);

        /// Advances touch panel state by one frame.
        static void Update();

    private:
        static intcs displayWidth_;
        static intcs displayHeight_;
        static Microsoft::Xna::Framework::DisplayOrientation displayOrientation_;
        static GestureType enabledGestures_;
        static std::uintptr_t windowHandle_;
        static bool touchDeviceExists_;

        static std::queue<GestureSample> gestures_;
        static std::array<TouchLocation, MAX_TOUCHES> touches_;
        static std::array<TouchLocation, MAX_TOUCHES> previousTouches_;
        static std::vector<TouchLocation> validTouches_;

        static void updateInputManagerTouch(intcs fingerId, TouchLocationState state, const Microsoft::Xna::Framework::Vector2& position);
    };
}
