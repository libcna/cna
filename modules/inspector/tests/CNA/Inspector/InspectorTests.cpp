// SPDX-License-Identifier: MS-PL

#include "CNA/Inspector/Agent.hpp"
#include "CNA/Inspector/Client.hpp"
#include "CNA/Inspector/Protocol.hpp"
#include "../../../src/InternalSocket.hpp"
#include "../../../src/WebAssets.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include <random>
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
        EXPECT_NE(script.find("state.events.length>1000"), std::string_view::npos);
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
}
