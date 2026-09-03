// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dCompiledEffect.hpp"

#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
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
#include "System/InvalidCastException.hpp"

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
    using Microsoft::Xna::Framework::Graphics::Blend;
    using Microsoft::Xna::Framework::Graphics::BlendFunction;
    using Microsoft::Xna::Framework::Graphics::ColorWriteChannels;
    using Microsoft::Xna::Framework::Graphics::CompareFunction;
    using Microsoft::Xna::Framework::Graphics::CullMode;
    using Microsoft::Xna::Framework::Graphics::FillMode;
    using Microsoft::Xna::Framework::Graphics::StencilOperation;
    using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
    using Microsoft::Xna::Framework::Graphics::TextureFilter;

    namespace
    {
        /// Same ceiling the shared translation applies to reflected tables.
        constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;

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
            MojoShaderEffect::ValidateNativeEffect(effectData_, "create");
            // The shared validation knows nothing of FNA3D's own handle, so the half
            // of the original check that was FNA3D-specific stays here.
            if (effect_ == nullptr)
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: FNA3D returned no native effect handle.");
            }
            description_ = MojoShaderEffect::BuildDescription(effectData_);
            samplerTextureParameters_ =
                MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
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
            MojoShaderEffect::ValidateNativeEffect(effectData_, "clone");
            // The shared validation knows nothing of FNA3D's own handle, so the half
            // of the original check that was FNA3D-specific stays here.
            if (effect_ == nullptr)
            {
                throw std::runtime_error(
                    "FNA3D compiled effect: FNA3D returned no native effect handle.");
            }
            description_ = MojoShaderEffect::BuildDescription(effectData_);
            samplerTextureParameters_ =
                MojoShaderEffect::BuildSamplerTextureParameterMap(effectData_);
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
        const auto parameterType = static_cast<std::underlying_type_t<MOJOSHADER_symbolType>>(
            effectData_->params[runtimeIndex].value.type.parameter_type);
        if (parameterType < MOJOSHADER_SYMTYPE_TEXTURE ||
            parameterType > MOJOSHADER_SYMTYPE_TEXTURECUBE)
            throw std::invalid_argument("FNA3D compiled effect: parameter is not a texture.");
        if (texture != nullptr && GetSampledTexture(texture) == nullptr)
            throw std::invalid_argument(
                "FNA3D compiled effect: texture was not created by the active FNA3D renderer.");
        // plans/plan_fx.md FX-110: the assigned texture's dimension must match the one the effect
        // declared. FNA3D binds a texture to a slot by its own target, so a Texture2D assigned to a
        // samplerCUBE leaves the cube target unbound and the shader samples black -- silently, and
        // indistinguishably from a genuinely black texture. Refused at assignment, which is where
        // XNA refuses it too (EffectParameter.SetValue's own InvalidCastException).
        if (texture != nullptr)
        {
            using namespace Microsoft::Xna::Framework::Graphics;
            const bool isCube = dynamic_cast<TextureCube*>(texture) != nullptr;
            const bool isVolume = !isCube && dynamic_cast<Texture3D*>(texture) != nullptr;
            const auto declared = static_cast<MOJOSHADER_symbolType>(parameterType);
            const bool declaredCube = declared == MOJOSHADER_SYMTYPE_TEXTURECUBE;
            const bool declaredVolume = declared == MOJOSHADER_SYMTYPE_TEXTURE3D;
            // A parameter declared as the dimensionless `texture` accepts any kind, exactly as
            // Direct3D 9's own `texture` type does; only an explicit dimension is checked.
            const bool declaredAny = declared == MOJOSHADER_SYMTYPE_TEXTURE;
            if (!declaredAny && (isCube != declaredCube || isVolume != declaredVolume))
            {
                const auto* declaredName = declaredCube ? "TextureCube"
                                         : declaredVolume ? "Texture3D" : "Texture2D";
                const auto* assignedName = isCube ? "TextureCube"
                                         : isVolume ? "Texture3D" : "Texture2D";
                throw System::InvalidCastException(
                    std::string("FNA3D compiled effect: parameter declares ") + declaredName +
                    " but a " + assignedName + " was assigned; the dimensions must match.");
            }
        }
        textures_[runtimeIndex] = texture;
    }

    void Fna3dCompiledEffect::ApplyPass(std::uint32_t passIndex,
                                        const CompiledEffectDeviceState& deviceState,
                                        CompiledEffectPassStateChanges& changes)
    {
        const MOJOSHADER_effectTechnique& technique = effectData_->techniques[techniqueIndex_];
        if (passIndex >= technique.pass_count)
            throw std::out_of_range("FNA3D compiled effect: pass index is out of range.");

        // plans/plan_fx.md FX-101: `stateChanges_` is NOT cleared before each application, and that is
        // load-bearing rather than an oversight.
        //
        // MojoShader writes this struct only in `MOJOSHADER_effectBeginPass`. FNA3D reaches that
        // only when the effect, technique or pass actually CHANGES; re-applying the same pass takes
        // its `MOJOSHADER_effectCommitChanges` shortcut, which re-runs preshaders and re-copies
        // parameter data but leaves the struct exactly as the last BeginPass wrote it -- still
        // pointing at this pass's own render states and sampler states. FNA relies on that: it
        // allocates the struct once per Effect and never clears it, so every `EffectPass.Apply()`
        // re-establishes the pass's state whether or not MojoShader wrote it again.
        //
        // Clearing it first made a repeat application publish NOTHING: no render states, no sampler
        // states, no texture binding. The second and every later Apply() of one compiled effect
        // silently dropped its own sampler binding, and the draw that followed rendered nothing at
        // all -- reproduced with two consecutive draws of one sampling effect, which produced the
        // clear colour from the second onwards. It is zero-initialised once at construction, which
        // is all the "no stale pointers before the first apply" the safety check below needs.
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
        MojoShaderEffect::TranslateRenderStates(stateChanges_, deviceState, changes);

        // CNA's GraphicsDevice publishes pixel sampler state to the renderer at draw time and has
        // no vertex-stage path at all, so the native binding still happens here. The published
        // changes are what make the same state observable through the XNA API.
        const std::size_t alreadyPublished = changes.samplers.size();
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.sampler_state_changes, stateChanges_.sampler_state_change_count,
            /*vertexStage=*/false, renderer_.samplerStates_.size(),
            samplerTextureParameters_, textures_, deviceState, changes);
        MojoShaderEffect::TranslateSamplers(
            stateChanges_.vertex_sampler_state_changes,
            stateChanges_.vertex_sampler_state_change_count,
            /*vertexStage=*/true, renderer_.vertexSamplerStates_.size(),
            samplerTextureParameters_, textures_, deviceState, changes);
        MojoShaderEffect::TranslateLegacySamplerAssignments(
            effectData_, stateChanges_, renderer_.samplerStates_.size(),
            samplerTextureParameters_, textures_, deviceState, changes);
        for (std::size_t i = alreadyPublished; i < changes.samplers.size(); ++i)
        {
            const CompiledEffectSamplerChange& change = changes.samplers[i];
            ApplyNativeSampler(change.slot, change.vertexStage, change.sampler, change.texture);
        }
    }



    void Fna3dCompiledEffect::ApplyNativeSampler(std::size_t slot, bool vertexStage,
                                                 const SamplerState& sampler, Texture* texture)
    {
        FNA3D_SamplerState native = vertexStage ? renderer_.vertexSamplerStates_[slot]
                                                : renderer_.samplerStates_[slot];
        native.filter = static_cast<FNA3D_TextureFilter>(sampler.getFilterProperty());
        native.addressU =
            static_cast<FNA3D_TextureAddressMode>(sampler.getAddressUProperty());
        native.addressV =
            static_cast<FNA3D_TextureAddressMode>(sampler.getAddressVProperty());
        native.addressW =
            static_cast<FNA3D_TextureAddressMode>(sampler.getAddressWProperty());
        native.mipMapLevelOfDetailBias = sampler.getMipMapLevelOfDetailBiasProperty();
        native.maxMipLevel = sampler.getMaxMipLevelProperty();
        native.maxAnisotropy = sampler.getMaxAnisotropyProperty();

        const Fna3dSampledTexture* sampled = GetSampledTexture(texture);
        FNA3D_Texture* nativeTexture = sampled != nullptr ? sampled->GetFna3dTextureEXT() :
            (vertexStage ? renderer_.boundVertexTextures_[slot]
                         : renderer_.boundPixelTextures_[slot]);
        if (vertexStage)
        {
            renderer_.vertexSamplerStates_[slot] = native;
            FNA3D_VerifyVertexSampler(device_, static_cast<int32_t>(slot), nativeTexture, &native);
            renderer_.boundVertexTextures_[slot] = nativeTexture;
        }
        else
        {
            renderer_.samplerStates_[slot] = native;
            FNA3D_VerifySampler(device_, static_cast<int32_t>(slot), nativeTexture, &native);
            renderer_.boundPixelTextures_[slot] = nativeTexture;
        }
    }
}
