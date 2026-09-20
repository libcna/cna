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

    std::unordered_map<std::type_index, std::string>& ContentTypeReaderManager::TargetTypeNames()
    {
        static std::unordered_map<std::type_index, std::string> names;
        return names;
    }

    void ContentTypeReaderManager::AssociateTargetType(const System::Type& targetType,
                                                       const std::string& canonicalName)
    {
        const std::type_info* info = targetType.getTypeInfo();
        if (info == nullptr || canonicalName.empty())
        {
            return;
        }
        TargetTypeNames()[std::type_index(*info)] = canonicalName;
    }

    void ContentTypeReaderManager::ClearTargetTypeAssociationsEXT()
    {
        TargetTypeNames().clear();
    }

    namespace detail
    {
        void AssociateReaderTargetType(const System::Type& targetType,
                                       const std::string& canonicalName)
        {
            ContentTypeReaderManager::AssociateTargetType(targetType, canonicalName);
        }
    }

    std::unique_ptr<ContentTypeReaderBase> ContentTypeReaderManager::GetTypeReader(
        const System::Type& targetType) const
    {
        return CreateReaderForTargetTypeEXT(targetType);
    }

    std::unique_ptr<ContentTypeReaderBase> ContentTypeReaderManager::CreateReaderForTargetTypeEXT(
        const System::Type& targetType)
    {
        const std::type_info* info = targetType.getTypeInfo();
        if (info == nullptr)
        {
            return nullptr;
        }
        const auto& names = TargetTypeNames();
        const auto found = names.find(std::type_index(*info));
        if (found == names.end())
        {
            return nullptr;
        }
        return CreateReader(found->second);
    }
}
