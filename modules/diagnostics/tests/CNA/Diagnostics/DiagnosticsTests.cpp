// SPDX-License-Identifier: MS-PL
#include "CNA/Diagnostics/Diagnostics.hpp"
#include "CNA/Diagnostics/Instrumentation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace CNA::Diagnostics;

#if CNA_DIAGNOSTICS_LEVEL >= 1
    class RuntimeModeGuard
    {
    public:
        explicit RuntimeModeGuard(Mode mode) : previous_(GetRuntimeMode())
        {
            EXPECT_TRUE(SetRuntimeMode(mode));
        }

        ~RuntimeModeGuard()
        {
            (void)SetRuntimeMode(previous_);
        }

    private:
        Mode previous_;
    };

    [[nodiscard]] const MetricSample* FindMetric(const Snapshot& snapshot,
                                                  std::string_view name)
    {
        const auto found = std::find_if(snapshot.metrics.begin(), snapshot.metrics.end(),
            [name](const MetricSample& metric) { return metric.name == name; });
        return found == snapshot.metrics.end() ? nullptr : &*found;
    }
#endif

#if CNA_DIAGNOSTICS_LEVEL >= 2
    [[nodiscard]] std::uint64_t NewestSequence()
    {
        return GetProvider().ReadEvents(0, EventHistoryCapacity).newestAvailableSequence;
    }
#endif

    TEST(DiagnosticsBuildModeTest, PublishedModeMatchesTheCompileDefinition)
    {
        EXPECT_EQ(static_cast<int>(GetBuildMode()), CNA_DIAGNOSTICS_LEVEL);
        EXPECT_EQ(IDiagnosticsProvider::InterfaceVersion, 1);
        EXPECT_FALSE(SetRuntimeMode(static_cast<Mode>(CNA_DIAGNOSTICS_LEVEL + 1)));
    }

    TEST(DiagnosticsBuildModeTest, DisabledMacrosDoNotEvaluateArguments)
    {
        int evaluations = 0;
        CNA_DIAGNOSTICS_COUNTER_ADD("Tests/Disabled/Evaluation", ++evaluations);
        CNA_DIAGNOSTICS_GAUGE_SET("Tests/Disabled/GaugeEvaluation", ++evaluations);
        CNA_DIAGNOSTICS_FRAME_COUNTER_ADD("Tests/Disabled/FrameEvaluation", ++evaluations);
        CNA_DIAGNOSTICS_EVENT("Tests/Disabled/EventEvaluation");
#if CNA_DIAGNOSTICS_LEVEL == 0
        EXPECT_EQ(evaluations, 0);
        EXPECT_EQ(GetRuntimeMode(), Mode::Off);
        EXPECT_TRUE(GetProvider().CaptureSnapshot().metrics.empty());
#else
        EXPECT_EQ(evaluations, 3);
#endif
    }

