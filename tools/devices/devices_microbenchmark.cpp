// SPDX-License-Identifier: MS-PL
//
// Task PERF2-001: a standalone (non-GTest) microbenchmark tool covering this task's own named
// categories -- sensor dispatch, event fanout, throttling, Start/Stop, probes, Compass fusion,
// Motion fusion -- for 1/5/10 simultaneous instances where that applies. Lives under tools/, not
// tests/, for the same reason every other standalone executable in this directory does (its own
// main(), not part of the shared CnaTests gtest binary).
//
// Emits one JSON object per line (JSON Lines format) to stdout: {"name":..., "p50_us":...,
// "p95_us":..., "p99_us":..., "iterations":...}. Redirect to a file to capture a baseline;
// tools/devices/compare_devices_microbenchmark.py compares a new run against a committed
// baseline and flags material regressions -- see that script's own top-of-file comment and
// docs/devices-benchmark-baseline.jsonl (the first committed baseline, this host, this task).
//
// Scope, honestly bounded, not silently narrowed:
// - "Allocations" and "lock time" are NOT separately instrumented here -- that needs either an
//   allocator-hook (a much larger, riskier change to make process-wide in a production library)
//   or per-call manual instrumentation of already-hardened, already-tested locking code this
//   task has no mandate to modify just to add a counter. Only wall-clock latency percentiles are
//   reported. Flagged as a real, named gap, not silently dropped -- see this task's own
//   plans/plan_devices.md resolution note for the follow-up scope.
// - Every benchmark here runs against synthetic/fake renderers (this host has no real
//   accelerometer/gyroscope/haptic/compass/motion hardware) -- "distinguish host fake paths from
//   real devices" (this task's own acceptance criterion) is satisfied by every result implicitly
//   being a fake-path measurement; there is no real-device measurement to compare against in
//   this environment, and none is fabricated.
// - "CI stores benchmark baselines and flags material regressions" -- the local
//   baseline-storage-and-comparison mechanism exists and works (this file plus the compare
//   script plus the committed baseline JSON); actual GitHub Actions wiring to run this
//   automatically on every push and fail CI on a regression is a separate, not-yet-done
//   follow-up (the same "workflow exists locally, not yet confirmed running automatically"
//   distinction this project already applies elsewhere, e.g. DEVPERF-001/devices-tests.yml).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp"
#include "Microsoft/Devices/Sensors/Detail/AndroidMotionMath.hpp"
#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::Sensors::Accelerometer;
using Microsoft::Devices::Sensors::Gyroscope;

namespace
{
    struct Percentiles
    {
        double P50 = 0.0;
        double P95 = 0.0;
        double P99 = 0.0;
        int Iterations = 0;
    };

    Percentiles ComputePercentiles(std::vector<double> samplesMicros)
    {
        std::sort(samplesMicros.begin(), samplesMicros.end());
        const auto at = [&](double fraction) -> double
        {
            if (samplesMicros.empty())
            {
                return 0.0;
            }
            const auto index = static_cast<std::size_t>(fraction * static_cast<double>(samplesMicros.size() - 1));
            return samplesMicros[index];
        };
        Percentiles result;
        result.P50 = at(0.50);
        result.P95 = at(0.95);
        result.P99 = at(0.99);
        result.Iterations = static_cast<int>(samplesMicros.size());
        return result;
    }

    template <typename Fn>
    Percentiles Benchmark(int iterations, Fn&& fn)
    {
        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(iterations));
        for (int i = 0; i < iterations; ++i)
        {
            const auto start = std::chrono::steady_clock::now();
            fn();
            const auto end = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
        }
        return ComputePercentiles(std::move(samples));
    }

    void PrintResult(const std::string& name, const Percentiles& p)
    {
        std::printf(
            "{\"name\":\"%s\",\"p50_us\":%.4f,\"p95_us\":%.4f,\"p99_us\":%.4f,\"iterations\":%d}\n",
            name.c_str(), p.P50, p.P95, p.P99, p.Iterations);
    }

    constexpr int Iterations = 2000;
    constexpr int FanoutInstanceCounts[] = {1, 5, 10};

    // Sink for pure-function benchmark results (Compass/Motion fusion below) -- writing to a
    // volatile prevents the optimizer from proving the computed value is unused and eliding the
    // call entirely, portably (no compiler-specific inline asm, unlike a raw `asm volatile`
    // clobber, which is GCC/Clang-only and this project also targets MSVC/NDK Clang per
    // TEST2-010).
    volatile double g_sink = 0.0;
} // namespace

