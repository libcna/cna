// SPDX-License-Identifier: MS-PL
#include "CNA/Diagnostics/Diagnostics.hpp"
#include "CNA/Diagnostics/Instrumentation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
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

    [[nodiscard]] std::uint64_t SaturatingResourceTotal(const Snapshot& snapshot)
    {
        std::uint64_t total = 0;
        for (const ResourceRecord& resource : snapshot.resources)
        {
            if (total > std::numeric_limits<std::uint64_t>::max() - resource.estimatedBytes)
                return std::numeric_limits<std::uint64_t>::max();
            total += resource.estimatedBytes;
        }
        return total;
    }

    [[nodiscard]] bool IsValidUtf8Text(std::string_view value)
    {
        std::size_t index = 0;
        while (index < value.size())
        {
            const auto first = static_cast<unsigned char>(value[index]);
            std::size_t length = 0;
            unsigned char secondMinimum = 0x80U;
            unsigned char secondMaximum = 0xBFU;
            if (first < 0x80U) length = 1;
            else if (first >= 0xC2U && first <= 0xDFU) length = 2;
            else if (first == 0xE0U) { length = 3; secondMinimum = 0xA0U; }
            else if (first >= 0xE1U && first <= 0xECU) length = 3;
            else if (first == 0xEDU) { length = 3; secondMaximum = 0x9FU; }
            else if (first >= 0xEEU && first <= 0xEFU) length = 3;
            else if (first == 0xF0U) { length = 4; secondMinimum = 0x90U; }
            else if (first >= 0xF1U && first <= 0xF3U) length = 4;
            else if (first == 0xF4U) { length = 4; secondMaximum = 0x8FU; }
            else return false;
            if (index + length > value.size()) return false;
            for (std::size_t offset = 1; offset < length; ++offset)
            {
                const auto continuation = static_cast<unsigned char>(value[index + offset]);
                const unsigned char minimum = offset == 1 ? secondMinimum : 0x80U;
                const unsigned char maximum = offset == 1 ? secondMaximum : 0xBFU;
                if (continuation < minimum || continuation > maximum) return false;
            }
            index += length;
        }
        return true;
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

    TEST(DiagnosticsFrameTest, FrameHistoryIsBoundedOrderedAndReportsExactFps)
    {
        RuntimeModeGuard mode(Mode::Stats);
        for (std::size_t frame = 0; frame < FrameHistoryCapacity + 5; ++frame)
        {
            FrameScope scope;
        }

        const Snapshot snapshot = GetProvider().CaptureSnapshot();
        ASSERT_EQ(snapshot.recentFrames.size(), FrameHistoryCapacity);
        for (std::size_t index = 1; index < snapshot.recentFrames.size(); ++index)
        {
            EXPECT_LT(snapshot.recentFrames[index - 1].frameNumber,
                      snapshot.recentFrames[index].frameNumber);
        }
        const FrameSample& latest = snapshot.recentFrames.back();
        const double expectedFps = latest.durationNs == 0
            ? 0.0 : 1'000'000'000.0 / static_cast<double>(latest.durationNs);
        EXPECT_DOUBLE_EQ(latest.framesPerSecond, expectedFps);
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

    TEST(DiagnosticsResourceTest, RepeatedLifecycleReturnsCountAndBytesToBaseline)
    {
        RuntimeModeGuard mode(Mode::Stats);
        const Snapshot baseline = GetProvider().CaptureSnapshot();
        ResourceId previousId = 0;
        for (int iteration = 0; iteration < 2'000; ++iteration)
        {
            ResourceHandle resource(ResourceDescriptor{
                .kind = ResourceKind::Texture2D,
                .label = "stress texture",
                .format = "Color",
                .width = 32,
                .height = 16,
                .depth = 1,
                .mipCount = 1,
                .estimatedBytes = 2048,
                .byteAccuracy = Accuracy::Estimated});
            ASSERT_GT(resource.GetId(), previousId);
            previousId = resource.GetId();
            resource.Update(ResourceDescriptor{
                .kind = ResourceKind::RenderTarget2D,
                .label = "updated stress texture",
                .format = "Color",
                .width = 32,
                .height = 16,
                .depth = 1,
                .mipCount = 1,
                .estimatedBytes = 4096,
                .byteAccuracy = Accuracy::Estimated});
        }

        const Snapshot after = GetProvider().CaptureSnapshot();
        EXPECT_EQ(after.resources.size(), baseline.resources.size());
        EXPECT_EQ(after.registeredResourceBytes, baseline.registeredResourceBytes);
    }

    TEST(DiagnosticsResourceTest, SaturatedByteTotalRecoversAfterUpdatesAndDestruction)
    {
        RuntimeModeGuard mode(Mode::Stats);
        const Snapshot baseline = GetProvider().CaptureSnapshot();
        ASSERT_LT(baseline.registeredResourceBytes,
                  std::numeric_limits<std::uint64_t>::max());
        ResourceHandle maximum(ResourceDescriptor{
            .kind = ResourceKind::Custom,
            .label = "maximum",
            .format = {},
            .width = 0,
            .height = 0,
            .depth = 0,
            .mipCount = 0,
            .estimatedBytes = std::numeric_limits<std::uint64_t>::max(),
            .byteAccuracy = Accuracy::Estimated});
        ResourceHandle extra(ResourceDescriptor{
            .kind = ResourceKind::Custom,
            .label = "extra",
            .format = {},
            .width = 0,
            .height = 0,
            .depth = 0,
            .mipCount = 0,
            .estimatedBytes = 1,
            .byteAccuracy = Accuracy::Exact});
        EXPECT_EQ(GetProvider().CaptureSnapshot().registeredResourceBytes,
                  std::numeric_limits<std::uint64_t>::max());

        maximum.Update(ResourceDescriptor{
            .kind = ResourceKind::Custom,
            .label = "reduced",
            .format = {},
            .width = 0,
            .height = 0,
            .depth = 0,
            .mipCount = 0,
            .estimatedBytes = 4,
            .byteAccuracy = Accuracy::Exact});
        EXPECT_EQ(GetProvider().CaptureSnapshot().registeredResourceBytes,
                  baseline.registeredResourceBytes + 5);
        extra.Reset();
        maximum.Reset();
        EXPECT_EQ(GetProvider().CaptureSnapshot().registeredResourceBytes,
                  baseline.registeredResourceBytes);
    }

    TEST(DiagnosticsResourceTest, MoveOperationsTransferOneLiveRegistration)
    {
        RuntimeModeGuard mode(Mode::Stats);
        const Snapshot baseline = GetProvider().CaptureSnapshot();
        ResourceHandle first(ResourceDescriptor{
            .kind = ResourceKind::Custom,
            .label = "first",
            .format = {},
            .width = 0,
            .height = 0,
            .depth = 0,
            .mipCount = 0,
            .estimatedBytes = 10,
            .byteAccuracy = Accuracy::Unavailable});
        const ResourceId retainedId = first.GetId();
        ResourceHandle second(std::move(first));
        EXPECT_EQ(first.GetId(), 0u);
        EXPECT_EQ(second.GetId(), retainedId);

        ResourceHandle replaced(ResourceDescriptor{
            .kind = ResourceKind::Custom,
            .label = "replaced",
            .format = {},
            .width = 0,
            .height = 0,
            .depth = 0,
            .mipCount = 0,
            .estimatedBytes = 20,
            .byteAccuracy = Accuracy::Unavailable});
        const ResourceId removedId = replaced.GetId();
        replaced = std::move(second);
        EXPECT_EQ(second.GetId(), 0u);
        EXPECT_EQ(replaced.GetId(), retainedId);
        const Snapshot moved = GetProvider().CaptureSnapshot();
        EXPECT_EQ(std::count_if(moved.resources.begin(), moved.resources.end(),
            [retainedId](const ResourceRecord& record) { return record.id == retainedId; }), 1);
        EXPECT_EQ(std::count_if(moved.resources.begin(), moved.resources.end(),
            [removedId](const ResourceRecord& record) { return record.id == removedId; }), 0);

        replaced.Reset();
        const Snapshot after = GetProvider().CaptureSnapshot();
        EXPECT_EQ(after.resources.size(), baseline.resources.size());
        EXPECT_EQ(after.registeredResourceBytes, baseline.registeredResourceBytes);
    }

    TEST(DiagnosticsResourceTest, ConcurrentSnapshotsMatchLiveResourceMetadata)
    {
        RuntimeModeGuard mode(Mode::Stats);
        const Snapshot baseline = GetProvider().CaptureSnapshot();
        std::barrier start(2);
        std::atomic<bool> finished{false};
        std::thread worker([&] {
            start.arrive_and_wait();
            for (int iteration = 0; iteration < 2'000; ++iteration)
            {
                ResourceHandle resource(ResourceDescriptor{
                    .kind = ResourceKind::Custom,
                    .label = "concurrent",
                    .format = {},
                    .width = 0,
                    .height = 0,
                    .depth = 0,
                    .mipCount = 0,
                    .estimatedBytes = static_cast<std::uint64_t>(iteration + 1),
                    .byteAccuracy = Accuracy::Exact});
                resource.Update(ResourceDescriptor{
                    .kind = ResourceKind::Custom,
                    .label = "concurrent updated",
                    .format = {},
                    .width = 0,
                    .height = 0,
                    .depth = 0,
                    .mipCount = 0,
                    .estimatedBytes = static_cast<std::uint64_t>(iteration + 2),
                    .byteAccuracy = Accuracy::Exact});
                std::this_thread::yield();
            }
            finished.store(true, std::memory_order_release);
        });
        start.arrive_and_wait();
        std::size_t snapshots = 0;
        do
        {
            const Snapshot snapshot = GetProvider().CaptureSnapshot();
            EXPECT_EQ(snapshot.registeredResourceBytes, SaturatingResourceTotal(snapshot));
            ++snapshots;
        }
        while (!finished.load(std::memory_order_acquire));
        worker.join();

        EXPECT_GT(snapshots, 0u);
        const Snapshot after = GetProvider().CaptureSnapshot();
        EXPECT_EQ(after.resources.size(), baseline.resources.size());
        EXPECT_EQ(after.registeredResourceBytes, baseline.registeredResourceBytes);
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

    TEST(DiagnosticsSourceTest, InvalidAndOffFrameEndsDoNotCollectSources)
    {
        class Source final : public IDiagnosticsSource
        {
        public:
            void Collect(FrameStatisticsSink&) override { ++calls; }
            int calls = 0;
        };

        RuntimeModeGuard mode(Mode::Stats);
        auto source = std::make_shared<Source>();
        SourceRegistration registration = RegisterSource(source);
        const std::uint64_t malformedBefore =
            GetProvider().CaptureSnapshot().malformedFrameCount;
        EndFrame();
        EXPECT_EQ(source->calls, 0);
        EXPECT_EQ(GetProvider().CaptureSnapshot().malformedFrameCount, malformedBefore + 1);

        ASSERT_TRUE(SetRuntimeMode(Mode::Off));
        BeginFrame();
        EndFrame();
        EXPECT_EQ(source->calls, 0);
        EXPECT_EQ(GetProvider().CaptureSnapshot().malformedFrameCount, malformedBefore + 1);

        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        BeginFrame();
        EndFrame();
        EXPECT_EQ(source->calls, 1);
    }

    TEST(DiagnosticsSourceTest, ASourceEndingAFrameFromCollectIsRefusedNotDeadlocked)
    {
        // Collect() runs while frame completion holds its non-recursive mutex, so a source that
        // ends a frame from inside it used to block forever on that same thread.
        class Source final : public IDiagnosticsSource
        {
        public:
            void Collect(FrameStatisticsSink&) override
            {
                ++calls;
                EndFrame();
            }
            int calls = 0;
        };

        RuntimeModeGuard mode(Mode::Stats);
        auto source = std::make_shared<Source>();
        SourceRegistration registration = RegisterSource(source);
        const std::uint64_t malformedBefore =
            GetProvider().CaptureSnapshot().malformedFrameCount;
        const std::uint64_t frameBefore = GetProvider().CaptureSnapshot().currentFrameNumber;

        BeginFrame();
        EndFrame();

        EXPECT_EQ(source->calls, 1);
        EXPECT_EQ(GetProvider().CaptureSnapshot().malformedFrameCount, malformedBefore + 1)
            << "the nested end is reported like any other malformed transition";
        // The outer frame still completes, and frame accounting keeps working afterwards.
        EXPECT_EQ(GetProvider().CaptureSnapshot().currentFrameNumber, frameBefore + 1);
        BeginFrame();
        EndFrame();
        EXPECT_EQ(source->calls, 2);
    }

    TEST(DiagnosticsMetricsTest, InvalidUtf8MetricAndResourceNamesAreStoredValid)
    {
        RuntimeModeGuard mode(Mode::Stats);
        const CounterHandle counter("Tests/Metrics/Bad\xFFName");
        counter.Add(1);
        ResourceDescriptor descriptor;
        descriptor.kind = ResourceKind::Custom;
        descriptor.label = "Tests/Resources/Bad\xC3";
        descriptor.format = "Fmt\xED\xA0\x80";
        const ResourceHandle handle(descriptor);
        const ResourceId id = handle.GetId();
        ASSERT_NE(id, 0U);

        const Snapshot snapshot = GetProvider().CaptureSnapshot();
        const MetricSample* metric = FindMetric(snapshot, "Tests/Metrics/Bad\xEF\xBF\xBDName");
        ASSERT_NE(metric, nullptr) << "the invalid byte is stored as U+FFFD";
        EXPECT_TRUE(IsValidUtf8Text(metric->name));
        const auto resource = std::find_if(snapshot.resources.begin(), snapshot.resources.end(),
            [id](const ResourceRecord& record) { return record.id == id; });
        ASSERT_NE(resource, snapshot.resources.end());
        EXPECT_TRUE(IsValidUtf8Text(resource->label));
        EXPECT_TRUE(IsValidUtf8Text(resource->format));
    }

    TEST(DiagnosticsRuntimeModeTest, OffStatsAndFullTransitionsPreserveMetricSemantics)
    {
        RuntimeModeGuard mode(Mode::Stats);
        CounterHandle counter("Tests/Modes/Cumulative");

        ASSERT_TRUE(SetRuntimeMode(Mode::Off));
        counter.Add(11);
        {
            FrameScope frame;
        }

        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        counter.Add(2);
        {
            FrameScope frame;
        }
#if CNA_DIAGNOSTICS_LEVEL >= 2
        ASSERT_TRUE(SetRuntimeMode(Mode::Full));
        counter.Add(3);
        {
            FrameScope frame;
        }
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        ASSERT_TRUE(SetRuntimeMode(Mode::Full));
        ASSERT_TRUE(SetRuntimeMode(Mode::Off));
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
#endif

        const Snapshot snapshot = GetProvider().CaptureSnapshot();
        const MetricSample* sample = FindMetric(snapshot, "Tests/Modes/Cumulative");
        ASSERT_NE(sample, nullptr);
#if CNA_DIAGNOSTICS_LEVEL >= 2
        EXPECT_EQ(sample->value, 5);
#else
        EXPECT_EQ(sample->value, 2);
#endif
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

    TEST(DiagnosticsZonesTest, RepeatedModeCyclesResetAWaitingWorkerContext)
    {
        RuntimeModeGuard mode(Mode::Full);
        const NameHandle marker("Tests/Modes/WaitingWorker");
        const NameHandle outerName("Tests/Modes/WorkerOuter");
        const NameHandle innerName("Tests/Modes/WorkerInner");
        std::barrier ready(2);
        std::barrier release(2);
        RecordingSession recording = StartRecording(8);
        std::thread worker([&] {
            MarkEvent(marker);
            ready.arrive_and_wait();
            release.arrive_and_wait();
            ZoneScope outer(outerName);
            ZoneScope inner(innerName);
        });
        ready.arrive_and_wait();
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        ASSERT_TRUE(SetRuntimeMode(Mode::Full));
        ASSERT_TRUE(SetRuntimeMode(Mode::Off));
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        ASSERT_TRUE(SetRuntimeMode(Mode::Full));
        release.arrive_and_wait();
        worker.join();

        const Trace trace = StopRecording(recording);
        const auto outer = std::find_if(trace.GetEvents().begin(), trace.GetEvents().end(),
            [&outerName](const EventRecord& event) { return event.name == outerName.GetId(); });
        const auto inner = std::find_if(trace.GetEvents().begin(), trace.GetEvents().end(),
            [&innerName](const EventRecord& event) { return event.name == innerName.GetId(); });
        ASSERT_NE(outer, trace.GetEvents().end());
        ASSERT_NE(inner, trace.GetEvents().end());
        EXPECT_EQ(outer->parentCorrelationId, 0u);
        EXPECT_EQ(inner->parentCorrelationId, outer->correlationId);
        EXPECT_EQ(inner->threadId, outer->threadId);
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

    TEST(DiagnosticsEventsTest, SequentialThreadContextsRetainProcessUniqueIdentities)
    {
        RuntimeModeGuard mode(Mode::Full);
        const NameHandle marker("Tests/Events/SequentialThreadIdentity");
        constexpr int ThreadCount = 256;
        RecordingSession recording = StartRecording(ThreadCount);
        ASSERT_TRUE(recording.IsActive());
        for (int thread = 0; thread < ThreadCount; ++thread)
        {
            std::thread worker([&marker, thread] {
                MarkEvent(marker, Category::Application, thread);
            });
            worker.join();
        }

        const Trace trace = StopRecording(recording);
        std::unordered_set<std::uint64_t> threadIds;
        for (const EventRecord& event : trace.GetEvents())
        {
            if (event.name == marker.GetId())
            {
                EXPECT_NE(event.threadId, 0u);
                threadIds.insert(event.threadId);
            }
        }
        EXPECT_EQ(trace.GetEvents().size(), ThreadCount);
        EXPECT_EQ(threadIds.size(), ThreadCount);
    }

    TEST(DiagnosticsEventsTest, ConcurrentThreadContextsHaveDistinctIdentities)
    {
        RuntimeModeGuard mode(Mode::Full);
        const NameHandle marker("Tests/Events/ConcurrentThreadIdentity");
        constexpr int ThreadCount = 64;
        std::barrier ready(ThreadCount);
        RecordingSession recording = StartRecording(ThreadCount);
        ASSERT_TRUE(recording.IsActive());
        std::vector<std::thread> workers;
        workers.reserve(ThreadCount);
        for (int thread = 0; thread < ThreadCount; ++thread)
        {
            workers.emplace_back([&marker, &ready, thread] {
                ready.arrive_and_wait();
                MarkEvent(marker, Category::Application, thread);
            });
        }
        for (std::thread& worker : workers)
            worker.join();

        const Trace trace = StopRecording(recording);
        std::unordered_set<std::uint64_t> threadIds;
        for (const EventRecord& event : trace.GetEvents())
        {
            if (event.name == marker.GetId())
                threadIds.insert(event.threadId);
        }
        EXPECT_EQ(trace.GetEvents().size(), ThreadCount);
        EXPECT_EQ(threadIds.size(), ThreadCount);
    }

    TEST(DiagnosticsEventsTest, ActiveThreadContextIsSafeToSnapshotAndReleasedAtExit)
    {
        RuntimeModeGuard mode(Mode::Full);
        const Snapshot baseline = GetProvider().CaptureSnapshot();
        const NameHandle marker("Tests/Events/ActiveWorkerSnapshot");
        std::barrier ready(2);
        std::barrier release(2);
        std::thread worker([&] {
            MarkEvent(marker);
            ready.arrive_and_wait();
            release.arrive_and_wait();
        });
        ready.arrive_and_wait();
        const Snapshot active = GetProvider().CaptureSnapshot();
        EXPECT_GT(active.profilerOwnedBytes, baseline.profilerOwnedBytes);
        release.arrive_and_wait();
        worker.join();
        const Snapshot after = GetProvider().CaptureSnapshot();
        EXPECT_EQ(after.profilerOwnedBytes, baseline.profilerOwnedBytes);
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

    TEST(DiagnosticsEventsTest, IncrementalCursorReturnsExactlyTheUnseenEvents)
    {
        RuntimeModeGuard mode(Mode::Full);
        const NameHandle marker("Tests/Events/Cursor");
        std::uint64_t cursor = NewestSequence();

        for (std::int64_t value = 0; value < 8; ++value)
            MarkEvent(marker, Category::Application, value);

        const EventBatch first = GetProvider().ReadEvents(cursor, EventHistoryCapacity);
        ASSERT_GE(first.events.size(), 8U);
        EXPECT_EQ(first.eventsDroppedBeforeStart, 0U);
        for (const EventRecord& event : first.events)
            EXPECT_GT(event.sequence, cursor);

        cursor = first.newestAvailableSequence;
        const EventBatch empty = GetProvider().ReadEvents(cursor, EventHistoryCapacity);
        EXPECT_TRUE(empty.events.empty());
        EXPECT_EQ(empty.eventsDroppedBeforeStart, 0U);
        EXPECT_EQ(empty.newestAvailableSequence, cursor);

        for (std::int64_t value = 0; value < 3; ++value)
            MarkEvent(marker, Category::Application, value);
        const EventBatch second = GetProvider().ReadEvents(cursor, EventHistoryCapacity);
        ASSERT_GE(second.events.size(), 3U);
        EXPECT_EQ(second.events.front().sequence, cursor + 1);
        for (std::size_t index = 1; index < second.events.size(); ++index)
            EXPECT_EQ(second.events[index].sequence, second.events[index - 1].sequence + 1);
    }

    TEST(DiagnosticsEventsTest, ACursorBeyondTheHistoryReportsNoPhantomDiscontinuity)
    {
        RuntimeModeGuard mode(Mode::Full);
        const NameHandle marker("Tests/Events/CursorBounds");
        MarkEvent(marker, Category::Application, 1);

        const EventBatch saturated = GetProvider().ReadEvents(
            std::numeric_limits<std::uint64_t>::max(), EventHistoryCapacity);
        EXPECT_TRUE(saturated.events.empty());
        EXPECT_EQ(saturated.eventsDroppedBeforeStart, 0U);

        const EventBatch ahead = GetProvider().ReadEvents(
            saturated.newestAvailableSequence + 1000, EventHistoryCapacity);
        EXPECT_TRUE(ahead.events.empty());
        EXPECT_EQ(ahead.eventsDroppedBeforeStart, 0U);
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
        EXPECT_EQ(trace.GetDroppedEventCount(), 2u);
        EXPECT_EQ(trace.GetEvents().front().value, 2);
        EXPECT_EQ(trace.ResolveName(marker.GetId()), "Tests/Recording/Bounded");
        EXPECT_FALSE(session.IsActive());

        std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(trace.WriteBinary(binary));
        binary.seekg(0);
        const Trace decoded = Trace::ReadBinary(binary);
        ASSERT_EQ(decoded.GetEvents().size(), 3u);
        EXPECT_EQ(decoded.GetDroppedEventCount(), 2u);
        EXPECT_EQ(decoded.GetEvents().front().value, 2);
    }

    TEST(DiagnosticsRecordingTest, MaximumCapacityRetainsEveryNormallyDrainedEvent)
    {
        RuntimeModeGuard mode(Mode::Full);
        RecordingSession session = StartRecording(EventHistoryCapacity);
        ASSERT_TRUE(session.IsActive());
        const NameHandle marker("Tests/Recording/MaximumCapacity");
        for (std::size_t first = 0; first < EventHistoryCapacity;
             first += ThreadEventCapacity)
        {
            for (std::size_t offset = 0; offset < ThreadEventCapacity; ++offset)
            {
                MarkEvent(marker, Category::Application,
                          static_cast<std::int64_t>(first + offset));
            }
            (void)GetProvider().ReadEvents(0, 1);
        }

        const Trace trace = StopRecording(session);
        ASSERT_EQ(trace.GetEvents().size(), EventHistoryCapacity);
        EXPECT_EQ(trace.GetDroppedEventCount(), 0u);
        EXPECT_EQ(trace.GetEvents().front().value, 0);
        EXPECT_EQ(trace.GetEvents().back().value,
                  static_cast<std::int64_t>(EventHistoryCapacity - 1));
        for (std::size_t index = 1; index < trace.GetEvents().size(); ++index)
        {
            EXPECT_LT(trace.GetEvents()[index - 1].sequence, trace.GetEvents()[index].sequence);
            EXPECT_LE(trace.GetEvents()[index - 1].timestampNs,
                      trace.GetEvents()[index].timestampNs);
        }
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

    TEST(DiagnosticsRecordingTest, MoveAssignmentTransfersTheOnlyActiveCursor)
    {
        RuntimeModeGuard mode(Mode::Full);
        RecordingSession destination = StartRecording(4);
        RecordingSession source = StartRecording(4);
        ASSERT_TRUE(destination.IsActive());
        ASSERT_TRUE(source.IsActive());
        destination = std::move(source);
        EXPECT_FALSE(source.IsActive());
        EXPECT_TRUE(destination.IsActive());
        const NameHandle marker("Tests/Recording/MoveAssigned");
        MarkEvent(marker);
        EXPECT_EQ(StopRecording(destination).GetEvents().size(), 1u);
    }

    TEST(DiagnosticsRecordingTest, RuntimeModesBeforeAndDuringRecordingFailSafely)
    {
        RuntimeModeGuard mode(Mode::Full);
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        EXPECT_FALSE(StartRecording(4).IsActive());
        ASSERT_TRUE(SetRuntimeMode(Mode::Full));
        RecordingSession session = StartRecording(4);
        ASSERT_TRUE(session.IsActive());
        const NameHandle marker("Tests/Recording/ModeTransition");
        MarkEvent(marker, Category::Application, 1);
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        MarkEvent(marker, Category::Application, 2);
        ASSERT_TRUE(SetRuntimeMode(Mode::Off));
        ASSERT_TRUE(SetRuntimeMode(Mode::Full));
        MarkEvent(marker, Category::Application, 3);

        const Trace trace = StopRecording(session);
        ASSERT_EQ(trace.GetEvents().size(), 2u);
        EXPECT_EQ(trace.GetEvents()[0].value, 1);
        EXPECT_EQ(trace.GetEvents()[1].value, 3);
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
        const EventRecord& expected = original.GetEvents()[0];
        const EventRecord& actual = decoded.GetEvents()[0];
        EXPECT_EQ(actual.sequence, expected.sequence);
        EXPECT_EQ(actual.timestampNs, expected.timestampNs);
        EXPECT_EQ(actual.durationNs, expected.durationNs);
        EXPECT_EQ(actual.frameNumber, expected.frameNumber);
        EXPECT_EQ(actual.threadId, expected.threadId);
        EXPECT_EQ(actual.correlationId, expected.correlationId);
        EXPECT_EQ(actual.parentCorrelationId, expected.parentCorrelationId);
        EXPECT_EQ(actual.value, 42);
        EXPECT_EQ(actual.name, expected.name);
        EXPECT_EQ(actual.kind, expected.kind);
        EXPECT_EQ(actual.category, expected.category);
        EXPECT_EQ(decoded.ResolveName(decoded.GetEvents()[0].name), "Tests/Trace/RoundTrip");

        std::ostringstream chrome;
        ASSERT_TRUE(decoded.WriteChromeTrace(chrome));
        EXPECT_NE(chrome.str().find("Tests/Trace/RoundTrip"), std::string::npos);
        EXPECT_NE(chrome.str().find("traceEvents"), std::string::npos);
    }

    TEST(DiagnosticsTraceTest, EmptyTraceRoundTripsAndProducesValidChromeJson)
    {
        Trace empty;
        std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(empty.WriteBinary(binary));
        binary.seekg(0);
        Trace decoded = Trace::ReadBinary(binary);
        EXPECT_TRUE(decoded.GetEvents().empty());
        EXPECT_EQ(decoded.GetDroppedEventCount(), 0u);

        std::ostringstream chrome;
        ASSERT_TRUE(decoded.WriteChromeTrace(chrome));
        EXPECT_EQ(chrome.str(),
                  "{\"traceEvents\":[],\"displayTimeUnit\":\"ns\",\"cnaDroppedEvents\":0}");
    }

    TEST(DiagnosticsTraceTest, MoveConstructionAndAssignmentPreserveOwnedData)
    {
        RuntimeModeGuard mode(Mode::Full);
        RecordingSession session = StartRecording(2);
        const NameHandle marker("Tests/Trace/Moved");
        MarkEvent(marker, Category::Content, 17);
        Trace original = StopRecording(session);
        Trace moved(std::move(original));
        Trace assigned;
        assigned = std::move(moved);
        ASSERT_EQ(assigned.GetEvents().size(), 1u);
        EXPECT_EQ(assigned.GetEvents()[0].value, 17);
        EXPECT_EQ(assigned.ResolveName(assigned.GetEvents()[0].name), "Tests/Trace/Moved");
    }

    TEST(DiagnosticsTraceTest, ChromeJsonEscapesNamesAndRetainsNestedAndThreadedEvents)
    {
        RuntimeModeGuard mode(Mode::Full);
        RecordingSession session = StartRecording(8);
        const NameHandle special(R"(Tests/Trace/"quoted"\path/Ω)");
        const NameHandle outerName("Tests/Trace/ChromeOuter");
        const NameHandle innerName("Tests/Trace/ChromeInner");
        std::thread worker([&special] { MarkEvent(special); });
        {
            ZoneScope outer(outerName);
            ZoneScope inner(innerName);
        }
        worker.join();
        const Trace trace = StopRecording(session);
        ASSERT_EQ(trace.GetEvents().size(), 3u);

        std::ostringstream chrome;
        ASSERT_TRUE(trace.WriteChromeTrace(chrome));
        const std::string json = chrome.str();
        EXPECT_EQ(json.front(), '{');
        EXPECT_EQ(json.back(), '}');
        EXPECT_NE(json.find("\"name\":\"Tests/Trace/\\\"quoted\\\"\\\\path/Ω\""),
                  std::string::npos);
        EXPECT_NE(json.find("\"ph\":\"X\""), std::string::npos);
        EXPECT_NE(json.find("\"ph\":\"i\""), std::string::npos);
    }

    TEST(DiagnosticsTraceTest, MalformedBinaryInputIsRejected)
    {
        std::istringstream malformed("not-a-cna-trace");
        EXPECT_THROW((void)Trace::ReadBinary(malformed), std::runtime_error);
    }

    TEST(DiagnosticsTraceTest, InvalidUtf8NamesStillProduceValidChromeJson)
    {
        RuntimeModeGuard mode(Mode::Full);
        RecordingSession session = StartRecording(8);
        // A lone continuation byte, a truncated sequence, an overlong encoding, a UTF-16
        // surrogate and a value above U+10FFFF. None of these may reach the JSON unchanged.
        // Split so the escape stops at 0x80: in "\x80End" the E is a hex digit and would
        // be swallowed into one out-of-range escape.
        const NameHandle lone("Tests/Trace/Bad\x80" "End");
        const NameHandle truncated("Tests/Trace/Bad\xE2\x82");
        const NameHandle overlong("Tests/Trace/Bad\xC0\xAF");
        const NameHandle surrogate("Tests/Trace/Bad\xED\xA0\x80");
        const NameHandle tooLarge("Tests/Trace/Bad\xF5\x80\x80\x80");
        for (const NameHandle* name : {&lone, &truncated, &overlong, &surrogate, &tooLarge})
            MarkEvent(*name, Category::Application);
        const Trace trace = StopRecording(session);
        ASSERT_EQ(trace.GetEvents().size(), 5u);

        std::ostringstream chrome;
        ASSERT_TRUE(trace.WriteChromeTrace(chrome));
        const std::string json = chrome.str();
        EXPECT_TRUE(IsValidUtf8Text(json)) << "Chrome trace JSON must be valid UTF-8";
        EXPECT_NE(json.find("\xEF\xBF\xBD"), std::string::npos)
            << "invalid bytes should survive as U+FFFD rather than vanish";
        EXPECT_EQ(json.front(), '{');
        EXPECT_EQ(json.back(), '}');
        for (const EventRecord& event : trace.GetEvents())
            EXPECT_TRUE(IsValidUtf8Text(trace.ResolveName(event.name)));
    }

    TEST(DiagnosticsTraceTest, RandomlyMutatedTracesAreRejectedOrReadBackCleanly)
    {
        RuntimeModeGuard mode(Mode::Full);
        RecordingSession session = StartRecording(8);
        const NameHandle marker("Tests/Trace/Fuzz");
        {
            ZoneScope outer(marker);
            MarkEvent(marker, Category::Graphics, 5);
        }
        const Trace seed = StopRecording(session);
        std::ostringstream binary;
        ASSERT_TRUE(seed.WriteBinary(binary));
        const std::string original = binary.str();
        ASSERT_FALSE(original.empty());

        // Deterministic so a failure is reproducible from the seed alone.
        std::mt19937 random(0x0DDBA11U);
        std::size_t parsed = 0;
        std::size_t rejected = 0;
        for (int iteration = 0; iteration < 3000; ++iteration)
        {
            std::string mutated = original;
            const int mutations =
                1 + static_cast<int>(random() % 4U);
            for (int mutation = 0; mutation < mutations; ++mutation)
            {
                switch (random() % 3U)
                {
                    case 0:
                        mutated[random() % mutated.size()] =
                            static_cast<char>(random() % 256U);
                        break;
                    case 1:
                        mutated.resize(random() % mutated.size() + 1);
                        break;
                    default:
                        mutated.insert(random() % mutated.size(), 1,
                                       static_cast<char>(random() % 256U));
                        break;
                }
            }
            std::istringstream input(mutated);
            try
            {
                const Trace decoded = Trace::ReadBinary(input);
                ++parsed;
                // Whatever it accepted must still be internally coherent and exportable.
                EXPECT_LE(decoded.GetEvents().size(), EventHistoryCapacity);
                for (const EventRecord& event : decoded.GetEvents())
                    EXPECT_TRUE(IsValidUtf8Text(decoded.ResolveName(event.name)));
                std::ostringstream chrome;
                if (decoded.WriteChromeTrace(chrome))
                {
                    EXPECT_TRUE(IsValidUtf8Text(chrome.str()));
                }
            }
            catch (const std::exception&)
            {
                ++rejected;
            }
        }
        // The point is that nothing crashed, over-allocated or escaped its bounds; both
        // outcomes are acceptable, and both must occur for the corpus to be meaningful.
        EXPECT_GT(rejected, 0u);
        EXPECT_EQ(parsed + rejected, 3000u);
    }
#endif
}
