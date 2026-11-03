// SPDX-License-Identifier: MS-PL

#include "CNA/Inspector/Agent.hpp"
#include "CNA/Inspector/Client.hpp"
#include "CNA/Inspector/Protocol.hpp"
#include "../../../src/InternalSocket.hpp"
#include "../../../src/WebAssets.hpp"
#include "../../../src/WebBridge.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>
#endif

namespace CNA::Inspector
{
    namespace
    {
        using namespace std::chrono_literals;

        class FakeProvider final : public Diagnostics::IDiagnosticsProvider
        {
        public:
            Diagnostics::Snapshot CaptureSnapshot() override
            {
                ++snapshotCalls;
                return snapshot;
            }

            Diagnostics::EventBatch ReadEvents(std::uint64_t afterSequence,
                                               std::size_t maximumEvents) override
            {
                ++eventCalls;
                lastAfterSequence = afterSequence;
                lastMaximumEvents = maximumEvents;
                return events;
            }

            std::string ResolveName(Diagnostics::NameId id) override
            {
                ++resolveCalls;
                return id == 7 ? "Game/Update" : "unknown";
            }

            Diagnostics::Snapshot snapshot;
            Diagnostics::EventBatch events;
            std::atomic<int> snapshotCalls{0};
            std::atomic<int> eventCalls{0};
            std::atomic<int> resolveCalls{0};
            std::uint64_t lastAfterSequence = 0;
            std::size_t lastMaximumEvents = 0;
        };

        class FakePreviewProvider final : public IResourcePreviewProvider
        {
        public:
            PreviewResponse RequestPreview(const PreviewRequest& request) override
            {
                ++requestCalls;
                lastResourceId = request.resourceId;
                if (returnInvalidImage)
                {
                    PreviewResponse response;
                    response.status = PreviewStatus::Ready;
                    response.width = 5000;
                    response.height = 1;
                    response.mimeType = "image/png";
                    response.encodedImage = {1};
                    return response;
                }
                PreviewResponse response;
                response.status = PreviewStatus::Pending;
                response.ticket = 42;
                response.message = "readback pending";
                return response;
            }

            PreviewResponse PollPreview(std::uint64_t ticket) override
            {
                ++pollCalls;
                PreviewResponse response;
                response.status = PreviewStatus::Ready;
                response.ticket = ticket;
                response.width = pollWidth;
                response.height = pollHeight;
                response.mimeType = "image/png";
                response.encodedImage = {0x89, 0x50, 0x4E, 0x47};
                return response;
            }

            int requestCalls = 0;
            int pollCalls = 0;
            Diagnostics::ResourceId lastResourceId = 0;
            std::uint32_t pollWidth = 1;
            std::uint32_t pollHeight = 1;
            bool returnInvalidImage = false;
        };

        Diagnostics::Snapshot MakeSnapshot()
        {
            Diagnostics::Snapshot snapshot;
            snapshot.buildMode = Diagnostics::Mode::Full;
            snapshot.runtimeMode = Diagnostics::Mode::Full;
            snapshot.currentFrameNumber = 99;
            snapshot.producerEventsDropped = 3;
            snapshot.eventHistoryOverwrites = 4;
            snapshot.profilerOwnedBytes = 4096;
            snapshot.registeredResourceBytes = 16384;
            snapshot.metrics.push_back({1, "Graphics/DrawCalls", 12,
                Diagnostics::MetricKind::FrameCounter, Diagnostics::MetricUnit::Count,
                Diagnostics::Accuracy::Exact});
            Diagnostics::FrameSample frame;
            frame.frameNumber = 99;
            frame.startTimestampNs = 1000;
            frame.durationNs = 16666667;
            frame.framesPerSecond = 60.0;
            frame.metrics = snapshot.metrics;
            snapshot.recentFrames.push_back(frame);
            snapshot.resources.push_back({17, Diagnostics::ResourceKind::Texture2D, "Albedo",
                "Color", 64, 32, 1, 1, 8192, Diagnostics::Accuracy::Estimated});
            return snapshot;
        }

        AgentConfiguration MakeAgentConfiguration()
        {
            AgentConfiguration configuration;
            configuration.authenticationToken = "test-token";
            configuration.applicationName = "Inspector tests";
            configuration.platformBackend = "TEST";
            configuration.buildConfiguration = "Test";
            configuration.maximumRequestsPerSecond = 64;
            configuration.previewCooldownMilliseconds = 50;
            return configuration;
        }

        ClientConfiguration MakeClientConfiguration(const Agent& agent)
        {
            ClientConfiguration configuration;
            configuration.host = agent.GetBindAddress();
            configuration.port = agent.GetPort();
            configuration.authenticationToken = agent.GetAuthenticationToken();
            configuration.timeoutMilliseconds = 2000;
            return configuration;
        }

        std::unique_ptr<Agent> StartAgent(FakeProvider& provider,
                                          AgentConfiguration configuration)
        {
            std::string error;
            auto agent = Agent::Start(provider, configuration, error);
            EXPECT_NE(agent, nullptr) << error;
            return agent;
        }

        Packet RawRoundTrip(const Agent& agent, const Packet& request)
        {
            std::string error;
            const auto socket = Detail::ConnectTcp(agent.GetBindAddress(), agent.GetPort(), 2000, error);
            EXPECT_NE(socket, Detail::InvalidSocket) << error;
            Packet response;
            EXPECT_TRUE(Detail::SendPacket(socket, request, 2000, error)) << error;
            EXPECT_TRUE(Detail::ReceivePacket(socket, response, 2000, error)) << error;
            Detail::ShutdownSocket(socket);
            Detail::CloseSocket(socket);
            return response;
        }
    }

    TEST(InspectorProtocolTests, HeaderRoundTripsExactly)
    {
        PacketHeader source;
        source.type = MessageType::EventsResponse;
        source.payloadBytes = 1234;
        source.requestId = 0x1122334455667788ULL;
        const auto bytes = ProtocolCodec::EncodeHeader(source);
        ASSERT_EQ(bytes.size(), 24U);
        PacketHeader decoded;
        std::string error;
        ASSERT_TRUE(ProtocolCodec::DecodeHeader(bytes, decoded, error)) << error;
        EXPECT_EQ(decoded.type, source.type);
        EXPECT_EQ(decoded.payloadBytes, source.payloadBytes);
        EXPECT_EQ(decoded.requestId, source.requestId);
    }

    TEST(InspectorProtocolTests, RejectsInvalidAndOversizedHeaders)
    {
        PacketHeader header;
        auto bytes = ProtocolCodec::EncodeHeader(header);
        std::string error;
        bytes[0] = 0;
        EXPECT_FALSE(ProtocolCodec::DecodeHeader(bytes, header, error));

        bytes = ProtocolCodec::EncodeHeader(PacketHeader{});
        const auto overLimit = static_cast<std::uint32_t>(MaximumPayloadBytes + 1);
        for (std::size_t i = 0; i < 4; ++i)
        {
            bytes[12 + i] = static_cast<std::uint8_t>((overLimit >> (i * 8U)) & 0xFFU);
        }
        error.clear();
        EXPECT_FALSE(ProtocolCodec::DecodeHeader(bytes, header, error));

        bytes = ProtocolCodec::EncodeHeader(PacketHeader{});
        bytes[8] = 4;
        bytes[9] = 0;
        error.clear();
        EXPECT_FALSE(ProtocolCodec::DecodeHeader(bytes, header, error));
    }

    TEST(InspectorProtocolTests, SnapshotRoundTripPreservesAccuracyAndLossCounters)
    {
        SnapshotResponse source;
        source.includedParts = static_cast<std::uint32_t>(SnapshotPart::Metrics)
            | static_cast<std::uint32_t>(SnapshotPart::Frames)
            | static_cast<std::uint32_t>(SnapshotPart::Resources);
        source.snapshot = MakeSnapshot();
        source.metricCount = 1;
        source.frameCount = 1;
        source.resourceCount = 1;
        const auto packet = ProtocolCodec::Encode(source, 9);
        SnapshotResponse decoded;
        std::string error;
        ASSERT_TRUE(ProtocolCodec::Decode(packet, decoded, error)) << error;
        EXPECT_EQ(decoded.snapshot.producerEventsDropped, 3U);
        EXPECT_EQ(decoded.snapshot.eventHistoryOverwrites, 4U);
        ASSERT_EQ(decoded.snapshot.resources.size(), 1U);
        EXPECT_EQ(decoded.snapshot.resources[0].byteAccuracy, Diagnostics::Accuracy::Estimated);
        EXPECT_EQ(decoded.snapshot.metrics[0].accuracy, Diagnostics::Accuracy::Exact);
    }

    TEST(InspectorProtocolTests, EventRoundTripPreservesCursorDiscontinuity)
    {
        EventsResponse source;
        source.oldestAvailableSequence = 40;
        source.newestAvailableSequence = 80;
        source.eventsDroppedBeforeStart = 12;
        source.producerEventsDropped = 7;
        source.events.push_back({{80, 1000, 20, 9, 4, 5, 0, -3, 7,
                                  Diagnostics::EventKind::Zone, Diagnostics::Category::Update},
                                 "Game/Update"});
        const auto packet = ProtocolCodec::Encode(source, 10);
        EventsResponse decoded;
        std::string error;
        ASSERT_TRUE(ProtocolCodec::Decode(packet, decoded, error)) << error;
        EXPECT_EQ(decoded.eventsDroppedBeforeStart, 12U);
        EXPECT_EQ(decoded.producerEventsDropped, 7U);
        ASSERT_EQ(decoded.events.size(), 1U);
        EXPECT_EQ(decoded.events[0].name, "Game/Update");
        EXPECT_EQ(decoded.events[0].event.value, -3);
    }

