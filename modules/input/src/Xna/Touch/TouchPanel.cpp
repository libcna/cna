// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "CNA/Internal/Input/GestureDetector.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "System/InvalidOperationException.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    using Microsoft::Xna::Framework::DisplayOrientation;
    using Microsoft::Xna::Framework::Vector2;

    namespace
    {
        struct EventTouchLocationState
        {
            TouchPanel::intcs id = 0;
            TouchLocationState state = TouchLocationState::Invalid;
            Vector2 position{};
            bool removeAfterFrame = false;
            TouchLocationState previousState = TouchLocationState::Invalid;
            Vector2 previousPosition{};
            float pressure = 0.0f;
        };

        std::map<TouchPanel::intcs, EventTouchLocationState>& eventTouches()
        {
            static std::map<TouchPanel::intcs, EventTouchLocationState> touches;
            return touches;
        }

        void advanceEventTouches()
        {
            auto& touches = eventTouches();
            for (auto it = touches.begin(); it != touches.end();)
            {
                auto& touch = it->second;
                touch.previousState = touch.state;
                touch.previousPosition = touch.position;

                if (touch.removeAfterFrame)
                {
                    it = touches.erase(it);
                    continue;
                }
                if (touch.state == TouchLocationState::Pressed)
                {
                    touch.state = TouchLocationState::Moved;
                }
                ++it;
            }
        }
    }

    TouchPanel::intcs TouchPanel::displayWidth_ = 0;
    TouchPanel::intcs TouchPanel::displayHeight_ = 0;
    DisplayOrientation TouchPanel::displayOrientation_ = DisplayOrientation::Default;
    GestureType TouchPanel::enabledGestures_ = GestureType::None;
    std::uintptr_t TouchPanel::windowHandle_ = 0;
    bool TouchPanel::touchDeviceExists_ = false;

    std::queue<GestureSample> TouchPanel::gestures_;
    std::array<TouchLocation, TouchPanel::MAX_TOUCHES> TouchPanel::touches_{};
    std::array<TouchLocation, TouchPanel::MAX_TOUCHES> TouchPanel::previousTouches_{};
    std::vector<TouchLocation> TouchPanel::validTouches_;

    TouchPanel::intcs TouchPanel::getDisplayWidthProperty()
    {
        return displayWidth_;
    }

    void TouchPanel::setDisplayWidthProperty(intcs value)
    {
        displayWidth_ = value;
    }

    TouchPanel::intcs TouchPanel::getDisplayHeightProperty()
    {
        return displayHeight_;
    }

    void TouchPanel::setDisplayHeightProperty(intcs value)
    {
        displayHeight_ = value;
    }

    DisplayOrientation TouchPanel::getDisplayOrientationProperty()
    {
        return displayOrientation_;
    }

    void TouchPanel::setDisplayOrientationProperty(DisplayOrientation value)
    {
        displayOrientation_ = value;
    }

    GestureType TouchPanel::getEnabledGesturesProperty()
    {
        return enabledGestures_;
    }

    void TouchPanel::setEnabledGesturesProperty(GestureType value)
    {
        enabledGestures_ = value;
    }

    bool TouchPanel::getIsGestureAvailableProperty()
    {
        return !gestures_.empty();
    }

    std::uintptr_t TouchPanel::getWindowHandleProperty()
    {
        return windowHandle_;
    }

    void TouchPanel::setWindowHandleProperty(std::uintptr_t value)
    {
        windowHandle_ = value;
    }

    bool TouchPanel::getTouchDeviceExistsProperty()
    {
        return touchDeviceExists_;
    }

    void TouchPanel::setTouchDeviceExistsProperty(bool value)
    {
        touchDeviceExists_ = value;
    }

    TouchPanelCapabilities TouchPanel::GetCapabilities()
    {
        // DEC-09: XNA/FNA always report MaximumTouchCount = 4 for any touch device. It is a fixed
        // XNA-compat value ("completely bogus" per FNA SDL3_FNAPlatform.cs) and does NOT limit the
        // number of tracked touches — that cap is MAX_TOUCHES (= 8, see GetState). Report 4 to match
        // FNA; 0 when no touch device is present.
        constexpr int kXnaReportedMaxTouchCount = 4;

        // INP-AUD-003: query the platform's live device enumeration on every call, matching FNA's
        // GetTouchCapabilities(), which enumerates on every query. Neither this call nor the two
        // fallbacks below mutate touch state, so a capability query never consumes a frame of input.
        // The sticky touchDeviceExists_ flag and the live event-state peek remain as fallbacks for
        // platforms (FNA notes Windows) that only enumerate a touch device after first interaction.
        CNA::Platform::IPlatformInputDevices* devices =
            CNA::Platform::GetCurrentPlatform().GetInputDevices();
        const bool isConnected =
            (devices != nullptr && devices->HasDevice(CNA::Platform::InputDeviceKind::Touch)) ||
            touchDeviceExists_ ||
            !eventTouches().empty();
        return TouchPanelCapabilities(isConnected, isConnected ? kXnaReportedMaxTouchCount : 0);
    }

    TouchCollection TouchPanel::GetState()
    {
        validTouches_.clear();

        for (const TouchLocation& touch : touches_)
        {
            if (touch.getStateProperty() != TouchLocationState::Invalid)
            {
                validTouches_.push_back(touch);
            }
        }

        if (!validTouches_.empty())
        {
            return TouchCollection(validTouches_);
        }

        // Intentional deviation from FNA: FNA populates touches_ exclusively via SetFinger,
        // driven by a per-frame platform poll (FNAPlatform.UpdateTouchPanelState() ->
        // SDL_GetTouchFingers()) that Update() runs every tick. CNA's PlatformInputBridge is
        // event-driven (dispatches discrete PlatformEvent values) rather than poll-driven, so
        // SetFinger/touches_ are not fed by the real input path and stay empty in production.
        // Fall back to the panel-owned event snapshot so GetState() still reports real touches.
        // The ordered map keeps the public MAX_TOUCHES truncation deterministic by touch id.
        validTouches_.reserve(static_cast<std::size_t>(MAX_TOUCHES));
        for (const auto& [_, touch] : eventTouches())
        {
            if (validTouches_.size() == static_cast<std::size_t>(MAX_TOUCHES))
            {
                break;
            }

            if (touch.previousState != TouchLocationState::Invalid)
            {
                validTouches_.emplace_back(touch.id, touch.state, touch.position,
                                           touch.previousState, touch.previousPosition,
                                           touch.pressure);
            }
            else
            {
                validTouches_.emplace_back(touch.id, touch.state, touch.position, touch.pressure);
            }
        }
        return TouchCollection(validTouches_);
    }

    GestureSample TouchPanel::ReadGesture()
    {
        if (gestures_.empty())
        {
            throw System::InvalidOperationException();
        }

        GestureSample result = gestures_.front();
        gestures_.pop();
        return result;
    }

    void TouchPanel::EnqueueGesture(const GestureSample& gesture)
    {
        gestures_.push(gesture);
    }

    void TouchPanel::INTERNAL_onTouchEvent(
        intcs fingerId,
        TouchLocationState state,
        float x,
        float y,
        float dx,
        float dy
    )
    {
        // Guard against processing touches before the display size is published (task 828). At
        // startup, before GraphicsDevice sets DisplayWidth/Height (task 711), scaling by a zero
        // display size would collapse every touch to (0,0) and emit bogus corner gestures. Drop
        // the event until a real display size is known. In the normal flow DisplayWidth/Height are
        // set at GraphicsDevice creation, before any SDL input is pumped, so this never fires.
        if (displayWidth_ <= 0 || displayHeight_ <= 0)
        {
            return;
        }

        const Vector2 touchPos(
            std::round(x * static_cast<float>(displayWidth_)),
            std::round(y * static_cast<float>(displayHeight_))
        );

        const Vector2 delta(
            std::round(dx * static_cast<float>(displayWidth_)),
            std::round(dy * static_cast<float>(displayHeight_))
        );

        switch (state)
        {
        case TouchLocationState::Pressed:
            CNA::Internal::Input::GestureDetector::OnPressed(fingerId, touchPos);
            break;
        case TouchLocationState::Moved:
            CNA::Internal::Input::GestureDetector::OnMoved(fingerId, touchPos, delta);
            break;
        case TouchLocationState::Released:
            CNA::Internal::Input::GestureDetector::OnReleased(fingerId, touchPos);
            break;
        default:
            break;
        }
    }

    void TouchPanel::INTERNAL_setTouchState(
        const intcs touchId,
        const TouchLocationState state,
        const Vector2& position,
        const float pressure)
    {
        auto& touch = eventTouches()[touchId];
        touch.id = touchId;
        touch.state = state;
        touch.position = position;
        touch.pressure = pressure;
        touch.removeAfterFrame = state == TouchLocationState::Released;
    }

    void TouchPanel::SetFinger(intcs index, intcs fingerId, const Vector2& fingerPos)
    {
        if (index < 0 || index >= MAX_TOUCHES)
        {
            throw std::out_of_range("index");
        }

        const auto slot = static_cast<std::size_t>(index);
        const TouchLocation& previous = previousTouches_[slot];

        if (fingerId == NO_FINGER)
        {
            if (previous.getStateProperty() != TouchLocationState::Invalid &&
                previous.getStateProperty() != TouchLocationState::Released)
            {
                touches_[slot] = TouchLocation(
                    previous.getIdProperty(),
                    TouchLocationState::Released,
                    previous.getPositionProperty(),
                    previous.getStateProperty(),
                    previous.getPositionProperty()
                );

                INTERNAL_setTouchState(
                    previous.getIdProperty(),
                    TouchLocationState::Released,
                    previous.getPositionProperty()
                );
            }
            else
            {
                touches_[slot] = TouchLocation(
                    NO_FINGER,
                    TouchLocationState::Invalid,
                    Vector2::Zero
                );
            }

            return;
        }

        if (previous.getStateProperty() == TouchLocationState::Invalid)
        {
            touches_[slot] = TouchLocation(
                fingerId,
                TouchLocationState::Pressed,
                fingerPos
            );

            INTERNAL_setTouchState(fingerId, TouchLocationState::Pressed, fingerPos);
        }
        else
        {
            touches_[slot] = TouchLocation(
                fingerId,
                TouchLocationState::Moved,
                fingerPos,
                previous.getStateProperty(),
                previous.getPositionProperty()
            );

            INTERNAL_setTouchState(fingerId, TouchLocationState::Moved, fingerPos);
        }

        touchDeviceExists_ = true;
    }

    void TouchPanel::Update()
    {
        previousTouches_ = touches_;

        advanceEventTouches();

        CNA::Internal::Input::GestureDetector::OnUpdate();
    }

    void TouchPanel::ResetForTests()
    {
        touches_.fill(TouchLocation());
        previousTouches_.fill(TouchLocation());
        eventTouches().clear();
        validTouches_.clear();
        while (!gestures_.empty())
        {
            gestures_.pop();
        }
        touchDeviceExists_  = false;
        enabledGestures_    = GestureType::None;
        // Also reset the display metrics + window handle. INTERNAL_onTouchEvent scales touch
        // coordinates by displayWidth_/displayHeight_ and early-returns when either is <= 0, so a
        // leaked display size from a prior test silently corrupts another test's touch/gesture
        // coordinates. These were previously worked around by save/restore in the touch tests.
        displayWidth_       = 0;
        displayHeight_      = 0;
        displayOrientation_ = DisplayOrientation::Default;
        windowHandle_       = 0;
    }

}
