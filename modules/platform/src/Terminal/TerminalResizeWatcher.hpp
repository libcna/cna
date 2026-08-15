// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Platform::Terminal {

    /**
     * @brief Notices that the terminal was resized.
     *
     * A terminal announces a resize with `SIGWINCH` and nothing else — there is no event queue to
     * poll and no state to compare against except the size itself. So this installs a handler that
     * does the only thing a signal handler safely can, setting a flag, and the platform's normal
     * per-frame poll turns that flag into a `PlatformEvent`.
     *
     * ### Why a flag and not a queue
     *
     * `sig_atomic_t` is the only type a handler may touch without a race, and even `write(2)` to a
     * self-pipe would be more machinery than this needs: resizes coalesce naturally. Ten resizes
     * between two frames are one resize as far as any caller is concerned, because what a caller
     * wants is the *current* size, which is read at poll time anyway.
     *
     * ### One at a time
     *
     * The handler is process-global, so a second watcher would overwrite the first's saved
     * disposition and leave `SIGWINCH` pointing at a handler whose owner is gone. A second one is
     * therefore refused.
     */
    class TerminalResizeWatcher
    {
    public:
        /**
         * @brief Starts watching for terminal resizes.
         *
         * @throws PlatformException If another watcher is already active in this process.
         */
        TerminalResizeWatcher();

        /** @brief Stops watching and restores the previous `SIGWINCH` disposition. */
        ~TerminalResizeWatcher();

        TerminalResizeWatcher(const TerminalResizeWatcher&) = delete;
        TerminalResizeWatcher& operator=(const TerminalResizeWatcher&) = delete;

        /**
         * @brief Consumes a pending resize, if any.
         *
         * @return True if the terminal was resized since this was last called.
         */
        [[nodiscard]] static bool TakePendingResize();

        /**
         * @brief Marks a resize as pending, as though `SIGWINCH` had arrived.
         *
         * The only way to test the resize path without resizing a real terminal, which no test
         * can do: a pseudo-terminal's size can be changed, but `SIGWINCH` is delivered to the
         * foreground process group of the *controlling* terminal, which a test process's pty is
         * not.
         */
        static void SimulateResizeForTesting();

        /**
         * @brief Reports whether a watcher is installed.
         *
         * @return True between the construction and destruction of a watcher.
         */
        [[nodiscard]] static bool IsWatching();
    };

} // namespace CNA::Platform::Terminal
