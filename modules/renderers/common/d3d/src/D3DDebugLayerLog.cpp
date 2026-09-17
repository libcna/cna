// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/D3DCommon/D3DDebugLayerLog.hpp"

#include "CNA/Logger.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace CNA::Internal::Renderers::D3DCommon
{
    namespace
    {
        constexpr std::size_t kRetainedMessages = 256;

        struct LogState
        {
            std::mutex mutex;
            D3DDebugLayerTotals totals;
            std::deque<D3DDebugLayerMessage> recent;
            D3DDebugLayerLog::Observer observer;
            std::vector<std::pair<const void*, std::function<void()>>> drains;
        };

        LogState& State()
        {
            static LogState state;
            return state;
        }

        const char* ApiName(D3DDebugLayerApi api)
        {
            return api == D3DDebugLayerApi::Direct3D11 ? "D3D11" : "D3D12";
        }
    }

    void D3DDebugLayerLog::Record(D3DDebugLayerMessage message)
    {
        LogState& state = State();
        const std::string text = std::string(ApiName(message.api)) + " debug layer [severity " +
                                 std::to_string(message.severity) + ", id " +
                                 std::to_string(message.id) + "]: " + message.description;
        Observer observer;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            switch (message.severity)
            {
                case 0: ++state.totals.corruption; break;
                case 1: ++state.totals.error; break;
                default: ++state.totals.warning; break;
            }
            state.recent.push_back(message);
            if (state.recent.size() > kRetainedMessages)
                state.recent.pop_front();
            observer = state.observer;
        }
        if (message.severity <= 1)
            CNA::Logger::Error(text, CNA::LogCategory::RENDER);
        else
            CNA::Logger::Warn(text, CNA::LogCategory::RENDER);
        if (observer)
            observer(message);
    }

    D3DDebugLayerTotals D3DDebugLayerLog::Totals() noexcept
    {
        LogState& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.totals;
    }

    std::vector<D3DDebugLayerMessage> D3DDebugLayerLog::Recent()
    {
        LogState& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        return {state.recent.begin(), state.recent.end()};
    }

    D3DDebugLayerLog::Observer D3DDebugLayerLog::SetObserver(Observer observer)
    {
        LogState& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        std::swap(state.observer, observer);
        return observer;
    }

    void D3DDebugLayerLog::RegisterLiveQueue(const void* owner, std::function<void()> drain)
    {
        LogState& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.drains.emplace_back(owner, std::move(drain));
    }

    void D3DDebugLayerLog::UnregisterLiveQueue(const void* owner)
    {
        LogState& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        std::erase_if(state.drains, [owner](const auto& entry) { return entry.first == owner; });
    }

    void D3DDebugLayerLog::DrainLiveQueues()
    {
        // Copied out, because each drain records messages and so takes the lock itself.
        std::vector<std::function<void()>> drains;
        {
            LogState& state = State();
            std::lock_guard<std::mutex> lock(state.mutex);
            for (const auto& entry : state.drains)
                drains.push_back(entry.second);
        }
        for (const auto& drain : drains)
            drain();
    }
}
