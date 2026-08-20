// SPDX-License-Identifier: MS-PL
#pragma once

// Shared scaffolding for the terminal-platform tests (plans/plan_platform.md Phase 10).
//
// Everything a terminal does is invisible without a terminal, and CI has none: output is
// redirected, so `isatty` is false and every interesting path refuses. A test that skips there is
// a test that never runs, which is the failure mode this campaign has already been bitten by once
// -- 21 conformance tests that matched no filter and were green for years.
//
// So the tests make their own terminal. A pseudo-terminal is a real tty by every check the code
// performs, its size can be set, and what a program writes to it can be read back. Where the code
// under test insists on the process's own standard descriptors -- SIGWINCH goes to the foreground
// process group of the *controlling* terminal, which no test process's pty is -- a harness is
// spawned with the pty as its stdin and stdout instead.

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <string>
#include <thread>

extern char** environ;

namespace CNA::Platform::TestSupport {

    /** @brief A pseudo-terminal the test owns, plays and inspects. */
    class PseudoTerminal
    {
    public:
        /**
         * @brief Allocates a pseudo-terminal.
         *
         * @param columns Width to report, in cells; zero leaves the size unset.
         * @param rows Height to report, in cells; zero leaves the size unset.
         */
        explicit PseudoTerminal(const int columns = 0, const int rows = 0)
        {
            controller_ = posix_openpt(O_RDWR | O_NOCTTY);
            if (controller_ < 0 || grantpt(controller_) != 0 || unlockpt(controller_) != 0)
            {
                return;
            }
            const char* name = ptsname(controller_);
            if (name == nullptr)
            {
                return;
            }
            device_ = open(name, O_RDWR | O_NOCTTY);
            if (device_ >= 0 && columns > 0 && rows > 0)
            {
                SetSize(columns, rows);
            }
        }

        ~PseudoTerminal()
        {
            if (device_ >= 0)
            {
                close(device_);
            }
            if (controller_ >= 0)
            {
                close(controller_);
            }
        }

        PseudoTerminal(const PseudoTerminal&) = delete;
        PseudoTerminal& operator=(const PseudoTerminal&) = delete;

        /** @brief Whether allocation succeeded. @return True if both ends are open. */
        [[nodiscard]] bool IsOpen() const { return controller_ >= 0 && device_ >= 0; }

        /** @brief The end the program under test sees. @return The device descriptor. */
        [[nodiscard]] int Device() const { return device_; }

        /** @brief The end this test plays the terminal from. @return The controller descriptor. */
        [[nodiscard]] int Controller() const { return controller_; }

        /**
         * @brief Changes the terminal's reported size.
         *
         * @param columns Width in cells.
         * @param rows Height in cells.
         */
        void SetSize(const int columns, const int rows) const
        {
            winsize size{};
            size.ws_col = static_cast<unsigned short>(columns);
            size.ws_row = static_cast<unsigned short>(rows);
            ioctl(device_, TIOCSWINSZ, &size);
        }

        /** @brief Whether echo is currently on. @return True if the `ECHO` flag is set. */
        [[nodiscard]] bool EchoIsOn() const
        {
            termios attributes{};
            if (tcgetattr(device_, &attributes) != 0)
            {
                return false;
            }
            return (attributes.c_lflag & static_cast<tcflag_t>(ECHO)) != 0;
        }

        /**
         * @brief Reads everything sent to the terminal that has not been read yet.
         *
         * @param quietMilliseconds How long to wait for more before concluding there is none.
         * @return The bytes collected.
         */
        [[nodiscard]] std::string DrainOutput(const int quietMilliseconds = 120) const
        {
            std::string collected;
            char buffer[1024];
            for (;;)
            {
                pollfd waiting{};
                waiting.fd = controller_;
                waiting.events = POLLIN;
                if (poll(&waiting, 1, quietMilliseconds) <= 0)
                {
                    break;
                }
                const ssize_t count = read(controller_, buffer, sizeof(buffer));
                if (count <= 0)
                {
                    break;
                }
                collected.append(buffer, static_cast<std::size_t>(count));
                if (collected.size() > 65536)
                {
                    break;
                }
            }
            return collected;
        }

