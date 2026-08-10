// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Fna3d/Fna3dApi.hpp"

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <stdexcept>
#include <string>

/**
 * @file
 * @brief XNA-ordinal to FNA3D-enumerator bridge for the FNA3D graphics renderer.
 *
 * FNA3D's header states that its enumerations "should match XNA 4.0", and CNA's own enums are
 * ports of the same XNA 4.0 specification, so every one of the mappings below is numerically the
 * identity. That is a fact worth *proving* rather than assuming: each converter is guarded by
 * static_asserts pinning the endpoints (and, where the enum is small enough to matter, every
 * value), so a future divergence on either side becomes a compile error instead of a silently
 * wrong blend mode or vertex format. The runtime range check then catches an out-of-contract
 * ordinal from the shared graphics layer rather than casting it into an undefined enumerator.
 */
namespace CNA::Internal::Renderers::Fna3d
{
    /**
     * @brief Throws for an ordinal the FNA3D enumeration has no value for.
     *
     * @param what    Name of the state being converted, e.g. "Blend".
     * @param ordinal The rejected ordinal.
     */
    [[noreturn]] inline void RejectOrdinal(const char* what, int ordinal)
    {
        throw std::runtime_error(
            std::string("FNA3D renderer: no FNA3D ") + what + " for XNA ordinal " +
            std::to_string(ordinal) + " -- the binding list is never silently truncated to a "
            "default.");
    }

    /**
     * @brief Converts an XNA `Blend` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::Blend` value.
     * @return The matching `FNA3D_Blend`.
     */
    [[nodiscard]] inline FNA3D_Blend ToFna3dBlend(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::Blend;
        static_assert(static_cast<int>(Xna::One) == FNA3D_BLEND_ONE);
        static_assert(static_cast<int>(Xna::Zero) == FNA3D_BLEND_ZERO);
        static_assert(static_cast<int>(Xna::SourceAlpha) == FNA3D_BLEND_SOURCEALPHA);
        static_assert(static_cast<int>(Xna::InverseSourceAlpha) == FNA3D_BLEND_INVERSESOURCEALPHA);
        static_assert(static_cast<int>(Xna::BlendFactor) == FNA3D_BLEND_BLENDFACTOR);
        static_assert(static_cast<int>(Xna::SourceAlphaSaturation) ==
                      FNA3D_BLEND_SOURCEALPHASATURATION);
        if (ordinal < FNA3D_BLEND_ONE || ordinal > FNA3D_BLEND_SOURCEALPHASATURATION)
            RejectOrdinal("Blend", ordinal);
        return static_cast<FNA3D_Blend>(ordinal);
    }

    /**
     * @brief Converts an XNA `BlendFunction` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::BlendFunction` value.
     * @return The matching `FNA3D_BlendFunction`.
     */
    [[nodiscard]] inline FNA3D_BlendFunction ToFna3dBlendFunction(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::BlendFunction;
        static_assert(static_cast<int>(Xna::Add) == FNA3D_BLENDFUNCTION_ADD);
        static_assert(static_cast<int>(Xna::Subtract) == FNA3D_BLENDFUNCTION_SUBTRACT);
        static_assert(static_cast<int>(Xna::ReverseSubtract) ==
                      FNA3D_BLENDFUNCTION_REVERSESUBTRACT);
        static_assert(static_cast<int>(Xna::Max) == FNA3D_BLENDFUNCTION_MAX);
        static_assert(static_cast<int>(Xna::Min) == FNA3D_BLENDFUNCTION_MIN);
        if (ordinal < FNA3D_BLENDFUNCTION_ADD || ordinal > FNA3D_BLENDFUNCTION_MIN)
            RejectOrdinal("BlendFunction", ordinal);
        return static_cast<FNA3D_BlendFunction>(ordinal);
    }

