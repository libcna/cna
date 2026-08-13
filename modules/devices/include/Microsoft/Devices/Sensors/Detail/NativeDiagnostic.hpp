// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "System/DateTimeOffset.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /** @brief How severe a NativeDiagnosticRecord is (Task DEVPERF-005). */
    enum class NativeDiagnosticSeverity
    {
        /** @brief A native call failed but an equivalent, more specific error signal already exists elsewhere (e.g. a thrown exception or a state transition) -- this record exists purely for observability. */
        Info,
        /** @brief A native call failed and was intentionally ignored: no state/error currently reflects it, and no safe corrective action exists at this call site. */
        Warning,
        /** @brief A native call failed in a way that leaves this backend's own internal state degraded (e.g. a resource left in an unknown state, or a swallowed exception that could have corrupted a dispatch batch). */
        Error,
    };

    /**
     * @brief One recorded native-call failure or swallowed-exception event
     * (Task DEVPERF-005). Backend, operation, native return code/message,
     * device identifier, timestamp and severity -- exactly the fields
     * DEVPERF-005's own required work names ("an internal error record with
     * backend, operation, native code/message, sensor/device id, timestamp
     * and severity").
     */
    struct NativeDiagnosticRecord
    {
        /** @brief Which backend produced this record, e.g. "SDL" or "Android". */
        std::string Backend;

        /** @brief The native API call or internal operation that failed, e.g. "SDL_PlayHapticRumble" or "AndroidSensorBridge::Run callback". */
        std::string Operation;

        /** @brief The native return code, if the underlying API has one; 0 if not applicable (e.g. a swallowed C++ exception has no native code of its own). */
        int NativeCode = 0;

        /** @brief Human-readable detail -- SDL_GetError() text, an exception's what(), or a short fixed description. */
        std::string NativeMessage;

        /** @brief Best-effort identifier of the sensor/haptic device this record is about; empty if none is available at this call site. */
        std::string DeviceId;

        /** @brief Wall-clock time this record was created. */
        System::DateTimeOffset Timestamp;

        /** @brief How severe this event is -- see NativeDiagnosticSeverity's own values. */
        NativeDiagnosticSeverity Severity = NativeDiagnosticSeverity::Warning;
    };

    /**
     * @brief Process-wide sink for NativeDiagnosticRecord events (Task
     * DEVPERF-005). Every method here is safe to call from any thread,
     * including directly from a C callback boundary or a std::thread entry
     * point -- Record() never throws (required work: "expose it through
     * logging and an optional diagnostic callback/counter without throwing
     * across C callbacks").
     */
    class NativeDiagnosticSink
    {
    public:
        /** @brief Signature for an optional, test/diagnostics-only observer callback. */
        using Callback = std::function<void(const NativeDiagnosticRecord&)>;

        /**
         * @brief Records one diagnostic event: logs it (debug builds only,
         * via CNA's process-wide logger), increments the process-wide counter,
         * stores a copy as the last record, and invokes the test/diagnostics
         * callback if one is set. Never throws, regardless of what logging,
         * copying, or the callback itself do internally.
         *
         * @param record The event to record.
         */
        static void Record(NativeDiagnosticRecord record) noexcept;

        /** @brief Test-only hook: total records ever recorded, process-wide, never reset by Record() itself. */
        static std::size_t GetRecordCountForTesting();

        /** @brief Test-only hook: the most recently recorded record's copy; default-constructed if none have been recorded yet. */
        static NativeDiagnosticRecord GetLastRecordForTesting();

        /** @brief Test-only hook: installs an observer invoked synchronously from Record(), after logging/counting. Pass an empty std::function to clear it. */
        static void SetCallbackForTesting(Callback callback);

        /** @brief Test-only hook: resets the counter, last record, and callback back to their initial state. Tests must call this in SetUp()/TearDown() -- state is process-wide, not per-test. */
        static void ResetForTesting();
    };
} // namespace Microsoft::Devices::Sensors::Detail