    private:
        int controller_ = -1;
        int device_ = -1;
    };

    /**
     * @brief Keeps reading a pseudo-terminal so that writing to it never blocks.
     *
     * A pty's buffer is a few kilobytes. A truecolor full-screen frame is tens, so a test that
     * presents one to a terminal nobody is reading **deadlocks**: the write fills the buffer and
     * waits for a reader that is the same thread. This is not a hypothetical — it is how the
     * frame-budget tests first behaved, and the symptom is a hang rather than a failure, which is
     * considerably worse to diagnose.
     *
     * Nothing here inspects what it reads. A test that needs the bytes should use
     * PseudoTerminal::DrainOutput() instead; this exists purely so the writer can make progress.
     */
    class PseudoTerminalDrain
    {
    public:
        /**
         * @brief Starts draining.
         *
         * @param controller The controller end to read from.
         */
        explicit PseudoTerminalDrain(const int controller)
            : thread_([this, controller] {
                char buffer[4096];
                while (!stop_.load())
                {
                    pollfd waiting{};
                    waiting.fd = controller;
                    waiting.events = POLLIN;
                    if (poll(&waiting, 1, 20) <= 0)
                    {
                        continue;
                    }
                    if (read(controller, buffer, sizeof(buffer)) <= 0)
                    {
                        return;
                    }
                }
            })
        {
        }

        ~PseudoTerminalDrain()
        {
            stop_.store(true);
            thread_.join();
        }

        PseudoTerminalDrain(const PseudoTerminalDrain&) = delete;
        PseudoTerminalDrain& operator=(const PseudoTerminalDrain&) = delete;

    private:
        std::atomic<bool> stop_{false};
        std::thread thread_;
    };

    /** @brief What running a harness under a pseudo-terminal produced. */
    struct HarnessRun
    {
        /** @brief Whether the harness was spawned at all. */
        bool spawned = false;
        /** @brief The wait status, for `WIFEXITED` and friends. */
        int status = 0;
        /** @brief Everything the harness wrote to the terminal. */
        std::string terminalOutput;
        /** @brief Whether echo was on after the harness exited. */
        bool echoRestored = false;
    };

    /**
     * @brief Runs a program with a pseudo-terminal as its standard descriptors.
     *
     * The pty is created here so that its state after the child exits is observable: this process
     * still holds the terminal, so it can be asked what mode it is in even though the program that
     * changed it is gone.
     *
     * @param path The executable to run; null or missing yields an unspawned result.
     * @param argument The single argument to pass.
     * @param columns Terminal width to report, in cells.
     * @param rows Terminal height to report, in cells.
     * @return What happened.
     */
    inline HarnessRun RunUnderPseudoTerminal(const char* path, const std::string& argument,
                                             const int columns = 80, const int rows = 24)
    {
        HarnessRun result;
        if (path == nullptr)
        {
            return result;
        }

        const PseudoTerminal pty(columns, rows);
        if (!pty.IsOpen())
        {
            return result;
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pty.Device(), STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pty.Device(), STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pty.Device(), STDERR_FILENO);

        char* argv[] = {const_cast<char*>(path), const_cast<char*>(argument.c_str()), nullptr};
        pid_t child = 0;
        const int spawned = posix_spawn(&child, path, &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);

        if (spawned != 0)
        {
            return result;
        }
        result.spawned = true;

        // Drained while the child runs: a full pty buffer would block it mid-write, and on the
        // restoration tests the blocked write is the very thing being measured.
        result.terminalOutput = pty.DrainOutput();
        waitpid(child, &result.status, 0);
        result.terminalOutput += pty.DrainOutput();
        result.echoRestored = pty.EchoIsOn();
        return result;
    }

} // namespace CNA::Platform::TestSupport
