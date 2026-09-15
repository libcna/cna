// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sys/socket.h>
#include <sys/types.h>

namespace CNA::Platform::X11 {

    /** @brief A rectangle in root-window pixels. */
    struct X11Rect
    {
        /** @brief Left edge. */
        int x = 0;
        /** @brief Top edge. */
        int y = 0;
        /** @brief Width in pixels. */
        int width = 0;
        /** @brief Height in pixels. */
        int height = 0;
    };

    /**
     * @brief The two screen-size changes that bracket one CRTC change.
     *
     * RandR refuses a CRTC that reaches outside the screen, and refuses a screen size that cuts
     * through a lit CRTC. So a mode change that needs a larger screen grows it first, and one
     * that leaves the screen too large shrinks it afterwards -- and no step ever passes through
     * an invalid configuration, which is what lets it work without switching the CRTC off in
     * between (SDL does, and then fails with BadMatch on a multi-monitor desktop).
     */
    struct X11ScreenPlan
    {
        /** @brief Screen width to set before the CRTC change; the current width when unchanged. */
        int growWidth = 0;
        /** @brief Screen height to set before the CRTC change; the current height when unchanged. */
        int growHeight = 0;
        /** @brief Screen width to set after the CRTC change; equal to growWidth when unchanged. */
        int finalWidth = 0;
        /** @brief Screen height to set after the CRTC change; equal to growHeight when unchanged. */
        int finalHeight = 0;
    };

    /**
     * @brief Plans the screen sizes around one CRTC change.
     *
     * The final size is the bounding box of every lit CRTC after the change -- so on a single
     * monitor the screen follows the new mode and the pointer cannot wander off the visible area
     * -- but never smaller than @p preferredWidth x @p preferredHeight, which is how a restore
     * gets back a screen that was deliberately larger than its monitors. It is then clamped to the
     * server's size range.
     *
     * Allocates nothing and cannot fail: the mode guardian calls it after `fork()`.
     *
     * @param currentWidth The screen width now.
     * @param currentHeight The screen height now.
     * @param others Every other lit CRTC.
     * @param otherCount How many there are.
     * @param changed The changed CRTC's rectangle after the change.
     * @param preferredWidth The smallest final width wanted, or 0 for the bounding box.
     * @param preferredHeight The smallest final height wanted, or 0 for the bounding box.
     * @param minimumWidth The server's minimum screen width.
     * @param minimumHeight The server's minimum screen height.
     * @param maximumWidth The server's maximum screen width.
     * @param maximumHeight The server's maximum screen height.
     * @return The plan.
     */
    [[nodiscard]] X11ScreenPlan PlanScreenSizes(int currentWidth, int currentHeight,
                                                const X11Rect* others, std::size_t otherCount,
                                                const X11Rect& changed, int preferredWidth,
                                                int preferredHeight, int minimumWidth,
                                                int minimumHeight, int maximumWidth,
                                                int maximumHeight) noexcept;

    /**
     * @brief A screen dimension in millimetres, keeping the density the screen had.
     *
     * @param pixels The new size in pixels.
     * @param referencePixels The size the density is taken from, in pixels.
     * @param referenceMillimetres The same size in millimetres.
     * @return The millimetres; never zero.
     */
    [[nodiscard]] std::uint32_t MillimetresFor(int pixels, int referencePixels,
                                               std::uint32_t referenceMillimetres) noexcept;

    /**
     * @brief Everything it takes to put one CRTC back, captured while the process is healthy.
     *
     * Plain data with fixed-size arrays, because the guardian reads it after `fork()`, where
     * nothing may allocate.
     */
    struct X11ModeRestorePlan
    {
        /** @brief The most outputs one CRTC is restored with. */
        static constexpr std::size_t kMaxOutputs = 16;
        /** @brief The most bytes a connection-setup request with its cookie takes. */
        static constexpr std::size_t kMaxSetupBytes = 256;

        /** @brief The address the application's own connection reached the server at. */
        sockaddr_storage address{};
        /** @brief The length of @ref address. */
        socklen_t addressLength = 0;
        /** @brief The connection-setup request, authorisation included. */
        unsigned char setup[kMaxSetupBytes] = {};
        /** @brief The length of @ref setup. */
        std::size_t setupLength = 0;
        /** @brief The RANDR extension's major opcode on that server. */
        std::uint8_t randrOpcode = 0;
        /** @brief The root window of the screen. */
        std::uint32_t root = 0;
        /** @brief The CRTC to restore. */
        std::uint32_t crtc = 0;
        /** @brief The mode the application set; the CRTC is restored only while it is still in it. */
        std::uint32_t appliedMode = 0;
        /** @brief The mode the CRTC had before. */
        std::uint32_t originalMode = 0;
        /** @brief The CRTC's position before. */
        std::int16_t originalX = 0;
        /** @brief The CRTC's position before. */
        std::int16_t originalY = 0;
        /** @brief The CRTC's extent before, rotation included. */
        std::uint16_t originalWidth = 0;
        /** @brief The CRTC's extent before, rotation included. */
        std::uint16_t originalHeight = 0;
        /** @brief The CRTC's rotation and reflection before. */
        std::uint16_t originalRotation = 1;
        /** @brief How many outputs the CRTC drove. */
        std::uint16_t outputCount = 0;
        /** @brief The outputs the CRTC drove. */
        std::uint32_t outputs[kMaxOutputs] = {};
        /** @brief The screen size before. */
        std::uint16_t screenWidth = 0;
        /** @brief The screen size before. */
        std::uint16_t screenHeight = 0;
        /** @brief The screen's physical size before. */
        std::uint32_t screenWidthMm = 0;
        /** @brief The screen's physical size before. */
        std::uint32_t screenHeightMm = 0;
        /** @brief The server's screen size range. */
        std::uint16_t minimumWidth = 0;
        /** @brief The server's screen size range. */
        std::uint16_t minimumHeight = 0;
        /** @brief The server's screen size range. */
        std::uint16_t maximumWidth = 0;
        /** @brief The server's screen size range. */
        std::uint16_t maximumHeight = 0;
    };

