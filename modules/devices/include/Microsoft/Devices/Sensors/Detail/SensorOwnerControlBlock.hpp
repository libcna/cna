// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstdint>
#include <mutex>

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Shared-ownership control block letting a native backend callback
     * safely detect "the owner object is gone" without ever dereferencing it (Tasks LIFE-002/LIFE-003/LIFE-005).
     *
     * `Compass`/`Motion`'s constructor holds the single authoritative
     * `shared_ptr<SensorOwnerControlBlock<TOwner>>` for their own lifetime.
     * Every lambda handed to a `Detail::ICompassBackend`/`Detail::IMotionBackend`
     * captures a *copy* of that `shared_ptr` (never a raw `TOwner*`, and
     * never `this` — capturing `this` would implicitly reach back through a
     * member access the moment the owner is destroyed) plus the
     * `generation` value active when it was registered.
     *
     * Before touching `owner`, a callback locks `mutex` and checks that
     * `generation` still matches (no newer Start()/Stop() has superseded
     * this registration) and that `owner != nullptr` (the owner has not
     * begun tearing itself down) — only then does it read the pointer and
     * call through it, after releasing the lock (never call user code, or
     * anything that can re-enter the owner's own public API, while holding
     * this lock — see `Compass`/`Motion`'s own `Start()`/`Stop()` doc
     * comments for the full Task LIFE-001 rationale).
     *
     * `owner` is set to `nullptr` by the owner's own `Dispose(true)`, under
     * `mutex`, as close to the start of teardown as practical — this is
     * what lets any callback that has not *yet* passed its own generation/
     * owner check safely no-op once disposal has begun, closing the
     * specific two-callback-ordering use-after-free Task LIFE-005 found in
     * `AndroidCompassBackend::HandleMagneticFieldSample()` (a
     * `CurrentValueChanged` handler destroying the owner before an already-
     * copied `Calibrate` callback runs).
     *
     * **Documented, accepted remaining boundary, consistent with this
     * project's existing `AndroidSensorBridge`-level precedent
     * (`ANDROID-BRIDGE-006`):** a callback that has *already* passed its
     * generation/owner check and is *currently*, on another thread, calling
     * into `owner` at the exact instant a *different* thread completes that
     * owner's destruction remains unsupported/undefined — this control
     * block closes the far more common "callback arrives after the owner
     * decided to tear down" case, not full concurrent-destruction safety
     * for a callback already mid-flight on another thread. Destroying the
     * owner from *within* its own currently-executing callback (the same
     * thread) is fully safe: the owner's destructor runs synchronously
     * before that same call stack can reach any later callback capturing
     * this same control block (see `Compass::Dispose(bool)`'s own comment).
     */
    template <typename TOwner>
    struct SensorOwnerControlBlock
    {
        std::mutex mutex;
        std::uint64_t generation = 0;
        TOwner* owner = nullptr;
    };
} // namespace Microsoft::Devices::Sensors::Detail