    /**
     * @brief Converts an XNA `CompareFunction` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::CompareFunction` value.
     * @return The matching `FNA3D_CompareFunction`.
     */
    [[nodiscard]] inline FNA3D_CompareFunction ToFna3dCompareFunction(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::CompareFunction;
        static_assert(static_cast<int>(Xna::Always) == FNA3D_COMPAREFUNCTION_ALWAYS);
        static_assert(static_cast<int>(Xna::Never) == FNA3D_COMPAREFUNCTION_NEVER);
        static_assert(static_cast<int>(Xna::Less) == FNA3D_COMPAREFUNCTION_LESS);
        static_assert(static_cast<int>(Xna::LessEqual) == FNA3D_COMPAREFUNCTION_LESSEQUAL);
        static_assert(static_cast<int>(Xna::Equal) == FNA3D_COMPAREFUNCTION_EQUAL);
        static_assert(static_cast<int>(Xna::GreaterEqual) == FNA3D_COMPAREFUNCTION_GREATEREQUAL);
        static_assert(static_cast<int>(Xna::Greater) == FNA3D_COMPAREFUNCTION_GREATER);
        static_assert(static_cast<int>(Xna::NotEqual) == FNA3D_COMPAREFUNCTION_NOTEQUAL);
        if (ordinal < FNA3D_COMPAREFUNCTION_ALWAYS || ordinal > FNA3D_COMPAREFUNCTION_NOTEQUAL)
            RejectOrdinal("CompareFunction", ordinal);
        return static_cast<FNA3D_CompareFunction>(ordinal);
    }

    /**
     * @brief Converts an XNA `StencilOperation` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::StencilOperation` value.
     * @return The matching `FNA3D_StencilOperation`.
     */
    [[nodiscard]] inline FNA3D_StencilOperation ToFna3dStencilOperation(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::StencilOperation;
        static_assert(static_cast<int>(Xna::Keep) == FNA3D_STENCILOPERATION_KEEP);
        static_assert(static_cast<int>(Xna::Zero) == FNA3D_STENCILOPERATION_ZERO);
        static_assert(static_cast<int>(Xna::Replace) == FNA3D_STENCILOPERATION_REPLACE);
        static_assert(static_cast<int>(Xna::Increment) == FNA3D_STENCILOPERATION_INCREMENT);
        static_assert(static_cast<int>(Xna::Decrement) == FNA3D_STENCILOPERATION_DECREMENT);
        static_assert(static_cast<int>(Xna::IncrementSaturation) ==
                      FNA3D_STENCILOPERATION_INCREMENTSATURATION);
        static_assert(static_cast<int>(Xna::DecrementSaturation) ==
                      FNA3D_STENCILOPERATION_DECREMENTSATURATION);
        static_assert(static_cast<int>(Xna::Invert) == FNA3D_STENCILOPERATION_INVERT);
        if (ordinal < FNA3D_STENCILOPERATION_KEEP || ordinal > FNA3D_STENCILOPERATION_INVERT)
            RejectOrdinal("StencilOperation", ordinal);
        return static_cast<FNA3D_StencilOperation>(ordinal);
    }

    /**
     * @brief Converts an XNA `CullMode` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::CullMode` value.
     * @return The matching `FNA3D_CullMode`.
     */
    [[nodiscard]] inline FNA3D_CullMode ToFna3dCullMode(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::CullMode;
        static_assert(static_cast<int>(Xna::None) == FNA3D_CULLMODE_NONE);
        static_assert(static_cast<int>(Xna::CullClockwiseFace) ==
                      FNA3D_CULLMODE_CULLCLOCKWISEFACE);
        static_assert(static_cast<int>(Xna::CullCounterClockwiseFace) ==
                      FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE);
        if (ordinal < FNA3D_CULLMODE_NONE || ordinal > FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE)
            RejectOrdinal("CullMode", ordinal);
        return static_cast<FNA3D_CullMode>(ordinal);
    }

    /**
     * @brief Converts an XNA `FillMode` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::FillMode` value.
     * @return The matching `FNA3D_FillMode`.
     */
    [[nodiscard]] inline FNA3D_FillMode ToFna3dFillMode(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::FillMode;
        static_assert(static_cast<int>(Xna::Solid) == FNA3D_FILLMODE_SOLID);
        static_assert(static_cast<int>(Xna::WireFrame) == FNA3D_FILLMODE_WIREFRAME);
        if (ordinal < FNA3D_FILLMODE_SOLID || ordinal > FNA3D_FILLMODE_WIREFRAME)
            RejectOrdinal("FillMode", ordinal);
        return static_cast<FNA3D_FillMode>(ordinal);
    }

