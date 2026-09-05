// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/FontProcessors.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

#include "CNA/Content/Pipeline/SpriteFontContentPipeline.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    namespace
    {
        namespace Canonical = CNA::Content::Pipeline;

        /** @brief The description's own font file, or the system font of that name. */
        [[nodiscard]] std::filesystem::path ResolveFontFile(const Graphics::FontDescription& description)
        {
            const std::string& name = description.getFontNameProperty();
            const std::string& identity = description.getIdentityProperty().getSourceFilenameProperty();
            const std::filesystem::path directory =
                identity.empty() ? std::filesystem::path() : std::filesystem::path(identity).parent_path();
            std::error_code error;
            for (const std::string& candidate : {name, name + ".ttf", name + ".otf", name + ".ttc"})
            {
                const std::filesystem::path probe = directory / candidate;
                if (!candidate.empty() && std::filesystem::is_regular_file(probe, error))
                {
                    return probe;
                }
            }
            return Canonical::FindSystemFontFile(name);
        }

        /** @brief The canonical style of an XNA one; the two enumerations share their members. */
        [[nodiscard]] Canonical::FontDescriptionStyle CanonicalStyle(Graphics::FontDescriptionStyle style)
        {
            switch (style)
            {
            case Graphics::FontDescriptionStyle::Bold:
                return Canonical::FontDescriptionStyle::Bold;
            case Graphics::FontDescriptionStyle::Italic:
                return Canonical::FontDescriptionStyle::Italic;
            case Graphics::FontDescriptionStyle::Regular:
            default:
                break;
            }
            // XNA has no BoldItalic member at all; a combined value is whatever bits it carries.
            const auto bits = static_cast<SharpRuntime::intcs>(style);
            if ((bits & static_cast<SharpRuntime::intcs>(Graphics::FontDescriptionStyle::Bold)) != 0 &&
                (bits & static_cast<SharpRuntime::intcs>(Graphics::FontDescriptionStyle::Italic)) != 0)
            {
                return Canonical::FontDescriptionStyle::BoldItalic;
            }
            return Canonical::FontDescriptionStyle::Regular;
        }
    }

    std::shared_ptr<SpriteFontContent> FontDescriptionProcessor::Process(
        const std::shared_ptr<Graphics::FontDescription>& input, ContentProcessorContext& context)
    {
        (void)context;
        if (input == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
        if (input->getCharactersProperty().empty())
        {
            // The runtime's own words for a description that asks for nothing (measured,
            // fontprocessor/description_no_characters).
            throw InvalidContentException("Cannot build this font: there were no glyphs found to build.");
        }
        const std::filesystem::path fontFile = ResolveFontFile(*input);
        if (fontFile.empty())
        {
            throw InvalidContentException("The font family \"" + input->getFontNameProperty() +
                                          "\" could not be found. Please ensure the requested font is installed, "
                                          "and is a TrueType or OpenType font.");
        }
        Canonical::FontDescription description;
        description.fontName = input->getFontNameProperty();
        description.size = input->getSizeProperty();
        description.spacing = input->getSpacingProperty();
        description.useKerning = input->getUseKerningProperty();
        description.style = CanonicalStyle(input->getStyleProperty());
        description.defaultCharacter = input->getDefaultCharacterProperty();
        for (const Graphics::CharacterRegion& region : input->getCharacterRegionsProperty())
        {
            description.characterRegions.push_back(Canonical::FontCharacterRegion{region.Start, region.End});
        }
        description.resolvedFontFile = fontFile;
        std::vector<std::string> warnings;
        try
        {
            return std::make_shared<SpriteFontContent>(Canonical::RasterizeFontDescription(description, warnings));
        }
        catch (const std::exception& error)
        {
            throw InvalidContentException(std::string("Cannot build this font: ") + error.what());
        }
    }

    const std::string& FontDescriptionProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    SharpRuntime::charcs FontTextureProcessor::getFirstCharacterProperty() const noexcept { return firstCharacter_; }

    void FontTextureProcessor::setFirstCharacterProperty(SharpRuntime::charcs value) noexcept
    {
        firstCharacter_ = value;
    }

    bool FontTextureProcessor::getPremultiplyAlphaProperty() const noexcept { return premultiplyAlpha_; }

    void FontTextureProcessor::setPremultiplyAlphaProperty(bool value) noexcept { premultiplyAlpha_ = value; }

    TextureProcessorOutputFormat FontTextureProcessor::getTextureFormatProperty() const noexcept
    {
        return textureFormat_;
    }

    void FontTextureProcessor::setTextureFormatProperty(TextureProcessorOutputFormat value) noexcept
    {
        textureFormat_ = value;
    }

    SharpRuntime::charcs FontTextureProcessor::GetCharacterForIndex(SharpRuntime::intcs index) const
    {
        // Measured: the glyphs stand for consecutive characters from FirstCharacter
        // (fontprocessor/texture_character_for_index and /texture_first_character_set).
        return static_cast<SharpRuntime::charcs>(static_cast<SharpRuntime::intcs>(firstCharacter_) + index);
    }

    std::shared_ptr<SpriteFontContent> FontTextureProcessor::Process(
        const std::shared_ptr<Graphics::Texture2DContent>& input, ContentProcessorContext& context)
    {
        (void)context;
        if (input == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
        // The glyphs are the columns that are not the border colour, which is the colour of the
        // texture's top-left pixel. XNA's exact packing is not observable -- SpriteFontContent
        // exposes nothing -- so what is reproduced is the boundary: which columns hold glyphs, and
        // the refusal when none does (measured, fontprocessor/texture_strip and /texture_empty).
        std::shared_ptr<Graphics::PixelBitmapContent<Color>> pixels;
        if (input->getMipmapsProperty().getCountProperty() > 0)
        {
            const std::shared_ptr<Graphics::BitmapContent>& level =
                static_cast<const System::Collections::ObjectModel::Collection<
                    std::shared_ptr<Graphics::BitmapContent>>&>(input->getMipmapsProperty())[0];
            pixels = std::dynamic_pointer_cast<Graphics::PixelBitmapContent<Color>>(level);
            if (pixels == nullptr && level != nullptr)
            {
                pixels = std::make_shared<Graphics::PixelBitmapContent<Color>>(level->getWidthProperty(),
                                                                              level->getHeightProperty());
                Graphics::BitmapContent::Copy(level, pixels);
            }
        }
        if (pixels == nullptr || pixels->getWidthProperty() == 0 || pixels->getHeightProperty() == 0)
        {
            throw InvalidContentException("Cannot build this font: there were no glyphs found to build.");
        }
        const Color border = pixels->GetPixel(0, 0);
        std::vector<Rectangle> glyphs;
        SharpRuntime::intcs start = -1;
        for (SharpRuntime::intcs x = 0; x < pixels->getWidthProperty(); ++x)
        {
            bool columnHasGlyph = false;
            for (SharpRuntime::intcs y = 0; y < pixels->getHeightProperty() && !columnHasGlyph; ++y)
            {
                columnHasGlyph = pixels->GetPixel(x, y) != border;
            }
            if (columnHasGlyph && start < 0)
            {
                start = x;
            }
            else if (!columnHasGlyph && start >= 0)
            {
                glyphs.push_back(Rectangle(start, 0, x - start, pixels->getHeightProperty()));
                start = -1;
            }
        }
        if (start >= 0)
        {
            glyphs.push_back(
                Rectangle(start, 0, pixels->getWidthProperty() - start, pixels->getHeightProperty()));
        }
        if (glyphs.empty())
        {
            throw InvalidContentException("Cannot build this font: there were no glyphs found to build.");
        }
        CNA::Content::Cnb::CnbSpriteFontData data;
        data.lineSpacing = pixels->getHeightProperty();
        data.spacing = 0.0f;
        for (std::size_t i = 0; i < glyphs.size(); ++i)
        {
            data.glyphBounds.push_back(glyphs[i]);
            data.cropping.push_back(Rectangle(0, 0, glyphs[i].Width, glyphs[i].Height));
            data.kerning.push_back(Vector3(0.0f, static_cast<float>(glyphs[i].Width), 0.0f));
            data.characters.push_back(GetCharacterForIndex(static_cast<SharpRuntime::intcs>(i)));
        }
        return std::make_shared<SpriteFontContent>(std::move(data));
    }

    const std::string& FontTextureProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