    // Seeded, so a failure reproduces from the seed alone. Every mutated payload must either be
    // rejected by its decoder or decode into a value the encoder can emit again. The encoder
    // enforces UTF-8 and every protocol bound, so re-encoding checks both at once; a decoder that
    // accepted what the encoder refuses would make the agent fail when it answered.
    template<typename Message>
    void FuzzDecoder(const Packet& seed,
                     std::mt19937& random,
                     int iterations,
                     std::size_t& accepted,
                     std::size_t& rejected)
    {
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            Packet packet = seed;
            auto& payload = packet.payload;
            const int mutations = 1 + static_cast<int>(random() % 4U);
            for (int mutation = 0; mutation < mutations && !payload.empty(); ++mutation)
            {
                switch (random() % 4U)
                {
                    case 0:
                        payload[random() % payload.size()] = static_cast<std::uint8_t>(random());
                        break;
                    case 1:
                        payload.resize(random() % payload.size());
                        break;
                    case 2:
                        payload.insert(payload.begin()
                                           + static_cast<std::ptrdiff_t>(random() % (payload.size() + 1)),
                                       static_cast<std::uint8_t>(random()));
                        break;
                    default:
                    {
                        // Aim at length and count fields with an extreme little-endian value.
                        const std::size_t at = random() % payload.size();
                        const std::uint32_t extreme = (random() & 1U) != 0 ? 0xFFFFFFFFU : 0x7FFFFFFFU;
                        for (std::size_t byte = 0; byte < 4 && at + byte < payload.size(); ++byte)
                            payload[at + byte] = static_cast<std::uint8_t>(extreme >> (byte * 8U));
                        break;
                    }
                }
            }
            packet.header.payloadBytes = static_cast<std::uint32_t>(payload.size());
            Message decoded;
            std::string error;
            bool decodedCleanly = false;
            ASSERT_NO_THROW(decodedCleanly = ProtocolCodec::Decode(packet, decoded, error))
                << "decoders report hostile input through their result, not by throwing";
            if (!decodedCleanly)
            {
                ++rejected;
                continue;
            }
            ++accepted;
            EXPECT_NO_THROW((void)ProtocolCodec::Encode(decoded, 1))
                << "decoder accepted a value the encoder refuses";
        }
    }

    TEST(InspectorProtocolTests, MutatedPayloadsAreRejectedOrDecodeIntoEncodableValues)
    {
        std::mt19937 random(0x1A5BEC7U);
        std::size_t accepted = 0;
        std::size_t rejected = 0;

        SnapshotResponse snapshot;
        snapshot.includedParts = static_cast<std::uint32_t>(SnapshotPart::Metrics)
            | static_cast<std::uint32_t>(SnapshotPart::Frames)
            | static_cast<std::uint32_t>(SnapshotPart::Resources);
        snapshot.snapshot = MakeSnapshot();
        snapshot.metricCount = 1;
        snapshot.frameCount = 1;
        snapshot.resourceCount = 1;
        FuzzDecoder<SnapshotResponse>(ProtocolCodec::Encode(snapshot, 1), random, 2000,
                                      accepted, rejected);

        EventsResponse events;
        events.oldestAvailableSequence = 1;
        events.newestAvailableSequence = 3;
        for (std::uint64_t sequence = 1; sequence <= 3; ++sequence)
        {
            ResolvedEvent event;
            event.event.sequence = sequence;
            event.event.name = 1;
            event.name = "Tests/Fuzz/Event";
            events.events.push_back(event);
        }
        FuzzDecoder<EventsResponse>(ProtocolCodec::Encode(events, 1), random, 2000,
                                    accepted, rejected);

        HelloRequest hello;
        hello.clientName = "fuzz-client";
        hello.authenticationToken = "fuzz-token";
        FuzzDecoder<HelloRequest>(ProtocolCodec::Encode(hello, 1), random, 2000,
                                  accepted, rejected);

        FuzzDecoder<EventsRequest>(ProtocolCodec::Encode(EventsRequest{5, 10}, 1), random, 1000,
                                   accepted, rejected);

        // Both outcomes must occur, or the corpus is not exercising anything.
        EXPECT_GT(rejected, 0U);
        EXPECT_GT(accepted, 0U);
        EXPECT_EQ(accepted + rejected, 7000U);
    }

    TEST(InspectorProtocolTests, RandomHeadersNeverAdmitAnOversizedPayload)
    {
        // The receive path allocates the payload from this field before reading it, so a header
        // that decodes must never carry a size above the protocol bound.
        PacketHeader validHeader;
        validHeader.type = MessageType::Ping;
        validHeader.requestId = 1;
        const std::vector<std::uint8_t> valid = ProtocolCodec::EncodeHeader(validHeader);
        ASSERT_EQ(valid.size(), 24U);

        std::mt19937 random(0x4EAD3E5U);
        std::size_t accepted = 0;
        for (int iteration = 0; iteration < 20000; ++iteration)
        {
            std::vector<std::uint8_t> bytes = valid;
            // Mostly keep the magic so mutations reach the fields behind it.
            const std::size_t first = (iteration % 4 == 0) ? 0U : 4U;
            const int mutations = 1 + static_cast<int>(random() % 6U);
            for (int mutation = 0; mutation < mutations; ++mutation)
                bytes[first + random() % (bytes.size() - first)] = static_cast<std::uint8_t>(random());
            PacketHeader header;
            std::string error;
            if (!ProtocolCodec::DecodeHeader(bytes, header, error))
                continue;
            ++accepted;
            EXPECT_LE(header.payloadBytes, MaximumPayloadBytes);
            EXPECT_EQ(header.flags, 0U);
        }
        EXPECT_GT(accepted, 0U) << "the corpus must reach headers that decode";
    }

    TEST(InspectorProtocolTests, RejectsTruncationTrailingBytesAndInvalidBounds)
    {
        auto packet = ProtocolCodec::Encode(EventsRequest{5, 10}, 1);
        packet.payload.pop_back();
        packet.header.payloadBytes--;
        EventsRequest decoded;
        std::string error;
        EXPECT_FALSE(ProtocolCodec::Decode(packet, decoded, error));

        packet = ProtocolCodec::Encode(EventsRequest{5, 10}, 1);
        packet.payload.push_back(0);
        packet.header.payloadBytes++;
        error.clear();
        EXPECT_FALSE(ProtocolCodec::Decode(packet, decoded, error));

        packet = ProtocolCodec::Encode(EventsRequest{5,
            static_cast<std::uint32_t>(MaximumEventsPerResponse + 1)}, 1);
        error.clear();
        EXPECT_FALSE(ProtocolCodec::Decode(packet, decoded, error));

        SnapshotResponse snapshot;
        auto snapshotPacket = ProtocolCodec::Encode(snapshot, 2);
        snapshotPacket.payload[0] = 8;
        SnapshotResponse decodedSnapshot;
        error.clear();
        EXPECT_FALSE(ProtocolCodec::Decode(snapshotPacket, decodedSnapshot, error));

        PreviewResponse pending;
        pending.status = PreviewStatus::Pending;
        pending.ticket = 1;
        pending.width = 1;
        pending.height = 1;
        pending.mimeType = "image/png";
        pending.encodedImage = {1};
        const auto pendingPacket = ProtocolCodec::Encode(pending, 3);
        PreviewResponse decodedPreview;
        error.clear();
        EXPECT_FALSE(ProtocolCodec::Decode(pendingPacket, decodedPreview, error));
    }

    TEST(InspectorFrontendTests, AssetsAreOfflineBoundedAndDemandDriven)
    {
        const auto html = Detail::InspectorIndexHtml();
        const auto script = Detail::InspectorAppJavaScript();
        EXPECT_NE(html.find("__CNA_UI_TOKEN__"), std::string_view::npos);
        EXPECT_EQ(html.find("https://"), std::string_view::npos);
        EXPECT_EQ(html.find("http://"), std::string_view::npos);
        EXPECT_NE(script.find("const eventRetention=1000"), std::string_view::npos);
        EXPECT_NE(script.find("state.events.length>eventRetention"), std::string_view::npos);
        // The event views follow the live tail instead of replaying history they would discard,
        // and loss is reported relative to when the view connected.
        EXPECT_NE(script.find("newest-cursor>retain"), std::string_view::npos);
        EXPECT_NE(script.find("state.lossBaseline=lossCounters("), std::string_view::npos);
        EXPECT_EQ(script.find("eventHistoryOverwrites"), std::string_view::npos)
            << "cumulative ring overwrites are normal operation, not a loss for this view";
        EXPECT_EQ(script.find("?.value??'0'"), std::string_view::npos)
            << "an unpublished metric must not be shown as a measured zero";
        EXPECT_NE(html.find("rel=\"icon\""), std::string_view::npos);
        EXPECT_NE(script.find("refresh-resources"), std::string_view::npos);
        EXPECT_NE(script.find("method:'POST'"), std::string_view::npos);
        EXPECT_NE(script.find("X-CNA-Inspector-UI-Token"), std::string_view::npos);
        EXPECT_EQ(script.find("WebSocket"), std::string_view::npos);
        EXPECT_EQ(script.find("cdn"), std::string_view::npos);
        EXPECT_EQ(script.find(".style."), std::string_view::npos);
        EXPECT_NE(script.find("<progress"), std::string_view::npos);
        EXPECT_NE(script.find("batch.events.at(-1).sequence"), std::string_view::npos);
        EXPECT_NE(script.find("state.cursor='0'"), std::string_view::npos);
    }

    TEST(InspectorAgentTests, EnabledWithoutClientDoesNotPollProvider)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        std::this_thread::sleep_for(75ms);
        EXPECT_EQ(provider.snapshotCalls.load(), 0);
        EXPECT_EQ(provider.eventCalls.load(), 0);
    }

    TEST(InspectorAgentTests, GeneratesA256BitAuthenticationTokenWhenOmitted)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto configuration = MakeAgentConfiguration();
        configuration.authenticationToken.clear();
        auto agent = StartAgent(provider, configuration);
        ASSERT_NE(agent, nullptr);
        const auto& token = agent->GetAuthenticationToken();
        EXPECT_EQ(token.size(), 64U);
        EXPECT_TRUE(std::all_of(token.begin(), token.end(), [](unsigned char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        }));
    }

    TEST(InspectorAgentTests, NegotiatesProviderVersionAndCapabilities)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        const auto& session = client.GetSession();
        EXPECT_EQ(session.providerInterfaceVersion, 1U);
        EXPECT_TRUE(HasCapability(session.capabilities, Capability::Snapshots));
        EXPECT_TRUE(HasCapability(session.capabilities, Capability::Events));
        EXPECT_TRUE(HasCapability(session.capabilities, Capability::ResourceMetadata));
        EXPECT_TRUE(HasCapability(session.capabilities, Capability::Accuracy));
        EXPECT_TRUE(HasCapability(session.capabilities, Capability::Discontinuities));
        EXPECT_FALSE(HasCapability(session.capabilities, Capability::ResourcePreviews));
    }

    TEST(InspectorAgentTests, EnforcesTheNegotiatedResourceCapability)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        std::string error;
        const auto socket = Detail::ConnectTcp(agent->GetBindAddress(), agent->GetPort(), 2000, error);
        ASSERT_NE(socket, Detail::InvalidSocket) << error;

        HelloRequest hello;
        hello.requestedCapabilities = static_cast<std::uint64_t>(Capability::Snapshots);
        hello.authenticationToken = agent->GetAuthenticationToken();
        ASSERT_TRUE(Detail::SendPacket(socket, ProtocolCodec::Encode(hello, 1), 2000, error)) << error;
        Packet response;
        ASSERT_TRUE(Detail::ReceivePacket(socket, response, 2000, error)) << error;
        HelloResponse session;
        ASSERT_TRUE(ProtocolCodec::Decode(response, session, error)) << error;
        EXPECT_FALSE(HasCapability(session.capabilities, Capability::ResourceMetadata));

        SnapshotRequest request;
        request.parts = static_cast<std::uint32_t>(SnapshotPart::Resources);
        ASSERT_TRUE(Detail::SendPacket(socket, ProtocolCodec::Encode(request, 2), 2000, error)) << error;
        ASSERT_TRUE(Detail::ReceivePacket(socket, response, 2000, error)) << error;
        ErrorResponse unavailable;
        ASSERT_TRUE(ProtocolCodec::Decode(response, unavailable, error)) << error;
        EXPECT_EQ(unavailable.code, ErrorCode::Unavailable);
        Detail::ShutdownSocket(socket);
        Detail::CloseSocket(socket);
    }

    TEST(InspectorAgentTests, InvalidIdentityStringsFailBeforeProviderAccess)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto invalidAgent = MakeAgentConfiguration();
        invalidAgent.applicationName.assign(4097, 'x');
        std::string error;
        EXPECT_EQ(Agent::Start(provider, invalidAgent, error), nullptr);

        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        auto invalidClient = MakeClientConfiguration(*agent);
        invalidClient.clientName.assign(129, 'x');
        Client client;
        error.clear();
        EXPECT_FALSE(client.Connect(invalidClient, error));
        EXPECT_FALSE(client.IsConnected());
        EXPECT_EQ(provider.snapshotCalls.load(), 0);
    }

    TEST(InspectorAgentTests, RejectsUnsupportedProtocolVersion)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        HelloRequest hello;
        hello.minimumMajorVersion = 2;
        hello.maximumMajorVersion = 2;
        hello.authenticationToken = agent->GetAuthenticationToken();
        const auto response = RawRoundTrip(*agent, ProtocolCodec::Encode(hello, 88));
        ErrorResponse decoded;
        std::string error;
        ASSERT_TRUE(ProtocolCodec::Decode(response, decoded, error)) << error;
        EXPECT_EQ(decoded.code, ErrorCode::UnsupportedVersion);
    }

    TEST(InspectorAgentTests, RejectsBadAuthenticationWithoutProviderAccess)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        auto configuration = MakeClientConfiguration(*agent);
        configuration.authenticationToken = "wrong-token";
        Client client;
        std::string error;
        EXPECT_FALSE(client.Connect(configuration, error));
        EXPECT_EQ(provider.snapshotCalls.load(), 0);
    }

    TEST(InspectorAgentTests, SnapshotAndEventRequestsUseProviderVersionOne)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        provider.events.oldestAvailableSequence = 11;
        provider.events.newestAvailableSequence = 20;
        provider.events.eventsDroppedBeforeStart = 2;
        provider.events.producerEventsDropped = 5;
        provider.events.events.push_back({20, 100, 30, 99, 3, 4, 0, 0, 7,
                                          Diagnostics::EventKind::Zone,
                                          Diagnostics::Category::Update});
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;

        SnapshotRequest snapshotRequest;
        SnapshotResponse snapshot;
        ASSERT_TRUE(client.CaptureSnapshot(snapshotRequest, snapshot, error)) << error;
        EXPECT_EQ(snapshot.resourceCount, 1U);
        EXPECT_TRUE(snapshot.snapshot.resources.empty());
        EXPECT_EQ(snapshot.snapshot.metrics[0].accuracy, Diagnostics::Accuracy::Exact);

        EventsResponse events;
        ASSERT_TRUE(client.ReadEvents({10, 64}, events, error)) << error;
        EXPECT_EQ(provider.lastAfterSequence, 10U);
        EXPECT_EQ(provider.lastMaximumEvents, 64U);
        EXPECT_EQ(events.eventsDroppedBeforeStart, 2U);
        EXPECT_EQ(events.producerEventsDropped, 5U);
        EXPECT_EQ(events.events[0].name, "Game/Update");
    }

    TEST(InspectorAgentTests, ReconnectsAfterAClientDisconnects)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        std::string error;
        {
            Client first;
            ASSERT_TRUE(first.Connect(MakeClientConfiguration(*agent), error)) << error;
            EXPECT_TRUE(first.Ping(error)) << error;
        }
        Client second;
        ASSERT_TRUE(second.Connect(MakeClientConfiguration(*agent), error)) << error;
        EXPECT_TRUE(second.Ping(error)) << error;
        EXPECT_EQ(provider.snapshotCalls.load(), 2);
    }

    TEST(InspectorAgentTests, MalformedClientDoesNotPreventReconnect)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        std::string error;
        auto socket = Detail::ConnectTcp(agent->GetBindAddress(), agent->GetPort(), 2000, error);
        ASSERT_NE(socket, Detail::InvalidSocket) << error;
        std::array<std::uint8_t, 24> malformed{};
        EXPECT_TRUE(Detail::SendAll(socket, malformed, 2000, error)) << error;
        Detail::ShutdownSocket(socket);
        Detail::CloseSocket(socket);

        Client client;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        EXPECT_TRUE(client.Ping(error)) << error;
    }

    TEST(InspectorAgentTests, RequestRateBackpressureIsBounded)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto configuration = MakeAgentConfiguration();
        configuration.maximumRequestsPerSecond = 2;
        auto agent = StartAgent(provider, configuration);
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        EXPECT_TRUE(client.Ping(error)) << error;
        EXPECT_TRUE(client.Ping(error)) << error;
        EXPECT_FALSE(client.Ping(error));
        EXPECT_TRUE(client.IsConnected());
    }

    TEST(InspectorAgentTests, ResourceLifetimeChangesAreObservedOnlyOnRequest)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        SnapshotRequest request;
        request.parts = static_cast<std::uint32_t>(SnapshotPart::Resources);
        SnapshotResponse response;
        ASSERT_TRUE(client.CaptureSnapshot(request, response, error)) << error;
        ASSERT_EQ(response.snapshot.resources.size(), 1U);
        provider.snapshot.resources.clear();
        ASSERT_TRUE(client.CaptureSnapshot(request, response, error)) << error;
        EXPECT_TRUE(response.snapshot.resources.empty());
        EXPECT_EQ(response.resourceCount, 0U);
    }

    TEST(InspectorAgentTests, OptionalPreviewUnavailableIsANormalResponse)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        PreviewResponse response;
        EXPECT_TRUE(client.RequestPreview({17, 64, 64, 4096}, response, error)) << error;
        EXPECT_EQ(response.status, PreviewStatus::Unavailable);
        EXPECT_TRUE(client.Ping(error)) << error;
    }

    TEST(InspectorAgentTests, PreviewIsExplicitAsynchronousAndBounded)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto previews = std::make_shared<FakePreviewProvider>();
        auto configuration = MakeAgentConfiguration();
        configuration.previewProvider = previews;
        auto agent = StartAgent(provider, configuration);
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        EXPECT_TRUE(HasCapability(client.GetSession().capabilities, Capability::ResourcePreviews));
        PreviewResponse response;
        ASSERT_TRUE(client.RequestPreview({17, 64, 64, 4096}, response, error)) << error;
        EXPECT_EQ(response.status, PreviewStatus::Pending);
        EXPECT_EQ(response.ticket, 42U);
        ASSERT_TRUE(client.PollPreview({response.ticket}, response, error)) << error;
        EXPECT_EQ(response.status, PreviewStatus::Ready);
        EXPECT_LE(response.encodedImage.size(), 4096U);
        EXPECT_EQ(previews->requestCalls, 1);
        EXPECT_EQ(previews->pollCalls, 1);
        EXPECT_EQ(previews->lastResourceId, 17U);
    }

    TEST(InspectorAgentTests, InvalidPreviewSourceCannotBreakTheConnection)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto previews = std::make_shared<FakePreviewProvider>();
        previews->returnInvalidImage = true;
        auto configuration = MakeAgentConfiguration();
        configuration.previewProvider = previews;
        auto agent = StartAgent(provider, configuration);
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        PreviewResponse response;
        EXPECT_FALSE(client.RequestPreview({17, 64, 64, 4096}, response, error));
        EXPECT_TRUE(client.IsConnected());
        EXPECT_TRUE(client.Ping(error)) << error;
    }

    TEST(InspectorAgentTests, PreviewPollHonorsTheOriginalRequestBounds)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto previews = std::make_shared<FakePreviewProvider>();
        previews->pollWidth = 2;
        auto configuration = MakeAgentConfiguration();
        configuration.previewProvider = previews;
        auto agent = StartAgent(provider, configuration);
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        PreviewResponse response;
        ASSERT_TRUE(client.RequestPreview({17, 1, 1, 4096}, response, error)) << error;
        EXPECT_FALSE(client.PollPreview({response.ticket}, response, error));
        EXPECT_TRUE(client.IsConnected());
        EXPECT_TRUE(client.Ping(error)) << error;
    }

    TEST(InspectorAgentTests, PendingPreviewCountIsBounded)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto previews = std::make_shared<FakePreviewProvider>();
        auto configuration = MakeAgentConfiguration();
        configuration.previewProvider = previews;
        configuration.maximumPendingPreviews = 1;
        auto agent = StartAgent(provider, configuration);
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        PreviewResponse response;
        ASSERT_TRUE(client.RequestPreview({17, 64, 64, 4096}, response, error)) << error;
        std::this_thread::sleep_for(55ms);
        EXPECT_FALSE(client.RequestPreview({17, 64, 64, 4096}, response, error));
        EXPECT_EQ(previews->requestCalls, 1);
        EXPECT_TRUE(client.IsConnected());
        EXPECT_TRUE(client.Ping(error)) << error;
    }

