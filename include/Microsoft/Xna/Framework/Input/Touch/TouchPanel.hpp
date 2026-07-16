// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <queue>
#include <vector>

#include "CNA/CNAHelper.hpp"
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
    /**
     * @brief Provides access to touch input state and queued gesture samples.
     */
    class TouchPanel final
    {
    public:
        using intcs = SharpRuntime::intcs;

        TouchPanel() = delete;

        /** @brief Maximum number of simultaneous touches accepted by the XNA touch panel API. */
        static constexpr intcs MAX_TOUCHES = 8;

        /** @brief Marker used when no finger is present for a touch slot. */
        static constexpr intcs NO_FINGER = -1;

        /**
         * @brief Gets the display width used for normalized touch coordinates.
         * @return The display width in pixels.
         */
        [[nodiscard]] static intcs getDisplayWidthProperty();

        /**
         * @brief Sets the display width used for normalized touch coordinates.
         * @param value The display width in pixels.
         */
        static void setDisplayWidthProperty(intcs value);

        /**
         * @brief Gets the display height used for normalized touch coordinates.
         * @return The display height in pixels.
         */
        [[nodiscard]] static intcs getDisplayHeightProperty();

        /**
         * @brief Sets the display height used for normalized touch coordinates.
         * @param value The display height in pixels.
         */
        static void setDisplayHeightProperty(intcs value);

        /**
         * @brief Gets the current display orientation.
         * @return The display orientation.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::DisplayOrientation getDisplayOrientationProperty();

        /**
         * @brief Sets the current display orientation.
         * @param value The display orientation to set.
         */
        static void setDisplayOrientationProperty(Microsoft::Xna::Framework::DisplayOrientation value);

        /**
         * @brief Gets the gesture types currently enabled for detection.
         * @return The enabled gesture types.
         */
        [[nodiscard]] static GestureType getEnabledGesturesProperty();

        /**
         * @brief Sets the gesture types enabled for detection.
         * @param value The gesture types to enable.
         */
        static void setEnabledGesturesProperty(GestureType value);

        /**
         * @brief Gets whether a gesture sample is ready to be read.
         * @return True if a gesture is available; false otherwise.
         */
        [[nodiscard]] static bool getIsGestureAvailableProperty();

        /**
         * @brief Gets the native window handle associated with the touch panel, if any.
         * @return The native window handle.
         */
        [[nodiscard]] static std::uintptr_t getWindowHandleProperty();

        /**
         * @brief Sets the native window handle associated with the touch panel.
         * @param value The native window handle.
         */
        static void setWindowHandleProperty(std::uintptr_t value);

        /**
         * @brief Gets whether a touch device is currently known to exist.
         * @note NOXNA — FNA declares `TouchDeviceExists` `internal`, not part of the
         *       public XNA `TouchPanel` API. Exposed for the platform input bridge and
         *       `FrameworkDispatcher`'s `Update()` gate.
         * @return True if a touch device exists; false otherwise.
         */
        NOXNA [[nodiscard]] static bool getTouchDeviceExistsProperty();

        /**
         * @brief Sets whether a touch device is currently known to exist.
         * @note NOXNA — see getTouchDeviceExistsProperty().
         * @param value True if a touch device exists; false otherwise.
         */
        NOXNA static void setTouchDeviceExistsProperty(bool value);

        /**
         * @brief Returns touch panel capabilities.
         * @return The touch panel capabilities.
         */
        [[nodiscard]] static TouchPanelCapabilities GetCapabilities();

        /**
         * @brief Returns the current touch state snapshot.
         * @return The current touch collection.
         */
        [[nodiscard]] static TouchCollection GetState();

        /**
         * @brief Removes and returns the oldest queued gesture sample.
         * @return The next gesture sample.
         */
        [[nodiscard]] static GestureSample ReadGesture();

        /**
         * @brief Queues a gesture sample for later retrieval via ReadGesture.
         * @note NOXNA — FNA declares `EnqueueGesture` `internal`, not part of the public
         *       XNA `TouchPanel` API. Exposed for `GestureDetector`.
         * @param gesture The gesture sample to enqueue.
         */
        NOXNA static void EnqueueGesture(const GestureSample& gesture);

        /**
         * @brief Handles a normalized platform touch event used by gesture processing.
         * @note NOXNA — FNA declares `INTERNAL_onTouchEvent` `internal`, not part of the
         *       public XNA `TouchPanel` API. Exposed for the platform input bridge.
         * @param fingerId The finger identifier.
         * @param state The touch location state of this event.
         * @param x The normalized x coordinate.
         * @param y The normalized y coordinate.
         * @param dx The x delta since the last event.
         * @param dy The y delta since the last event.
         */
        NOXNA static void INTERNAL_onTouchEvent(
            intcs fingerId,
            TouchLocationState state,
            float x,
            float y,
            float dx,
            float dy
        );

        /**
         * @brief Updates one touch slot with a finger id and pixel position.
         * @note NOXNA — FNA declares `SetFinger` `internal`, not part of the public XNA
         *       `TouchPanel` API.
         * @param index The slot index to update.
         * @param fingerId The finger identifier.
         * @param fingerPos The current finger position in pixels.
         */
        NOXNA static void SetFinger(intcs index, intcs fingerId, const Microsoft::Xna::Framework::Vector2& fingerPos);

        /**
         * @brief Advances touch panel state by one frame.
         *
         * Copies the SetFinger()-driven touch array to its previous-frame snapshot, advances the
         * event-driven InputManager touch map by one frame (see InputManager::AdvanceTouchFrame —
         * promotes Pressed to Moved, retires Released touches), and updates gesture detection.
         * Must be called at most once per frame; GetState() itself no longer mutates state.
         *
         * @note NOXNA — FNA declares `Update` `internal`, not part of the public XNA
         *       `TouchPanel` API. Exposed for `FrameworkDispatcher::Update()`.
         */
        NOXNA static void Update();

        /**
         * @brief Test-only: resets all process-wide touch/gesture state — the touch arrays, the
         *        gesture queue, the touch-device-exists flag, and enabled gestures — to defaults.
         * @note NOXNA — a CNA test-support helper, not part of the XNA 4.0 API. Display size /
         *       orientation are left untouched (tests set those explicitly).
         */
        NOXNA static void ResetForTests();

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

        static void updateInputManagerTouch(intcs fingerId, TouchLocationState state,
                                            const Microsoft::Xna::Framework::Vector2& position);
    };
}
