// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"

#include <algorithm>
#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/DxtBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        /** @brief Reads an element through the const indexer, which yields the stored pointer itself
         *  rather than the mutable proxy the non-const indexer returns. */
        template<typename Collection>
        const auto& At(const Collection& collection, SharpRuntime::intcs index)
        {
            return collection[index];
        }

        std::string Size(const BitmapContent& bitmap)
        {
            return std::to_string(bitmap.getWidthProperty()) + "x" + std::to_string(bitmap.getHeightProperty());
        }

        SharpRuntime::intcs Half(SharpRuntime::intcs value) { return std::max<SharpRuntime::intcs>(1, value / 2); }

        std::string FormatName(SurfaceFormat format)
        {
            switch (format)
            {
            case SurfaceFormat::Color: return "Color";
            case SurfaceFormat::Bgr565: return "Bgr565";
            case SurfaceFormat::Bgra5551: return "Bgra5551";
            case SurfaceFormat::Bgra4444: return "Bgra4444";
            case SurfaceFormat::Dxt1: return "Dxt1";
            case SurfaceFormat::Dxt3: return "Dxt3";
            case SurfaceFormat::Dxt5: return "Dxt5";
            case SurfaceFormat::NormalizedByte2: return "NormalizedByte2";
            case SurfaceFormat::NormalizedByte4: return "NormalizedByte4";
            case SurfaceFormat::Rgba1010102: return "Rgba1010102";
            case SurfaceFormat::Rg32: return "Rg32";
            case SurfaceFormat::Rgba64: return "Rgba64";
            case SurfaceFormat::Alpha8: return "Alpha8";
            case SurfaceFormat::Single: return "Single";
            case SurfaceFormat::Vector2: return "Vector2";
            case SurfaceFormat::Vector4: return "Vector4";
            case SurfaceFormat::HalfSingle: return "HalfSingle";
            case SurfaceFormat::HalfVector2: return "HalfVector2";
            case SurfaceFormat::HalfVector4: return "HalfVector4";
            case SurfaceFormat::HdrBlendable: return "HdrBlendable";
            default: return "Unknown";
            }
        }

        /** @brief The surface formats the Reach profile can hold in a texture. */
        bool ReachSupports(SurfaceFormat format)
        {
            switch (format)
            {
            case SurfaceFormat::Color:
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
                return true;
            default:
                return false;
            }
        }

        bool IsDxt(const BitmapContent& bitmap) { return dynamic_cast<const DxtBitmapContent*>(&bitmap) != nullptr; }

        std::shared_ptr<BitmapContent> SameTypeAs(const BitmapContent& prototype, SharpRuntime::intcs width,
                                                  SharpRuntime::intcs height)
        {
            return BitmapContent::CreateBitmap(System::Type::FromTypeInfo(typeid(prototype)), width, height);
        }
    }

    TextureContent::TextureContent(std::shared_ptr<MipmapChainCollection> faces) : faces_(std::move(faces)) {}

    MipmapChainCollection& TextureContent::getFacesProperty() noexcept { return *faces_; }

    const MipmapChainCollection& TextureContent::getFacesProperty() const noexcept { return *faces_; }

    const std::string& TextureContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    void TextureContent::ConvertBitmapType(System::Type newBitmapType)
    {
        if (newBitmapType.getTypeInfo() == nullptr)
        {
            throw System::ArgumentNullException("newBitmapType");
        }
        for (SharpRuntime::intcs face = 0; face < faces_->getCountProperty(); ++face)
        {
            MipmapChain& chain = *At(*faces_, face);
            for (SharpRuntime::intcs level = 0; level < chain.getCountProperty(); ++level)
            {
                const std::shared_ptr<BitmapContent>& bitmap = At(chain, level);
                if (System::Type::FromTypeInfo(typeid(*bitmap)) == newBitmapType)
                {
                    continue;
                }
                std::shared_ptr<BitmapContent> converted =
                    BitmapContent::CreateBitmap(newBitmapType, bitmap->getWidthProperty(), bitmap->getHeightProperty());
                BitmapContent::Copy(bitmap, converted);
                chain.setItem(level, converted);
            }
        }
    }

    void TextureContent::GenerateChain(MipmapChain& chain, bool overwriteExistingMipmaps)
    {
        if (chain.getCountProperty() == 0)
        {
            return;
        }
        if (chain.getCountProperty() > 1 && !overwriteExistingMipmaps)
        {
            return;
        }
        const std::shared_ptr<BitmapContent> base = At(chain, 0);
        while (chain.getCountProperty() > 1)
        {
            chain.RemoveAt(chain.getCountProperty() - 1);
        }
        SharpRuntime::intcs width = base->getWidthProperty();
        SharpRuntime::intcs height = base->getHeightProperty();
        std::shared_ptr<BitmapContent> previous = base;
        while (width > 1 || height > 1)
        {
            width = Half(width);
            height = Half(height);
            std::shared_ptr<BitmapContent> level = SameTypeAs(*base, width, height);
            BitmapContent::Copy(previous, level);
            chain.Add(level);
            previous = level;
        }
    }

    void TextureContent::GenerateMipmaps(bool overwriteExistingMipmaps)
    {
        for (SharpRuntime::intcs face = 0; face < faces_->getCountProperty(); ++face)
        {
            GenerateChain(*At(*faces_, face), overwriteExistingMipmaps);
        }
    }

    void TextureContent::ValidateFaces(std::optional<GraphicsProfile> targetProfile, bool requireSquare,
                                       bool requireSameFaceSize) const
    {
        const auto faceCount = faces_->getCountProperty();
        for (SharpRuntime::intcs face = 0; face < faceCount; ++face)
        {
            if (At(*faces_, face)->getCountProperty() == 0)
            {
                throw InvalidContentException("Invalid texture. Face " + std::to_string(face) +
                                              " does not contain any mipmaps.");
            }
        }
        const BitmapContent& first = *At(*At(*faces_, 0), 0);
        if (requireSquare && first.getWidthProperty() != first.getHeightProperty())
        {
            throw InvalidContentException("Invalid texture. The cubemap is not square.");
        }
        for (SharpRuntime::intcs face = 0; face < faceCount; ++face)
        {
            const MipmapChain& chain = *At(*faces_, face);
            SharpRuntime::intcs expectedWidth = first.getWidthProperty();
            SharpRuntime::intcs expectedHeight = first.getHeightProperty();
            if (!requireSameFaceSize)
            {
                expectedWidth = At(chain, 0)->getWidthProperty();
                expectedHeight = At(chain, 0)->getHeightProperty();
            }
            for (SharpRuntime::intcs level = 0; level < chain.getCountProperty(); ++level)
            {
                const BitmapContent& bitmap = *At(chain, level);
                if (bitmap.getWidthProperty() != expectedWidth || bitmap.getHeightProperty() != expectedHeight)
                {
                    throw InvalidContentException("Invalid texture. Face " + std::to_string(face) + " mip " +
                                                  std::to_string(level) + " is sized " + Size(bitmap) + ", but should be " +
                                                  std::to_string(expectedWidth) + "x" + std::to_string(expectedHeight) + ".");
                }
                if (typeid(bitmap) != typeid(first))
                {
                    throw InvalidContentException("Invalid texture. Bitmap type mismatch: face " + std::to_string(face) +
                                                  " mip " + std::to_string(level) + " is " + bitmap.ToString() +
                                                  ", but face 0 mip 0 is " + first.ToString() + ".");
                }
                expectedWidth = Half(expectedWidth);
                expectedHeight = Half(expectedHeight);
            }
            const BitmapContent& top = *At(chain, 0);
            if (IsDxt(top) && (top.getWidthProperty() % 4 != 0 || top.getHeightProperty() % 4 != 0))
            {
                throw InvalidContentException("Invalid texture. Face " + std::to_string(face) + " is sized " + Size(top) +
                                              ", but textures using DXT compressed formats must be multiples of four.");
            }
        }
        if (!targetProfile.has_value())
        {
            return;
        }
        const std::string profileName = *targetProfile == GraphicsProfile::Reach ? "Reach" : "HiDef";
        const SharpRuntime::intcs limit = ProfileSizeLimit(*targetProfile);
        if (first.getWidthProperty() > limit || first.getHeightProperty() > limit)
        {
            throw InvalidContentException("XNA Framework " + profileName + " profile supports a maximum " +
                                          ProfileTypeName() + " size of " + std::to_string(limit) + ", but this " +
                                          ProfileTypeName() + " is " + Size(first) + ".");
        }
        SurfaceFormat format{};
        if (*targetProfile == GraphicsProfile::Reach && first.TryGetFormat(format) && !ReachSupports(format))
        {
            throw InvalidContentException("XNA Framework Reach profile does not support " + ProfileTypeName() +
                                          " format " + FormatName(format) + ".");
        }
    }

    void TextureContent::Validate(std::optional<GraphicsProfile> targetProfile)
    {
        ValidateFaces(targetProfile, false, false);
    }

    // ---- Texture2DContent ----------------------------------------------------------------------
    Texture2DContent::Texture2DContent() : TextureContent(std::make_shared<MipmapChainCollection>(1, true)) {}

    MipmapChain& Texture2DContent::getMipmapsProperty() noexcept { return *At(getFacesProperty(), 0); }

    const MipmapChain& Texture2DContent::getMipmapsProperty() const noexcept { return *At(getFacesProperty(), 0); }

    void Texture2DContent::setMipmapsProperty(std::shared_ptr<MipmapChain> value)
    {
        getFacesProperty().setItem(0, std::move(value));
    }

    void Texture2DContent::Validate(std::optional<GraphicsProfile> targetProfile)
    {
        ValidateFaces(targetProfile, false, true);
    }

    const std::string& Texture2DContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string Texture2DContent::ProfileTypeName() const { return "Texture2D"; }

    SharpRuntime::intcs Texture2DContent::ProfileSizeLimit(GraphicsProfile targetProfile) const
    {
        return targetProfile == GraphicsProfile::Reach ? 2048 : 4096;
    }

    // ---- Texture3DContent ----------------------------------------------------------------------
    Texture3DContent::Texture3DContent() : TextureContent(std::make_shared<MipmapChainCollection>(0, false)) {}

    void Texture3DContent::GenerateMipmaps(bool overwriteExistingMipmaps)
    {
        MipmapChainCollection& faces = getFacesProperty();
        const SharpRuntime::intcs depth = faces.getCountProperty();
        if (depth == 0)
        {
            return;
        }
        // Every slice keeps only its first level, then level k is built for the slices that still
        // exist at that level (depth / 2^k, at least one) by averaging pairs of slices as well as
        // pixel quads.
        for (SharpRuntime::intcs slice = 0; slice < depth; ++slice)
        {
            MipmapChain& chain = *At(faces, slice);
            if (chain.getCountProperty() == 0)
            {
                return;
            }
            if (chain.getCountProperty() > 1 && !overwriteExistingMipmaps)
            {
                return;
            }
        }
        for (SharpRuntime::intcs slice = 0; slice < depth; ++slice)
        {
            MipmapChain& chain = *At(faces, slice);
            while (chain.getCountProperty() > 1)
            {
                chain.RemoveAt(chain.getCountProperty() - 1);
            }
        }
        const std::shared_ptr<BitmapContent> base = At(*At(faces, 0), 0);
        SharpRuntime::intcs width = base->getWidthProperty();
        SharpRuntime::intcs height = base->getHeightProperty();
        SharpRuntime::intcs slices = depth;
        SharpRuntime::intcs level = 0;
        while (width > 1 || height > 1 || slices > 1)
        {
            width = Half(width);
            height = Half(height);
            const SharpRuntime::intcs previousSlices = slices;
            slices = Half(slices);
            ++level;
            for (SharpRuntime::intcs slice = 0; slice < slices; ++slice)
            {
                auto target = std::make_shared<PixelBitmapContent<Vector4>>(width, height);
                const SharpRuntime::intcs firstSource = slice * 2;
                const SharpRuntime::intcs sourceCount = std::min<SharpRuntime::intcs>(2, previousSlices - firstSource);
                for (SharpRuntime::intcs s = 0; s < sourceCount; ++s)
                {
                    auto scaled = std::make_shared<PixelBitmapContent<Vector4>>(width, height);
                    BitmapContent::Copy(At(*At(faces, firstSource + s), level - 1), scaled);
                    for (SharpRuntime::intcs y = 0; y < height; ++y)
                    {
                        for (SharpRuntime::intcs x = 0; x < width; ++x)
                        {
                            target->SetPixel(x, y, target->GetPixel(x, y) + scaled->GetPixel(x, y) * (1.0f / sourceCount));
                        }
                    }
                }
                std::shared_ptr<BitmapContent> stored = SameTypeAs(*base, width, height);
                BitmapContent::Copy(target, stored);
                At(faces, slice)->Add(stored);
            }
        }
    }

    void Texture3DContent::Validate(std::optional<GraphicsProfile> targetProfile)
    {
        if (getFacesProperty().getCountProperty() == 0)
        {
            throw InvalidContentException("Invalid texture. The Faces collection is empty.");
        }
        if (targetProfile == GraphicsProfile::Reach)
        {
            throw InvalidContentException("XNA Framework Reach profile does not support Texture3D.");
        }
        ValidateFaces(targetProfile, false, true);
    }

    const std::string& Texture3DContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string Texture3DContent::ProfileTypeName() const { return "Texture3D"; }

    SharpRuntime::intcs Texture3DContent::ProfileSizeLimit(GraphicsProfile targetProfile) const
    {
        (void)targetProfile;
        return 256;
    }

    // ---- TextureCubeContent --------------------------------------------------------------------
    TextureCubeContent::TextureCubeContent() : TextureContent(std::make_shared<MipmapChainCollection>(6, true)) {}

    void TextureCubeContent::Validate(std::optional<GraphicsProfile> targetProfile)
    {
        ValidateFaces(targetProfile, true, true);
    }

    const std::string& TextureCubeContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string TextureCubeContent::ProfileTypeName() const { return "TextureCube"; }

    SharpRuntime::intcs TextureCubeContent::ProfileSizeLimit(GraphicsProfile targetProfile) const
    {
        return targetProfile == GraphicsProfile::Reach ? 512 : 4096;
    }
}
