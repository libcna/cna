// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief The output of a font processor: a rasterized sprite font, ready to be written.
     *
     * XNA exposes nothing on this type -- measured, `fontprocessor/spritefont_content_members`
     * lists no public member of its own -- so what it holds is reachable here only through a
     * CNAEXT accessor, and what it holds is the canonical sprite-font data this repository
     * already writes.
     */
    class SpriteFontContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.SpriteFontContent";

        /** @brief Initializes an empty sprite font. */
        SpriteFontContent() = default;

        /**
         * @brief Initializes a sprite font holding the given rasterized data.
         *
         * @param data The canonical sprite-font data.
         */
        CNAEXT explicit SpriteFontContent(CNA::Content::Cnb::CnbSpriteFontData data) : data_(std::move(data)) {}

        /**
         * @brief Gets the rasterized font: its atlas, glyphs, cropping, kerning and spacing.
         *
         * @return The canonical sprite-font data.
         */
        CNAEXT [[nodiscard]] const CNA::Content::Cnb::CnbSpriteFontData& Data() const noexcept { return data_; }

        /**
         * @brief Gets the rasterized font for modification.
         *
         * @return The canonical sprite-font data.
         */
        CNAEXT [[nodiscard]] CNA::Content::Cnb::CnbSpriteFontData& Data() noexcept { return data_; }

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        CNA::Content::Cnb::CnbSpriteFontData data_;
    };
}
