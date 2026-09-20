// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
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
         * @brief Returns a hash code combining the friendly name and the renderer ID.
         *
         * @return The XOR of the two fields' hashes, with an empty field contributing 0, as XNA
         *         combines them. Consistent with Equals and operator==.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Compares this RendererDetail with a boxed object for equality.
         *
         * Mirrors the CLR `Equals(object)` contract: an empty object, or an object holding a
         * different type, is unequal; otherwise the comparison is the typed one below.
         *
         * @param obj The boxed object to compare against.
         * @return @c true if @p obj holds an equal RendererDetail; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const;

        /**
         * @brief Returns whether this renderer detail describes the same renderer as another.
         *
         * @param other The renderer detail to compare against.
         * @return true if both the FriendlyName and the RendererId match; otherwise false.
         */
        [[nodiscard]] bool Equals(const RendererDetail& other) const;

        /**
         * @brief Returns whether two renderer details describe the same renderer.
         *
         * @param other The renderer detail to compare against.
         * @return true if both the FriendlyName and the RendererId match; otherwise false.
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
