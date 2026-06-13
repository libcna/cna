// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Provides event data for the GraphicsDevice.ResourceCreated event. */
    class ResourceCreatedEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Constructs a ResourceCreatedEventArgs with the given resource pointer.
         * @param resource Pointer to the newly created resource object (default nullptr).
         */
        explicit ResourceCreatedEventArgs(void* resource = nullptr) : resource_(resource) {}

        /** @brief Returns a pointer to the newly created resource object. */
        [[nodiscard]] void* getResourceProperty() const { return resource_; }

    private:
        void* resource_;
    };
}