#if CNA_DIAGNOSTICS_LEVEL >= 1
    TEST(DiagnosticsMetricsTest, CountersAndGaugesRetainTheirDocumentedSemantics)
    {
        RuntimeModeGuard mode(Mode::Stats);
        CounterHandle counter("Tests/Metrics/Cumulative");
        GaugeHandle gauge("Tests/Metrics/Gauge", MetricUnit::Bytes, Accuracy::Estimated);
        counter.Add(2);
        counter.Add(5);
        gauge.Set(11);
        gauge.Add(-3);

        const Snapshot snapshot = GetProvider().CaptureSnapshot();
        const MetricSample* counterSample = FindMetric(snapshot, "Tests/Metrics/Cumulative");
        const MetricSample* gaugeSample = FindMetric(snapshot, "Tests/Metrics/Gauge");
        ASSERT_NE(counterSample, nullptr);
        ASSERT_NE(gaugeSample, nullptr);
        EXPECT_EQ(counterSample->value, 7);
        EXPECT_EQ(counterSample->kind, MetricKind::Counter);
        EXPECT_EQ(gaugeSample->value, 8);
        EXPECT_EQ(gaugeSample->unit, MetricUnit::Bytes);
        EXPECT_EQ(gaugeSample->accuracy, Accuracy::Estimated);

        CounterHandle identity("Tests/Metrics/UniqueIdentity");
        GaugeHandle conflictingIdentity("Tests/Metrics/UniqueIdentity");
        EXPECT_NE(identity.GetId(), 0u);
        EXPECT_EQ(conflictingIdentity.GetId(), 0u);
    }

    TEST(DiagnosticsFrameTest, FrameCountersArePublishedAndThenReset)
    {
        RuntimeModeGuard mode(Mode::Stats);
        FrameCounterHandle frameCounter("Tests/Frames/Work");

        BeginFrame();
        frameCounter.Add(9);
        EndFrame();
        Snapshot first = GetProvider().CaptureSnapshot();
        ASSERT_FALSE(first.recentFrames.empty());
        const FrameSample& firstFrame = first.recentFrames.back();
        const auto firstValue = std::find_if(firstFrame.metrics.begin(), firstFrame.metrics.end(),
            [](const MetricSample& metric) { return metric.name == "Tests/Frames/Work"; });
        ASSERT_NE(firstValue, firstFrame.metrics.end());
        EXPECT_EQ(firstValue->value, 9);
        EXPECT_GT(firstFrame.durationNs, 0u);

        BeginFrame();
        EndFrame();
        Snapshot second = GetProvider().CaptureSnapshot();
        ASSERT_FALSE(second.recentFrames.empty());
        const FrameSample& secondFrame = second.recentFrames.back();
        const auto secondValue = std::find_if(secondFrame.metrics.begin(), secondFrame.metrics.end(),
            [](const MetricSample& metric) { return metric.name == "Tests/Frames/Work"; });
        ASSERT_NE(secondValue, secondFrame.metrics.end());
        EXPECT_EQ(secondValue->value, 0);
        EXPECT_GT(secondFrame.frameNumber, firstFrame.frameNumber);
    }

    TEST(DiagnosticsFrameTest, MalformedFrameTransitionsAreReportedSeparately)
    {
        RuntimeModeGuard mode(Mode::Stats);
        const std::uint64_t before = GetProvider().CaptureSnapshot().malformedFrameCount;
        BeginFrame();
        BeginFrame();
        EndFrame();
        EndFrame();
        const Snapshot after = GetProvider().CaptureSnapshot();
        EXPECT_EQ(after.malformedFrameCount, before + 2);
    }

    TEST(DiagnosticsResourceTest, RegistrationUpdateAndUnregistrationUseOneStableId)
    {
        RuntimeModeGuard mode(Mode::Stats);
        ResourceId id = 0;
        {
            ResourceHandle resource(ResourceDescriptor{
                .kind = ResourceKind::Texture2D,
                .label = "test texture",
                .format = "Color",
                .width = 16,
                .height = 8,
                .depth = 1,
                .mipCount = 1,
                .estimatedBytes = 512,
                .byteAccuracy = Accuracy::Estimated});
            id = resource.GetId();
            ASSERT_NE(id, 0u);
            resource.Update(ResourceDescriptor{
                .kind = ResourceKind::RenderTarget2D,
                .label = "test target",
                .format = "Color",
                .width = 16,
                .height = 8,
                .depth = 1,
                .mipCount = 1,
                .estimatedBytes = 1024,
                .byteAccuracy = Accuracy::Estimated});

            const Snapshot snapshot = GetProvider().CaptureSnapshot();
            const auto found = std::find_if(snapshot.resources.begin(), snapshot.resources.end(),
                [id](const ResourceRecord& record) { return record.id == id; });
            ASSERT_NE(found, snapshot.resources.end());
            EXPECT_EQ(found->kind, ResourceKind::RenderTarget2D);
            EXPECT_EQ(found->estimatedBytes, 1024u);
        }
        const Snapshot after = GetProvider().CaptureSnapshot();
        EXPECT_EQ(std::count_if(after.resources.begin(), after.resources.end(),
            [id](const ResourceRecord& record) { return record.id == id; }), 0);
    }

    TEST(DiagnosticsSourceTest, OptionalSourcePublishesOnlyAtFrameBoundaries)
    {
        class Source final : public IDiagnosticsSource
        {
        public:
            void Collect(FrameStatisticsSink& sink) override
            {
                sink.Set(gpuTime, 1234);
                sink.Add(queryResults, 2);
            }

            GaugeHandle gpuTime{"Tests/Source/GpuTime", MetricUnit::Nanoseconds};
            FrameCounterHandle queryResults{"Tests/Source/QueryResults"};
        };

        RuntimeModeGuard mode(Mode::Stats);
        auto source = std::make_shared<Source>();
        SourceRegistration registration = RegisterSource(source);
        ASSERT_TRUE(registration.IsRegistered());
        BeginFrame();
        EndFrame();

        const Snapshot snapshot = GetProvider().CaptureSnapshot();
        const MetricSample* gpuTime = FindMetric(snapshot, "Tests/Source/GpuTime");
        ASSERT_NE(gpuTime, nullptr);
        EXPECT_EQ(gpuTime->value, 1234);
        ASSERT_FALSE(snapshot.recentFrames.empty());
        const auto queryResults = std::find_if(
            snapshot.recentFrames.back().metrics.begin(),
            snapshot.recentFrames.back().metrics.end(),
            [](const MetricSample& metric) { return metric.name == "Tests/Source/QueryResults"; });
        ASSERT_NE(queryResults, snapshot.recentFrames.back().metrics.end());
        EXPECT_EQ(queryResults->value, 2);
        registration.Reset();
        EXPECT_FALSE(registration.IsRegistered());
    }
