// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Internal::Graphics
{
    /** @brief Tracks the public wrapper independently of a queued sprite's retained renderer. */
    class SpriteTextureBinding
    {
    public:
        /** @brief Creates an empty binding for renderer-only callers. */
        SpriteTextureBinding() = default;
        /**
         * @brief Captures a device and a movable, invalidatable texture identity.
         * @param device Device whose public slot is updated, or null for renderer-only callers.
         * @param identity Live wrapper identity, invalidated when the wrapper is destroyed.
         */
        SpriteTextureBinding(Microsoft::Xna::Framework::Graphics::GraphicsDevice* device,
            std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D*> identity);
        /** @brief Publishes the live wrapper through the normal validated texture setter. */
        void Apply() const;
        /**
         * @brief Compares queued public identities, not shared native texture handles.
         * @param other Binding to compare.
         * @return True when both bindings name the same device and wrapper identity.
         */
        bool operator==(const SpriteTextureBinding& other) const = default;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device_ = nullptr;
        std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D*> identity_;
    };
}
