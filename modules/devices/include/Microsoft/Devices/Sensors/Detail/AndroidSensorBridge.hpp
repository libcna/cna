// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

#include "System/DateTimeOffset.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Converts a requested update interval to ASensorEventQueue_setEventRate()'s microsecond parameter.
     *
     * Pure function (mirrors Detail::ConvertAndroidPortraitToXnaLandscape()'s
     * precedent in AndroidSensorOrientation.hpp): testable on any platform
     * without needing a real Android sensor queue. Floors at 1 microsecond
     * — a non-positive or sub-microsecond requested interval is clamped up
     * rather than passed through as 0 or negative, which the NDK does not
     * document a defined meaning for. Ceils at `INT32_MAX` — a huge
     * requested interval (e.g. `TimeSpan::MaxValue`) converted to
     * microseconds as a `double` can exceed what `std::int32_t` can
     * represent, and `static_cast`-ing an out-of-range `double` to
     * `std::int32_t` is undefined behavior, not just a saturating
     * truncation.
     *
     * @param timeBetweenUpdates Requested update interval.
     * @return Equivalent microsecond value, clamped to `[1, INT32_MAX]`.
     */
    [[nodiscard]] inline std::int32_t ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(
        const System::TimeSpan& timeBetweenUpdates)
    {
        const double requestedMicroseconds = timeBetweenUpdates.getTotalMillisecondsProperty() * 1000.0;
        if (requestedMicroseconds <= 1.0)
        {
            return 1;
        }
        if (requestedMicroseconds >= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
        {
            return std::numeric_limits<std::int32_t>::max();
        }
        return static_cast<std::int32_t>(requestedMicroseconds);
    }

    /**
     * @brief Returns the number of semantically valid entries in an `ASensorEvent::data`/`AndroidSensorSample::Values` array, for a given raw NDK sensor type constant.
     *
     * Pure function (mirrors `ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds()`'s
     * own precedent immediately above): takes the sensor type as a plain
     * `int` rather than `ASensorType`, so this header still needs no
     * `<android/sensor.h>` include and stays testable on any platform
     * (Task ANDROID-BRIDGE-001, 2026-07-06 — the numeric literals below are
     * `ASENSOR_TYPE_*`'s own documented values, confirmed directly against
     * the vendored NDK's `android/sensor.h`, not guessed).
     *
     * - `ASENSOR_TYPE_MAGNETIC_FIELD` (2) / `ASENSOR_TYPE_GRAVITY` (9) /
     *   `ASENSOR_TYPE_LINEAR_ACCELERATION` (10) / `ASENSOR_TYPE_GYROSCOPE`
     *   (4): 3 — these all report through the NDK's shared `ASensorVector`
     *   union (`float v[3]` plus a status byte), confirmed directly from
     *   its own struct definition.
     * - `ASENSOR_TYPE_ROTATION_VECTOR` (11) / `ASENSOR_TYPE_GAME_ROTATION_VECTOR`
     *   (15): 5 — Android's own Java `SensorEvent.values` documentation for
     *   `TYPE_ROTATION_VECTOR` specifies `values[0..2]` = `x*sin(θ/2)`/
     *   `y*sin(θ/2)`/`z*sin(θ/2)`, `values[3]` = `cos(θ/2)` (optional on
     *   older API levels, always populated on the API 24+ minimum this
     *   project targets), `values[4]` = estimated heading accuracy in
     *   radians (`-1` if unavailable) — 5 total; the NDK's own `data[16]`
     *   union has no dedicated named field for this sensor type (unlike
     *   `ASensorVector`), consistent with it being a wider, less uniform
     *   shape than the 3-value vector sensors.
     * - Any other/unrecognized sensor type: 16 (the full raw union size —
     *   the safe, conservative fallback for a sensor type this codebase
     *   doesn't specifically know about, matching the previous unconditional
     *   behavior for every type before this task).
     *
     * @param androidSensorType Raw `ASENSOR_TYPE_*` constant, as a plain `int`.
     * @return Number of valid leading entries in the sample's `Values` array.
     */
    [[nodiscard]] inline int GetValueCountForAndroidSensorType(int androidSensorType)
    {
        switch (androidSensorType)
        {
        case 2:  // ASENSOR_TYPE_MAGNETIC_FIELD
        case 9:  // ASENSOR_TYPE_GRAVITY
        case 10: // ASENSOR_TYPE_LINEAR_ACCELERATION
        case 4:  // ASENSOR_TYPE_GYROSCOPE
            return 3;
        case 11: // ASENSOR_TYPE_ROTATION_VECTOR
        case 15: // ASENSOR_TYPE_GAME_ROTATION_VECTOR
            return 5;
        default:
            return 16;
        }
    }

    /**
     * One raw Android NDK sensor sample. Already stamped with real
     * wall-clock time, not the sensor's own monotonic boot-time timestamp —
     * matches the precedent Accelerometer/Gyroscope already established
     * (Task P4-7): ASensorEvent::timestamp is nanoseconds since boot, not
     * suitable for a WP7-style DateTimeOffset reading timestamp.
     */
    struct AndroidSensorSample
    {
        /** @brief Raw sensor values, in Android's own units/axis convention, unconverted. Mirrors ASensorEvent::data (up to 16 floats). */
        float Values[16] = {};

        /** @brief Number of valid entries in Values (e.g. 3 for a vector sensor, up to 5 for a rotation vector with accuracy). */
        int ValueCount = 0;

        /**
         * @brief Accuracy status, for sensor types that report one via `ASensorVector::status` (e.g. magnetic field, accelerometer, gyroscope).
         *
         * Numerically matches the NDK's `ASENSOR_STATUS_*` constants. This
         * is a distinct field, not part of Values — `ASensorVector::status`
         * occupies the same union memory as `Values[3]` would for a 4-float
         * interpretation (it is a byte-sized field packed after the 3
         * vector floats, not a 4th float), so it cannot be read correctly
         * through `Values` alone. Meaningless (left at its default, 0) for
         * sensor types that don't report a `ASensorVector`-shaped event
         * (e.g. rotation vector, which uses the plain `data[]` float form
         * instead).
         */
        int Status = 0;

        /** @brief Wall-clock time this sample was delivered. */
        System::DateTimeOffset Timestamp;
    };

    /**
     * @brief Shared Android-only bridge to the NDK's ASensorManager/ASensorEventQueue API, for exactly one Android sensor type.
     *
     * Delivers samples via callback on its own dedicated background thread.
     * An ALooper is thread-affine (must be ALooper_prepare()'d on the same
     * thread that later polls it), so this bridge owns and pumps its own
     * looper internally on a private worker thread, rather than requiring
     * CNA's game loop to pump anything — this keeps the eventual
     * Compass/Motion Android backends' threading model consistent with
     * Accelerometer/Gyroscope's existing SDL-callback-thread model (a game
     * subscribing to CurrentValueChanged must already treat the handler as
     * running on an unknown thread; see AUDIT.md's "Event-thread model"
     * note).
     *
     * This is CNA-internal plumbing shared by Compass/Motion's future
     * native Android backends (`plan_devices.md` Phase 7/8) — not itself an
     * XNA-facing sensor class, and not wired into Compass/Motion by this
     * class alone (see AndroidCompassBackend/AndroidMotionBackend).
     *
     * On any non-Android platform, every method is a safe, inert no-op
     * (IsAvailable() always false, Start() always returns false) — this
     * header has no Android-only #include, so it compiles cleanly on every
     * platform, matching the discipline Accelerometer.hpp/Gyroscope.hpp
     * already established for keeping SDL/platform headers out of public
     * headers.
     */
    class AndroidSensorBridge final
    {
    public:
        using SampleCallback = std::function<void(const AndroidSensorSample&)>;

        /**
         * @param androidSensorType One of the NDK's ASENSOR_TYPE_* constants
         * (e.g. ASENSOR_TYPE_MAGNETIC_FIELD), passed as a plain int so this
         * header never needs to include <android/sensor.h> — mirrors
         * Accelerometer::GetSdlSensorType()'s identical discipline for
         * SDL_SensorType.
         */
        explicit AndroidSensorBridge(int androidSensorType);

        /** @brief Stops delivery (if started) and destroys the bridge. */
        ~AndroidSensorBridge();

        AndroidSensorBridge(const AndroidSensorBridge&) = delete;
        AndroidSensorBridge& operator=(const AndroidSensorBridge&) = delete;
        AndroidSensorBridge(AndroidSensorBridge&&) = delete;
        AndroidSensorBridge& operator=(AndroidSensorBridge&&) = delete;

        /**
         * @brief Cheap probe: true if this sensor type exists on this device.
         *
         * Does not start delivery, and does not hold any resource open as a
         * side effect of probing (same discipline as
         * VibrateController::getIsSupportedProperty()).
         *
         * @return true if a sensor of this type is available; otherwise false.
         */
        [[nodiscard]] bool IsAvailable() const;

        /**
         * @brief Starts delivering samples on a dedicated background thread.
         *
         * Calling this while already started (a prior Start() succeeded and
         * Stop() has not been called since) is a **documented failure, not
         * a silent restart or an overwrite of the running worker thread** —
         * matching Accelerometer::Start()'s own "already started" exception
         * convention (translated to this class's bool-return contract,
         * since this is CNA-internal plumbing, not an XNA-facing API that
         * throws). Returns false immediately without touching the running
         * worker. Callers that want a restart must call Stop() first.
         *
         * The return value reflects **real, confirmed delivery having
         * started** — this method blocks (briefly, bounded, never
         * indefinitely) until the worker thread has either successfully
         * created and enabled its `ASensorEventQueue` or failed to do so,
         * so a caller never sees a false "true" while the platform sensor
         * queue silently failed to initialize.
         *
         * @param timeBetweenUpdates Requested sample interval, mapped to
         * ASensorEventQueue_setEventRate()'s microsecond parameter. Applied
         * at Start() time; call SetSampleInterval() (Task ANDROID-BRIDGE-002)
         * to change it again on an already-running bridge without a
         * Stop()/Start() cycle. Note: Android 12+ (API 31+) restricts
         * high-frequency sensor sampling (rates faster than ~200Hz) unless
         * the app declares the `HIGH_SAMPLING_RATE_SENSORS` permission —
         * requesting a very small `timeBetweenUpdates` on such a device may
         * silently be capped by the OS to a slower effective rate than
         * requested; this bridge does not detect or compensate for that.
         * @param callback Invoked once per sample, on this bridge's own
         * background thread — never the calling thread. May throw: any
         * exception escaping this callback is caught and silently
         * discarded here (mirrors
         * Detail::PlatformSensorSubsystem<TSensor>::DispatchToInstances()'s
         * identical per-instance exception-swallowing policy, Task P8-5) —
         * an exception left to escape a `std::thread`'s entry point would
         * otherwise call `std::terminate()` and crash the whole process,
         * which is strictly worse than swallowing it. A game's own
         * `CurrentValueChanged`/`Calibrate` handler should not rely on an
         * exception it throws propagating anywhere — it never will, same
         * as the platform-backed sensors.
         * @return true if sensor delivery genuinely started (the platform
         * sensor queue was created and enabled); false if this sensor type
         * is unavailable, if delivery could not actually be started, or if
         * this bridge was already started (including a prior worker that is
         * still in the process of stopping — see Task ANDROID-BRIDGE-005
         * below).
         *
         * @note Thread safety: the "already started" check is guarded by one
         * internal mutex, so two threads calling Start() concurrently (or
         * one calling Start() while another calls Stop()) cannot corrupt
         * this bridge's internal `std::thread` handle. That mutex is never
         * held across this method's own bounded wait for the worker's
         * startup handshake, so it cannot deadlock against Stop(). Two or
         * more *external* (non-worker) threads calling `Stop()` concurrently
         * on the same bridge is safe and synchronous for every caller (Task
         * ANDROID-BRIDGE-003, 2026-07-06) — see `Stop()`'s own note for how.
         *
         * **Fixed (Task ANDROID-BRIDGE-005, 2026-07-16):** the "already
         * started" gate no longer keys off `std::thread::joinable()`, which
         * a reentrant self-stop's `detach()` clears immediately even though
         * the worker's OS thread is still executing. This method now rejects
         * a new `Start()` for as long as a prior worker's `Run()` has not
         * genuinely finished (confirmed by the worker itself, not inferred
         * from the `std::thread` object's own reclaim state) — closing a
         * real bug where a self-stopped-then-immediately-restarted bridge
         * could end up with two workers concurrently sharing one `Impl`.
         */
        bool Start(const System::TimeSpan& timeBetweenUpdates, SampleCallback callback);

        /**
         * @brief Stops delivery and joins the background thread.
         *
         * Safe to call even if Start() was never called, or was already
         * stopped. Calling this reentrantly from within this bridge's own
         * callback (on its own worker thread) does not deadlock — see this
         * method's .cpp implementation for the accepted boundary this
         * still leaves (detaches rather than joins in that one case,
         * mirroring Accelerometer's own documented "destroying from within
         * your own callback" limitation rather than fully solving it).
         *
         * @note Thread safety: the initial "is a worker running" check and
         * the stop signal (setting the stop flag, waking the looper) are
         * guarded by the same internal mutex Start() uses, which is
         * released again before this method's own blocking `join()` (or the
         * reentrant case's non-blocking `detach()`) — never held across
         * either, so a reentrant self-stop from the worker's own callback
         * cannot deadlock against it. **Fixed (Task ANDROID-BRIDGE-003,
         * 2026-07-06), previously unsupported:** two or more distinct
         * *external* (non-worker) threads calling `Stop()` concurrently on
         * the same bridge. Only the first caller to claim it actually calls
         * `join()` (if external) or `detach()` (if this is a reentrant
         * self-stop) on the worker thread; every other concurrent caller
         * touches the underlying `std::thread` object at all — mirroring
         * `SensorBase<T>`'s own `ClaimDisposalOnce()`/
         * `WaitForDisposalToComplete()` pattern for the analogous
         * concurrent-`Dispose()` case. Every *external* caller's `Stop()` is
         * synchronous: it does not return until the worker's `Run()` has
         * genuinely finished, not merely until the `std::thread` object has
         * been reclaimed. A reentrant self-stop never waits for that (it
         * cannot — `Run()`, several stack frames below the callback that
         * called `Stop()`, cannot finish until this very call returns).
         *
         * **Fixed (Task ANDROID-BRIDGE-005, 2026-07-16), previously a real
         * bug:** a reentrant self-stop's `detach()` and a concurrent
         * external caller's `join()` could previously race on the same
         * `std::thread` object — one of two harmful outcomes was possible:
         * a `join()` call on an already-detached thread (throws
         * `std::system_error`), or both calling `detach()`/`join()`
         * concurrently on the same object (a data race). Exactly one caller
         * (self or external, whichever reaches the internal mutex first)
         * now claims the sole right to touch the `std::thread` object at
         * all for a given stop cycle; every other caller — including a
         * second, nested reentrant self-stop — never calls `joinable()`,
         * `join()`, or `detach()` on it.
         *
         * @note This bridge's internal worker state (the `Impl` this class
         * privately owns) is kept alive by the worker thread itself for as
         * long as that thread is still running, independent of this
         * `AndroidSensorBridge` wrapper's own lifetime — so destroying (not
         * just Stop()-ping) this object from within its own callback does
         * not use-after-free the worker's own state. It does **not**
         * extend the lifetime of whatever object *owns* this bridge (e.g.
         * `AndroidCompassBackend`, or the `Compass`/`Motion` instance above
         * it) — destroying one of *those* from within this bridge's own
         * callback remains an accepted, unsupported boundary, identical in
         * spirit to Accelerometer's own documented "destroying from within
         * your own callback" limitation.
         */
        void Stop();

        /**
         * @brief Changes the sample interval on an already-running bridge, without requiring Stop()/Start().
         *
         * A safe no-op if this bridge is not currently started — the next
         * Start() call already takes its own explicit interval parameter,
         * so there is nothing useful to stash for later (Task
         * ANDROID-BRIDGE-002). If started, the new interval is handed to
         * the worker thread, which calls `ASensorEventQueue_setEventRate()`
         * again on the live queue from its own thread — the only thread
         * that ever touches the queue, matching `Run()`'s existing
         * single-owner-thread discipline for `queue_`/`sensor_`. Applied
         * on the worker's own next loop iteration (bounded by the same
         * ~100ms `ALooper_pollOnce()` cadence `Stop()`'s signal already
         * relies on), not synchronously with this call returning — this
         * method never blocks.
         *
         * Same non-fatal-rejection handling as the interval passed to
         * Start(): if the platform rejects the new rate,
         * `ASensorEventQueue_setEventRate()`'s negative return is ignored
         * and delivery continues at whatever rate was already in effect,
         * rather than stopping delivery over a rate mismatch alone.
         *
         * @param timeBetweenUpdates New requested sample interval.
         */
        void SetSampleInterval(const System::TimeSpan& timeBetweenUpdates);

        /**
         * @brief Test-only hook (Task ANDR2-009): how many times `Run()`'s inner drain loop hit its per-batch event cap.
         *
         * Counts real backpressure (more events were available than one
         * batch drains before yielding back to the outer loop), not dropped
         * or coalesced samples — every event is still delivered to the
         * caller's callback exactly once; only how many get processed
         * before the outer loop re-checks a pending `SetSampleInterval()`
         * request is bounded. See `Run()`'s own doc comment for the full
         * rationale. Always `0` on a platform with no native backend.
         *
         * @return The number of times the per-batch drain cap was hit.
         */
        [[nodiscard]] int GetDrainBatchLimitHitCountForTesting() const;

        /**
         * @brief Test-only hook (Task ANDR2-010): whether the most recent `ASensorEventQueue_setEventRate()` call succeeded.
         *
         * Covers both the initial rate set at `Start()` time and every
         * subsequent live `SetSampleInterval()`-triggered call — "Rate-change
         * rejection is ignored" (this task's own problem statement) is what
         * this closes: a caller now has a way to discover a rejection that
         * was previously silently absorbed. `true` before any `Start()` call
         * has ever run (nothing has failed yet). Reset to `true` by every
         * `Start()` call, so a stale result from a previous run/session
         * cannot be misread as describing the current one.
         *
         * @return true if the most recent rate-set attempt succeeded (or none has been attempted yet); false if the platform rejected it.
         */
        [[nodiscard]] bool GetLastSetEventRateSucceededForTesting() const;

        /**
         * @brief Test-only hook (Task ANDR2-010): this sensor's own `ASensor_getMinDelay()`, in microseconds.
         *
         * Exposes "effective/min-delay information where available"
         * (required work) — the hardware/driver's own documented minimum
         * delay between events, independent of whatever rate was actually
         * requested. `0` (per the NDK's own documented meaning: "doesn't
         * report events at a constant rate") if no sensor is currently open.
         *
         * @return The sensor's minimum delay between events, in microseconds; `0` if unknown/not currently open.
         */
        [[nodiscard]] int32_t GetMinDelayMicrosecondsForTesting() const;

    private:
        struct Impl;

        /**
         * shared_ptr, not unique_ptr: the worker thread's own lambda
         * captures a copy of this pointer (not `this`), so Impl's lifetime
         * extends for as long as the worker thread is still running, even
         * if this AndroidSensorBridge wrapper is destroyed first (e.g. a
         * reentrant Stop()/destroy from within the bridge's own callback,
         * on its own worker thread) — see Stop()'s own doc comment.
         */
        std::shared_ptr<Impl> impl_;
    };
} // namespace Microsoft::Devices::Sensors::Detail
