// SPDX-License-Identifier: MS-PL
#pragma once

// Shared test scaffolding for the whole-mouse service. The pending/current split proves frame
// publication separately from relative displacement, whose consume-on-read behaviour is an
// intentional exception to an otherwise immutable snapshot.

#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <vector>

namespace CNA::Platform::Testing {

    /** @brief A mouse service whose state and relative motion are controlled by a test. */
    class CannedMouse final : public IPlatformMouse
    {
    public:
        /** @brief Sets the whole state that the next Update() publishes. @param state Next state. */
        void SetPending(const MouseSnapshot& state) { pending_ = state; }

        /**
         * @brief Adds relative displacement that the next Update() publishes.
         * @param x Horizontal displacement.
         * @param y Vertical displacement.
         */
        void AddPendingRelativeDelta(const float x, const float y)
        {
            pendingDeltaX_ += x;
            pendingDeltaY_ += y;
        }

        /** @brief Publishes the pending frame and adds its relative displacement. */
        void Update() override
        {
            snapshot_ = pending_;
            relativeDeltaX_ += pendingDeltaX_;
            relativeDeltaY_ += pendingDeltaY_;
            pendingDeltaX_ = 0.0f;
            pendingDeltaY_ = 0.0f;
            ++updateCount_;
        }

        /** @brief Gets the published snapshot. @return The current scripted state. */
        [[nodiscard]] const MouseSnapshot& GetSnapshot() const override { return snapshot_; }

        /** @brief Returns and clears scripted relative displacement. @return Pending displacement. */
        [[nodiscard]] MouseDelta ConsumeRelativeDelta() override
        {
            if (!relativeMode_)
            {
                return {};
            }
            const MouseDelta result{static_cast<int>(relativeDeltaX_),
                                    static_cast<int>(relativeDeltaY_)};
            relativeDeltaX_ = 0.0f;
            relativeDeltaY_ = 0.0f;
            return result;
        }

        /** @brief Records a client-coordinate warp and updates the snapshot immediately. */
        void SetPosition(const WindowId window, const int x, const int y) override
        {
            ++positionCalls_;
            lastPositionWindow_ = window;
            snapshot_.window = window;
            snapshot_.x = x;
            snapshot_.y = y;
        }

        /** @brief Records cursor visibility. @param visible Requested state. */
        void SetCursorVisible(const bool visible) override { cursorVisible_ = visible; }
        /** @brief Records the cursor shape. @param cursor Requested shape. */
        void SetCursor(const SystemCursor cursor) override
        {
            cursor_ = cursor;
            customCursor_ = false;
            ++cursorCalls_;
        }
        /** @brief Copies and records a custom cursor. @param cursor Requested image and hot spot. */
        void SetCursor(const CursorImage& cursor) override
        {
            customCursor_ = true;
            customCursorWidth_ = cursor.width;
            customCursorHeight_ = cursor.height;
            customCursorHotSpotX_ = cursor.hotSpotX;
            customCursorHotSpotY_ = cursor.hotSpotY;
            customCursorPixels_.assign(cursor.rgba.begin(), cursor.rgba.end());
            ++cursorCalls_;
        }

        /** @brief Records relative mode and flushes old displacement. */
        void SetRelativeMode(const WindowId window, const bool enabled) override
        {
            ++relativeModeCalls_;
            lastRelativeWindow_ = window;
            relativeMode_ = enabled;
            relativeDeltaX_ = 0.0f;
            relativeDeltaY_ = 0.0f;
        }

        /** @brief Gets scripted relative mode. @return True when enabled. */
        [[nodiscard]] bool IsRelativeMode() const override { return relativeMode_; }

        /** @brief Reports unsupported desktop capture. @return False. */
        bool SetCapture(bool) override { return false; }
        /** @brief Reports no desktop position. @return False. */
        [[nodiscard]] bool TryGetGlobalPosition(float&, float&) const override { return false; }
        /** @brief Reports unsupported desktop warp. @return False. */
        bool SetGlobalPosition(float, float) override { return false; }