#if !defined(_WIN32)
    // A POSIX fd_set is a bitmap indexed by descriptor number, so waiting on a socket through
    // one writes out of bounds as soon as a descriptor reaches FD_SETSIZE. The agent runs inside
    // the host game, which can easily hold that many descriptors, so it must keep working there.
    class DescriptorBallast
    {
    public:
        ~DescriptorBallast()
        {
            for (const int descriptor : held_) ::close(descriptor);
        }

        void FillTo(std::size_t count)
        {
            while (held_.size() < count)
            {
                const int descriptor = ::open("/dev/null", O_RDONLY);
                if (descriptor < 0) return;
                held_.push_back(descriptor);
            }
        }

        [[nodiscard]] int Highest() const { return held_.empty() ? -1 : held_.back(); }

    private:
        std::vector<int> held_;
    };

    TEST(InspectorAgentTests, ServesClientsWhenSocketsExceedTheDescriptorSetLimit)
    {
        DescriptorBallast ballast;
        ballast.FillTo(static_cast<std::size_t>(FD_SETSIZE) + 64U);
        if (ballast.Highest() < FD_SETSIZE)
            GTEST_SKIP() << "the descriptor limit here does not reach FD_SETSIZE";

        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        Client client;
        std::string error;
        ASSERT_TRUE(client.Connect(MakeClientConfiguration(*agent), error)) << error;
        SnapshotResponse response;
        EXPECT_TRUE(client.CaptureSnapshot(SnapshotRequest{}, response, error)) << error;
        EXPECT_GT(provider.snapshotCalls, 0);
    }
