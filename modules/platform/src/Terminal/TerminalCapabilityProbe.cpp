// SPDX-License-Identifier: MS-PL

#include "TerminalCapabilityProbe.hpp"

#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace CNA::Platform::Terminal {

    namespace {

        /// Raw mode, restored on every exit path including an exception.
        ///
        /// A probe that left the user's shell without echo would be a hostile bug in a function
        /// whose entire job is to be safe to call.
        class ScopedRawMode
        {
        public:
            explicit ScopedRawMode(const int fd) : fd_(fd)
            {
                if (tcgetattr(fd_, &saved_) != 0)
                {
                    return;
                }
                entered_ = true;

                termios raw = saved_;
                raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
                raw.c_cc[VMIN] = 0;   // a read may return with nothing
                raw.c_cc[VTIME] = 0;  // poll() owns the timing
                tcsetattr(fd_, TCSANOW, &raw);
            }

            ~ScopedRawMode()
            {
                if (entered_)
                {
                    tcsetattr(fd_, TCSANOW, &saved_);
                }
            }

            [[nodiscard]] bool Entered() const { return entered_; }

            ScopedRawMode(const ScopedRawMode&) = delete;
            ScopedRawMode& operator=(const ScopedRawMode&) = delete;

        private:
            int fd_;
            termios saved_{};
            bool entered_ = false;
        };

        /// Writes a query and collects whatever comes back within the timeout.
        ///
        /// Queries go to stdin rather than stdout. Both refer to the same terminal when one is
        /// attached, so either would send them — but the reply arrives on stdin regardless, and
        /// writing to stdout would send the query into a file if output were ever redirected,
        /// leaving this waiting for an answer nobody was asked for.
        std::string Ask(const int fd, const std::string& query, const int timeoutMs)
        {
            if (write(fd, query.data(), query.size()) < 0)
            {
                return {};
            }

            std::string reply;
            char buffer[256];

            // Keep reading while data keeps arriving: a reply can be split across reads, and some
            // terminals answer one query with several sequences.
            for (;;)
            {
                pollfd waiting{};
                waiting.fd = fd;
                waiting.events = POLLIN;

                if (poll(&waiting, 1, reply.empty() ? timeoutMs : 40) <= 0)
                {
                    break;
                }

                const ssize_t count = read(fd, buffer, sizeof(buffer));
                if (count <= 0)
                {
                    break;
                }
                reply.append(buffer, static_cast<std::size_t>(count));

                if (reply.size() > 4096)
                {
                    break;  // a terminal echoing our own query back, or worse
                }
            }
            return reply;
        }

        /// The colour depth the environment claims. A starting point, not an answer.
        TerminalColourDepth ColourDepthFromEnvironment()
        {
            const char* colorterm = std::getenv("COLORTERM");
            if (colorterm != nullptr && (std::strstr(colorterm, "truecolor") != nullptr ||
                                         std::strstr(colorterm, "24bit") != nullptr))
            {
                return TerminalColourDepth::TrueColour;
            }

            const char* term = std::getenv("TERM");
            if (term == nullptr || std::strcmp(term, "dumb") == 0)
            {
                return TerminalColourDepth::Monochrome;
            }
            if (std::strstr(term, "direct") != nullptr)
            {
                return TerminalColourDepth::TrueColour;
            }
            if (std::strstr(term, "256color") != nullptr)
            {
                return TerminalColourDepth::Indexed256;
            }

            // A terminal that names no depth still almost certainly has the 16 ANSI colours; they
            // predate everything else here. Assuming 16 degrades gracefully, whereas assuming
            // none would throw away colour a terminal really has.
            return TerminalColourDepth::Ansi16;
        }

        /// Extracts the name from an XTVERSION reply (`DCS > | <name> ST`).
        std::string ParseTerminalName(const std::string& reply)
        {
            const std::size_t start = reply.find(">|");
            if (start == std::string::npos)
            {
                return {};
            }

            std::string name = reply.substr(start + 2);
            const std::size_t terminator = name.find('\x1b');
            if (terminator != std::string::npos)
            {
                name.erase(terminator);
            }
            return name;
        }

    } // namespace

    TerminalCapabilities DetectTerminalCapabilities(const int outputDescriptor,
                                                    const int inputDescriptor,
                                                    const int queryTimeoutMilliseconds)
    {
        TerminalCapabilities capabilities;
        capabilities.isTerminal = isatty(outputDescriptor) != 0;

        if (!capabilities.isTerminal)
        {
            // Not an error. Piped into a log, redirected to a file, captured by CI -- all normal.
            // Reporting it is what lets the platform refuse instead of writing escape sequences
            // into somebody's build output.
            return capabilities;
        }

        capabilities.colourDepth = ColourDepthFromEnvironment();
        capabilities.canQuery = isatty(inputDescriptor) != 0;

        if (!capabilities.canQuery)
        {
            // Output is a terminal but input is not, so nothing can be asked. The environment's
            // guess is all there is, and every queried capability stays false.
            return capabilities;
        }

        const ScopedRawMode raw(inputDescriptor);
        if (!raw.Entered())
        {
            return capabilities;
        }

        // Primary Device Attributes: the control. Every real terminal answers it, which is what
        // makes silence from the queries below a real negative rather than an ambiguous one.
        capabilities.respondedToDeviceAttributes =
            !Ask(inputDescriptor, "\x1b[c", queryTimeoutMilliseconds).empty();
        if (!capabilities.respondedToDeviceAttributes)
        {
            return capabilities;
        }

        const std::string kitty = Ask(inputDescriptor, "\x1b[?u", queryTimeoutMilliseconds);
        capabilities.hasKittyKeyboard =
            kitty.find("\x1b[?") != std::string::npos && !kitty.empty() && kitty.back() == 'u';

        capabilities.terminalName =
            ParseTerminalName(Ask(inputDescriptor, "\x1b[>0q", queryTimeoutMilliseconds));

        return capabilities;
    }

    TerminalCapabilities DetectTerminalCapabilities(const int queryTimeoutMilliseconds)
    {
        return DetectTerminalCapabilities(STDOUT_FILENO, STDIN_FILENO, queryTimeoutMilliseconds);
    }

    bool IsAttachedToTerminal() { return isatty(STDOUT_FILENO) != 0; }

    const std::string& ToString(const TerminalColourDepth depth)
    {
        static const std::string monochrome = "Monochrome";
        static const std::string ansi16 = "Ansi16";
        static const std::string indexed256 = "Indexed256";
        static const std::string trueColour = "TrueColour";

        // Exhaustive with no default arm: a depth added without a name here is a compiler
        // diagnostic rather than a silent fallback string.
        switch (depth)
        {
            case TerminalColourDepth::Monochrome: return monochrome;
            case TerminalColourDepth::Ansi16:     return ansi16;
            case TerminalColourDepth::Indexed256: return indexed256;
            case TerminalColourDepth::TrueColour: return trueColour;
        }
        return monochrome;
    }

} // namespace CNA::Platform::Terminal
