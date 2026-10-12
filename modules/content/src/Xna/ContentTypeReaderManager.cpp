// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "ContentTypeReaderManagerState.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_set>

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

        // The declared built-in readers (see the header), guarded by CreatorsMutex().
        struct DeclaredBuiltins
        {
            bool declared = false;
            bool installed = false;
            // Registered by a game while the built-ins were declared but not installed: the name
            // may be a built-in's, which would have kept the game's registration out.
            std::unordered_set<std::string> provisional;
        };

        DeclaredBuiltins& Declared()
        {
            static DeclaredBuiltins declared;
            return declared;
        }

        thread_local int builtinRegistrationDepth = 0;
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
        auto& declared = Declared();
        const auto existing = creators.find(canonicalName);
        if (existing == creators.end())
        {
            creators.emplace(canonicalName, std::move(factory));
            if (builtinRegistrationDepth == 0 && declared.declared && !declared.installed)
            {
                declared.provisional.insert(canonicalName);
            }
            return;
        }
        if (builtinRegistrationDepth > 0 && declared.provisional.erase(canonicalName) != 0U)
        {
            existing->second = std::move(factory);
        }
    }

    void ContentTypeReaderManager::ClearTypeCreators()
    {
        const std::unique_lock lock(CreatorsMutex());
        TypeCreators().clear();
        Declared() = DeclaredBuiltins{};
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
        return detail::CreateRegisteredReader(canonicalName);
    }

    namespace detail
    {
        struct ContentTypeReaderRegistryAccess
        {
            static std::unordered_map<std::string, ContentTypeReaderManager::ReaderFactory>& Creators()
            {
                return ContentTypeReaderManager::TypeCreators();
            }
        };

        std::unique_ptr<ContentTypeReaderBase> CreateRegisteredReader(const std::string& canonicalName)
        {
            ContentTypeReaderManager::ReaderFactory factory;
            {
                const std::shared_lock lock(CreatorsMutex());
                const auto& creators = ContentTypeReaderRegistryAccess::Creators();
                const auto it = creators.find(canonicalName);
                if (it == creators.end())
                {
                    return nullptr;
                }
                factory = it->second;
            }
            return factory();
        }

        bool HasTypeCreator(const std::string& canonicalName)
        {
            const std::shared_lock lock(CreatorsMutex());
            const auto& creators = ContentTypeReaderRegistryAccess::Creators();
            return creators.find(canonicalName) != creators.end();
        }

        bool EraseTypeCreator(const std::string& canonicalName)
        {
            const std::unique_lock lock(CreatorsMutex());
            Declared().provisional.erase(canonicalName);
            return ContentTypeReaderRegistryAccess::Creators().erase(canonicalName) != 0U;
        }

        void DeclareBuiltinXnbReaders()
        {
            const std::unique_lock lock(CreatorsMutex());
            auto& declared = Declared();
            declared.declared = true;
            // As a second Game's constructor re-registered every reader.
            declared.installed = false;
        }

        bool BuiltinXnbReadersDeclared()
        {
            const std::shared_lock lock(CreatorsMutex());
            return Declared().declared;
        }

        bool DeclaredXnbReadersPending()
        {
            const std::shared_lock lock(CreatorsMutex());
            return Declared().declared && !Declared().installed;
        }

        void NoteEveryBuiltinXnbReaderRegistered()
        {
            const std::unique_lock lock(CreatorsMutex());
            auto& declared = Declared();
            declared.declared = true;
            declared.installed = true;
            declared.provisional.clear();
        }

        void RegisterDeclaredXnbReaders(void (*registerReaders)())
        {
            if (!BuiltinXnbReadersDeclared())
            {
                return;
            }
            const BuiltinXnbRegistrationScope scope;
            registerReaders();
        }

        BuiltinXnbRegistrationScope::BuiltinXnbRegistrationScope()
        {
            ++builtinRegistrationDepth;
        }

        BuiltinXnbRegistrationScope::~BuiltinXnbRegistrationScope()
        {
            --builtinRegistrationDepth;
        }
    }
}
