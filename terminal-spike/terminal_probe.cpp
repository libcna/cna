// SPDX-License-Identifier: MS-PL
//
// plans/plan_platform.md PLAT-129 — terminal capability spike.
//
// Proves, before any TerminalPlatform code is written, that a process can find out at runtime
// what the terminal it is attached to can actually do. Everything in Phase 10 keys off these
// answers: the colour ladder (PLAT-134) picks a rung from the colour depth, the keyboard path
// (PLAT-137 vs PLAT-138) is chosen by whether the Kitty protocol is present, and the whole
// platform must refuse cleanly when it is not attached to a terminal at all (PLAT-140).
//
// Deliberately standalone: no CNA headers, no SDL, no CMake integration. A spike proves the
// underlying mechanism works; it is not the implementation.
//
//   g++ -std=c++23 -O2 -o terminal_probe terminal_probe.cpp
//   ./terminal_probe
//
// The interesting part is the *querying*. Environment variables are guesses — TERM is frequently
// wrong, inherited over ssh, or set to something conservative by a multiplexer. A real answer
// comes from asking the terminal and reading its reply, which needs raw mode and a timeout,
// because a terminal that does not understand a query simply says nothing back.

#include <termios.h>
#include <unistd.h>
#include <poll.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// ---------------------------------------------------------------------------------------------
// Raw mode, restored no matter how we leave.
//
// Every early return in this file goes through the destructor. A spike that leaves the developer's
// shell without echo is exactly the hostile failure PLAT-131 exists to prevent, and it would be
// absurd to demonstrate that risk while probing for it.
// ---------------------------------------------------------------------------------------------
class RawMode
{
public:
    explicit RawMode(const int fd) : fd_(fd)
    {
        if (tcgetattr(fd_, &saved_) != 0)
        {
            return;
        }
        entered_ = true;

        termios raw = saved_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;   // a read may return with nothing
        raw.c_cc[VTIME] = 0;  // the poll() below owns the timing
        tcsetattr(fd_, TCSANOW, &raw);
    }

    ~RawMode()
    {
        if (entered_)
        {
            tcsetattr(fd_, TCSANOW, &saved_);
        }
    }

    [[nodiscard]] bool Entered() const { return entered_; }

    RawMode(const RawMode&) = delete;
    RawMode& operator=(const RawMode&) = delete;

private:
    int fd_;
    termios saved_{};
    bool entered_ = false;
};

/// Writes a query and collects whatever the terminal says back within the timeout.
///
/// The timeout is the whole design. There is no way to ask a terminal "do you support X" and get
/// "no" — an unsupported query produces silence, so absence of a reply *is* the negative answer.
/// That makes the timeout a correctness parameter: too short and a capable terminal over a slow
/// ssh link is misread as incapable.
std::string Ask(const int fd, const std::string& query, const int timeoutMs)
{
    if (write(fd, query.data(), query.size()) < 0)
    {
        return {};
    }

    std::string reply;
    char buffer[256];

    // Keep reading while data keeps arriving: a reply can be split across reads, and some
    // terminals answer a single query with several sequences.
    for (;;)
    {
        pollfd waiting{};
        waiting.fd = fd;
        waiting.events = POLLIN;

        const int ready = poll(&waiting, 1, reply.empty() ? timeoutMs : 40);
        if (ready <= 0)
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
            break;  // a terminal echoing our own query back at us, or worse
        }
    }
    return reply;
}

std::string Printable(const std::string& raw)
{
    std::string out;
    for (const unsigned char c : raw)
    {
        if (c == 0x1B)
        {
            out += "\\e";
        }
        else if (c >= 0x20 && c < 0x7F)
        {
            out += static_cast<char>(c);
        }
        else
        {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "\\x%02X", c);
            out += hex;
        }
    }
    return out;
}

const char* Env(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr ? value : "(unset)";
}

