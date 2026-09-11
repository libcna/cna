// SPDX-License-Identifier: MS-PL
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short4.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics::detail
{
    /**
     * @brief The per-element facts `PixelBitmapContent<T>` and `VectorConverter` need: the byte
     *        layout of one element, its GPU formats, and the conversion to and from `Vector4`.
     *
     * XNA accepts exactly the 22 types specialized below ("Supported types are Single, Vector2,
     * Vector3, Vector4, and value types that implement IPackedVector"); any other `T` fails to
     * compile, where XNA throws `NotSupportedException` at run time
     * (`tests/reference/xna40/graphics/graphics-content-oracle.json`, `pixel_type/Byte/describe`).
     */
    // Declared and left undefined: the specializations below are the whole list, and `ValidPixelType`
    // asks whether one exists. A definition carrying a static_assert would fire while the concept
    // merely asks the question, which is what a vertex channel of indices does
    // (VertexChannel<int> is a legitimate type with no vector conversion). The friendly message a
    // wrong T deserves lives on the two entry points that take one: PixelBitmapContent<T> and
    // VectorConverter::GetConverter<TIn, TOut>().
    template<typename T>
    struct PixelTraits;

    inline void WriteLittleEndian(std::uint64_t value, std::size_t bytes, std::uint8_t* out)
    {
        for (std::size_t i = 0; i < bytes; ++i)
        {
            out[i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
    }

    inline std::uint64_t ReadLittleEndian(const std::uint8_t* in, std::size_t bytes)
    {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < bytes; ++i)
        {
            value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
        }
        return value;
    }

    template<std::size_t N>
    inline void WriteFloats(const float* values, std::uint8_t* out)
    {
        std::memcpy(out, values, 4 * N);
    }

    template<std::size_t N>
    inline void ReadFloats(const std::uint8_t* in, float* values)
    {
        std::memcpy(values, in, 4 * N);
    }

    template<>
    struct PixelTraits<float>
    {
        static constexpr std::size_t Bytes = 4;
        static constexpr std::string_view Name = "Single";
        static constexpr std::string_view DotNetName = "System.Single";
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::SurfaceFormat> Surface{Microsoft::Xna::Framework::Graphics::SurfaceFormat::Single};
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::VertexElementFormat> Vertex{Microsoft::Xna::Framework::Graphics::VertexElementFormat::Single};
        static Vector4 ToVector4(float v) { return Vector4(v, 0.0f, 0.0f, 1.0f); }
        static float FromVector4(const Vector4& v) { return v.X; }
        static void Write(float v, std::uint8_t* out) { WriteFloats<1>(&v, out); }
        static float Read(const std::uint8_t* in) { float v; ReadFloats<1>(in, &v); return v; }
        static bool Equal(float a, float b) { return a == b; }
    };

    template<>
    struct PixelTraits<Vector2>
    {
        static constexpr std::size_t Bytes = 8;
        static constexpr std::string_view Name = "Vector2";
        static constexpr std::string_view DotNetName = "Microsoft.Xna.Framework.Vector2";
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::SurfaceFormat> Surface{Microsoft::Xna::Framework::Graphics::SurfaceFormat::Vector2};
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::VertexElementFormat> Vertex{Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector2};
        static Vector4 ToVector4(const Vector2& v) { return Vector4(v.X, v.Y, 0.0f, 1.0f); }
        static Vector2 FromVector4(const Vector4& v) { return Vector2(v.X, v.Y); }
        static void Write(const Vector2& v, std::uint8_t* out) { const float f[2] = {v.X, v.Y}; WriteFloats<2>(f, out); }
        static Vector2 Read(const std::uint8_t* in) { float f[2]; ReadFloats<2>(in, f); return Vector2(f[0], f[1]); }
        static bool Equal(const Vector2& a, const Vector2& b) { return a == b; }
    };

    template<>
    struct PixelTraits<Vector3>
    {
        static constexpr std::size_t Bytes = 12;
        static constexpr std::string_view Name = "Vector3";
        static constexpr std::string_view DotNetName = "Microsoft.Xna.Framework.Vector3";
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::SurfaceFormat> Surface{};
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::VertexElementFormat> Vertex{Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector3};
        static Vector4 ToVector4(const Vector3& v) { return Vector4(v.X, v.Y, v.Z, 1.0f); }
        static Vector3 FromVector4(const Vector4& v) { return Vector3(v.X, v.Y, v.Z); }
        static void Write(const Vector3& v, std::uint8_t* out) { const float f[3] = {v.X, v.Y, v.Z}; WriteFloats<3>(f, out); }
        static Vector3 Read(const std::uint8_t* in) { float f[3]; ReadFloats<3>(in, f); return Vector3(f[0], f[1], f[2]); }
        static bool Equal(const Vector3& a, const Vector3& b) { return a == b; }
    };

    template<>
    struct PixelTraits<Vector4>
    {
        static constexpr std::size_t Bytes = 16;
        static constexpr std::string_view Name = "Vector4";
        static constexpr std::string_view DotNetName = "Microsoft.Xna.Framework.Vector4";
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::SurfaceFormat> Surface{Microsoft::Xna::Framework::Graphics::SurfaceFormat::Vector4};
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::VertexElementFormat> Vertex{Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector4};
        static Vector4 ToVector4(const Vector4& v) { return v; }
        static Vector4 FromVector4(const Vector4& v) { return v; }
        static void Write(const Vector4& v, std::uint8_t* out) { const float f[4] = {v.X, v.Y, v.Z, v.W}; WriteFloats<4>(f, out); }
        static Vector4 Read(const std::uint8_t* in) { float f[4]; ReadFloats<4>(in, f); return Vector4(f[0], f[1], f[2], f[3]); }
        static bool Equal(const Vector4& a, const Vector4& b) { return a == b; }
    };

    template<>
    struct PixelTraits<Color>
    {
        static constexpr std::size_t Bytes = 4;
        static constexpr std::string_view Name = "Color";
        static constexpr std::string_view DotNetName = "Microsoft.Xna.Framework.Color";
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::SurfaceFormat> Surface{Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color};
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::VertexElementFormat> Vertex{Microsoft::Xna::Framework::Graphics::VertexElementFormat::Color};
        static Vector4 ToVector4(const Color& c) { return c.ToVector4(); }
        static Color FromVector4(const Vector4& v) { return Color(v); }
        static void Write(const Color& c, std::uint8_t* out) { WriteLittleEndian(c.getPackedValueProperty(), 4, out); }
        static Color Read(const std::uint8_t* in) { Color c; c.setPackedValueProperty(static_cast<std::uint32_t>(ReadLittleEndian(in, 4))); return c; }
        static bool Equal(const Color& a, const Color& b) { return a == b; }
    };

/// Declares the traits of one IPackedVector type: @p bytes of little-endian packed value.
#define CNA_XNA_PACKED_PIXEL_TRAITS(type, bytes, surface, vertex)                                 \
    template<>                                                                                    \
    struct PixelTraits<Microsoft::Xna::Framework::Graphics::PackedVector::type>                   \
    {                                                                                             \
        using Packed = Microsoft::Xna::Framework::Graphics::PackedVector::type;                   \
        static constexpr std::size_t Bytes = bytes;                                               \
        static constexpr std::string_view Name = #type;                                           \
        static constexpr std::string_view DotNetName = "Microsoft.Xna.Framework.Graphics.PackedVector." #type; \
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::SurfaceFormat> Surface surface; \
        static constexpr std::optional<Microsoft::Xna::Framework::Graphics::VertexElementFormat> Vertex vertex; \
        static Vector4 ToVector4(const Packed& p) { return p.ToVector4(); }                       \
        static Packed FromVector4(const Vector4& v) { Packed p; p.PackFromVector4(v); return p; }  \
        static void Write(const Packed& p, std::uint8_t* out)                                     \
        {                                                                                         \
            WriteLittleEndian(static_cast<std::uint64_t>(p.getPackedValueProperty()), bytes, out); \
        }                                                                                         \
        static Packed Read(const std::uint8_t* in)                                                \
        {                                                                                         \
            Packed p;                                                                             \
            p.setPackedValueProperty(static_cast<decltype(p.getPackedValueProperty())>(ReadLittleEndian(in, bytes))); \
            return p;                                                                             \
        }                                                                                         \
        static bool Equal(const Packed& a, const Packed& b) { return a == b; }                    \
    }

    CNA_XNA_PACKED_PIXEL_TRAITS(Alpha8, 1, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::Alpha8}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(Bgr565, 2, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::Bgr565}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(Bgra4444, 2, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::Bgra4444}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(Bgra5551, 2, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::Bgra5551}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(Byte4, 4, {}, {Microsoft::Xna::Framework::Graphics::VertexElementFormat::Byte4});
    CNA_XNA_PACKED_PIXEL_TRAITS(HalfSingle, 2, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::HalfSingle}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(HalfVector2, 4, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::HalfVector2}, {Microsoft::Xna::Framework::Graphics::VertexElementFormat::HalfVector2});
    CNA_XNA_PACKED_PIXEL_TRAITS(HalfVector4, 8, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::HalfVector4}, {Microsoft::Xna::Framework::Graphics::VertexElementFormat::HalfVector4});
    CNA_XNA_PACKED_PIXEL_TRAITS(NormalizedByte2, 2, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::NormalizedByte2}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(NormalizedByte4, 4, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::NormalizedByte4}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(NormalizedShort2, 4, {}, {Microsoft::Xna::Framework::Graphics::VertexElementFormat::NormalizedShort2});
    CNA_XNA_PACKED_PIXEL_TRAITS(NormalizedShort4, 8, {}, {Microsoft::Xna::Framework::Graphics::VertexElementFormat::NormalizedShort4});
    CNA_XNA_PACKED_PIXEL_TRAITS(Rg32, 4, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::Rg32}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(Rgba1010102, 4, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::Rgba1010102}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(Rgba64, 8, {Microsoft::Xna::Framework::Graphics::SurfaceFormat::Rgba64}, {});
    CNA_XNA_PACKED_PIXEL_TRAITS(Short2, 4, {}, {Microsoft::Xna::Framework::Graphics::VertexElementFormat::Short2});
    CNA_XNA_PACKED_PIXEL_TRAITS(Short4, 8, {}, {Microsoft::Xna::Framework::Graphics::VertexElementFormat::Short4});
#undef CNA_XNA_PACKED_PIXEL_TRAITS

    /** @brief True when @p T is one of the 22 element types XNA's PixelBitmapContent accepts. */
    template<typename T>
    concept ValidPixelType = requires { PixelTraits<T>::Bytes; };
}

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief The .NET name of a packed vector type, taken from the traits that already hold it.
     *
     * A vertex channel is named by its element type, and a packed vector can be one: a model's
     * `BlendIndices` are `Byte4`. These types are structs without the `XnaTypeName` member the
     * generic `ContentTypeName` looks for, and their names are already spelled once in
     * `PixelTraits` -- so they are taken from there rather than written a second time
     * (plans/plan_xnapipeline_parity.md `XNAPP-266`).
     */
    template<typename T>
        requires(!requires { { T::XnaTypeName } -> std::convertible_to<std::string_view>; } &&
                 requires { Graphics::detail::PixelTraits<T>::DotNetName; })
    struct CNAEXT ContentTypeName<T>
    {
        /** @brief Returns the type's .NET full name. */
        [[nodiscard]] static std::string Name()
        {
            return std::string(Graphics::detail::PixelTraits<T>::DotNetName);
        }
    };
}
