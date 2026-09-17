// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::D3DCommon
{
    /** @brief Which Direct3D debug layer reported a message. */
    enum class D3DDebugLayerApi
    {
        /** @brief ID3D11InfoQueue. */
        Direct3D11,
        /** @brief ID3D12InfoQueue. */
        Direct3D12,
    };

    /**
     * @brief One message a Direct3D debug layer stored.
     *
     * The severity ordinals of D3D11_MESSAGE_SEVERITY and D3D12_MESSAGE_SEVERITY are identical, and the
     * id is the API's own D3D11_MESSAGE_ID or D3D12_MESSAGE_ID, so a message is only meaningful together
     * with its api.
     */
    struct D3DDebugLayerMessage
    {
        /** @brief The debug layer that stored the message. */
        D3DDebugLayerApi api = D3DDebugLayerApi::Direct3D12;
        /** @brief 0 corruption, 1 error, 2 warning, 3 info, 4 message. */
        int severity = 0;
        /** @brief D3D11_MESSAGE_ID or D3D12_MESSAGE_ID. */
        int id = 0;
        /** @brief The layer's description text. */
        std::string description;
    };

    /** @brief Process-wide counts of recorded debug-layer messages. */
    struct D3DDebugLayerTotals
    {
        /** @brief Corruption-severity messages. */
        std::uint64_t corruption = 0;
        /** @brief Error-severity messages. */
        std::uint64_t error = 0;
        /** @brief Warning-severity messages that were not filtered at the queue. */
        std::uint64_t warning = 0;
    };

    /**
     * @brief The one process-wide record of what the Direct3D 11 and 12 debug layers reported.
     *
     * plans/plan_graphics_shared_cleanup.md GSC-0006. Each renderer drains its info queue into this log
     * (at Present, device loss and teardown) and registers that drain while its device lives, so a test
     * harness can observe every message as it is recorded and force a drain of every live device at the
     * end of a test -- which is what lets a debug-layer error fail the test that caused it rather than be
     * printed after it has passed.
     */
    class D3DDebugLayerLog
    {
    public:
        /** @brief Called for every recorded message, after the log has counted it. */
        using Observer = std::function<void(const D3DDebugLayerMessage&)>;

        /**
         * @brief Logs, counts and retains one message, then hands it to the observer.
         *
         * @param message The drained message.
         */
        static void Record(D3DDebugLayerMessage message);

        /**
         * @brief Counts recorded so far in this process.
         *
         * @return The totals.
         */
        [[nodiscard]] static D3DDebugLayerTotals Totals() noexcept;

        /**
         * @brief The most recently recorded messages, oldest first; bounded, the totals keep counting.
         *
         * @return The retained messages.
         */
        [[nodiscard]] static std::vector<D3DDebugLayerMessage> Recent();

        /**
         * @brief Installs the observer every later message is handed to; an empty function removes it.
         *
         * @param observer The observer.
         * @return The observer it replaces, so a scoped user can put it back.
         */
        static Observer SetObserver(Observer observer);

        /**
         * @brief Registers a live device's drain, so DrainLiveQueues() reaches it.
         *
         * @param owner Identity of the registration, normally the renderer.
         * @param drain Moves the device's stored messages into this log.
         */
        static void RegisterLiveQueue(const void* owner, std::function<void()> drain);

        /**
         * @brief Removes the drain registered for @p owner; nothing happens when there is none.
         *
         * @param owner The identity passed to RegisterLiveQueue().
         */
        static void UnregisterLiveQueue(const void* owner);

        /** @brief Runs every registered drain, so no stored message waits for a Present. */
        static void DrainLiveQueues();
    };
}
