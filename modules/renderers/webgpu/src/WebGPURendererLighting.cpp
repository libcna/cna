// SPDX-License-Identifier: MS-PL
//
// Shadow reception and image-based lighting for the stock lit families: the per-draw state each
// captures at the public call and the cached bind groups the replay binds for it. Both follow the
// renderer's one rule for queued draws -- resolve every resource to a value at the draw call, so a
// map or environment bound later cannot change what an already-queued draw receives.

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::WebGPU
{
    namespace
    {
        [[nodiscard]] WGPUStringView Label(const char* text)
        {
            return WGPUStringView{text, text != nullptr ? std::strlen(text) : 0};
        }
    }

    // ---- WMG-0014: shadow reception -----------------------

    void WebGPURenderer::EnsureShadowResourcesEXT()
    {
        if (shadowBindGroupLayout_ != nullptr || device_ == nullptr) return;
        std::array<WGPUBindGroupLayoutEntry, 7> entries{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = sizeof(WebGPUShadowStateEXT::uniforms);
        // WEBGPUPERF-0004: the block lives in the uniform arena, so draws
        // receiving the same maps share one group and differ only in this offset.
        entries[0].buffer.hasDynamicOffset = true;
        const auto sampler = [&entries](const std::uint32_t binding) {
            entries[binding].binding = binding;
            entries[binding].visibility = WGPUShaderStage_Fragment;
            entries[binding].sampler.type = WGPUSamplerBindingType_Filtering;
        };
        const auto texture = [&entries](const std::uint32_t binding,
                                        const WGPUTextureViewDimension dimension) {
            entries[binding].binding = binding;
            entries[binding].visibility = WGPUShaderStage_Fragment;
            entries[binding].texture.sampleType = WGPUTextureSampleType_Float;
            entries[binding].texture.viewDimension = dimension;
        };
        sampler(1); texture(2, WGPUTextureViewDimension_2D);
        sampler(3); texture(4, WGPUTextureViewDimension_Cube);
        sampler(5); texture(6, WGPUTextureViewDimension_2D);
        WGPUBindGroupLayoutDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU Shadow BindGroupLayout");
        descriptor.entryCount = entries.size();
        descriptor.entries = entries.data();
        shadowBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &descriptor);
        if (shadowBindGroupLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create the shadow bind-group layout");
    }

    WebGPURenderer::WebGPUShadowStateEXT WebGPURenderer::CaptureShadowStateEXT(
        const GpuDrawParams& params)
    {
        WebGPUShadowStateEXT state;
        const bool haveDirectional = params.shadowsEnabled && params.shadowMap != nullptr;
        const int cascadeCount =
            haveDirectional && params.cascadeCount > 0 ? std::min(params.cascadeCount, 4) : 0;
        const int punctualKind =
            params.punctualKind >= 1 && params.punctualKind <= 2 ? params.punctualKind : 0;
        const bool havePoint = punctualKind == 1 && params.punctualShadowCube != nullptr;
        const bool haveSpot = punctualKind == 2 && params.punctualShadowMap != nullptr;

        // The same float-for-float block the retired VULKAN renderer filled.
        float* out = state.uniforms.data();
        std::copy_n(params.lightViewProjColMajor, 16, out);
        std::copy_n(params.cascadeMatricesColMajor, 64, out + 16);
        std::copy_n(params.punctualViewProjColMajor, 16, out + 80);
        out[96] = haveDirectional ? 1.0f : 0.0f;
        out[97] = params.shadowDepthBias;
        out[98] = static_cast<float>(std::clamp(params.shadowPcfRadius, 0, 2));
        out[99] = static_cast<float>(cascadeCount);
        const int shadowWidth = haveDirectional ? params.shadowMap->GetWidth() : 1;
        const int shadowHeight = haveDirectional ? params.shadowMap->GetHeight() : 1;
        out[100] = shadowWidth > 0 ? 1.0f / static_cast<float>(shadowWidth) : 0.0f;
        out[101] = shadowHeight > 0 ? 1.0f / static_cast<float>(shadowHeight) : 0.0f;
        out[102] = params.cascadeBlendBand;
        out[103] = params.cascadeDebugTint ? 1.0f : 0.0f;
        std::copy_n(params.cascadeSplits, 4, out + 104);
        std::copy_n(params.cascadeViewZRow, 4, out + 108);
        std::copy_n(params.punctualPosition, 3, out + 112);
        out[115] = params.punctualRange > 0.0f ? params.punctualRange : 1.0f;
        std::copy_n(params.punctualDirection, 3, out + 116);
        out[119] = static_cast<float>(punctualKind);
        std::copy_n(params.punctualDiffuse, 3, out + 120);
        out[123] = (havePoint || haveSpot) ? 1.0f : 0.0f;
        out[124] = params.punctualCosInner;
        out[125] = params.punctualCosOuter;
        out[126] = params.punctualShadowBias;
        const int spotWidth = haveSpot ? params.punctualShadowMap->GetWidth() : 1;
        const int spotHeight = haveSpot ? params.punctualShadowMap->GetHeight() : 1;
        out[127] = spotWidth > 0 ? 1.0f / static_cast<float>(spotWidth) : 0.0f;
        out[128] = spotHeight > 0 ? 1.0f / static_cast<float>(spotHeight) : 0.0f;

        // The rule: resolve to a VALUE here, at the public draw call, while the
        // resource is unambiguously alive, so the queued command never dereferences a wrapper.
        const auto sampled2D = [](const ITextureRenderer* texture) {
            const auto* samplable =
                texture != nullptr ? dynamic_cast<const IWebGPUSamplable*>(texture) : nullptr;
            return samplable != nullptr ? samplable->Sampled() : WebGPUSampledTextureEXT{};
        };
        if (haveDirectional) state.map = sampled2D(params.shadowMap);
        if (havePoint)
        {
            const auto* cube = dynamic_cast<const IWebGPUCubeSamplable*>(params.punctualShadowCube);
            if (cube != nullptr) state.cube = cube->SampledCube();
        }
        if (haveSpot) state.spot = sampled2D(params.punctualShadowMap);
        for (std::size_t i = 0; i < state.samplers.size(); ++i)
            state.samplers[i] = slotSamplers_[7 + i];
        return state;
    }

    WGPUBindGroup WebGPURenderer::AcquireShadowBindingEXT(const WebGPUShadowStateEXT& state,
                                                          std::uint32_t& offset)
    {
        EnsureShadowResourcesEXT();
        // WEBGPUPERF-0004: the block goes into the uniform arena and is
        // bound at its own offset; what is left in the group is the maps and their samplers, which
        // a whole frame of draws usually shares -- so the group comes from the binding cache
        // instead of being built, written and released for every draw.
        if (!ReserveArenaEXT(uniformArenaEXT_, sizeof(state.uniforms)))
            throw std::runtime_error("CNA WebGPU: the shadow block does not fit an arena chunk");
        const ArenaSliceEXT slice =
            AppendArenaEXT(uniformArenaEXT_, state.uniforms.data(), sizeof(state.uniforms));
        offset = slice.offset;

        // A pipeline statically uses every binding its layout declares, so an absent map binds a
        // 1x1 white texture rather than nothing. White reads as "nothing occludes", which is what
        // the parameter block already says when the shadow is off.
        const WebGPUSampledTextureEXT white2D =
            NeutralTextureForDimensionEXT(WGPUTextureViewDimension_2D);
        const WebGPUSampledTextureEXT whiteCube =
            NeutralTextureForDimensionEXT(WGPUTextureViewDimension_Cube);
        const WebGPUSampledTextureEXT& map = state.map ? state.map : white2D;
        const WebGPUSampledTextureEXT& cube = state.cube ? state.cube : whiteCube;
        const WebGPUSampledTextureEXT& spot = state.spot ? state.spot : white2D;

        const auto sampler = [&](const std::size_t slot, const char* label) {
            const SlotSamplerState& s = state.samplers[slot];
            return GetOrCreateSlotSampler(s.filter, s.addressU, s.addressV, s.addressW,
                                          s.maxMipLevel, s.maxAnisotropy, label);
        };
        const WGPUSampler mapSampler = sampler(0, "ShadowMap");
        const WGPUSampler cubeSampler = sampler(1, "PunctualCube");
        const WGPUSampler spotSampler = sampler(2, "PunctualMap");

        std::array<WGPUBindGroupEntry, 7> entries{};
        entries[0].binding = 0;
        entries[0].buffer = slice.buffer;
        entries[0].size = sizeof(state.uniforms);
        entries[1].binding = 1;
        entries[1].sampler = mapSampler;
        entries[2].binding = 2;
        entries[2].textureView = map.View();
        entries[3].binding = 3;
        entries[3].sampler = cubeSampler;
        entries[4].binding = 4;
        entries[4].textureView = cube.View();
        entries[5].binding = 5;
        entries[5].sampler = spotSampler;
        entries[6].binding = 6;
        entries[6].textureView = spot.View();

        WGPUBindGroupDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU Shadow BindGroup");
        descriptor.layout = shadowBindGroupLayout_;
        descriptor.entryCount = entries.size();
        descriptor.entries = entries.data();

        BindingCacheKeyEXT key;
        key.layout = shadowBindGroupLayout_;
        key.arenaChunk = slice.chunk;
        key.handles = {mapSampler, map.View(), cubeSampler, cube.View(), spotSampler, spot.View()};
        return AcquireCachedBindGroupEXT(key, {&map, &cube, &spot}, descriptor);
    }

    // ---- WMG-0022: image-based lighting -------------------

    void WebGPURenderer::EnsureIblResourcesEXT()
    {
        if (iblBindGroupLayout_ != nullptr || device_ == nullptr) return;
        std::array<WGPUBindGroupLayoutEntry, 7> entries{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = sizeof(WebGPUIblStateEXT::uniforms);
        // WEBGPUPERF-0004: in the uniform arena, as the shadow block is.
        entries[0].buffer.hasDynamicOffset = true;
        const auto sampler = [&entries](const std::uint32_t binding) {
            entries[binding].binding = binding;
            entries[binding].visibility = WGPUShaderStage_Fragment;
            entries[binding].sampler.type = WGPUSamplerBindingType_Filtering;
        };
        const auto texture = [&entries](const std::uint32_t binding,
                                        const WGPUTextureViewDimension dimension) {
            entries[binding].binding = binding;
            entries[binding].visibility = WGPUShaderStage_Fragment;
            entries[binding].texture.sampleType = WGPUTextureSampleType_Float;
            entries[binding].texture.viewDimension = dimension;
        };
        sampler(1); texture(2, WGPUTextureViewDimension_Cube);
        sampler(3); texture(4, WGPUTextureViewDimension_Cube);
        sampler(5); texture(6, WGPUTextureViewDimension_2D);
        WGPUBindGroupLayoutDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU Ibl BindGroupLayout");
        descriptor.entryCount = entries.size();
        descriptor.entries = entries.data();
        iblBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &descriptor);
        if (iblBindGroupLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create the IBL bind-group layout");
    }

    WebGPURenderer::WebGPUIblStateEXT WebGPURenderer::CaptureIblStateEXT(
        const GpuDrawParams& params)
    {
        WebGPUIblStateEXT state;

        // The rule, as CaptureShadowStateEXT above states it: resolve to a VALUE here,
        // at the public draw call, while the resources are unambiguously alive.
        const auto* irradiance = params.iblIrradiance != nullptr
            ? dynamic_cast<const IWebGPUCubeSamplable*>(params.iblIrradiance) : nullptr;
        const auto* specular = params.iblPrefilteredSpecular != nullptr
            ? dynamic_cast<const IWebGPUCubeSamplable*>(params.iblPrefilteredSpecular) : nullptr;
        const auto* brdf = params.iblBrdfLut != nullptr
            ? dynamic_cast<const IWebGPUSamplable*>(params.iblBrdfLut) : nullptr;

        // The flag alone is not enough, because a bundle missing any one of its three products
        // cannot be evaluated at all.
        const bool haveIbl = params.iblEnabled && irradiance != nullptr && specular != nullptr &&
                             brdf != nullptr;
        if (haveIbl)
        {
            state.irradiance = irradiance->SampledCube();
            state.specular = specular->SampledCube();
            state.brdfLut = brdf->Sampled();
        }
        state.uniforms[0] = haveIbl ? 1.0f : 0.0f;
        state.uniforms[1] = static_cast<float>(
            params.iblPrefilteredMipCount > 0 ? params.iblPrefilteredMipCount : 1);
        state.uniforms[2] = params.iblIntensity;
        state.uniforms[3] = 0.0f;
        // Slots 10, 11 and 12, which is where EasyGL's texture units read these three from
        // (MOD-1225).
        for (std::size_t i = 0; i < state.samplers.size(); ++i)
            state.samplers[i] = slotSamplers_[10 + i];
        return state;
    }

    WGPUBindGroup WebGPURenderer::AcquireIblBindingEXT(const WebGPUIblStateEXT& state,
                                                       std::uint32_t& offset)
    {
        EnsureIblResourcesEXT();
        // WEBGPUPERF-0004: the same shape as AcquireShadowBindingEXT: the block in the arena, the
        // cubes and the table -- the sky's or a probe's, shared by every draw near it -- cached.
        if (!ReserveArenaEXT(uniformArenaEXT_, sizeof(state.uniforms)))
            throw std::runtime_error("CNA WebGPU: the IBL block does not fit an arena chunk");
        const ArenaSliceEXT slice =
            AppendArenaEXT(uniformArenaEXT_, state.uniforms.data(), sizeof(state.uniforms));
        offset = slice.offset;

        // A pipeline statically uses every binding its layout declares, so a draw with no
        // environment binds 1x1 white rather than nothing. What it samples does not matter --
        // `cnaIblAmbient` returns before its first tap when the enabled flag is zero -- but the
        // binding must exist and must have the declared dimension.
        const WebGPUSampledTextureEXT white2D =
            NeutralTextureForDimensionEXT(WGPUTextureViewDimension_2D);
        const WebGPUSampledTextureEXT whiteCube =
            NeutralTextureForDimensionEXT(WGPUTextureViewDimension_Cube);
        const WebGPUSampledTextureEXT& irradiance = state.irradiance ? state.irradiance : whiteCube;
        const WebGPUSampledTextureEXT& specular = state.specular ? state.specular : whiteCube;
        const WebGPUSampledTextureEXT& brdfLut = state.brdfLut ? state.brdfLut : white2D;

        const auto sampler = [&](const std::size_t slot, const char* label) {
            const SlotSamplerState& s = state.samplers[slot];
            return GetOrCreateSlotSampler(s.filter, s.addressU, s.addressV, s.addressW,
                                          s.maxMipLevel, s.maxAnisotropy, label);
        };
        const WGPUSampler irradianceSampler = sampler(0, "IblIrradiance");
        const WGPUSampler specularSampler = sampler(1, "IblPrefilteredSpecular");
        const WGPUSampler brdfSampler = sampler(2, "IblBrdfLut");

        std::array<WGPUBindGroupEntry, 7> entries{};
        entries[0].binding = 0;
        entries[0].buffer = slice.buffer;
        entries[0].size = sizeof(state.uniforms);
        entries[1].binding = 1;
        entries[1].sampler = irradianceSampler;
        entries[2].binding = 2;
        entries[2].textureView = irradiance.View();
        entries[3].binding = 3;
        entries[3].sampler = specularSampler;
        entries[4].binding = 4;
        entries[4].textureView = specular.View();
        entries[5].binding = 5;
        entries[5].sampler = brdfSampler;
        entries[6].binding = 6;
        entries[6].textureView = brdfLut.View();

        WGPUBindGroupDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU Ibl BindGroup");
        descriptor.layout = iblBindGroupLayout_;
        descriptor.entryCount = entries.size();
        descriptor.entries = entries.data();

        BindingCacheKeyEXT key;
        key.layout = iblBindGroupLayout_;
        key.arenaChunk = slice.chunk;
        key.handles = {irradianceSampler, irradiance.View(), specularSampler, specular.View(),
                       brdfSampler, brdfLut.View()};
        return AcquireCachedBindGroupEXT(key, {&irradiance, &specular, &brdfLut}, descriptor);
    }
}
