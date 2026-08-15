// SPDX-License-Identifier: MS-PL

#include "TerminalResizeWatcher.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <signal.h>

#include <atomic>

namespace CNA::Platform::Terminal {

    namespace {

        /// Set by the handler, cleared by the poll. `sig_atomic_t` is the only type a handler may
        /// write without a race, and `volatile` is what stops the compiler caching the read in
        /// the loop that clears it.
        volatile sig_atomic_t g_resized = 0;

        std::atomic<bool> g_watching{false};
        struct sigaction g_previous = {};
        bool g_previousValid = false;

        extern "C" void OnWindowChange(int)
        {
            // The entire handler. Anything more -- querying the size, allocating an event -- would
            // be calling functions that are not async-signal-safe from a context that may have
            // interrupted them mid-update.
            g_resized = 1;
        }

    } // namespace

    TerminalResizeWatcher::TerminalResizeWatcher()
    {
        if (g_watching.load())
        {
            throw PlatformException("TerminalResizeWatcher",
                                    "a resize watcher is already active in this process");
        }

        struct sigaction watching = {};
        watching.sa_handler = &OnWindowChange;
        sigemptyset(&watching.sa_mask);
        // SA_RESTART so a resize does not turn every blocking read in the process into EINTR.
        // Unlike the session's handlers, this one does not end anything -- the flag is picked up
        // at the next poll, and interrupting unrelated work to deliver it would be a nuisance
        // rather than a service.
        watching.sa_flags = SA_RESTART;
        sigaction(SIGWINCH, &watching, &g_previous);
        g_previousValid = true;

        g_resized = 0;
        g_watching.store(true);
    }

    TerminalResizeWatcher::~TerminalResizeWatcher()
    {
        if (g_previousValid)
        {
            sigaction(SIGWINCH, &g_previous, nullptr);
            g_previousValid = false;
        }
        g_resized = 0;
        g_watching.store(false);
    }

    bool TerminalResizeWatcher::TakePendingResize()
    {
        if (g_resized == 0)
        {
            return false;
        }
        g_resized = 0;
        return true;
    }

    void TerminalResizeWatcher::SimulateResizeForTesting() { g_resized = 1; }

    bool TerminalResizeWatcher::IsWatching() { return g_watching.load(); }

} // namespace CNA::Platform::Terminal
