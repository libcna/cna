// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dCompiledEffect.hpp"

#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace CNA::Internal::Renderers::Fna3d
{
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
                        "FNA3D compiled effect: unsupported MojoShader parameter class " +
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
                        "FNA3D compiled effect: unsupported public MojoShader parameter type " +
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
                    "FNA3D compiled effect: a string parameter references an invalid object "
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
                    "FNA3D compiled effect: reflected value exceeds the safety limit.");
            }
            const std::size_t byteCount = static_cast<std::size_t>(value.value_count) * 4;
            if (byteCount > kMaximumReflectedValueBytes - reflectedValueBytes)
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: aggregate reflected values exceed the safety limit.");
            }
            reflectedValueBytes += byteCount;
            if (byteCount > 0 && value.values == nullptr)
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: reflected value storage is missing.");
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
                    "FNA3D compiled effect: annotation count exceeds the safety limit.");
            }
            if (count > 0 && annotations == nullptr)
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: annotation storage is missing.");
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
                    "FNA3D compiled effect: structure nesting exceeds the safety limit.");
            }
            if (reflectedItemCount >= kMaximumReflectedItems)
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: structure member count exceeds the safety limit.");
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
                    "FNA3D compiled effect: structure member storage is missing.");
            }
            result.structureMembers.reserve(member.info.member_count);
            for (unsigned int i = 0; i < member.info.member_count; ++i)
            {
                result.structureMembers.push_back(
                    DescribeMember(member.info.members[i], depth + 1, reflectedItemCount));
            }
            return result;
        }

        FNA3D_Blend ToBlend(MOJOSHADER_blendMode value)
        {
            switch (value)
            {
                case MOJOSHADER_BLEND_ZERO:           return FNA3D_BLEND_ZERO;
                case MOJOSHADER_BLEND_ONE:            return FNA3D_BLEND_ONE;
                case MOJOSHADER_BLEND_SRCCOLOR:       return FNA3D_BLEND_SOURCECOLOR;
                case MOJOSHADER_BLEND_INVSRCCOLOR:    return FNA3D_BLEND_INVERSESOURCECOLOR;
                case MOJOSHADER_BLEND_SRCALPHA:       return FNA3D_BLEND_SOURCEALPHA;
                case MOJOSHADER_BLEND_INVSRCALPHA:    return FNA3D_BLEND_INVERSESOURCEALPHA;
                case MOJOSHADER_BLEND_DESTALPHA:      return FNA3D_BLEND_DESTINATIONALPHA;
                case MOJOSHADER_BLEND_INVDESTALPHA:   return FNA3D_BLEND_INVERSEDESTINATIONALPHA;
                case MOJOSHADER_BLEND_DESTCOLOR:      return FNA3D_BLEND_DESTINATIONCOLOR;
                case MOJOSHADER_BLEND_INVDESTCOLOR:   return FNA3D_BLEND_INVERSEDESTINATIONCOLOR;
                case MOJOSHADER_BLEND_SRCALPHASAT:    return FNA3D_BLEND_SOURCEALPHASATURATION;
                case MOJOSHADER_BLEND_BLENDFACTOR:    return FNA3D_BLEND_BLENDFACTOR;
                case MOJOSHADER_BLEND_INVBLENDFACTOR: return FNA3D_BLEND_INVERSEBLENDFACTOR;
                default:
                    throw std::runtime_error(
                        "FNA3D compiled effect: unsupported D3D9 blend mode " +
                        std::to_string(static_cast<int>(value)) + ".");
            }
        }

        FNA3D_BlendFunction ToBlendFunction(MOJOSHADER_blendOp value)
        {
            switch (value)
            {
                case MOJOSHADER_BLENDOP_ADD:         return FNA3D_BLENDFUNCTION_ADD;
                case MOJOSHADER_BLENDOP_SUBTRACT:    return FNA3D_BLENDFUNCTION_SUBTRACT;
                case MOJOSHADER_BLENDOP_REVSUBTRACT: return FNA3D_BLENDFUNCTION_REVERSESUBTRACT;
                case MOJOSHADER_BLENDOP_MIN:         return FNA3D_BLENDFUNCTION_MIN;
                case MOJOSHADER_BLENDOP_MAX:         return FNA3D_BLENDFUNCTION_MAX;
                default:
                    throw std::runtime_error(
                        "FNA3D compiled effect: unsupported D3D9 blend operation.");
            }
        }

        FNA3D_CompareFunction ToCompare(MOJOSHADER_compareFunc value)
        {
            switch (value)
            {
                case MOJOSHADER_CMP_NEVER:        return FNA3D_COMPAREFUNCTION_NEVER;
                case MOJOSHADER_CMP_LESS:         return FNA3D_COMPAREFUNCTION_LESS;
                case MOJOSHADER_CMP_EQUAL:        return FNA3D_COMPAREFUNCTION_EQUAL;
                case MOJOSHADER_CMP_LESSEQUAL:    return FNA3D_COMPAREFUNCTION_LESSEQUAL;
                case MOJOSHADER_CMP_GREATER:      return FNA3D_COMPAREFUNCTION_GREATER;
                case MOJOSHADER_CMP_NOTEQUAL:     return FNA3D_COMPAREFUNCTION_NOTEQUAL;
                case MOJOSHADER_CMP_GREATEREQUAL: return FNA3D_COMPAREFUNCTION_GREATEREQUAL;
                case MOJOSHADER_CMP_ALWAYS:       return FNA3D_COMPAREFUNCTION_ALWAYS;
                default:
                    throw std::runtime_error(
                        "FNA3D compiled effect: unsupported D3D9 comparison function.");
            }
        }

        FNA3D_StencilOperation ToStencil(MOJOSHADER_stencilOp value)
        {
            switch (value)
            {
                case MOJOSHADER_STENCILOP_KEEP:    return FNA3D_STENCILOPERATION_KEEP;
                case MOJOSHADER_STENCILOP_ZERO:    return FNA3D_STENCILOPERATION_ZERO;
                case MOJOSHADER_STENCILOP_REPLACE: return FNA3D_STENCILOPERATION_REPLACE;
                case MOJOSHADER_STENCILOP_INCRSAT: return FNA3D_STENCILOPERATION_INCREMENTSATURATION;
                case MOJOSHADER_STENCILOP_DECRSAT: return FNA3D_STENCILOPERATION_DECREMENTSATURATION;
                case MOJOSHADER_STENCILOP_INVERT:  return FNA3D_STENCILOPERATION_INVERT;
                case MOJOSHADER_STENCILOP_INCR:    return FNA3D_STENCILOPERATION_INCREMENT;
                case MOJOSHADER_STENCILOP_DECR:    return FNA3D_STENCILOPERATION_DECREMENT;
                default:
                    throw std::runtime_error(
                        "FNA3D compiled effect: unsupported D3D9 stencil operation.");
            }
        }

        FNA3D_TextureAddressMode ToAddress(MOJOSHADER_textureAddress value)
        {
            switch (value)
            {
                case MOJOSHADER_TADDRESS_WRAP:   return FNA3D_TEXTUREADDRESSMODE_WRAP;
                case MOJOSHADER_TADDRESS_MIRROR: return FNA3D_TEXTUREADDRESSMODE_MIRROR;
                case MOJOSHADER_TADDRESS_CLAMP:  return FNA3D_TEXTUREADDRESSMODE_CLAMP;
                default:
                    throw std::runtime_error(
                        "FNA3D compiled effect: Border and MirrorOnce sampler addressing are "
                        "not representable by XNA 4.0 SamplerState.");
            }
        }

        FNA3D_TextureFilter ToFilter(MOJOSHADER_textureFilterType mag,
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
                    "FNA3D compiled effect: unsupported sampler filter component.");
            }
            if (magLinear && minLinear)
                return mipLinear ? FNA3D_TEXTUREFILTER_LINEAR :
                                   FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT;
            if (magPoint && minPoint)
                return mipLinear ? FNA3D_TEXTUREFILTER_POINT_MIPLINEAR :
                                   FNA3D_TEXTUREFILTER_POINT;
            if (magPoint && minLinear)
                return mipLinear ? FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR :
                                   FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT;
            return mipLinear ? FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR :
                               FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT;
        }

        void FromFilter(FNA3D_TextureFilter value, MOJOSHADER_textureFilterType& mag,
                        MOJOSHADER_textureFilterType& min,
                        MOJOSHADER_textureFilterType& mip)
        {
            using Filter = MOJOSHADER_textureFilterType;
            constexpr Filter point = MOJOSHADER_TEXTUREFILTER_POINT;
            constexpr Filter linear = MOJOSHADER_TEXTUREFILTER_LINEAR;
            switch (value)
            {
                case FNA3D_TEXTUREFILTER_LINEAR: mag = linear; min = linear; mip = linear; break;
                case FNA3D_TEXTUREFILTER_POINT: mag = point; min = point; mip = point; break;
                case FNA3D_TEXTUREFILTER_ANISOTROPIC:
                    mag = MOJOSHADER_TEXTUREFILTER_ANISOTROPIC;
                    min = MOJOSHADER_TEXTUREFILTER_ANISOTROPIC;
                    mip = MOJOSHADER_TEXTUREFILTER_ANISOTROPIC;
                    break;
                case FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT:
                    mag = linear; min = linear; mip = point; break;
                case FNA3D_TEXTUREFILTER_POINT_MIPLINEAR:
                    mag = point; min = point; mip = linear; break;
                case FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR:
                    mag = point; min = linear; mip = linear; break;
                case FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT:
                    mag = point; min = linear; mip = point; break;
                case FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR:
                    mag = linear; min = point; mip = linear; break;
                case FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT:
                    mag = linear; min = point; mip = point; break;
                default:
                    throw std::runtime_error(
                        "FNA3D compiled effect: current sampler filter is invalid.");
            }
        }

        FNA3D_CullMode ToCullMode(MOJOSHADER_cullMode value)
        {
            switch (value)
            {
                case MOJOSHADER_CULL_NONE: return FNA3D_CULLMODE_NONE;
                case MOJOSHADER_CULL_CW: return FNA3D_CULLMODE_CULLCLOCKWISEFACE;
                case MOJOSHADER_CULL_CCW: return FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE;
                default:
                    throw std::runtime_error(
                        "FNA3D compiled effect: unsupported cull mode.");
            }
        }

        const Fna3dSampledTexture* GetSampledTexture(Texture* texture)
        {
            if (texture == nullptr) return nullptr;
            if (auto* texture2D = dynamic_cast<Microsoft::Xna::Framework::Graphics::Texture2D*>(texture))
            {
                return dynamic_cast<const Fna3dSampledTexture*>(&texture2D->GetRenderer());
            }
            if (auto* texture3D = dynamic_cast<Microsoft::Xna::Framework::Graphics::Texture3D*>(texture))
            {
                return dynamic_cast<const Fna3dSampledTexture*>(&texture3D->GetRenderer());
            }
            if (auto* textureCube = dynamic_cast<Microsoft::Xna::Framework::Graphics::TextureCube*>(texture))
            {
                return dynamic_cast<const Fna3dSampledTexture*>(&textureCube->GetRenderer());
            }
            return nullptr;
        }

        bool CanSafelyDisposeNativeEffect(const MOJOSHADER_effect* effect)
        {
            // Several MojoShader parse failures are represented by static sentinel objects.
            // Their callback context is zeroed and MOJOSHADER_deleteEffect is not safe for all of
            // them in the FNA3D-pinned revision. A normally allocated parse tree always owns a
            // resolved free callback.
            return effect != nullptr && effect->ctx.f != nullptr;
        }

        void DisposeFailedNativeEffect(FNA3D_Device* device, FNA3D_Effect* effect,
                                       const MOJOSHADER_effect* effectData)
        {
            if (effect == nullptr) return;
            if (CanSafelyDisposeNativeEffect(effectData))
            {
                FNA3D_AddDisposeEffect(device, effect);
                return;
            }

            // Every effect wrapper in CNA's pinned FNA3D drivers is allocated with SDL_malloc.
            // Calling the public disposal entry point for an unexpected-EOF sentinel would make
            // FNA3D call MOJOSHADER_deleteEffect on static storage. Free only the driver wrapper
            // in this narrowly identified failure case; no shaders or parser allocations exist.
            SDL_free(effect);
        }
    }

    Fna3dCompiledEffect::Fna3dCompiledEffect(Fna3dRenderer& renderer,
                                             const std::uint8_t* effectCode,
                                             std::size_t effectCodeLength)
        : renderer_(renderer)
        , device_(renderer.GetDeviceEXT())
    {
        if (effectCode == nullptr || effectCodeLength == 0 ||
            effectCodeLength > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("FNA3D compiled effect: invalid bytecode buffer.");
        }
        FNA3D_CreateEffect(device_, const_cast<std::uint8_t*>(effectCode),
                           static_cast<std::uint32_t>(effectCodeLength), &effect_, &effectData_);
        try
        {
            ValidateNativeEffect("create");
            BuildDescription();
            BuildSamplerMap();
            textures_.resize(static_cast<std::size_t>(effectData_->param_count), nullptr);
            SetTechnique(0);
        }
        catch (...)
        {
            DisposeFailedNativeEffect(device_, effect_, effectData_);
            effect_ = nullptr;
            effectData_ = nullptr;
            throw;
        }
    }

    Fna3dCompiledEffect::Fna3dCompiledEffect(Fna3dRenderer& renderer,
                                             const Fna3dCompiledEffect& cloneSource)
        : renderer_(renderer)
        , device_(renderer.GetDeviceEXT())
        , techniqueIndex_(cloneSource.techniqueIndex_)
    {
        FNA3D_CloneEffect(device_, cloneSource.effect_, &effect_, &effectData_);
        try
        {
            ValidateNativeEffect("clone");
            BuildDescription();
            BuildSamplerMap();
            textures_ = cloneSource.textures_;
            SetTechnique(techniqueIndex_);
        }
        catch (...)
        {
            DisposeFailedNativeEffect(device_, effect_, effectData_);
            effect_ = nullptr;
            effectData_ = nullptr;
            throw;
        }
    }

    Fna3dCompiledEffect::~Fna3dCompiledEffect()
    {
        if (effect_ != nullptr && device_ != nullptr)
        {
            FNA3D_AddDisposeEffect(device_, effect_);
        }
        effect_ = nullptr;
        effectData_ = nullptr;
    }

    std::unique_ptr<ICompiledEffectRuntime> Fna3dCompiledEffect::Clone() const
    {
        return std::unique_ptr<ICompiledEffectRuntime>(
            new Fna3dCompiledEffect(renderer_, *this));
    }

    const CompiledEffectDescription& Fna3dCompiledEffect::GetDescription() const
    {
        return description_;
    }

    void Fna3dCompiledEffect::ValidateNativeEffect(const char* operation)
    {
        if (effect_ == nullptr || effectData_ == nullptr)
        {
            throw std::runtime_error(
                std::string("FNA3D compiled effect: MojoShader failed to ") + operation +
                " the Effect Framework bytecode.");
        }
        if (effectData_->error_count > 0)
        {
            std::ostringstream message;
            message << "FNA3D compiled effect: MojoShader reported " << effectData_->error_count
                    << " error(s) while attempting to " << operation << " the bytecode";
            const int reported = effectData_->errors != nullptr
                                     ? std::min(effectData_->error_count, 8)
                                     : 0;
            for (int i = 0; i < reported; ++i)
            {
                message << (i == 0 ? ": " : "; ")
                        << SafeString(effectData_->errors[i].error);
            }
            throw std::runtime_error(message.str());
        }
        if (effectData_->param_count < 0 ||
            static_cast<std::size_t>(effectData_->param_count) > kMaximumReflectedItems ||
            (effectData_->param_count > 0 && effectData_->params == nullptr))
        {
            throw std::runtime_error(
                "FNA3D compiled effect: parameter table is invalid or exceeds the safety limit.");
        }
        if (effectData_->object_count < 0 ||
            static_cast<std::size_t>(effectData_->object_count) > kMaximumReflectedItems ||
            (effectData_->object_count > 0 && effectData_->objects == nullptr))
        {
            throw std::runtime_error(
                "FNA3D compiled effect: object table is invalid or exceeds the safety limit.");
        }
        if (effectData_->technique_count < 1 ||
            static_cast<std::size_t>(effectData_->technique_count) > kMaximumReflectedItems ||
            effectData_->techniques == nullptr)
        {
            throw std::runtime_error(
                "FNA3D compiled effect: technique table is invalid or exceeds the safety limit.");
        }
    }

    void Fna3dCompiledEffect::BuildDescription()
    {
        description_ = {};
        std::size_t reflectedItemCount = 0;
        std::size_t reflectedValueBytes = 0;
        description_.parameters.reserve(static_cast<std::size_t>(effectData_->param_count));
        for (int i = 0; i < effectData_->param_count; ++i)
        {
            const MOJOSHADER_effectParam& parameter = effectData_->params[i];
            const auto type = ReadEnumStorage(parameter.value.type.parameter_type);
            if (IsSamplerType(type) || IsShaderObjectType(type)) continue;

            CompiledEffectParameterDescription result;
            static_cast<CompiledEffectValueDescription&>(result) =
                DescribeValue(effectData_, parameter.value, reflectedValueBytes);
            result.runtimeIndex = static_cast<std::uint32_t>(i);
            result.annotations = DescribeAnnotations(effectData_, parameter.annotations,
                                                     parameter.annotation_count,
                                                     reflectedItemCount,
                                                     reflectedValueBytes);
            if (parameter.value.type.member_count > 0 &&
                parameter.value.type.members == nullptr)
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: top-level structure member storage is missing.");
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

        description_.techniques.reserve(static_cast<std::size_t>(effectData_->technique_count));
        for (int i = 0; i < effectData_->technique_count; ++i)
        {
            const MOJOSHADER_effectTechnique& technique = effectData_->techniques[i];
            CompiledEffectTechniqueDescription reflected;
            reflected.name = SafeString(technique.name);
            reflected.annotations = DescribeAnnotations(effectData_, technique.annotations,
                                                         technique.annotation_count,
                                                         reflectedItemCount,
                                                         reflectedValueBytes);
            if (technique.pass_count > kMaximumReflectedItems - reflectedItemCount ||
                (technique.pass_count > 0 && technique.passes == nullptr))
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: pass table is invalid or exceeds the safety limit.");
            }
            reflectedItemCount += technique.pass_count;
            reflected.passes.reserve(technique.pass_count);
            for (unsigned int passIndex = 0; passIndex < technique.pass_count; ++passIndex)
            {
                const MOJOSHADER_effectPass& pass = technique.passes[passIndex];
                CompiledEffectPassDescription reflectedPass;
                reflectedPass.name = SafeString(pass.name);
                reflectedPass.annotations = DescribeAnnotations(effectData_, pass.annotations,
                                                                 pass.annotation_count,
                                                                 reflectedItemCount,
                                                                 reflectedValueBytes);
                reflected.passes.push_back(std::move(reflectedPass));
            }
            if (reflected.passes.empty())
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: technique '" + reflected.name +
                    "' declares no passes.");
            }
            description_.techniques.push_back(std::move(reflected));
        }
    }

    void Fna3dCompiledEffect::BuildSamplerMap()
    {
        samplerTextureParameters_.clear();
        std::unordered_map<std::string, std::uint32_t> texturesByName;
        for (int i = 0; i < effectData_->param_count; ++i)
        {
            const MOJOSHADER_effectValue& value = effectData_->params[i].value;
            if (IsTextureType(ReadEnumStorage(value.type.parameter_type)))
            {
                texturesByName[SafeString(value.name)] = static_cast<std::uint32_t>(i);
            }
        }

        for (int i = 0; i < effectData_->param_count; ++i)
        {
            const MOJOSHADER_effectValue& sampler = effectData_->params[i].value;
            if (!IsSamplerType(ReadEnumStorage(sampler.type.parameter_type))) continue;
            const auto* states = sampler.valuesSS;
            if (sampler.value_count > kMaximumReflectedItems ||
                (sampler.value_count > 0 && states == nullptr))
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: sampler state table is invalid or exceeds the "
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
                if (objectIndex < 0 || objectIndex >= effectData_->object_count) continue;
                const std::string textureName =
                    SafeString(effectData_->objects[objectIndex].mapping.name);
                const auto found = texturesByName.find(textureName);
                if (found != texturesByName.end())
                {
                    samplerTextureParameters_[SafeString(sampler.name)] = found->second;
                }
                break;
            }
        }
    }

    void Fna3dCompiledEffect::SetTechnique(std::uint32_t techniqueIndex)
    {
        if (techniqueIndex >= static_cast<std::uint32_t>(effectData_->technique_count))
        {
            throw std::out_of_range("FNA3D compiled effect: technique index is out of range.");
        }
        techniqueIndex_ = techniqueIndex;
        FNA3D_SetEffectTechnique(device_, effect_, &effectData_->techniques[techniqueIndex]);
    }

    void Fna3dCompiledEffect::SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                                                std::size_t dataBytes)
    {
        if (runtimeIndex >= static_cast<std::uint32_t>(effectData_->param_count))
            throw std::out_of_range("FNA3D compiled effect: parameter index is out of range.");
        MOJOSHADER_effectParam& parameter = effectData_->params[runtimeIndex];
        const std::size_t capacity = static_cast<std::size_t>(parameter.value.value_count) * 4;
        if (dataBytes > capacity)
            throw std::invalid_argument("FNA3D compiled effect: parameter value is too large.");
        if (dataBytes > 0 && data == nullptr)
            throw std::invalid_argument("FNA3D compiled effect: parameter data is null.");
        if (dataBytes > 0)
        {
            MOJOSHADER_effectSetRawValueHandle(&parameter, data, 0,
                                               static_cast<unsigned int>(dataBytes));
        }
    }

    void Fna3dCompiledEffect::SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture)
    {
        if (runtimeIndex >= textures_.size())
            throw std::out_of_range("FNA3D compiled effect: texture parameter index is out of range.");
        if (!IsTextureType(ReadEnumStorage(
                effectData_->params[runtimeIndex].value.type.parameter_type)))
            throw std::invalid_argument("FNA3D compiled effect: parameter is not a texture.");
        if (texture != nullptr && GetSampledTexture(texture) == nullptr)
            throw std::invalid_argument(
                "FNA3D compiled effect: texture was not created by the active FNA3D renderer.");
        textures_[runtimeIndex] = texture;
    }

    void Fna3dCompiledEffect::ApplyPass(std::uint32_t passIndex)
    {
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("FNA3D compiled effect: pass index is out of range.");

        std::memset(&stateChanges_, 0, sizeof(stateChanges_));
        FNA3D_ApplyEffect(device_, effect_, passIndex, &stateChanges_);
        if (stateChanges_.render_state_change_count > kMaximumReflectedItems ||
            (stateChanges_.render_state_change_count > 0 &&
             stateChanges_.render_state_changes == nullptr) ||
            stateChanges_.sampler_state_change_count > kMaximumReflectedItems ||
            (stateChanges_.sampler_state_change_count > 0 &&
             stateChanges_.sampler_state_changes == nullptr) ||
            stateChanges_.vertex_sampler_state_change_count > kMaximumReflectedItems ||
            (stateChanges_.vertex_sampler_state_change_count > 0 &&
             stateChanges_.vertex_sampler_state_changes == nullptr))
        {
            throw std::runtime_error(
                "FNA3D compiled effect: native pass state changes exceed the safety limit.");
        }
        ApplyRenderStates();
        ApplySamplers(stateChanges_.sampler_state_changes,
                      stateChanges_.sampler_state_change_count, false);
        ApplySamplers(stateChanges_.vertex_sampler_state_changes,
                      stateChanges_.vertex_sampler_state_change_count, true);
    }

    void Fna3dCompiledEffect::ApplyRenderStates()
    {
        // FNA builds temporary pipeline-cache values and publishes each state group only after
        // every token has been translated. Keep the same transactional behavior: an unsupported
        // token must not corrupt the renderer's tracked state for the next draw.
        FNA3D_BlendState blend = renderer_.blendState_;
        FNA3D_DepthStencilState depth = renderer_.depthStencilState_;
        FNA3D_RasterizerState rasterizer = renderer_.rasterizerState_;
        bool separateAlphaBlend =
            blend.colorBlendFunction != blend.alphaBlendFunction ||
            blend.colorDestinationBlend != blend.alphaDestinationBlend;
        bool blendChanged = false;
        bool depthChanged = false;
        bool rasterizerChanged = false;
        for (unsigned int i = 0; i < stateChanges_.render_state_change_count; ++i)
        {
            const MOJOSHADER_effectState& state = stateChanges_.render_state_changes[i];
            if (state.type == MOJOSHADER_RS_VERTEXSHADER ||
                state.type == MOJOSHADER_RS_PIXELSHADER)
            {
                continue;
            }
            if (state.value.values == nullptr || state.value.value_count == 0)
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: render state value storage is missing.");
            }

            switch (state.type)
            {
                case MOJOSHADER_RS_ZENABLE:
                    depth.depthBufferEnable =
                        (*state.value.valuesZBT == MOJOSHADER_ZB_TRUE) ? 1 : 0;
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_FILLMODE:
                    if (*state.value.valuesFiM == MOJOSHADER_FILL_SOLID)
                        rasterizer.fillMode = FNA3D_FILLMODE_SOLID;
                    else if (*state.value.valuesFiM == MOJOSHADER_FILL_WIREFRAME)
                        rasterizer.fillMode = FNA3D_FILLMODE_WIREFRAME;
                    else
                        throw std::runtime_error(
                            "FNA3D compiled effect: point fill mode is unsupported.");
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_ZWRITEENABLE:
                    depth.depthBufferWriteEnable =
                        state.value.valuesI[0] == 1;
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_SRCBLEND:
                    blend.colorSourceBlend = ToBlend(*state.value.valuesBM);
                    if (!separateAlphaBlend)
                        blend.alphaSourceBlend = blend.colorSourceBlend;
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_DESTBLEND:
                    blend.colorDestinationBlend = ToBlend(*state.value.valuesBM);
                    if (!separateAlphaBlend)
                        blend.alphaDestinationBlend = blend.colorDestinationBlend;
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_CULLMODE:
                    rasterizer.cullMode = ToCullMode(*state.value.valuesCM);
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_ZFUNC:
                    depth.depthBufferFunction =
                        ToCompare(*state.value.valuesCF);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_ALPHABLENDENABLE:
                    if (state.value.valuesI[0] == 0)
                    {
                        blend.colorSourceBlend = FNA3D_BLEND_ONE;
                        blend.colorDestinationBlend = FNA3D_BLEND_ZERO;
                        blend.alphaSourceBlend = FNA3D_BLEND_ONE;
                        blend.alphaDestinationBlend = FNA3D_BLEND_ZERO;
                        blendChanged = true;
                    }
                    break;
                case MOJOSHADER_RS_STENCILENABLE:
                    depth.stencilEnable = state.value.valuesI[0] == 1;
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILFAIL:
                    depth.stencilFail = ToStencil(*state.value.valuesSO);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILZFAIL:
                    depth.stencilDepthBufferFail =
                        ToStencil(*state.value.valuesSO);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILPASS:
                    depth.stencilPass = ToStencil(*state.value.valuesSO);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILFUNC:
                    depth.stencilFunction =
                        ToCompare(*state.value.valuesCF);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILREF:
                    depth.referenceStencil = state.value.valuesI[0];
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILMASK:
                    depth.stencilMask = state.value.valuesI[0];
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_STENCILWRITEMASK:
                    depth.stencilWriteMask = state.value.valuesI[0];
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_MULTISAMPLEANTIALIAS:
                    rasterizer.multiSampleAntiAlias =
                        state.value.valuesI[0] == 1;
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_MULTISAMPLEMASK:
                    blend.multiSampleMask = state.value.valuesI[0];
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_COLORWRITEENABLE:
                    blend.colorWriteEnable =
                        static_cast<FNA3D_ColorWriteChannels>(state.value.valuesI[0] & 15);
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_BLENDOP:
                    blend.colorBlendFunction =
                        ToBlendFunction(*state.value.valuesBO);
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_SCISSORTESTENABLE:
                    rasterizer.scissorTestEnable = state.value.valuesI[0] == 1;
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_SLOPESCALEDEPTHBIAS:
                    rasterizer.slopeScaleDepthBias = state.value.valuesF[0];
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_TWOSIDEDSTENCILMODE:
                    depth.twoSidedStencilMode =
                        state.value.valuesI[0] == 1;
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_CCW_STENCILFAIL:
                    depth.ccwStencilFail =
                        ToStencil(*state.value.valuesSO);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_CCW_STENCILZFAIL:
                    depth.ccwStencilDepthBufferFail =
                        ToStencil(*state.value.valuesSO);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_CCW_STENCILPASS:
                    depth.ccwStencilPass =
                        ToStencil(*state.value.valuesSO);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_CCW_STENCILFUNC:
                    depth.ccwStencilFunction =
                        ToCompare(*state.value.valuesCF);
                    depthChanged = true;
                    break;
                case MOJOSHADER_RS_COLORWRITEENABLE1:
                    blend.colorWriteEnable1 =
                        static_cast<FNA3D_ColorWriteChannels>(state.value.valuesI[0] & 15);
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_COLORWRITEENABLE2:
                    blend.colorWriteEnable2 =
                        static_cast<FNA3D_ColorWriteChannels>(state.value.valuesI[0] & 15);
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_COLORWRITEENABLE3:
                    blend.colorWriteEnable3 =
                        static_cast<FNA3D_ColorWriteChannels>(state.value.valuesI[0] & 15);
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_BLENDFACTOR:
                {
                    const std::uint32_t color =
                        static_cast<std::uint32_t>(state.value.valuesI[0]);
                    // Match FNA Effect.cs exactly, including its historical byte ordering.
                    blend.blendFactor = {
                        static_cast<std::uint8_t>((color >> 24) & 0xFF),
                        static_cast<std::uint8_t>((color >> 16) & 0xFF),
                        static_cast<std::uint8_t>((color >> 8) & 0xFF),
                        static_cast<std::uint8_t>(color & 0xFF)
                    };
                    blendChanged = true;
                    break;
                }
                case MOJOSHADER_RS_DEPTHBIAS:
                    rasterizer.depthBias = state.value.valuesF[0];
                    rasterizerChanged = true;
                    break;
                case MOJOSHADER_RS_SEPARATEALPHABLENDENABLE:
                    separateAlphaBlend = state.value.valuesI[0] == 1;
                    break;
                case MOJOSHADER_RS_SRCBLENDALPHA:
                    blend.alphaSourceBlend = ToBlend(*state.value.valuesBM);
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_DESTBLENDALPHA:
                    blend.alphaDestinationBlend = ToBlend(*state.value.valuesBM);
                    blendChanged = true;
                    break;
                case MOJOSHADER_RS_BLENDOPALPHA:
                    blend.alphaBlendFunction =
                        ToBlendFunction(*state.value.valuesBO);
                    blendChanged = true;
                    break;
                default:
                    // FNA treats the legacy Effect compiler's undocumented "SetSampler" token
                    // as metadata; the actual sampler records are applied below.
                    if (static_cast<int>(state.type) == 178) break;
                    throw std::runtime_error(
                        "FNA3D compiled effect: unsupported render state " +
                        std::to_string(static_cast<int>(state.type)) + ".");
            }
        }
        if (blendChanged)
        {
            renderer_.blendState_ = blend;
            renderer_.ApplyCurrentBlendState();
        }
        if (depthChanged)
        {
            renderer_.depthStencilState_ = depth;
            renderer_.ApplyCurrentDepthStencilState();
        }
        if (rasterizerChanged)
        {
            renderer_.rasterizerState_ = rasterizer;
            FNA3D_ApplyRasterizerState(device_, &renderer_.rasterizerState_);
        }
    }

    void Fna3dCompiledEffect::ApplySamplers(const MOJOSHADER_samplerStateRegister* changes,
                                            std::uint32_t count, bool vertexStage)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const MOJOSHADER_samplerStateRegister& change = changes[i];
            const std::size_t slot = static_cast<std::size_t>(change.sampler_register);
            const std::size_t samplerCount = vertexStage
                ? renderer_.vertexSamplerStates_.size()
                : renderer_.samplerStates_.size();
            if (slot >= samplerCount)
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: sampler register exceeds CNA's XNA slot limit.");
            }
            auto& trackedSamplers = vertexStage
                ? renderer_.vertexSamplerStates_[slot]
                : renderer_.samplerStates_[slot];
            FNA3D_SamplerState sampler = trackedSamplers;
            MOJOSHADER_textureFilterType mag;
            MOJOSHADER_textureFilterType min;
            MOJOSHADER_textureFilterType mip;
            FromFilter(sampler.filter, mag, min, mip);
            bool filterChanged = false;

            if (change.sampler_state_count > kMaximumReflectedItems ||
                (change.sampler_state_count > 0 && change.sampler_states == nullptr))
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: sampler state changes exceed the safety limit.");
            }

            for (unsigned int stateIndex = 0;
                 stateIndex < change.sampler_state_count; ++stateIndex)
            {
                const MOJOSHADER_effectSamplerState& state = change.sampler_states[stateIndex];
                if (state.value.values == nullptr || state.value.value_count == 0)
                {
                    throw std::runtime_error(
                        "FNA3D compiled effect: sampler state value storage is missing.");
                }
                switch (state.type)
                {
                    case MOJOSHADER_SAMP_TEXTURE: break;
                    case MOJOSHADER_SAMP_ADDRESSU:
                        sampler.addressU = ToAddress(*state.value.valuesTA); break;
                    case MOJOSHADER_SAMP_ADDRESSV:
                        sampler.addressV = ToAddress(*state.value.valuesTA); break;
                    case MOJOSHADER_SAMP_ADDRESSW:
                        sampler.addressW = ToAddress(*state.value.valuesTA); break;
                    case MOJOSHADER_SAMP_MAGFILTER:
                        mag = *state.value.valuesTFT; filterChanged = true; break;
                    case MOJOSHADER_SAMP_MINFILTER:
                        min = *state.value.valuesTFT; filterChanged = true; break;
                    case MOJOSHADER_SAMP_MIPFILTER:
                        mip = *state.value.valuesTFT; filterChanged = true; break;
                    case MOJOSHADER_SAMP_MIPMAPLODBIAS:
                        sampler.mipMapLevelOfDetailBias = state.value.valuesF[0]; break;
                    case MOJOSHADER_SAMP_MAXMIPLEVEL:
                        sampler.maxMipLevel = state.value.valuesI[0]; break;
                    case MOJOSHADER_SAMP_MAXANISOTROPY:
                        sampler.maxAnisotropy = state.value.valuesI[0]; break;
                    default:
                        throw std::runtime_error(
                            "FNA3D compiled effect: unsupported sampler state " +
                            std::to_string(static_cast<int>(state.type)) + ".");
                }
            }
            if (filterChanged) sampler.filter = ToFilter(mag, min, mip);

            Texture* texture = nullptr;
            const auto mapped = samplerTextureParameters_.find(SafeString(change.sampler_name));
            if (mapped != samplerTextureParameters_.end() && mapped->second < textures_.size())
                texture = textures_[mapped->second];
            const Fna3dSampledTexture* sampled = GetSampledTexture(texture);
            FNA3D_Texture* native = sampled != nullptr ? sampled->GetFna3dTextureEXT() :
                (vertexStage ? renderer_.boundVertexTextures_[slot]
                             : renderer_.boundPixelTextures_[slot]);
            trackedSamplers = sampler;
            if (vertexStage)
            {
                FNA3D_VerifyVertexSampler(device_, static_cast<int32_t>(slot), native, &sampler);
                renderer_.boundVertexTextures_[slot] = native;
            }
            else
            {
                FNA3D_VerifySampler(device_, static_cast<int32_t>(slot), native, &sampler);
                renderer_.boundPixelTextures_[slot] = native;
            }
        }
    }
}
