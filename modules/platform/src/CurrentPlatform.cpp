// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/CurrentPlatform.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace CNA::Platform {

    namespace {

        // Every holder below that owns a destructor is deliberately IMMORTAL -- allocated once and
        // never destroyed -- for the reason X11Error.cpp records
        // (plans/plan_native_platform_validation.md NPV-0102) and the C API handle registry proved
        // again: whatever installed or pinned a platform can be destroyed at ANY point of process
        // teardown, including from a later static's destructor, and must still find this state
        // alive. ~Game reaches all of it through SetCurrentPlatform on its way out
        // (plans/plan_capi_smoke_stability.md CSS-2). Explicit teardown is unaffected --
        // ResetCurrentPlatform still destroys the lazily created default deterministically; what
        // goes away is only the implicit destruction of one never-reset default at process exit,
        // which no caller could have observed and which is exactly the destruction that was
        // reaching into already-dead state.
        //
        // Installed() needs no such treatment: a raw pointer is trivially destructible, so it
        // registers no destructor and keeps its value for the whole of process teardown already.

        std::mutex& StateMutex()
        {
            static auto* const mutex = new std::mutex();
            return *mutex;
        }

        /// Borrowed: installed by SetCurrentPlatform and owned by whoever installed it.
        IPlatform*& Installed()
        {
            static IPlatform* platform = nullptr;
            return platform;
        }

        /// Owned: created lazily only when the static API is used with nothing installed.
        std::unique_ptr<IPlatform>& LazyDefault()
        {
            static auto* const platform = new std::unique_ptr<IPlatform>();
            return *platform;
        }

        struct AmbientSubsystemPin
        {
            const void* owner;
            IPlatform* platform;
            PlatformSubsystem subsystem;
        };

        std::vector<AmbientSubsystemPin>& SubsystemPins()
        {
            static auto* const pins = new std::vector<AmbientSubsystemPin>();
            return *pins;
        }

        IPlatform* ExistingCurrentPlatform()
        {
            return Installed() != nullptr ? Installed() : LazyDefault().get();
        }

        IPlatform& ResolveCurrentPlatform()
        {
            if (IPlatform* existing = ExistingCurrentPlatform())
            {
                return *existing;
            }

            LazyDefault() = PlatformFactory::Create();
            if (!LazyDefault())
            {
                throw PlatformException(
                    "GetCurrentPlatform", "the default platform could not be created");
            }
            return *LazyDefault();
        }

        void ReleasePin(AmbientSubsystemPin& pin) noexcept
        {
            try
            {
                pin.platform->ReleaseSubsystem(pin.subsystem);
            }
            catch (...)
            {
                // Platform teardown must continue. A failed release leaves the subsystem up,
                // which is safer than invalidating another owner's native handles.
            }
        }

        void TransferPins(IPlatform* previous, IPlatform* replacement) noexcept
        {
            if (previous == replacement)
                return;

            auto& pins = SubsystemPins();
            for (auto it = pins.begin(); it != pins.end();)
            {
                if (it->platform != previous)
                {
                    ++it;
                    continue;
                }

                bool replacementAcquired = false;
                if (replacement != nullptr)
                {
                    try
                    {
                        replacement->AcquireSubsystem(it->subsystem);
                        replacementAcquired = true;
                    }
                    catch (...)
                    {
                        // Installing a platform is not itself a subsystem request. The owner will
                        // retry on its next use and report the platform's exact acquisition error.
                    }
                }

                ReleasePin(*it);
                if (replacementAcquired)
                {
                    it->platform = replacement;
                    ++it;
                }
                else
                {
                    it = pins.erase(it);
                }
            }
        }

    } // namespace

    IPlatform& GetCurrentPlatform()
    {
        const std::lock_guard<std::mutex> guard(StateMutex());
        return ResolveCurrentPlatform();
    }

    void SetCurrentPlatform(IPlatform* platform)
    {
        const std::lock_guard<std::mutex> guard(StateMutex());
        IPlatform* const previous = ExistingCurrentPlatform();
        IPlatform* const replacement =
            platform != nullptr ? platform : LazyDefault().get();
        TransferPins(previous, replacement);
        Installed() = platform;
    }

    bool HasCurrentPlatform()
    {
        const std::lock_guard<std::mutex> guard(StateMutex());
        return Installed() != nullptr || LazyDefault() != nullptr;
    }

    void ResetCurrentPlatform()
    {
        const std::lock_guard<std::mutex> guard(StateMutex());
        TransferPins(ExistingCurrentPlatform(), nullptr);
        Installed() = nullptr;
        LazyDefault().reset();
    }

    namespace Detail {

        void PinCurrentPlatformSubsystem(
            const void* owner, const PlatformSubsystem subsystem)
        {
            if (owner == nullptr)
            {
                throw std::invalid_argument("ambient subsystem pin owner must not be null");
            }

            const std::lock_guard<std::mutex> guard(StateMutex());
            IPlatform& current = ResolveCurrentPlatform();
            auto& pins = SubsystemPins();
            const auto existing = std::find_if(
                pins.begin(), pins.end(),
                [owner](const AmbientSubsystemPin& pin) { return pin.owner == owner; });

            if (existing != pins.end() &&
                existing->platform == &current && existing->subsystem == subsystem)
            {
                return;
            }

            current.AcquireSubsystem(subsystem);
            if (existing == pins.end())
            {
                pins.push_back({owner, &current, subsystem});
                return;
            }

            ReleasePin(*existing);
            existing->platform = &current;
            existing->subsystem = subsystem;
        }

        void UnpinCurrentPlatformSubsystem(const void* owner) noexcept
        {
            if (owner == nullptr)
                return;

            const std::lock_guard<std::mutex> guard(StateMutex());
            auto& pins = SubsystemPins();
            const auto existing = std::find_if(
                pins.begin(), pins.end(),
                [owner](const AmbientSubsystemPin& pin) { return pin.owner == owner; });
            if (existing == pins.end())
                return;

            ReleasePin(*existing);
            pins.erase(existing);
        }

    } // namespace Detail

} // namespace CNA::Platform
