// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef CNA_DIAGNOSTICS_LEVEL
#define CNA_DIAGNOSTICS_LEVEL 0
#endif

#if CNA_DIAGNOSTICS_LEVEL < 0 || CNA_DIAGNOSTICS_LEVEL > 2
#error "CNA_DIAGNOSTICS_LEVEL must be 0 (OFF), 1 (STATS), or 2 (FULL)"
#endif

namespace CNA::Diagnostics
{
    /** @brief Compile-time and runtime diagnostics mode. */
    enum class Mode : std::uint8_t
    {
        /** @brief No diagnostics work is performed. */
        Off = 0,
        /** @brief Frame statistics, counters, gauges, and resource metadata are retained. */
        Stats = 1,
        /** @brief Full timing zones, markers, event history, and recording are retained. */
        Full = 2
    };

    /** @brief Unit attached to a diagnostic metric. */
    enum class MetricUnit : std::uint8_t
    {
        /** @brief Dimensionless value. */
        Count,
        /** @brief Byte count. */
        Bytes,
        /** @brief Nanosecond duration. */
        Nanoseconds,
        /** @brief Rate in events per second. */
        PerSecond,
        /** @brief Percentage represented in basis points, where 10000 is 100 percent. */
        BasisPoints
    };

    /** @brief Semantic behavior of a metric. */
    enum class MetricKind : std::uint8_t
    {
        /** @brief Monotonically accumulated counter. */
        Counter,
        /** @brief Last-value gauge. */
        Gauge,
        /** @brief Counter exchanged to zero at each frame boundary. */
        FrameCounter
    };

    /** @brief Accuracy classification for a diagnostic value. */
    enum class Accuracy : std::uint8_t
    {
        /** @brief Value is known exactly at the CNA abstraction layer. */
        Exact,
        /** @brief Value is a documented approximation. */
        Estimated,
        /** @brief The platform cannot currently provide the value. */
        Unavailable
    };

    /** @brief Functional category attached to a zone or event. */
    enum class Category : std::uint8_t
    {
        /** @brief General engine work. */
        Core,
        /** @brief Game update work. */
        Update,
        /** @brief Game drawing work. */
        Draw,
        /** @brief Graphics work. */
        Graphics,
        /** @brief Audio work. */
        Audio,
        /** @brief Content work. */
        Content,
        /** @brief Application-defined work. */
        Application,
        /** @brief GPU work reported asynchronously by a renderer. */
        Gpu
    };

    /** @brief Type of an event retained in the full profiler stream. */
    enum class EventKind : std::uint8_t
    {
        /** @brief Completed CPU timing zone. */
        Zone,
        /** @brief Instantaneous marker. */
        Marker,
        /** @brief Completed frame. */
        Frame,
        /** @brief Resource registration. */
        ResourceCreated,
        /** @brief Resource unregistration. */
        ResourceDestroyed,
        /** @brief Malformed manual-zone usage. */
        Malformed
    };

    /** @brief Kind of engine-owned resource exposed as metadata. */
    enum class ResourceKind : std::uint8_t
    {
        /** @brief Resource whose more specific kind is unavailable. */
        Unknown,
        /** @brief Two-dimensional texture. */
        Texture2D,
        /** @brief Three-dimensional texture. */
        Texture3D,
        /** @brief Cube texture. */
        TextureCube,
        /** @brief Vertex buffer. */
        VertexBuffer,
        /** @brief Index buffer. */
        IndexBuffer,
        /** @brief Two-dimensional render target. */
        RenderTarget2D,
        /** @brief Cube render target. */
        RenderTargetCube,
        /** @brief Audio mixer voice or track. */
        AudioVoice,
        /** @brief Application-defined resource. */
        Custom
    };

    using NameId = std::uint32_t;
    using MetricId = std::uint32_t;
    using ResourceId = std::uint64_t;

