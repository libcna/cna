// SPDX-License-Identifier: MS-PL
#pragma once

#include "TerminalSession.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace CNA::Platform::Terminal {

    /** @brief A reason one subsystem currently needs ownership of the terminal. */
    enum class TerminalSessionUse : std::size_t
    {
        /** @brief Keyboard input in raw mode, with Kitty reporting when supported. */
        Keyboard,
        /** @brief Frame presentation on the alternate screen with the cursor hidden. */
        Presenter,
        /** @brief SGR-1006 mouse reporting. */
        Mouse,
        /** @brief Number of use kinds; not a usable lease kind. */
        Count
    };

    /**
     * @brief Shares the process's one terminal session between presentation and input.
     *
     * Merely constructing the controller touches nothing. The first lease starts a session, a
     * second kind upgrades its options, and releasing the last lease restores the terminal.
     * Reconfiguration deliberately rebuilds the session: that gives the signal-safe restoration
     * record one immutable option set at a time instead of mutating memory a crash handler could
     * be reading.
     */
    class TerminalSessionController final
        : public std::enable_shared_from_this<TerminalSessionController>
    {
    public:
        /** @brief A movable ownership token for one terminal use. */
        class Lease
        {
        public:
            /** @brief Creates an empty lease. */
            Lease() = default;
            /** @brief Releases the held use, if any. */
            ~Lease();

            Lease(const Lease&) = delete;
            Lease& operator=(const Lease&) = delete;

            /** @brief Moves a lease. @param other The lease to take. */
            Lease(Lease&& other) noexcept;
            /** @brief Replaces this lease by another. @param other The lease to take. @return This lease. */
            Lease& operator=(Lease&& other) noexcept;

            /** @brief Gets whether this token currently owns a use. @return True when non-empty. */
            [[nodiscard]] explicit operator bool() const { return controller_ != nullptr; }

        private:
            friend class TerminalSessionController;
            Lease(std::shared_ptr<TerminalSessionController> controller, TerminalSessionUse use);
            void Reset() noexcept;

            std::shared_ptr<TerminalSessionController> controller_;
            TerminalSessionUse use_ = TerminalSessionUse::Count;
        };

        /**
         * @brief Creates a dormant controller.
         * @param outputDescriptor The descriptor presentation escapes use.
         * @param inputDescriptor The descriptor input and raw mode use.
         * @param kittyKeyboardSupported Whether Kitty keyboard mode was detected.
         */
        TerminalSessionController(int outputDescriptor, int inputDescriptor,
                                  bool kittyKeyboardSupported);

        /**
         * @brief Acquires one terminal use and applies the union of all requested modes.
         * @param use The use to acquire.
         * @return A lease whose destruction releases the use.
         */
        [[nodiscard]] Lease Acquire(TerminalSessionUse use);

        /** @brief Gets the input descriptor. @return The descriptor keys arrive on. */
        [[nodiscard]] int GetInputDescriptor() const { return inputDescriptor_; }
        /** @brief Gets the output descriptor. @return The descriptor frames use. */
        [[nodiscard]] int GetOutputDescriptor() const { return outputDescriptor_; }

        /**
         * @brief Gets whether keyboard leases enable the exact Kitty protocol.
         * @return True for exact press/repeat/release input; false for the timed fallback.
         */
        [[nodiscard]] bool HasKittyKeyboard() const { return kittyKeyboardSupported_; }

        /**
         * @brief Gets a counter changed whenever rebuilding invalidates displayed contents.
         * @return The current generation.
         */
        [[nodiscard]] std::uint64_t GetGeneration() const { return generation_; }

    private:
        void Release(TerminalSessionUse use) noexcept;
        void Refresh();
        void RefreshNoThrow() noexcept;
        void RestoreAfterFailedAcquire(TerminalSessionUse use) noexcept;
        [[nodiscard]] TerminalSessionOptions BuildOptions() const;
        [[nodiscard]] bool HasAnyUse() const;

        int outputDescriptor_;
        int inputDescriptor_;
        bool kittyKeyboardSupported_;
        std::array<std::size_t, static_cast<std::size_t>(TerminalSessionUse::Count)> uses_{};
        std::unique_ptr<TerminalSession> session_;
        TerminalSessionOptions activeOptions_{};
        bool haveActiveOptions_ = false;
        std::uint64_t generation_ = 0;
    };

} // namespace CNA::Platform::Terminal
