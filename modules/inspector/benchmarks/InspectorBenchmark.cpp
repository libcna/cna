// SPDX-License-Identifier: MS-PL

#include "CNA/Inspector/Agent.hpp"
#include "CNA/Inspector/Client.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    class BenchmarkProvider final : public CNA::Diagnostics::IDiagnosticsProvider
    {
    public:
        BenchmarkProvider()
        {
            snapshot.buildMode = CNA::Diagnostics::Mode::Full;
            snapshot.runtimeMode = CNA::Diagnostics::Mode::Full;
            snapshot.metrics.push_back({1, "Graphics/DrawCalls", 100,
                CNA::Diagnostics::MetricKind::FrameCounter,
                CNA::Diagnostics::MetricUnit::Count,
                CNA::Diagnostics::Accuracy::Exact});
            for (std::uint64_t i = 0; i < 240; ++i)
            {
                CNA::Diagnostics::FrameSample frame;
                frame.frameNumber = i + 1;
                frame.durationNs = 16666667;
                frame.framesPerSecond = 60.0;
                frame.metrics = snapshot.metrics;
                snapshot.recentFrames.push_back(frame);
            }
            snapshot.resources.push_back({193, CNA::Diagnostics::ResourceKind::Texture2D,
                "Benchmark texture", "Color", 512, 512, 1, 1, 1048576,
                CNA::Diagnostics::Accuracy::Estimated});
            events.oldestAvailableSequence = 1;
            events.newestAvailableSequence = 512;
            for (std::uint64_t i = 0; i < 512; ++i)
            {
                events.events.push_back({i + 1, i * 1000, 500, i / 4, 1, i + 1, 0, 0, 7,
                    CNA::Diagnostics::EventKind::Zone, CNA::Diagnostics::Category::Update});
            }
        }

        CNA::Diagnostics::Snapshot CaptureSnapshot() override
        {
            ++snapshotCalls;
            return snapshot;
        }

        CNA::Diagnostics::EventBatch ReadEvents(std::uint64_t, std::size_t maximum) override
        {
            ++eventCalls;
            auto result = events;
            if (result.events.size() > maximum) result.events.resize(maximum);
            return result;
        }

        std::string ResolveName(CNA::Diagnostics::NameId) override
        {
            return "Game/Update";
        }

        CNA::Diagnostics::Snapshot snapshot;
        CNA::Diagnostics::EventBatch events;
        std::atomic<std::uint64_t> snapshotCalls{0};
        std::atomic<std::uint64_t> eventCalls{0};
    };

    class BenchmarkPreview final : public CNA::Inspector::IResourcePreviewProvider
    {
    public:
        CNA::Inspector::PreviewResponse RequestPreview(
            const CNA::Inspector::PreviewRequest&) override
        {
            CNA::Inspector::PreviewResponse response;
            response.status = CNA::Inspector::PreviewStatus::Ready;
            response.width = 512;
            response.height = 512;
            response.mimeType = "image/png";
            response.encodedImage.resize(1024U * 1024U, 0x5A);
            return response;
        }

        CNA::Inspector::PreviewResponse PollPreview(std::uint64_t) override
        {
            return {};
        }
    };

    double ApplicationLoop(std::uint64_t iterations)
    {
        std::uint64_t value = 1;
        const auto start = Clock::now();
        for (std::uint64_t i = 0; i < iterations; ++i)
        {
            value = value * 1664525U + 1013904223U + i;
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
        const auto elapsed = Clock::now() - start;
        if (value == 0) std::cerr << value;
        return std::chrono::duration<double, std::nano>(elapsed).count() / iterations;
    }

    template<typename Function>
    double AverageMicroseconds(std::size_t iterations, Function&& function)
    {
        const auto start = Clock::now();
        for (std::size_t i = 0; i < iterations; ++i) function();
        const auto elapsed = Clock::now() - start;
        return std::chrono::duration<double, std::micro>(elapsed).count() / iterations;
    }
}

int main()
{
    constexpr std::uint64_t applicationIterations = 50000000;
    const auto linkedDisabled = ApplicationLoop(applicationIterations);

    BenchmarkProvider provider;
    auto preview = std::make_shared<BenchmarkPreview>();
    CNA::Inspector::AgentConfiguration agentConfiguration;
    agentConfiguration.authenticationToken = "benchmark-token";
    agentConfiguration.previewProvider = preview;
    agentConfiguration.maximumRequestsPerSecond = 1024;
    agentConfiguration.previewCooldownMilliseconds = 50;
    std::string error;
    auto agent = CNA::Inspector::Agent::Start(provider, agentConfiguration, error);
    if (!agent)
    {
        std::cerr << error << '\n';
        return 1;
    }
    const auto enabledNoClient = ApplicationLoop(applicationIterations);
    const auto idleProviderCalls = provider.snapshotCalls.load() + provider.eventCalls.load();

    CNA::Inspector::ClientConfiguration clientConfiguration;
    clientConfiguration.port = agent->GetPort();
    clientConfiguration.authenticationToken = agent->GetAuthenticationToken();
    CNA::Inspector::Client client;
    if (!client.Connect(clientConfiguration, error))
    {
        std::cerr << error << '\n';
        return 1;
    }

    CNA::Inspector::SnapshotResponse snapshot;
    const auto liveBasic = AverageMicroseconds(500, [&] {
        if (!client.CaptureSnapshot({}, snapshot, error)) throw std::runtime_error(error);
    });
    CNA::Inspector::EventsResponse events;
    const auto activeProfiling = AverageMicroseconds(200, [&] {
        if (!client.ReadEvents({0, 512}, events, error)) throw std::runtime_error(error);
    });

    std::vector<double> previewMilliseconds;
    for (int i = 0; i < 5; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(55));
        CNA::Inspector::PreviewResponse response;
        const auto start = Clock::now();
        if (!client.RequestPreview({193, 512, 512, 1024U * 1024U}, response, error))
        {
            std::cerr << error << '\n';
            return 1;
        }
        previewMilliseconds.push_back(
            std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    std::sort(previewMilliseconds.begin(), previewMilliseconds.end());

    std::cout << "compiled_in_disabled_ns_per_iteration=" << linkedDisabled << '\n'
              << "enabled_no_client_ns_per_iteration=" << enabledNoClient
              << " provider_calls=" << idleProviderCalls << '\n'
              << "live_basic_snapshot_us_per_request=" << liveBasic << '\n'
              << "active_profiling_512_events_us_per_request=" << activeProfiling << '\n'
              << "explicit_1MiB_preview_median_ms="
              << previewMilliseconds[previewMilliseconds.size() / 2] << '\n';
    return 0;
}
