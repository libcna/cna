// SPDX-License-Identifier: MS-PL

#include "X11ModeGuardian.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

namespace CNA::Platform::X11 {

    namespace {

        // --- the wire -------------------------------------------------------------------------
        //
        // The connection is opened little-endian ('l' in the setup request), so every field below
        // is written and read little-endian regardless of the host. Request layouts are the
        // RandR 1.2 protocol's (randrproto.h); the opcodes are its X_RR* minor numbers.

        constexpr std::uint8_t kGrabServer = 36;
        constexpr std::uint8_t kUngrabServer = 37;
        constexpr std::uint8_t kGetGeometry = 14;
        constexpr std::uint8_t kRandrQueryVersion = 0;
        constexpr std::uint8_t kRandrSetScreenSize = 7;
        constexpr std::uint8_t kRandrGetCrtcInfo = 20;
        constexpr std::uint8_t kRandrSetCrtcConfig = 21;
        constexpr std::uint8_t kRandrGetScreenResourcesCurrent = 25;

        /// How long any one read may wait. A server that is wedged -- grabbed by another client
        /// that died holding the grab, say -- must not keep the guardian alive forever.
        constexpr int kReadTimeoutMilliseconds = 3000;

        /// The most CRTCs a restore looks at. A GPU with more than this is not a machine this
        /// code has met; the rest are then treated as absent, which can only make the screen
        /// plan more conservative, never invalid -- the server still refuses an invalid one.
        constexpr std::size_t kMaxCrtcs = 32;

        void Put16(unsigned char* at, const std::uint16_t value) noexcept
        {
            at[0] = static_cast<unsigned char>(value & 0xFFu);
            at[1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
        }

        void Put32(unsigned char* at, const std::uint32_t value) noexcept
        {
            for (int index = 0; index < 4; ++index)
            {
                at[index] = static_cast<unsigned char>((value >> (8 * index)) & 0xFFu);
            }
        }

        std::uint16_t Get16(const unsigned char* at) noexcept
        {
            return static_cast<std::uint16_t>(at[0] | (at[1] << 8));
        }

        std::uint32_t Get32(const unsigned char* at) noexcept
        {
            return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8) |
                   (static_cast<std::uint32_t>(at[2]) << 16) |
                   (static_cast<std::uint32_t>(at[3]) << 24);
        }

        std::size_t Pad4(const std::size_t length) noexcept
        {
            return (4 - (length % 4)) % 4;
        }

        struct RawConnection
        {
            int fd = -1;
            /// The sequence number of the last request sent; the server numbers them from 1.
            std::uint16_t sequence = 0;
            unsigned char* scratch = nullptr;
            std::size_t scratchSize = 0;
        };

        bool WriteFully(const int fd, const unsigned char* data, std::size_t length) noexcept
        {
            while (length > 0)
            {
                // send() rather than write(): MSG_NOSIGNAL turns a server that went away into
                // EPIPE instead of a SIGPIPE.
                const ssize_t written = ::send(fd, data, length, MSG_NOSIGNAL);
                if (written < 0)
                {
                    if (errno == EINTR) { continue; }
                    return false;
                }
                data += written;
                length -= static_cast<std::size_t>(written);
            }
            return true;
        }

        bool ReadFully(const int fd, unsigned char* data, std::size_t length) noexcept
        {
            while (length > 0)
            {
                pollfd readable{fd, POLLIN, 0};
                const int ready = ::poll(&readable, 1, kReadTimeoutMilliseconds);
                if (ready < 0 && errno == EINTR) { continue; }
                if (ready <= 0) { return false; }
                const ssize_t got = ::read(fd, data, length);
                if (got < 0)
                {
                    if (errno == EINTR) { continue; }
                    return false;
                }
                if (got == 0) { return false; }
                data += got;
                length -= static_cast<std::size_t>(got);
            }
            return true;
        }

        /// Reads and throws away bytes, a buffer-full at a time.
        bool Discard(RawConnection& connection, std::size_t length) noexcept
        {
            while (length > 0)
            {
                const std::size_t chunk = std::min(length, connection.scratchSize);
                if (!ReadFully(connection.fd, connection.scratch, chunk)) { return false; }
                length -= chunk;
            }
            return true;
        }