    /**
     * @brief Converts an XNA `TextureAddressMode` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::TextureAddressMode` value.
     * @return The matching `FNA3D_TextureAddressMode`.
     */
    [[nodiscard]] inline FNA3D_TextureAddressMode ToFna3dTextureAddressMode(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::TextureAddressMode;
        static_assert(static_cast<int>(Xna::Wrap) == FNA3D_TEXTUREADDRESSMODE_WRAP);
        static_assert(static_cast<int>(Xna::Clamp) == FNA3D_TEXTUREADDRESSMODE_CLAMP);
        static_assert(static_cast<int>(Xna::Mirror) == FNA3D_TEXTUREADDRESSMODE_MIRROR);
        if (ordinal < FNA3D_TEXTUREADDRESSMODE_WRAP || ordinal > FNA3D_TEXTUREADDRESSMODE_MIRROR)
            RejectOrdinal("TextureAddressMode", ordinal);
        return static_cast<FNA3D_TextureAddressMode>(ordinal);
    }

    /**
     * @brief Converts an XNA `TextureFilter` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::TextureFilter` value.
     * @return The matching `FNA3D_TextureFilter`.
     */
    [[nodiscard]] inline FNA3D_TextureFilter ToFna3dTextureFilter(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::TextureFilter;
        static_assert(static_cast<int>(Xna::Linear) == FNA3D_TEXTUREFILTER_LINEAR);
        static_assert(static_cast<int>(Xna::Point) == FNA3D_TEXTUREFILTER_POINT);
        static_assert(static_cast<int>(Xna::Anisotropic) == FNA3D_TEXTUREFILTER_ANISOTROPIC);
        static_assert(static_cast<int>(Xna::MinPointMagLinearMipPoint) ==
                      FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT);
        if (ordinal < FNA3D_TEXTUREFILTER_LINEAR ||
            ordinal > FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT)
            RejectOrdinal("TextureFilter", ordinal);
        return static_cast<FNA3D_TextureFilter>(ordinal);
    }

    /**
     * @brief Converts an XNA `DepthFormat` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::DepthFormat` value.
     * @return The matching `FNA3D_DepthFormat`.
     */
    [[nodiscard]] inline FNA3D_DepthFormat ToFna3dDepthFormat(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::DepthFormat;
        static_assert(static_cast<int>(Xna::None) == FNA3D_DEPTHFORMAT_NONE);
        static_assert(static_cast<int>(Xna::Depth16) == FNA3D_DEPTHFORMAT_D16);
        static_assert(static_cast<int>(Xna::Depth24) == FNA3D_DEPTHFORMAT_D24);
        static_assert(static_cast<int>(Xna::Depth24Stencil8) == FNA3D_DEPTHFORMAT_D24S8);
        if (ordinal < FNA3D_DEPTHFORMAT_NONE || ordinal > FNA3D_DEPTHFORMAT_D24S8)
            RejectOrdinal("DepthFormat", ordinal);
        return static_cast<FNA3D_DepthFormat>(ordinal);
    }

