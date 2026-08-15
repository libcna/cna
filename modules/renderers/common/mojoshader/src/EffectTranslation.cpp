// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    using Microsoft::Xna::Framework::Graphics::Blend;
    using Microsoft::Xna::Framework::Graphics::BlendFunction;
    using Microsoft::Xna::Framework::Graphics::ColorWriteChannels;
    using Microsoft::Xna::Framework::Graphics::CompareFunction;
    using Microsoft::Xna::Framework::Graphics::CullMode;
    using Microsoft::Xna::Framework::Graphics::FillMode;
    using Microsoft::Xna::Framework::Graphics::SamplerState;
    using Microsoft::Xna::Framework::Graphics::SamplerStateCollection;
    using Microsoft::Xna::Framework::Graphics::StencilOperation;
    using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
    using Microsoft::Xna::Framework::Graphics::TextureFilter;

    namespace
    {
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;
        constexpr std::size_t kMaximumReflectionDepth = 32u;
        constexpr std::size_t kMaximumReflectedValueBytes = 64u * 1024u * 1024u;

        template<typename TEnum>
        std::underlying_type_t<TEnum> ReadEnumStorage(const TEnum& value)
        {
            static_assert(std::is_enum_v<TEnum>);
            std::underlying_type_t<TEnum> result{};
            static_assert(sizeof(result) == sizeof(value));
            std::memcpy(&result, &value, sizeof(result));
            return result;
        }

        std::string SafeString(const char* value)
        {
            return value != nullptr ? value : "";
        }

        EffectParameterClass ToParameterClass(
            std::underlying_type_t<MOJOSHADER_symbolClass> value)
        {
            switch (value)
            {
                case MOJOSHADER_SYMCLASS_SCALAR:         return EffectParameterClass::Scalar;
                case MOJOSHADER_SYMCLASS_VECTOR:         return EffectParameterClass::Vector;
                case MOJOSHADER_SYMCLASS_MATRIX_ROWS:
                case MOJOSHADER_SYMCLASS_MATRIX_COLUMNS: return EffectParameterClass::Matrix;
                case MOJOSHADER_SYMCLASS_OBJECT:         return EffectParameterClass::Object;
                case MOJOSHADER_SYMCLASS_STRUCT:         return EffectParameterClass::Struct;
                default:
                    throw std::runtime_error(
                        "Compiled effect: unsupported MojoShader parameter class " +
                        std::to_string(static_cast<int>(value)) + ".");
            }
        }

        EffectParameterType ToParameterType(
            std::underlying_type_t<MOJOSHADER_symbolType> value)
        {
            switch (value)
            {
                case MOJOSHADER_SYMTYPE_VOID:        return EffectParameterType::Void;
                case MOJOSHADER_SYMTYPE_BOOL:        return EffectParameterType::Bool;
                case MOJOSHADER_SYMTYPE_INT:         return EffectParameterType::Int32;
                case MOJOSHADER_SYMTYPE_FLOAT:       return EffectParameterType::Single;
                case MOJOSHADER_SYMTYPE_STRING:      return EffectParameterType::String;
                case MOJOSHADER_SYMTYPE_TEXTURE:     return EffectParameterType::Texture;
                case MOJOSHADER_SYMTYPE_TEXTURE1D:   return EffectParameterType::Texture1D;
                case MOJOSHADER_SYMTYPE_TEXTURE2D:   return EffectParameterType::Texture2D;
                case MOJOSHADER_SYMTYPE_TEXTURE3D:   return EffectParameterType::Texture3D;
                case MOJOSHADER_SYMTYPE_TEXTURECUBE: return EffectParameterType::TextureCube;
                default:
                    throw std::runtime_error(
                        "Compiled effect: unsupported public MojoShader parameter type " +
                        std::to_string(static_cast<int>(value)) + ".");
            }
        }

        bool IsTextureType(std::underlying_type_t<MOJOSHADER_symbolType> value)
        {
            return value >= MOJOSHADER_SYMTYPE_TEXTURE &&
                   value <= MOJOSHADER_SYMTYPE_TEXTURECUBE;
        }

        bool IsSamplerType(std::underlying_type_t<MOJOSHADER_symbolType> value)
        {
            return value >= MOJOSHADER_SYMTYPE_SAMPLER &&
                   value <= MOJOSHADER_SYMTYPE_SAMPLERCUBE;
        }

        /// A value's storage layout follows its class first and its type second: only an object of
        /// a sampler type is allocated as an array of MOJOSHADER_effectSamplerState. Class and type
        /// are independent fields of the compiled binary, so asking about the type alone can walk a
        /// much smaller array of plain values as if it held sampler states.
        bool IsSamplerValue(const MOJOSHADER_effectValue& value)
        {
            return ReadEnumStorage(value.type.parameter_class) == MOJOSHADER_SYMCLASS_OBJECT &&
                   IsSamplerType(ReadEnumStorage(value.type.parameter_type));
        }

        bool IsShaderObjectType(std::underlying_type_t<MOJOSHADER_symbolType> value)
        {
            return value == MOJOSHADER_SYMTYPE_VERTEXSHADER ||
                   value == MOJOSHADER_SYMTYPE_PIXELSHADER ||
                   value == MOJOSHADER_SYMTYPE_VERTEXFRAGMENT ||
                   value == MOJOSHADER_SYMTYPE_PIXELFRAGMENT;
        }

        std::string ResolveString(const MOJOSHADER_effect* effect,
                                  const MOJOSHADER_effectValue& value)
        {
            if (ReadEnumStorage(value.type.parameter_type) != MOJOSHADER_SYMTYPE_STRING ||
                value.values == nullptr ||
                value.value_count == 0)
            {
                return {};
            }
            const int objectIndex = value.valuesI[0];
            if (objectIndex < 0 || objectIndex >= effect->object_count)
            {
                throw std::runtime_error(
                    "Compiled effect: a string parameter references an invalid object "
                    "table index.");
            }
            return SafeString(effect->objects[objectIndex].string.string);
        }

        CompiledEffectValueDescription DescribeValue(const MOJOSHADER_effect* effect,
                                                       const MOJOSHADER_effectValue& value,
                                                       std::size_t& reflectedValueBytes)
        {
            CompiledEffectValueDescription result;
            result.name = SafeString(value.name);
            result.semantic = SafeString(value.semantic);
            result.rowCount = static_cast<int>(value.type.rows);
            result.columnCount = static_cast<int>(value.type.columns);
            result.elementCount = static_cast<int>(value.type.elements);
            result.parameterClass = ToParameterClass(
                ReadEnumStorage(value.type.parameter_class));
            result.parameterType = ToParameterType(
                ReadEnumStorage(value.type.parameter_type));
            if (value.value_count > kMaximumReflectedValueBytes / 4 ||
                value.value_count > std::numeric_limits<std::size_t>::max() / 4)
            {
                throw std::runtime_error(
                    "Compiled effect: reflected value exceeds the safety limit.");
            }
            const std::size_t byteCount = static_cast<std::size_t>(value.value_count) * 4;
            if (byteCount > kMaximumReflectedValueBytes - reflectedValueBytes)
            {
                throw std::runtime_error(
                    "Compiled effect: aggregate reflected values exceed the safety limit.");
            }
            reflectedValueBytes += byteCount;
            if (byteCount > 0 && value.values == nullptr)
            {
                throw std::runtime_error(
                    "Compiled effect: reflected value storage is missing.");
            }
            if (byteCount > 0)
            {
                const auto* first = static_cast<const std::uint8_t*>(value.values);
                result.rawValue.assign(first, first + byteCount);
            }
            result.stringValue = ResolveString(effect, value);
            return result;
        }

        std::vector<CompiledEffectAnnotationDescription> DescribeAnnotations(
            const MOJOSHADER_effect* effect, const MOJOSHADER_effectAnnotation* annotations,
            unsigned int count, std::size_t& reflectedItemCount,
            std::size_t& reflectedValueBytes)
        {
            if (count > kMaximumReflectedItems - reflectedItemCount)
            {
                throw std::runtime_error(
                    "Compiled effect: annotation count exceeds the safety limit.");
            }
            if (count > 0 && annotations == nullptr)
            {
                throw std::runtime_error(
                    "Compiled effect: annotation storage is missing.");
            }
            reflectedItemCount += count;
            std::vector<CompiledEffectAnnotationDescription> result;
            result.reserve(count);
            for (unsigned int i = 0; i < count; ++i)
            {
                result.push_back(
                    DescribeValue(effect, annotations[i], reflectedValueBytes));
            }
            return result;
        }

        CompiledEffectParameterDescription DescribeMember(
            const MOJOSHADER_symbolStructMember& member, std::size_t depth,
            std::size_t& reflectedItemCount)
        {
            if (depth >= kMaximumReflectionDepth)
            {
                throw std::runtime_error(
                    "Compiled effect: structure nesting exceeds the safety limit.");
            }
            if (reflectedItemCount >= kMaximumReflectedItems)
            {
                throw std::runtime_error(
                    "Compiled effect: structure member count exceeds the safety limit.");
            }
            ++reflectedItemCount;
            CompiledEffectParameterDescription result;
            result.name = SafeString(member.name);
            result.rowCount = static_cast<int>(member.info.rows);
            result.columnCount = static_cast<int>(member.info.columns);
            result.elementCount = static_cast<int>(member.info.elements);
            result.parameterClass = ToParameterClass(
                ReadEnumStorage(member.info.parameter_class));
            result.parameterType = ToParameterType(
                ReadEnumStorage(member.info.parameter_type));
            if (member.info.member_count > 0 && member.info.members == nullptr)
            {
                throw std::runtime_error(
                    "Compiled effect: structure member storage is missing.");
            }
            result.structureMembers.reserve(member.info.member_count);
            for (unsigned int i = 0; i < member.info.member_count; ++i)
            {
                result.structureMembers.push_back(
                    DescribeMember(member.info.members[i], depth + 1, reflectedItemCount));
            }
            return result;
        }

        Blend ToBlend(MOJOSHADER_blendMode value)
        {
            switch (value)
            {
                case MOJOSHADER_BLEND_ZERO:           return Blend::Zero;
                case MOJOSHADER_BLEND_ONE:            return Blend::One;
                case MOJOSHADER_BLEND_SRCCOLOR:       return Blend::SourceColor;
                case MOJOSHADER_BLEND_INVSRCCOLOR:    return Blend::InverseSourceColor;
                case MOJOSHADER_BLEND_SRCALPHA:       return Blend::SourceAlpha;
                case MOJOSHADER_BLEND_INVSRCALPHA:    return Blend::InverseSourceAlpha;
                case MOJOSHADER_BLEND_DESTALPHA:      return Blend::DestinationAlpha;
                case MOJOSHADER_BLEND_INVDESTALPHA:   return Blend::InverseDestinationAlpha;
                case MOJOSHADER_BLEND_DESTCOLOR:      return Blend::DestinationColor;
                case MOJOSHADER_BLEND_INVDESTCOLOR:   return Blend::InverseDestinationColor;
                case MOJOSHADER_BLEND_SRCALPHASAT:    return Blend::SourceAlphaSaturation;
                case MOJOSHADER_BLEND_BLENDFACTOR:    return Blend::BlendFactor;
                case MOJOSHADER_BLEND_INVBLENDFACTOR: return Blend::InverseBlendFactor;
                default:
                    throw std::runtime_error(
                        "Compiled effect: unsupported D3D9 blend mode " +
                        std::to_string(static_cast<int>(value)) + ".");
            }
        }

        BlendFunction ToBlendFunction(MOJOSHADER_blendOp value)
        {
            switch (value)
            {
                case MOJOSHADER_BLENDOP_ADD:         return BlendFunction::Add;
                case MOJOSHADER_BLENDOP_SUBTRACT:    return BlendFunction::Subtract;
                case MOJOSHADER_BLENDOP_REVSUBTRACT: return BlendFunction::ReverseSubtract;
                case MOJOSHADER_BLENDOP_MIN:         return BlendFunction::Min;
                case MOJOSHADER_BLENDOP_MAX:         return BlendFunction::Max;
                default:
                    throw std::runtime_error(
                        "Compiled effect: unsupported D3D9 blend operation.");
            }
        }

        CompareFunction ToCompare(MOJOSHADER_compareFunc value)
        {
            switch (value)
            {
                case MOJOSHADER_CMP_NEVER:        return CompareFunction::Never;
                case MOJOSHADER_CMP_LESS:         return CompareFunction::Less;
                case MOJOSHADER_CMP_EQUAL:        return CompareFunction::Equal;
                case MOJOSHADER_CMP_LESSEQUAL:    return CompareFunction::LessEqual;
                case MOJOSHADER_CMP_GREATER:      return CompareFunction::Greater;
                case MOJOSHADER_CMP_NOTEQUAL:     return CompareFunction::NotEqual;
                case MOJOSHADER_CMP_GREATEREQUAL: return CompareFunction::GreaterEqual;
                case MOJOSHADER_CMP_ALWAYS:       return CompareFunction::Always;
                default:
                    throw std::runtime_error(
                        "Compiled effect: unsupported D3D9 comparison function.");
            }
        }

        StencilOperation ToStencil(MOJOSHADER_stencilOp value)
        {
            switch (value)
            {
                case MOJOSHADER_STENCILOP_KEEP:    return StencilOperation::Keep;
                case MOJOSHADER_STENCILOP_ZERO:    return StencilOperation::Zero;
                case MOJOSHADER_STENCILOP_REPLACE: return StencilOperation::Replace;
                case MOJOSHADER_STENCILOP_INCRSAT: return StencilOperation::IncrementSaturation;
                case MOJOSHADER_STENCILOP_DECRSAT: return StencilOperation::DecrementSaturation;
                case MOJOSHADER_STENCILOP_INVERT:  return StencilOperation::Invert;
                case MOJOSHADER_STENCILOP_INCR:    return StencilOperation::Increment;
                case MOJOSHADER_STENCILOP_DECR:    return StencilOperation::Decrement;
                default:
                    throw std::runtime_error(
                        "Compiled effect: unsupported D3D9 stencil operation.");
            }
        }

        TextureAddressMode ToAddress(MOJOSHADER_textureAddress value)
        {
            switch (value)
            {
                case MOJOSHADER_TADDRESS_WRAP:   return TextureAddressMode::Wrap;
                case MOJOSHADER_TADDRESS_MIRROR: return TextureAddressMode::Mirror;
                case MOJOSHADER_TADDRESS_CLAMP:  return TextureAddressMode::Clamp;
                default:
                    throw std::runtime_error(
                        "Compiled effect: Border and MirrorOnce sampler addressing are "
                        "not representable by XNA 4.0 SamplerState.");
            }
        }

        TextureFilter ToFilter(MOJOSHADER_textureFilterType mag,
                               MOJOSHADER_textureFilterType min,
                               MOJOSHADER_textureFilterType mip)
        {
            // Effect.cs treats anisotropic tokens as linear components when an effect changes
            // an individual filter axis. Preserve that behavior instead of manufacturing an
            // anisotropic aggregate state which the original XNA/FNA path would not select.
            const bool magPoint = mag == MOJOSHADER_TEXTUREFILTER_POINT;
            const bool minPoint = min == MOJOSHADER_TEXTUREFILTER_POINT;
            const bool magLinear = mag == MOJOSHADER_TEXTUREFILTER_LINEAR ||
                                   mag == MOJOSHADER_TEXTUREFILTER_ANISOTROPIC;
            const bool minLinear = min == MOJOSHADER_TEXTUREFILTER_LINEAR ||
                                   min == MOJOSHADER_TEXTUREFILTER_ANISOTROPIC;
            const bool mipPoint = mip == MOJOSHADER_TEXTUREFILTER_NONE ||
                                  mip == MOJOSHADER_TEXTUREFILTER_POINT;
            const bool mipLinear = mip == MOJOSHADER_TEXTUREFILTER_LINEAR ||
                                   mip == MOJOSHADER_TEXTUREFILTER_ANISOTROPIC;
            if ((!magPoint && !magLinear) || (!minPoint && !minLinear) ||
                (!mipPoint && !mipLinear))
            {
                throw std::runtime_error(
                    "Compiled effect: unsupported sampler filter component.");
            }
            if (magLinear && minLinear)
                return mipLinear ? TextureFilter::Linear : TextureFilter::LinearMipPoint;
            if (magPoint && minPoint)
                return mipLinear ? TextureFilter::PointMipLinear : TextureFilter::Point;
            if (magPoint && minLinear)
                return mipLinear ? TextureFilter::MinLinearMagPointMipLinear :
                                   TextureFilter::MinLinearMagPointMipPoint;
            return mipLinear ? TextureFilter::MinPointMagLinearMipLinear :
                               TextureFilter::MinPointMagLinearMipPoint;
        }

        void FromFilter(TextureFilter value, MOJOSHADER_textureFilterType& mag,
                        MOJOSHADER_textureFilterType& min,
                        MOJOSHADER_textureFilterType& mip)
        {
            using Filter = MOJOSHADER_textureFilterType;
            constexpr Filter point = MOJOSHADER_TEXTUREFILTER_POINT;
            constexpr Filter linear = MOJOSHADER_TEXTUREFILTER_LINEAR;
            switch (value)
            {
                case TextureFilter::Linear: mag = linear; min = linear; mip = linear; break;
                case TextureFilter::Point: mag = point; min = point; mip = point; break;
                case TextureFilter::Anisotropic:
                    mag = MOJOSHADER_TEXTUREFILTER_ANISOTROPIC;
                    min = MOJOSHADER_TEXTUREFILTER_ANISOTROPIC;
                    mip = MOJOSHADER_TEXTUREFILTER_ANISOTROPIC;
                    break;
                case TextureFilter::LinearMipPoint:
                    mag = linear; min = linear; mip = point; break;
                case TextureFilter::PointMipLinear:
                    mag = point; min = point; mip = linear; break;
                case TextureFilter::MinLinearMagPointMipLinear:
                    mag = point; min = linear; mip = linear; break;
                case TextureFilter::MinLinearMagPointMipPoint:
                    mag = point; min = linear; mip = point; break;
                case TextureFilter::MinPointMagLinearMipLinear:
                    mag = linear; min = point; mip = linear; break;
                case TextureFilter::MinPointMagLinearMipPoint:
                    mag = linear; min = point; mip = point; break;
                default:
                    throw std::runtime_error(
                        "Compiled effect: current sampler filter is invalid.");
            }
        }

        CullMode ToCullMode(MOJOSHADER_cullMode value)
        {
            switch (value)
            {
                case MOJOSHADER_CULL_NONE: return CullMode::None;
                case MOJOSHADER_CULL_CW: return CullMode::CullClockwiseFace;
                case MOJOSHADER_CULL_CCW: return CullMode::CullCounterClockwiseFace;
                default:
                    throw std::runtime_error(
                        "Compiled effect: unsupported cull mode.");
            }
        }

    }

    void ValidateNativeEffect(const MOJOSHADER_effect* effectData, const char* operation)
    {
        if (effectData == nullptr)
        {
            throw std::runtime_error(
                std::string("Compiled effect: MojoShader failed to ") + operation +
                " the Effect Framework bytecode.");
        }
        if (effectData->error_count > 0)
        {
            std::ostringstream message;
            message << "Compiled effect: MojoShader reported " << effectData->error_count
                    << " error(s) while attempting to " << operation << " the bytecode";
            const int reported = effectData->errors != nullptr
                                     ? std::min(effectData->error_count, 8)
                                     : 0;
            for (int i = 0; i < reported; ++i)
            {
                message << (i == 0 ? ": " : "; ")
                        << SafeString(effectData->errors[i].error);
            }
            throw std::runtime_error(message.str());
        }
        if (effectData->param_count < 0 ||
            static_cast<std::size_t>(effectData->param_count) > kMaximumReflectedItems ||
            (effectData->param_count > 0 && effectData->params == nullptr))
        {
            throw std::runtime_error(
                "Compiled effect: parameter table is invalid or exceeds the safety limit.");
        }
        if (effectData->object_count < 0 ||
            static_cast<std::size_t>(effectData->object_count) > kMaximumReflectedItems ||
            (effectData->object_count > 0 && effectData->objects == nullptr))
        {
            throw std::runtime_error(
                "Compiled effect: object table is invalid or exceeds the safety limit.");
        }
        if (effectData->technique_count < 1 ||
            static_cast<std::size_t>(effectData->technique_count) > kMaximumReflectedItems ||
            effectData->techniques == nullptr)
        {
            throw std::runtime_error(
                "Compiled effect: technique table is invalid or exceeds the safety limit.");
        }
    }

    CompiledEffectDescription BuildDescription(const MOJOSHADER_effect* effectData)
    {
        CompiledEffectDescription description_;
        std::size_t reflectedItemCount = 0;
        std::size_t reflectedValueBytes = 0;
        description_.parameters.reserve(static_cast<std::size_t>(effectData->param_count));
        for (int i = 0; i < effectData->param_count; ++i)
        {
            const MOJOSHADER_effectParam& parameter = effectData->params[i];
            const auto type = ReadEnumStorage(parameter.value.type.parameter_type);
            if (IsSamplerType(type) || IsShaderObjectType(type)) continue;

            CompiledEffectParameterDescription result;
            static_cast<CompiledEffectValueDescription&>(result) =
                DescribeValue(effectData, parameter.value, reflectedValueBytes);
            result.runtimeIndex = static_cast<std::uint32_t>(i);
            result.annotations = DescribeAnnotations(effectData, parameter.annotations,
                                                     parameter.annotation_count,
                                                     reflectedItemCount,
                                                     reflectedValueBytes);
            if (parameter.value.type.member_count > 0 &&
                parameter.value.type.members == nullptr)
            {
                throw std::runtime_error(
                    "Compiled effect: top-level structure member storage is missing.");
            }
            result.structureMembers.reserve(parameter.value.type.member_count);
            for (unsigned int member = 0; member < parameter.value.type.member_count; ++member)
            {
                result.structureMembers.push_back(
                    DescribeMember(parameter.value.type.members[member], 0,
                                   reflectedItemCount));
            }
            description_.parameters.push_back(std::move(result));
        }

        description_.techniques.reserve(static_cast<std::size_t>(effectData->technique_count));
        for (int i = 0; i < effectData->technique_count; ++i)
        {
            const MOJOSHADER_effectTechnique& technique = effectData->techniques[i];
            CompiledEffectTechniqueDescription reflected;
            reflected.name = SafeString(technique.name);
            reflected.annotations = DescribeAnnotations(effectData, technique.annotations,
                                                         technique.annotation_count,
                                                         reflectedItemCount,
                                                         reflectedValueBytes);
            if (technique.pass_count > kMaximumReflectedItems - reflectedItemCount ||
                (technique.pass_count > 0 && technique.passes == nullptr))
            {
                throw std::runtime_error(
                    "Compiled effect: pass table is invalid or exceeds the safety limit.");
            }
            reflectedItemCount += technique.pass_count;
            reflected.passes.reserve(technique.pass_count);
            for (unsigned int passIndex = 0; passIndex < technique.pass_count; ++passIndex)
            {
                const MOJOSHADER_effectPass& pass = technique.passes[passIndex];
                CompiledEffectPassDescription reflectedPass;
                reflectedPass.name = SafeString(pass.name);
                reflectedPass.annotations = DescribeAnnotations(effectData, pass.annotations,
                                                                 pass.annotation_count,
                                                                 reflectedItemCount,
                                                                 reflectedValueBytes);
                reflected.passes.push_back(std::move(reflectedPass));
            }
            if (reflected.passes.empty())
            {
                throw std::runtime_error(
                    "Compiled effect: technique '" + reflected.name +
                    "' declares no passes.");
            }
            description_.techniques.push_back(std::move(reflected));
        }
        return description_;
    }

    std::unordered_map<std::string, std::uint32_t> BuildSamplerTextureParameterMap(
        const MOJOSHADER_effect* effectData)
    {
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::unordered_map<std::string, std::uint32_t> texturesByName;
        for (int i = 0; i < effectData->param_count; ++i)
        {
            const MOJOSHADER_effectValue& value = effectData->params[i].value;
            if (IsTextureType(ReadEnumStorage(value.type.parameter_type)))
            {
                texturesByName[SafeString(value.name)] = static_cast<std::uint32_t>(i);
            }
        }

        for (int i = 0; i < effectData->param_count; ++i)
        {
            const MOJOSHADER_effectValue& sampler = effectData->params[i].value;
            if (!IsSamplerValue(sampler)) continue;
            const auto* states = sampler.valuesSS;
            if (sampler.value_count > kMaximumReflectedItems ||
                (sampler.value_count > 0 && states == nullptr))
            {
                throw std::runtime_error(
                    "Compiled effect: sampler state table is invalid or exceeds the "
                    "safety limit.");
            }
            for (unsigned int stateIndex = 0;
                 stateIndex < sampler.value_count; ++stateIndex)
            {
                const MOJOSHADER_effectValue& value = states[stateIndex].value;
                if (!IsTextureType(ReadEnumStorage(value.type.parameter_type)) ||
                    value.values == nullptr ||
                    value.value_count == 0)
                {
                    continue;
                }
                const int objectIndex = value.valuesI[0];
                if (objectIndex < 0 || objectIndex >= effectData->object_count) continue;
                const std::string textureName =
                    SafeString(effectData->objects[objectIndex].mapping.name);
                const auto found = texturesByName.find(textureName);
                if (found != texturesByName.end())
                {
                    samplerTextureParameters_[SafeString(sampler.name)] = found->second;
                }
                break;
            }
        }
        return samplerTextureParameters_;
    }

    void TranslateRenderStates(const MOJOSHADER_effectStateChanges& stateChanges,
                               const CompiledEffectDeviceState& deviceState,
                               CompiledEffectPassStateChanges& changes)
    {
        // FNA builds temporary pipeline-cache values and publishes each state group only after
        // every token has been translated. Keep the same transactional behavior: an unsupported
        // token must not corrupt the state the next draw uses.
        BlendState blend = deviceState.blend != nullptr ? *deviceState.blend : BlendState();
        DepthStencilState depth =
            deviceState.depthStencil != nullptr ? *deviceState.depthStencil : DepthStencilState();
        RasterizerState rasterizer =
            deviceState.rasterizer != nullptr ? *deviceState.rasterizer : RasterizerState();
        bool separateAlphaBlend =
            blend.getColorBlendFunctionProperty() != blend.getAlphaBlendFunctionProperty() ||
            blend.getColorDestinationBlendProperty() !=
                blend.getAlphaDestinationBlendProperty();
        bool blendChanged = false;
        bool depthChanged = false;
        bool rasterizerChanged = false;
        for (unsigned int i = 0; i < stateChanges.render_state_change_count; ++i)
        {
            const MOJOSHADER_effectState& state = stateChanges.render_state_changes[i];
            if (state.type == MOJOSHADER_RS_VERTEXSHADER ||
                state.type == MOJOSHADER_RS_PIXELSHADER)
            {
                continue;
            }
            if (state.value.values == nullptr || state.value.value_count == 0)
            {
                throw std::runtime_error(
                    "Compiled effect: render state value storage is missing.");
            }

            switch (state.type)
            {
                case MOJOSHADER_RS_ZENABLE:
                    depth.setDepthBufferEnableProperty(
                        *state.value.valuesZBT == MOJOSHADER_ZB_TRUE);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_FILLMODE:
                    if (*state.value.valuesFiM == MOJOSHADER_FILL_SOLID)
                        rasterizer.setFillModeProperty(FillMode::Solid);
                    else if (*state.value.valuesFiM == MOJOSHADER_FILL_WIREFRAME)
                        rasterizer.setFillModeProperty(FillMode::WireFrame);
                    else
                        throw std::runtime_error(
                            "Compiled effect: point fill mode is unsupported.");
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_ZWRITEENABLE:
                    depth.setDepthBufferWriteEnableProperty(state.value.valuesI[0] == 1);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_SRCBLEND:
                    blend.setColorSourceBlendProperty(ToBlend(*state.value.valuesBM));
                    if (!separateAlphaBlend)
                        blend.setAlphaSourceBlendProperty(blend.getColorSourceBlendProperty());
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_DESTBLEND:
                    blend.setColorDestinationBlendProperty(ToBlend(*state.value.valuesBM));
                    if (!separateAlphaBlend)
                    {
                        blend.setAlphaDestinationBlendProperty(
                            blend.getColorDestinationBlendProperty());
                    }
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_CULLMODE:
                    rasterizer.setCullModeProperty(ToCullMode(*state.value.valuesCM));
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_ZFUNC:
                    depth.setDepthBufferFunctionProperty(ToCompare(*state.value.valuesCF));
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_ALPHABLENDENABLE:
                    if (state.value.valuesI[0] == 0)
                    {
                        blend.setColorSourceBlendProperty(Blend::One);
                        blend.setColorDestinationBlendProperty(Blend::Zero);
                        blend.setAlphaSourceBlendProperty(Blend::One);
                        blend.setAlphaDestinationBlendProperty(Blend::Zero);
                        blendChanged = true;
                    }
                    break;
                case MOJOSHADER_RS_STENCILENABLE:
                    depth.setStencilEnableProperty(state.value.valuesI[0] == 1);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILFAIL:
                    depth.setStencilFailProperty(ToStencil(*state.value.valuesSO));
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILZFAIL:
                    depth.setStencilDepthBufferFailProperty(ToStencil(*state.value.valuesSO));
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILPASS:
                    depth.setStencilPassProperty(ToStencil(*state.value.valuesSO));
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILFUNC:
                    depth.setStencilFunctionProperty(ToCompare(*state.value.valuesCF));
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILREF:
                    depth.setReferenceStencilProperty(state.value.valuesI[0]);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILMASK:
                    depth.setStencilMaskProperty(state.value.valuesI[0]);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILWRITEMASK:
                    depth.setStencilWriteMaskProperty(state.value.valuesI[0]);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_MULTISAMPLEANTIALIAS:
                    rasterizer.setMultiSampleAntiAliasProperty(state.value.valuesI[0] == 1);
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_MULTISAMPLEMASK:
                    blend.setMultiSampleMaskProperty(state.value.valuesI[0]);
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_COLORWRITEENABLE:
                    blend.setColorWriteChannelsProperty(
                        static_cast<ColorWriteChannels>(state.value.valuesI[0] & 15));
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_BLENDOP:
                    blend.setColorBlendFunctionProperty(
                        ToBlendFunction(*state.value.valuesBO));
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_SCISSORTESTENABLE:
                    rasterizer.setScissorTestEnableProperty(state.value.valuesI[0] == 1);
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_SLOPESCALEDEPTHBIAS:
                    rasterizer.setSlopeScaleDepthBiasProperty(state.value.valuesF[0]);
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_TWOSIDEDSTENCILMODE:
                    depth.setTwoSidedStencilModeProperty(state.value.valuesI[0] == 1);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_CCW_STENCILFAIL:
                    depth.setCounterClockwiseStencilFailProperty(
                        ToStencil(*state.value.valuesSO));
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_CCW_STENCILZFAIL:
                    depth.setCounterClockwiseStencilDepthBufferFailProperty(
                        ToStencil(*state.value.valuesSO));
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_CCW_STENCILPASS:
                    depth.setCounterClockwiseStencilPassProperty(
                        ToStencil(*state.value.valuesSO));
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_CCW_STENCILFUNC:
                    depth.setCounterClockwiseStencilFunctionProperty(
                        ToCompare(*state.value.valuesCF));
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_COLORWRITEENABLE1:
                    blend.setColorWriteChannels1Property(
                        static_cast<ColorWriteChannels>(state.value.valuesI[0] & 15));
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_COLORWRITEENABLE2:
                    blend.setColorWriteChannels2Property(
                        static_cast<ColorWriteChannels>(state.value.valuesI[0] & 15));
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_COLORWRITEENABLE3:
                    blend.setColorWriteChannels3Property(
                        static_cast<ColorWriteChannels>(state.value.valuesI[0] & 15));
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_BLENDFACTOR:
                {
                    const std::uint32_t color =
                        static_cast<std::uint32_t>(state.value.valuesI[0]);
                    // Match FNA Effect.cs exactly, including its historical byte ordering.
                    blend.setBlendFactorProperty(Microsoft::Xna::Framework::Color(
                        static_cast<int>((color >> 24) & 0xFF),
                        static_cast<int>((color >> 16) & 0xFF),
                        static_cast<int>((color >> 8) & 0xFF),
                        static_cast<int>(color & 0xFF)));
                    blendChanged = true;
                    break;
                }
                case MOJOSHADER_RS_DEPTHBIAS:
                    rasterizer.setDepthBiasProperty(state.value.valuesF[0]);
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_SEPARATEALPHABLENDENABLE:
                    separateAlphaBlend = state.value.valuesI[0] == 1;
                    break;
                case MOJOSHADER_RS_SRCBLENDALPHA:
                    blend.setAlphaSourceBlendProperty(ToBlend(*state.value.valuesBM));
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_DESTBLENDALPHA:
                    blend.setAlphaDestinationBlendProperty(ToBlend(*state.value.valuesBM));
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_BLENDOPALPHA:
                    blend.setAlphaBlendFunctionProperty(
                        ToBlendFunction(*state.value.valuesBO));
                    blendChanged = true;
                    break;
                default:
                    // FNA treats the legacy Effect compiler's undocumented "SetSampler" token
                    // as metadata; the actual sampler records are applied below.
                    if (static_cast<int>(state.type) == 178) break;
                    throw std::runtime_error(
                        "Compiled effect: unsupported render state " +
                        std::to_string(static_cast<int>(state.type)) + ".");
            }
        }
        if (blendChanged)
        {
            changes.blendChanged = true;
            changes.blend = blend;
        }
        if (depthChanged)
        {
            changes.depthStencilChanged = true;
            changes.depthStencil = depth;
        }
        if (rasterizerChanged)
        {
            changes.rasterizerChanged = true;
            changes.rasterizer = rasterizer;
        }
    }

    void TranslateSamplers(const MOJOSHADER_samplerStateRegister* changeList,
                           std::uint32_t count, bool vertexStage, std::size_t maxSlots,
                           const std::unordered_map<std::string, std::uint32_t>& samplerTextureParameters,
                           const std::vector<Texture*>& textures,
                           const CompiledEffectDeviceState& deviceState,
                           CompiledEffectPassStateChanges& changes)
    {
        const SamplerStateCollection* tracked = vertexStage
            ? deviceState.vertexSamplerStates : deviceState.samplerStates;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const MOJOSHADER_samplerStateRegister& change = changeList[i];
            // FNA skips a register that carries no sampler state at all, including its texture.
            if (change.sampler_state_count == 0) continue;
            const std::size_t slot = static_cast<std::size_t>(change.sampler_register);
            if (slot >= maxSlots ||
                slot >= static_cast<std::size_t>(SamplerStateCollection::MaxSamplers))
            {
                throw std::runtime_error(
                    "Compiled effect: sampler register exceeds CNA's XNA slot limit.");
            }
            SamplerState sampler = tracked != nullptr ? (*tracked)[static_cast<int>(slot)]
                                                      : SamplerState();
            MOJOSHADER_textureFilterType mag;
            MOJOSHADER_textureFilterType min;
            MOJOSHADER_textureFilterType mip;
            FromFilter(sampler.getFilterProperty(), mag, min, mip);
            bool filterChanged = false;
            bool samplerChanged = false;
            bool textureAssigned = false;

            if (change.sampler_state_count > kMaximumReflectedItems ||
                (change.sampler_state_count > 0 && change.sampler_states == nullptr))
            {
                throw std::runtime_error(
                    "Compiled effect: sampler state changes exceed the safety limit.");
            }

            for (unsigned int stateIndex = 0;
                 stateIndex < change.sampler_state_count; ++stateIndex)
            {
                const MOJOSHADER_effectSamplerState& state = change.sampler_states[stateIndex];
                if (state.value.values == nullptr || state.value.value_count == 0)
                {
                    throw std::runtime_error(
                        "Compiled effect: sampler state value storage is missing.");
                }
                switch (state.type)
                {
                    case MOJOSHADER_SAMP_TEXTURE: textureAssigned = true; break;
                    case MOJOSHADER_SAMP_ADDRESSU:
                        sampler.setAddressUProperty(ToAddress(*state.value.valuesTA));
                        samplerChanged = true;
                        break;
                    case MOJOSHADER_SAMP_ADDRESSV:
                        sampler.setAddressVProperty(ToAddress(*state.value.valuesTA));
                        samplerChanged = true;
                        break;
                    case MOJOSHADER_SAMP_ADDRESSW:
                        sampler.setAddressWProperty(ToAddress(*state.value.valuesTA));
                        samplerChanged = true;
                        break;
                    case MOJOSHADER_SAMP_MAGFILTER:
                        mag = *state.value.valuesTFT; filterChanged = true; break;
                    case MOJOSHADER_SAMP_MINFILTER:
                        min = *state.value.valuesTFT; filterChanged = true; break;
                    case MOJOSHADER_SAMP_MIPFILTER:
                        mip = *state.value.valuesTFT; filterChanged = true; break;
                    case MOJOSHADER_SAMP_MIPMAPLODBIAS:
                        sampler.setMipMapLevelOfDetailBiasProperty(state.value.valuesF[0]);
                        samplerChanged = true;
                        break;
                    case MOJOSHADER_SAMP_MAXMIPLEVEL:
                        sampler.setMaxMipLevelProperty(state.value.valuesI[0]);
                        samplerChanged = true;
                        break;
                    case MOJOSHADER_SAMP_MAXANISOTROPY:
                        sampler.setMaxAnisotropyProperty(state.value.valuesI[0]);
                        samplerChanged = true;
                        break;
                    default:
                        throw std::runtime_error(
                            "Compiled effect: unsupported sampler state " +
                            std::to_string(static_cast<int>(state.type)) + ".");
                }
            }
            if (filterChanged)
            {
                sampler.setFilterProperty(ToFilter(mag, min, mip));
                samplerChanged = true;
            }

            // FNA rebinds a sampler's texture only when the pass assigns SAMP_TEXTURE and the
            // reflected texture parameter actually holds one.
            Texture* texture = nullptr;
            if (textureAssigned)
            {
                const auto mapped =
                    samplerTextureParameters.find(SafeString(change.sampler_name));
                if (mapped != samplerTextureParameters.end() &&
                    mapped->second < textures.size())
                {
                    texture = textures[mapped->second];
                }
            }
            const bool textureChanged = texture != nullptr;
            if (!samplerChanged && !textureChanged) continue;

            CompiledEffectSamplerChange result;
            result.slot = static_cast<std::uint32_t>(slot);
            result.vertexStage = vertexStage;
            result.samplerChanged = samplerChanged;
            result.sampler = sampler;
            result.textureChanged = textureChanged;
            result.texture = texture;
            changes.samplers.push_back(std::move(result));
        }
    }
}
