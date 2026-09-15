// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0169/X11-0170: a session bus of a test's own, for the tests of the desktop
// services the X11 and Wayland platforms ask for on the session bus (shared since
// plans/plan_wayland.md WAYLAND-0011). Never the bus of the desktop the tests
// run on: its configuration names no service it could start, so nothing real can appear on it.
#pragma once

#if defined(CNA_PLATFORM_HAVE_DBUS)

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace CNA::Platform::Freedesktop::Testing {

/// A dbus-daemon of the test's own: a session bus with no service it could start.
class PrivateBus
{
public:
    PrivateBus()
    {
        directory_ = std::filesystem::temp_directory_path() /
                     ("cna-bus-" + std::to_string(::getpid()) + "-" + std::to_string(++counter_));
        std::filesystem::create_directories(directory_);
        const std::filesystem::path config = directory_ / "bus.conf";
        std::ofstream(config) << "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
                                 " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                                 "<busconfig><type>session</type>"
                                 "<listen>unix:dir=" << directory_.string() << "</listen>"
                                 "<auth>EXTERNAL</auth>"
                                 "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
                                 "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n";
        int pipeEnds[2] = {-1, -1};
        if (::pipe2(pipeEnds, O_CLOEXEC) != 0)
        {
            return;
        }
        // The daemon writes its address to this descriptor; the child gets a copy without CLOEXEC.
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], 3);
        const std::string configArgument = "--config-file=" + config.string();
        const char* const arguments[] = {"dbus-daemon", configArgument.c_str(), "--nofork", "--print-address=3",
                                         nullptr};
        const int spawned = posix_spawnp(&pid_, "dbus-daemon", &actions, nullptr, const_cast<char* const*>(arguments),
                                         environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(pipeEnds[1]);
        if (spawned != 0)
        {
            pid_ = -1;
            ::close(pipeEnds[0]);
            return;
        }
        std::string line;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (line.find('\n') == std::string::npos && std::chrono::steady_clock::now() < deadline)
        {
            pollfd waiting{pipeEnds[0], POLLIN, 0};
            if (::poll(&waiting, 1, 100) <= 0) { continue; }
            char buffer[256];
            const ssize_t got = ::read(pipeEnds[0], buffer, sizeof(buffer));
            if (got <= 0) { break; }
            line.append(buffer, static_cast<std::size_t>(got));
        }
        ::close(pipeEnds[0]);
        if (const std::size_t end = line.find('\n'); end != std::string::npos)
        {
            address_ = line.substr(0, end);
        }
    }

    ~PrivateBus()
    {
        Stop();
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    PrivateBus(const PrivateBus&) = delete;
    PrivateBus& operator=(const PrivateBus&) = delete;

    [[nodiscard]] const std::string& Address() const { return address_; }

    void Stop()
    {
        if (pid_ > 0)
        {
            ::kill(pid_, SIGTERM);
            int status = 0;
            ::waitpid(pid_, &status, 0);
            pid_ = -1;
        }
    }

private:
    static inline int counter_ = 0;  // Several buses in one process never share a directory.
    std::filesystem::path directory_;
    pid_t pid_ = -1;
    std::string address_;
};

} // namespace CNA::Platform::Freedesktop::Testing

#endif // CNA_PLATFORM_HAVE_DBUS
