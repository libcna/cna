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

        /** @brief TouchPanel is a static class (XNA `public static class TouchPanel`) and cannot be instantiated. */
        TouchPanel() = delete;

        /**
         * @brief Maximum number of simultaneous touches accepted by the XNA touch panel API.
         * @note CNAEXT — FNA declares this `internal const int MAX_TOUCHES` (TouchPanel.cs:23), not
         *       part of the public XNA `TouchPanel` API. Exposed as a public CNAEXT constant (mirroring
         *       `GamePad::LeftDeadZone`/`RightDeadZone`/`TriggerThreshold`) since C++ has no
         *       assembly-internal visibility and other translation units (`GestureDetector`, tests)
         *       need it.
         */
        CNAEXT static constexpr intcs MAX_TOUCHES = 8;

        /**
         * @brief Marker used when no finger is present for a touch slot.
         * @note CNAEXT — FNA declares this `internal const int NO_FINGER` (TouchPanel.cs:26); see
         *       MAX_TOUCHES's note for why it is exposed as a public CNAEXT constant in CNA.
         */
        CNAEXT static constexpr intcs NO_FINGER = -1;

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
         * @note CNAEXT — FNA declares `TouchDeviceExists` `internal`, not part of the
         *       public XNA `TouchPanel` API. Exposed for the platform input bridge and
         *       `FrameworkDispatcher`'s `Update()` gate.
         * @return True if a touch device exists; false otherwise.
         */
        CNAEXT [[nodiscard]] static bool getTouchDeviceExistsProperty();

        /**
         * @brief Sets whether a touch device is currently known to exist.
         * @note CNAEXT — see getTouchDeviceExistsProperty().
         * @param value True if a touch device exists; false otherwise.
         */
        CNAEXT static void setTouchDeviceExistsProperty(bool value);

        /**
         * @brief Gets whether left-mouse-button input is reported as touch input.
         *
         * @note CNAEXT — not part of the XNA `TouchPanel` API, and off by default so that
         *       the default behavior stays XNA's and FNA's: both feed `TouchPanel` from
         *       real finger events only and neither synthesizes touches from a mouse. A
         *       host that has no touch digitizer can switch this on to make a touch-only
         *       game playable with a pointer; every other behavior is unchanged.
         * @return True when mouse input is also reported as touch input.
         */
        CNAEXT [[nodiscard]] static bool getMouseTouchEmulationEnabledEXT();

        /**
         * @brief Sets whether left-mouse-button input is reported as touch input.
         *
         * While enabled, pressing the left mouse button begins a touch at the cursor,
         * moving with it held reports a moved touch, and releasing it ends the touch.
         * The synthesized touch travels the same path as a real one, so `GetState()`,
         * the gesture recognizer and `TouchPanelCapabilities` all see it.
         *
         * @note CNAEXT — see getMouseTouchEmulationEnabledEXT().
         * @param value True to report mouse input as touch input.
         */
        CNAEXT static void setMouseTouchEmulationEnabledEXT(bool value);

        /**
         * @brief Gets whether touch input is currently withheld from the game.
         *
         * @note CNAEXT — not part of the XNA `TouchPanel` API. On every real XNA platform a
         *       visible Guide overlay is drawn and driven by the system shell, which owns the
         *       screen while it is up, so the game's own touch panel reports nothing behind it.
         *       CNA renders its Guide overlay inside the game's own Draw(), so that ownership has
         *       to be expressed explicitly; `Guide` raises and lowers this flag around a pending
         *       message box or keyboard-input prompt.
         * @return True while touch input is withheld.
         */
        CNAEXT [[nodiscard]] static bool getInputSuppressedEXT();

        /**
         * @brief Withholds touch input from the game, or resumes delivering it.
         *
         * While suppressed, `GetState()` reports no touches and no gesture is queued or
         * available; any gesture already queued when suppression begins is discarded, so a tap
         * made while an overlay was up cannot fire the moment it closes.
         *
         * @note CNAEXT — see getInputSuppressedEXT().
         * @param value True to withhold touch input, false to resume delivering it.
         */
        CNAEXT static void setInputSuppressedEXT(bool value);

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
         * @note CNAEXT — FNA declares `EnqueueGesture` `internal`, not part of the public
         *       XNA `TouchPanel` API. Exposed for `GestureDetector`.
         * @param gesture The gesture sample to enqueue.
         */
        CNAEXT static void EnqueueGesture(const GestureSample& gesture);

        /**
         * @brief Handles a normalized platform touch event used by gesture processing.
         * @note CNAEXT — FNA declares `INTERNAL_onTouchEvent` `internal`, not part of the
         *       public XNA `TouchPanel` API. Exposed for the platform input bridge.
         * @param fingerId The finger identifier.
         * @param state The touch location state of this event.
         * @param x The normalized x coordinate.
         * @param y The normalized y coordinate.
         * @param dx The x delta since the last event.
         * @param dy The y delta since the last event.
         */
        CNAEXT static void INTERNAL_onTouchEvent(
            intcs fingerId,
            TouchLocationState state,
            float x,
            float y,
            float dx,
            float dy
        );

        /**
         * @brief Updates one event-driven touch point in the panel-owned state snapshot.
         * @note CNAEXT — internal platform-bridge entry point, not part of the XNA 4.0 API.
         * @param touchId The stable CNA touch identifier.
         * @param state The touch location state of this event.
         * @param position The touch position in logical pixels.
         * @param pressure CNAEXT/EXT pressure in the inclusive 0..1 range.
         */
        CNAEXT static void INTERNAL_setTouchState(
            intcs touchId,
            TouchLocationState state,
            const Microsoft::Xna::Framework::Vector2& position,
            float pressure = 0.0f
        );

        /**
         * @brief Updates one touch slot with a finger id and pixel position.
         * @note CNAEXT — FNA declares `SetFinger` `internal`, not part of the public XNA
         *       `TouchPanel` API.
         * @param index The slot index to update.
         * @param fingerId The finger identifier.
         * @param fingerPos The current finger position in pixels.
         */
        CNAEXT static void SetFinger(intcs index, intcs fingerId, const Microsoft::Xna::Framework::Vector2& fingerPos);

        /**
         * @brief Advances touch panel state by one frame.
         *
         * Copies the SetFinger()-driven touch array to its previous-frame snapshot, advances the
         * panel-owned event state by one frame (promotes Pressed to Moved and retires Released
         * touches), and updates gesture detection.
         * Must be called at most once per frame; GetState() itself no longer mutates state.
         *
         * @note CNAEXT — FNA declares `Update` `internal`, not part of the public XNA
         *       `TouchPanel` API. Exposed for `FrameworkDispatcher::Update()`.
         */
        CNAEXT static void Update();

        /**
         * @brief Test-only: resets all process-wide touch/gesture state to defaults.
         * @note CNAEXT — a CNA test-support helper, not part of the XNA 4.0 API.
         */
        CNAEXT static void ResetForTests();

    private:
        static intcs displayWidth_;
        static intcs displayHeight_;
        static Microsoft::Xna::Framework::DisplayOrientation displayOrientation_;
        static GestureType enabledGestures_;
        static std::uintptr_t windowHandle_;
        static bool touchDeviceExists_;
        static bool mouseTouchEmulationEnabled_;
        static bool inputSuppressed_;

        static std::queue<GestureSample> gestures_;
        static std::array<TouchLocation, MAX_TOUCHES> touches_;
        static std::array<TouchLocation, MAX_TOUCHES> previousTouches_;
        static std::vector<TouchLocation> validTouches_;
    };
}
