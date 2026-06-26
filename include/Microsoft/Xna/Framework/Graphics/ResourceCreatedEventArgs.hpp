// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"

namespace System { class Object; }

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
        explicit ResourceCreatedEventArgs(System::Object* resource = nullptr)
            : resource_(resource) {}

        /** @brief Returns a pointer to the newly created resource object. */
        [[nodiscard]] System::Object* getResourceProperty() const { return resource_; }

    private:
        System::Object* resource_;
    };
}