        bool Send(RawConnection& connection, const unsigned char* request,
                  const std::size_t length) noexcept
        {
            ++connection.sequence;
            return WriteFully(connection.fd, request, length);
        }

        /// Waits for the reply to the last request sent, leaving it at the start of the scratch
        /// buffer. Errors for EARLIER requests -- the ones without replies -- are skipped; an
        /// error for this one is a failure, because no reply will follow it.
        ///
        /// @return The reply's length in the buffer (its head, when it was larger), or 0.
        std::size_t AwaitReply(RawConnection& connection) noexcept
        {
            const std::uint16_t awaited = connection.sequence;
            for (;;)
            {
                if (!ReadFully(connection.fd, connection.scratch, 32)) { return 0; }
                const unsigned char kind = connection.scratch[0];
                const std::uint16_t sequence = Get16(connection.scratch + 2);
                if (kind == 1)
                {
                    const std::size_t extra = static_cast<std::size_t>(Get32(connection.scratch + 4)) * 4;
                    const std::size_t kept = std::min(extra, connection.scratchSize - 32);
                    if (!ReadFully(connection.fd, connection.scratch + 32, kept)) { return 0; }
                    if (kept < extra)
                    {
                        // The rest does not fit. Every field this code reads sits at the head of
                        // its reply -- the CRTC list comes first in a GetScreenResources reply,
                        // the only kind that grows large -- so the tail is read past into a
                        // throwaway buffer rather than left in the stream to be misread as the
                        // next packet.
                        unsigned char tail[256];
                        std::size_t remaining = extra - kept;
                        while (remaining > 0)
                        {
                            const std::size_t chunk = std::min(remaining, sizeof(tail));
                            if (!ReadFully(connection.fd, tail, chunk)) { return 0; }
                            remaining -= chunk;
                        }
                    }
                    if (sequence == awaited) { return 32 + kept; }
                    continue;
                }
                if (kind == 0)
                {
                    if (sequence == awaited) { return 0; }
                    continue;
                }
                // An event. Nothing was selected on this connection, but a GenericEvent is the one
                // kind longer than 32 bytes, and leaving its tail in the stream would desync it.
                if ((kind & 0x7F) == 35)
                {
                    unsigned char tail[256];
                    std::size_t remaining = static_cast<std::size_t>(Get32(connection.scratch + 4)) * 4;
                    while (remaining > 0)
                    {
                        const std::size_t chunk = std::min(remaining, sizeof(tail));
                        if (!ReadFully(connection.fd, tail, chunk)) { return 0; }
                        remaining -= chunk;
                    }
                }
            }
        }