/// What the environment *claims*, which is a starting point and not an answer.
const char* GuessColourDepthFromEnvironment()
{
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm != nullptr &&
        (std::strstr(colorterm, "truecolor") != nullptr || std::strstr(colorterm, "24bit") != nullptr))
    {
        return "truecolor (COLORTERM)";
    }

    const char* term = std::getenv("TERM");
    if (term == nullptr)
    {
        return "unknown (no TERM)";
    }
    if (std::strstr(term, "256color") != nullptr)
    {
        return "256 (TERM)";
    }
    if (std::strcmp(term, "dumb") == 0)
    {
        return "none (TERM=dumb)";
    }
    return "16 assumed (TERM names no depth)";
}

} // namespace

int main()
{
    std::printf("PLAT-129 terminal capability spike\n");
    std::printf("==================================\n\n");

    const bool stdoutIsTty = isatty(STDOUT_FILENO) != 0;
    const bool stdinIsTty = isatty(STDIN_FILENO) != 0;

    std::printf("stdout is a tty : %s\n", stdoutIsTty ? "yes" : "no");
    std::printf("stdin  is a tty : %s\n", stdinIsTty ? "yes" : "no");
    std::printf("TERM            : %s\n", Env("TERM"));
    std::printf("COLORTERM       : %s\n", Env("COLORTERM"));
    std::printf("TERM_PROGRAM    : %s\n", Env("TERM_PROGRAM"));
    std::printf("colour (env)    : %s\n", GuessColourDepthFromEnvironment());
    std::printf("\n");

    if (!stdoutIsTty)
    {
        // The finding that matters for PLAT-140: this is a normal way to be run — piped into a
        // log, redirected to a file, captured by CI. A terminal platform must detect it and
        // refuse, not emit escape sequences into somebody's build log.
        std::printf("VERDICT: not attached to a terminal.\n");
        std::printf("A TerminalPlatform must refuse here rather than write escapes to a pipe.\n");
        return 0;
    }

    if (!stdinIsTty)
    {
        std::printf("VERDICT: output is a terminal but input is not; queries are impossible.\n");
        std::printf("Detection must fall back to the environment alone in this case.\n");
        return 0;
    }

    const RawMode raw(STDIN_FILENO);
    if (!raw.Entered())
    {
        std::printf("VERDICT: could not enter raw mode; no query is possible.\n");
        return 1;
    }

    // Primary Device Attributes. Every real terminal answers this, so it doubles as the control:
    // if DA1 gets no reply, silence from the other queries means nothing.
    const std::string da1 = Ask(STDIN_FILENO, "\x1b[c", 250);
    std::printf("DA1  (\\e[c)     : %s\n", da1.empty() ? "(no reply)" : Printable(da1).c_str());

    // Kitty keyboard protocol. A reply of CSI ? <flags> u means the terminal understands it, and
    // that is the difference between exact key-release events (PLAT-137) and synthesising them
    // from a timeout (PLAT-138) -- which is the single biggest fidelity question in Phase 10.
    const std::string kitty = Ask(STDIN_FILENO, "\x1b[?u", 250);
    const bool hasKitty = kitty.find("\x1b[?") != std::string::npos && kitty.back() == 'u';
    std::printf("Kitty (\\e[?u)   : %s\n", kitty.empty() ? "(no reply)" : Printable(kitty).c_str());

    // XTVERSION: a self-identifying name, useful for diagnostics and for working around known
    // emulator quirks. Optional -- silence here is not a problem.
    const std::string version = Ask(STDIN_FILENO, "\x1b[>0q", 250);
    std::printf("XTVERSION       : %s\n", version.empty() ? "(no reply)" : Printable(version).c_str());

    std::printf("\n");
    std::printf("VERDICT\n");
    std::printf("  terminal present : %s\n", da1.empty() ? "unconfirmed (DA1 silent)" : "yes");
    std::printf("  keyboard path    : %s\n",
                hasKitty ? "PLAT-137 exact (Kitty protocol)"
                         : "PLAT-138 synthetic key-up (approximation)");
    std::printf("  exactKeyboardState capability should be reported as: %s\n",
                hasKitty ? "true" : "false");
    std::printf("  colour ladder rung (env-derived): %s\n", GuessColourDepthFromEnvironment());
    std::printf("\nSGR mouse (?1006) has no query; it is enabled and assumed, per PLAT-139.\n");

    return 0;
}
