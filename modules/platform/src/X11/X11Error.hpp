// SPDX-License-Identifier: MS-PL
#pragma once

#include "X11Headers.hpp"

#include <string>

namespace CNA::Platform::X11 {

    /**
     * @brief Owns CNA's X protocol error policy for the process, without seizing it.
     *
     * ### Why this exists
     *
     * Xlib's default error handler calls `exit()`. That is not a theoretical hazard here: the
     * SDL3 backend measured it twice (plans/plan_vulkan.md VULKAN-154/157) — one bad request
     * either killed a test binary outright or deadlocked it inside a platform destructor that
     * `exit()` had run while the caller still held the platform lock. A native X11 backend issues
     * far more requests than SDL did, so it needs the same protection by construction rather than
     * as a diagnostic.
     *
     * ### Why the previous handler is chained and restored
     *
     * `XSetErrorHandler` is process-global, and CNA is a library. A host application that set its
     * own handler before creating the platform must still receive errors for its own connections:
     * an error on a display CNA did not open is forwarded unchanged to whatever handler was
     * installed, and the moment the last X11 platform closes its display the previous handler is
     * put back. Taking a process-global permanently would be exactly the kind of silent
     * reconfiguration the platform contract forbids.
     *
     * `XSetIOErrorHandler` is deliberately left alone. An I/O error means the connection is gone,
     * and that handler must not return — replacing it with something that does would turn a dead
     * connection into an infinite loop of failing requests.
     */
    class X11ErrorPolicy
    {
    public:
        /**
         * @brief Installs CNA's handler if no X11 platform has yet done so, and registers
         * @p display as one of CNA's own connections.
         *
         * Reference-counted: the handler is installed on the first registration and restored on
         * the last unregistration.
         *
         * @param display The connection CNA owns.
         */
        static void Register(Display* display);

        /**
         * @brief Stops treating @p display as one of CNA's connections, restoring the previous
         * handler when it was the last one.
         *
         * @param display The connection being closed.
         */
        static void Unregister(Display* display);
    };

    /**
     * @brief Brackets one narrow X request so its asynchronous error becomes a synchronous answer.
     *
     * X protocol errors arrive asynchronously: the request that caused one has long returned by
     * the time the error reaches the client. Code that must know whether a specific request
     * succeeded — `XGetImage` on a window that may have been destroyed, an `XShmAttach` the server
     * may refuse — constructs a trap, issues the request, and calls @ref Sync, which round-trips
     * the connection so that any error for that request has certainly been delivered.
     *
     * The trap installs nothing globally. It registers itself with the policy above, which routes
     * errors to the innermost active trap on the same display and falls back to CNA's logging
     * handler when no trap is active. Nesting is supported and correctly unwound, which matters
     * because the destructor is what deactivates it — an exception thrown between the request and
     * the `Sync` cannot leave the trap installed.
     */
    class X11ErrorTrap
    {
    public:
        /**
         * @brief Begins trapping errors on one connection.
         *
         * @param display The connection to trap errors on.
         */
        explicit X11ErrorTrap(Display* display);

        /** @brief Ends trapping, restoring the enclosing trap if there was one. */
        ~X11ErrorTrap();

        X11ErrorTrap(const X11ErrorTrap&) = delete;
        X11ErrorTrap& operator=(const X11ErrorTrap&) = delete;

        /**
         * @brief Round-trips the connection so every error for the bracketed requests is delivered.
         *
         * @return True when no error was trapped.
         */
        bool Sync();

        /**
         * @brief Gets whether an error was trapped since construction or the last @ref Sync.
         *
         * @return True when at least one error arrived.
         */
        [[nodiscard]] bool HasError() const { return errorCode_ != 0; }

        /**
         * @brief Gets the X error code of the first trapped error.
         *
         * @return The `error_code` field, or zero when nothing was trapped.
         */
        [[nodiscard]] unsigned char GetErrorCode() const { return errorCode_; }

        /**
         * @brief Builds a human-readable description of the first trapped error.
         *
         * @return A description such as `"BadWindow (request 20.0)"`, or an empty string when
         * nothing was trapped.
         */
        [[nodiscard]] std::string Describe() const;

        /**
         * @brief Records an error into the innermost active trap, or logs it.
         *
         * Called only by the policy's installed handler.
         *
         * @param display The connection the error arrived on.
         * @param error The error event.
         * @return True when a trap consumed it.
         */
        static bool Deliver(Display* display, const XErrorEvent& error);

    private:
        Display* display_ = nullptr;
        X11ErrorTrap* previous_ = nullptr;
        unsigned char errorCode_ = 0;
        unsigned char requestCode_ = 0;
        unsigned char minorCode_ = 0;
        unsigned long resourceId_ = 0;
    };

} // namespace CNA::Platform::X11
