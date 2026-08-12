// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/PlatformEvent.hpp"

#include "TerminalSessionController.hpp"

#include <memory>
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
     * @brief The shared terminal input pump and exact held-key state.
     *
     * Both `PollEvents()` and `IPlatformKeyboard::Update()` may call Pump. Bytes are consumed only
     * once, events wait in a queue for `PollEvents`, and the held-key set remains available to the
     * snapshot service. This prevents the two callers from racing to drain the terminal.
     */
    class TerminalInputDecoder final
    {
    public:
        /**
         * @brief Creates a dormant decoder; the first Pump acquires raw Kitty input mode.
         * @param sessions The controller shared with the presenter.
         */
        explicit TerminalInputDecoder(std::shared_ptr<TerminalSessionController> sessions);

        /** @brief Reads every key sequence currently available without blocking. */
        void Pump();

        /**
         * @brief Moves all decoded events into a caller's batch.
         * @param destination The batch to append to.
         * @param window The focused terminal window, or zero.
         */
        void DrainEvents(std::vector<PlatformEvent>& destination, WindowId window);

        /** @brief Gets the exact current held-key snapshot. @return The snapshot. */
        [[nodiscard]] const KeyboardSnapshot& GetSnapshot() const { return snapshot_; }

    private:
        void ConsumeCompleteSequences();
        void Apply(const KeyEvent& event);
        void RebuildSnapshot();

        std::shared_ptr<TerminalSessionController> sessions_;
        TerminalSessionController::Lease keyboardLease_;
        std::string inputBuffer_;
        std::set<std::pair<Scancode, KeyCode>> heldKeys_;
        std::set<KeyCode> pressed_;
        std::vector<KeyEvent> events_;
        KeyboardSnapshot snapshot_;
        std::uint16_t lockModifiers_ = 0;
    };

    /** @brief Exact held-key snapshots backed by the Kitty keyboard protocol. */
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
        /** @brief Gets the most recent exact snapshot. @return The snapshot. */
        [[nodiscard]] const KeyboardSnapshot& GetSnapshot() const override;
        /** @brief Reports the attached terminal keyboard. @return True always. */
        [[nodiscard]] bool HasKeyboard() const override;

    private:
        std::shared_ptr<TerminalInputDecoder> decoder_;
    };

} // namespace CNA::Platform::Terminal
