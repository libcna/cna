// SPDX-License-Identifier: MS-PL
//
// plan_platform.md PLAT-131 -- terminal restoration, on the exit paths that kill the process.
//
// TerminalSession promises the terminal comes back on normal exit, on SIGINT/SIGTERM/SIGHUP, on
// an uncaught exception and on abort(). Four of those five destroy the process, so none of them
// can be asserted from inside a GTest binary: the assertion would die with everything else, and a
// test that "passed" because the process vanished before it could fail is worse than no test.
//
// So this is a separate program. TerminalRestorationTests allocates a pseudo-terminal, spawns
// this with the terminal's device end as its stdin and stdout, tells it how to die, and then --
// still holding that terminal itself -- checks that the echo flag came back and that the
// restoring escape sequence was actually written. Same "needs its own process" precedent as
// tools/net/two_process_harness.cpp and tools/devices/shutdown_ordering_harness.cpp.
//
//   terminal_restoration_harness <mode>
//
//     normal      enter a session, leave the scope, exit 0
//     sigint      enter a session, then raise(SIGINT)
//     sigterm     enter a session, then raise(SIGTERM)
//     sighup      enter a session, then raise(SIGHUP)
//     abort       enter a session, then abort()
//     throw       enter a session, then throw an uncaught exception
//     leak        enter a session and _exit(0) without unwinding -- the one case restoration
//                 genuinely CANNOT cover, present so the tests can prove the others are not
//                 passing by accident (a test that cannot fail proves nothing)
//
// Exit status is deliberately not the signal: the harness is expected to die of the signal it
// raises, and the test asserts on that.

#include "../../modules/platform/src/Terminal/TerminalSession.hpp"

#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {

using CNA::Platform::Terminal::TerminalSession;
using CNA::Platform::Terminal::TerminalSessionOptions;

/// Every mode the session knows how to take, so that each death mode below is measured against
/// the largest thing there is to restore. `HasEpilogue()` in TerminalRestorationTests.cpp builds
/// the expected bytes from the same option set and compares them exactly -- the two must agree.
TerminalSessionOptions EverythingOn()
{
    TerminalSessionOptions options;
    options.rawMode = true;
    options.alternateScreen = true;
    options.hideCursor = true;
    options.mouseReporting = true;
    // The keyboard mode is the one whose leak a user cannot shrug off: with the Kitty stack left
    // pushed, the shell that inherits the terminal receives every keystroke as a CSI-u sequence
    // and is unusable until it is reset by hand.
    options.kittyKeyboard = true;
    return options;
}

} // namespace

int main(const int argc, char** argv)
{
    if (argc < 2)
    {
        return 2;
    }
    const char* mode = argv[1];

    if (std::strcmp(mode, "leak") == 0)
    {
        // Deliberately unrecoverable: _exit() runs no destructor, no atexit handler and no signal
        // handler. The terminal stays raw. This is the control -- if this mode's assertions
        // looked the same as the others', the others would be proving nothing.
        auto* session = new TerminalSession(STDOUT_FILENO, STDIN_FILENO, EverythingOn());
        (void)session;
        _exit(0);
    }

    // No try/catch anywhere below, and that is load-bearing for the "throw" mode: an exception
    // caught here would unwind through the destructor, which restores, and the test would pass
    // while proving nothing about the terminate handler. Left uncaught, the C++ runtime calls
    // std::terminate *without* unwinding -- so the destructor never runs and restoration has to
    // come from std::set_terminate or not at all.
    const TerminalSession session(STDOUT_FILENO, STDIN_FILENO, EverythingOn());

    if (std::strcmp(mode, "normal") == 0)
    {
        return 0;  // the destructor restores on the way out
    }
    if (std::strcmp(mode, "sigint") == 0)
    {
        raise(SIGINT);
    }
    else if (std::strcmp(mode, "sigterm") == 0)
    {
        raise(SIGTERM);
    }
    else if (std::strcmp(mode, "sighup") == 0)
    {
        raise(SIGHUP);
    }
    else if (std::strcmp(mode, "abort") == 0)
    {
        std::abort();
    }
    else if (std::strcmp(mode, "throw") == 0)
    {
        throw std::runtime_error("PLAT-131 uncaught exception while holding the terminal");
    }
    else
    {
        return 2;
    }

    // Reached only if a signal that was supposed to kill the process did not.
    return 4;
}
