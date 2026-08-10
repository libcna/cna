// SPDX-License-Identifier: MS-PL

#pragma once

#include <stdexcept>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

#include "CNA/CNAHelper.hpp"
#include "System/IServiceProvider.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Stores game services and retrieves them by service type. */
    class GameServiceContainer : public System::IServiceProvider
    {
    public:
        /** @brief Creates an empty service container. */
        GameServiceContainer();

        /** @brief Destructor. */
        ~GameServiceContainer() override = default;

        /** @brief Copying is disabled; services are registered by pointer identity. */
        CNAEXT GameServiceContainer(const GameServiceContainer&) = delete;
        /** @brief Copy assignment is disabled. */
        CNAEXT GameServiceContainer& operator=(const GameServiceContainer&) = delete;
        /** @brief Move constructor. */
        CNAEXT GameServiceContainer(GameServiceContainer&&) = default;
        /** @brief Move assignment operator. */
        CNAEXT GameServiceContainer& operator=(GameServiceContainer&&) = default;

        /**
         * @brief Adds a service provider for the specified service type.
         * @tparam TService The service interface type to register under.
         * @param provider Pointer to the service implementation; must not be null.
         */
        template <typename TService>
        void AddService(TService* provider)
        {
            if (provider == nullptr)
            {
                throw std::invalid_argument("provider");
            }

            AddService(typeid(TService), static_cast<void*>(provider));
        }

        /**
         * @brief Adds a service provider using a runtime type key.
         * @param type The runtime type used as the service key.
         * @param provider Pointer to the service implementation.
         */
        void AddService(const std::type_info& type, void* provider);

        /**
         * @brief Gets a service provider for the specified service type.
         * @tparam TService The service interface type to look up.
         * @return Pointer to the registered service, or nullptr if not found.
         */
        template <typename TService>
        [[nodiscard]] TService* GetService() const
        {
            return static_cast<TService*>(GetService(typeid(TService)));
        }

        /**
         * @brief Gets a service provider using a runtime type key.
         * @param type The runtime type used as the service key.
         * @return Pointer to the registered service, or nullptr if not found.
         */
        [[nodiscard]] void* GetService(const std::type_info& type) const override;

        /**
         * @brief Removes the service registered for the specified service type.
         * @tparam TService The service interface type to unregister.
         */
        template <typename TService>
        void RemoveService()
        {
            RemoveService(typeid(TService));
        }

        /**
         * @brief Removes the service registered for the runtime type key.
         * @param type The runtime type whose service entry should be removed.
         */
        void RemoveService(const std::type_info& type);

    private:
        std::unordered_map<std::type_index, void*> services_;
    };
}
