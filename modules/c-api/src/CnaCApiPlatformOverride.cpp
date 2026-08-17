// SPDX-License-Identifier: MS-PL

#include "CnaCApiPlatformOverride.hpp"

#include <mutex>

namespace CNA::C::Detail {

namespace {

std::mutex& OverrideMutex()
{
    static std::mutex mutex;
    return mutex;
}

// The platform in force when the override was installed, so it can be put back. Resolved at install
// time rather than once: a Game owns its platform and installs it on a stack, and capturing the
// first one ever seen outlives it.
CNA::Platform::IPlatform*& WrappedPlatform()
{
    static CNA::Platform::IPlatform* wrapped = nullptr;
    return wrapped;
}

PlatformOverride& OverrideInstance()
{
    static PlatformOverride instance(CNA::Platform::GetCurrentPlatform());
    return instance;
}

} // namespace

PlatformOverride& GetPlatformOverride()
{
    const std::lock_guard<std::mutex> lock(OverrideMutex());
    PlatformOverride& instance = OverrideInstance();
    if (WrappedPlatform() == nullptr) {
        CNA::Platform::IPlatform& current = CNA::Platform::GetCurrentPlatform();
        WrappedPlatform() = &current;
        instance.SetInner(current);
        // Ownership stays here: the instance is a function-local static and outlives every C call
        // that can reach it, which is what SetCurrentPlatform's borrowed pointer requires.
        CNA::Platform::SetCurrentPlatform(&instance);
    }
    return instance;
}

void ReleasePlatformOverrideIfUnused()
{
    const std::lock_guard<std::mutex> lock(OverrideMutex());
    if (WrappedPlatform() == nullptr || OverrideInstance().HasAnyOverride()) {
        return;
    }
    CNA::Platform::SetCurrentPlatform(WrappedPlatform());
    WrappedPlatform() = nullptr;
}

void ResetPlatformOverride()
{
    const std::lock_guard<std::mutex> lock(OverrideMutex());
    if (WrappedPlatform() == nullptr) {
        return;
    }
    PlatformOverride& instance = OverrideInstance();
    instance.SetDialogs(nullptr);
    instance.SetTray(nullptr);
    instance.SetCamera(nullptr);
    // Put the platform back before the game goes: this runs from the destroy path *before* the
    // game's handle is released, so the platform it owns is still alive here and is exactly what
    // should be current again. Leaving the decorator installed is what breaks -- `Game`'s own
    // teardown re-aims the ambient accessor only when the game is the one currently aimed at, so a
    // decorator sitting in front of it keeps that from ever happening.
    CNA::Platform::SetCurrentPlatform(WrappedPlatform());
    WrappedPlatform() = nullptr;
}

} // namespace CNA::C::Detail
