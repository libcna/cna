// SPDX-License-Identifier: MS-PL
// ContentManager::GetXnbReaderUsageSummary().
//
// Link boundary: it asks ContentTypeReaderManager whether each reader is registered, which first
// installs every declared built-in reader (ContentTypeReaderManagerQueries.cpp), so it is apart
// from the manager that every game links.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Microsoft::Xna::Framework::Content
{
    std::vector<ContentManifestReaderUsage> ContentManager::GetXnbReaderUsageSummary()
    {
        const auto& manifest = GetContentManifest();

        std::unordered_map<std::string, int> counts;
        for (const auto& entry : manifest)
        {
            for (const auto& readerName : entry.xnbReaderNames)
            {
                ++counts[readerName];
            }
        }

        std::vector<ContentManifestReaderUsage> result;
        result.reserve(counts.size());
        for (const auto& [readerName, count] : counts)
        {
            ContentManifestReaderUsage usage;
            usage.readerName = readerName;
            usage.fileCount = count;
            usage.isRegistered = ContentTypeReaderManager::IsRegistered(readerName);
            result.push_back(std::move(usage));
        }
        return result;
    }
}
