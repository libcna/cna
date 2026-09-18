// SPDX-License-Identifier: MS-PL
#include "CNA/Diagnostics/Diagnostics.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <istream>
#include <limits>
#include <mutex>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace CNA::Diagnostics
{
    namespace
    {
        constexpr std::size_t MaximumMetrics = 512;
        constexpr std::size_t MaximumFrameMetrics = 64;
        constexpr std::size_t MaximumTraceNames = 65'536;
        constexpr std::size_t MaximumTraceNameBytes = 1U << 20U;
        constexpr std::size_t MaximumTraceTotalNameBytes = 16U << 20U;

#if CNA_DIAGNOSTICS_LEVEL >= 1
        [[nodiscard]] std::uint64_t TimestampNowNs() noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
        }
#endif

        template<typename T>
        void WriteLittleEndian(std::ostream& output, T value)
        {
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned bits = static_cast<Unsigned>(value);
            for (std::size_t byte = 0; byte < sizeof(T); ++byte)
            {
                output.put(static_cast<char>((bits >> (byte * 8U)) & 0xffU));
            }
        }

        template<typename T>
        [[nodiscard]] T ReadLittleEndian(std::istream& input)
        {
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned bits = 0;
            for (std::size_t byte = 0; byte < sizeof(T); ++byte)
            {
                const int value = input.get();
                if (value == std::char_traits<char>::eof())
                    throw std::runtime_error("Truncated CNA profiler trace");
                bits |= static_cast<Unsigned>(static_cast<unsigned char>(value)) << (byte * 8U);
            }
            return static_cast<T>(bits);
        }

        void WriteJsonString(std::ostream& output, std::string_view value)
        {
            output.put('"');
            for (const unsigned char character : value)
            {
                switch (character)
                {
                    case '"': output << "\\\""; break;
                    case '\\': output << "\\\\"; break;
                    case '\b': output << "\\b"; break;
                    case '\f': output << "\\f"; break;
                    case '\n': output << "\\n"; break;
                    case '\r': output << "\\r"; break;
                    case '\t': output << "\\t"; break;
                    default:
                        if (character < 0x20U)
                        {
                            const auto flags = output.flags();
                            const auto fill = output.fill();
                            output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                                   << static_cast<unsigned int>(character);
                            output.flags(flags);
                            output.fill(fill);
                        }
                        else
                        {
                            output.put(static_cast<char>(character));
                        }
                        break;
                }
            }
            output.put('"');
        }

#if CNA_DIAGNOSTICS_LEVEL >= 1
        struct MetricSlot
        {
            std::atomic<std::int64_t> value{0};
            std::atomic<std::int64_t> lastFrameValue{0};
            std::string name;
            MetricKind kind = MetricKind::Gauge;
            MetricUnit unit = MetricUnit::Count;
            Accuracy accuracy = Accuracy::Exact;
        };

        struct InternalMetricValue
        {
            MetricId id = 0;
            std::int64_t value = 0;
        };

        struct InternalFrame
        {
            std::uint64_t frameNumber = 0;
            std::uint64_t startTimestampNs = 0;
            std::uint64_t durationNs = 0;
            std::array<InternalMetricValue, MaximumFrameMetrics> metrics{};
            std::size_t metricCount = 0;
        };

#if CNA_DIAGNOSTICS_LEVEL >= 2
        struct RawEvent
        {
            std::uint64_t timestampNs = 0;
            std::uint64_t durationNs = 0;
            std::uint64_t frameNumber = 0;
            std::uint64_t threadId = 0;
            std::uint64_t correlationId = 0;
            std::uint64_t parentCorrelationId = 0;
            std::int64_t value = 0;
            NameId name = 0;
            EventKind kind = EventKind::Marker;
            Category category = Category::Core;
        };

        struct ActiveZone
        {
            std::uint64_t startTimestampNs = 0;
            std::uint64_t correlationId = 0;
            std::uint64_t parentCorrelationId = 0;
            NameId name = 0;
            Category category = Category::Application;
        };

        struct ThreadContext
        {
            explicit ThreadContext(std::uint64_t id) : threadId(id) {}

            bool Push(const RawEvent& event) noexcept
            {
                const std::uint64_t read = readIndex.load(std::memory_order_acquire);
                if (writeIndex - read >= ThreadEventCapacity)
                {
                    dropped.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
                events[static_cast<std::size_t>(writeIndex % ThreadEventCapacity)] = event;
                ++writeIndex;
                publishedWriteIndex.store(writeIndex, std::memory_order_release);
                return true;
            }

            std::array<RawEvent, ThreadEventCapacity> events{};
            std::array<ActiveZone, MaximumZoneDepth> zones{};
            std::atomic<std::uint64_t> publishedWriteIndex{0};
            std::atomic<std::uint64_t> readIndex{0};
            std::atomic<std::uint64_t> dropped{0};
            std::uint64_t writeIndex = 0;
            std::uint64_t nextCorrelation = 1;
            std::uint64_t threadId = 0;
            std::uint64_t modeGeneration = 0;
            std::size_t zoneDepth = 0;
        };
#endif

        struct State
        {
            std::atomic<Mode> runtimeMode{static_cast<Mode>(CNA_DIAGNOSTICS_LEVEL)};
            std::atomic<std::uint64_t> modeGeneration{1};

            std::mutex metricMutex;
            std::array<MetricSlot, MaximumMetrics> metrics{};
            std::unordered_map<std::string, MetricId> metricIds;
            std::size_t metricCount = 0;

            std::mutex frameMutex;
            std::mutex frameCompletionMutex;
            std::array<InternalFrame, FrameHistoryCapacity> frames{};
            std::size_t frameWriteIndex = 0;
            std::size_t frameCount = 0;
            std::atomic<std::uint64_t> currentFrameNumber{0};
            std::uint64_t activeFrameStart = 0;
            bool frameActive = false;

            std::mutex resourceMutex;
            std::unordered_map<ResourceId, ResourceRecord> resources;
            std::uint64_t nextResourceId = 1;
            std::uint64_t resourceBytes = 0;

            std::atomic<std::uint64_t> malformedZones{0};
            std::atomic<std::uint64_t> malformedFrames{0};

            std::mutex sourceMutex;
            std::unordered_map<std::uint64_t, std::shared_ptr<IDiagnosticsSource>> sources;
            std::uint64_t nextSourceId = 1;
            std::atomic<std::uint64_t> sourceCollectionFailures{0};

#if CNA_DIAGNOSTICS_LEVEL >= 2
            std::mutex nameMutex;
            std::vector<std::string> names{std::string{}};
            std::unordered_map<std::string, NameId> nameIds;
            std::size_t nameBytes = 0;

            std::mutex threadMutex;
            std::vector<ThreadContext*> threads;
            std::atomic<std::uint64_t> nextThreadId{1};

            std::mutex historyMutex;
            std::array<EventRecord, EventHistoryCapacity> history{};
            std::size_t historyWriteIndex = 0;
            std::size_t historyCount = 0;
            std::uint64_t nextEventSequence = 1;
            std::uint64_t historyOverwrites = 0;
            std::atomic<std::uint64_t> producerDrops{0};
#endif
        };

        [[nodiscard]] State& GetState() noexcept
        {
            // Intentionally process-lifetime. Thread-local destructors may still publish their
            // final events after ordinary namespace statics would otherwise have been destroyed.
            static State* state = new State();
            return *state;
        }

        [[nodiscard]] MetricId RegisterMetric(std::string_view name, MetricKind kind,
                                              MetricUnit unit, Accuracy accuracy) noexcept
        {
            try
            {
                if (name.empty() || name.size() > MaximumTraceNameBytes)
                    return 0;
                State& state = GetState();
                const std::lock_guard lock(state.metricMutex);
                std::string key(name);
                if (const auto found = state.metricIds.find(key); found != state.metricIds.end())
                {
                    const MetricSlot& slot = state.metrics[found->second - 1];
                    return slot.kind == kind && slot.unit == unit && slot.accuracy == accuracy
                        ? found->second : 0;
                }
                if (state.metricCount >= MaximumMetrics)
                    return 0;
                const MetricId id = static_cast<MetricId>(state.metricCount + 1);
                MetricSlot& slot = state.metrics[state.metricCount];
                slot.name.assign(name);
                slot.kind = kind;
                slot.unit = unit;
                slot.accuracy = accuracy;
                state.metricIds.emplace(std::move(key), id);
                ++state.metricCount;
                return id;
            }
            catch (...)
            {
                return 0;
            }
        }

        void AddMetric(MetricId id, std::int64_t delta) noexcept
        {
            if (id == 0)
                return;
            State& state = GetState();
            if (state.runtimeMode.load(std::memory_order_relaxed) == Mode::Off)
                return;
            state.metrics[id - 1].value.fetch_add(delta, std::memory_order_relaxed);
        }

        void SetMetric(MetricId id, std::int64_t value) noexcept
        {
            if (id == 0)
                return;
            State& state = GetState();
            if (state.runtimeMode.load(std::memory_order_relaxed) == Mode::Off)
                return;
            state.metrics[id - 1].value.store(value, std::memory_order_relaxed);
        }

#if CNA_DIAGNOSTICS_LEVEL >= 2
        [[nodiscard]] NameId RegisterName(std::string_view name) noexcept
        {
            try
            {
                if (name.empty() || name.size() > MaximumTraceNameBytes)
                    return 0;
                State& state = GetState();
                const std::lock_guard lock(state.nameMutex);
                const std::string key(name);
                if (const auto found = state.nameIds.find(key); found != state.nameIds.end())
                    return found->second;
                if (state.names.size() >= MaximumTraceNames ||
                    state.nameBytes > MaximumTraceTotalNameBytes - name.size())
                    return 0;
                const NameId id = static_cast<NameId>(state.names.size());
                state.names.push_back(key);
                state.nameIds.emplace(key, id);
                state.nameBytes += key.size();
                return id;
            }
            catch (...)
            {
                return 0;
            }
        }

        void AppendToHistoryLocked(State& state, const RawEvent& raw) noexcept
        {
            EventRecord record;
            record.sequence = state.nextEventSequence++;
            record.timestampNs = raw.timestampNs;
            record.durationNs = raw.durationNs;
            record.frameNumber = raw.frameNumber;
            record.threadId = raw.threadId;
            record.correlationId = raw.correlationId;
            record.parentCorrelationId = raw.parentCorrelationId;
            record.value = raw.value;
            record.name = raw.name;
            record.kind = raw.kind;
            record.category = raw.category;
            state.history[state.historyWriteIndex] = record;
            state.historyWriteIndex = (state.historyWriteIndex + 1) % EventHistoryCapacity;
            if (state.historyCount < EventHistoryCapacity)
                ++state.historyCount;
            else
                ++state.historyOverwrites;
        }

        void DrainContextLocked(State& state, ThreadContext& context) noexcept
        {
            std::uint64_t read = context.readIndex.load(std::memory_order_relaxed);
            const std::uint64_t write =
                context.publishedWriteIndex.load(std::memory_order_acquire);
            if (read == write)
                return;
            const std::lock_guard historyLock(state.historyMutex);
            while (read < write)
            {
                AppendToHistoryLocked(
                    state, context.events[static_cast<std::size_t>(read % ThreadEventCapacity)]);
                ++read;
            }
            context.readIndex.store(read, std::memory_order_release);
            const std::uint64_t dropped = context.dropped.exchange(0, std::memory_order_relaxed);
            state.producerDrops.fetch_add(dropped, std::memory_order_relaxed);
        }

        void CollectEvents() noexcept
        {
            State& state = GetState();
            const std::lock_guard lock(state.threadMutex);
            for (ThreadContext* context : state.threads)
                DrainContextLocked(state, *context);
        }

        void UnregisterThreadContext(ThreadContext* context) noexcept
        {
            if (context == nullptr)
                return;
            State& state = GetState();
            {
                const std::lock_guard lock(state.threadMutex);
                DrainContextLocked(state, *context);
                const auto found = std::find(state.threads.begin(), state.threads.end(), context);
                if (found != state.threads.end())
                    state.threads.erase(found);
            }
            delete context;
        }

        struct ThreadContextHolder
        {
            ~ThreadContextHolder() { UnregisterThreadContext(context); }
            ThreadContext* context = nullptr;
        };

        [[nodiscard]] ThreadContext& GetThreadContext()
        {
            thread_local ThreadContextHolder holder;
            if (holder.context == nullptr)
            {
                State& state = GetState();
                std::uint64_t threadId = state.nextThreadId.load(std::memory_order_relaxed);
                while (threadId != 0)
                {
                    const std::uint64_t next = threadId == std::numeric_limits<std::uint64_t>::max()
                        ? 0 : threadId + 1;
                    if (state.nextThreadId.compare_exchange_weak(
                            threadId, next, std::memory_order_relaxed,
                            std::memory_order_relaxed))
                    {
                        break;
                    }
                }
                if (threadId == 0)
                    throw std::overflow_error("Diagnostics thread ID space exhausted");
                holder.context = new ThreadContext(threadId);
                const std::lock_guard lock(state.threadMutex);
                state.threads.push_back(holder.context);
            }
            return *holder.context;
        }

        void EmitRawEvent(RawEvent event) noexcept
        {
            State& state = GetState();
            if (state.runtimeMode.load(std::memory_order_relaxed) != Mode::Full)
                return;
            try
            {
                ThreadContext& context = GetThreadContext();
                event.threadId = context.threadId;
                event.frameNumber = state.currentFrameNumber.load(std::memory_order_relaxed);
                (void)context.Push(event);
            }
            catch (...)
            {
                state.producerDrops.fetch_add(1, std::memory_order_relaxed);
            }
        }
#endif

        [[nodiscard]] ResourceRecord CopyResource(ResourceId id,
                                                  const ResourceDescriptor& descriptor)
        {
            ResourceRecord record;
            record.id = id;
            record.kind = descriptor.kind;
            record.label.assign(descriptor.label);
            record.format.assign(descriptor.format);
            record.width = descriptor.width;
            record.height = descriptor.height;
            record.depth = descriptor.depth;
            record.mipCount = descriptor.mipCount;
            record.estimatedBytes = descriptor.estimatedBytes;
            record.byteAccuracy = descriptor.byteAccuracy;
            return record;
        }

        void AddResourceBytesLocked(State& state, std::uint64_t bytes) noexcept
        {
            if (state.resourceBytes > std::numeric_limits<std::uint64_t>::max() - bytes)
                state.resourceBytes = std::numeric_limits<std::uint64_t>::max();
            else
                state.resourceBytes += bytes;
        }

        void RecalculateResourceBytesLocked(State& state) noexcept
        {
            state.resourceBytes = 0;
            for (const auto& [id, resource] : state.resources)
            {
                (void)id;
                AddResourceBytesLocked(state, resource.estimatedBytes);
                if (state.resourceBytes == std::numeric_limits<std::uint64_t>::max())
                    break;
            }
        }

        [[nodiscard]] ResourceId RegisterResource(const ResourceDescriptor& descriptor) noexcept
        {
            try
            {
                State& state = GetState();
                if (state.runtimeMode.load(std::memory_order_relaxed) == Mode::Off)
                    return 0;
                ResourceId id = 0;
                {
                    const std::lock_guard lock(state.resourceMutex);
                    if (state.nextResourceId == 0)
                        return 0;
                    id = state.nextResourceId;
                    state.nextResourceId = id == std::numeric_limits<ResourceId>::max()
                        ? 0 : id + 1;
                    state.resources.emplace(id, CopyResource(id, descriptor));
                    AddResourceBytesLocked(state, descriptor.estimatedBytes);
                }
#if CNA_DIAGNOSTICS_LEVEL >= 2
                static const NameHandle eventName("Diagnostics/ResourceCreated");
                EmitRawEvent(RawEvent{.timestampNs = TimestampNowNs(),
                                      .value = static_cast<std::int64_t>(id),
                                      .name = eventName.GetId(),
                                      .kind = EventKind::ResourceCreated,
                                      .category = Category::Core});
#endif
                return id;
            }
            catch (...)
            {
                return 0;
            }
        }

        void UpdateResource(ResourceId id, const ResourceDescriptor& descriptor) noexcept
        {
            if (id == 0)
                return;
            try
            {
                State& state = GetState();
                const std::lock_guard lock(state.resourceMutex);
                const auto found = state.resources.find(id);
                if (found == state.resources.end())
                    return;
                const std::uint64_t previousBytes = found->second.estimatedBytes;
                found->second = CopyResource(id, descriptor);
                if (state.resourceBytes == std::numeric_limits<std::uint64_t>::max())
                    RecalculateResourceBytesLocked(state);
                else
                {
                    state.resourceBytes -= previousBytes;
                    AddResourceBytesLocked(state, descriptor.estimatedBytes);
                }
            }
            catch (...)
            {
            }
        }

        void UnregisterResource(ResourceId id) noexcept
        {
            if (id == 0)
                return;
            State& state = GetState();
            {
                const std::lock_guard lock(state.resourceMutex);
                const auto found = state.resources.find(id);
                if (found == state.resources.end())
                    return;
                const bool wasSaturated =
                    state.resourceBytes == std::numeric_limits<std::uint64_t>::max();
                const std::uint64_t removedBytes = found->second.estimatedBytes;
                state.resources.erase(found);
                if (wasSaturated)
                    RecalculateResourceBytesLocked(state);
                else
                    state.resourceBytes -= removedBytes;
            }
#if CNA_DIAGNOSTICS_LEVEL >= 2
            static const NameHandle eventName("Diagnostics/ResourceDestroyed");
            EmitRawEvent(RawEvent{.timestampNs = TimestampNowNs(),
                                  .value = static_cast<std::int64_t>(id),
                                  .name = eventName.GetId(),
                                  .kind = EventKind::ResourceDestroyed,
                                  .category = Category::Core});
#endif
        }

        [[nodiscard]] std::uint64_t RegisterDiagnosticsSource(
            std::shared_ptr<IDiagnosticsSource> source) noexcept
        {
            if (!source)
                return 0;
            try
            {
                State& state = GetState();
                const std::lock_guard lock(state.sourceMutex);
                if (state.nextSourceId == 0)
                    return 0;
                const std::uint64_t id = state.nextSourceId;
                state.nextSourceId = id == std::numeric_limits<std::uint64_t>::max()
                    ? 0 : id + 1;
                state.sources.emplace(id, std::move(source));
                return id;
            }
            catch (...)
            {
                return 0;
            }
        }

        void UnregisterDiagnosticsSource(std::uint64_t id) noexcept
        {
            if (id == 0)
                return;
            State& state = GetState();
            const std::lock_guard lock(state.sourceMutex);
            state.sources.erase(id);
        }

        void CollectDiagnosticsSources(State& state) noexcept
        {
            std::vector<std::shared_ptr<IDiagnosticsSource>> sources;
            try
            {
                const std::lock_guard lock(state.sourceMutex);
                sources.reserve(state.sources.size());
                for (const auto& [id, source] : state.sources)
                {
                    (void)id;
                    sources.push_back(source);
                }
            }
            catch (...)
            {
                state.sourceCollectionFailures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            FrameStatisticsSink sink;
            for (const std::shared_ptr<IDiagnosticsSource>& source : sources)
            {
                try
                {
                    source->Collect(sink);
                }
                catch (...)
                {
                    state.sourceCollectionFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        [[nodiscard]] bool TryBeginFrame() noexcept
        {
            State& state = GetState();
            if (state.runtimeMode.load(std::memory_order_relaxed) == Mode::Off)
                return false;
            const std::lock_guard lock(state.frameMutex);
            if (state.frameActive)
            {
                state.malformedFrames.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            state.frameActive = true;
            state.activeFrameStart = TimestampNowNs();
            state.currentFrameNumber.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        void FinishFrame(bool reportMalformed) noexcept
        {
            State& state = GetState();
            const std::lock_guard completionLock(state.frameCompletionMutex);
            {
                const std::lock_guard frameLock(state.frameMutex);
                if (!state.frameActive)
                {
                    if (reportMalformed &&
                        state.runtimeMode.load(std::memory_order_relaxed) != Mode::Off)
                    {
                        state.malformedFrames.fetch_add(1, std::memory_order_relaxed);
                    }
                    return;
                }
            }
            CollectDiagnosticsSources(state);
            InternalFrame completed;
            {
                const std::lock_guard frameLock(state.frameMutex);
                const std::uint64_t now = TimestampNowNs();
                completed.frameNumber =
                    state.currentFrameNumber.load(std::memory_order_relaxed);
                completed.startTimestampNs = state.activeFrameStart;
                completed.durationNs = now - state.activeFrameStart;
                state.frameActive = false;

                const std::lock_guard metricLock(state.metricMutex);
                for (std::size_t index = 0; index < state.metricCount; ++index)
                {
                    MetricSlot& metric = state.metrics[index];
                    if (metric.kind != MetricKind::FrameCounter)
                        continue;
                    const std::int64_t value =
                        metric.value.exchange(0, std::memory_order_relaxed);
                    metric.lastFrameValue.store(value, std::memory_order_relaxed);
                    if (completed.metricCount < MaximumFrameMetrics)
                    {
                        completed.metrics[completed.metricCount++] = {
                            static_cast<MetricId>(index + 1), value};
                    }
                }

                state.frames[state.frameWriteIndex] = completed;
                state.frameWriteIndex = (state.frameWriteIndex + 1) % FrameHistoryCapacity;
                if (state.frameCount < FrameHistoryCapacity)
                    ++state.frameCount;
            }

#if CNA_DIAGNOSTICS_LEVEL >= 2
            static const NameHandle frameName("Frame");
            EmitRawEvent(RawEvent{.timestampNs = completed.startTimestampNs,
                                  .durationNs = completed.durationNs,
                                  .correlationId = completed.frameNumber,
                                  .name = frameName.GetId(),
                                  .kind = EventKind::Frame,
                                  .category = Category::Core});
            CollectEvents();
#endif
        }
#endif

        class ProcessProvider final : public IDiagnosticsProvider
        {
        public:
            Snapshot CaptureSnapshot() override
            {
                Snapshot snapshot;
                snapshot.buildMode = GetBuildMode();
                snapshot.runtimeMode = GetRuntimeMode();
#if CNA_DIAGNOSTICS_LEVEL >= 1
#if CNA_DIAGNOSTICS_LEVEL >= 2
                CollectEvents();
#endif
                State& state = GetState();
                snapshot.currentFrameNumber =
                    state.currentFrameNumber.load(std::memory_order_relaxed);
                snapshot.malformedZoneCount =
                    state.malformedZones.load(std::memory_order_relaxed);
                snapshot.malformedFrameCount =
                    state.malformedFrames.load(std::memory_order_relaxed);
                snapshot.sourceCollectionFailures =
                    state.sourceCollectionFailures.load(std::memory_order_relaxed);

                {
                    const std::lock_guard lock(state.metricMutex);
                    snapshot.metrics.reserve(state.metricCount);
                    for (std::size_t index = 0; index < state.metricCount; ++index)
                    {
                        const MetricSlot& slot = state.metrics[index];
                        const std::int64_t value = slot.kind == MetricKind::FrameCounter
                            ? slot.lastFrameValue.load(std::memory_order_relaxed)
                            : slot.value.load(std::memory_order_relaxed);
                        snapshot.metrics.push_back(MetricSample{
                            static_cast<MetricId>(index + 1), slot.name, value, slot.kind,
                            slot.unit, slot.accuracy});
                    }
                }
                {
                    const std::lock_guard lock(state.frameMutex);
                    snapshot.recentFrames.reserve(state.frameCount);
                    const std::size_t oldest =
                        (state.frameWriteIndex + FrameHistoryCapacity - state.frameCount) %
                        FrameHistoryCapacity;
                    for (std::size_t offset = 0; offset < state.frameCount; ++offset)
                    {
                        const InternalFrame& frame =
                            state.frames[(oldest + offset) % FrameHistoryCapacity];
                        FrameSample publicFrame;
                        publicFrame.frameNumber = frame.frameNumber;
                        publicFrame.startTimestampNs = frame.startTimestampNs;
                        publicFrame.durationNs = frame.durationNs;
                        publicFrame.framesPerSecond = frame.durationNs == 0
                            ? 0.0
                            : 1'000'000'000.0 / static_cast<double>(frame.durationNs);
                        publicFrame.metrics.reserve(frame.metricCount);
                        const std::lock_guard metricLock(state.metricMutex);
                        for (std::size_t index = 0; index < frame.metricCount; ++index)
                        {
                            const InternalMetricValue& value = frame.metrics[index];
                            const MetricSlot& slot = state.metrics[value.id - 1];
                            publicFrame.metrics.push_back(MetricSample{
                                value.id, slot.name, value.value, slot.kind, slot.unit,
                                slot.accuracy});
                        }
                        snapshot.recentFrames.push_back(std::move(publicFrame));
                    }
                }
                {
                    const std::lock_guard lock(state.resourceMutex);
                    snapshot.registeredResourceBytes = state.resourceBytes;
                    snapshot.resources.reserve(state.resources.size());
                    for (const auto& [id, resource] : state.resources)
                    {
                        (void)id;
                        snapshot.resources.push_back(resource);
                    }
                    std::sort(snapshot.resources.begin(), snapshot.resources.end(),
                              [](const ResourceRecord& left, const ResourceRecord& right)
                              { return left.id < right.id; });
                }

                snapshot.profilerOwnedBytes = sizeof(State);
#if CNA_DIAGNOSTICS_LEVEL >= 2
                {
                    const std::lock_guard lock(state.threadMutex);
                    snapshot.profilerOwnedBytes += state.threads.size() * sizeof(ThreadContext);
                }
                snapshot.producerEventsDropped =
                    state.producerDrops.load(std::memory_order_relaxed);
                {
                    const std::lock_guard lock(state.historyMutex);
                    snapshot.eventHistoryOverwrites = state.historyOverwrites;
                }
#endif
#endif
                return snapshot;
            }

            EventBatch ReadEvents(std::uint64_t afterSequence,
                                  std::size_t maximumEvents) override
            {
                EventBatch batch;
#if CNA_DIAGNOSTICS_LEVEL >= 2
                CollectEvents();
                State& state = GetState();
                const std::lock_guard lock(state.historyMutex);
                batch.producerEventsDropped =
                    state.producerDrops.load(std::memory_order_relaxed);
                if (state.historyCount == 0 || maximumEvents == 0)
                    return batch;
                const std::size_t oldestIndex =
                    (state.historyWriteIndex + EventHistoryCapacity - state.historyCount) %
                    EventHistoryCapacity;
                batch.oldestAvailableSequence = state.history[oldestIndex].sequence;
                const std::size_t newestIndex =
                    (state.historyWriteIndex + EventHistoryCapacity - 1) % EventHistoryCapacity;
                batch.newestAvailableSequence = state.history[newestIndex].sequence;
                if (afterSequence != 0
                    && afterSequence != std::numeric_limits<std::uint64_t>::max()
                    && afterSequence + 1 < batch.oldestAvailableSequence)
                    batch.eventsDroppedBeforeStart =
                        batch.oldestAvailableSequence - (afterSequence + 1);
                if (afterSequence >= batch.newestAvailableSequence)
                    return batch;
                // Sequences are consecutive within the ring, so an incremental poll can start at
                // its first unseen event. Scanning from the oldest entry instead would walk the
                // whole history on every poll while holding the lock that frame completion needs.
                std::size_t startOffset = 0;
                if (afterSequence >= batch.oldestAvailableSequence)
                    startOffset =
                        static_cast<std::size_t>(afterSequence - batch.oldestAvailableSequence) + 1;
                batch.events.reserve(
                    std::min(maximumEvents, state.historyCount - startOffset));
                for (std::size_t offset = startOffset;
                     offset < state.historyCount && batch.events.size() < maximumEvents;
                     ++offset)
                {
                    const EventRecord& event =
                        state.history[(oldestIndex + offset) % EventHistoryCapacity];
                    if (event.sequence > afterSequence)
                        batch.events.push_back(event);
                }
#else
                (void)afterSequence;
                (void)maximumEvents;
#endif
                return batch;
            }

            std::string ResolveName(NameId id) override
            {
#if CNA_DIAGNOSTICS_LEVEL >= 2
                State& state = GetState();
                const std::lock_guard lock(state.nameMutex);
                return id < state.names.size() ? state.names[id] : std::string{};
#else
                (void)id;
                return {};
#endif
            }
        };
    }

    NameHandle::NameHandle(std::string_view name) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 2
        id_ = RegisterName(name);
#else
        (void)name;
#endif
    }

    CounterHandle::CounterHandle(std::string_view name, MetricUnit unit,
                                 Accuracy accuracy) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        id_ = RegisterMetric(name, MetricKind::Counter, unit, accuracy);
#else
        (void)name;
        (void)unit;
        (void)accuracy;
#endif
    }

    void CounterHandle::Add(std::int64_t delta) const noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        AddMetric(id_, delta);
#else
        (void)delta;
#endif
    }

    GaugeHandle::GaugeHandle(std::string_view name, MetricUnit unit, Accuracy accuracy) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        id_ = RegisterMetric(name, MetricKind::Gauge, unit, accuracy);
#else
        (void)name;
        (void)unit;
        (void)accuracy;
#endif
    }

    void GaugeHandle::Set(std::int64_t value) const noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        SetMetric(id_, value);
#else
        (void)value;
#endif
    }

    void GaugeHandle::Add(std::int64_t delta) const noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        AddMetric(id_, delta);
#else
        (void)delta;
#endif
    }

    FrameCounterHandle::FrameCounterHandle(std::string_view name, MetricUnit unit,
                                           Accuracy accuracy) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        id_ = RegisterMetric(name, MetricKind::FrameCounter, unit, accuracy);
#else
        (void)name;
        (void)unit;
        (void)accuracy;
#endif
    }

    void FrameCounterHandle::Add(std::int64_t delta) const noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        AddMetric(id_, delta);
