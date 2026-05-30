#pragma once

#include <stdexcept>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

#include "System/IServiceProvider.hpp"

namespace Microsoft::Xna::Framework
{
    /// Stores game services and retrieves them by service type.
    class GameServiceContainer : public System::IServiceProvider
    {
    public:
        /// Creates an empty service container.
        GameServiceContainer();

        ~GameServiceContainer() override = default;

        GameServiceContainer(const GameServiceContainer&) = delete;
        GameServiceContainer& operator=(const GameServiceContainer&) = delete;
        GameServiceContainer(GameServiceContainer&&) = default;
        GameServiceContainer& operator=(GameServiceContainer&&) = default;

        /// Adds a service provider for the specified service type.
        template <typename TService>
        void AddService(TService* provider)
        {
            if (provider == nullptr)
            {
                throw std::invalid_argument("provider");
            }

            AddService(typeid(TService), static_cast<void*>(provider));
        }

        /// Adds a service provider using a runtime type key.
        void AddService(const std::type_info& type, void* provider);

        /// Gets a service provider for the specified service type.
        template <typename TService>
        [[nodiscard]] TService* GetService() const
        {
            return static_cast<TService*>(GetService(typeid(TService)));
        }

        /// Gets a service provider using a runtime type key.
        [[nodiscard]] void* GetService(const std::type_info& type) const override;

        /// Removes the service registered for the specified service type.
        template <typename TService>
        void RemoveService()
        {
            RemoveService(typeid(TService));
        }

        /// Removes the service registered for the runtime type key.
        void RemoveService(const std::type_info& type);

    private:
        std::unordered_map<std::type_index, void*> services_;
    };
}
