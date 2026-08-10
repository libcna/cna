// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/GameServiceContainer.hpp"

namespace Microsoft::Xna::Framework
{
    GameServiceContainer::GameServiceContainer()
        : services_()
    {
    }

    void GameServiceContainer::AddService(const std::type_info& type, void* provider)
    {
        if (provider == nullptr)
        {
            throw std::invalid_argument("provider");
        }

        const std::type_index key(type);

        if (services_.find(key) != services_.end())
        {
            throw std::invalid_argument("A service with this type is already registered.");
        }

        // FNA also checks type.IsAssignableFrom(provider.GetType()) here.
        // C++ has no runtime reflection to verify assignability across void*, so the check is omitted.
        services_.emplace(key, provider);
    }

    void* GameServiceContainer::GetService(const std::type_info& type) const
    {
        const auto it = services_.find(std::type_index(type));
        if (it == services_.end())
        {
            return nullptr;
        }

        return it->second;
    }

    void GameServiceContainer::RemoveService(const std::type_info& type)
    {
        services_.erase(std::type_index(type));
    }
}