#else
        (void)delta;
#endif
    }

    ResourceHandle::ResourceHandle(const ResourceDescriptor& descriptor) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        id_ = RegisterResource(descriptor);
#else
        (void)descriptor;
#endif
    }

    SourceRegistration::~SourceRegistration()
    {
        Reset();
    }

    SourceRegistration::SourceRegistration(SourceRegistration&& other) noexcept : id_(other.id_)
    {
        other.id_ = 0;
    }

    SourceRegistration& SourceRegistration::operator=(SourceRegistration&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    void SourceRegistration::Reset() noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        UnregisterDiagnosticsSource(id_);
#endif
        id_ = 0;
    }

    SourceRegistration RegisterSource(std::shared_ptr<IDiagnosticsSource> source) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        return SourceRegistration(RegisterDiagnosticsSource(std::move(source)));
#else
        (void)source;
        return {};
#endif
    }

    ResourceHandle::~ResourceHandle()
    {
        Reset();
    }

    ResourceHandle::ResourceHandle(ResourceHandle&& other) noexcept : id_(other.id_)
    {
        other.id_ = 0;
    }

    ResourceHandle& ResourceHandle::operator=(ResourceHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    void ResourceHandle::Update(const ResourceDescriptor& descriptor) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        UpdateResource(id_, descriptor);
#else
        (void)descriptor;
#endif
    }

    void ResourceHandle::Reset() noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        UnregisterResource(id_);
