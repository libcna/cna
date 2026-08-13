// SPDX-License-Identifier: MS-PL
#include "Microsoft/Devices/Sensors/Detail/NativeDiagnostic.hpp"

#include "CNA/Logger.hpp"

#include <mutex>
#include <string>

namespace Microsoft::Devices::Sensors::Detail
{
    namespace
    {
        std::mutex& StateMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::size_t& RecordCount()
        {
            static std::size_t count = 0;
            return count;
        }

        NativeDiagnosticRecord& LastRecord()
        {
            static NativeDiagnosticRecord record;
            return record;
        }

        NativeDiagnosticSink::Callback& TestCallback()
        {
            static NativeDiagnosticSink::Callback callback;
            return callback;
        }
    } // namespace

    void NativeDiagnosticSink::Record(NativeDiagnosticRecord record) noexcept
    {
        try
        {
#ifndef NDEBUG
            CNA::Logger::Debug(
                "NativeDiagnostic [" + record.Backend + "] " + record.Operation
                + " failed (code " + std::to_string(record.NativeCode) + "): "
                + record.NativeMessage);
#endif

            Callback callbackCopy;
            {
                std::lock_guard<std::mutex> lock(StateMutex());
                ++RecordCount();
                LastRecord() = record;
                callbackCopy = TestCallback();
            }

            if (callbackCopy)
            {
                callbackCopy(record);
            }
        }
        catch (...)
        {
            // Record() must never let an exception escape -- callers include
            // C callback boundaries (SDL_EventFilter, Android NDK sensor
            // callbacks) and std::thread entry points, exactly the hazard
            // this whole mechanism exists to make observable instead of
            // fatal. Nothing further can be done if logging, the copy, or
            // the test callback itself throws.
        }
    }

    std::size_t NativeDiagnosticSink::GetRecordCountForTesting()
    {
        std::lock_guard<std::mutex> lock(StateMutex());
        return RecordCount();
    }

    NativeDiagnosticRecord NativeDiagnosticSink::GetLastRecordForTesting()
    {
        std::lock_guard<std::mutex> lock(StateMutex());
        return LastRecord();
    }

    void NativeDiagnosticSink::SetCallbackForTesting(Callback callback)
    {
        std::lock_guard<std::mutex> lock(StateMutex());
        TestCallback() = std::move(callback);
    }

    void NativeDiagnosticSink::ResetForTesting()
    {
        std::lock_guard<std::mutex> lock(StateMutex());
        RecordCount() = 0;
        LastRecord() = NativeDiagnosticRecord{};
        TestCallback() = nullptr;
    }
} // namespace Microsoft::Devices::Sensors::Detail
