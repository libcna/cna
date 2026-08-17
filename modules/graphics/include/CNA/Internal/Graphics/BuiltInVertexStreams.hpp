// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>

#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

/**
 * @brief GPU stream layouts for the built-in XNA vertex structures.
 *
 * XNA's own vertex types are blittable value types, so over there `sizeof(T)` *is* the byte
 * distance between consecutive GPU vertices and the two facts are interchangeable. CNA cannot
 * reproduce that identity: `Color` implements `IPackedVector` and most vertex structures
 * implement `IVertexType`, both of which are polymorphic C++ base classes here, so a vertex
 * object carries a vtable pointer and places its members at alignment-driven offsets that have
 * nothing to do with the stream.
 *
 * Each structure below is that stream — the exact bytes a renderer walks — and is the single
 * source of truth for the matching type's `VertexDeclaration`: the declaration takes its stride
 * from `sizeof(stream)` and every element offset from `offsetof(stream, member)`, so the two can
 * never drift apart. `Pack()` is the one place where a vertex object becomes stream bytes.
 */
namespace CNA::Internal::Graphics
{
    /** @brief GPU stream for VertexPositionColor: float3 position + ubyte4 colour. */
    struct PositionColorStream
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(PositionColorStream) == 16);
    static_assert(offsetof(PositionColorStream, x) == 0);
    static_assert(offsetof(PositionColorStream, r) == 12);

    /** @brief GPU stream for VertexPositionTexture: float3 position + float2 texture coordinate. */
    struct PositionTextureStream
    {
        float x, y, z;
        float u, v;
    };
    static_assert(sizeof(PositionTextureStream) == 20);
    static_assert(offsetof(PositionTextureStream, x) == 0);
    static_assert(offsetof(PositionTextureStream, u) == 12);

    /** @brief GPU stream for VertexPositionColorTexture: float3 + ubyte4 + float2. */
    struct PositionColorTextureStream
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
        float u, v;
    };
    static_assert(sizeof(PositionColorTextureStream) == 24);
    static_assert(offsetof(PositionColorTextureStream, x) == 0);
    static_assert(offsetof(PositionColorTextureStream, r) == 12);
    static_assert(offsetof(PositionColorTextureStream, u) == 16);

    /** @brief GPU stream for VertexPositionNormalTexture: float3 + float3 + float2. */
    struct PositionNormalTextureStream
    {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
    };
    static_assert(sizeof(PositionNormalTextureStream) == 32);
    static_assert(offsetof(PositionNormalTextureStream, x) == 0);
    static_assert(offsetof(PositionNormalTextureStream, nx) == 12);
    static_assert(offsetof(PositionNormalTextureStream, u) == 24);

    /** @brief GPU stream for VertexPositionNormalTextureSkinned: float3 + float3 + float2 + float4 + ubyte4. */
    struct PositionNormalTextureSkinnedStream
    {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(PositionNormalTextureSkinnedStream) == 52);
    static_assert(offsetof(PositionNormalTextureSkinnedStream, x) == 0);
    static_assert(offsetof(PositionNormalTextureSkinnedStream, nx) == 12);
    static_assert(offsetof(PositionNormalTextureSkinnedStream, u) == 24);
    static_assert(offsetof(PositionNormalTextureSkinnedStream, w0) == 32);
    static_assert(offsetof(PositionNormalTextureSkinnedStream, i0) == 48);

    /** @brief GPU stream for VertexPositionNormalTangentTexture: float3 + float3 + float4 + float2. */
    struct PositionNormalTangentTextureStream
    {
        float x, y, z;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PositionNormalTangentTextureStream) == 48);
    static_assert(offsetof(PositionNormalTangentTextureStream, x) == 0);
    static_assert(offsetof(PositionNormalTangentTextureStream, nx) == 12);
    static_assert(offsetof(PositionNormalTangentTextureStream, tx) == 24);
    static_assert(offsetof(PositionNormalTangentTextureStream, u) == 40);

    /**
     * @brief GPU stream for a rigid PBR vertex carrying two texture-coordinate sets and a colour.
     *
     * The Position/Normal/Tangent/UV0 fields occupy the stride-48 PBR record byte for byte, and
     * UV1 follows it, which brings the useful data to 56 bytes. Stride 56 already identifies the
     * skinned+colour layout, so `GLTF-182` padded this record to 60 to keep one meaning per stride.
     *
     * plan_gltf.md `GLTF-462` gives those four bytes a job instead of leaving them reserved: they
     * are the packed `COLOR_0` a metallic-roughness primitive may carry (§3.7.2.1 makes vertex
     * colour "an additional linear multiplier to base color"). The change is byte-compatible with
     * every renderer that already binds this stride — offsets 0..55 are untouched and the four
     * trailing bytes were declared padding a consumer must ignore. A primitive with no `COLOR_0`
     * writes **opaque white** there, which is the identity multiplier, so a renderer that starts
     * reading the slot cannot darken anything that used to be correct.
     */
    struct PositionNormalTangentTexture2ColorStream
    {
        float x, y, z;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u0, v0;
        float u1, v1;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(PositionNormalTangentTexture2ColorStream) == 60);
    static_assert(offsetof(PositionNormalTangentTexture2ColorStream, x) == 0);
    static_assert(offsetof(PositionNormalTangentTexture2ColorStream, nx) == 12);
    static_assert(offsetof(PositionNormalTangentTexture2ColorStream, tx) == 24);
    static_assert(offsetof(PositionNormalTangentTexture2ColorStream, u0) == 40);
    static_assert(offsetof(PositionNormalTangentTexture2ColorStream, u1) == 48);
    static_assert(offsetof(PositionNormalTangentTexture2ColorStream, r) == 56);

    /** @brief GPU stream for VertexPositionNormalTangentTextureSkinned. */
    struct PositionNormalTangentTextureSkinnedStream
    {
        float x, y, z;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(PositionNormalTangentTextureSkinnedStream) == 68);
    static_assert(offsetof(PositionNormalTangentTextureSkinnedStream, x) == 0);
    static_assert(offsetof(PositionNormalTangentTextureSkinnedStream, nx) == 12);
    static_assert(offsetof(PositionNormalTangentTextureSkinnedStream, tx) == 24);
    static_assert(offsetof(PositionNormalTangentTextureSkinnedStream, u) == 40);
    static_assert(offsetof(PositionNormalTangentTextureSkinnedStream, w0) == 48);
    static_assert(offsetof(PositionNormalTangentTextureSkinnedStream, i0) == 64);

    /** @brief GPU stream for a skinned PBR vertex carrying two texture-coordinate sets. */
    struct PositionNormalTangentTextureSkinned2Stream
    {
        float x, y, z;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u0, v0;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
        float u1, v1;
    };
    static_assert(sizeof(PositionNormalTangentTextureSkinned2Stream) == 76);
    static_assert(offsetof(PositionNormalTangentTextureSkinned2Stream, x) == 0);
    static_assert(offsetof(PositionNormalTangentTextureSkinned2Stream, nx) == 12);
    static_assert(offsetof(PositionNormalTangentTextureSkinned2Stream, tx) == 24);
    static_assert(offsetof(PositionNormalTangentTextureSkinned2Stream, u0) == 40);
    static_assert(offsetof(PositionNormalTangentTextureSkinned2Stream, w0) == 48);
    static_assert(offsetof(PositionNormalTangentTextureSkinned2Stream, i0) == 64);
    static_assert(offsetof(PositionNormalTangentTextureSkinned2Stream, u1) == 68);


    /**
     * @brief Maps a built-in vertex structure to the GPU stream that carries its values.
     *
     * Specialised for every built-in type CNA can convert. A type with no specialisation has no
     * object-to-stream conversion, so it can only reach a renderer through a caller-packed stream.
     */
    template <typename VertexT>
    struct VertexStreamOf;

    /** @brief Stream mapping for VertexPositionColor. */
    template <>
    struct VertexStreamOf<Microsoft::Xna::Framework::Graphics::VertexPositionColor>
    {
        /** @brief The GPU stream structure. */
        using type = PositionColorStream;
    };
    /** @brief Stream mapping for VertexPositionTexture. */
    template <>
    struct VertexStreamOf<Microsoft::Xna::Framework::Graphics::VertexPositionTexture>
    {
        /** @brief The GPU stream structure. */
        using type = PositionTextureStream;
    };
    /** @brief Stream mapping for VertexPositionColorTexture. */
    template <>
    struct VertexStreamOf<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture>
    {
        /** @brief The GPU stream structure. */
        using type = PositionColorTextureStream;
    };
    /** @brief Stream mapping for VertexPositionNormalTexture. */
    template <>
    struct VertexStreamOf<Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture>
    {
        /** @brief The GPU stream structure. */
        using type = PositionNormalTextureStream;
    };
    /** @brief Stream mapping for VertexPositionNormalTextureSkinned. */
    template <>
    struct VertexStreamOf<Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned>
    {
        /** @brief The GPU stream structure. */
        using type = PositionNormalTextureSkinnedStream;
    };
    /** @brief Stream mapping for VertexPositionNormalTangentTexture. */
    template <>
    struct VertexStreamOf<Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTexture>
    {
        /** @brief The GPU stream structure. */
        using type = PositionNormalTangentTextureStream;
    };
    /** @brief Stream mapping for VertexPositionNormalTangentTextureSkinned. */
    template <>
    struct VertexStreamOf<
        Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTextureSkinned>
    {
        /** @brief The GPU stream structure. */
        using type = PositionNormalTangentTextureSkinnedStream;
    };

    /** @brief The GPU stream structure that carries @p VertexT's values. */
    template <typename VertexT>
    using VertexStream = typename VertexStreamOf<VertexT>::type;

    /** @brief The GPU stride, in bytes, of the stream that carries @p VertexT's values. */
    template <typename VertexT>
    inline constexpr int VertexStreamStride = static_cast<int>(sizeof(VertexStream<VertexT>));

    /**
     * @brief Converts one VertexPositionColor object into its GPU stream vertex.
     * @param vertex The source vertex object.
     * @return The packed stream vertex.
     */
    inline PositionColorStream Pack(
        const Microsoft::Xna::Framework::Graphics::VertexPositionColor& vertex)
    {
        return PositionColorStream{
            vertex.Position.X, vertex.Position.Y, vertex.Position.Z,
            vertex.Color.getRProperty(), vertex.Color.getGProperty(),
            vertex.Color.getBProperty(), vertex.Color.getAProperty()};
    }

    /**
     * @brief Converts one VertexPositionTexture object into its GPU stream vertex.
     * @param vertex The source vertex object.
     * @return The packed stream vertex.
     */
    inline PositionTextureStream Pack(
        const Microsoft::Xna::Framework::Graphics::VertexPositionTexture& vertex)
    {
        return PositionTextureStream{
            vertex.Position.X, vertex.Position.Y, vertex.Position.Z,
            vertex.TextureCoordinate.X, vertex.TextureCoordinate.Y};
    }

    /**
     * @brief Converts one VertexPositionColorTexture object into its GPU stream vertex.
     * @param vertex The source vertex object.
     * @return The packed stream vertex.
     */
    inline PositionColorTextureStream Pack(
        const Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture& vertex)
    {
        return PositionColorTextureStream{
            vertex.Position.X, vertex.Position.Y, vertex.Position.Z,
            vertex.Color.getRProperty(), vertex.Color.getGProperty(),
            vertex.Color.getBProperty(), vertex.Color.getAProperty(),
            vertex.TextureCoordinate.X, vertex.TextureCoordinate.Y};
    }

    /**
     * @brief Converts one VertexPositionNormalTexture object into its GPU stream vertex.
     * @param vertex The source vertex object.
     * @return The packed stream vertex.
     */
    inline PositionNormalTextureStream Pack(
        const Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture& vertex)
    {
        return PositionNormalTextureStream{
            vertex.Position.X, vertex.Position.Y, vertex.Position.Z,
            vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z,
            vertex.TextureCoordinate.X, vertex.TextureCoordinate.Y};
    }

    /**
     * @brief Converts one VertexPositionNormalTextureSkinned object into its GPU stream vertex.
     * @param vertex The source vertex object.
     * @return The packed stream vertex.
     */
    inline PositionNormalTextureSkinnedStream Pack(
        const Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned& vertex)
    {
        return PositionNormalTextureSkinnedStream{
            vertex.Position.X, vertex.Position.Y, vertex.Position.Z,
            vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z,
            vertex.TextureCoordinate.X, vertex.TextureCoordinate.Y,
            vertex.BlendWeight.X, vertex.BlendWeight.Y,
            vertex.BlendWeight.Z, vertex.BlendWeight.W,
            vertex.BlendIndices[0], vertex.BlendIndices[1],
            vertex.BlendIndices[2], vertex.BlendIndices[3]};
    }
}