#endif
        id_ = 0;
    }

    ZoneToken BeginZone(const NameHandle& name, Category category) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 2
        State& state = GetState();
        if (state.runtimeMode.load(std::memory_order_relaxed) != Mode::Full || name.GetId() == 0)
            return {};
        try
        {
            ThreadContext& context = GetThreadContext();
            const std::uint64_t modeGeneration =
                state.modeGeneration.load(std::memory_order_relaxed);
            if (context.modeGeneration != modeGeneration)
            {
                context.zoneDepth = 0;
                context.modeGeneration = modeGeneration;
            }
            if (context.zoneDepth >= MaximumZoneDepth)
            {
                state.malformedZones.fetch_add(1, std::memory_order_relaxed);
                return {};
            }
            const std::uint64_t local = context.nextCorrelation++;
            const std::uint64_t correlation =
                (context.threadId * UINT64_C(0x9e3779b97f4a7c15)) ^ local;
            const std::uint64_t parent = context.zoneDepth == 0
                ? 0
                : context.zones[context.zoneDepth - 1].correlationId;
            context.zones[context.zoneDepth] = ActiveZone{
                TimestampNowNs(), correlation, parent, name.GetId(), category};
            ZoneToken token{correlation, context.threadId, modeGeneration,
                            static_cast<std::uint32_t>(context.zoneDepth), true};
            ++context.zoneDepth;
            return token;
        }
        catch (...)
        {
            state.producerDrops.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
#else
        (void)name;
        (void)category;
        return {};
#endif
    }

    bool EndZone(ZoneToken token) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 2
        if (!token.active)
            return true;
        State& state = GetState();
        if (state.runtimeMode.load(std::memory_order_relaxed) != Mode::Full)
            return true;
        try
        {
            ThreadContext& context = GetThreadContext();
            const std::uint64_t modeGeneration =
                state.modeGeneration.load(std::memory_order_relaxed);
            if (token.modeGeneration != modeGeneration)
            {
                if (context.modeGeneration != modeGeneration)
                {
                    context.zoneDepth = 0;
                    context.modeGeneration = modeGeneration;
                }
                return true;
            }
            if (context.threadId != token.threadId || context.zoneDepth == 0)
            {
                state.malformedZones.fetch_add(1, std::memory_order_relaxed);
                static const NameHandle malformedName("Diagnostics/MalformedZone");
                EmitRawEvent(RawEvent{.timestampNs = TimestampNowNs(),
                                      .correlationId = token.correlationId,
                                      .name = malformedName.GetId(),
                                      .kind = EventKind::Malformed,
                                      .category = Category::Core});
                return false;
            }

            std::size_t found = context.zoneDepth;
            for (std::size_t index = context.zoneDepth; index > 0; --index)
            {
                if (context.zones[index - 1].correlationId == token.correlationId)
                {
                    found = index - 1;
                    break;
                }
            }
            if (found == context.zoneDepth)
            {
                state.malformedZones.fetch_add(1, std::memory_order_relaxed);
                static const NameHandle malformedName("Diagnostics/MalformedZone");
                EmitRawEvent(RawEvent{.timestampNs = TimestampNowNs(),
                                      .correlationId = token.correlationId,
                                      .name = malformedName.GetId(),
                                      .kind = EventKind::Malformed,
                                      .category = Category::Core});
                return false;
            }

            const bool wellFormed = found + 1 == context.zoneDepth &&
                                    token.depth == static_cast<std::uint32_t>(found);
            if (!wellFormed)
                state.malformedZones.fetch_add(1, std::memory_order_relaxed);
            const std::uint64_t now = TimestampNowNs();
            while (context.zoneDepth > found)
            {
                const ActiveZone zone = context.zones[context.zoneDepth - 1];
                --context.zoneDepth;
                (void)context.Push(RawEvent{.timestampNs = zone.startTimestampNs,
                                            .durationNs = now - zone.startTimestampNs,
                                            .frameNumber = state.currentFrameNumber.load(
                                                std::memory_order_relaxed),
                                            .threadId = context.threadId,
                                            .correlationId = zone.correlationId,
                                            .parentCorrelationId = zone.parentCorrelationId,
                                            .name = zone.name,
                                            .kind = wellFormed || zone.correlationId != token.correlationId
                                                ? EventKind::Zone : EventKind::Malformed,
                                            .category = zone.category});
            }
            return wellFormed;
        }
        catch (...)
        {
            state.producerDrops.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
#else
        (void)token;
        return true;
#endif
    }

    ZoneScope::ZoneScope(const NameHandle& name, Category category) noexcept
        : token_(BeginZone(name, category))
    {
    }

    ZoneScope::~ZoneScope()
    {
        (void)EndZone(token_);
    }

    void MarkEvent(const NameHandle& name, Category category, std::int64_t value) noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 2
        if (name.GetId() == 0)
            return;
        EmitRawEvent(RawEvent{.timestampNs = TimestampNowNs(),
                              .value = value,
                              .name = name.GetId(),
                              .kind = EventKind::Marker,
                              .category = category});
