// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /** @brief Provides initialization behavior for a game component. */
    class IGameComponent
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IGameComponent() = default;

        /** @brief Initializes the component. */
        virtual void Initialize() = 0;
    };
}
