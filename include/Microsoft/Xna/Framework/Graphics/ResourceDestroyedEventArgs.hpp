#pragma once

#include <string>

#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Provides data for the GraphicsDevice.ResourceDestroyed event.
    class ResourceDestroyedEventArgs : public System::EventArgs
    {
    public:
        explicit ResourceDestroyedEventArgs(const std::string& name = {}, void* tag = nullptr)
            : name_(name), tag_(tag) {}

        /// Gets the name of the resource that was destroyed.
        [[nodiscard]] const std::string& getNameProperty() const { return name_; }

        /// Gets the tag associated with the destroyed resource.
        [[nodiscard]] void* getTagProperty() const { return tag_; }

    private:
        std::string name_;
        void* tag_;
    };
}