#else
        (void)name;
        (void)category;
        (void)value;
#endif
    }

    void BeginFrame() noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        (void)TryBeginFrame();
#endif
    }

    void EndFrame() noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        FinishFrame(true);
#endif
    }

    FrameScope::FrameScope() noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        active_ = TryBeginFrame();
#endif
    }

    FrameScope::~FrameScope()
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        if (active_)
            FinishFrame(false);
#endif
    }

    IDiagnosticsProvider& GetProvider() noexcept
    {
        static ProcessProvider provider;
        return provider;
    }

    Mode GetRuntimeMode() noexcept
    {
#if CNA_DIAGNOSTICS_LEVEL >= 1
        return GetState().runtimeMode.load(std::memory_order_relaxed);
#else
        return Mode::Off;
#endif
    }

    bool SetRuntimeMode(Mode mode) noexcept
    {
        if (static_cast<unsigned int>(mode) > static_cast<unsigned int>(GetBuildMode()))
            return false;
#if CNA_DIAGNOSTICS_LEVEL >= 1
        State& state = GetState();
        const Mode previous = state.runtimeMode.exchange(mode, std::memory_order_relaxed);
        if (previous != mode)
            state.modeGeneration.fetch_add(1, std::memory_order_relaxed);
        return true;
#else
        return mode == Mode::Off;
#endif
    }

    std::string Trace::ResolveName(NameId id) const
    {
        const auto found = std::find_if(names_.begin(), names_.end(),
            [id](const auto& entry) { return entry.first == id; });
        return found == names_.end() ? std::string{} : found->second;
    }

    bool Trace::WriteBinary(std::ostream& output) const
    {
        static constexpr std::array<char, 8> Magic{'C', 'N', 'A', 'T', 'R', 'A', 'C', 'E'};
        output.write(Magic.data(), static_cast<std::streamsize>(Magic.size()));
        WriteLittleEndian(output, version_);
        WriteLittleEndian(output, std::uint16_t{0});
        WriteLittleEndian(output, static_cast<std::uint32_t>(names_.size()));
        WriteLittleEndian(output, static_cast<std::uint64_t>(events_.size()));
        WriteLittleEndian(output, droppedEvents_);
        for (const auto& [id, name] : names_)
        {
            WriteLittleEndian(output, id);
            WriteLittleEndian(output, static_cast<std::uint32_t>(name.size()));
            output.write(name.data(), static_cast<std::streamsize>(name.size()));
        }
        for (const EventRecord& event : events_)
        {
            WriteLittleEndian(output, event.sequence);
            WriteLittleEndian(output, event.timestampNs);
            WriteLittleEndian(output, event.durationNs);
            WriteLittleEndian(output, event.frameNumber);
            WriteLittleEndian(output, event.threadId);
            WriteLittleEndian(output, event.correlationId);
            WriteLittleEndian(output, event.parentCorrelationId);
            WriteLittleEndian(output, event.value);
            WriteLittleEndian(output, event.name);
            WriteLittleEndian(output, static_cast<std::uint8_t>(event.kind));
            WriteLittleEndian(output, static_cast<std::uint8_t>(event.category));
            WriteLittleEndian(output, std::uint16_t{0});
        }
        return static_cast<bool>(output);
    }

    Trace Trace::ReadBinary(std::istream& input)
    {
        static constexpr std::array<char, 8> Magic{'C', 'N', 'A', 'T', 'R', 'A', 'C', 'E'};
        std::array<char, 8> magic{};
        input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (magic != Magic)
            throw std::runtime_error("Invalid CNA profiler trace magic");
        Trace trace;
        trace.version_ = ReadLittleEndian<std::uint16_t>(input);
        if (trace.version_ != FormatVersion)
            throw std::runtime_error("Unsupported CNA profiler trace version");
        (void)ReadLittleEndian<std::uint16_t>(input);
        const std::uint32_t nameCount = ReadLittleEndian<std::uint32_t>(input);
        const std::uint64_t eventCount = ReadLittleEndian<std::uint64_t>(input);
        trace.droppedEvents_ = ReadLittleEndian<std::uint64_t>(input);
        if (nameCount > MaximumTraceNames || eventCount > EventHistoryCapacity)
            throw std::runtime_error("CNA profiler trace exceeds bounded format limits");
        trace.names_.reserve(nameCount);
        std::size_t totalNameBytes = 0;
        for (std::uint32_t index = 0; index < nameCount; ++index)
        {
            const NameId id = ReadLittleEndian<NameId>(input);
            const std::uint32_t length = ReadLittleEndian<std::uint32_t>(input);
            if (length > MaximumTraceNameBytes)
                throw std::runtime_error("CNA profiler trace name is too large");
            if (totalNameBytes > MaximumTraceTotalNameBytes - length)
                throw std::runtime_error("CNA profiler trace names exceed the bounded limit");
            totalNameBytes += length;
            std::string name(length, '\0');
            input.read(name.data(), static_cast<std::streamsize>(name.size()));
            if (!input)
                throw std::runtime_error("Truncated CNA profiler trace name");
            trace.names_.emplace_back(id, std::move(name));
        }
        trace.events_.reserve(static_cast<std::size_t>(eventCount));
        for (std::uint64_t index = 0; index < eventCount; ++index)
        {
            EventRecord event;
            event.sequence = ReadLittleEndian<std::uint64_t>(input);
            event.timestampNs = ReadLittleEndian<std::uint64_t>(input);
            event.durationNs = ReadLittleEndian<std::uint64_t>(input);
            event.frameNumber = ReadLittleEndian<std::uint64_t>(input);
            event.threadId = ReadLittleEndian<std::uint64_t>(input);
            event.correlationId = ReadLittleEndian<std::uint64_t>(input);
            event.parentCorrelationId = ReadLittleEndian<std::uint64_t>(input);
            event.value = ReadLittleEndian<std::int64_t>(input);
            event.name = ReadLittleEndian<NameId>(input);
            event.kind = static_cast<EventKind>(ReadLittleEndian<std::uint8_t>(input));
            event.category = static_cast<Category>(ReadLittleEndian<std::uint8_t>(input));
            (void)ReadLittleEndian<std::uint16_t>(input);
            if (static_cast<unsigned int>(event.kind) >
                    static_cast<unsigned int>(EventKind::Malformed) ||
                static_cast<unsigned int>(event.category) >
                    static_cast<unsigned int>(Category::Gpu))
            {
                throw std::runtime_error("CNA profiler trace contains an invalid event enum");
            }
            trace.events_.push_back(event);
        }
        return trace;
    }

    bool Trace::WriteChromeTrace(std::ostream& output) const
    {
        output << "{\"traceEvents\":[";
        bool first = true;
        for (const EventRecord& event : events_)
        {
            if (!first)
                output.put(',');
            first = false;
            output << "{\"name\":";
            WriteJsonString(output, ResolveName(event.name));
            output << ",\"cat\":\"cna\",\"ph\":\"";
            if (event.kind == EventKind::Zone || event.kind == EventKind::Frame)
                output << 'X';
            else
                output << 'i';
            output << "\",\"ts\":" << (event.timestampNs / 1000U)
                   << ",\"pid\":1,\"tid\":" << event.threadId;
            if (event.kind == EventKind::Zone || event.kind == EventKind::Frame)
                output << ",\"dur\":" << (event.durationNs / 1000U);
            else
                output << ",\"s\":\"t\"";
            output << ",\"args\":{\"value\":" << event.value
                   << ",\"frame\":" << event.frameNumber << "}}";
        }
        output << "],\"displayTimeUnit\":\"ns\",\"cnaDroppedEvents\":"
               << droppedEvents_ << '}';
        return static_cast<bool>(output);
    }

    RecordingSession StartRecording(std::size_t maximumEvents) noexcept
    {
        RecordingSession session;
#if CNA_DIAGNOSTICS_LEVEL >= 2
        if (GetRuntimeMode() != Mode::Full || maximumEvents == 0)
            return session;
        CollectEvents();
        State& state = GetState();
        const std::lock_guard lock(state.historyMutex);
        session.startSequence_ = state.nextEventSequence - 1;
        session.maximumEvents_ = std::min(maximumEvents, EventHistoryCapacity);
        session.startingProducerDrops_ = state.producerDrops.load(std::memory_order_relaxed);
        session.active_ = true;
#else
        (void)maximumEvents;
#endif
        return session;
    }

    RecordingSession::RecordingSession(RecordingSession&& other) noexcept
        : startSequence_(other.startSequence_)
        , maximumEvents_(other.maximumEvents_)
        , startingProducerDrops_(other.startingProducerDrops_)
        , active_(other.active_)
    {
        other.active_ = false;
    }

    RecordingSession& RecordingSession::operator=(RecordingSession&& other) noexcept
    {
        if (this != &other)
        {
            startSequence_ = other.startSequence_;
            maximumEvents_ = other.maximumEvents_;
            startingProducerDrops_ = other.startingProducerDrops_;
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }

    Trace StopRecording(RecordingSession& session)
    {
        Trace trace;
#if CNA_DIAGNOSTICS_LEVEL >= 2
        if (!session.active_)
            return trace;
        session.active_ = false;
        CollectEvents();
        State& state = GetState();
        {
            const std::lock_guard historyLock(state.historyMutex);
            if (state.historyCount != 0)
            {
                const std::size_t oldestIndex =
                    (state.historyWriteIndex + EventHistoryCapacity - state.historyCount) %
                    EventHistoryCapacity;
                const std::uint64_t oldestSequence = state.history[oldestIndex].sequence;
                if (session.startSequence_ + 1 < oldestSequence)
                    trace.droppedEvents_ += oldestSequence - (session.startSequence_ + 1);

                std::size_t matchingCount = 0;
                for (std::size_t offset = 0; offset < state.historyCount; ++offset)
                {
                    const EventRecord& event =
                        state.history[(oldestIndex + offset) % EventHistoryCapacity];
                    if (event.sequence > session.startSequence_)
                        ++matchingCount;
                }
                const std::size_t skip = matchingCount > session.maximumEvents_
                    ? matchingCount - session.maximumEvents_ : 0;
                trace.droppedEvents_ += skip;
                trace.events_.reserve(std::min(matchingCount, session.maximumEvents_));
                std::size_t seen = 0;
                for (std::size_t offset = 0; offset < state.historyCount; ++offset)
                {
                    const EventRecord& event =
                        state.history[(oldestIndex + offset) % EventHistoryCapacity];
                    if (event.sequence <= session.startSequence_)
                        continue;
                    if (seen++ >= skip)
                        trace.events_.push_back(event);
                }
            }
            const std::uint64_t producerDrops =
                state.producerDrops.load(std::memory_order_relaxed);
            trace.droppedEvents_ += producerDrops - session.startingProducerDrops_;
        }
        {
            const std::lock_guard nameLock(state.nameMutex);
            trace.names_.reserve(state.names.size() > 0 ? state.names.size() - 1 : 0);
            for (NameId id = 1; id < state.names.size(); ++id)
                trace.names_.emplace_back(id, state.names[id]);
        }
#else
        (void)session;
#endif
        return trace;
    }
}