    /** @brief Maximum nesting depth retained for one profiling thread. */
    inline constexpr std::size_t MaximumZoneDepth = 64;
    /** @brief Events retained before one producer thread must drop new events. */
    inline constexpr std::size_t ThreadEventCapacity = 1024;
    /** @brief Events retained in the process-wide rolling history. */
    inline constexpr std::size_t EventHistoryCapacity = 32768;
    /** @brief Frames retained in the rolling frame history. */
    inline constexpr std::size_t FrameHistoryCapacity = 240;

    /** @brief Immutable registered name used by zones and markers. */
    class NameHandle
    {
    public:
        /**
         * @brief Registers a diagnostic name.
         * @param name Stable name to copy into the registry.
         */
        explicit NameHandle(std::string_view name) noexcept;

        /**
         * @brief Returns the registered identifier, or zero when unavailable.
         * @return Registered name identifier.
         */
        [[nodiscard]] NameId GetId() const noexcept { return id_; }

    private:
        NameId id_ = 0;
    };

    /** @brief Handle for a cumulative counter. */
    class CounterHandle
    {
    public:
        /**
         * @brief Registers a counter.
         * @param name Stable slash-separated metric name.
         * @param unit Unit of the counter.
         * @param accuracy Accuracy classification.
         */
        explicit CounterHandle(std::string_view name,
                               MetricUnit unit = MetricUnit::Count,
                               Accuracy accuracy = Accuracy::Exact) noexcept;

        /**
         * @brief Adds a delta to the counter.
         * @param delta Signed amount to add.
         */
        void Add(std::int64_t delta = 1) const noexcept;

        /**
         * @brief Returns the registered metric identifier, or zero when unavailable.
         * @return Registered metric identifier.
         */
        [[nodiscard]] MetricId GetId() const noexcept { return id_; }

    private:
        MetricId id_ = 0;
    };

    /** @brief Handle for a last-value gauge. */
    class GaugeHandle
    {
    public:
        /**
         * @brief Registers a gauge.
         * @param name Stable slash-separated metric name.
         * @param unit Unit of the gauge.
         * @param accuracy Accuracy classification.
         */
        explicit GaugeHandle(std::string_view name,
                             MetricUnit unit = MetricUnit::Count,
                             Accuracy accuracy = Accuracy::Exact) noexcept;

        /**
         * @brief Replaces the gauge value.
         * @param value New gauge value.
         */
        void Set(std::int64_t value) const noexcept;

        /**
         * @brief Adds a signed delta to the gauge.
         * @param delta Amount to add.
         */
        void Add(std::int64_t delta) const noexcept;

        /**
         * @brief Returns the registered metric identifier, or zero when unavailable.
         * @return Registered metric identifier.
         */
        [[nodiscard]] MetricId GetId() const noexcept { return id_; }

    private:
        MetricId id_ = 0;
    };

    /** @brief Handle for a counter reset at each completed frame. */
    class FrameCounterHandle
    {
    public:
        /**
         * @brief Registers a frame counter.
         * @param name Stable slash-separated metric name.
         * @param unit Unit of the counter.
         * @param accuracy Accuracy classification.
         */
        explicit FrameCounterHandle(std::string_view name,
                                    MetricUnit unit = MetricUnit::Count,
                                    Accuracy accuracy = Accuracy::Exact) noexcept;

        /**
         * @brief Adds a delta to the current frame.
         * @param delta Signed amount to add.
         */
        void Add(std::int64_t delta = 1) const noexcept;

        /**
         * @brief Returns the registered metric identifier, or zero when unavailable.
         * @return Registered metric identifier.
         */
        [[nodiscard]] MetricId GetId() const noexcept { return id_; }

    private:
        MetricId id_ = 0;
    };

    /** @brief Restricted write surface passed to optional subsystem and renderer sources. */
    class FrameStatisticsSink
    {
    public:
        /**
         * @brief Publishes a gauge obtained asynchronously by a source.
         * @param gauge Pre-registered gauge handle owned by the source.
         * @param value Latest available value.
         */
        void Set(const GaugeHandle& gauge, std::int64_t value) const noexcept
        {
            gauge.Set(value);
        }

