// SPDX-License-Identifier: MS-PL

#include "InternalSocket.hpp"
#include "WebBridge.hpp"

#include <charconv>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    void PrintUsage()
    {
        std::cout
            << "CNA Inspector\n\n"
            << "Usage: cna-inspector --agent-port PORT [options]\n\n"
            << "Options:\n"
            << "  --agent-host HOST       Agent host (default 127.0.0.1)\n"
            << "  --agent-port PORT       Agent port printed by the CNA application\n"
            << "  --token TOKEN           Agent authentication token\n"
            << "  --token-file PATH       Read the token from a bounded local file\n"
            << "  --http-port PORT        Local browser port (default: ephemeral)\n"
            << "  --allow-remote-agent    Permit an explicitly configured non-loopback agent\n"
            << "  --help                  Show this help\n\n"
            << "CNA_INSPECTOR_TOKEN may be used instead of --token. The browser server always\n"
            << "binds to 127.0.0.1 and never receives the agent authentication token.\n";
    }

    bool ParsePort(std::string_view text, std::uint16_t& port, bool allowZero = false)
    {
        unsigned value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
            || value > 65535 || (!allowZero && value == 0))
        {
            return false;
        }
        port = static_cast<std::uint16_t>(value);
        return true;
    }

    bool ReadTokenFile(const std::string& path, std::string& token, std::string& error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            error = "could not open Inspector token file";
            return false;
        }
        std::string value(257, '\0');
        input.read(value.data(), static_cast<std::streamsize>(value.size()));
        value.resize(static_cast<std::size_t>(input.gcount()));
        if (value.size() > 256 || (!input.eof() && input.fail()))
        {
            error = "Inspector token file exceeds 256 bytes";
            return false;
        }
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
        if (value.empty())
        {
            error = "Inspector token file is empty";
            return false;
        }
        token = std::move(value);
        return true;
    }
}

namespace
{
int RunInspector(int argc, char** argv)
{
    CNA::Inspector::Detail::WebBridgeConfiguration configuration;
    bool allowRemoteAgent = false;
    std::string tokenFile;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument(argv[i]);
        const auto requireValue = [&](std::string_view option) -> const char* {
            if (i + 1 >= argc)
            {
                std::cerr << option << " requires a value\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (argument == "--help")
        {
            PrintUsage();
            return 0;
        }
        if (argument == "--allow-remote-agent")
        {
            allowRemoteAgent = true;
            continue;
        }
        if (argument == "--agent-host")
        {
            const auto* value = requireValue(argument);
            if (!value) return 2;
            configuration.agent.host = value;
            continue;
        }
        if (argument == "--agent-port" || argument == "--http-port")
        {
            const auto* value = requireValue(argument);
            if (!value) return 2;
            auto& port = argument == "--agent-port" ? configuration.agent.port
                                                     : configuration.httpPort;
            if (!ParsePort(value, port, argument == "--http-port"))
            {
                std::cerr << argument << " is not a valid port\n";
                return 2;
            }
            continue;
        }
        if (argument == "--token")
        {
            const auto* value = requireValue(argument);
            if (!value) return 2;
            configuration.agent.authenticationToken = value;
            continue;
        }
        if (argument == "--token-file")
        {
            const auto* value = requireValue(argument);
            if (!value) return 2;
            tokenFile = value;
            continue;
        }
        std::cerr << "unknown option: " << argument << "\n";
        PrintUsage();
        return 2;
    }

    std::string error;
    if (!tokenFile.empty()
        && !ReadTokenFile(tokenFile, configuration.agent.authenticationToken, error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    if (configuration.agent.authenticationToken.empty())
    {
        if (const auto* environmentToken = std::getenv("CNA_INSPECTOR_TOKEN"))
        {
            configuration.agent.authenticationToken = environmentToken;
        }
    }
    if (configuration.agent.port == 0 || configuration.agent.authenticationToken.empty())
    {
        std::cerr << "--agent-port and an authentication token are required\n";
        PrintUsage();
        return 2;
    }
    if (!CNA::Inspector::Detail::IsLoopbackAddress(configuration.agent.host)
        && !allowRemoteAgent)
    {
        std::cerr << "a non-loopback agent requires --allow-remote-agent\n";
        return 2;
    }

    CNA::Inspector::Detail::WebBridge bridge(std::move(configuration));
    if (!bridge.Start(error))
    {
        std::cerr << "could not start CNA Inspector browser bridge: " << error << '\n';
        return 1;
    }
    // Flushed explicitly: Run() never returns, so when stdout is a pipe or a file (a launcher
    // script, tee, an IDE) a buffered line would never appear, and with the default ephemeral
    // port this URL is the one thing the user needs.
    std::cout << "CNA Inspector is available at http://127.0.0.1:"
              << bridge.GetHttpPort() << "/\n"
              << "The bridge is localhost-only. Press Ctrl+C to stop it." << std::endl;
    return bridge.Run();
}
}

int main(int argc, char** argv)
{
    try
    {
        return RunInspector(argc, argv);
    }
    catch (const std::exception& exception)
    {
        std::cerr << "CNA Inspector failed safely: " << exception.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "CNA Inspector failed safely with an unknown error\n";
        return 1;
    }
}
