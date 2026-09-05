// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MipmapChainCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides a base class for all texture objects.
     */
    class TextureContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent";

        /**
         * @brief Collection of image faces that hold a single mipmap chain each; a 2D texture has
         *        one face, a cube six, a 3D texture one per depth slice.
         *
         * @return The faces.
         */
        [[nodiscard]] MipmapChainCollection& getFacesProperty() noexcept;

        /** @brief Const access to the faces. */
        [[nodiscard]] const MipmapChainCollection& getFacesProperty() const noexcept;

        /**
         * @brief Converts all bitmaps for this texture to a different format.
         *
         * @param newBitmapType Type being converted to; must be a registered concrete bitmap class
         *        (`PixelBitmapContent<T>`, `Dxt1/3/5BitmapContent`).
         * @throws System::ArgumentNullException for no type.
         * @throws System::ArgumentException for a type that is not a bitmap class.
         */
        void ConvertBitmapType(System::Type newBitmapType);

        /**
         * @brief Converts all bitmaps for this texture to a different format.
         *
         * @tparam TBitmap The bitmap class to convert to.
         */
        template<typename TBitmap>
        CNAEXT void ConvertBitmapType()
        {
            ConvertBitmapType(System::Type::From<TBitmap>());
        }

        /**
         * @brief Generates a full set of mipmaps for the texture.
         *
         * @param overwriteExistingMipmaps True to regenerate a chain that already has more than one
         *        level; false leaves such a chain as it is.
         */
        virtual void GenerateMipmaps(bool overwriteExistingMipmaps);

        /**
         * @brief Verifies that all contents of this texture are present, correct and match the
         *        capabilities of the device.
         *
         * @param targetProfile The target graphics profile, or none to skip profile checks.
         * @throws InvalidContentException with XNA's message for the first defect found.
         */
        virtual void Validate(std::optional<Microsoft::Xna::Framework::Graphics::GraphicsProfile> targetProfile);

        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /**
         * @brief Initializes a new instance of TextureContent with the specified face collection.
         *
         * @param faces Mipmap chain containing the face collection.
         */
        explicit TextureContent(std::shared_ptr<MipmapChainCollection> faces);

        /**
         * @brief The name the profile messages use for this texture type (`Texture2D`, `TextureCube`,
         *        `Texture3D`).
         *
         * @return The name.
         */
        CNAEXT [[nodiscard]] virtual std::string ProfileTypeName() const = 0;

        /**
         * @brief The largest face dimension the profile allows for this texture type.
         *
         * @param targetProfile The profile.
         * @return The limit in pixels.
         */
        CNAEXT [[nodiscard]] virtual SharpRuntime::intcs ProfileSizeLimit(
            Microsoft::Xna::Framework::Graphics::GraphicsProfile targetProfile) const = 0;

        /**
         * @brief Validates the faces' shapes: every face has a mipmap, every chain halves correctly
         *        and keeps one bitmap type, DXT faces are multiples of four, and the profile's size
         *        and format limits hold.
         *
         * @param targetProfile The profile, or none.
         * @param requireSquare True for a cube map.
         * @param requireSameFaceSize True when every face must be sized like face 0.
         */
        CNAEXT void ValidateFaces(std::optional<Microsoft::Xna::Framework::Graphics::GraphicsProfile> targetProfile,
                                  bool requireSquare, bool requireSameFaceSize) const;

        /**
         * @brief Regenerates one chain from its first level, halving until 1x1.
         *
         * @param chain The chain.
         * @param overwriteExistingMipmaps As for `GenerateMipmaps`.
         */
        CNAEXT static void GenerateChain(MipmapChain& chain, bool overwriteExistingMipmaps);

    private:
        std::shared_ptr<MipmapChainCollection> faces_;
    };

    /** @brief Provides properties for maintaining a 2D texture. */
    class Texture2DContent : public TextureContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent";

        /** @brief Initializes a new instance of Texture2DContent: one face, fixed. */
        Texture2DContent();

        /**
         * @brief Gets the mipmap chain of the one face.
         *
         * @return The mipmap chain.
         */
        [[nodiscard]] MipmapChain& getMipmapsProperty() noexcept;

        /** @brief Const access to the mipmap chain. */
        [[nodiscard]] const MipmapChain& getMipmapsProperty() const noexcept;

        /**
         * @brief Sets the mipmap chain of the one face.
         *
         * @param value The chain.
         */
        void setMipmapsProperty(std::shared_ptr<MipmapChain> value);

        /**
         * @brief Verifies that all contents of this texture are present, correct and match the
         *        capabilities of the device.
         *
         * @param targetProfile The target graphics profile, or none.
         */
        void Validate(std::optional<Microsoft::Xna::Framework::Graphics::GraphicsProfile> targetProfile) override;

        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        [[nodiscard]] std::string ProfileTypeName() const override;
        [[nodiscard]] SharpRuntime::intcs ProfileSizeLimit(Microsoft::Xna::Framework::Graphics::GraphicsProfile targetProfile) const override;
    };

    /** @brief Provides properties for maintaining a 3D texture: one face per depth slice. */
    class Texture3DContent : public TextureContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture3DContent";

        /** @brief Initializes a new instance of Texture3DContent: no faces, resizable. */
        Texture3DContent();

        /**
         * @brief Generates a full set of mipmaps for the texture, halving the depth as well: level
         *        k exists for slice s only while s is below depth / 2^k.
         *
         * @param overwriteExistingMipmaps True to regenerate existing chains.
         */
        void GenerateMipmaps(bool overwriteExistingMipmaps) override;

        /**
         * @brief Verifies that all contents of this texture are present, correct and match the
         *        capabilities of the device; Reach does not support 3D textures.
         *
         * @param targetProfile The target graphics profile, or none.
         */
        void Validate(std::optional<Microsoft::Xna::Framework::Graphics::GraphicsProfile> targetProfile) override;

        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        [[nodiscard]] std::string ProfileTypeName() const override;
        [[nodiscard]] SharpRuntime::intcs ProfileSizeLimit(Microsoft::Xna::Framework::Graphics::GraphicsProfile targetProfile) const override;
    };

    /** @brief Provides properties for maintaining a cube texture: six square faces of one size. */
    class TextureCubeContent : public TextureContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureCubeContent";

        /** @brief Initializes a new instance of TextureCubeContent: six faces, fixed. */
        TextureCubeContent();

        /**
         * @brief Verifies that all contents of this texture are present, correct and match the
         *        capabilities of the device.
         *
         * @param targetProfile The target graphics profile, or none.
         */
        void Validate(std::optional<Microsoft::Xna::Framework::Graphics::GraphicsProfile> targetProfile) override;

        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        [[nodiscard]] std::string ProfileTypeName() const override;
        [[nodiscard]] SharpRuntime::intcs ProfileSizeLimit(Microsoft::Xna::Framework::Graphics::GraphicsProfile targetProfile) const override;
    };
}
