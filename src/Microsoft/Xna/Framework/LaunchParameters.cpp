#include "Microsoft/Xna/Framework/LaunchParameters.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace Microsoft::Xna::Framework
{
    LaunchParameters::LaunchParameters()
    {
        Parse(GetCommandLineArgs());
    }

    LaunchParameters::LaunchParameters(const std::vector<std::string>& args)
    {
        Parse(args);
    }

    bool LaunchParameters::ContainsKey(const std::string& key) const
    {
        return find(key) != end();
    }

    void LaunchParameters::Add(const std::string& key, const std::string& value)
    {
        emplace(key, value);
    }

    std::vector<std::string> LaunchParameters::GetCommandLineArgs()
    {
#if defined(_WIN32)
        std::vector<std::string> result;
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv == nullptr)
        {
            return result;
        }

        for (int i = 0; i < argc; ++i)
        {
            const int needed = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 0)
            {
                continue;
            }

            std::string value(static_cast<std::size_t>(needed - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, value.data(), needed, nullptr, nullptr);
            result.push_back(std::move(value));
        }

        LocalFree(argv);
        return result;
#elif defined(__linux__) || defined(__ANDROID__)
        std::vector<std::string> result;
        std::ifstream file("/proc/self/cmdline", std::ios::binary);
        if (!file)
        {
            return result;
        }

        std::string current;
        char ch;
        while (file.get(ch))
        {
            if (ch == '\0')
            {
                result.push_back(current);
                current.clear();
            }
            else
            {
                current.push_back(ch);
            }
        }

        if (!current.empty())
        {
            result.push_back(current);
        }

        return result;
#else
        // Portable C++ does not expose process arguments globally.
        // Platforms that need this should pass argv through the explicit constructor
        // or move this helper into sharp-runtime's Environment implementation.
        return {};
#endif
    }

    void LaunchParameters::Parse(const std::vector<std::string>& args)
    {
        for (const std::string& raw : args)
        {
            const std::string arg = TrimStartFlags(raw);

            // One char for ':', one for key, one for value.
            if (arg.size() < 3)
            {
                continue;
            }

            const std::size_t valueOffset = arg.find(':', 1);
            if (valueOffset == std::string::npos || valueOffset > arg.size() - 2)
            {
                continue;
            }

            const std::string key = arg.substr(0, valueOffset);
            if (!ContainsKey(key))
            {
                Add(key, arg.substr(valueOffset + 1));
            }
        }
    }

    std::string LaunchParameters::TrimStartFlags(const std::string& value)
    {
        std::size_t start = 0;
        while (start < value.size() && (value[start] == '/' || value[start] == '-'))
        {
            ++start;
        }

        return value.substr(start);
    }
}
