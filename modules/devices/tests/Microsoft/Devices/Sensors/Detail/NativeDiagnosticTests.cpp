// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Microsoft/Devices/Sensors/Detail/NativeDiagnostic.hpp"
#include "System/DateTimeOffset.hpp"

using Microsoft::Devices::Sensors::Detail::NativeDiagnosticRecord;
using Microsoft::Devices::Sensors::Detail::NativeDiagnosticSeverity;
using Microsoft::Devices::Sensors::Detail::NativeDiagnosticSink;

namespace
{
    // Task DEVPERF-005 (2026-07-18, external audit `audit_devices_2026-07-17.md`):
    // NativeDiagnosticSink's state is process-wide (static), not per-instance --
    // every test must start from a known-empty state and clean up after itself,
    // or an earlier test's leftover record/callback would silently leak into a
    // later, unrelated test's assertions.
    class NativeDiagnosticSinkTest : public ::testing::Test
    {
    protected:
        void SetUp() override { NativeDiagnosticSink::ResetForTesting(); }
        void TearDown() override { NativeDiagnosticSink::ResetForTesting(); }
    };
} // namespace

TEST_F(NativeDiagnosticSinkTest, StartsWithZeroRecordsAndADefaultLastRecord)
{
    EXPECT_EQ(NativeDiagnosticSink::GetRecordCountForTesting(), 0u);
    const NativeDiagnosticRecord last = NativeDiagnosticSink::GetLastRecordForTesting();
    EXPECT_TRUE(last.Backend.empty());
    EXPECT_TRUE(last.Operation.empty());
    EXPECT_EQ(last.NativeCode, 0);
}

TEST_F(NativeDiagnosticSinkTest, RecordIncrementsTheCounter)
{
    NativeDiagnosticRecord record;
    record.Backend = "SDL";
    record.Operation = "PlayHapticRumble";
    EXPECT_NO_THROW(NativeDiagnosticSink::Record(record));
    EXPECT_EQ(NativeDiagnosticSink::GetRecordCountForTesting(), 1u);

    EXPECT_NO_THROW(NativeDiagnosticSink::Record(record));
    EXPECT_EQ(NativeDiagnosticSink::GetRecordCountForTesting(), 2u);
}

TEST_F(NativeDiagnosticSinkTest, RecordStoresAnExactCopyAsTheLastRecord)
{
    NativeDiagnosticRecord record;
    record.Backend = "Android";
    record.Operation = "ASensorEventQueue_getEvents";
    record.NativeCode = -11;
    record.NativeMessage = "EAGAIN";
    record.DeviceId = "sensor-type-1";
    record.Timestamp = System::DateTimeOffset::getUtcNowProperty();
    record.Severity = NativeDiagnosticSeverity::Error;

    NativeDiagnosticSink::Record(record);

    const NativeDiagnosticRecord last = NativeDiagnosticSink::GetLastRecordForTesting();
    EXPECT_EQ(last.Backend, "Android");
    EXPECT_EQ(last.Operation, "ASensorEventQueue_getEvents");
    EXPECT_EQ(last.NativeCode, -11);
    EXPECT_EQ(last.NativeMessage, "EAGAIN");
    EXPECT_EQ(last.DeviceId, "sensor-type-1");
    EXPECT_EQ(last.Timestamp, record.Timestamp);
    EXPECT_EQ(last.Severity, NativeDiagnosticSeverity::Error);
}

TEST_F(NativeDiagnosticSinkTest, LastRecordReflectsTheMostRecentCallNotTheFirst)
{
    NativeDiagnosticRecord first;
    first.Operation = "first";
    NativeDiagnosticSink::Record(first);

    NativeDiagnosticRecord second;
    second.Operation = "second";
    NativeDiagnosticSink::Record(second);

    EXPECT_EQ(NativeDiagnosticSink::GetLastRecordForTesting().Operation, "second");
    EXPECT_EQ(NativeDiagnosticSink::GetRecordCountForTesting(), 2u);
}

