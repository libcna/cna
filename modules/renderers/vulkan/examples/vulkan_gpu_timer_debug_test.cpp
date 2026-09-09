// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2246: Vulkan timestamp queries and debug-utils integration.

#include "CNA/Graphics/GpuTimer.hpp"
#include "CNA/Logger.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using CNA::Graphics::GpuTimer;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    int passCount = 0;
    int failCount = 0;

    void Check(const bool condition, const std::string& label, const std::string& detail = {})
    {
        std::printf("[%s] %s%s%s\n", condition ? "PASS" : "FAIL", label.c_str(),
                    detail.empty() ? "" : ": ", detail.c_str());
        std::fflush(stdout);
        if (condition) ++passCount; else ++failCount;
    }

    bool Collect(GpuTimer& timer, GraphicsDevice& device)
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            if (timer.poll()) return true;
            device.Clear(Color::Black);
            device.Present();
        }
        return timer.poll();
    }
}

int main()
{
    GraphicsDevice device;
    auto* renderer = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
    Check(renderer != nullptr, "A the live renderer is Vulkan");
    if (renderer == nullptr) return 1;

    const auto& properties = renderer->GetPhysicalDevicePropertiesEXT();
    const bool nativeTimer = renderer->GetGraphicsQueueTimestampValidBitsEXT() != 0 &&
        properties.limits.timestampPeriod > 0.0f;
    const std::uint64_t expectedPicoseconds = nativeTimer
        ? std::max<std::uint64_t>(
              1, static_cast<std::uint64_t>(
                     std::llround(static_cast<long double>(
                         properties.limits.timestampPeriod) * 1000.0L)))
        : 0;
    const auto timerFeature =
        device.GetRendererFeatureSupportEXT(CNA::RendererFeature::GpuTimers);
    const auto timestampLimit =
        device.GetRendererLimitEXT(CNA::RendererLimit::TimestampPeriodPicoseconds);
    GpuTimer timer(device);
    Check(renderer->SupportsGpuTimerEXT() == nativeTimer &&
              timer.isSupported() == nativeTimer &&
              timerFeature == (nativeTimer ? CNA::RendererFeatureSupport::Supported
                                           : CNA::RendererFeatureSupport::Unsupported) &&
              timestampLimit.known && timestampLimit.value == expectedPicoseconds &&
              renderer->GetTimestampPeriodPicosecondsEXT() == expectedPicoseconds,
          "B timer capability and period equal the selected queue/device facts",
          "validBits=" +
              std::to_string(renderer->GetGraphicsQueueTimestampValidBitsEXT()) +
              " periodPs=" + std::to_string(timestampLimit.value));

    if (!nativeTimer)
    {
        Check(!timer.isSupported() && !timer.getUnsupportedReason().empty() &&
                  renderer->GetGpuTimerQueryPoolCreateCountEXT() == 0,
              "C an unsupported timestamp path stays false, empty and allocation-free",
              timer.getUnsupportedReason());
    }
    else
    {
        RenderTarget2D target(device, 512, 512);
        const auto at = [](const float x, const float y) {
            return VertexPositionColor(Vector3(x, y, 0.0f), Color::White);
        };
        const VertexPositionColor quad[6] = {
            at(-1.0f, -1.0f), at(-1.0f, 1.0f), at(1.0f, 1.0f),
            at(-1.0f, -1.0f), at(1.0f, 1.0f), at(1.0f, -1.0f),
        };
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.World = Matrix::getIdentityProperty();
        effect.View = Matrix::getIdentityProperty();
        effect.Projection = Matrix::getIdentityProperty();
        const auto drawWork = [&](const int draws, const Color& clear) {
            device.SetRenderTarget(&target);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.Clear(clear);
            effect.Apply();
            device.SetVertexBuffer(nullptr);
            for (int i = 0; i < draws; ++i)
                device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            device.SetRenderTarget(nullptr);
        };
        const std::uint64_t poolCreates = renderer->GetGpuTimerQueryPoolCreateCountEXT();
        const std::uint64_t resetBaseline = renderer->GetGpuTimerQueryResetCountEXT();
        const std::uint64_t waits = renderer->GetDeviceWaitIdleCountEXT();
        const std::uint64_t markerBaseline = renderer->GetRecordedDebugMarkerCountEXT();
        const std::uint64_t regionBeginBaseline =
            renderer->GetRecordedDebugRegionBeginCountEXT();
        const std::uint64_t regionEndBaseline =
            renderer->GetRecordedDebugRegionEndCountEXT();

        timer.begin();
        device.SetStringMarkerEXT("MOD-2246 timed draw range");
        drawWork(24, Color::Blue);
        timer.end();
        device.Present();
        const bool firstCollected = Collect(timer, device);
        Check(firstCollected && timer.getSampleCount() == 1 &&
                  timer.getLastMilliseconds() > 0.0,
              "C a closed graphics range returns a real positive GPU duration",
              firstCollected ? std::to_string(timer.getLastMilliseconds()) + " ms"
                             : "result remained unavailable");

        timer.begin();
        drawWork(4, Color::Green);
        timer.end();
        device.Present();
        const bool secondCollected = Collect(timer, device);
        Check(secondCollected && timer.getSampleCount() == 2 &&
                  renderer->GetGpuTimerQueryPoolCreateCountEXT() == poolCreates &&
                  renderer->GetGpuTimerQueryResetCountEXT() >= resetBaseline + 2,
              "D repeated samples recycle one timestamp query pool",
              "pools=" + std::to_string(poolCreates) +
                  " resets=" +
                  std::to_string(renderer->GetGpuTimerQueryResetCountEXT() - resetBaseline));

        const bool labels = renderer->SupportsDebugUtilsLabelsEXT();
        const std::uint64_t markers =
            renderer->GetRecordedDebugMarkerCountEXT() - markerBaseline;
        const std::uint64_t regionBegins =
            renderer->GetRecordedDebugRegionBeginCountEXT() - regionBeginBaseline;
        const std::uint64_t regionEnds =
            renderer->GetRecordedDebugRegionEndCountEXT() - regionEndBaseline;
        Check(labels ? (markers == 1 && regionBegins > 0 && regionBegins == regionEnds)
                     : (markers == 0 && regionBegins == 0 && regionEnds == 0),
              "E debug labels are structured and balanced, or honestly unavailable",
              std::string("supported=") + (labels ? "true" : "false") +
                  " markers=" + std::to_string(markers) +
                  " regions=" + std::to_string(regionBegins) + "/" +
                  std::to_string(regionEnds));
        Check(renderer->GetDeviceWaitIdleCountEXT() == waits,
              "F timer measurement adds no full-device wait",
              "waits=" + std::to_string(waits));
    }

    const bool messages = renderer->SupportsDebugUtilsMessagesEXT();
    const std::size_t validationBaseline = renderer->GetValidationMessagesEXT().size();
    const CNA::LogLevel oldLevel = CNA::Logger::GetMinimumLevel();
    std::vector<std::string> logged;
    std::vector<CNA::LogLevel> levels;
    std::vector<CNA::LogCategory> categories;
    CNA::Logger::SetMinimumLevel(CNA::LogLevel::TRACE);
    CNA::Logger::SetSink(
        [&](const CNA::LogLevel level, const CNA::LogCategory category,
            const std::string_view text) {
            levels.push_back(level);
            categories.push_back(category);
            logged.emplace_back(text);
        });
    const bool submitted =
        renderer->SubmitDebugUtilsMessageForTestEXT("MOD-2246 structured debug message");
    CNA::Logger::ResetSink();
    CNA::Logger::SetMinimumLevel(oldLevel);

    const bool messageResult = messages
        ? submitted && logged.size() == 1 && levels.front() == CNA::LogLevel::WARN &&
              categories.front() == CNA::LogCategory::GPU &&
              logged.front().find("MOD-2246 structured debug message") != std::string::npos &&
              renderer->GetValidationMessagesEXT().size() == validationBaseline + 1
        : !submitted && logged.empty() &&
              renderer->GetValidationMessagesEXT().size() == validationBaseline;
    Check(messageResult,
          "G debug-utils messages use the CNA logger exactly once, or remain empty",
          std::string("supported=") + (messages ? "true" : "false") +
              " logged=" + std::to_string(logged.size()));

    const std::size_t expectedMessages =
        validationBaseline + (messages && submitted ? 1u : 0u);
    Check(renderer->GetValidationMessagesEXT().size() == expectedMessages,
          "H timer and label commands emit no unexpected Vulkan validation message",
          renderer->GetValidationMessagesEXT().size() == expectedMessages
              ? "none"
              : renderer->GetValidationMessagesEXT().back());

    std::printf("=== %d/%d PASS ===\n", passCount, passCount + failCount);
    return failCount == 0 ? 0 : 1;
}
