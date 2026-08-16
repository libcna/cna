// SPDX-License-Identifier: MS-PL
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffectVertexLayout.hpp"

#include "System/NotSupportedException.hpp"

#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::SdlGpu
{
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

    SDL_GPUVertexElementFormat ToSdlGpuVertexElementFormat(VertexElementFormat format)
    {
        switch (format)
        {
            case VertexElementFormat::Single:           return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
            case VertexElementFormat::Vector2:          return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            case VertexElementFormat::Vector3:          return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            case VertexElementFormat::Vector4:          return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            case VertexElementFormat::Color:            return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
            case VertexElementFormat::Byte4:            return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
            case VertexElementFormat::Short2:           return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2;
            case VertexElementFormat::Short4:           return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4;
            case VertexElementFormat::NormalizedShort2: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM;
            case VertexElementFormat::NormalizedShort4: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM;
            case VertexElementFormat::HalfVector2:      return SDL_GPU_VERTEXELEMENTFORMAT_HALF2;
            case VertexElementFormat::HalfVector4:      return SDL_GPU_VERTEXELEMENTFORMAT_HALF4;
        }
        throw std::invalid_argument(
            "CNA SDL_GPU: unrecognized VertexElementFormat ordinal " +
            std::to_string(static_cast<int>(format)));
    }

    MOJOSHADER_usage ToMojoShaderUsage(VertexElementUsage usage)
    {
        switch (usage)
        {
            case VertexElementUsage::Position:          return MOJOSHADER_USAGE_POSITION;
            case VertexElementUsage::Color:              return MOJOSHADER_USAGE_COLOR;
            case VertexElementUsage::TextureCoordinate:  return MOJOSHADER_USAGE_TEXCOORD;
            case VertexElementUsage::Normal:             return MOJOSHADER_USAGE_NORMAL;
            case VertexElementUsage::Binormal:           return MOJOSHADER_USAGE_BINORMAL;
            case VertexElementUsage::Tangent:            return MOJOSHADER_USAGE_TANGENT;
            case VertexElementUsage::BlendIndices:       return MOJOSHADER_USAGE_BLENDINDICES;
            case VertexElementUsage::BlendWeight:        return MOJOSHADER_USAGE_BLENDWEIGHT;
            case VertexElementUsage::Depth:              return MOJOSHADER_USAGE_DEPTH;
            case VertexElementUsage::Fog:                return MOJOSHADER_USAGE_FOG;
            case VertexElementUsage::PointSize:          return MOJOSHADER_USAGE_POINTSIZE;
            case VertexElementUsage::Sample:             return MOJOSHADER_USAGE_SAMPLE;
            case VertexElementUsage::TessellateFactor:   return MOJOSHADER_USAGE_TESSFACTOR;
        }
        throw std::invalid_argument(
            "CNA SDL_GPU: unrecognized VertexElementUsage ordinal " +
            std::to_string(static_cast<int>(usage)));
    }

    MOJOSHADER_vertexElementFormat ToMojoShaderVertexElementFormat(VertexElementFormat format)
    {
        switch (format)
        {
            case VertexElementFormat::Single:           return MOJOSHADER_VERTEXELEMENTFORMAT_SINGLE;
            case VertexElementFormat::Vector2:          return MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR2;
            case VertexElementFormat::Vector3:          return MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR3;
            case VertexElementFormat::Vector4:          return MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
            case VertexElementFormat::Color:            return MOJOSHADER_VERTEXELEMENTFORMAT_COLOR;
            case VertexElementFormat::Byte4:            return MOJOSHADER_VERTEXELEMENTFORMAT_BYTE4;
            case VertexElementFormat::Short2:           return MOJOSHADER_VERTEXELEMENTFORMAT_SHORT2;
            case VertexElementFormat::Short4:           return MOJOSHADER_VERTEXELEMENTFORMAT_SHORT4;
            case VertexElementFormat::NormalizedShort2: return MOJOSHADER_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2;
            case VertexElementFormat::NormalizedShort4: return MOJOSHADER_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4;
            case VertexElementFormat::HalfVector2:      return MOJOSHADER_VERTEXELEMENTFORMAT_HALFVECTOR2;
            case VertexElementFormat::HalfVector4:      return MOJOSHADER_VERTEXELEMENTFORMAT_HALFVECTOR4;
        }
        throw std::invalid_argument(
            "CNA SDL_GPU: unrecognized VertexElementFormat ordinal " +
            std::to_string(static_cast<int>(format)));
    }

    std::vector<MOJOSHADER_vertexAttribute> BuildMojoShaderVertexAttributes(
        const MOJOSHADER_parseData& vertexParseData,
        const std::vector<VertexElement>& declaredElements)
    {
        std::vector<MOJOSHADER_vertexAttribute> attributes;
        if (vertexParseData.attribute_count <= 0 || vertexParseData.attributes == nullptr)
            return attributes;
        attributes.reserve(static_cast<std::size_t>(vertexParseData.attribute_count));

        for (int i = 0; i < vertexParseData.attribute_count; ++i)
        {
            const MOJOSHADER_attribute& shaderInput = vertexParseData.attributes[i];

            const VertexElement* match = nullptr;
            for (const VertexElement& element : declaredElements)
            {
                if (ToMojoShaderUsage(element.getVertexElementUsageProperty()) == shaderInput.usage &&
                    element.getUsageIndexProperty() == shaderInput.index)
                {
                    match = &element;
                    break;
                }
            }
            if (match == nullptr)
            {
                const char* name = shaderInput.name != nullptr ? shaderInput.name : "<unnamed>";
                throw System::NotSupportedException(
                    "CNA SDL_GPU: this compiled effect's vertex shader requires attribute '" +
                    std::string(name) + "' (usage " + std::to_string(static_cast<int>(shaderInput.usage)) +
                    ", index " + std::to_string(shaderInput.index) +
                    "), but the VertexDeclaration supplied to this draw does not declare an element "
                    "with that usage and usage index.");
            }

            MOJOSHADER_vertexAttribute attribute{};
            attribute.usage = shaderInput.usage;
            attribute.usageIndex = shaderInput.index;
            attribute.vertexElementFormat =
                ToMojoShaderVertexElementFormat(match->getVertexElementFormatProperty());
            attributes.push_back(attribute);
        }

        return attributes;
    }

    std::vector<SDL_GPUVertexAttribute> BuildCompiledEffectVertexAttributes(
        const MOJOSHADER_parseData& vertexParseData,
        const std::vector<VertexElement>& declaredElements,
        Uint32 bufferSlot)
    {
        std::vector<SDL_GPUVertexAttribute> attributes;
        if (vertexParseData.attribute_count <= 0 || vertexParseData.attributes == nullptr)
            return attributes;
        attributes.reserve(static_cast<std::size_t>(vertexParseData.attribute_count));

        for (int i = 0; i < vertexParseData.attribute_count; ++i)
        {
            const MOJOSHADER_attribute& shaderInput = vertexParseData.attributes[i];

            const VertexElement* match = nullptr;
            for (const VertexElement& element : declaredElements)
            {
                if (ToMojoShaderUsage(element.getVertexElementUsageProperty()) == shaderInput.usage &&
                    element.getUsageIndexProperty() == shaderInput.index)
                {
                    match = &element;
                    break;
                }
            }
            if (match == nullptr)
            {
                const char* name = shaderInput.name != nullptr ? shaderInput.name : "<unnamed>";
                throw System::NotSupportedException(
                    "CNA SDL_GPU: this compiled effect's vertex shader requires attribute '" +
                    std::string(name) + "' (usage " + std::to_string(static_cast<int>(shaderInput.usage)) +
                    ", index " + std::to_string(shaderInput.index) +
                    "), but the VertexDeclaration supplied to this draw does not declare an element "
                    "with that usage and usage index.");
            }

            SDL_GPUVertexAttribute attribute{};
            attribute.location = static_cast<Uint32>(i);
            attribute.buffer_slot = bufferSlot;
            attribute.format = ToSdlGpuVertexElementFormat(match->getVertexElementFormatProperty());
            attribute.offset = static_cast<Uint32>(match->getOffsetProperty());
            attributes.push_back(attribute);
        }

        return attributes;
    }
}

#endif  // CNA_SDL_GPU_COMPILED_EFFECTS
