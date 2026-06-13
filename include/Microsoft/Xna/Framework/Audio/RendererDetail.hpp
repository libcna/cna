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

        /**
         * @brief Returns a string representation of this renderer detail.
         *
         * @return String containing the friendly name and renderer ID.
         */
        [[nodiscard]] std::string ToString() const;

        /** @brief Returns whether two renderer details are equal. */
        bool operator==(const RendererDetail& other) const;

        /** @brief Returns whether two renderer details are not equal. */
        bool operator!=(const RendererDetail& other) const;

        /**
         * @brief Constructs a RendererDetail with the given display name and ID.
         *
         * @param friendlyName Human-readable renderer name.
         * @param rendererId   Unique renderer identifier.
         */
        RendererDetail(std::string friendlyName, std::string rendererId);

    private:
        std::string friendlyName_;
        std::string rendererId_;
    };
}