        /** @brief Gets the Update call count. @return Count. */
        [[nodiscard]] int UpdateCount() const { return updateCount_; }
        /** @brief Gets the SetPosition call count. @return Count. */
        [[nodiscard]] int PositionCalls() const { return positionCalls_; }
        /** @brief Gets the most recent SetPosition window. @return Window id. */
        [[nodiscard]] WindowId LastPositionWindow() const { return lastPositionWindow_; }
        /** @brief Gets the relative-mode setter count. @return Count. */
        [[nodiscard]] int RelativeModeCalls() const { return relativeModeCalls_; }
        /** @brief Gets the most recent relative-mode window. @return Window id. */
        [[nodiscard]] WindowId LastRelativeWindow() const { return lastRelativeWindow_; }
        /** @brief Gets the SetCursor call count. @return Count. */
        [[nodiscard]] int CursorCalls() const { return cursorCalls_; }
        /** @brief Gets the last system cursor. @return Shape. */
        [[nodiscard]] SystemCursor LastSystemCursor() const { return cursor_; }
        /** @brief Gets whether the last cursor was custom. @return True for an image cursor. */
        [[nodiscard]] bool LastCursorWasCustom() const { return customCursor_; }
        /** @brief Gets the last custom cursor width. @return Width. */
        [[nodiscard]] int LastCustomCursorWidth() const { return customCursorWidth_; }
        /** @brief Gets the last custom cursor height. @return Height. */
        [[nodiscard]] int LastCustomCursorHeight() const { return customCursorHeight_; }
        /** @brief Gets the last custom cursor hot-spot x. @return X coordinate. */
        [[nodiscard]] int LastCustomCursorHotSpotX() const { return customCursorHotSpotX_; }
        /** @brief Gets the last custom cursor hot-spot y. @return Y coordinate. */
        [[nodiscard]] int LastCustomCursorHotSpotY() const { return customCursorHotSpotY_; }
        /** @brief Gets an owned copy of the last custom cursor pixels. @return Pixels. */
        [[nodiscard]] const std::vector<std::uint32_t>& LastCustomCursorPixels() const
        {
            return customCursorPixels_;
        }

    private:
        MouseSnapshot pending_;
        MouseSnapshot snapshot_;
        float pendingDeltaX_ = 0.0f;
        float pendingDeltaY_ = 0.0f;
        float relativeDeltaX_ = 0.0f;
        float relativeDeltaY_ = 0.0f;
        bool relativeMode_ = false;
        bool cursorVisible_ = true;
        SystemCursor cursor_ = SystemCursor::Arrow;
        bool customCursor_ = false;
        int customCursorWidth_ = 0;
        int customCursorHeight_ = 0;
        int customCursorHotSpotX_ = 0;
        int customCursorHotSpotY_ = 0;
        std::vector<std::uint32_t> customCursorPixels_;
        int updateCount_ = 0;
        int positionCalls_ = 0;
        int cursorCalls_ = 0;
        WindowId lastPositionWindow_ = 0;
        int relativeModeCalls_ = 0;
        WindowId lastRelativeWindow_ = 0;
    };

    /** @brief A platform that is real in every respect except its mouse service. */
    class CannedMousePlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Gets the scripted mouse, or null when hidden. @return Mouse service. */
        [[nodiscard]] IPlatformMouse* GetMouse() override
        {
            return mouseAvailable_ ? &mouse_ : nullptr;
        }

        /** @brief Gets the scripted service for writing. @return Scripted mouse. */
        [[nodiscard]] CannedMouse& Canned() { return mouse_; }
        /** @brief Controls whether the platform exposes a mouse. @param available New state. */
        void SetMouseAvailable(const bool available) { mouseAvailable_ = available; }

    private:
        CannedMouse mouse_;
        bool mouseAvailable_ = true;
    };

} // namespace CNA::Platform::Testing
