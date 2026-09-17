// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Inspector/Protocol.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Inspector
{
    /**
     * @brief Optional non-blocking source for explicitly requested resource previews.
     *
     * Implementations must only start or poll asynchronous readback and return promptly. They
     * must never wait for a render device or queue. No provider is installed by default.
     */
    class IResourcePreviewProvider
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IResourcePreviewProvider() = default;

        /**
         * @brief Starts one explicit bounded resource preview.
         * @param request Validated resource identifier and output bounds.
         * @return Unavailable, pending, ready, or failed preview state.
         */
        [[nodiscard]] virtual PreviewResponse RequestPreview(const PreviewRequest& request) = 0;

        /**
         * @brief Polls one ticket without waiting for GPU work.
         * @param ticket Ticket previously returned with Pending status.
         * @return Current bounded preview state.
         */
        [[nodiscard]] virtual PreviewResponse PollPreview(std::uint64_t ticket) = 0;
    };

    /** @brief Explicit application-side Inspector agent configuration. */
    struct AgentConfiguration
    {
        std::string bindAddress = "127.0.0.1";
        std::uint16_t port = 0;
        bool allowRemote = false;
        std::string authenticationToken;
        std::string applicationName = "CNA application";
        std::string platformBackend;
        std::string buildConfiguration;
        std::vector<MetadataEntry> metadata;
        std::shared_ptr<IResourcePreviewProvider> previewProvider;
        std::uint32_t maximumRequestsPerSecond = 64;
        std::uint32_t maximumPendingPreviews = 4;
        std::uint32_t previewCooldownMilliseconds = 250;
    };

    /**
     * @brief Optional authenticated diagnostics agent owned explicitly by a CNA application.
     *
     * The agent owns one background network thread. It is never started automatically and it
     * never executes on an instrumentation or render thread.
     */
    class Agent
    {
    public:
        /** @brief Stops the agent and releases its local endpoint. */
        ~Agent();

        /**
         * @brief Agents cannot be copied.
         * @param other Agent that would otherwise be copied.
         */
        Agent(const Agent& other) = delete;

        /**
         * @brief Agents cannot be copy-assigned.
         * @param other Agent that would otherwise be assigned.
         * @return This agent is never returned because assignment is deleted.
         */
        Agent& operator=(const Agent& other) = delete;

        /**
         * @brief Starts an agent over the process diagnostics provider.
         * @param configuration Explicit security, identity, and limit configuration.
         * @param error Receives a startup error.
         * @return Running agent, or null on failure.
         */
        [[nodiscard]] static std::unique_ptr<Agent> Start(
            const AgentConfiguration& configuration,
            std::string& error);

        /**
         * @brief Starts an agent over an injected version-1 provider.
         * @param provider Provider that must outlive the returned agent.
         * @param configuration Explicit security, identity, and limit configuration.
         * @param error Receives a startup error.
         * @return Running agent, or null on failure.
         */
        [[nodiscard]] static std::unique_ptr<Agent> Start(
            Diagnostics::IDiagnosticsProvider& provider,
            const AgentConfiguration& configuration,
            std::string& error);

        /** @brief Stops the agent; safe to call more than once. */
        void Stop() noexcept;

        /**
         * @brief Returns whether the background listener is running.
         * @return True while the agent accepts authenticated clients.
         */
        [[nodiscard]] bool IsRunning() const noexcept;

        /**
         * @brief Returns the address on which the agent listens.
         * @return Configured bind address.
         */
        [[nodiscard]] const std::string& GetBindAddress() const noexcept;

        /**
         * @brief Returns the selected TCP port.
         * @return Bound port, including the ephemeral port selected for a zero configuration.
         */
        [[nodiscard]] std::uint16_t GetPort() const noexcept;

        /**
         * @brief Returns the session authentication token.
         * @return Configured or securely generated token.
         */
        [[nodiscard]] const std::string& GetAuthenticationToken() const noexcept;

    private:
        class Impl;
        explicit Agent(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };
}
