// SPDX-License-Identifier: MS-PL
#pragma once

#include <termios.h>

#include <cstddef>
#include <string>

namespace CNA::Platform::Terminal {

    /**
     * @brief Which parts of the terminal a session takes over.
     *
     * Each is separately switchable because they are separately *needed*: presentation
     * (PLAT-132) wants the alternate screen and a hidden cursor, input (PLAT-137/138) wants raw
     * mode, and mouse reporting (PLAT-139) is worth turning on only when something reads it —
     * a terminal with mouse reporting enabled sends bytes on every pointer movement, which is
     * wasted bandwidth on a link the frame budget is already rationing.
     */
    struct TerminalSessionOptions
    {
        /** @brief Disable canonical mode and echo so keys arrive as they are pressed. */
        bool rawMode = true;
        /** @brief Draw on the alternate screen buffer, leaving the user's scrollback intact. */
        bool alternateScreen = true;
        /** @brief Hide the text cursor, which would otherwise blink over the frame. */
        bool hideCursor = true;
        /** @brief Ask the terminal to report mouse press, release, motion and wheel (SGR 1006). */
        bool mouseReporting = false;
        /** @brief Ask a Kitty-capable terminal to report every key's press, repeat and release. */
        bool kittyKeyboard = false;
    };

    /**
     * @brief Ownership of the terminal, with restoration guaranteed on every exit path.
     *
     * ### Why this is its own class and its own task
     *
     * A process that dies while holding the terminal leaves a real person with no echo, an
     * invisible cursor, their scrollback replaced by a half-drawn frame, and a shell that appears
     * to have stopped responding. That is a hostile failure, and it is *more* likely on the paths
     * nobody tests: a crash, an assertion, `Ctrl-C`, the terminal window being closed.
     *
     * So restoration here does not rest on the destructor alone. It covers, in order of how
     * easily each is forgotten:
     *
     * - **Normal scope exit** — the destructor.
     * - **`SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`** — a handler that restores, reinstates the
     *   previous disposition and re-raises, so the process still dies of the signal it was sent
     *   and its exit status stays honest.
     * - **`SIGABRT`** — which is how a failed assertion and `std::terminate` both arrive.
     * - **`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`** — the hard crashes. Re-raising with the
     *   default disposition keeps the core dump.
     * - **An uncaught exception** — via `std::set_terminate`, which restores *before* chaining to
     *   the previous handler. Order matters: the message the runtime prints would otherwise land
     *   on the alternate screen and disappear with it, turning a diagnosable crash into a silent
     *   one.
     *
     * ### What a signal handler is allowed to do
     *
     * Almost nothing. The restoration path calls only `write(2)` and `tcsetattr(3)` — both
     * async-signal-safe — over state captured *before* any handler was installed. No allocation,
     * no `std::string`, no stream. That is why the escape sequence to emit is built once at
     * construction into a fixed buffer rather than assembled when it is needed.
     *
     * ### One at a time
     *
     * There is one terminal, and restoration state lives in process-global storage because a
     * signal handler cannot be given a context. A second concurrent session is therefore refused
     * rather than allowed to overwrite the first's saved settings — which would restore the
     * terminal to the *raw* state the first session left it in.
     */
    class TerminalSession
    {
    public:
        /**
         * @brief Takes over the terminal.
         *
         * @param outputDescriptor The descriptor escape sequences are written to.
         * @param inputDescriptor The descriptor whose line discipline raw mode applies to.
         * @param options Which parts of the terminal to take over.
         * @throws PlatformException If another session is already active, or if the descriptors
         *         do not refer to a terminal.
         */
        TerminalSession(int outputDescriptor, int inputDescriptor,
                        const TerminalSessionOptions& options);

        /** @brief Restores everything this session changed. */
        ~TerminalSession();

        TerminalSession(const TerminalSession&) = delete;
        TerminalSession& operator=(const TerminalSession&) = delete;

        /** @brief Gets the descriptor frames are written to. @return The output descriptor. */
        [[nodiscard]] int GetOutputDescriptor() const { return outputDescriptor_; }

        /** @brief Gets the descriptor input is read from. @return The input descriptor. */
        [[nodiscard]] int GetInputDescriptor() const { return inputDescriptor_; }

        /** @brief Gets what this session took over. @return The options it was created with. */
        [[nodiscard]] const TerminalSessionOptions& GetOptions() const { return options_; }

        /**
         * @brief Reports whether a session currently owns the terminal.
         *
         * @return True between the construction and destruction of a session.
         */
        [[nodiscard]] static bool IsActive();

    private:
        int outputDescriptor_;
        int inputDescriptor_;
        TerminalSessionOptions options_;
        termios savedAttributes_{};
        bool savedAttributesValid_ = false;
    };

    /**
     * @brief Returns the escape sequence a session emits when it takes the terminal over.
     *
     * Exposed so a test can assert on the exact bytes rather than on a paraphrase of them, and so
     * the two sequences can be checked against each other for symmetry.
     *
     * @param options Which parts of the terminal would be taken over.
     * @return The prologue, empty when @p options asks for nothing that needs one.
     */
    [[nodiscard]] std::string BuildSessionPrologue(const TerminalSessionOptions& options);

    /**
     * @brief Returns the escape sequence a session emits when it gives the terminal back.
     *
     * The exact inverse of BuildSessionPrologue, in reverse order — colour and mouse reporting are
     * reset *before* the alternate screen is left, because after leaving it the terminal is the
     * user's again and anything still set is theirs to live with.
     *
     * @param options Which parts of the terminal were taken over.
     * @return The epilogue, empty when @p options asks for nothing that needs one.
     */
    [[nodiscard]] std::string BuildSessionEpilogue(const TerminalSessionOptions& options);

} // namespace CNA::Platform::Terminal
