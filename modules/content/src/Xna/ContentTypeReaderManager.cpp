// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

#include <mutex>
#include <shared_mutex>

namespace Microsoft::Xna::Framework::Content
{
    namespace
    {
        // Built-in readers register on the first Load<T>() of their type (detail::BuiltinContent),
        // which may run on a loading thread while another thread reads a file, so the two tables
        // are guarded. A factory is always called outside the lock: a reader's constructor may
        // associate its target type.
        std::shared_mutex& CreatorsMutex()
        {
            static std::shared_mutex mutex;
            return mutex;
        }

        std::shared_mutex& TargetNamesMutex()
        {
            static std::shared_mutex mutex;
            return mutex;
        }
    }

    std::unordered_map<std::string, ContentTypeReaderManager::ReaderFactory>&
    ContentTypeReaderManager::TypeCreators()
    {
        static std::unordered_map<std::string, ReaderFactory> creators;
        return creators;
    }

    void ContentTypeReaderManager::AddTypeCreator(const std::string& canonicalName, ReaderFactory factory)
    {
        const std::unique_lock lock(CreatorsMutex());
        auto& creators = TypeCreators();
        if (creators.find(canonicalName) == creators.end())
        {
            creators.emplace(canonicalName, std::move(factory));
        }
    }

    void ContentTypeReaderManager::ClearTypeCreators()
    {
        const std::unique_lock lock(CreatorsMutex());
        TypeCreators().clear();
    }

    bool ContentTypeReaderManager::RemoveTypeCreatorEXT(const std::string& canonicalName)
    {
        const std::unique_lock lock(CreatorsMutex());
        return TypeCreators().erase(canonicalName) != 0U;
    }

    std::unique_ptr<ContentTypeReaderBase> ContentTypeReaderManager::CreateReader(const std::string& canonicalName)
    {
        ReaderFactory factory;
        {
            const std::shared_lock lock(CreatorsMutex());
            const auto& creators = TypeCreators();
            const auto it = creators.find(canonicalName);
            if (it == creators.end())
            {
                return nullptr;
            }
            factory = it->second;
        }
        return factory();
    }

    bool ContentTypeReaderManager::IsRegistered(const std::string& canonicalName)
    {
        const std::shared_lock lock(CreatorsMutex());
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
        const std::unique_lock lock(TargetNamesMutex());
        TargetTypeNames()[std::type_index(*info)] = canonicalName;
    }

    void ContentTypeReaderManager::ClearTargetTypeAssociationsEXT()
    {
        const std::unique_lock lock(TargetNamesMutex());
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
        std::string canonicalName;
        {
            const std::shared_lock lock(TargetNamesMutex());
            const auto& names = TargetTypeNames();
            const auto found = names.find(std::type_index(*info));
            if (found == names.end())
            {
                return nullptr;
            }
            canonicalName = found->second;
        }
        return CreateReader(canonicalName);
    }
}
