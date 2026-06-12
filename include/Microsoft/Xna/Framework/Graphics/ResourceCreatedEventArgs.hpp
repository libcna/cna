// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Provides data for the GraphicsDevice.ResourceCreated event.
    class ResourceCreatedEventArgs : public System::EventArgs
    {
    public:
        explicit ResourceCreatedEventArgs(void* resource = nullptr) : resource_(resource) {}

        /// Gets the resource that was created.
        [[nodiscard]] void* getResourceProperty() const { return resource_; }

    private:
        void* resource_;
    };
}