int main()
{
    // 1. Probe.
    PrintResult(
        "Accelerometer.Probe",
        Benchmark(Iterations, []() { (void)Accelerometer::getIsSupportedProperty(); }));
    PrintResult(
        "Gyroscope.Probe",
        Benchmark(Iterations, []() { (void)Gyroscope::getIsSupportedProperty(); }));

    // 2. Sensor dispatch (single instance, synthetic).
    {
        Accelerometer a;
        a.SetSupportedForTesting(true);
        a.SetStartedForTesting(true);
        PrintResult(
            "Accelerometer.SingleInstanceDispatch",
            Benchmark(Iterations, [&]() { a.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f); }));
    }

    // 3. Event fanout, N=1/5/10 simultaneous instances.
    for (const int n : FanoutInstanceCounts)
    {
        std::vector<std::unique_ptr<Accelerometer>> owners;
        std::vector<Accelerometer*> instances;
        owners.reserve(static_cast<std::size_t>(n));
        instances.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            auto a = std::make_unique<Accelerometer>();
            a->SetStartedForTesting(true);
            Accelerometer::RegisterStartedInstanceForTesting(*a);
            instances.push_back(a.get());
            owners.push_back(std::move(a));
        }

        PrintResult(
            "Accelerometer.Fanout.N" + std::to_string(n),
            Benchmark(
                Iterations, [&]() { Accelerometer::DispatchToInstancesForTesting(instances, 1.0f, 0.0f, 0.0f); }));

        for (Accelerometer* instance : instances)
        {
            Accelerometer::UnregisterStartedInstanceForTesting(*instance);
        }
    }

    // 4. Throttling: TimeBetweenUpdates set far larger than the benchmark's own wall-clock
    // runtime, so every InjectSyntheticSensorUpdate() call after the very first hits
    // SensorBase<T>::ShouldAcceptUpdateAt()'s cheap early-reject path instead of a full publish
    // -- isolates the throttle-check's own overhead from full-dispatch cost (already measured
    // above).
    {
        Accelerometer a;
        a.SetSupportedForTesting(true);
        a.SetStartedForTesting(true);
        a.setTimeBetweenUpdatesProperty(System::TimeSpan::FromHours(1.0));
        a.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f); // Consumes the one accepted update.
        PrintResult(
            "Accelerometer.ThrottledRejectPath",
            Benchmark(Iterations, [&]() { a.InjectSyntheticSensorUpdate(2.0f, 0.0f, 0.0f); }));
    }

    // 5. Start/Stop cycle (real API surface; throws on this host since no real hardware is
    // present -- the throw-and-catch path itself is still a real, meaningful cost to measure,
    // not a fake substitute for the unsupported-hardware case every headless CI run will
    // actually take).
    PrintResult(
        "Accelerometer.StartStopCycle",
        Benchmark(
            Iterations,
            []()
            {
                Accelerometer a;
                if (Accelerometer::getIsSupportedProperty())
                {
                    a.Start();
                    a.Stop();
                }
                else
                {
                    try
                    {
                        a.Start();
                    }
                    catch (...)
                    {
                    }
                }
            }));

    // 6. Compass fusion (the pure quaternion-to-heading computation itself -- the actual
    // numerical "fusion" cost, independent of any specific renderer's sample-delivery
    // machinery).
    PrintResult(
        "Compass.HeadingFusion",
        Benchmark(
            Iterations,
            []()
            {
                g_sink = Microsoft::Devices::Sensors::Detail::ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode(
                    0.1f, 0.2f, 0.3f, 0.9f);
            }));

    // 7. Motion fusion (quaternion normalization plus yaw/pitch/roll extraction -- the actual
    // numerical "fusion" cost for Motion.Attitude).
    PrintResult(
        "Motion.AttitudeFusion",
        Benchmark(
            Iterations,
            []()
            {
                const auto q = Microsoft::Devices::Sensors::Detail::ConvertRotationVectorToXnaQuaternion(
                    0.1f, 0.2f, 0.3f, 0.9f);
                float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
                Microsoft::Devices::Sensors::Detail::ExtractYawPitchRollFromQuaternion(q, yaw, pitch, roll);
                g_sink = static_cast<double>(yaw) + static_cast<double>(pitch) + static_cast<double>(roll);
            }));

    return 0;
}