TEST_F(NativeDiagnosticSinkTest, CallbackReceivesTheExactRecord)
{
    bool invoked = false;
    NativeDiagnosticRecord observed;
    NativeDiagnosticSink::SetCallbackForTesting(
        [&invoked, &observed](const NativeDiagnosticRecord& record)
        {
            invoked = true;
            observed = record;
        });

    NativeDiagnosticRecord record;
    record.Backend = "SDL";
    record.Operation = "EnumerateHaptics";
    record.NativeCode = 7;
    NativeDiagnosticSink::Record(record);

    ASSERT_TRUE(invoked);
    EXPECT_EQ(observed.Backend, "SDL");
    EXPECT_EQ(observed.Operation, "EnumerateHaptics");
    EXPECT_EQ(observed.NativeCode, 7);
}

TEST_F(NativeDiagnosticSinkTest, ClearingTheCallbackWithAnEmptyStdFunctionStopsInvocations)
{
    int invokeCount = 0;
    NativeDiagnosticSink::SetCallbackForTesting(
        [&invokeCount](const NativeDiagnosticRecord&) { ++invokeCount; });

    NativeDiagnosticSink::Record(NativeDiagnosticRecord{});
    EXPECT_EQ(invokeCount, 1);

    NativeDiagnosticSink::SetCallbackForTesting(nullptr);
    NativeDiagnosticSink::Record(NativeDiagnosticRecord{});
    EXPECT_EQ(invokeCount, 1); // No further invocation.
}

// A throwing test callback must not escape Record() -- Record() is documented
// (and typed noexcept) to never let an exception cross a caller that may
// itself be a C callback boundary or std::thread entry point.
TEST_F(NativeDiagnosticSinkTest, RecordDoesNotThrowEvenWhenTheTestCallbackThrows)
{
    NativeDiagnosticSink::SetCallbackForTesting(
        [](const NativeDiagnosticRecord&) { throw std::runtime_error("boom"); });

    EXPECT_NO_THROW(NativeDiagnosticSink::Record(NativeDiagnosticRecord{}));
    // The throw happened after the counter/last-record update (Record()'s own
    // ordering: log, then update state, then invoke the callback), so both
    // still reflect this call.
    EXPECT_EQ(NativeDiagnosticSink::GetRecordCountForTesting(), 1u);
}

TEST_F(NativeDiagnosticSinkTest, ResetForTestingClearsCounterLastRecordAndCallback)
{
    int invokeCount = 0;
    NativeDiagnosticSink::SetCallbackForTesting(
        [&invokeCount](const NativeDiagnosticRecord&) { ++invokeCount; });

    NativeDiagnosticRecord record;
    record.Operation = "before-reset";
    NativeDiagnosticSink::Record(record);
    ASSERT_EQ(NativeDiagnosticSink::GetRecordCountForTesting(), 1u);
    ASSERT_EQ(invokeCount, 1);

    NativeDiagnosticSink::ResetForTesting();

    EXPECT_EQ(NativeDiagnosticSink::GetRecordCountForTesting(), 0u);
    EXPECT_TRUE(NativeDiagnosticSink::GetLastRecordForTesting().Operation.empty());

    NativeDiagnosticSink::Record(NativeDiagnosticRecord{});
    EXPECT_EQ(invokeCount, 1); // Callback was cleared by ResetForTesting() too.
}

// Task DEVPERF-005: Record() is documented as safe to call from any thread,
// including concurrently -- proves that empirically rather than just by
// reasoning about the mutex-guarded state it touches.
TEST_F(NativeDiagnosticSinkTest, ConcurrentRecordCallsFromMultipleThreadsDoNotCrashOrLoseCounts)
{
    constexpr int ThreadCount = 8;
    constexpr int RecordsPerThread = 200;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);
    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back(
            [t]()
            {
                for (int i = 0; i < RecordsPerThread; ++i)
                {
                    NativeDiagnosticRecord record;
                    record.Backend = "Test";
                    record.Operation = "ConcurrentRecord";
                    record.NativeCode = t;
                    NativeDiagnosticSink::Record(record);
                }
            });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(
        NativeDiagnosticSink::GetRecordCountForTesting(),
        static_cast<std::size_t>(ThreadCount) * static_cast<std::size_t>(RecordsPerThread));
}