        bool Connect(RawConnection& connection, const X11ModeRestorePlan& plan) noexcept
        {
            connection.fd = ::socket(plan.address.ss_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (connection.fd < 0) { return false; }
            // A connect to a TCP display whose host is gone would otherwise wait for the
            // kernel's own timeout, which is minutes.
            timeval timeout{kReadTimeoutMilliseconds / 1000, 0};
            ::setsockopt(connection.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            if (::connect(connection.fd, reinterpret_cast<const sockaddr*>(&plan.address),
                          plan.addressLength) != 0)
            {
                return false;
            }
            if (!WriteFully(connection.fd, plan.setup, plan.setupLength)) { return false; }
            if (!ReadFully(connection.fd, connection.scratch, 8)) { return false; }
            // 1 is Success; 0 is Failed and 2 is Authenticate, neither of which this client can
            // answer.
            if (connection.scratch[0] != 1) { return false; }
            // Nothing in the setup reply is needed: the root and the RandR opcode came from the
            // application's own connection, and this client creates no resources.
            return Discard(connection, static_cast<std::size_t>(Get16(connection.scratch + 6)) * 4);
        }

        bool SendSimple(RawConnection& connection, const std::uint8_t opcode) noexcept
        {
            unsigned char request[4] = {};
            request[0] = opcode;
            Put16(request + 2, 1);
            return Send(connection, request, sizeof(request));
        }

        bool SendSetScreenSize(RawConnection& connection, const X11ModeRestorePlan& plan,
                               const int width, const int height) noexcept
        {
            unsigned char request[20] = {};
            request[0] = plan.randrOpcode;
            request[1] = kRandrSetScreenSize;
            Put16(request + 2, 5);
            Put32(request + 4, plan.root);
            Put16(request + 8, static_cast<std::uint16_t>(width));
            Put16(request + 10, static_cast<std::uint16_t>(height));
            Put32(request + 12, MillimetresFor(width, plan.screenWidth, plan.screenWidthMm));
            Put32(request + 16, MillimetresFor(height, plan.screenHeight, plan.screenHeightMm));
            return Send(connection, request, sizeof(request));
        }

        /// Reads the root's size. Also the final round trip: its reply proves the server has
        /// processed everything sent before it.
        bool QueryRootSize(RawConnection& connection, const X11ModeRestorePlan& plan, int& width,
                           int& height) noexcept
        {
            unsigned char request[8] = {};
            request[0] = kGetGeometry;
            Put16(request + 2, 2);
            Put32(request + 4, plan.root);
            if (!Send(connection, request, sizeof(request))) { return false; }
            if (AwaitReply(connection) < 32) { return false; }
            width = Get16(connection.scratch + 16);
            height = Get16(connection.scratch + 18);
            return true;
        }

        struct RawCrtc
        {
            std::uint32_t id = 0;
            X11Rect rect;
            std::uint32_t mode = 0;
        };

        X11ModeRestoreResult RestoreOver(RawConnection& connection,
                                         const X11ModeRestorePlan& plan) noexcept
        {
            // RandR answers a client by the version that client claimed; claiming none is legal
            // but relies on the server's defaults for requests this code depends on.
            {
                unsigned char request[12] = {};
                request[0] = plan.randrOpcode;
                request[1] = kRandrQueryVersion;
                Put16(request + 2, 3);
                Put32(request + 4, 1);
                Put32(request + 8, 2);
                if (!Send(connection, request, sizeof(request)) || AwaitReply(connection) < 32)
                {
                    return X11ModeRestoreResult::Failed;
                }
            }

            // Grabbed from the first read to the last write, so no other client -- a desktop's
            // own display daemon reacting to the application's death, typically -- can change
            // the configuration between this reading it and acting on it.
            if (!SendSimple(connection, kGrabServer)) { return X11ModeRestoreResult::Failed; }

            std::uint32_t configTimestamp = 0;
            std::uint32_t crtcIds[kMaxCrtcs] = {};
            std::size_t crtcCount = 0;
            {
                unsigned char request[8] = {};
                request[0] = plan.randrOpcode;
                request[1] = kRandrGetScreenResourcesCurrent;
                Put16(request + 2, 2);
                Put32(request + 4, plan.root);
                if (!Send(connection, request, sizeof(request))) { return X11ModeRestoreResult::Failed; }
                const std::size_t length = AwaitReply(connection);
                if (length < 32) { return X11ModeRestoreResult::Failed; }
                configTimestamp = Get32(connection.scratch + 12);
                const std::size_t reported = Get16(connection.scratch + 16);
                crtcCount = std::min({reported, kMaxCrtcs, (length - 32) / 4});
                for (std::size_t index = 0; index < crtcCount; ++index)
                {
                    crtcIds[index] = Get32(connection.scratch + 32 + index * 4);
                }
            }

            RawCrtc crtcs[kMaxCrtcs];
            for (std::size_t index = 0; index < crtcCount; ++index)
            {
                unsigned char request[12] = {};
                request[0] = plan.randrOpcode;
                request[1] = kRandrGetCrtcInfo;
                Put16(request + 2, 3);
                Put32(request + 4, crtcIds[index]);
                Put32(request + 8, configTimestamp);
                if (!Send(connection, request, sizeof(request)) || AwaitReply(connection) < 32)
                {
                    return X11ModeRestoreResult::Failed;
                }
                crtcs[index].id = crtcIds[index];
                crtcs[index].rect.x = static_cast<std::int16_t>(Get16(connection.scratch + 12));
                crtcs[index].rect.y = static_cast<std::int16_t>(Get16(connection.scratch + 14));
                crtcs[index].rect.width = Get16(connection.scratch + 16);
                crtcs[index].rect.height = Get16(connection.scratch + 18);
                crtcs[index].mode = Get32(connection.scratch + 20);
            }

            const RawCrtc* target = nullptr;
            X11Rect others[kMaxCrtcs];
            std::size_t otherCount = 0;
            for (std::size_t index = 0; index < crtcCount; ++index)
            {
                if (crtcs[index].id == plan.crtc)
                {
                    target = &crtcs[index];
                }
                else if (crtcs[index].mode != 0)
                {
                    others[otherCount++] = crtcs[index].rect;
                }
            }
            if (target == nullptr || target->mode != plan.appliedMode)
            {
                // Someone else has set this CRTC since -- or it is gone. Either way it is no
                // longer the application's to put back.
                SendSimple(connection, kUngrabServer);
                return X11ModeRestoreResult::LeftAlone;
            }

            int rootWidth = 0;
            int rootHeight = 0;
            if (!QueryRootSize(connection, plan, rootWidth, rootHeight))
            {
                return X11ModeRestoreResult::Failed;
            }
            const X11Rect original{plan.originalX, plan.originalY, plan.originalWidth,
                                   plan.originalHeight};
            const X11ScreenPlan screen =
                PlanScreenSizes(rootWidth, rootHeight, others, otherCount, original,
                                plan.screenWidth, plan.screenHeight, plan.minimumWidth,
                                plan.minimumHeight, plan.maximumWidth, plan.maximumHeight);

            if ((screen.growWidth != rootWidth || screen.growHeight != rootHeight) &&
                !SendSetScreenSize(connection, plan, screen.growWidth, screen.growHeight))
            {
                return X11ModeRestoreResult::Failed;
            }

            unsigned char request[28 + 4 * X11ModeRestorePlan::kMaxOutputs] = {};
            const std::size_t outputCount =
                std::min<std::size_t>(plan.outputCount, X11ModeRestorePlan::kMaxOutputs);
            request[0] = plan.randrOpcode;
            request[1] = kRandrSetCrtcConfig;
            Put16(request + 2, static_cast<std::uint16_t>(7 + outputCount));
            Put32(request + 4, plan.crtc);
            Put32(request + 8, 0); // CurrentTime
            Put32(request + 12, configTimestamp);
            Put16(request + 16, static_cast<std::uint16_t>(plan.originalX));
            Put16(request + 18, static_cast<std::uint16_t>(plan.originalY));
            Put32(request + 20, plan.originalMode);
            Put16(request + 24, plan.originalRotation);
            for (std::size_t index = 0; index < outputCount; ++index)
            {
                Put32(request + 28 + index * 4, plan.outputs[index]);
            }
            if (!Send(connection, request, 28 + outputCount * 4) || AwaitReply(connection) < 32 ||
                connection.scratch[1] != 0)
            {
                // Refused. A screen grown for it is shrunk back, so what is left is the
                // application's mode on the screen it had, not a half-restore.
                if (screen.growWidth != rootWidth || screen.growHeight != rootHeight)
                {
                    SendSetScreenSize(connection, plan, rootWidth, rootHeight);
                }
                SendSimple(connection, kUngrabServer);
                return X11ModeRestoreResult::Failed;
            }

            if (screen.finalWidth != screen.growWidth || screen.finalHeight != screen.growHeight)
            {
                SendSetScreenSize(connection, plan, screen.finalWidth, screen.finalHeight);
            }
            SendSimple(connection, kUngrabServer);
            int ignoredWidth = 0;
            int ignoredHeight = 0;
            QueryRootSize(connection, plan, ignoredWidth, ignoredHeight);
            return X11ModeRestoreResult::Restored;
        }

        void CloseEverythingFrom(const int first) noexcept
        {
#if defined(__linux__) && defined(SYS_close_range)
            if (::syscall(SYS_close_range, static_cast<unsigned>(first), ~0u, 0u) == 0)
            {
                return;
            }
#endif
            const long limit = ::sysconf(_SC_OPEN_MAX);
            const int last = limit > 0 && limit < 1'048'576 ? static_cast<int>(limit) : 65'536;
            for (int fd = first; fd < last; ++fd)
            {
                ::close(fd);
            }
        }

        /// The guardian itself. Only async-signal-safe calls from here on: this is a fork of a
        /// process whose other threads may have held any lock at the moment of the fork.
        [[noreturn]] void RunGuardian(int socket, X11ModeRestorePlan plan, unsigned char* scratch,
                                      const std::size_t scratchSize) noexcept
        {
            // Out of the terminal's session: a Ctrl+C, a hang-up or a job-control stop sent to the
            // game's process group must not reach the one process that repairs the display.
            ::setsid();
            struct sigaction ignore{};
            ignore.sa_handler = SIG_IGN;
            for (const int number : {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE, SIGTSTP, SIGTTIN,
                                     SIGTTOU, SIGUSR1, SIGUSR2, SIGALRM})
            {
                ::sigaction(number, &ignore, nullptr);
            }
            sigset_t none;
            sigemptyset(&none);
            ::sigprocmask(SIG_SETMASK, &none, nullptr);
#if defined(__linux__)
            ::prctl(PR_SET_NAME, "cna-x11-mode", 0, 0, 0);
#endif

            // Nothing of the application's stays open: an inherited pipe would keep someone
            // else's end-of-file from arriving, an inherited device node would keep it busy.
            const int devNull = ::open("/dev/null", O_RDWR);
            if (devNull >= 0)
            {
                ::dup2(devNull, 0);
                ::dup2(devNull, 1);
                ::dup2(devNull, 2);
            }
            if (socket != 3)
            {
                ::dup2(socket, 3);
                socket = 3;
            }
            CloseEverythingFrom(4);

            for (;;)
            {
                unsigned char message[5] = {};
                std::size_t got = 0;
                bool ended = false;
                while (got < sizeof(message))
                {
                    const ssize_t count = ::read(socket, message + got, sizeof(message) - got);
                    if (count < 0 && errno == EINTR) { continue; }
                    if (count <= 0)
                    {
                        ended = true;
                        break;
                    }
                    got += static_cast<std::size_t>(count);
                }
                if (ended)
                {
                    // The application is gone -- however it went.
                    (void) RestoreDisplayModeRaw(plan, scratch, scratchSize);
                    ::_exit(0);
                }
                if (message[0] == 'D')
                {
                    ::_exit(0);
                }
                if (message[0] == 'M')
                {
                    plan.appliedMode = Get32(message + 1);
                }
            }
        }

    } // namespace

    X11ScreenPlan PlanScreenSizes(const int currentWidth, const int currentHeight,
                                  const X11Rect* others, const std::size_t otherCount,
                                  const X11Rect& changed, const int preferredWidth,
                                  const int preferredHeight, const int minimumWidth,
                                  const int minimumHeight, const int maximumWidth,
                                  const int maximumHeight) noexcept
    {
        int right = changed.x + changed.width;
        int bottom = changed.y + changed.height;
        for (std::size_t index = 0; index < otherCount; ++index)
        {
            right = std::max(right, others[index].x + others[index].width);
            bottom = std::max(bottom, others[index].y + others[index].height);
        }
        int finalWidth = std::max(right, preferredWidth);
        int finalHeight = std::max(bottom, preferredHeight);
        if (minimumWidth > 0) { finalWidth = std::max(finalWidth, minimumWidth); }
        if (minimumHeight > 0) { finalHeight = std::max(finalHeight, minimumHeight); }
        if (maximumWidth > 0) { finalWidth = std::min(finalWidth, maximumWidth); }
        if (maximumHeight > 0) { finalHeight = std::min(finalHeight, maximumHeight); }

        X11ScreenPlan plan;
        plan.growWidth = std::max(currentWidth, finalWidth);
        plan.growHeight = std::max(currentHeight, finalHeight);
        plan.finalWidth = finalWidth;
        plan.finalHeight = finalHeight;
        return plan;
    }

    std::uint32_t MillimetresFor(const int pixels, const int referencePixels,
                                 const std::uint32_t referenceMillimetres) noexcept
    {
        double millimetres = 0.0;
        if (referencePixels > 0 && referenceMillimetres > 0)
        {
            millimetres = static_cast<double>(pixels) * referenceMillimetres / referencePixels;
        }
        else
        {
            // No density to keep: 96 DPI, the X server's own default.
            millimetres = static_cast<double>(pixels) * 25.4 / 96.0;
        }
        const auto rounded = static_cast<std::uint32_t>(millimetres + 0.5);
        return rounded > 0 ? rounded : 1u;
    }

    std::size_t EncodeConnectionSetup(unsigned char* output, const std::size_t capacity,
                                      const char* authName, const std::size_t authNameLength,
                                      const unsigned char* authData,
                                      const std::size_t authDataLength) noexcept
    {
        const std::size_t nameLength = authName != nullptr ? authNameLength : 0;
        const std::size_t dataLength = authData != nullptr ? authDataLength : 0;
        if (nameLength > 0xFFFF || dataLength > 0xFFFF)
        {
            return 0;
        }
        const std::size_t length =
            12 + nameLength + Pad4(nameLength) + dataLength + Pad4(dataLength);
        if (length > capacity)
        {
            return 0;
        }
        std::memset(output, 0, length);
        output[0] = 'l';
        Put16(output + 2, 11);
        Put16(output + 4, 0);
        Put16(output + 6, static_cast<std::uint16_t>(nameLength));
        Put16(output + 8, static_cast<std::uint16_t>(dataLength));
        if (nameLength > 0)
        {
            std::memcpy(output + 12, authName, nameLength);
        }
        if (dataLength > 0)
        {
            std::memcpy(output + 12 + nameLength + Pad4(nameLength), authData, dataLength);
        }
        return length;
    }

    X11ModeRestoreResult RestoreDisplayModeRaw(const X11ModeRestorePlan& plan,
                                               unsigned char* scratch,
                                               const std::size_t scratchSize) noexcept
    {
        if (scratch == nullptr || scratchSize < 1024 || plan.addressLength == 0 ||
            plan.setupLength == 0)
        {
            return X11ModeRestoreResult::Failed;
        }
        RawConnection connection;
        connection.scratch = scratch;
        connection.scratchSize = scratchSize;
        X11ModeRestoreResult result = X11ModeRestoreResult::Failed;
        if (Connect(connection, plan))
        {
            result = RestoreOver(connection, plan);
        }
        if (connection.fd >= 0)
        {
            // Closing releases a grab left behind by a failure part-way through.
            ::close(connection.fd);
        }
        return result;
    }

    std::unique_ptr<X11ModeGuardian> X11ModeGuardian::Start(const X11ModeRestorePlan& plan)
    {
        // A socket pair rather than a pipe: a write to a guardian that has died must be EPIPE,
        // and only send(MSG_NOSIGNAL) can promise that without touching the process's SIGPIPE.
        // Close-on-exec, so a program the game execs does not hold the guardian's end-of-file
        // hostage for as long as it runs.
        int sockets[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0)
        {
            return nullptr;
        }

        // Allocated before the fork, because the child may not allocate.
        constexpr std::size_t kScratchSize = 64 * 1024;
        std::unique_ptr<unsigned char[]> scratch(new unsigned char[kScratchSize]);

        const pid_t pid = ::fork();
        if (pid < 0)
        {
            ::close(sockets[0]);
            ::close(sockets[1]);
            return nullptr;
        }
        if (pid == 0)
        {
            ::close(sockets[0]);
            RunGuardian(sockets[1], plan, scratch.get(), kScratchSize);
        }
        ::close(sockets[1]);
        return std::unique_ptr<X11ModeGuardian>(new X11ModeGuardian(sockets[0], pid));
    }

    X11ModeGuardian::~X11ModeGuardian()
    {
        if (socket_ >= 0)
        {
            const unsigned char disarm[5] = {'D', 0, 0, 0, 0};
            (void) ::send(socket_, disarm, sizeof(disarm), MSG_NOSIGNAL);
            ::close(socket_);
        }
        if (pid_ > 0)
        {
            // Reaped here so it does not linger as a zombie for the rest of the game. It exits
            // as soon as it reads the disarm; the bound only matters if something stopped it,
            // and ECHILD means the application reaps its children itself.
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                const pid_t reaped = ::waitpid(pid_, nullptr, WNOHANG);
                if (reaped == pid_ || (reaped < 0 && errno != EINTR))
                {
                    break;
                }
                ::usleep(2000);
            }
        }
    }

    void X11ModeGuardian::UpdateAppliedMode(const std::uint32_t mode)
    {
        if (socket_ < 0)
        {
            return;
        }
        unsigned char message[5] = {'M', 0, 0, 0, 0};
        Put32(message + 1, mode);
        (void) ::send(socket_, message, sizeof(message), MSG_NOSIGNAL);
    }

} // namespace CNA::Platform::X11