#endif

#if CNA_DIAGNOSTICS_LEVEL >= 2
    TEST(DiagnosticsZonesTest, NestedZonesRetainParentChildRelationships)
    {
        RuntimeModeGuard mode(Mode::Full);
        const std::uint64_t cursor = NewestSequence();
        const NameHandle outerName("Tests/Zones/Outer");
        const NameHandle innerName("Tests/Zones/Inner");
        {
            ZoneScope outer(outerName, Category::Update);
            ZoneScope inner(innerName, Category::Update);
        }

        const EventBatch batch = GetProvider().ReadEvents(cursor, 16);
        const auto outer = std::find_if(batch.events.begin(), batch.events.end(),
            [&outerName](const EventRecord& event) {
                return event.kind == EventKind::Zone && event.name == outerName.GetId();
            });
        const auto inner = std::find_if(batch.events.begin(), batch.events.end(),
            [&innerName](const EventRecord& event) {
                return event.kind == EventKind::Zone && event.name == innerName.GetId();
            });
        ASSERT_NE(outer, batch.events.end());
        ASSERT_NE(inner, batch.events.end());
        EXPECT_EQ(inner->parentCorrelationId, outer->correlationId);
        EXPECT_EQ(outer->parentCorrelationId, 0u);
        EXPECT_GT(outer->durationNs, 0u);
    }

    TEST(DiagnosticsZonesTest, MalformedManualEndIsReportedWithoutCorruptingTheStack)
    {
        RuntimeModeGuard mode(Mode::Full);
        const std::uint64_t before = GetProvider().CaptureSnapshot().malformedZoneCount;
        const NameHandle name("Tests/Zones/Malformed");
        const ZoneToken token = BeginZone(name);
        ASSERT_TRUE(token.active);
        EXPECT_TRUE(EndZone(token));
        EXPECT_FALSE(EndZone(token));
        EXPECT_EQ(GetProvider().CaptureSnapshot().malformedZoneCount, before + 1);
    }

    TEST(DiagnosticsZonesTest, RuntimeModeTransitionInvalidatesOpenZones)
    {
        RuntimeModeGuard mode(Mode::Full);
        const NameHandle name("Tests/Zones/ModeTransition");
        const ZoneToken token = BeginZone(name);
        ASSERT_TRUE(token.active);
        EXPECT_TRUE(SetRuntimeMode(Mode::Stats));
        EXPECT_TRUE(SetRuntimeMode(Mode::Full));
        EXPECT_TRUE(EndZone(token));

        const ZoneToken next = BeginZone(name);
        ASSERT_TRUE(next.active);
        EXPECT_EQ(next.depth, 0u);
        EXPECT_TRUE(EndZone(next));
    }

    TEST(DiagnosticsEventsTest, MultipleThreadsPublishIntoOneBoundedRecording)
    {
        RuntimeModeGuard mode(Mode::Full);
        const NameHandle marker("Tests/Events/WorkerMarker");
        constexpr int ThreadCount = 4;
        constexpr int EventsPerThread = 64;
        RecordingSession recording = StartRecording(ThreadCount * EventsPerThread + 16);
        ASSERT_TRUE(recording.IsActive());
        std::vector<std::thread> workers;
        for (int thread = 0; thread < ThreadCount; ++thread)
        {
            workers.emplace_back([&marker, thread] {
                for (int event = 0; event < EventsPerThread; ++event)
                    MarkEvent(marker, Category::Application, thread * EventsPerThread + event);
            });
        }
        for (std::thread& worker : workers)
            worker.join();

        const Trace trace = StopRecording(recording);
        EXPECT_EQ(std::count_if(trace.GetEvents().begin(), trace.GetEvents().end(),
            [&marker](const EventRecord& event) { return event.name == marker.GetId(); }),
            ThreadCount * EventsPerThread);
    }

    TEST(DiagnosticsEventsTest, ThreadBufferOverflowDropsNewEventsAndReportsIt)
    {
        RuntimeModeGuard mode(Mode::Full);
        const EventBatch before = GetProvider().ReadEvents(0, EventHistoryCapacity);
        const NameHandle marker("Tests/Events/Overflow");
        for (std::size_t index = 0; index < ThreadEventCapacity + 32; ++index)
            MarkEvent(marker, Category::Application, static_cast<std::int64_t>(index));
        const EventBatch after = GetProvider().ReadEvents(
            before.newestAvailableSequence, EventHistoryCapacity);
        EXPECT_GE(after.producerEventsDropped, before.producerEventsDropped + 32);
    }

    TEST(DiagnosticsEventsTest, ProcessHistoryOverwritesOldestEventsAtItsBound)
    {
        RuntimeModeGuard mode(Mode::Full);
        const std::uint64_t before = GetProvider().CaptureSnapshot().eventHistoryOverwrites;
        const NameHandle marker("Tests/Events/HistoryOverflow");
        for (std::size_t first = 0; first < EventHistoryCapacity + ThreadEventCapacity;
             first += ThreadEventCapacity)
        {
            for (std::size_t offset = 0; offset < ThreadEventCapacity; ++offset)
                MarkEvent(marker, Category::Application,
                          static_cast<std::int64_t>(first + offset));
            (void)GetProvider().ReadEvents(0, 1);
        }
        const Snapshot after = GetProvider().CaptureSnapshot();
        EXPECT_GT(after.eventHistoryOverwrites, before);
        const EventBatch retained = GetProvider().ReadEvents(0, EventHistoryCapacity + 1);
        EXPECT_LE(retained.events.size(), EventHistoryCapacity);
    }

    TEST(DiagnosticsRecordingTest, RecordingIsBoundedAndDropsTheOldestExcessEvents)
    {
        RuntimeModeGuard mode(Mode::Full);
        RecordingSession session = StartRecording(3);
        ASSERT_TRUE(session.IsActive());
        const NameHandle marker("Tests/Recording/Bounded");
        for (int value = 0; value < 5; ++value)
            MarkEvent(marker, Category::Application, value);
        Trace trace = StopRecording(session);
        ASSERT_EQ(trace.GetEvents().size(), 3u);
        EXPECT_GE(trace.GetDroppedEventCount(), 2u);
        EXPECT_EQ(trace.GetEvents().front().value, 2);
        EXPECT_EQ(trace.ResolveName(marker.GetId()), "Tests/Recording/Bounded");
        EXPECT_FALSE(session.IsActive());
    }

    TEST(DiagnosticsRecordingTest, MovingSessionTransfersTheOnlyActiveCursor)
    {
        RuntimeModeGuard mode(Mode::Full);
        RecordingSession original = StartRecording(4);
        ASSERT_TRUE(original.IsActive());
        RecordingSession moved(std::move(original));
        EXPECT_FALSE(original.IsActive());
        EXPECT_TRUE(moved.IsActive());
        const NameHandle marker("Tests/Recording/Moved");
        MarkEvent(marker);
        EXPECT_EQ(StopRecording(moved).GetEvents().size(), 1u);
    }

    TEST(DiagnosticsTraceTest, BinaryAndChromeExportsRoundTripWithoutPerFrameJson)
    {
        RuntimeModeGuard mode(Mode::Full);
        RecordingSession session = StartRecording(8);
        const NameHandle marker("Tests/Trace/RoundTrip");
        MarkEvent(marker, Category::Graphics, 42);
        Trace original = StopRecording(session);

        std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(original.WriteBinary(binary));
        binary.seekg(0);
        Trace decoded = Trace::ReadBinary(binary);
        ASSERT_EQ(decoded.GetEvents().size(), 1u);
        EXPECT_EQ(decoded.GetEvents()[0].value, 42);
        EXPECT_EQ(decoded.ResolveName(decoded.GetEvents()[0].name), "Tests/Trace/RoundTrip");

        std::ostringstream chrome;
        ASSERT_TRUE(decoded.WriteChromeTrace(chrome));
        EXPECT_NE(chrome.str().find("Tests/Trace/RoundTrip"), std::string::npos);
        EXPECT_NE(chrome.str().find("traceEvents"), std::string::npos);
    }

    TEST(DiagnosticsTraceTest, MalformedBinaryInputIsRejected)
    {
        std::istringstream malformed("not-a-cna-trace");
        EXPECT_THROW((void)Trace::ReadBinary(malformed), std::runtime_error);
    }
#endif
}