        /**
         * @brief Adds work to the current frame.
         * @param counter Pre-registered frame counter owned by the source.
         * @param delta Signed amount to add.
         */
        void Add(const FrameCounterHandle& counter, std::int64_t delta = 1) const noexcept
        {
            counter.Add(delta);
        }
    };

    /**
     * @brief Optional frame-boundary source for renderer- or subsystem-specific statistics.
     *
     * Implementations must return promptly and publish only already-available data. A GPU source
     * must poll asynchronous queries and report unavailable results; it must never wait for the
     * device or queue from this callback.
     */
    class IDiagnosticsSource
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IDiagnosticsSource() = default;

        /**
         * @brief Publishes the latest non-blocking statistics at a frame boundary.
         * @param sink Restricted metric writer.
         */
        virtual void Collect(FrameStatisticsSink& sink) = 0;
    };

    /** @brief RAII registration for an optional diagnostics source. */
    class SourceRegistration
    {
    public:
        /** @brief Creates an empty registration. */
        SourceRegistration() noexcept = default;
        /** @brief Unregisters the source. */
        ~SourceRegistration();

        /** @brief Source registrations cannot be copied. */
        SourceRegistration(const SourceRegistration&) = delete;
        /** @brief Source registrations cannot be copy-assigned. */
        SourceRegistration& operator=(const SourceRegistration&) = delete;

        /**
         * @brief Move-constructs a registration.
         * @param other Registration to consume.
         */
        SourceRegistration(SourceRegistration&& other) noexcept;
        /**
         * @brief Move-assigns a registration.
         * @param other Registration to consume.
         * @return This registration.
         */
        SourceRegistration& operator=(SourceRegistration&& other) noexcept;

        /** @brief Unregisters the source and empties this registration. */
        void Reset() noexcept;
        /**
         * @brief Returns whether a source is registered.
         * @return True when this object owns a live registration.
         */
        [[nodiscard]] bool IsRegistered() const noexcept { return id_ != 0; }

    private:
        friend SourceRegistration RegisterSource(std::shared_ptr<IDiagnosticsSource> source) noexcept;
        explicit SourceRegistration(std::uint64_t id) noexcept : id_(id) {}
        std::uint64_t id_ = 0;
    };

    /**
     * @brief Registers an optional subsystem or renderer diagnostics source.
     * @param source Shared source lifetime retained until registration is reset.
     * @return RAII registration, empty when diagnostics are compiled out or source is null.
     */
    [[nodiscard]] SourceRegistration RegisterSource(
        std::shared_ptr<IDiagnosticsSource> source) noexcept;

    /** @brief One metric value returned to a diagnostics consumer. */
    struct MetricSample
    {
        MetricId id = 0;
        std::string name;
        std::int64_t value = 0;
        MetricKind kind = MetricKind::Gauge;
        MetricUnit unit = MetricUnit::Count;
        Accuracy accuracy = Accuracy::Exact;
    };

    /** @brief One completed engine frame and its frame-scoped metrics. */
    struct FrameSample
    {
        std::uint64_t frameNumber = 0;
        std::uint64_t startTimestampNs = 0;
        std::uint64_t durationNs = 0;
        double framesPerSecond = 0.0;
        std::vector<MetricSample> metrics;
    };

    /** @brief One retained full-profiler event. */
    struct EventRecord
    {
        std::uint64_t sequence = 0;
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

    /** @brief Result of reading the bounded event history. */
    struct EventBatch
    {
        std::vector<EventRecord> events;
        std::uint64_t oldestAvailableSequence = 0;
        std::uint64_t newestAvailableSequence = 0;
        std::uint64_t eventsDroppedBeforeStart = 0;
        std::uint64_t producerEventsDropped = 0;
    };

    /** @brief Metadata supplied when registering an engine resource. */
    struct ResourceDescriptor
    {
        ResourceKind kind = ResourceKind::Unknown;
        std::string_view label;
        std::string_view format;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t depth = 0;
        std::uint32_t mipCount = 0;
        std::uint64_t estimatedBytes = 0;
        Accuracy byteAccuracy = Accuracy::Unavailable;
    };

    /** @brief Owned resource metadata returned to a diagnostics consumer. */
    struct ResourceRecord
    {
        ResourceId id = 0;
        ResourceKind kind = ResourceKind::Unknown;
        std::string label;
        std::string format;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t depth = 0;
        std::uint32_t mipCount = 0;
        std::uint64_t estimatedBytes = 0;
        Accuracy byteAccuracy = Accuracy::Unavailable;
    };

    /** @brief RAII registration for resource metadata. */
    class ResourceHandle
    {
    public:
        /** @brief Creates an empty resource handle. */
        ResourceHandle() noexcept = default;

        /**
         * @brief Registers a resource.
         * @param descriptor Metadata to copy into the registry.
         */
        explicit ResourceHandle(const ResourceDescriptor& descriptor) noexcept;

        /** @brief Unregisters the resource, if any. */
        ~ResourceHandle();

        /** @brief Resource handles cannot be copied. */
        ResourceHandle(const ResourceHandle&) = delete;
        /** @brief Resource handles cannot be copy-assigned. */
        ResourceHandle& operator=(const ResourceHandle&) = delete;

        /**
         * @brief Move-constructs a handle while preserving its stable diagnostic ID.
         * @param other Handle to consume.
         */
        ResourceHandle(ResourceHandle&& other) noexcept;
        /**
         * @brief Move-assigns a handle while preserving its stable diagnostic ID.
         * @param other Handle to consume.
         * @return This handle.
         */
        ResourceHandle& operator=(ResourceHandle&& other) noexcept;

        /**
         * @brief Replaces the registered metadata without changing the ID.
         * @param descriptor New metadata.
         */
        void Update(const ResourceDescriptor& descriptor) noexcept;

        /** @brief Unregisters the resource and empties this handle. */
        void Reset() noexcept;

        /**
         * @brief Returns the stable resource identifier, or zero for an empty handle.
         * @return Stable diagnostic resource identifier.
         */
        [[nodiscard]] ResourceId GetId() const noexcept { return id_; }

    private:
        ResourceId id_ = 0;
    };

    /** @brief Token returned by the manual zone-begin API. */
    struct ZoneToken
    {
        std::uint64_t correlationId = 0;
        std::uint64_t threadId = 0;
        std::uint64_t modeGeneration = 0;
        std::uint32_t depth = 0;
        bool active = false;
    };

    /**
     * @brief Begins a hierarchical CPU timing zone.
     * @param name Registered static name.
     * @param category Functional category.
     * @return Token that must be passed to EndZone on the same thread.
     */
    [[nodiscard]] ZoneToken BeginZone(const NameHandle& name,
                                      Category category = Category::Application) noexcept;

    /**
     * @brief Ends a manual CPU timing zone.
     * @param token Token returned by BeginZone.
     * @return True when the token matched the top zone; false for malformed use.
     */
    [[nodiscard]] bool EndZone(ZoneToken token) noexcept;

    /** @brief Scope guard for one hierarchical CPU timing zone. */
    class ZoneScope
    {
    public:
        /**
         * @brief Begins a zone for this lexical scope.
         * @param name Registered static name.
         * @param category Functional category.
         */
        explicit ZoneScope(const NameHandle& name,
                           Category category = Category::Application) noexcept;
        /** @brief Ends the zone if it began successfully. */
        ~ZoneScope();

        /** @brief Zone scopes cannot be copied. */
        ZoneScope(const ZoneScope&) = delete;
        /** @brief Zone scopes cannot be copy-assigned. */
        ZoneScope& operator=(const ZoneScope&) = delete;

    private:
        ZoneToken token_{};
    };

    /**
     * @brief Emits an instantaneous event marker.
     * @param name Registered static name.
     * @param category Functional category.
     * @param value Optional signed payload.
     */
    void MarkEvent(const NameHandle& name,
                   Category category = Category::Application,
                   std::int64_t value = 0) noexcept;

    /** @brief Begins an engine frame if statistics are active. */
    void BeginFrame() noexcept;
    /** @brief Ends an engine frame and publishes its statistics. */
    void EndFrame() noexcept;

    /** @brief Scope guard that publishes one engine frame. */
    class FrameScope
    {
    public:
        /** @brief Begins a frame. */
        FrameScope() noexcept;
        /** @brief Ends the frame. */
        ~FrameScope();

        /** @brief Frame scopes cannot be copied. */
        FrameScope(const FrameScope&) = delete;
        /** @brief Frame scopes cannot be copy-assigned. */
        FrameScope& operator=(const FrameScope&) = delete;

    private:
        bool active_ = false;
    };

    /** @brief Snapshot of the latest bounded diagnostics state. */
    struct Snapshot
    {
        Mode buildMode = Mode::Off;
        Mode runtimeMode = Mode::Off;
        std::uint64_t currentFrameNumber = 0;
        std::uint64_t malformedZoneCount = 0;
        std::uint64_t malformedFrameCount = 0;
        std::uint64_t producerEventsDropped = 0;
        std::uint64_t eventHistoryOverwrites = 0;
        std::uint64_t sourceCollectionFailures = 0;
        std::uint64_t profilerOwnedBytes = 0;
        std::uint64_t registeredResourceBytes = 0;
        std::vector<MetricSample> metrics;
        std::vector<FrameSample> recentFrames;
        std::vector<ResourceRecord> resources;
    };

    /** @brief Pull-only provider interface for tools such as the future CNA Inspector. */
    class IDiagnosticsProvider
    {
    public:
        /** @brief Current in-process provider contract version. */
        static constexpr std::uint16_t InterfaceVersion = 1;

        /** @brief Virtual destructor. */
        virtual ~IDiagnosticsProvider() = default;

        /**
         * @brief Captures metrics, frame history, and resource metadata on demand.
         * @return Owned point-in-time snapshot.
         */
        [[nodiscard]] virtual Snapshot CaptureSnapshot() = 0;

        /**
         * @brief Reads retained events newer than a sequence number.
         * @param afterSequence Last sequence already consumed, or zero initially.
         * @param maximumEvents Maximum number of records to return.
         * @return Bounded event batch and overflow information.
         */
        [[nodiscard]] virtual EventBatch ReadEvents(std::uint64_t afterSequence,
                                                    std::size_t maximumEvents) = 0;

        /**
         * @brief Resolves a registered event name.
         * @param id Registered name identifier.
         * @return Owned name, or an empty string for an unknown identifier.
         */
        [[nodiscard]] virtual std::string ResolveName(NameId id) = 0;
    };

    /**
     * @brief Returns the process diagnostics provider.
     * @return Process-lifetime pull provider.
     */
    [[nodiscard]] IDiagnosticsProvider& GetProvider() noexcept;

    /**
     * @brief Returns the maximum mode compiled into this build.
     * @return Compile-time diagnostics mode.
     */
    [[nodiscard]] constexpr Mode GetBuildMode() noexcept
    {
        return static_cast<Mode>(CNA_DIAGNOSTICS_LEVEL);
    }

    /**
     * @brief Returns the current runtime mode.
     * @return Active diagnostics mode.
     */
    [[nodiscard]] Mode GetRuntimeMode() noexcept;

    /**
     * @brief Selects a runtime mode no higher than the compiled build mode.
     * @param mode Requested mode.
     * @return True if accepted; false when the build cannot provide that mode.
     */
    [[nodiscard]] bool SetRuntimeMode(Mode mode) noexcept;

    /** @brief Versioned, owned trace recording suitable for offline export. */
    class Trace
    {
    public:
        /** @brief Current CNA profiler trace format version. */
        static constexpr std::uint16_t FormatVersion = 1;

        /**
         * @brief Returns the trace format version.
         * @return Version number encoded by this trace.
         */
        [[nodiscard]] std::uint16_t GetVersion() const noexcept { return version_; }
        /**
         * @brief Returns the recorded events.
         * @return Immutable event sequence.
         */
        [[nodiscard]] const std::vector<EventRecord>& GetEvents() const noexcept { return events_; }
        /**
         * @brief Returns the number of events lost before or during this recording.
         * @return Combined producer, history, and recording-capacity drop count.
         */
        [[nodiscard]] std::uint64_t GetDroppedEventCount() const noexcept { return droppedEvents_; }

        /**
         * @brief Resolves a trace-local event name.
         * @param id Registered name identifier.
         * @return Name, or an empty string if absent.
         */
        [[nodiscard]] std::string ResolveName(NameId id) const;

        /**
         * @brief Writes the compact little-endian CNA trace format.
         * @param output Destination stream.
         * @return True if every byte was written.
         */
        [[nodiscard]] bool WriteBinary(std::ostream& output) const;

        /**
         * @brief Reads and validates the compact CNA trace format.
         * @param input Source stream.
         * @return Decoded trace; throws std::runtime_error on malformed input.
         */
        [[nodiscard]] static Trace ReadBinary(std::istream& input);

        /**
         * @brief Streams Chrome Trace Event JSON without constructing a giant JSON document.
         * @param output Destination stream.
         * @return True if the stream remained writable.
         */
        [[nodiscard]] bool WriteChromeTrace(std::ostream& output) const;

    private:
        friend class RecordingSession;
        friend Trace StopRecording(class RecordingSession& session);
        std::uint16_t version_ = FormatVersion;
        std::uint64_t droppedEvents_ = 0;
        std::vector<std::pair<NameId, std::string>> names_;
        std::vector<EventRecord> events_;
    };

    /** @brief Bounded recording cursor over the process event history. */
    class RecordingSession
    {
    public:
        /** @brief Creates an inactive recording session. */
        RecordingSession() noexcept = default;

        /** @brief Recording sessions cannot be copied. */
        RecordingSession(const RecordingSession&) = delete;
        /** @brief Recording sessions cannot be copy-assigned. */
        RecordingSession& operator=(const RecordingSession&) = delete;

        /**
         * @brief Move-constructs a recording cursor.
         * @param other Session to consume.
         */
        RecordingSession(RecordingSession&& other) noexcept;
        /**
         * @brief Move-assigns a recording cursor.
         * @param other Session to consume.
         * @return This session.
         */
        RecordingSession& operator=(RecordingSession&& other) noexcept;

        /**
         * @brief Returns whether this session can still be stopped.
         * @return True for an active session.
         */
        [[nodiscard]] bool IsActive() const noexcept { return active_; }

    private:
        friend RecordingSession StartRecording(std::size_t maximumEvents) noexcept;
        friend Trace StopRecording(RecordingSession& session);
        std::uint64_t startSequence_ = 0;
        std::size_t maximumEvents_ = 0;
        std::uint64_t startingProducerDrops_ = 0;
        bool active_ = false;
    };

    /**
     * @brief Starts a bounded recording cursor.
     * @param maximumEvents Maximum events retained in the returned trace, clamped to history size.
     * @return Active session in FULL mode, otherwise an inactive session.
     */
    [[nodiscard]] RecordingSession StartRecording(std::size_t maximumEvents) noexcept;

    /**
     * @brief Stops a recording session and copies its bounded events.
     * @param session Active session to consume.
     * @return Owned trace; empty when the session was inactive.
     */
    [[nodiscard]] Trace StopRecording(RecordingSession& session);
}
