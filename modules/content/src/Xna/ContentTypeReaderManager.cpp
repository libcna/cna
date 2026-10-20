// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "ContentTypeReaderManagerState.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_set>

namespace Microsoft::Xna::Framework::Content
{
    namespace detail
    {
        struct ContentTypeReaderRegistryAccess
        {
            static auto& Creators()
            {
                return ContentTypeReaderManager::TypeCreators();
            }
        };
    }

    namespace
    {
        using FactoryRegistration = std::shared_ptr<ContentTypeReaderManager::ReaderFactory>;

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

        struct TargetFactory
        {
            std::string readerName;
            std::weak_ptr<ContentTypeReaderManager::ReaderFactory> registration;
        };

        // Guarded by CreatorsMutex; an association belongs to one registration, not a reusable key.
        std::unordered_map<std::string, TargetFactory>& TargetFactories()
        {
            static std::unordered_map<std::string, TargetFactory> factories;
            return factories;
        }

        void ForgetFactoryTargets(const std::string& readerName)
        {
            std::erase_if(TargetFactories(), [&](const auto& entry) {
                return entry.second.readerName == readerName;
            });
        }

        std::unique_ptr<ContentTypeReaderBase> CreateAndAssociateReader(
            const std::string& readerName, const FactoryRegistration& registration)
        {
            // Preserve the existing per-request callable copy, including mutable captures.
            auto factory = *registration;
            auto reader = factory();
            if (reader && !reader->getTargetTypeNameProperty().empty())
            {
                const std::unique_lock lock(CreatorsMutex());
                const auto& creators = detail::ContentTypeReaderRegistryAccess::Creators();
                const auto current = creators.find(readerName);
                // A callback may remove or replace itself while the lock is released.
                if (current != creators.end() && current->second == registration)
                {
                    TargetFactories()[reader->getTargetTypeNameProperty()] = {readerName, registration};
                }
            }
            return reader;
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

        // Serializes installing declared readers with each other, so that a reader removed after
        // everything is installed cannot be put back by an installation that began before.
        std::mutex& InstallMutex()
        {
            static std::mutex mutex;
            return mutex;
        }
    }

    std::unordered_map<std::string, std::shared_ptr<ContentTypeReaderManager::ReaderFactory>>&
    ContentTypeReaderManager::TypeCreators()
    {
        static std::unordered_map<std::string, std::shared_ptr<ReaderFactory>> creators;
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
            creators.emplace(canonicalName, std::make_shared<ReaderFactory>(std::move(factory)));
            if (builtinRegistrationDepth == 0 && declared.declared && !declared.installed)
            {
                declared.provisional.insert(canonicalName);
            }
            return;
        }
        if (builtinRegistrationDepth > 0 && declared.provisional.erase(canonicalName) != 0U)
        {
            auto replacement = std::make_shared<ReaderFactory>(std::move(factory));
            ForgetFactoryTargets(canonicalName);
            existing->second = std::move(replacement);
        }
    }

    void ContentTypeReaderManager::ClearTypeCreators()
    {
        const std::unique_lock lock(CreatorsMutex());
        TypeCreators().clear();
        TargetFactories().clear();
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
        const std::scoped_lock lock(TargetNamesMutex(), CreatorsMutex());
        TargetTypeNames().clear();
        TargetFactories().clear();
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
        FactoryRegistration registration;
        std::string readerName = canonicalName;
        {
            const std::shared_lock lock(CreatorsMutex());
            const auto associated = TargetFactories().find(canonicalName);
            if (associated != TargetFactories().end())
            {
                registration = associated->second.registration.lock();
                if (registration)
                    readerName = associated->second.readerName;
            }
            if (!registration)
            {
                const auto found = TypeCreators().find(canonicalName);
                if (found == TypeCreators().end())
                    return nullptr;
                registration = found->second;
            }
        }
        return CreateAndAssociateReader(readerName, registration);
    }

    namespace detail
    {
        std::unique_ptr<ContentTypeReaderBase> CreateRegisteredReader(const std::string& canonicalName)
        {
            FactoryRegistration factory;
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
            return CreateAndAssociateReader(canonicalName, factory);
        }

        std::vector<std::string> RegisteredReaderNames()
        {
            const std::shared_lock lock(CreatorsMutex());
            const auto& creators = ContentTypeReaderRegistryAccess::Creators();
            std::vector<std::string> names;
            names.reserve(creators.size());
            for (const auto& [name, factory] : creators)
            {
                names.push_back(name);
            }
            return names;
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
            ForgetFactoryTargets(canonicalName);
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
            // Once every declared reader is installed, the registry holds what the eager
            // registration would hold, a reader removed since included: a load adds nothing.
            const std::lock_guard lock(InstallMutex());
            if (!DeclaredXnbReadersPending())
            {
                return;
            }
            const BuiltinXnbRegistrationScope scope;
            registerReaders();
        }

        void InstallDeclaredXnbReaders(void (*registerEveryReader)())
        {
            if (!DeclaredXnbReadersPending())
            {
                return;
            }
            const std::lock_guard lock(InstallMutex());
            if (DeclaredXnbReadersPending())
            {
                registerEveryReader();
            }
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
