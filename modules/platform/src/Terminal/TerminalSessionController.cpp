// SPDX-License-Identifier: MS-PL

#include "TerminalSessionController.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <utility>

namespace CNA::Platform::Terminal {

    namespace {

        bool SameOptions(const TerminalSessionOptions& left, const TerminalSessionOptions& right)
        {
            return left.rawMode == right.rawMode &&
                   left.alternateScreen == right.alternateScreen &&
                   left.hideCursor == right.hideCursor &&
                   left.mouseReporting == right.mouseReporting &&
                   left.kittyKeyboard == right.kittyKeyboard;
        }

        std::size_t Index(const TerminalSessionUse use)
        {
            return static_cast<std::size_t>(use);
        }

    } // namespace

    TerminalSessionController::Lease::Lease(
        std::shared_ptr<TerminalSessionController> controller, const TerminalSessionUse use)
        : controller_(std::move(controller)), use_(use)
    {
    }

    TerminalSessionController::Lease::~Lease() { Reset(); }

    TerminalSessionController::Lease::Lease(Lease&& other) noexcept
        : controller_(std::move(other.controller_)), use_(other.use_)
    {
        other.use_ = TerminalSessionUse::Count;
    }

    TerminalSessionController::Lease& TerminalSessionController::Lease::operator=(
        Lease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            controller_ = std::move(other.controller_);
            use_ = other.use_;
            other.use_ = TerminalSessionUse::Count;
        }
        return *this;
    }

    void TerminalSessionController::Lease::Reset() noexcept
    {
        if (controller_ != nullptr)
        {
            std::shared_ptr<TerminalSessionController> controller = std::move(controller_);
            const TerminalSessionUse use = use_;
            use_ = TerminalSessionUse::Count;
            controller->Release(use);
        }
    }

    TerminalSessionController::TerminalSessionController(const int outputDescriptor,
                                                         const int inputDescriptor,
                                                         const bool kittyKeyboardSupported)
        : outputDescriptor_(outputDescriptor)
        , inputDescriptor_(inputDescriptor)
        , kittyKeyboardSupported_(kittyKeyboardSupported)
    {
    }

    TerminalSessionController::Lease TerminalSessionController::Acquire(
        const TerminalSessionUse use)
    {
        if (use == TerminalSessionUse::Count)
        {
            throw PlatformException("TerminalSessionController", "invalid session use");
        }
        if (use == TerminalSessionUse::Keyboard && !kittyKeyboardSupported_)
        {
            throw PlatformException("TerminalSessionController",
                                    "Kitty keyboard mode was not detected");
        }

        ++uses_[Index(use)];
        try
        {
            Refresh();
        }
        catch (...)
        {
            RestoreAfterFailedAcquire(use);
            throw;
        }
        return Lease(shared_from_this(), use);
    }

    void TerminalSessionController::Release(const TerminalSessionUse use) noexcept
    {
        if (use == TerminalSessionUse::Count || uses_[Index(use)] == 0)
        {
            return;
        }
        --uses_[Index(use)];
        RefreshNoThrow();
    }

    bool TerminalSessionController::HasAnyUse() const
    {
        for (const std::size_t count : uses_)
        {
            if (count != 0)
            {
                return true;
            }
        }
        return false;
    }

    TerminalSessionOptions TerminalSessionController::BuildOptions() const
    {
        TerminalSessionOptions options;
        const bool presenter = uses_[Index(TerminalSessionUse::Presenter)] != 0;
        const bool keyboard = uses_[Index(TerminalSessionUse::Keyboard)] != 0;
        const bool mouse = uses_[Index(TerminalSessionUse::Mouse)] != 0;

        options.rawMode = keyboard || mouse || presenter;
        options.alternateScreen = presenter;
        options.hideCursor = presenter;
        options.mouseReporting = mouse;
        options.kittyKeyboard = keyboard;
        return options;
    }

    void TerminalSessionController::Refresh()
    {
        if (!HasAnyUse())
        {
            if (session_ != nullptr)
            {
                session_.reset();
                haveActiveOptions_ = false;
                ++generation_;
            }
            return;
        }

        const TerminalSessionOptions wanted = BuildOptions();
        if (session_ != nullptr && haveActiveOptions_ && SameOptions(activeOptions_, wanted))
        {
            return;
        }

        // Rebuild instead of modifying the active TerminalSession. Its signal handler reads an
        // immutable restoration record; making that record mutable would trade a rare screen
        // redraw for a race exactly on the crash path whose safety matters most.
        session_.reset();
        haveActiveOptions_ = false;
        ++generation_;
        session_ = std::make_unique<TerminalSession>(outputDescriptor_, inputDescriptor_, wanted);
        activeOptions_ = wanted;
        haveActiveOptions_ = true;
    }

    void TerminalSessionController::RefreshNoThrow() noexcept
    {
        try
        {
            Refresh();
        }
        catch (...)
        {
            // A release cannot report an error from a destructor. The old session has already
            // restored the terminal; leaving no session is the only safe degraded state.
            session_.reset();
            haveActiveOptions_ = false;
        }
    }

    void TerminalSessionController::RestoreAfterFailedAcquire(const TerminalSessionUse use) noexcept
    {
        --uses_[Index(use)];
        try
        {
            Refresh();
        }
        catch (...)
        {
            // Refresh() restores before rebuilding. If even the previous union cannot be
            // recreated, retain no half-configured session and let existing leases degrade to a
            // safely restored terminal. Their next explicit operation can report its own error.
            session_.reset();
            haveActiveOptions_ = false;
        }
    }

} // namespace CNA::Platform::Terminal
