// SPDX-License-Identifier: MS-PL

#include "X11Error.hpp"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

namespace CNA::Platform::X11 {

    namespace {

        // The policy's state is deliberately IMMORTAL -- allocated once and never destroyed.
        //
        // A platform can outlive every ordinary static: CurrentPlatform keeps the lazily created
        // default in a function-local static that is constructed BEFORE the first connection
        // registers here, so at exit() it is destroyed AFTER this file's statics. Its
        // ~X11Connection then called Unregister on a vector and a mutex that no longer existed --
        // a heap-use-after-free at the end of every X11 application that used the XNA API, found
        // by AddressSanitizer (plans/plan_native_platform_validation.md NPV-0102). Never
        // destroying them makes Unregister valid at any point of process teardown.

        std::mutex& PolicyMutex()
        {
            static auto* const mutex = new std::mutex();
            return *mutex;
        }

        /// Connections CNA opened. An error on a connection that is not in this list belongs to
        /// the host application and is forwarded to the handler CNA replaced.
        std::vector<Display*>& OwnedDisplays()
        {
            static auto* const displays = new std::vector<Display*>();
            return *displays;
        }

        XErrorHandler& PreviousHandler()
        {
            static XErrorHandler handler = nullptr;
            return handler;
        }

        bool& HandlerInstalled()
        {
            static bool installed = false;
            return installed;
        }

        /// The innermost trap, per thread. Xlib is not thread-safe without XInitThreads(), which
        /// this backend deliberately does not call (plans/plan_x11.md design decision 7), so each
        /// thread's traps are its own and no lock is needed on this path.
        X11ErrorTrap*& ActiveTrap()
        {
            static thread_local X11ErrorTrap* trap = nullptr;
            return trap;
        }

        bool IsOwnedDisplay(Display* display)
        {
            std::lock_guard<std::mutex> lock(PolicyMutex());
            const std::vector<Display*>& displays = OwnedDisplays();
            return std::find(displays.begin(), displays.end(), display) != displays.end();
        }

        int HandleXError(Display* display, XErrorEvent* error)
        {
            if (error == nullptr)
            {
                return 0;
            }

            if (!IsOwnedDisplay(display))
            {
                // Not ours. The host application, or a library it loaded, opened this connection
                // and owns the policy for it.
                XErrorHandler previous = PreviousHandler();
                return previous != nullptr ? previous(display, error) : 0;
            }

            if (X11ErrorTrap::Deliver(display, *error))
            {
                return 0;
            }

            // Untrapped error on one of CNA's own connections. Reported and swallowed: an X
            // protocol error is not fatal to the connection, and Xlib's default handler calling
            // exit() from inside a platform call is the documented hazard this policy exists for.
            char text[128] = {};
            XGetErrorText(display, error->error_code, text, static_cast<int>(sizeof(text) - 1));
            std::fprintf(stderr,
                         "[CNA][X11] X protocol error ignored: %s (error_code=%u request=%u.%u "
                         "serial=%lu resource=0x%lx). Xlib's default handler would have called "
                         "exit() here; see plans/plan_x11.md design decision 8.\n",
                         text, static_cast<unsigned>(error->error_code),
                         static_cast<unsigned>(error->request_code),
                         static_cast<unsigned>(error->minor_code), error->serial,
                         error->resourceid);
            std::fflush(stderr);
            return 0;
        }

    } // namespace

    void X11ErrorPolicy::Register(Display* display)
    {
        if (display == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(PolicyMutex());
        OwnedDisplays().push_back(display);
        if (!HandlerInstalled())
        {
            PreviousHandler() = XSetErrorHandler(&HandleXError);
            HandlerInstalled() = true;
        }
    }

    void X11ErrorPolicy::Unregister(Display* display)
    {
        if (display == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(PolicyMutex());
        std::vector<Display*>& displays = OwnedDisplays();
        const auto found = std::find(displays.begin(), displays.end(), display);
        if (found != displays.end())
        {
            displays.erase(found);
        }
        if (displays.empty() && HandlerInstalled())
        {
            // Give the process back exactly what it had. XSetErrorHandler(nullptr) restores
            // Xlib's own default, which is what a host that never set one expects to get back.
            XSetErrorHandler(PreviousHandler());
            PreviousHandler() = nullptr;
            HandlerInstalled() = false;
        }
    }

    X11ErrorTrap::X11ErrorTrap(Display* display)
        : display_(display), previous_(ActiveTrap())
    {
        ActiveTrap() = this;
    }

    X11ErrorTrap::~X11ErrorTrap()
    {
        ActiveTrap() = previous_;
    }

    bool X11ErrorTrap::Sync()
    {
        if (display_ != nullptr)
        {
            // discard = False: this is a synchronisation point, not a queue flush. Discarding
            // would throw away the ConfigureNotify the caller is about to poll for.
            XSync(display_, False);
        }
        return errorCode_ == 0;
    }

    std::string X11ErrorTrap::Describe() const
    {
        if (errorCode_ == 0)
        {
            return {};
        }
        char text[128] = {};
        if (display_ != nullptr)
        {
            XGetErrorText(display_, errorCode_, text, static_cast<int>(sizeof(text) - 1));
        }
        std::string description = text[0] != '\0' ? std::string(text) : std::string("X error");
        description += " (request " + std::to_string(static_cast<unsigned>(requestCode_)) + "." +
                       std::to_string(static_cast<unsigned>(minorCode_)) + ", resource 0x";
        char resource[32] = {};
        std::snprintf(resource, sizeof(resource), "%lx", resourceId_);
        description += resource;
        description += ")";
        return description;
    }

    bool X11ErrorTrap::Deliver(Display* display, const XErrorEvent& error)
    {
        for (X11ErrorTrap* trap = ActiveTrap(); trap != nullptr; trap = trap->previous_)
        {
            if (trap->display_ != display)
            {
                continue;
            }
            // First error wins: it is the one that describes what went wrong, and later errors in
            // the same bracket are usually consequences of it.
            if (trap->errorCode_ == 0)
            {
                trap->errorCode_ = error.error_code;
                trap->requestCode_ = error.request_code;
                trap->minorCode_ = error.minor_code;
                trap->resourceId_ = error.resourceid;
            }
            return true;
        }
        return false;
    }

} // namespace CNA::Platform::X11
