// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Provides event data for the GraphicsDevice.ResourceDestroyed event. */
    class ResourceDestroyedEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Constructs a ResourceDestroyedEventArgs with the given name and tag.
         * @param name The name of the destroyed resource.
         * @param tag  The user tag associated with the destroyed resource.
         */
        explicit ResourceDestroyedEventArgs(const std::string& name = {}, void* tag = nullptr)
            : name_(name), tag_(tag) {}

        /** @brief Returns the name of the resource that was destroyed. */
        [[nodiscard]] const std::string& getNameProperty() const { return name_; }

        /** @brief Returns the user tag associated with the destroyed resource. */
        [[nodiscard]] void* getTagProperty() const { return tag_; }

    private:
        std::string name_;
        void* tag_;
    };
}
