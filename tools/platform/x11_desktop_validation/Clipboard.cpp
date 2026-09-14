// SPDX-License-Identifier: MS-PL
//
// Phase 11: the clipboard against clients that share no code with CNA.
//
//   * xclip and xsel -- ordinary X clients on the same X server;
//   * wl-copy / wl-paste -- NATIVE WAYLAND clients. On a GNOME desktop their clipboard is the
//     compositor's, and reaching an Xwayland client means going through mutter's own X11 <->
//     Wayland selection bridge: the path a copy from a GTK4 or Qt6 Wayland application takes.
//
// X11 has no clipboard storage: CNA serves its text for as long as it owns the selection AND its
// event pump runs. Every external read here therefore happens on a thread while the main thread
// keeps pumping, exactly as a game's frame loop would.
//
// The desktop's clipboard is overwritten; tools/platform/x11_desktop_validation's documentation
// says so, and the wrapper script used for real-desktop runs restores the previous text.

#include "Harness.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace CnaX11Validation {

    namespace {

        bool HaveTool(const char* name)
        {
            return std::system((std::string("command -v ") + name + " >/dev/null 2>&1").c_str()) == 0;
        }

        /// Runs a shell command and returns its standard output (and whether it exited 0).
        std::pair<bool, std::string> Capture(const std::string& command)
        {
            std::string output;
            FILE* pipe = popen(command.c_str(), "r");
            if (pipe == nullptr)
            {
                return {false, output};
            }
            std::array<char, 65536> buffer{};
            std::size_t read = 0;
            while ((read = std::fread(buffer.data(), 1, buffer.size(), pipe)) > 0)
            {
                output.append(buffer.data(), read);
            }
            const int status = pclose(pipe);
            return {WIFEXITED(status) && WEXITSTATUS(status) == 0, output};
        }

        /// Feeds @p text to a command's standard input (the command daemonises to serve it).
        bool Feed(const std::string& command, const std::string& text)
        {
            FILE* pipe = popen(command.c_str(), "w");
            if (pipe == nullptr)
            {
                return false;
            }
            const bool written = std::fwrite(text.data(), 1, text.size(), pipe) == text.size();
            return pclose(pipe) != -1 && written;
        }

        /// Reads with an external tool while CNA keeps serving the selection.
        std::string ReadExternally(Session& session, const std::string& command)
        {
            auto reader = std::async(std::launch::async, [command] { return Capture(command); });
            while (reader.wait_for(std::chrono::milliseconds(2)) != std::future_status::ready)
            {
                session.Poll();
            }
            return reader.get().second;
        }

        std::string Describe(const std::string& text)
        {
            if (text.size() <= 48)
            {
                return "\"" + text + "\"";
            }
            return std::to_string(text.size()) + " bytes";
        }

        std::string MakeLarge(const std::size_t size)
        {
            std::string text;
            text.reserve(size);
            const std::string unit = "CNA INCR \xC5\x99\xC3\xAD\xC5\xA1 0123456789abcdef\n";
            while (text.size() + unit.size() <= size)
            {
                text += unit;
            }
            while (text.size() < size)
            {
                text.push_back('.');
            }
            return text;
        }

    } // namespace

    int RunClipboard(const std::vector<std::string>& arguments)
    {
        const bool withWayland = !OptionFlag(arguments, "no-wayland");
        Session session;
        if (!session.Ok())
        {
            Fail("clipboard.session", session.Error());
            return 1;
        }
        IPlatformClipboard* clipboard = session.Platform().GetClipboard();
        if (clipboard == nullptr)
        {
            Fail("clipboard.service");
            return 1;
        }
        const bool xclip = HaveTool("xclip");
        const bool xsel = HaveTool("xsel");
        bool wayland = withWayland && HaveTool("wl-paste") && HaveTool("wl-copy") &&
                       std::getenv("WAYLAND_DISPLAY") != nullptr;
        std::string waylandNote = wayland ? "yes" : "no (not installed or no WAYLAND_DISPLAY)";
        if (wayland)
        {
            // wl-clipboard needs the compositor's data-control protocol or keyboard focus of its
            // own; where it has neither (GNOME's mutter, for one) it waits forever. A probe with
            // a deadline tells the two apart before any check depends on it.
            if (std::system("printf probe | timeout 5 wl-copy >/dev/null 2>&1") != 0)
            {
                wayland = false;
                waylandNote = "no (wl-copy could not take the compositor's clipboard within 5 s "
                              "-- this compositor offers wl-clipboard no data-control protocol)";
            }
        }
        Info(std::string("external peers: xclip ") + (xclip ? "yes" : "no") + ", xsel " +
             (xsel ? "yes" : "no") + ", native Wayland wl-clipboard " + waylandNote);

        const std::vector<std::pair<std::string, std::string>> payloads = {
            {"ascii", "CNA clipboard, plain ASCII"},
            {"czech", "P\xC5\x99\xC3\xADli\xC5\xA1 \xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD k\xC5\xAF\xC5\x88 "
                      "\xC3\xBAp\xC4\x9Bl \xC4\x8F\xC3\xA1\x62\x65lsk\xC3\xA9 \xC3\xB3\x64y"},
            {"emoji-and-scripts", "\xF0\x9F\x98\x80 \xCE\xA9\xCE\xBB \xD0\x96 \xE4\xB8\xAD\xE6\x96\x87"},
            {"incr-600k", MakeLarge(600 * 1024)},
            {"incr-4m", MakeLarge(4 * 1024 * 1024)},
        };

        struct Peer
        {
            const char* name;
            bool available;
            std::string read;
            std::string write;
        };
        const std::vector<Peer> peers = {
            // Every peer command runs under a deadline: an external tool that cannot reach the
            // clipboard must become a failed check, never a harness that hangs.
            {"xclip", xclip, "timeout 20 xclip -o -selection clipboard",
             "timeout 20 xclip -selection clipboard -i"},
            {"xsel", xsel, "timeout 20 xsel --clipboard --output",
             "timeout 20 xsel --clipboard --input"},
            {"wayland", wayland, "timeout 20 wl-paste --no-newline", "timeout 20 wl-copy"},
        };

        for (const Peer& peer : peers)
        {
            if (!peer.available)
            {
                Skip(std::string("clipboard.") + peer.name, "tool not available");
                continue;
            }
            for (const auto& [name, text] : payloads)
            {
                // CNA -> peer.
                clipboard->SetText(text);
                session.Poll();
                const std::string pasted = ReadExternally(session, peer.read);
                Check(pasted == text, std::string("clipboard.cna-to-") + peer.name + "." + name,
                      pasted == text ? Describe(text)
                                     : "got " + Describe(pasted) + ", wanted " + Describe(text));

                // peer -> CNA. CNA first takes the clipboard with a sentinel, so a read that
                // answered from CNA's own copy could never look like success -- the external
                // tool's daemon takes ownership asynchronously, after its command has returned.
                clipboard->SetText("CNA sentinel before " + name);
                session.Poll();
                Feed(peer.write, text);
                std::string received;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    session.Poll();
                    received = clipboard->GetText();
                    if (received == text)
                    {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                Check(received == text, std::string("clipboard.") + peer.name + "-to-cna." + name,
                      received == text ? Describe(text)
                                       : "got " + Describe(received) + ", wanted " + Describe(text));
            }
        }

        // Empty text: X11 can own a selection whose value is the empty string.
        if (xclip)
        {
            clipboard->SetText("");
            const std::string pasted =
                ReadExternally(session, "timeout 20 xclip -o -selection clipboard");
            Check(pasted.empty(), "clipboard.empty-text-served-as-empty", Describe(pasted));
        }

        // Losing ownership to another client.
        if (xclip)
        {
            clipboard->SetText("owned by CNA");
            session.Poll();
            Feed("timeout 20 xclip -selection clipboard -i", "taken by xclip");
            std::string now;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < deadline && now != "taken by xclip")
            {
                session.Poll();
                now = clipboard->GetText();
            }
            Check(now == "taken by xclip", "clipboard.ownership-loss-is-observed", Describe(now));
        }

        // A process that owned the clipboard exits. X11 keeps nothing; whether the desktop does
        // (a clipboard manager, or the compositor's own bridge) is the desktop's business, so it
        // is recorded rather than asserted.
        if (xclip)
        {
            const pid_t child = fork();
            if (child == 0)
            {
                {
                    Session owner;
                    if (owner.Ok() && owner.Platform().GetClipboard() != nullptr)
                    {
                        owner.Platform().GetClipboard()->SetText("owner exited");
                        owner.PumpFor(std::chrono::milliseconds(400));
                    }
                }
                ::_exit(0);
            }
            int status = 0;
            waitpid(child, &status, 0);
            session.PumpFor(std::chrono::milliseconds(300));
            const auto [ok, after] = Capture("timeout 3 xclip -o -selection clipboard 2>/dev/null");
            Info(std::string("after the owning process exited, the clipboard reads ") +
                 (ok ? Describe(after) : std::string("nothing (no owner)")) +
                 " -- X11 itself persists nothing; any value here was kept by the desktop");
            Pass("clipboard.owner-exit-does-not-crash-or-hang");
        }
        return Results().failed == 0 ? 0 : 1;
    }

} // namespace CnaX11Validation
