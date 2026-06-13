// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

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

        /** @brief Returns the friendly name of this renderer. */
        [[nodiscard]] std::string ToString() const;

        /** @brief Returns a hash code based on the renderer ID. */
        [[nodiscard]] int GetHashCode() const;

        /** @brief Returns whether two renderer details are equal. */
        bool operator==(const RendererDetail& other) const;

        /** @brief Returns whether two renderer details are not equal. */
        bool operator!=(const RendererDetail& other) const;

    private:
        friend class AudioEngine;

        RendererDetail(std::string friendlyName, std::string rendererId);

        std::string friendlyName_;
        std::string rendererId_;
    };
}
