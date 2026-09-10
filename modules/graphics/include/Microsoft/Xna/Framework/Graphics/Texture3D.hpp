// SPDX-License-Identifier: MS-PL
#pragma once

#include <bit>
#include <concepts>
#include <cstring>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/ArgumentNullException.hpp"

namespace CNA::Internal::Renderers
{
    class ITexture3DRenderer;
}

namespace CNA::Internal::Graphics
{
    /** @brief Identifies XNA packed-vector elements that expose their exact storage word. */
    template<typename T>
    concept Texture3DPackedElement = requires(T value, const T constantValue)
    {
        constantValue.getPackedValueProperty();
        value.setPackedValueProperty(constantValue.getPackedValueProperty());
    };

    /** @brief Identifies scalar/vector binary32 elements accepted by float volume formats. */
    template<typename T>
    concept Texture3DFloatElement =
        std::same_as<std::remove_cvref_t<T>, float> ||
        std::same_as<std::remove_cvref_t<T>, Microsoft::Xna::Framework::Vector2> ||
        std::same_as<std::remove_cvref_t<T>, Microsoft::Xna::Framework::Vector4>;

    /** @brief Identifies raw bytes used by the XNA Texture3D content reader. */
    template<typename T>
    concept Texture3DByteElement =
        std::same_as<std::remove_cvref_t<T>, std::uint8_t>;

    /** @brief Identifies other C++ value types that can use XNA's raw generic transfer path. */
    template<typename T>
    concept Texture3DRawElement =
        std::is_trivially_copyable_v<std::remove_cvref_t<T>> &&
        (!Texture3DPackedElement<std::remove_cvref_t<T>>) &&
        (!Texture3DFloatElement<std::remove_cvref_t<T>>) &&
        (!Texture3DByteElement<std::remove_cvref_t<T>>);

