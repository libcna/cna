// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    /// Describes an available audio renderer device.
    struct RendererDetail
    {
        /// Returns the human-readable name of this renderer.
        [[nodiscard]] const std::string& getFriendlyNameProperty() const;

        /// Returns the unique identifier of this renderer.
        [[nodiscard]] const std::string& getRendererIdProperty() const;

        [[nodiscard]] std::string ToString() const;

        bool operator==(const RendererDetail& other) const;
        bool operator!=(const RendererDetail& other) const;

        /// Internal constructor.
        RendererDetail(std::string friendlyName, std::string rendererId);

    private:
        std::string friendlyName_;
        std::string rendererId_;
    };
}
