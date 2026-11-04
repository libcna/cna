// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp"

#include <memory>
#include <optional>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/DxtBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    namespace
    {
        namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
        namespace Packed = Microsoft::Xna::Framework::Graphics::PackedVector;
        using ChainCollection = System::Collections::ObjectModel::Collection<std::shared_ptr<Graphics::MipmapChain>>;
        using BitmapCollection = System::Collections::ObjectModel::Collection<std::shared_ptr<Graphics::BitmapContent>>;

        /** @brief The next power of two at or above a dimension (XNA's RoundUpToPowerOfTwo). */
        [[nodiscard]] SharpRuntime::intcs NextPowerOfTwo(SharpRuntime::intcs value)
        {
            SharpRuntime::intcs power = 1;
            while (power < value)
            {
                power *= 2;
            }
            return power;
        }

        /**
         * @brief Whether XNA keys and premultiplies a bitmap of this type in Vector4 rather than
         *        Color (TextureProcessorUtils.IsHighPrecisionFormat IL_0000-00d2).
         */
        [[nodiscard]] bool IsHighPrecisionFormat(const System::Type& type)
        {
            return type == System::Type::From<Graphics::PixelBitmapContent<float>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Vector2>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Vector4>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Packed::HalfSingle>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Packed::HalfVector2>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Packed::HalfVector4>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Packed::NormalizedByte2>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Packed::NormalizedByte4>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Packed::Rgba1010102>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Packed::Rg32>>() ||
                   type == System::Type::From<Graphics::PixelBitmapContent<Packed::Rgba64>>();
        }

        /** @brief Every bitmap of every face, in face order then level order. */
        template<typename Visit>
        void ForEachBitmap(Graphics::TextureContent& texture, Visit&& visit)
        {
            const ChainCollection& faces = texture.getFacesProperty();
            for (SharpRuntime::intcs face = 0; face < faces.getCountProperty(); ++face)
            {
                if (faces[face] == nullptr)
                {
                    continue;
                }
                Graphics::MipmapChain& chain = *faces[face];
                for (SharpRuntime::intcs level = 0; level < chain.getCountProperty(); ++level)
                {
                    visit(chain, level, static_cast<const BitmapCollection&>(chain)[level]);
                }
            }
        }

        /** @brief True when some pixel's alpha is neither 0 nor 255 (HasFractionalAlpha IL_0000-00b4). */
        [[nodiscard]] bool HasFractionalAlpha(Graphics::TextureContent& texture)
        {
            bool fractional = false;
            ForEachBitmap(texture, [&fractional](Graphics::MipmapChain&, SharpRuntime::intcs,
                                                 const std::shared_ptr<Graphics::BitmapContent>& bitmap)
            {
                const auto& pixels = dynamic_cast<const Graphics::PixelBitmapContent<Color>&>(*bitmap);
                for (SharpRuntime::intcs y = 0; y < pixels.getHeightProperty() && !fractional; ++y)
                {
                    for (SharpRuntime::intcs x = 0; x < pixels.getWidthProperty() && !fractional; ++x)
                    {
                        const SharpRuntime::bytecs alpha = pixels.GetPixel(x, y).getAProperty();
                        fractional = alpha != 0 && alpha != 255;
                    }
                }
            });
            return fractional;
        }

        /**
         * @brief A bitmap of the same type at another size (TextureContent.ConvertBitmap with the
         *        bitmap's own type): resampled into Vector4 at the new size, then converted, which is
         *        the order BitmapContent.Copy's own fallback takes (IL_0021-005b).
         */
        [[nodiscard]] std::shared_ptr<Graphics::BitmapContent> Resized(
            const std::shared_ptr<Graphics::BitmapContent>& bitmap, SharpRuntime::intcs width, SharpRuntime::intcs height)
        {
            const System::Type type = System::Type::FromTypeInfo(typeid(*bitmap));
            auto resampled = std::make_shared<Graphics::PixelBitmapContent<Vector4>>(width, height);
            Graphics::BitmapContent::Copy(bitmap, resampled);
            if (type == System::Type::From<Graphics::PixelBitmapContent<Vector4>>())
            {
                return resampled;
            }
            std::shared_ptr<Graphics::BitmapContent> converted = Graphics::BitmapContent::CreateBitmap(type, width, height);
            Graphics::BitmapContent::Copy(resampled, converted);
            return converted;
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
        // XNA's TextureProcessor.Process, step for step (TQ-034): colour key, premultiply, resize,
        // mipmaps, format (IL_0081-00be). The texture is validated and converted only when it is to
        // be keyed or premultiplied (IL_002c-007c), to Vector4 when its own type is a
        // high-precision one so a float source keeps its floats, to Color otherwise; that first
        // type is what NoChange restores (ChangeTextureToRequestedFormat IL_0015-0020).
        // Premultiplying before the resize matters: averaging straight colours with a transparent
        // neighbour's pushes them toward that neighbour's colour (measured, TQ-034 lane oracle
        // texture/order_premultiply_then_resize, which equals resizing an already premultiplied
        // source). With NoChange a texture keeps the bitmap type it arrived with
        // (tests/reference/xna40/graphics, textureprocessor/no_change: Bgr565 stays Bgr565), and a
        // source that brings its own mipmaps keeps and keys every one of them
        // (tests/reference/xna40/differential/texture_dds_mipchain_default.xnb, XNASWEEP-135).
        const bool colorKey = getColorKeyEnabledProperty();
        const bool premultiply = getPremultiplyAlphaProperty();
        std::optional<System::Type> originalType;
        if (colorKey || premultiply)
        {
            input->Validate(std::nullopt);
            const ChainCollection& faces = input->getFacesProperty();
            originalType = System::Type::FromTypeInfo(typeid(*static_cast<const BitmapCollection&>(*faces[0])[0]));
            input->ConvertBitmapType(IsHighPrecisionFormat(*originalType)
                                         ? System::Type::From<Graphics::PixelBitmapContent<Vector4>>()
                                         : System::Type::From<Graphics::PixelBitmapContent<Color>>());
        }
        if (colorKey)
        {
            // TextureProcessorUtils.ColorKey IL_0027-005d: an exact match, against the key's own
            // Vector4 for a float texture -- so (0.999, 0, 1, 1) is not keyed by magenta.
            const Color key = getColorKeyColorProperty();
            ForEachBitmap(*input, [&key](Graphics::MipmapChain&, SharpRuntime::intcs,
                                         const std::shared_ptr<Graphics::BitmapContent>& bitmap)
            {
                if (auto colours = std::dynamic_pointer_cast<Graphics::PixelBitmapContent<Color>>(bitmap))
                {
                    colours->ReplaceColor(key, Color(0, 0, 0, 0));
                }
                else if (auto vectors = std::dynamic_pointer_cast<Graphics::PixelBitmapContent<Vector4>>(bitmap))
                {
                    vectors->ReplaceColor(key.ToVector4(), Vector4(0, 0, 0, 0));
                }
            });
        }
        if (premultiply)
        {
            // TextureProcessorUtils.PremultiplyAlpha: bytes below alpha 255 through
            // Color.FromNonPremultiplied (IL_0045-0099); floats below alpha 1 scaled by it, so an
            // alpha above one -- or NaN -- leaves the pixel alone (IL_00d2-014b).
            ForEachBitmap(*input, [](Graphics::MipmapChain&, SharpRuntime::intcs,
                                     const std::shared_ptr<Graphics::BitmapContent>& bitmap)
            {
                if (auto colours = std::dynamic_pointer_cast<Graphics::PixelBitmapContent<Color>>(bitmap))
                {
                    for (SharpRuntime::intcs y = 0; y < colours->getHeightProperty(); ++y)
                    {
                        for (SharpRuntime::intcs x = 0; x < colours->getWidthProperty(); ++x)
                        {
                            const Color pixel = colours->GetPixel(x, y);
                            if (pixel.getAProperty() < 255)
                            {
                                colours->SetPixel(x, y, Color::FromNonPremultiplied(
                                                            static_cast<intcs>(pixel.getRProperty()),
                                                            static_cast<intcs>(pixel.getGProperty()),
                                                            static_cast<intcs>(pixel.getBProperty()),
                                                            static_cast<intcs>(pixel.getAProperty())));
                            }
                        }
                    }
                }
                else if (auto vectors = std::dynamic_pointer_cast<Graphics::PixelBitmapContent<Vector4>>(bitmap))
                {
                    for (SharpRuntime::intcs y = 0; y < vectors->getHeightProperty(); ++y)
                    {
                        for (SharpRuntime::intcs x = 0; x < vectors->getWidthProperty(); ++x)
                        {
                            const Vector4 pixel = vectors->GetPixel(x, y);
                            if (pixel.W < 1.0f)
                            {
                                vectors->SetPixel(x, y, Vector4(pixel.X * pixel.W, pixel.Y * pixel.W,
                                                                pixel.Z * pixel.W, pixel.W));
                            }
                        }
                    }
                }
            });
        }
        if (getResizeToPowerOfTwoProperty())
        {
            // TextureProcessorUtils.ResizeToPowerOfTwo IL_0019-0061: every level on its own, kept in
            // its own bitmap type.
            ForEachBitmap(*input, [](Graphics::MipmapChain& chain, SharpRuntime::intcs level,
                                     const std::shared_ptr<Graphics::BitmapContent>& bitmap)
            {
                const SharpRuntime::intcs width = NextPowerOfTwo(bitmap->getWidthProperty());
                const SharpRuntime::intcs height = NextPowerOfTwo(bitmap->getHeightProperty());
                if (width != bitmap->getWidthProperty() || height != bitmap->getHeightProperty())
                {
                    chain.setItem(level, Resized(bitmap, width, height));
                }
            });
        }
        if (getGenerateMipmapsProperty())
        {
            // Not an overwrite: a source that already carries a chain keeps it (measured, the
            // four-level `.dds` built with GenerateMipmaps=True is byte for byte the one built
            // without it, tests/reference/xna40/differential/texture_dds_mipchain_generate.xnb).
            input->GenerateMipmaps(false);
        }
        switch (getTextureFormatProperty())
        {
            case TextureProcessorOutputFormat::NoChange:
                if (originalType.has_value())
                {
                    input->ConvertBitmapType(*originalType);
                }
                break;
            case TextureProcessorOutputFormat::Color:
                input->ConvertBitmapType(System::Type::From<Graphics::PixelBitmapContent<Color>>());
                break;
            case TextureProcessorOutputFormat::DxtCompressed:
                // TextureProcessorUtils.BestGuessCompress IL_0000-0041: Color first; a volume stays
                // Color (IL_0010-0016, measured: texture/volume_dxt_stays_color); otherwise Dxt1
                // carries one bit of alpha, so it is enough unless a pixel is partly transparent
                // (measured, textureprocessor/dxt versus /dxt_opaque and /dxt_colorkeyed_opaque).
                input->ConvertBitmapType(System::Type::From<Graphics::PixelBitmapContent<Color>>());
                if (std::dynamic_pointer_cast<Graphics::Texture3DContent>(input) == nullptr)
                {
                    input->ConvertBitmapType(HasFractionalAlpha(*input)
                                                 ? System::Type::From<Graphics::Dxt5BitmapContent>()
                                                 : System::Type::From<Graphics::Dxt1BitmapContent>());
                }
                break;
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