    /** @brief Identifies the value types supported by Texture3D's XNA transfer surface. */
    template<typename T>
    concept Texture3DElement =
        Texture3DPackedElement<T> || Texture3DFloatElement<T> || Texture3DByteElement<T> ||
        Texture3DRawElement<T>;
}

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Represents a 3D (volume) texture. */
    class Texture3D : public Texture
    {
    public:
        using Texture::Dispose;

        /**
         * @brief Creates a 3D texture with the given dimensions and format.
         *
         * @param device  The graphics device to create the texture on.
         * @param width   Width in texels.
         * @param height  Height in texels.
         * @param depth   Depth (number of slices) in texels.
         * @param mipMap  True to allocate a full mipmap chain for authored per-level data.
         * @param format  The desired surface format.
         */
        Texture3D(GraphicsDevice& device, int width, int height, int depth, bool mipMap, SurfaceFormat format);

        /** @brief Destructor. */
        CNAEXT ~Texture3D() override;

        /**
         * @brief Copying is not allowed.
         *
         * CNAEXT, explicit for clarity: `renderer_`'s `std::unique_ptr` member already makes this
         * implicit, but plans/plan_xnb.md XNB-25 needed a real move path added (see below) and every
         * other similarly-shaped GPU-resource class in this codebase (`VertexBuffer`,
         * `IndexBuffer`, `TextureCube`) already declares both explicitly rather than relying on
         * what the compiler happens to imply.
         */
        CNAEXT Texture3D(const Texture3D&) = delete;
        /** @brief Copy-assignment is not allowed. */
        CNAEXT Texture3D& operator=(const Texture3D&) = delete;
        /**
         * @brief Move-constructs a Texture3D, transferring GPU handle ownership.
         *
         * CNAEXT: this class had no move path at all until plans/plan_xnb.md XNB-25's `Texture3DReader`
         * needed one -- a user-declared destructor already suppressed the implicit move
         * constructor the compiler would otherwise have generated, and the pre-existing
         * `std::unique_ptr` member independently blocks the implicit copy constructor, so this
         * type could not previously be returned by value at all (not even via NRVO, which the
         * standard never guarantees).
         */
        CNAEXT Texture3D(Texture3D&&) noexcept;
        /** @brief Move-assigns a Texture3D, transferring GPU handle ownership. */
        CNAEXT Texture3D& operator=(Texture3D&&) noexcept;

        /** @brief Returns the fully qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns the texture width in texels. */
        [[nodiscard]] int getWidthProperty() const;
        /** @brief Returns the texture height in texels. */
        [[nodiscard]] int getHeightProperty() const;
        /** @brief Returns the texture depth (number of slices) in texels. */
        [[nodiscard]] int getDepthProperty() const;

        // getFormatProperty() and getLevelCountProperty() are inherited from Texture.

        /**
         * @brief Uploads data to the entire texture.
         *
         * @param data         Pointer to the Color array to upload.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(const Color* data, int elementCount);

        /**
         * @brief Uploads a subset of data to the entire texture.
         *
         * @param data         Pointer to the source Color array.
         * @param startIndex   First element within @p data to start reading.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(const Color* data, int startIndex, int elementCount);

        /**
         * @brief Uploads data to a sub-volume of the specified mip level.
         *
         * REMED-GFX-135: this call has exactly two outcomes -- the complete requested box is
         * stored, or it throws. It never returns after storing nothing or only part of the box.
         * Every argument is validated before anything is uploaded, so a rejected call leaves the
         * resource and the source array untouched.
         *
         * @param level        Mip level to write (0 = full size).
         * @param left         Left boundary of the sub-volume in texels.
         * @param top          Top boundary of the sub-volume in texels.
         * @param right        Right boundary (exclusive) of the sub-volume in texels.
         * @param bottom       Bottom boundary (exclusive) of the sub-volume in texels.
         * @param front        Front boundary of the sub-volume in texels (slice index).
         * @param back         Back boundary (exclusive) of the sub-volume in texels.
         * @param data         Pointer to the source Color array.
         * @param elementCount Number of Color elements to upload; must exactly match the number
         *                     of format bytes in the requested box when multiplied by four.
         * @throws System::ObjectDisposedException if this Texture3D has been disposed.
         * @throws System::NotSupportedException if this renderer cannot store the requested mip
         *         level or box.
         * @throws System::ArgumentNullException if @p data is null.
         * @throws System::ArgumentOutOfRangeException for an invalid startIndex or elementCount.
         * @throws System::ArgumentException if the box or total transfer size is invalid.
         * @throws System::InvalidOperationException if @p level is invalid or this resource is
         *         currently bound for sampling.
         */
        void SetData(int level, int left, int top, int right, int bottom, int front, int back,
                     const Color* data, int startIndex, int elementCount);

        /**
         * @brief Uploads value-type data to the entire volume.
         *
         * @tparam T A trivially copyable value type whose byte width divides the format width.
         * @param data Source elements.
         * @param elementCount Number of elements to upload.
         */
        template<CNA::Internal::Graphics::Texture3DElement T>
        void SetData(const T* data, int elementCount)
        {
            SetData(data, 0, elementCount);
        }

        /**
         * @brief Uploads a source window of typed data to the entire volume.
         *
         * @tparam T A trivially copyable value type whose byte width divides the format width.
         * @param data Source elements.
         * @param startIndex First source element.
         * @param elementCount Number of elements to upload.
         */
        template<CNA::Internal::Graphics::Texture3DElement T>
        void SetData(const T* data, int startIndex, int elementCount)
        {
            SetData(0, 0, 0, width_, height_, 0, depth_, data, startIndex, elementCount);
        }

        /**
         * @brief Uploads typed data to a box in one volume mip level.
         *
         * @tparam T A trivially copyable value type whose byte width divides the format width.
         * @param level Mip level beginning at zero.
         * @param left Left box boundary.
         * @param top Top box boundary.
         * @param right Exclusive right box boundary.
         * @param bottom Exclusive bottom box boundary.
         * @param front Front box boundary.
         * @param back Exclusive back box boundary.
         * @param data Source elements.
         * @param startIndex First source element.
         * @param elementCount Number of elements whose total bytes must equal the box storage.
         */
        template<CNA::Internal::Graphics::Texture3DElement T>
        void SetData(int level, int left, int top, int right, int bottom, int front, int back,
                     const T* data, int startIndex, int elementCount)
        {
            using Element = std::remove_cvref_t<T>;
            int elementBytes = 0;
            if constexpr (CNA::Internal::Graphics::Texture3DPackedElement<Element>)
            {
                using Word = std::remove_cvref_t<decltype(data[0].getPackedValueProperty())>;
                static_assert(std::is_unsigned_v<Word>);
                elementBytes = static_cast<int>(sizeof(Word));
            }
            else if constexpr (CNA::Internal::Graphics::Texture3DByteElement<Element>)
            {
                elementBytes = 1;
            }
            else if constexpr (CNA::Internal::Graphics::Texture3DFloatElement<Element>)
            {
                constexpr int components = std::same_as<Element, float> ? 1
                    : (std::same_as<Element, Microsoft::Xna::Framework::Vector2> ? 2 : 4);
                elementBytes = components * static_cast<int>(sizeof(float));
            }
            else
            {
                elementBytes = static_cast<int>(sizeof(Element));
            }
            const int required = ValidateTypedTransferEXT(
                "Texture3D::SetData", true, level, left, top, right, bottom, front, back,
                data, startIndex, elementCount, elementBytes);
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(required) * static_cast<std::size_t>(elementBytes));
            if constexpr (CNA::Internal::Graphics::Texture3DPackedElement<Element>)
            {
                using Word = std::remove_cvref_t<decltype(data[0].getPackedValueProperty())>;
                for (int index = 0; index < required; ++index)
                {
                    const Word value = data[startIndex + index].getPackedValueProperty();
                    for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                        bytes[static_cast<std::size_t>(index) * sizeof(Word) + byte] =
                            static_cast<std::uint8_t>(value >> (byte * 8u));
                }
            }
            else if constexpr (CNA::Internal::Graphics::Texture3DByteElement<Element>)
            {
                for (int index = 0; index < required; ++index)
                    bytes[static_cast<std::size_t>(index)] = data[startIndex + index];
            }
            else if constexpr (CNA::Internal::Graphics::Texture3DFloatElement<Element>)
            {
                constexpr int components = std::same_as<Element, float> ? 1
                    : (std::same_as<Element, Microsoft::Xna::Framework::Vector2> ? 2 : 4);
                for (int index = 0; index < required; ++index)
                {
                    const Element& value = data[startIndex + index];
                    for (int component = 0; component < components; ++component)
                    {
                        float channel = 0.0f;
                        if constexpr (std::same_as<Element, float>) channel = value;
                        else if constexpr (std::same_as<Element, Microsoft::Xna::Framework::Vector2>)
                            channel = component == 0 ? value.X : value.Y;
                        else if (component == 0) channel = value.X;
                        else if (component == 1) channel = value.Y;
                        else if (component == 2) channel = value.Z;
                        else channel = value.W;
                        const std::uint32_t bits = std::bit_cast<std::uint32_t>(channel);
                        const std::size_t offset =
                            (static_cast<std::size_t>(index) * components + component) * sizeof(float);
                        for (std::size_t byte = 0; byte < sizeof(float); ++byte)
                            bytes[offset + byte] = static_cast<std::uint8_t>(bits >> (byte * 8u));
                    }
                }
            }
            else
            {
                std::memcpy(bytes.data(), data + startIndex, bytes.size());
            }
            SetTypedDataBytesEXT(level, left, top, right, bottom, front, back,
                                 bytes.data());
        }

        /**
         * @brief Uploads raw byte data to a sub-volume using a native pointer.
         *
         * REMED-GFX-135: shares SetData's completion contract -- it returns only after the whole
         * box has been stored, and otherwise throws.
         *
         * @param level      Mip level to write.
         * @param left       Left boundary in texels.
         * @param top        Top boundary in texels.
         * @param right      Right boundary (exclusive) in texels.
         * @param bottom     Bottom boundary (exclusive) in texels.
         * @param front      Front boundary in texels.
         * @param back       Back boundary (exclusive) in texels.
         * @param data       Pointer to the raw byte data.
         * @param dataLength Size of the data in bytes.
         * @throws System::ObjectDisposedException if this Texture3D has been disposed.
         * @throws System::NotSupportedException if this renderer did not store the whole box.
         * @throws std::invalid_argument if @p data is null.
         */
        CNAEXT void SetDataPointerEXT(int level, int left, int top, int right, int bottom, int front, int back,
                                     const void* data, int dataLength);

        /**
         * @brief Reads all texture data into the provided array.
         *
         * @param data         Output array to receive the Color data.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(Color* data, int elementCount) const;

        /**
         * @brief Reads a subset of texture data into the provided array.
         *
         * @param data         Output array to receive the Color data.
         * @param startIndex   First element within @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(Color* data, int startIndex, int elementCount) const;

        /**
         * @brief Reads data from a sub-volume of the specified mip level.
         *
         * REMED-GFX-130: both overloads above delegate here, so this describes all three. The call
         * has exactly two outcomes. It either writes the requested box's real content into @p data
         * starting at @p startIndex -- slice by slice front to back, each slice row-major with the
         * top row first -- or it throws and leaves @p data byte-for-byte untouched. There is no
         * partially written or fabricated result, and elements outside
         * `[startIndex, startIndex + width * height * depth)` are never modified.
         *
         * @param level        Mip level to read (0 = full size).
         * @param left         Left boundary of the sub-volume in texels.
         * @param top          Top boundary of the sub-volume in texels.
         * @param right        Right boundary (exclusive) in texels.
         * @param bottom       Bottom boundary (exclusive) in texels.
         * @param front        Front boundary in texels.
         * @param back         Back boundary (exclusive) in texels.
         * @param data         Output array to receive the Color data.
         * @param startIndex   First element within @p data to write to.
         * @param elementCount Number of Color elements whose total four-byte storage must exactly
         *                     match the requested box storage.
         * @throws System::ObjectDisposedException if this texture has been disposed.
         * @throws System::NotSupportedException if this graphics renderer cannot read the requested
         *         volume/mip level back to the CPU (including renderers that create no volume
         *         resource at all).
         * @throws System::ArgumentNullException if @p data is null.
         * @throws System::ArgumentOutOfRangeException for an invalid startIndex or elementCount.
         * @throws System::ArgumentException if the box or total transfer size is invalid.
         * @throws System::InvalidOperationException if @p level is invalid.
         */
        void GetData(int level, int left, int top, int right, int bottom, int front, int back,
                     Color* data, int startIndex, int elementCount) const;

        /**
         * @brief Reads the entire volume into value-type elements.
         *
         * @tparam T A trivially copyable value type whose byte width divides the format width.
         * @param data Destination elements.
         * @param elementCount Number of elements to read.
         */
        template<CNA::Internal::Graphics::Texture3DElement T>
        void GetData(T* data, int elementCount) const
        {
            GetData(data, 0, elementCount);
        }

        /**
         * @brief Reads the entire volume into a destination window of typed elements.
         *
         * @tparam T A trivially copyable value type whose byte width divides the format width.
         * @param data Destination elements.
         * @param startIndex First destination element.
         * @param elementCount Number of elements to read.
         */
        template<CNA::Internal::Graphics::Texture3DElement T>
        void GetData(T* data, int startIndex, int elementCount) const
        {
            GetData(0, 0, 0, width_, height_, 0, depth_, data, startIndex, elementCount);
        }

        /**
         * @brief Reads a box in one mip level into typed elements.
         *
         * @tparam T A trivially copyable value type whose byte width divides the format width.
         * @param level Mip level beginning at zero.
         * @param left Left box boundary.
         * @param top Top box boundary.
         * @param right Exclusive right box boundary.
         * @param bottom Exclusive bottom box boundary.
         * @param front Front box boundary.
         * @param back Exclusive back box boundary.
         * @param data Destination elements.
         * @param startIndex First destination element.
         * @param elementCount Number of elements whose total bytes must equal the box storage.
         */
        template<CNA::Internal::Graphics::Texture3DElement T>
        void GetData(int level, int left, int top, int right, int bottom, int front, int back,
                     T* data, int startIndex, int elementCount) const
        {
            using Element = std::remove_cvref_t<T>;
            int elementBytes = 0;
            if constexpr (CNA::Internal::Graphics::Texture3DPackedElement<Element>)
            {
                using Word = std::remove_cvref_t<decltype(data[0].getPackedValueProperty())>;
                static_assert(std::is_unsigned_v<Word>);
                elementBytes = static_cast<int>(sizeof(Word));
            }
            else if constexpr (CNA::Internal::Graphics::Texture3DByteElement<Element>)
            {
                elementBytes = 1;
            }
            else if constexpr (CNA::Internal::Graphics::Texture3DFloatElement<Element>)
            {
                constexpr int components = std::same_as<Element, float> ? 1
                    : (std::same_as<Element, Microsoft::Xna::Framework::Vector2> ? 2 : 4);
                elementBytes = components * static_cast<int>(sizeof(float));
            }
            else
            {
                elementBytes = static_cast<int>(sizeof(Element));
            }
            const int required = ValidateTypedTransferEXT(
                "Texture3D::GetData", false, level, left, top, right, bottom, front, back,
                data, startIndex, elementCount, elementBytes);
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(required) * static_cast<std::size_t>(elementBytes));
            GetTypedDataBytesEXT(level, left, top, right, bottom, front, back,
                                 bytes.data());
            if constexpr (CNA::Internal::Graphics::Texture3DPackedElement<Element>)
            {
                using Word = std::remove_cvref_t<decltype(data[0].getPackedValueProperty())>;
                for (int index = 0; index < required; ++index)
                {
                    Word value = 0;
                    for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                        value |= static_cast<Word>(
                            bytes[static_cast<std::size_t>(index) * sizeof(Word) + byte])
                            << (byte * 8u);
                    data[startIndex + index].setPackedValueProperty(value);
                }
            }
            else if constexpr (CNA::Internal::Graphics::Texture3DByteElement<Element>)
            {
                for (int index = 0; index < required; ++index)
                    data[startIndex + index] = bytes[static_cast<std::size_t>(index)];
            }
            else if constexpr (CNA::Internal::Graphics::Texture3DFloatElement<Element>)
            {
                constexpr int components = std::same_as<Element, float> ? 1
                    : (std::same_as<Element, Microsoft::Xna::Framework::Vector2> ? 2 : 4);
                for (int index = 0; index < required; ++index)
                {
                    Element& value = data[startIndex + index];
                    for (int component = 0; component < components; ++component)
                    {
                        const std::size_t offset =
                            (static_cast<std::size_t>(index) * components + component) * sizeof(float);
                        std::uint32_t bits = 0u;
                        for (std::size_t byte = 0; byte < sizeof(float); ++byte)
                            bits |= static_cast<std::uint32_t>(bytes[offset + byte]) << (byte * 8u);
                        const float channel = std::bit_cast<float>(bits);
                        if constexpr (std::same_as<Element, float>) value = channel;
                        else if constexpr (std::same_as<Element, Microsoft::Xna::Framework::Vector2>)
                        {
                            if (component == 0) value.X = channel;
                            else value.Y = channel;
                        }
                        else if (component == 0) value.X = channel;
                        else if (component == 1) value.Y = channel;
                        else if (component == 2) value.Z = channel;
                        else value.W = channel;
                    }
                }
            }
            else
            {
                std::memcpy(data + startIndex, bytes.data(), bytes.size());
            }
        }

        /**
         * @brief Returns a reference to the renderer implementation object.
         *
         * @return Reference to the renderer ITexture3DRenderer.
         */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::ITexture3DRenderer& GetRenderer() const { return *renderer_; }

    protected:
        /** @brief Releases the renderer 3D texture handle when the resource is disposed. */
        void Dispose(bool disposing) override;

    private:
        [[nodiscard]] int ValidateTypedTransferEXT(
            const char* api, bool setting, int level, int left, int top, int right, int bottom,
            int front, int back, const void* data, int startIndex, int elementCount,
            int elementBytes) const;
        void SetTypedDataBytesEXT(
            int level, int left, int top, int right, int bottom, int front, int back,
            const std::uint8_t* data);
        void GetTypedDataBytesEXT(
            int level, int left, int top, int right, int bottom, int front, int back,
            std::uint8_t* data) const;

        int width_;
        int height_;
        int depth_;
        // SKIA-149: shared (not unique) ownership so a SkiaEffectRenderer can hold a weak_ptr for
        // volume-sampling lifetime tracking, matching Texture2D's identical ITextureRenderer
        // pattern. Texture3D itself remains non-copyable; this only lets a second, weak observer
        // outlive a single call without becoming the resource's owner.
        std::shared_ptr<CNA::Internal::Renderers::ITexture3DRenderer> renderer_;
    };
}
