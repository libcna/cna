// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Describes an available audio renderer device. */
    struct RendererDetail
    {
        /**
         * @brief Gets the human-readable display name of this renderer.
         *
         * @return Friendly name string.
         */
        [[nodiscard]] const std::string& getFriendlyNameProperty() const;

        /**
         * @brief Gets the unique identifier string of this renderer.
         *
         * @return Renderer ID string.
         */
        [[nodiscard]] const std::string& getRendererIdProperty() const;

        /**
         * @brief Returns the friendly name of this renderer.
         *
         * @return The value of the FriendlyName property.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code based on the renderer ID.
         *
         * @return Hash code consistent with Equals and operator==.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns whether this renderer detail has the same renderer ID as another.
         *
         * @param other The renderer detail to compare against.
         * @return true if both have the same RendererId; otherwise false.
         */
        [[nodiscard]] bool Equals(const RendererDetail& other) const;

        /**
         * @brief Returns whether two renderer details have the same renderer ID.
         *
         * @param other The renderer detail to compare against.
         * @return true if both have the same RendererId; otherwise false.
         */
        bool operator==(const RendererDetail& other) const;

        /**
         * @brief Returns whether two renderer details have different renderer IDs.
         *
         * @param other The renderer detail to compare against.
         * @return true if the RendererId values differ; otherwise false.
         */
        bool operator!=(const RendererDetail& other) const;

    private:
        friend class AudioEngine;
        // Production code only ever constructs the single hardcoded SDL3_mixer renderer
        // (see AudioEngine::Init); tests need a distinct second instance to exercise the
        // unequal-comparison paths of Equals/operator==/operator!=.
        CNAEXT friend struct RendererDetailTestAccess;

        RendererDetail(std::string friendlyName, std::string rendererId);

        std::string friendlyName_;
        std::string rendererId_;
    };
}
