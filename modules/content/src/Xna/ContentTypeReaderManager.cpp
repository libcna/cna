// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace Microsoft::Xna::Framework::Content
{
    std::unordered_map<std::string, ContentTypeReaderManager::ReaderFactory>&
    ContentTypeReaderManager::TypeCreators()
    {
        static std::unordered_map<std::string, ReaderFactory> creators;
        return creators;
    }

    void ContentTypeReaderManager::AddTypeCreator(const std::string& canonicalName, ReaderFactory factory)
    {
        auto& creators = TypeCreators();
        if (creators.find(canonicalName) == creators.end())
        {
            creators.emplace(canonicalName, std::move(factory));
        }
    }

    void ContentTypeReaderManager::ClearTypeCreators()
    {
        TypeCreators().clear();
    }

    bool ContentTypeReaderManager::RemoveTypeCreatorEXT(const std::string& canonicalName)
    {
        return TypeCreators().erase(canonicalName) != 0U;
    }

    std::unique_ptr<ContentTypeReaderBase> ContentTypeReaderManager::CreateReader(const std::string& canonicalName)
    {
        const auto& creators = TypeCreators();
        const auto it = creators.find(canonicalName);
        if (it == creators.end())
        {
            return nullptr;
        }
        return it->second();
    }

    bool ContentTypeReaderManager::IsRegistered(const std::string& canonicalName)
    {
        const auto& creators = TypeCreators();
        return creators.find(canonicalName) != creators.end();
    }
}