#endif

    TEST(InspectorAgentTests, NonLoopbackBindingRequiresExplicitAuthorization)
    {
        FakeProvider provider;
        auto configuration = MakeAgentConfiguration();
        configuration.bindAddress = "0.0.0.0";
        std::string error;
        EXPECT_EQ(Agent::Start(provider, configuration, error), nullptr);
        EXPECT_NE(error.find("allowRemote"), std::string::npos);
    }

    // ── Hand-built frames against docs/inspector.md, "Protocol version 1" ────────────────────
    //
    // The header layout, the twelve message types, flags = 0 and the 8 MiB bound are the
    // documented wire contract. "An incompatible wire format needs a new major version", so the
    // version-1 payload layout (little-endian integers in declaration order; strings and byte
    // fields as a u32 byte count followed by the bytes) is pinned byte for byte as well.

    namespace
    {
        void PutLittleEndian(std::vector<std::uint8_t>& bytes, std::uint64_t value, std::size_t width)
        {
            for (std::size_t i = 0; i < width; ++i)
                bytes.push_back(static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU));
        }

        std::vector<std::uint8_t> HandBuiltHeader(std::uint16_t major, std::uint16_t minor,
                                                  std::uint16_t type, std::uint16_t flags,
                                                  std::uint32_t payloadBytes, std::uint64_t requestId)
        {
            std::vector<std::uint8_t> bytes{'C', 'N', 'A', 'I'};
            PutLittleEndian(bytes, major, 2);
            PutLittleEndian(bytes, minor, 2);
            PutLittleEndian(bytes, type, 2);
            PutLittleEndian(bytes, flags, 2);
            PutLittleEndian(bytes, payloadBytes, 4);
            PutLittleEndian(bytes, requestId, 8);
            return bytes;
        }

        void SetPayload(Packet& packet, std::vector<std::uint8_t> payload)
        {
            packet.payload = std::move(payload);
            packet.header.payloadBytes = static_cast<std::uint32_t>(packet.payload.size());
        }

        HelloResponse ValidServerHello()
        {
            HelloResponse hello;
            hello.providerInterfaceVersion = Diagnostics::IDiagnosticsProvider::InterfaceVersion;
            hello.maximumPayloadBytes = static_cast<std::uint32_t>(MaximumPayloadBytes);
            hello.maximumEventsPerResponse = static_cast<std::uint32_t>(MaximumEventsPerResponse);
            hello.maximumPreviewBytes = static_cast<std::uint32_t>(MaximumPreviewBytes);
            return hello;
        }
    }

    TEST(InspectorProtocolTests, EncodedHeaderIsTheDocumentedLittleEndianLayout)
    {
        PacketHeader header;
        header.type = MessageType::EventsResponse;
        header.payloadBytes = 0x00010203U;
        header.requestId = 0x1122334455667788ULL;
        const std::vector<std::uint8_t> expected{
            'C', 'N', 'A', 'I',             // magic
            0x01, 0x00, 0x00, 0x00,         // major 1, minor 0
            0x0D, 0x00, 0x00, 0x00,         // EventsResponse = 13, flags 0
            0x03, 0x02, 0x01, 0x00,         // payload byte count
            0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        EXPECT_EQ(ProtocolCodec::EncodeHeader(header), expected);

        PacketHeader decoded;
        std::string error;
        ASSERT_TRUE(ProtocolCodec::DecodeHeader(
            HandBuiltHeader(1, 0, 17, 0, 0x000ABCDEU, 0xA1B2C3D4E5F60718ULL), decoded, error)) << error;
        EXPECT_EQ(decoded.majorVersion, 1U);
        EXPECT_EQ(decoded.minorVersion, 0U);
        EXPECT_EQ(decoded.type, MessageType::Ping);
        EXPECT_EQ(decoded.flags, 0U);
        EXPECT_EQ(decoded.payloadBytes, 0x000ABCDEU);
        EXPECT_EQ(decoded.requestId, 0xA1B2C3D4E5F60718ULL);
    }

    TEST(InspectorProtocolTests, HeaderAcceptsExactlyTheDocumentedTypesFlagsMagicAndLength)
    {
        const std::set<std::uint16_t> documented{1, 2, 3, 10, 11, 12, 13, 14, 15, 16, 17, 18};
        PacketHeader decoded;
        std::string error;
        for (std::uint32_t type = 0; type <= 24; ++type)
        {
            error.clear();
            EXPECT_EQ(ProtocolCodec::DecodeHeader(
                          HandBuiltHeader(1, 0, static_cast<std::uint16_t>(type), 0, 0, 1), decoded, error),
                      documented.count(static_cast<std::uint16_t>(type)) == 1)
                << "message type " << type;
        }
        EXPECT_FALSE(ProtocolCodec::DecodeHeader(HandBuiltHeader(1, 0, 0xFFFF, 0, 0, 1), decoded, error));

        EXPECT_FALSE(ProtocolCodec::DecodeHeader(HandBuiltHeader(1, 0, 17, 1, 0, 1), decoded, error));
        EXPECT_FALSE(ProtocolCodec::DecodeHeader(HandBuiltHeader(1, 0, 17, 0x8000, 0, 1), decoded, error));

        const auto limit = static_cast<std::uint32_t>(MaximumPayloadBytes);
        EXPECT_TRUE(ProtocolCodec::DecodeHeader(HandBuiltHeader(1, 0, 13, 0, limit, 1), decoded, error)) << error;
        EXPECT_EQ(decoded.payloadBytes, limit);
        EXPECT_FALSE(ProtocolCodec::DecodeHeader(HandBuiltHeader(1, 0, 13, 0, limit + 1, 1), decoded, error));
        EXPECT_FALSE(ProtocolCodec::DecodeHeader(HandBuiltHeader(1, 0, 13, 0, 0xFFFFFFFFU, 1), decoded, error));

        for (const auto& magic : {std::array<std::uint8_t, 4>{'C', 'N', 'A', 'J'},
                                  std::array<std::uint8_t, 4>{'c', 'n', 'a', 'i'},
                                  std::array<std::uint8_t, 4>{'I', 'A', 'N', 'C'}})
        {
            auto bytes = HandBuiltHeader(1, 0, 17, 0, 0, 1);
            std::copy(magic.begin(), magic.end(), bytes.begin());
            EXPECT_FALSE(ProtocolCodec::DecodeHeader(bytes, decoded, error));
        }

        auto bytes = HandBuiltHeader(1, 0, 17, 0, 0, 1);
        EXPECT_FALSE(ProtocolCodec::DecodeHeader(std::span(bytes).first(23), decoded, error));
        bytes.push_back(0);
        EXPECT_FALSE(ProtocolCodec::DecodeHeader(bytes, decoded, error));
        EXPECT_FALSE(ProtocolCodec::DecodeHeader({}, decoded, error));
    }

    TEST(InspectorProtocolTests, VersionOnePayloadLayoutsArePinned)
    {
        HelloRequest hello;
        hello.minimumMajorVersion = 1;
        hello.maximumMajorVersion = 3;
        hello.requestedCapabilities = 0x7FU;
        hello.clientName = "ab";
        hello.authenticationToken = "xyz";
        const auto helloPacket = ProtocolCodec::Encode(hello, 5);
        EXPECT_EQ(helloPacket.header.type, MessageType::ClientHello);
        EXPECT_EQ(helloPacket.header.requestId, 5U);
        EXPECT_EQ(helloPacket.header.payloadBytes, helloPacket.payload.size());
        const std::vector<std::uint8_t> helloBytes{
            0x01, 0x00, 0x03, 0x00,                                  // version range 1..3
            0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          // capability mask
            0x02, 0x00, 0x00, 0x00, 'a', 'b',                        // client name
            0x03, 0x00, 0x00, 0x00, 'x', 'y', 'z'};                  // token
        EXPECT_EQ(helloPacket.payload, helloBytes);

        ErrorResponse failure;
        failure.code = ErrorCode::Unauthorized;
        failure.message = "no";
        const auto errorPacket = ProtocolCodec::Encode(failure, 6);
        EXPECT_EQ(errorPacket.header.type, MessageType::Error);
        EXPECT_EQ(errorPacket.payload, (std::vector<std::uint8_t>{0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 'n', 'o'}));

        EXPECT_EQ(ProtocolCodec::Encode(EventsRequest{0x0102030405060708ULL, 300}, 7).payload,
                  (std::vector<std::uint8_t>{0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
                                             0x2C, 0x01, 0x00, 0x00}));
        EXPECT_EQ(ProtocolCodec::Encode(PreviewPollRequest{0x42}, 8).payload,
                  (std::vector<std::uint8_t>{0x42, 0, 0, 0, 0, 0, 0, 0}));

        // And the decoders read hand-built payloads with the same layout.
        Packet handHello;
        handHello.header.type = MessageType::ClientHello;
        SetPayload(handHello, {0x00, 0x00, 0x02, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0x80,
                               0x01, 0x00, 0x00, 0x00, 'q', 0x00, 0x00, 0x00, 0x00});
        HelloRequest decodedHello;
        std::string error;
        ASSERT_TRUE(ProtocolCodec::Decode(handHello, decodedHello, error)) << error;
        EXPECT_EQ(decodedHello.minimumMajorVersion, 0U);
        EXPECT_EQ(decodedHello.maximumMajorVersion, 2U);
        EXPECT_EQ(decodedHello.requestedCapabilities, 0x8000000000000001ULL);
        EXPECT_EQ(decodedHello.clientName, "q");
        EXPECT_EQ(decodedHello.authenticationToken, "");
    }

    TEST(InspectorProtocolTests, DecodersRefuseAnotherMessageTypeAndAHeaderThatMisstatesThePayload)
    {
        auto packet = ProtocolCodec::Encode(EventsRequest{5, 10}, 1);
        std::string error;
        SnapshotRequest snapshot;
        EXPECT_FALSE(ProtocolCodec::Decode(packet, snapshot, error));
        EXPECT_FALSE(error.empty());
        PreviewPollRequest poll;
        error.clear();
        EXPECT_FALSE(ProtocolCodec::Decode(packet, poll, error));

        EventsRequest events;
        packet.header.payloadBytes += 1;
        error.clear();
        EXPECT_FALSE(ProtocolCodec::Decode(packet, events, error));
        packet.header.payloadBytes -= 2;
        error.clear();
        EXPECT_FALSE(ProtocolCodec::Decode(packet, events, error));
        packet.header.payloadBytes += 1;
        error.clear();
        EXPECT_TRUE(ProtocolCodec::Decode(packet, events, error)) << error;
        EXPECT_EQ(events.afterSequence, 5U);
        EXPECT_EQ(events.maximumEvents, 10U);
    }

    TEST(InspectorProtocolTests, StringsMustBeWellFormedUtf8)
    {
        // RFC 3629 section 4 (UTF8-octets): no overlong form, no surrogate, nothing above
        // U+10FFFF, no stray continuation byte, no truncated sequence -- including a sequence cut
        // off by the end of its own length-prefixed field. The token is 0xA9 bytes long, so the
        // byte right after the name field is 0xA9: a continuation byte a validator that looked
        // past the field would wrongly accept.
        HelloRequest hello;
        hello.clientName = "@@@@";
        hello.authenticationToken.assign(0xA9, 't');
        const auto seed = ProtocolCodec::Encode(hello, 1);
        const std::string_view marker = "@@@@";
        const auto at = std::search(seed.payload.begin(), seed.payload.end(), marker.begin(), marker.end());
        ASSERT_NE(at, seed.payload.end());
        const auto offset = static_cast<std::size_t>(at - seed.payload.begin());
        ASSERT_EQ(seed.payload[offset + 4], 0xA9U);

        struct Case
        {
            std::array<std::uint8_t, 4> bytes;
            bool valid;
            const char* what;
        };
        const Case cases[] = {
            {{'a', 'a', 'a', 'a'}, true, "ASCII"},
            {{0x7F, 'a', 'a', 'a'}, true, "U+007F"},
            {{'a', 'a', 0xC3, 0xA9}, true, "U+00E9 ending exactly at the field end"},
            {{0xE2, 0x82, 0xAC, 'a'}, true, "U+20AC"},
            {{0xED, 0x9F, 0xBF, 'a'}, true, "U+D7FF, last before the surrogates"},
            {{0xEE, 0x80, 0x80, 'a'}, true, "U+E000, first after the surrogates"},
            {{0xEF, 0xBF, 0xBF, 'a'}, true, "U+FFFF"},
            {{0xF0, 0x9F, 0x98, 0x80}, true, "U+1F600"},
            {{0xF4, 0x8F, 0xBF, 0xBF}, true, "U+10FFFF"},
            {{0xC0, 0x80, 'a', 'a'}, false, "overlong U+0000"},
            {{0xC1, 0xBF, 'a', 'a'}, false, "overlong U+007F"},
            {{0xE0, 0x80, 0x80, 'a'}, false, "overlong three-byte U+0000"},
            {{0xE0, 0x9F, 0xBF, 'a'}, false, "overlong three-byte U+07FF"},
            {{0xF0, 0x8F, 0xBF, 0xBF}, false, "overlong four-byte U+FFFF"},
            {{0xED, 0xA0, 0x80, 'a'}, false, "surrogate U+D800"},
            {{0xED, 0xBF, 0xBF, 'a'}, false, "surrogate U+DFFF"},
            {{0xF4, 0x90, 0x80, 0x80}, false, "U+110000"},
            {{0xF5, 0x80, 0x80, 0x80}, false, "lead byte F5"},
            {{0xF8, 0x88, 0x80, 0x80}, false, "five-byte lead"},
            {{0xFF, 'a', 'a', 'a'}, false, "byte FF"},
            {{0x80, 'a', 'a', 'a'}, false, "lone continuation byte"},
            {{0xE2, 0x82, 'a', 'a'}, false, "three-byte sequence missing its last byte"},
            {{0xC3, 0xC3, 'a', 'a'}, false, "lead byte where a continuation is required"},
            {{'a', 'a', 'a', 0xC3}, false, "sequence truncated by the end of its field"},
        };
        for (const auto& testCase : cases)
        {
            SCOPED_TRACE(testCase.what);
            Packet packet = seed;
            std::copy(testCase.bytes.begin(), testCase.bytes.end(),
                      packet.payload.begin() + static_cast<std::ptrdiff_t>(offset));
            HelloRequest decoded;
            std::string error;
            EXPECT_EQ(ProtocolCodec::Decode(packet, decoded, error), testCase.valid) << error;
            const std::string text(testCase.bytes.begin(), testCase.bytes.end());
            if (testCase.valid) EXPECT_EQ(decoded.clientName, text);

            HelloRequest outgoing;
            outgoing.clientName = text;
            if (testCase.valid)
                EXPECT_NO_THROW((void)ProtocolCodec::Encode(outgoing, 1));
            else
                EXPECT_THROW((void)ProtocolCodec::Encode(outgoing, 1), std::invalid_argument);
        }
    }

    TEST(InspectorProtocolTests, ClientNameAndTokenBoundsHoldOnBothSides)
    {
        HelloRequest hello;
        hello.clientName.assign(128, 'n');
        hello.authenticationToken.assign(256, 't');
        HelloRequest decoded;
        std::string error;
        ASSERT_TRUE(ProtocolCodec::Decode(ProtocolCodec::Encode(hello, 1), decoded, error)) << error;
        EXPECT_EQ(decoded.clientName.size(), 128U);
        EXPECT_EQ(decoded.authenticationToken.size(), 256U);

        hello.clientName.assign(129, 'n');
        EXPECT_THROW((void)ProtocolCodec::Encode(hello, 1), std::length_error);
        hello.clientName.assign(128, 'n');
        hello.authenticationToken.assign(257, 't');
        EXPECT_THROW((void)ProtocolCodec::Encode(hello, 1), std::length_error);

        // The name's u32 byte count sits after u16 + u16 + u64 = 12 bytes. Lengthening the field
        // by hand to 129 bytes must be refused by the decoder too.
        hello.authenticationToken = "t";
        Packet packet = ProtocolCodec::Encode(hello, 1);
        ASSERT_EQ(packet.payload[12], 128U);
        packet.payload[12] = 129;
        packet.payload.insert(packet.payload.begin() + 16, 'n');
        packet.header.payloadBytes += 1;
        error.clear();
        EXPECT_FALSE(ProtocolCodec::Decode(packet, decoded, error));

        // A byte count running past the end of the payload is a truncated field.
        packet = ProtocolCodec::Encode(hello, 1);
        packet.payload[12] = 0xFF;
        packet.payload[13] = 0xFF;
        packet.payload[14] = 0xFF;
        packet.payload[15] = 0xFF;
        error.clear();
        EXPECT_FALSE(ProtocolCodec::Decode(packet, decoded, error));
    }

    TEST(InspectorProtocolTests, BooleansAndEnumerationsRejectUndeclaredValues)
    {
        // ServerHello: u16 major, u16 minor, u16 provider version, u64 capabilities, three u32
        // limits, then the Boolean localOnly at byte 2 + 2 + 2 + 8 + 4 + 4 + 4 = 26.
        Packet packet = ProtocolCodec::Encode(ValidServerHello(), 1);
        ASSERT_EQ(packet.payload[26], 1U);
        HelloResponse session;
        std::string error;
        for (const std::uint8_t value : {0, 1})
        {
            packet.payload[26] = value;
            error.clear();
            EXPECT_TRUE(ProtocolCodec::Decode(packet, session, error)) << error;
            EXPECT_EQ(session.localOnly, value == 1);
        }
        for (const std::uint8_t value : {2, 0x80, 0xFF})
        {
            packet.payload[26] = value;
            error.clear();
            EXPECT_FALSE(ProtocolCodec::Decode(packet, session, error)) << "localOnly byte " << int{value};
        }

        ErrorResponse failure;
        failure.code = ErrorCode::InternalError;
        packet = ProtocolCodec::Encode(failure, 2);
        ASSERT_EQ(packet.payload[0], 6U);
        ErrorResponse decodedError;
        error.clear();
        EXPECT_TRUE(ProtocolCodec::Decode(packet, decodedError, error)) << error;
        packet.payload[0] = 7;
        EXPECT_FALSE(ProtocolCodec::Decode(packet, decodedError, error));
        packet.payload[0] = 0xFF;
        packet.payload[1] = 0xFF;
        EXPECT_FALSE(ProtocolCodec::Decode(packet, decodedError, error));

        PreviewResponse failed;
        failed.status = PreviewStatus::Failed;
        packet = ProtocolCodec::Encode(failed, 3);
        ASSERT_EQ(packet.payload[0], 3U);
        PreviewResponse decodedPreview;
        error.clear();
        EXPECT_TRUE(ProtocolCodec::Decode(packet, decodedPreview, error)) << error;
        packet.payload[0] = 4;
        EXPECT_FALSE(ProtocolCodec::Decode(packet, decodedPreview, error));
    }

    TEST(InspectorProtocolTests, ServerHelloLimitsMustBeNonZeroAndWithinTheProtocolBounds)
    {
        const auto decodes = [](const HelloResponse& hello) {
            HelloResponse decoded;
            std::string error;
            return ProtocolCodec::Decode(ProtocolCodec::Encode(hello, 1), decoded, error);
        };
        EXPECT_TRUE(decodes(ValidServerHello()));
        EXPECT_FALSE(decodes(HelloResponse{})) << "a default ServerHello states zero limits";

        auto hello = ValidServerHello();
        hello.maximumPayloadBytes = 0;
        EXPECT_FALSE(decodes(hello));
        hello.maximumPayloadBytes = static_cast<std::uint32_t>(MaximumPayloadBytes) + 1;
        EXPECT_FALSE(decodes(hello));

        hello = ValidServerHello();
        hello.maximumEventsPerResponse = 0;
        EXPECT_FALSE(decodes(hello));
        hello.maximumEventsPerResponse = static_cast<std::uint32_t>(MaximumEventsPerResponse) + 1;
        EXPECT_FALSE(decodes(hello));

        hello = ValidServerHello();
        hello.maximumPreviewBytes = 0;
        EXPECT_FALSE(decodes(hello));
        hello.maximumPreviewBytes = static_cast<std::uint32_t>(MaximumPreviewBytes) + 1;
        EXPECT_FALSE(decodes(hello));

        hello = ValidServerHello();
        hello.maximumPayloadBytes = 1;
        hello.maximumEventsPerResponse = 1;
        hello.maximumPreviewBytes = 1;
        EXPECT_TRUE(decodes(hello));
    }

    // ── The agent's handshake and request/response pairing, over a raw socket ───────────────

    namespace
    {
        class RawConnection
        {
        public:
            explicit RawConnection(const Agent& agent)
            {
                std::string error;
                socket_ = Detail::ConnectTcp(agent.GetBindAddress(), agent.GetPort(), 2000, error);
                EXPECT_NE(socket_, Detail::InvalidSocket) << error;
            }

            ~RawConnection()
            {
                if (socket_ == Detail::InvalidSocket) return;
                Detail::ShutdownSocket(socket_);
                Detail::CloseSocket(socket_);
            }

            RawConnection(const RawConnection&) = delete;
            RawConnection& operator=(const RawConnection&) = delete;

            [[nodiscard]] bool Send(const Packet& packet)
            {
                std::string error;
                const bool sent = Detail::SendPacket(socket_, packet, 2000, error);
                EXPECT_TRUE(sent) << error;
                return sent;
            }

            [[nodiscard]] bool Receive(Packet& packet, std::uint32_t timeoutMilliseconds = 2000)
            {
                std::string error;
                return Detail::ReceivePacket(socket_, packet, timeoutMilliseconds, error);
            }

            /// Sends @p request and returns the response, which must carry the same request id.
            Packet Exchange(const Packet& request)
            {
                Packet response;
                if (!Send(request)) return response;
                EXPECT_TRUE(Receive(response)) << "no response to request " << request.header.requestId;
                EXPECT_EQ(response.header.requestId, request.header.requestId)
                    << "the response does not answer request " << request.header.requestId;
                EXPECT_EQ(response.header.majorVersion, ProtocolMajorVersion);
                EXPECT_EQ(response.header.minorVersion, ProtocolMinorVersion);
                return response;
            }

        private:
            Detail::SocketHandle socket_ = Detail::InvalidSocket;
        };

        Packet HelloPacket(const Agent& agent, std::uint64_t requestId,
                           std::uint16_t minimum = 1, std::uint16_t maximum = 1)
        {
            HelloRequest hello;
            hello.minimumMajorVersion = minimum;
            hello.maximumMajorVersion = maximum;
            hello.authenticationToken = agent.GetAuthenticationToken();
            return ProtocolCodec::Encode(hello, requestId);
        }

        ErrorCode ExpectError(const Packet& response)
        {
            EXPECT_EQ(response.header.type, MessageType::Error);
            ErrorResponse decoded;
            std::string error;
            EXPECT_TRUE(ProtocolCodec::Decode(response, decoded, error)) << error;
            return decoded.code;
        }

        Packet PingPacket(std::uint64_t requestId)
        {
            Packet ping;
            ping.header.type = MessageType::Ping;
            ping.header.requestId = requestId;
            return ping;
        }
    }

    TEST(InspectorAgentTests, SelectsVersionOneFromAnyRangeThatContainsIt)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);

        {
            RawConnection connection(*agent);
            const Packet response = connection.Exchange(HelloPacket(*agent, 41, 0, 7));
            ASSERT_EQ(response.header.type, MessageType::ServerHello);
            HelloResponse session;
            std::string error;
            ASSERT_TRUE(ProtocolCodec::Decode(response, session, error)) << error;
            // docs/inspector.md: ServerHello selects version 1.0 and states the hard limits.
            EXPECT_EQ(session.selectedMajorVersion, 1U);
            EXPECT_EQ(session.selectedMinorVersion, 0U);
            EXPECT_EQ(session.maximumPayloadBytes, 8U * 1024U * 1024U);
            EXPECT_EQ(session.maximumEventsPerResponse, 4096U);
            EXPECT_EQ(session.maximumPreviewBytes, 4U * 1024U * 1024U);
            EXPECT_TRUE(session.localOnly);
        }

        // The agent serves one client at a time, so the session above is closed first.
        RawConnection below(*agent);
        EXPECT_EQ(ExpectError(below.Exchange(HelloPacket(*agent, 42, 0, 0))), ErrorCode::UnsupportedVersion);
    }

    TEST(InspectorAgentTests, AnInvertedVersionRangeIsAnInvalidHelloAndClosesTheConnection)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        RawConnection connection(*agent);
        EXPECT_EQ(ExpectError(connection.Exchange(HelloPacket(*agent, 9, 3, 1))), ErrorCode::InvalidRequest);
        Packet after;
        EXPECT_FALSE(connection.Receive(after)) << "an unauthenticated failure closes the connection";
        EXPECT_EQ(provider.snapshotCalls.load(), 0);
    }

    TEST(InspectorAgentTests, AnEnvelopeOfAnotherMajorOrANewerMinorVersionIsRefused)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);

        for (const auto& [major, minor] :
             std::initializer_list<std::pair<std::uint16_t, std::uint16_t>>{{2, 0}, {0, 0}, {1, 1}})
        {
            SCOPED_TRACE(testing::Message() << "envelope " << major << "." << minor);
            RawConnection connection(*agent);
            Packet hello = HelloPacket(*agent, 50 + major * 10 + minor);
            hello.header.majorVersion = major;
            hello.header.minorVersion = minor;
            EXPECT_EQ(ExpectError(connection.Exchange(hello)), ErrorCode::UnsupportedVersion);
        }
        EXPECT_EQ(provider.snapshotCalls.load(), 0);

        // After negotiation the envelope may not change either, and the session ends.
        RawConnection connection(*agent);
        ASSERT_EQ(connection.Exchange(HelloPacket(*agent, 1)).header.type, MessageType::ServerHello);
        Packet ping = PingPacket(2);
        ping.header.minorVersion = 1;
        EXPECT_EQ(ExpectError(connection.Exchange(ping)), ErrorCode::UnsupportedVersion);
        Packet after;
        EXPECT_FALSE(connection.Receive(after));
    }

    TEST(InspectorAgentTests, TheFirstMessageMustBeAClientHello)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        RawConnection connection(*agent);
        EXPECT_EQ(ExpectError(connection.Exchange(PingPacket(9))), ErrorCode::InvalidRequest);
        Packet after;
        EXPECT_FALSE(connection.Receive(after));
        EXPECT_EQ(provider.snapshotCalls.load(), 0);
    }

    TEST(InspectorAgentTests, EveryResponseAnswersItsRequestIdAndInvalidRequestsKeepTheSession)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        RawConnection connection(*agent);
        ASSERT_EQ(connection.Exchange(HelloPacket(*agent, 1)).header.type, MessageType::ServerHello);

        const Packet pong = connection.Exchange(PingPacket(0xDEADBEEF00000001ULL));
        EXPECT_EQ(pong.header.type, MessageType::Pong);
        EXPECT_TRUE(pong.payload.empty());
        EXPECT_EQ(connection.Exchange(ProtocolCodec::Encode(SnapshotRequest{}, 77)).header.type,
                  MessageType::SnapshotResponse);
        EXPECT_EQ(connection.Exchange(ProtocolCodec::Encode(EventsRequest{0, 16}, 78)).header.type,
                  MessageType::EventsResponse);

        // Each of these is refused with a structured InvalidRequest that answers its own id; none
        // of them ends the authenticated session.
        EXPECT_EQ(ExpectError(connection.Exchange(HelloPacket(*agent, 79))), ErrorCode::InvalidRequest);
        Packet clientPong;
        clientPong.header.type = MessageType::Pong;
        clientPong.header.requestId = 80;
        EXPECT_EQ(ExpectError(connection.Exchange(clientPong)), ErrorCode::InvalidRequest);
        Packet pingWithPayload = PingPacket(81);
        SetPayload(pingWithPayload, {1});
        EXPECT_EQ(ExpectError(connection.Exchange(pingWithPayload)), ErrorCode::InvalidRequest);
        EXPECT_EQ(ExpectError(connection.Exchange(ProtocolCodec::Encode(SnapshotRequest{8U}, 82))),
                  ErrorCode::InvalidRequest);
        EXPECT_EQ(ExpectError(connection.Exchange(ProtocolCodec::Encode(EventsRequest{0, 0}, 83))),
                  ErrorCode::InvalidRequest);
        Packet truncatedEvents = ProtocolCodec::Encode(EventsRequest{0, 16}, 84);
        truncatedEvents.payload.pop_back();
        truncatedEvents.header.payloadBytes -= 1;
        EXPECT_EQ(ExpectError(connection.Exchange(truncatedEvents)), ErrorCode::InvalidRequest);

        EXPECT_EQ(connection.Exchange(PingPacket(85)).header.type, MessageType::Pong);
    }

    // ── The client checks that every response answers the request it sent ──────────────────

    namespace
    {
        /// A scripted stand-in for the agent: accepts one connection and runs @p script on it.
        class ScriptedAgent
        {
        public:
            using Script = std::function<void(Detail::SocketHandle)>;

            explicit ScriptedAgent(Script script)
            {
                std::string error;
                listener_ = Detail::CreateTcpListener("127.0.0.1", 0, port_, error);
                EXPECT_NE(listener_, Detail::InvalidSocket) << error;
                thread_ = std::thread([this, script = std::move(script)] {
                    std::string acceptError;
                    const auto client = Detail::AcceptTcp(listener_, 3000, acceptError);
                    if (client == Detail::InvalidSocket) return;
                    script(client);
                    Detail::ShutdownSocket(client);
                    Detail::CloseSocket(client);
                });
            }

            ~ScriptedAgent()
            {
                thread_.join();
                Detail::CloseSocket(listener_);
            }

            ScriptedAgent(const ScriptedAgent&) = delete;
            ScriptedAgent& operator=(const ScriptedAgent&) = delete;

            [[nodiscard]] ClientConfiguration ClientFor() const
            {
                ClientConfiguration configuration;
                configuration.port = port_;
                configuration.authenticationToken = "token";
                configuration.timeoutMilliseconds = 2000;
                return configuration;
            }

        private:
            Detail::SocketHandle listener_ = Detail::InvalidSocket;
            std::uint16_t port_ = 0;
            std::thread thread_;
        };

        /// Answers the ClientHello with @p hello, after letting @p adjust edit the reply packet.
        void AnswerHello(Detail::SocketHandle socket, const HelloResponse& hello,
                         const std::function<void(Packet&)>& adjust = {})
        {
            Packet request;
            std::string error;
            if (!Detail::ReceivePacket(socket, request, 2000, error)) return;
            Packet reply = ProtocolCodec::Encode(hello, request.header.requestId);
            if (adjust) adjust(reply);
            (void)Detail::SendPacket(socket, reply, 2000, error);
        }

        /// Completes a valid handshake, then answers one request with whatever @p reply builds.
        void AnswerOneRequest(Detail::SocketHandle socket, const std::function<Packet(const Packet&)>& reply)
        {
            AnswerHello(socket, ValidServerHello());
            Packet request;
            std::string error;
            if (!Detail::ReceivePacket(socket, request, 2000, error)) return;
            (void)Detail::SendPacket(socket, reply(request), 2000, error);
            // Hold the connection until the client has judged the reply.
            Packet ignored;
            (void)Detail::ReceivePacket(socket, ignored, 500, error);
        }
    }

    TEST(InspectorClientTests, AServerHelloMustAnswerTheHelloAndSelectASupportedVersion)
    {
        const auto connectFails = [](const std::function<void(Detail::SocketHandle)>& script, const char* what) {
            SCOPED_TRACE(what);
            ScriptedAgent agent(script);
            Client client;
            std::string error;
            EXPECT_FALSE(client.Connect(agent.ClientFor(), error));
            EXPECT_FALSE(client.IsConnected());
            EXPECT_FALSE(error.empty());
        };
        connectFails([](Detail::SocketHandle socket) {
            AnswerHello(socket, ValidServerHello(), [](Packet& reply) { reply.header.requestId += 1; });
        }, "reply to another request id");
        connectFails([](Detail::SocketHandle socket) {
            AnswerHello(socket, ValidServerHello(), [](Packet& reply) { reply.header.majorVersion = 2; });
        }, "reply in a major-2 envelope");
        connectFails([](Detail::SocketHandle socket) {
            AnswerHello(socket, ValidServerHello(), [](Packet& reply) { reply.header.minorVersion = 1; });
        }, "reply in a newer minor envelope");
        connectFails([](Detail::SocketHandle socket) {
            auto hello = ValidServerHello();
            hello.selectedMajorVersion = 2;
            AnswerHello(socket, hello);
        }, "ServerHello selecting major 2");
        connectFails([](Detail::SocketHandle socket) {
            auto hello = ValidServerHello();
            hello.selectedMinorVersion = 1;
            AnswerHello(socket, hello);
        }, "ServerHello selecting minor 1");
        connectFails([](Detail::SocketHandle socket) {
            auto hello = ValidServerHello();
            hello.providerInterfaceVersion = 2;
            AnswerHello(socket, hello);
        }, "ServerHello with provider interface 2");

        // The control: the same script with nothing adjusted connects.
        ScriptedAgent agent([](Detail::SocketHandle socket) { AnswerHello(socket, ValidServerHello()); });
        Client client;
        std::string error;
        EXPECT_TRUE(client.Connect(agent.ClientFor(), error)) << error;
        EXPECT_TRUE(client.IsConnected());
    }

    TEST(InspectorClientTests, AMismatchedOrMistypedResponseEndsTheSessionButAStructuredErrorDoesNot)
    {
        {
            ScriptedAgent agent([](Detail::SocketHandle socket) {
                AnswerOneRequest(socket, [](const Packet& request) {
                    Packet pong;
                    pong.header.type = MessageType::Pong;
                    pong.header.requestId = request.header.requestId + 1;
                    return pong;
                });
            });
            Client client;
            std::string error;
            ASSERT_TRUE(client.Connect(agent.ClientFor(), error)) << error;
            EXPECT_FALSE(client.Ping(error)) << "a Pong for another request was accepted";
            EXPECT_FALSE(client.IsConnected());
        }
        {
            ScriptedAgent agent([](Detail::SocketHandle socket) {
                AnswerOneRequest(socket, [](const Packet& request) {
                    return ProtocolCodec::Encode(SnapshotResponse{}, request.header.requestId);
                });
            });
            Client client;
            std::string error;
            ASSERT_TRUE(client.Connect(agent.ClientFor(), error)) << error;
            EXPECT_FALSE(client.Ping(error)) << "a SnapshotResponse was accepted as a Pong";
            EXPECT_FALSE(client.IsConnected());
        }
        {
            ScriptedAgent agent([](Detail::SocketHandle socket) {
                AnswerOneRequest(socket, [](const Packet& request) {
                    ErrorResponse busy;
                    busy.code = ErrorCode::Busy;
                    busy.message = "busy";
                    return ProtocolCodec::Encode(busy, request.header.requestId);
                });
            });
            Client client;
            std::string error;
            ASSERT_TRUE(client.Connect(agent.ClientFor(), error)) << error;
            EXPECT_FALSE(client.Ping(error));
            EXPECT_EQ(error, "busy");
            EXPECT_TRUE(client.IsConnected()) << "a structured error answers the request; the session stays";
        }
    }

    TEST(InspectorClientTests, InvalidConfigurationAndADisconnectedClientFailWithoutTraffic)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);
        const auto valid = MakeClientConfiguration(*agent);
        std::vector<ClientConfiguration> invalid(5, valid);
        invalid[0].host.clear();
        invalid[1].port = 0;
        invalid[2].authenticationToken.clear();
        invalid[3].authenticationToken.assign(257, 't');
        invalid[4].timeoutMilliseconds = 0;
        for (std::size_t i = 0; i < invalid.size(); ++i)
        {
            Client client;
            std::string error;
            EXPECT_FALSE(client.Connect(invalid[i], error)) << "configuration " << i;
            EXPECT_FALSE(error.empty()) << "configuration " << i;
            EXPECT_FALSE(client.IsConnected());
        }
        EXPECT_EQ(provider.snapshotCalls.load(), 0);

        Client client;
        std::string error;
        EXPECT_FALSE(client.Ping(error));
        EXPECT_FALSE(error.empty());
        SnapshotResponse snapshot;
        EXPECT_FALSE(client.CaptureSnapshot(SnapshotRequest{}, snapshot, error));
    }

    // ── Socket error paths ──────────────────────────────────────────────────────────────────

    namespace
    {
        /// A connected loopback pair: the test writes raw bytes on `server` and reads on `client`.
        struct SocketPair
        {
            SocketPair()
            {
                std::string error;
                std::uint16_t port = 0;
                const auto listener = Detail::CreateTcpListener("127.0.0.1", 0, port, error);
                EXPECT_NE(listener, Detail::InvalidSocket) << error;
                client = Detail::ConnectTcp("127.0.0.1", port, 2000, error);
                EXPECT_NE(client, Detail::InvalidSocket) << error;
                server = Detail::AcceptTcp(listener, 2000, error);
                EXPECT_NE(server, Detail::InvalidSocket) << error;
                Detail::CloseSocket(listener);
            }

            ~SocketPair()
            {
                for (const auto socket : {client, server})
                {
                    if (socket == Detail::InvalidSocket) continue;
                    Detail::ShutdownSocket(socket);
                    Detail::CloseSocket(socket);
                }
            }

            void CloseServer()
            {
                Detail::ShutdownSocket(server);
                Detail::CloseSocket(server);
                server = Detail::InvalidSocket;
            }

            Detail::SocketHandle client = Detail::InvalidSocket;
            Detail::SocketHandle server = Detail::InvalidSocket;
        };
    }

    TEST(InspectorSocketTests, ConnectingToAPortWithNoListenerFails)
    {
        std::string error;
        std::uint16_t port = 0;
        const auto listener = Detail::CreateTcpListener("127.0.0.1", 0, port, error);
        ASSERT_NE(listener, Detail::InvalidSocket) << error;
        Detail::CloseSocket(listener);
        error.clear();
        EXPECT_EQ(Detail::ConnectTcp("127.0.0.1", port, 1000, error), Detail::InvalidSocket);
        EXPECT_FALSE(error.empty());
    }

    TEST(InspectorSocketTests, ReceivePacketFailsWhenThePeerCutsThePayloadShort)
    {
        SocketPair pair;
        ASSERT_NE(pair.server, Detail::InvalidSocket);
        std::vector<std::uint8_t> bytes = HandBuiltHeader(1, 0, 13, 0, 10, 1);
        bytes.insert(bytes.end(), {1, 2, 3});
        std::string error;
        ASSERT_TRUE(Detail::SendAll(pair.server, bytes, 1000, error)) << error;
        pair.CloseServer();
        Packet packet;
        EXPECT_FALSE(Detail::ReceivePacket(pair.client, packet, 2000, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(InspectorSocketTests, ReceivePacketRefusesAnOversizedHeaderWithoutWaitingForItsPayload)
    {
        SocketPair pair;
        ASSERT_NE(pair.server, Detail::InvalidSocket);
        const auto header = HandBuiltHeader(1, 0, 13, 0, static_cast<std::uint32_t>(MaximumPayloadBytes) + 1, 1);
        std::string error;
        ASSERT_TRUE(Detail::SendAll(pair.server, header, 1000, error)) << error;
        Packet packet;
        const auto start = std::chrono::steady_clock::now();
        EXPECT_FALSE(Detail::ReceivePacket(pair.client, packet, 3000, error));
        EXPECT_LT(std::chrono::steady_clock::now() - start, 1500ms)
            << "the header alone must be refused; nothing more is coming";
        EXPECT_TRUE(packet.payload.empty());
    }

    TEST(InspectorSocketTests, ReceivePacketTimesOutOnASilentPeer)
    {
        SocketPair pair;
        ASSERT_NE(pair.server, Detail::InvalidSocket);
        Packet packet;
        std::string error;
        const auto start = std::chrono::steady_clock::now();
        EXPECT_FALSE(Detail::ReceivePacket(pair.client, packet, 100, error));
        const auto elapsed = std::chrono::steady_clock::now() - start;
        EXPECT_GE(elapsed, 90ms);
        EXPECT_LT(elapsed, 1500ms);
        EXPECT_NE(error.find("timed out"), std::string::npos) << error;
    }

    TEST(InspectorSocketTests, SendPacketRefusesAHeaderThatMisstatesThePayload)
    {
        SocketPair pair;
        ASSERT_NE(pair.server, Detail::InvalidSocket);
        Packet packet = PingPacket(1);
        packet.payload = {1, 2};
        packet.header.payloadBytes = 3;
        std::string error;
        EXPECT_FALSE(Detail::SendPacket(pair.client, packet, 1000, error));
        EXPECT_FALSE(error.empty());
        // Nothing reached the wire.
        std::array<std::uint8_t, 1> probe{};
        EXPECT_FALSE(Detail::ReceiveExact(pair.server, probe, 100, error));
    }

    // ── The browser bridge, over HTTP ───────────────────────────────────────────────────────

    namespace
    {
        struct HttpReply
        {
            int status = 0;
            std::string headers;
            std::string body;
        };

        HttpReply HttpExchange(std::uint16_t port, const std::string& request)
        {
            HttpReply reply;
            std::string error;
            const auto socket = Detail::ConnectTcp("127.0.0.1", port, 2000, error);
            EXPECT_NE(socket, Detail::InvalidSocket) << error;
            if (socket == Detail::InvalidSocket) return reply;
            const auto requestBytes = std::span(reinterpret_cast<const std::uint8_t*>(request.data()), request.size());
            EXPECT_TRUE(Detail::SendAll(socket, requestBytes, 2000, error)) << error;
            std::vector<std::uint8_t> bytes;
            if (Detail::ReceiveUntil(socket, bytes, "\r\n\r\n", 256U * 1024U, 5000, error))
            {
                const std::string text(bytes.begin(), bytes.end());
                const auto end = text.find("\r\n\r\n");
                reply.headers = text.substr(0, end);
                reply.body = text.substr(end + 4);
                if (reply.headers.rfind("HTTP/1.1 ", 0) == 0) reply.status = std::stoi(reply.headers.substr(9, 3));
                const auto lengthAt = reply.headers.find("Content-Length: ");
                if (lengthAt != std::string::npos)
                {
                    const auto length = static_cast<std::size_t>(std::stoul(reply.headers.substr(lengthAt + 16)));
                    if (reply.body.size() < length)
                    {
                        std::vector<std::uint8_t> rest(length - reply.body.size());
                        EXPECT_TRUE(Detail::ReceiveExact(socket, rest, 5000, error)) << error;
                        reply.body.append(rest.begin(), rest.end());
                    }
                }
            }
            else
            {
                ADD_FAILURE() << "no HTTP response: " << error;
            }
            Detail::ShutdownSocket(socket);
            Detail::CloseSocket(socket);
            return reply;
        }
    }

    TEST(InspectorWebBridgeTests, GuardsHostAndTokenAndCarriesSixtyFourBitValuesAsJsonStrings)
    {
        FakeProvider provider;
        provider.snapshot = MakeSnapshot();
        // Both above 2^53, where a JavaScript number can no longer hold every integer.
        provider.snapshot.resources[0].id = (1ULL << 53U) + 1U;
        provider.snapshot.resources[0].estimatedBytes = 18446744073709551557ULL;
        auto agent = StartAgent(provider, MakeAgentConfiguration());
        ASSERT_NE(agent, nullptr);

        Detail::WebBridgeConfiguration configuration;
        configuration.agent = MakeClientConfiguration(*agent);
        // WebBridge::Run() never returns, so this bridge and its accept thread live until the
        // process exits. The pointer is held by a static, so it is reachable, not leaked.
        static std::vector<Detail::WebBridge*> bridges;
        auto* bridge = bridges.emplace_back(new Detail::WebBridge(configuration));
        std::string error;
        ASSERT_TRUE(bridge->Start(error)) << error;
        Detail::WebBridge* running = bridge;
        std::thread([running] { (void)running->Run(); }).detach();
        const auto port = bridge->GetHttpPort();
        const std::string host = "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";

        EXPECT_EQ(HttpExchange(port, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n").status, 400)
            << "docs/inspector.md: the bridge validates the HTTP Host";
        EXPECT_EQ(HttpExchange(port, "GET / HTTP/1.0\r\n" + host + "\r\n").status, 400);
        EXPECT_EQ(HttpExchange(port, "GET /api/session HTTP/1.1\r\n" + host + "Content-Length: 4\r\n\r\nabcd").status, 400);

        const HttpReply page = HttpExchange(port, "GET / HTTP/1.1\r\n" + host + "\r\n");
        ASSERT_EQ(page.status, 200);
        const std::string tokenMarker = "name=\"cna-ui-token\" content=\"";
        const auto tokenAt = page.body.find(tokenMarker);
        ASSERT_NE(tokenAt, std::string::npos);
        const auto tokenStart = tokenAt + tokenMarker.size();
        const std::string token = page.body.substr(tokenStart, page.body.find('"', tokenStart) - tokenStart);
        ASSERT_EQ(token.size(), 32U) << "a 128-bit hexadecimal UI token";
        EXPECT_EQ(page.body.find("__CNA_UI_TOKEN__"), std::string::npos);

        EXPECT_EQ(HttpExchange(port, "GET /api/session HTTP/1.1\r\n" + host + "\r\n").status, 404)
            << "an /api/ request without the UI token header";
        EXPECT_EQ(HttpExchange(port, "GET /api/session HTTP/1.1\r\n" + host
                                         + "X-CNA-Inspector-UI-Token: " + std::string(32, '0') + "\r\n\r\n").status,
                  404);

        const std::string authorized = host + "X-CNA-Inspector-UI-Token: " + token + "\r\n\r\n";
        const HttpReply session = HttpExchange(port, "GET /api/session HTTP/1.1\r\n" + authorized);
        ASSERT_EQ(session.status, 200) << session.body;
        EXPECT_NE(session.headers.find("Content-Type: application/json"), std::string::npos);
        EXPECT_NE(session.body.find("\"protocolMajor\":1,\"protocolMinor\":0"), std::string::npos) << session.body;
        EXPECT_NE(session.body.find("\"limits\":{\"payloadBytes\":8388608,\"events\":4096,\"previewBytes\":4194304}"),
                  std::string::npos) << session.body;

        const HttpReply snapshot = HttpExchange(port, "GET /api/snapshot?resources=1 HTTP/1.1\r\n" + authorized);
        ASSERT_EQ(snapshot.status, 200) << snapshot.body;
        EXPECT_NE(snapshot.body.find("\"id\":\"9007199254740993\""), std::string::npos) << snapshot.body;
        EXPECT_NE(snapshot.body.find("\"estimatedBytes\":\"18446744073709551557\""), std::string::npos)
            << snapshot.body;
        EXPECT_NE(snapshot.body.find("\"currentFrameNumber\":\"99\""), std::string::npos) << snapshot.body;

        EXPECT_EQ(HttpExchange(port, "GET /api/nothing HTTP/1.1\r\n" + authorized).status, 404);
        EXPECT_EQ(HttpExchange(port, "GET /api/preview?ticket=0 HTTP/1.1\r\n" + authorized).status, 400);
    }
}
