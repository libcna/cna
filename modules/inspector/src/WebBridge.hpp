// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Inspector/Client.hpp"

#include <cstdint>
#include <string>

namespace CNA::Inspector::Detail
{
    struct WebBridgeConfiguration
    {
        ClientConfiguration agent;
        std::uint16_t httpPort = 0;
    };

    class WebBridge
    {
    public:
        explicit WebBridge(WebBridgeConfiguration configuration);
        ~WebBridge();
        WebBridge(const WebBridge&) = delete;
        WebBridge& operator=(const WebBridge&) = delete;

        [[nodiscard]] bool Start(std::string& error);
        [[nodiscard]] int Run();
        [[nodiscard]] std::uint16_t GetHttpPort() const noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