    /**
     * @brief Converts an XNA `SurfaceFormat` ordinal to its FNA3D enumerator.
     * @param ordinal Raw `Microsoft::Xna::Framework::Graphics::SurfaceFormat` value.
     * @return The matching `FNA3D_SurfaceFormat`.
     */
    [[nodiscard]] inline FNA3D_SurfaceFormat ToFna3dSurfaceFormat(int ordinal)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        static_assert(static_cast<int>(Xna::Color) == FNA3D_SURFACEFORMAT_COLOR);
        static_assert(static_cast<int>(Xna::Dxt5) == FNA3D_SURFACEFORMAT_DXT5);
        static_assert(static_cast<int>(Xna::HdrBlendable) == FNA3D_SURFACEFORMAT_HDRBLENDABLE);
        static_assert(static_cast<int>(Xna::UShortEXT) == FNA3D_SURFACEFORMAT_USHORT_EXT);
        if (ordinal < FNA3D_SURFACEFORMAT_COLOR || ordinal > FNA3D_SURFACEFORMAT_USHORT_EXT)
            RejectOrdinal("SurfaceFormat", ordinal);
        return static_cast<FNA3D_SurfaceFormat>(ordinal);
    }

    /**
     * @brief Converts an XNA `PrimitiveType` to its FNA3D enumerator.
     * @param primitive The XNA primitive topology.
     * @return The matching `FNA3D_PrimitiveType`.
     */
    [[nodiscard]] inline FNA3D_PrimitiveType ToFna3dPrimitiveType(
        Microsoft::Xna::Framework::Graphics::PrimitiveType primitive)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::PrimitiveType;
        static_assert(static_cast<int>(Xna::TriangleList) == FNA3D_PRIMITIVETYPE_TRIANGLELIST);
        static_assert(static_cast<int>(Xna::TriangleStrip) == FNA3D_PRIMITIVETYPE_TRIANGLESTRIP);
        static_assert(static_cast<int>(Xna::LineList) == FNA3D_PRIMITIVETYPE_LINELIST);
        static_assert(static_cast<int>(Xna::LineStrip) == FNA3D_PRIMITIVETYPE_LINESTRIP);
        static_assert(static_cast<int>(Xna::PointListEXT) == FNA3D_PRIMITIVETYPE_POINTLIST_EXT);
        const int ordinal = static_cast<int>(primitive);
        if (ordinal < FNA3D_PRIMITIVETYPE_TRIANGLELIST ||
            ordinal > FNA3D_PRIMITIVETYPE_POINTLIST_EXT)
            RejectOrdinal("PrimitiveType", ordinal);
        return static_cast<FNA3D_PrimitiveType>(ordinal);
    }

    /**
     * @brief Converts an XNA `SetDataOptions` value to its FNA3D enumerator.
     * @param options The XNA streaming hint.
     * @return The matching `FNA3D_SetDataOptions`.
     */
    [[nodiscard]] inline FNA3D_SetDataOptions ToFna3dSetDataOptions(
        Microsoft::Xna::Framework::Graphics::SetDataOptions options)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::SetDataOptions;
        static_assert(static_cast<int>(Xna::None) == FNA3D_SETDATAOPTIONS_NONE);
        static_assert(static_cast<int>(Xna::Discard) == FNA3D_SETDATAOPTIONS_DISCARD);
        static_assert(static_cast<int>(Xna::NoOverwrite) == FNA3D_SETDATAOPTIONS_NOOVERWRITE);
        const int ordinal = static_cast<int>(options);
        if (ordinal < FNA3D_SETDATAOPTIONS_NONE || ordinal > FNA3D_SETDATAOPTIONS_NOOVERWRITE)
            RejectOrdinal("SetDataOptions", ordinal);
        return static_cast<FNA3D_SetDataOptions>(ordinal);
    }

    /**
     * @brief Converts an XNA `VertexElementFormat` to its FNA3D enumerator.
     * @param format The XNA vertex element format.
     * @return The matching `FNA3D_VertexElementFormat`.
     */
    [[nodiscard]] inline FNA3D_VertexElementFormat ToFna3dVertexElementFormat(
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        static_assert(static_cast<int>(Xna::Single) == FNA3D_VERTEXELEMENTFORMAT_SINGLE);
        static_assert(static_cast<int>(Xna::Vector3) == FNA3D_VERTEXELEMENTFORMAT_VECTOR3);
        static_assert(static_cast<int>(Xna::Color) == FNA3D_VERTEXELEMENTFORMAT_COLOR);
        static_assert(static_cast<int>(Xna::Byte4) == FNA3D_VERTEXELEMENTFORMAT_BYTE4);
        static_assert(static_cast<int>(Xna::HalfVector4) == FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR4);
        const int ordinal = static_cast<int>(format);
        if (ordinal < FNA3D_VERTEXELEMENTFORMAT_SINGLE ||
            ordinal > FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR4)
            RejectOrdinal("VertexElementFormat", ordinal);
        return static_cast<FNA3D_VertexElementFormat>(ordinal);
    }

    /**
     * @brief Converts an XNA `VertexElementUsage` to its FNA3D enumerator.
     * @param usage The XNA vertex element usage.
     * @return The matching `FNA3D_VertexElementUsage`.
     */
    [[nodiscard]] inline FNA3D_VertexElementUsage ToFna3dVertexElementUsage(
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        static_assert(static_cast<int>(Xna::Position) == FNA3D_VERTEXELEMENTUSAGE_POSITION);
        static_assert(static_cast<int>(Xna::Color) == FNA3D_VERTEXELEMENTUSAGE_COLOR);
        static_assert(static_cast<int>(Xna::TextureCoordinate) ==
                      FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE);
        static_assert(static_cast<int>(Xna::Normal) == FNA3D_VERTEXELEMENTUSAGE_NORMAL);
        static_assert(static_cast<int>(Xna::BlendIndices) ==
                      FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES);
        static_assert(static_cast<int>(Xna::BlendWeight) == FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT);
        static_assert(static_cast<int>(Xna::TessellateFactor) ==
                      FNA3D_VERTEXELEMENTUSAGE_TESSELATEFACTOR);
        const int ordinal = static_cast<int>(usage);
        if (ordinal < FNA3D_VERTEXELEMENTUSAGE_POSITION ||
            ordinal > FNA3D_VERTEXELEMENTUSAGE_TESSELATEFACTOR)
            RejectOrdinal("VertexElementUsage", ordinal);
        return static_cast<FNA3D_VertexElementUsage>(ordinal);
    }

    /**
     * @brief Converts an XNA `ColorWriteChannels` bitmask to its FNA3D enumerator.
     *
     * XNA's mask is bit0=R, bit1=G, bit2=B, bit3=A, which is FNA3D's own bit layout; only the
     * range is checked, since every value in 0..15 is meaningful.
     *
     * @param mask Raw `ColorWriteChannels` bitmask.
     * @return The matching `FNA3D_ColorWriteChannels`.
     */
    [[nodiscard]] inline FNA3D_ColorWriteChannels ToFna3dColorWriteChannels(int mask)
    {
        static_assert(FNA3D_COLORWRITECHANNELS_RED == 1);
        static_assert(FNA3D_COLORWRITECHANNELS_GREEN == 2);
        static_assert(FNA3D_COLORWRITECHANNELS_BLUE == 4);
        static_assert(FNA3D_COLORWRITECHANNELS_ALPHA == 8);
        if (mask < FNA3D_COLORWRITECHANNELS_NONE || mask > FNA3D_COLORWRITECHANNELS_ALL)
            RejectOrdinal("ColorWriteChannels", mask);
        return static_cast<FNA3D_ColorWriteChannels>(mask);
    }

    /**
     * @brief Number of vertices a primitive count of @p primitiveCount occupies.
     *
     * FNA3D takes primitive counts everywhere, exactly like XNA, so this exists only for the
     * internal routes that must size a staging buffer before submitting.
     *
     * @param primitive      Primitive topology.
     * @param primitiveCount Number of primitives.
     * @return Vertex count consumed by that many primitives.
     */
    [[nodiscard]] inline int VertexCountForPrimitives(
        Microsoft::Xna::Framework::Graphics::PrimitiveType primitive, int primitiveCount)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::PrimitiveType;
        switch (primitive)
        {
            case Xna::TriangleList:  return primitiveCount * 3;
            case Xna::TriangleStrip: return primitiveCount + 2;
            case Xna::LineList:      return primitiveCount * 2;
            case Xna::LineStrip:     return primitiveCount + 1;
            case Xna::PointListEXT:  return primitiveCount;
        }
        RejectOrdinal("PrimitiveType", static_cast<int>(primitive));
    }
}
