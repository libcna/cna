// SPDX-License-Identifier: MS-PL

#include "TerminalSession.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace CNA::Platform::Terminal {

    namespace {

        /// The signals worth restoring on, and why each is here.
        ///
        /// SIGINT/SIGQUIT are the user asking to stop; SIGTERM is a supervisor asking; SIGHUP is
        /// the terminal itself going away. SIGABRT covers both a failed assertion and
        /// std::terminate. The last four are hard crashes -- exactly the case where nobody
        /// remembered to clean up, and exactly the case a user is most likely to hit in the wild.
        constexpr int kRestoringSignals[] = {SIGINT, SIGTERM, SIGHUP,  SIGQUIT,
                                             SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL};
        constexpr std::size_t kRestoringSignalCount =
            sizeof(kRestoringSignals) / sizeof(kRestoringSignals[0]);

        constexpr std::size_t kMaxEpilogue = 128;

        /// Everything restoration needs, in storage a signal handler may legally reach.
        ///
        /// Process-global because a handler cannot be handed a context. Every field is written
        /// *before* any handler is installed and read-only afterwards, except `active`, so the
        /// handler never races with the constructor.
        struct RestorationRecord
        {
            std::atomic<bool> active{false};
            int outputDescriptor = -1;
            int inputDescriptor = -1;
            termios savedAttributes{};
            bool restoreAttributes = false;
            char epilogue[kMaxEpilogue] = {};
            std::size_t epilogueLength = 0;

            struct sigaction previousActions[kRestoringSignalCount] = {};
            bool previousActionsValid = false;
            std::terminate_handler previousTerminate = nullptr;
        };

        RestorationRecord& Record()
        {
            static RestorationRecord record;
            return record;
        }

        /// Gives the terminal back, using only what a signal handler is allowed to call.
        ///
        /// `write(2)` and `tcsetattr(3)` are both async-signal-safe. Nothing else here allocates,
        /// locks, or touches a stream -- which is the whole reason the epilogue is a fixed buffer
        /// filled at construction rather than a string built when it is needed.
        void RestoreNow()
        {
            RestorationRecord& record = Record();
            if (!record.active.exchange(false))
            {
                return;  // already restored, or never taken -- both are fine to be asked twice
            }

            if (record.epilogueLength > 0)
            {
                const ssize_t written =
                    write(record.outputDescriptor, record.epilogue, record.epilogueLength);
                (void)written;  // nothing useful to do about a failed restore inside a handler
            }
            if (record.restoreAttributes)
            {
                tcsetattr(record.inputDescriptor, TCSANOW, &record.savedAttributes);
            }
        }

        /// Restores, then lets the signal do what it was going to do anyway.
        ///
        /// Reinstating the previous disposition and re-raising rather than exiting keeps two
        /// things that matter: the process still reports as killed-by-signal instead of exiting
        /// zero, and a crash signal still produces a core dump.
        extern "C" void RestoringSignalHandler(const int signalNumber)
        {
            RestoreNow();

            RestorationRecord& record = Record();
            if (record.previousActionsValid)
            {
                for (std::size_t i = 0; i < kRestoringSignalCount; ++i)
                {
                    if (kRestoringSignals[i] == signalNumber)
                    {
                        sigaction(signalNumber, &record.previousActions[i], nullptr);
                        break;
                    }
                }
            }
            raise(signalNumber);
        }

        /// Restores *before* the runtime prints why it is dying.
        ///
        /// The ordering is the point. An uncaught exception's message printed while the alternate
        /// screen is still up scrolls away with it the moment the screen is left, turning a
        /// diagnosable crash into a process that vanished for no visible reason.
        void RestoringTerminateHandler()
        {
            RestoreNow();

            const std::terminate_handler previous = Record().previousTerminate;
            if (previous != nullptr && previous != RestoringTerminateHandler)
            {
                previous();
            }
            std::abort();
        }

        void AppendIf(std::string& target, const bool condition, const char* sequence)
        {
            if (condition)
            {
                target += sequence;
            }
        }

    } // namespace

    std::string BuildSessionPrologue(const TerminalSessionOptions& options)
    {
        std::string prologue;
        AppendIf(prologue, options.alternateScreen, "\x1b[?1049h");
        AppendIf(prologue, options.hideCursor, "\x1b[?25l");
        // 1000 is button events, 1002 adds motion while a button is held, 1006 switches the
        // encoding to SGR -- which is what lifts the 223-column limit of the original scheme and
        // makes release events distinguishable from presses.
        AppendIf(prologue, options.mouseReporting, "\x1b[?1000h\x1b[?1002h\x1b[?1006h");
        // Push rather than replace the mode. A shell, multiplexer or embedding application may
        // already have enabled its own flags; popping on exit restores that exact state instead
        // of assuming CNA was the only terminal application involved. Fifteen requests
        // disambiguation, event types, alternate keys and all-keys-as-CSI-u. The last flag is
        // essential for games: without it ordinary WASD presses remain plain UTF-8 and have no
        // release events even though report-event-types is enabled.
        AppendIf(prologue, options.kittyKeyboard, "\x1b[>15u");
        return prologue;
    }

    std::string BuildSessionEpilogue(const TerminalSessionOptions& options)
    {
        std::string epilogue;
        AppendIf(epilogue, options.kittyKeyboard, "\x1b[<u");
        AppendIf(epilogue, options.mouseReporting, "\x1b[?1006l\x1b[?1002l\x1b[?1000l");
        // Unconditional: a session that drew anything left an SGR state behind, and a shell
        // inheriting a stray background colour is the most visible way to get this wrong.
        epilogue += "\x1b[0m";
        AppendIf(epilogue, options.hideCursor, "\x1b[?25h");
        AppendIf(epilogue, options.alternateScreen, "\x1b[?1049l");
        return epilogue;
    }

    bool TerminalSession::IsActive() { return Record().active.load(); }

    TerminalSession::TerminalSession(const int outputDescriptor, const int inputDescriptor,
                                     const TerminalSessionOptions& options)
        : outputDescriptor_(outputDescriptor)
        , inputDescriptor_(inputDescriptor)
        , options_(options)
    {
        RestorationRecord& record = Record();
        if (record.active.load())
        {
            // Letting a second session start would overwrite the first's saved attributes with
            // the *raw* ones it installed, so the eventual restore would hand the user back a
            // terminal with no echo. Refusing is the only safe answer.
            throw PlatformException("TerminalSession",
                                    "a terminal session is already active in this process");
        }

        if (isatty(outputDescriptor_) == 0)
        {
            throw PlatformException("TerminalSession",
                                    "the output descriptor is not a terminal; refusing to write "
                                    "escape sequences into a pipe or a file");
        }

        const std::string epilogue = BuildSessionEpilogue(options_);
        if (epilogue.size() >= kMaxEpilogue)
        {
            throw PlatformException("TerminalSession", "restoration sequence too long to hold in "
                                                       "signal-safe storage");
        }

        if (options_.rawMode)
        {
            if (tcgetattr(inputDescriptor_, &savedAttributes_) != 0)
            {
                throw PlatformException("TerminalSession",
                                        "cannot read the terminal's current attributes");
            }
            savedAttributesValid_ = true;
        }

        // Fill the record completely before anything can run that reads it.
        record.outputDescriptor = outputDescriptor_;
        record.inputDescriptor = inputDescriptor_;
        record.savedAttributes = savedAttributes_;
        record.restoreAttributes = savedAttributesValid_;
        std::memcpy(record.epilogue, epilogue.data(), epilogue.size());
        record.epilogueLength = epilogue.size();

        struct sigaction restoring = {};
        restoring.sa_handler = &RestoringSignalHandler;
        sigemptyset(&restoring.sa_mask);
        // No SA_RESTART: a session's own read loop wants EINTR so it can notice the signal. And
        // no SA_RESETHAND -- the handler reinstates the previous disposition itself, which is
        // more precise than the default one SA_RESETHAND would leave.
        restoring.sa_flags = 0;
        for (std::size_t i = 0; i < kRestoringSignalCount; ++i)
        {
            sigaction(kRestoringSignals[i], &restoring, &record.previousActions[i]);
        }
        record.previousActionsValid = true;
        record.previousTerminate = std::set_terminate(&RestoringTerminateHandler);

        record.active.store(true);

        if (savedAttributesValid_)
        {
            termios raw = savedAttributes_;
            raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
            raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
            raw.c_cc[VMIN] = 0;   // a read may return with nothing
            raw.c_cc[VTIME] = 0;  // poll() owns the timing, not the line discipline
            tcsetattr(inputDescriptor_, TCSANOW, &raw);
        }

        const std::string prologue = BuildSessionPrologue(options_);
        if (!prologue.empty())
        {
            const ssize_t written = write(outputDescriptor_, prologue.data(), prologue.size());
            (void)written;
        }
    }

    TerminalSession::~TerminalSession()
    {
        RestoreNow();

        RestorationRecord& record = Record();
        if (record.previousActionsValid)
        {
            for (std::size_t i = 0; i < kRestoringSignalCount; ++i)
            {
                sigaction(kRestoringSignals[i], &record.previousActions[i], nullptr);
            }
            record.previousActionsValid = false;
        }
        if (record.previousTerminate != nullptr)
        {
            std::set_terminate(record.previousTerminate);
            record.previousTerminate = nullptr;
        }
    }

} // namespace CNA::Platform::Terminal