    /** @brief What a raw restore did. */
    enum class X11ModeRestoreResult
    {
        /** @brief The CRTC and the screen are back as they were. */
        Restored,
        /** @brief The CRTC was no longer in the application's mode; someone else set it, and it
         *  was left alone. */
        LeftAlone,
        /** @brief The server could not be reached, or refused. */
        Failed,
    };

    /**
     * @brief Builds an X connection-setup request, little-endian, with its authorisation.
     *
     * @param output Receives the request.
     * @param capacity The size of @p output.
     * @param authName The authorisation protocol name, or null for none.
     * @param authNameLength Its length.
     * @param authData The authorisation data.
     * @param authDataLength Its length.
     * @return The request's length, or 0 when it does not fit.
     */
    [[nodiscard]] std::size_t EncodeConnectionSetup(unsigned char* output, std::size_t capacity,
                                                    const char* authName,
                                                    std::size_t authNameLength,
                                                    const unsigned char* authData,
                                                    std::size_t authDataLength) noexcept;

    /**
     * @brief Restores one CRTC over a fresh connection of its own, speaking the protocol directly.
     *
     * Async-signal-safe: sockets, `poll` and the caller's buffer, and nothing from Xlib, the heap
     * or the C++ library. That is the whole reason it exists alongside the Xlib restore -- it
     * runs in the guardian process after `fork()` from a process that may have had any number of
     * threads, where calling into Xlib would be undefined.
     *
     * @param plan What to restore.
     * @param scratch A working buffer; replies that do not fit are read past, not truncated into
     * a misparse.
     * @param scratchSize Its size; 64 KiB is plenty.
     * @return What happened.
     */
    [[nodiscard]] X11ModeRestoreResult RestoreDisplayModeRaw(const X11ModeRestorePlan& plan,
                                                             unsigned char* scratch,
                                                             std::size_t scratchSize) noexcept;

    /**
     * @brief A process that puts a changed display mode back if this one dies.
     *
     * ### Why a process
     *
     * An application that changes the monitor's mode owes the user the old one back, and the
     * cases that matter are exactly the ones in which the application cannot pay: a crash, an
     * `abort()`, a `SIGKILL`, the OOM killer. Signal handlers cover some of those and not the
     * last two, and installing them from a library takes them from the game. A second process
     * covers all of them without touching the first: the kernel closes a dead process's
     * descriptors, so the guardian's end of a socket pair reads end-of-file at the moment the
     * application is gone -- however it went -- and the guardian then restores the mode over a
     * connection of its own and exits.
     *
     * While the application is alive the guardian does nothing but wait. A normal exit from
     * exclusive mode restores the mode in-process and then disarms the guardian, which exits
     * without touching anything. The guardian also re-checks, before restoring, that the CRTC is
     * still in the mode the application set: a user who changed the resolution in the meantime
     * has made a decision that a dying game must not overrule.
     *
     * The guardian is `fork()`ed and never `exec`s; between the fork and its exit it calls only
     * async-signal-safe functions, which is what makes forking a threaded process sound. It
     * leaves the terminal's session, ignores the terminal's signals -- a Ctrl+C that kills the
     * game must not also kill the one process that repairs the display -- and keeps nothing open
     * but its socket.
     */
    class X11ModeGuardian
    {
    public:
        /**
         * @brief Starts a guardian for one changed CRTC.
         *
         * @param plan What the guardian restores if this process dies.
         * @return The guardian, or null when no process could be started.
         */
        [[nodiscard]] static std::unique_ptr<X11ModeGuardian> Start(const X11ModeRestorePlan& plan);

        /** @brief Disarms the guardian: it exits without restoring anything. */
        ~X11ModeGuardian();

        X11ModeGuardian(const X11ModeGuardian&) = delete;
        X11ModeGuardian& operator=(const X11ModeGuardian&) = delete;

        /**
         * @brief Tells the guardian the application now uses a different mode on its CRTC.
         *
         * @param mode The mode the CRTC was switched to.
         */
        void UpdateAppliedMode(std::uint32_t mode);

        /** @brief Gets the guardian's process id. @return The process id. */
        [[nodiscard]] pid_t GetProcessId() const { return pid_; }

    private:
        X11ModeGuardian(int socket, pid_t pid) : socket_(socket), pid_(pid) {}

        int socket_ = -1;
        pid_t pid_ = -1;
    };

} // namespace CNA::Platform::X11
