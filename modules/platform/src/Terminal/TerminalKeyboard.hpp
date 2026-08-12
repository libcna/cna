// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/PlatformEvent.hpp"

#include "TerminalSessionController.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CNA::Platform::Terminal {

    /**
     * @brief Decodes one canonical Kitty keyboard `CSI ... u` sequence.
     *
     * @param sequence The complete sequence, including CSI and its final `u`.
     * @param event Receives the CNA key event; untouched on failure.
     * @return True when the sequence is a supported, well-formed key event.
     */
    [[nodiscard]] bool DecodeKittyKeyEvent(std::string_view sequence, KeyEvent& event);

    /**
     * @brief Decodes one traditional terminal key sequence which has no release information.
     *
     * Covers printable ASCII, control chords, xterm CSI navigation/editing/function sequences,
     * SS3 F1--F4, modifiers carried by xterm's `;N` parameter, and Alt-prefixed keys.
     *
     * @param sequence One complete traditional terminal key sequence.
     * @param event Receives a press event; untouched when the sequence is not a known key.
     * @return True when @p sequence names a supported key.
     */
    [[nodiscard]] bool DecodeLegacyKeyEvent(std::string_view sequence, KeyEvent& event);

    /** @brief The semantic kind carried by one decoded SGR-1006 mouse report. */
    enum class SgrMouseReportKind
    {
        /** @brief A physical button was pressed or released. */
        Button,
        /** @brief The pointer entered another terminal cell. */
        Motion,
        /** @brief A vertical or horizontal wheel step occurred. */
        Wheel
    };

    /** @brief One decoded SGR-1006 report before cell coordinates become client coordinates. */
    struct SgrMouseReport
    {
        /** @brief What the report describes. */
        SgrMouseReportKind kind = SgrMouseReportKind::Motion;
        /** @brief One-based terminal column. */
        std::uint32_t column = 0;
        /** @brief One-based terminal row. */
        std::uint32_t row = 0;
        /** @brief CNA button number (1 left, 2 middle, 3 right), or zero. */
        std::uint8_t button = 0;
        /** @brief True for a button press, false for a release. */
        bool pressed = false;
        /** @brief Horizontal wheel step: -1 left, +1 right, or zero. */
        int wheelX = 0;
        /** @brief Vertical wheel step: -1 down, +1 up, or zero. */
        int wheelY = 0;
    };

    /**
     * @brief Decodes one xterm SGR-1006 mouse report.
     * @param sequence The complete `CSI < Cb ; Cx ; Cy M|m` report.
     * @param report Receives cell coordinates and event semantics; untouched on failure.
     * @return True when the complete sequence is a supported report.
     */
    [[nodiscard]] bool DecodeSgrMouseReport(std::string_view sequence, SgrMouseReport& report);

    /**
     * @brief The shared terminal input pump and held-key state.
     *
     * Both `PollEvents()` and `IPlatformKeyboard::Update()` may call Pump. Bytes are consumed only
     * once, events wait in a queue for `PollEvents`, and the held-key set remains available to the
     * snapshot service. This prevents the two callers from racing to drain the terminal.
     */
    class TerminalInputDecoder final
    {
    public:
        /**
         * @brief Creates a dormant decoder; the first Pump acquires raw keyboard input mode.
         *
         * The controller decides whether that lease enables exact Kitty reporting or the timed
         * legacy fallback.
         *
         * @param sessions The controller shared with the presenter.
         */
        explicit TerminalInputDecoder(std::shared_ptr<TerminalSessionController> sessions);

        /** @brief Reads every key sequence currently available without blocking. */
        void Pump();

        /** @brief Acquires mouse reporting and pumps currently available input. */
        void PumpMouse();

        /** @brief Acquires both input modes and pumps the platform event stream. */
        void PumpAll();

        /**
         * @brief Pumps input using a supplied monotonic instant.
         *
         * This is the deterministic seam for testing synthetic release deadlines; production
         * callers use Pump(), which supplies `steady_clock::now()`.
         *
         * @param now The monotonic instant at which buffered input and expirations are processed.
         */
        void PumpAt(std::chrono::steady_clock::time_point now);

        /**
         * @brief Moves all decoded events into a caller's batch.
         * @param destination The batch to append to.
         * @param window The focused terminal window, or zero.
         */
        void DrainEvents(std::vector<PlatformEvent>& destination, WindowId window);

        /** @brief Gets the current held-key snapshot. @return Exact or synthesised state. */
        [[nodiscard]] const KeyboardSnapshot& GetSnapshot() const { return snapshot_; }

        /** @brief Gets the current cell-quantised mouse snapshot. @return The snapshot. */
        [[nodiscard]] const MouseSnapshot& GetMouseSnapshot() const { return mouseSnapshot_; }

    private:
        struct SyntheticHeldKey
        {
            std::chrono::steady_clock::time_point expiresAt;
            std::uint16_t modifiers = 0;
        };

        void ConsumeCompleteSequences(std::chrono::steady_clock::time_point now);
        void ConsumeKittySequences();
        void ConsumeLegacySequences(std::chrono::steady_clock::time_point now);
        void ApplySyntheticPress(KeyEvent event, std::chrono::steady_clock::time_point now);
        void ExpireSyntheticKeys(std::chrono::steady_clock::time_point now);
        void PumpInput(std::chrono::steady_clock::time_point now);
        void Apply(const KeyEvent& event);
        void Apply(const SgrMouseReport& report);
        void RebuildSnapshot();

        std::shared_ptr<TerminalSessionController> sessions_;
        TerminalSessionController::Lease keyboardLease_;
        TerminalSessionController::Lease mouseLease_;
        bool exactKeyboardState_ = false;
        std::string inputBuffer_;
        std::set<std::pair<Scancode, KeyCode>> heldKeys_;
        std::map<std::pair<Scancode, KeyCode>, SyntheticHeldKey> syntheticHeldKeys_;
        std::set<KeyCode> pressed_;
        std::vector<PlatformEvent> events_;
        KeyboardSnapshot snapshot_;
        MouseSnapshot mouseSnapshot_;
        std::uint16_t lockModifiers_ = 0;
        std::optional<std::chrono::steady_clock::time_point> pendingEscapeSince_;
    };

    /** @brief Terminal held-key snapshots, exact with Kitty and timed otherwise. */
    class TerminalKeyboard final : public IPlatformKeyboard
    {
    public:
        /**
         * @brief Wraps the platform's shared input decoder.
         * @param decoder The decoder also used by the event pump.
         */
        explicit TerminalKeyboard(std::shared_ptr<TerminalInputDecoder> decoder);

        /** @brief Pumps input and refreshes the snapshot. */
        void Update() override;
        /** @brief Gets the most recent snapshot. @return Exact or synthesised state. */
        [[nodiscard]] const KeyboardSnapshot& GetSnapshot() const override;
        /** @brief Reports the attached terminal keyboard. @return True always. */
        [[nodiscard]] bool HasKeyboard() const override;

    private:
        std::shared_ptr<TerminalInputDecoder> decoder_;
    };

} // namespace CNA::Platform::Terminal
