// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformMouse.hpp"

#include <memory>

namespace CNA::Platform::Terminal {

    class TerminalInputDecoder;

    /** @brief Cell-quantised mouse snapshots backed by xterm SGR-1006 reports. */
    class TerminalMouse final : public IPlatformMouse
    {
    public:
        /**
         * @brief Wraps the platform's shared terminal input decoder.
         * @param decoder The decoder also used by the keyboard and platform event pump.
         */
        explicit TerminalMouse(std::shared_ptr<TerminalInputDecoder> decoder);

        /** @brief Enables mouse reports, pumps input and refreshes the snapshot. */
        void Update() override;
        /** @brief Gets the most recent cell-quantised snapshot. @return The snapshot. */
        [[nodiscard]] const MouseSnapshot& GetSnapshot() const override;
        /** @brief Relative mode is unavailable. @return Zero displacement. */
        [[nodiscard]] MouseDelta ConsumeRelativeDelta() override;

        /**
         * @brief Refuses pointer warping, which SGR-1006 cannot express.
         * @param window The terminal window id.
         * @param x Requested client x.
         * @param y Requested client y.
         * @throws PlatformException Always.
         */
        void SetPosition(WindowId window, int x, int y) override;

        /**
         * @brief Refuses pointer visibility control, which terminal protocols do not expose.
         * @param visible Requested visibility.
         * @throws PlatformException Always.
         */
        void SetCursorVisible(bool visible) override;

        /**
         * @brief Refuses a graphical cursor shape.
         * @param cursor The requested shape.
         * @throws PlatformNotSupportedException Always; `CursorShapes` is false.
         */
        void SetCursor(SystemCursor cursor) override;

        /**
         * @brief Refuses a graphical image cursor.
         * @param cursor The requested image and hot spot.
         * @throws PlatformNotSupportedException Always; `CursorShapes` is false.
         */
        void SetCursor(const CursorImage& cursor) override;

        /**
         * @brief Refuses relative pointer mode.
         * @param window The terminal window id.
         * @param enabled The requested state.
         * @throws PlatformNotSupportedException Always; `RelativeMouse` is false.
         */
        void SetRelativeMode(WindowId window, bool enabled) override;
        /** @brief Reports that relative mode is unavailable. @return False. */
        [[nodiscard]] bool IsRelativeMode() const override;

        /**
         * @brief Refuses desktop-space capture.
         * @param enabled The requested state.
         * @return Never returns.
         * @throws PlatformNotSupportedException Always; `GlobalPointer` is false.
         */
        bool SetCapture(bool enabled) override;

        /**
         * @brief Refuses a desktop-space position query.
         * @param x Untouched.
         * @param y Untouched.
         * @return Never returns.
         * @throws PlatformNotSupportedException Always; `GlobalPointer` is false.
         */
        [[nodiscard]] bool TryGetGlobalPosition(float& x, float& y) const override;

        /**
         * @brief Refuses a desktop-space warp.
         * @param x Requested desktop x.
         * @param y Requested desktop y.
         * @return Never returns.
         * @throws PlatformNotSupportedException Always; `GlobalPointer` is false.
         */
        bool SetGlobalPosition(float x, float y) override;

    private:
        std::shared_ptr<TerminalInputDecoder> decoder_;
    };

} // namespace CNA::Platform::Terminal
