// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp"

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/DxtBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    namespace
    {
        namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;

        /** @brief The next power of two at or above a dimension. */
        [[nodiscard]] SharpRuntime::intcs NextPowerOfTwo(SharpRuntime::intcs value)
        {
            SharpRuntime::intcs power = 1;
            while (power < value)
            {
                power *= 2;
            }
            return power;
        }

        /** @brief The bitmap as a Color bitmap, converting when it is another type. */
        [[nodiscard]] std::shared_ptr<Graphics::PixelBitmapContent<Color>> AsColor(
            const std::shared_ptr<Graphics::BitmapContent>& bitmap)
        {
            if (auto already = std::dynamic_pointer_cast<Graphics::PixelBitmapContent<Color>>(bitmap))
            {
                return already;
            }
            auto converted = std::make_shared<Graphics::PixelBitmapContent<Color>>(bitmap->getWidthProperty(),
                                                                                  bitmap->getHeightProperty());
            Graphics::BitmapContent::Copy(bitmap, converted);
            return converted;
        }

        /** @brief True when every pixel's alpha is 0 or 255, which Dxt1's one-bit alpha can carry. */
        [[nodiscard]] bool AlphaIsOneBit(const Graphics::PixelBitmapContent<Color>& bitmap)
        {
            for (SharpRuntime::intcs y = 0; y < bitmap.getHeightProperty(); ++y)
            {
                for (SharpRuntime::intcs x = 0; x < bitmap.getWidthProperty(); ++x)
                {
                    const SharpRuntime::bytecs alpha = bitmap.GetPixel(x, y).getAProperty();
                    if (alpha != 0 && alpha != 255)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    void TextureProcessor::DescribeParameters(ProcessorParameterBindings<TextureProcessor>& bindings)
    {
        bindings.Add<Color>("ColorKeyColor", &TextureProcessor::getColorKeyColorProperty,
                            &TextureProcessor::setColorKeyColorProperty);
        bindings.Add<bool>("ColorKeyEnabled", &TextureProcessor::getColorKeyEnabledProperty,
                           &TextureProcessor::setColorKeyEnabledProperty);
        bindings.Add<bool>("GenerateMipmaps", &TextureProcessor::getGenerateMipmapsProperty,
                           &TextureProcessor::setGenerateMipmapsProperty);
        bindings.Add<bool>("PremultiplyAlpha", &TextureProcessor::getPremultiplyAlphaProperty,
                           &TextureProcessor::setPremultiplyAlphaProperty);
        bindings.Add<bool>("ResizeToPowerOfTwo", &TextureProcessor::getResizeToPowerOfTwoProperty,
                           &TextureProcessor::setResizeToPowerOfTwoProperty);
        bindings.AddEnum<TextureProcessorOutputFormat>(
            "TextureFormat", &TextureProcessor::getTextureFormatProperty,
            &TextureProcessor::setTextureFormatProperty,
            DeclaredEnumSpellings<TextureProcessorOutputFormat>());
    }

    Color TextureProcessor::getColorKeyColorProperty() const noexcept { return colorKeyColor_; }

    void TextureProcessor::setColorKeyColorProperty(Color value) noexcept { colorKeyColor_ = value; }

    bool TextureProcessor::getColorKeyEnabledProperty() const noexcept { return colorKeyEnabled_; }

    void TextureProcessor::setColorKeyEnabledProperty(bool value) noexcept { colorKeyEnabled_ = value; }

    bool TextureProcessor::getGenerateMipmapsProperty() const noexcept { return generateMipmaps_; }

    void TextureProcessor::setGenerateMipmapsProperty(bool value) noexcept { generateMipmaps_ = value; }

    bool TextureProcessor::getPremultiplyAlphaProperty() const noexcept { return premultiplyAlpha_; }

    void TextureProcessor::setPremultiplyAlphaProperty(bool value) noexcept { premultiplyAlpha_ = value; }

    bool TextureProcessor::getResizeToPowerOfTwoProperty() const noexcept { return resizeToPowerOfTwo_; }

    void TextureProcessor::setResizeToPowerOfTwoProperty(bool value) noexcept { resizeToPowerOfTwo_ = value; }

    TextureProcessorOutputFormat TextureProcessor::getTextureFormatProperty() const noexcept
    {
        return textureFormat_;
    }

    void TextureProcessor::setTextureFormatProperty(TextureProcessorOutputFormat value) noexcept
    {
        textureFormat_ = value;
    }

    std::shared_ptr<Graphics::TextureContent> TextureProcessor::Process(
        const std::shared_ptr<Graphics::TextureContent>& input, ContentProcessorContext& context)
    {
        (void)context;
        if (input == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
        Graphics::MipmapChainCollection& faces = input->getFacesProperty();
        const bool needsColor = getColorKeyEnabledProperty() || getPremultiplyAlphaProperty() ||
                                getResizeToPowerOfTwoProperty() ||
                                getTextureFormatProperty() != TextureProcessorOutputFormat::NoChange;
        // With NoChange the texture keeps the bitmap type it arrived with, whatever the steps in
        // between needed (measured, tests/reference/xna40/graphics case
        // textureprocessor/no_change: a Bgr565 texture stays Bgr565).
        System::Type originalType = System::Type::From<Graphics::PixelBitmapContent<Color>>();
        bool sawBitmap = false;
        for (SharpRuntime::intcs face = 0; face < faces.getCountProperty(); ++face)
        {
            const std::shared_ptr<Graphics::MipmapChain>& chain =
                static_cast<const System::Collections::ObjectModel::Collection<std::shared_ptr<Graphics::MipmapChain>>&>(
                    faces)[face];
            if (chain == nullptr || chain->getCountProperty() == 0)
            {
                continue;
            }
            if (!sawBitmap)
            {
                originalType = System::Type::FromTypeInfo(typeid(*static_cast<
                    const System::Collections::ObjectModel::Collection<std::shared_ptr<Graphics::BitmapContent>>&>(
                        *chain)[0]));
                sawBitmap = true;
            }
            if (!needsColor)
            {
                continue;
            }
            // Every level, not only the first. A source that brings its own mipmaps -- a `.dds`
            // does -- keeps them, and each of them is keyed and premultiplied: measured over a
            // four-level `.dds` whose every level carries the colour key at (0,0) and a
            // half-transparent texel beside it, and XNA's four output levels each answer
            // transparent black and a premultiplied texel
            // (tests/reference/xna40/differential/texture_dds_mipchain_default.xnb,
            // plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-135`). CNA kept level 0 and dropped the
            // rest, so `Stripe2.dds` came out with one level against the reference's ten.
            for (SharpRuntime::intcs level = 0; level < chain->getCountProperty(); ++level)
            {
                std::shared_ptr<Graphics::BitmapContent> source =
                    static_cast<const System::Collections::ObjectModel::Collection<
                        std::shared_ptr<Graphics::BitmapContent>>&>(*chain)[level];
                std::shared_ptr<Graphics::PixelBitmapContent<Color>> pixels = AsColor(source);
                // The measured order: key the colour out, resize, then premultiply
                // (tests/reference/xna40/graphics, textureprocessor/color_key and /premultiply).
                if (getColorKeyEnabledProperty())
                {
                    pixels->ReplaceColor(getColorKeyColorProperty(), Color(0, 0, 0, 0));
                }
                if (getResizeToPowerOfTwoProperty())
                {
                    const SharpRuntime::intcs width = NextPowerOfTwo(pixels->getWidthProperty());
                    const SharpRuntime::intcs height = NextPowerOfTwo(pixels->getHeightProperty());
                    if (width != pixels->getWidthProperty() || height != pixels->getHeightProperty())
                    {
                        auto resized = std::make_shared<Graphics::PixelBitmapContent<Color>>(width, height);
                        Graphics::BitmapContent::Copy(pixels, resized);
                        pixels = resized;
                    }
                }
                if (getPremultiplyAlphaProperty())
                {
                    for (SharpRuntime::intcs y = 0; y < pixels->getHeightProperty(); ++y)
                    {
                        for (SharpRuntime::intcs x = 0; x < pixels->getWidthProperty(); ++x)
                        {
                            const Color pixel = pixels->GetPixel(x, y);
                            pixels->SetPixel(x, y, Color::FromNonPremultiplied(static_cast<intcs>(pixel.getRProperty()),
                                                                               static_cast<intcs>(pixel.getGProperty()),
                                                                               static_cast<intcs>(pixel.getBProperty()),
                                                                               static_cast<intcs>(pixel.getAProperty())));
                        }
                    }
                }
                chain->setItem(level, pixels);
            }
        }
        if (getGenerateMipmapsProperty())
        {
            // Not an overwrite: a source that already carries a chain keeps it, and asking for
            // mipmaps changes nothing. Measured -- the same four-level `.dds` built with
            // GenerateMipmaps=True is byte for byte the one built without it
            // (tests/reference/xna40/differential/texture_dds_mipchain_generate.xnb).
            input->GenerateMipmaps(false);
        }
        if (getTextureFormatProperty() == TextureProcessorOutputFormat::NoChange && sawBitmap)
        {
            input->ConvertBitmapType(originalType);
        }
        if (getTextureFormatProperty() == TextureProcessorOutputFormat::DxtCompressed)
        {
            // Dxt1 carries one bit of alpha, so it is enough unless a pixel is partly transparent
            // (measured, textureprocessor/dxt versus /dxt_opaque and /dxt_colorkeyed_opaque).
            bool oneBitAlpha = true;
            for (SharpRuntime::intcs face = 0; face < faces.getCountProperty() && oneBitAlpha; ++face)
            {
                const std::shared_ptr<Graphics::MipmapChain>& chain =
                    static_cast<const System::Collections::ObjectModel::Collection<
                        std::shared_ptr<Graphics::MipmapChain>>&>(faces)[face];
                for (SharpRuntime::intcs level = 0; level < chain->getCountProperty() && oneBitAlpha; ++level)
                {
                    const std::shared_ptr<Graphics::BitmapContent>& bitmap =
                        static_cast<const System::Collections::ObjectModel::Collection<
                            std::shared_ptr<Graphics::BitmapContent>>&>(*chain)[level];
                    oneBitAlpha = AlphaIsOneBit(*AsColor(bitmap));
                }
            }
            input->ConvertBitmapType(oneBitAlpha ? System::Type::From<Graphics::Dxt1BitmapContent>()
                                                 : System::Type::From<Graphics::Dxt5BitmapContent>());
        }
        return input;
    }

    const std::string& TextureProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    const std::string& SpriteTextureProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    ModelTextureProcessor::ModelTextureProcessor()
    {
        setGenerateMipmapsProperty(true);
        setTextureFormatProperty(TextureProcessorOutputFormat::DxtCompressed);
    }

    const std::string& ModelTextureProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
