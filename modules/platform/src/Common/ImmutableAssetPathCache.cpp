// SPDX-License-Identifier: MS-PL

#include "ImmutableAssetPathCache.hpp"

#include <utility>

namespace CNA::Platform::Common {

    namespace {
        std::string FoldAscii(std::string value)
        {
            for (char& character : value)
            {
                if (character >= 'A' && character <= 'Z')
                {
                    character = static_cast<char>(character + ('a' - 'A'));
                }
            }
            return value;
        }
    }

    std::string ImmutableAssetPathCache::Resolve(
        const std::string& requested, const ListDirectory& listDirectory)
    {
        if (requested.empty() || requested.front() == '/')
        {
            return {};
        }

        // Serialize publication AND the first listing: concurrent misses must not list twice.
        const std::lock_guard lock(mutex_);
        std::string resolved;
        std::size_t componentStart = 0;
        while (componentStart < requested.size())
        {
            const std::size_t componentEnd = requested.find('/', componentStart);
            const std::string component = requested.substr(componentStart,
                componentEnd == std::string::npos
                    ? std::string::npos : componentEnd - componentStart);
            componentStart = componentEnd == std::string::npos
                ? requested.size() : componentEnd + 1;
            if (component.empty() || component == ".")
            {
                continue;
            }
            if (component == "..")
            {
                return {};
            }

            auto directory = directories_.find(resolved);
            if (directory == directories_.end())
            {
                const Listing entries = listDirectory(resolved);
                if (!entries)
                {
                    // JNI/environment failures can be transient: publish no partial/negative entry.
                    return {};
                }
                Directory folded;
                for (const std::string& entry : *entries)
                {
                    auto [position, inserted] = folded.try_emplace(FoldAscii(entry), entry);
                    if (!inserted)
                    {
                        position->second.clear();
                    }
                }
                directory = directories_.emplace(resolved, std::move(folded)).first;
            }

            const auto match = directory->second.find(FoldAscii(component));
            if (match == directory->second.end() || match->second.empty())
            {
                return {};
            }
            if (!resolved.empty())
            {
                resolved.push_back('/');
            }
            resolved += match->second;
        }
        return resolved;
    }

} // namespace CNA::Platform::Common
