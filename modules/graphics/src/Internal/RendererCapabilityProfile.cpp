// SPDX-License-Identifier: MS-PL
#include "CNA/RendererCapabilityProfile.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace CNA
{
    namespace
    {
        constexpr std::size_t FeatureCount = static_cast<std::size_t>(RendererFeature::Count);
        constexpr std::size_t LimitCount = static_cast<std::size_t>(RendererLimit::Count);

        constexpr std::array<RendererFeature, FeatureCount> Features = {
            RendererFeature::ThreeDimensionalPipeline,
            RendererFeature::DepthStencilBuffer,
            RendererFeature::MultiSampleAntiAliasing,
            RendererFeature::MultipleRenderTargets,
            RendererFeature::AnisotropicFiltering,
            RendererFeature::WireFrameRasterization,
            RendererFeature::OcclusionQueries,
            RendererFeature::ShaderEffects,
            RendererFeature::ShaderEffectSourceExecution,
            RendererFeature::Texture3DStorage,
            RendererFeature::MultiStreamVertexInput,
            RendererFeature::InstancedDrawing,
            RendererFeature::StencilBuffer,
            RendererFeature::AdditiveBlending,
            RendererFeature::CompiledXnaEffects,
            RendererFeature::Float32RenderTargets,
            RendererFeature::Float16RenderTargets,
            RendererFeature::Float16TextureLinearFiltering,
            RendererFeature::ComputeShaders,
            RendererFeature::ComputeImageBinding,
            RendererFeature::IndirectDrawing,
            RendererFeature::ShadowSampling,
            RendererFeature::ImageBasedLighting,
            RendererFeature::GpuTimers,
            RendererFeature::ShaderDialectGlslDesktop,
            RendererFeature::ShaderDialectGlslEs,
            RendererFeature::ShaderDialectGlslVulkan,
            RendererFeature::ShaderDialectHlsl,
            RendererFeature::ShaderDialectMsl,
            RendererFeature::ShaderDialectWgsl
        };

        constexpr std::array<RendererLimit, LimitCount> Limits = {
            RendererLimit::MaxTextureDimension,
            RendererLimit::MaxVertexStreams,
            RendererLimit::MaxComputeWorkGroupCountX,
            RendererLimit::MaxComputeWorkGroupCountY,
            RendererLimit::MaxComputeWorkGroupCountZ,
            RendererLimit::MaxComputeWorkGroupSizeX,
            RendererLimit::MaxComputeWorkGroupSizeY,
            RendererLimit::MaxComputeWorkGroupSizeZ,
            RendererLimit::MaxComputeWorkGroupInvocations,
            RendererLimit::MaxVertexShaderStorageBlocks
        };

        [[nodiscard]] constexpr bool ValidFeature(const RendererFeature feature)
        {
            return static_cast<std::uint32_t>(feature) <
                   static_cast<std::uint32_t>(RendererFeature::Count);
        }

        [[nodiscard]] constexpr bool ValidLimit(const RendererLimit limit)
        {
            return static_cast<std::uint32_t>(limit) <
                   static_cast<std::uint32_t>(RendererLimit::Count);
        }

        [[nodiscard]] std::string_view SupportName(const RendererFeatureSupport support)
        {
            switch (support)
            {
                case RendererFeatureSupport::Unknown: return "unknown";
                case RendererFeatureSupport::Unsupported: return "unsupported";
                case RendererFeatureSupport::Supported: return "supported";
                case RendererFeatureSupport::Restricted: return "restricted";
            }
            return "unknown";
        }

        void AppendUsage(std::ostringstream& out, const RendererFormatSupport support,
                         const RendererFormatUsage usage, const std::string_view name,
                         bool& first)
        {
            if (!support.IsKnown(usage)) return;
            if (!first) out << ", ";
            first = false;
            out << name << '=' << (support.Supports(usage) ? "yes" : "no");
        }
    }

    bool RendererFormatSupport::IsKnown(const RendererFormatUsage usage) const
    {
        const auto mask = static_cast<std::uint32_t>(usage);
        return (knownUsages & mask) == mask;
    }

    bool RendererFormatSupport::Supports(const RendererFormatUsage usage) const
    {
        const auto mask = static_cast<std::uint32_t>(usage);
        return IsKnown(usage) && (supportedUsages & mask) == mask;
    }

    RendererCapabilityProfile::RendererCapabilityProfile() = default;

    std::string_view RendererCapabilityProfile::GetRendererName() const
    {
        return rendererName_;
    }

    const RendererFeatureInfo& RendererCapabilityProfile::GetFeature(
        const RendererFeature feature) const
    {
        static const RendererFeatureInfo unknown{};
        if (!ValidFeature(feature)) return unknown;
        return features_[static_cast<std::size_t>(feature)];
    }

    bool RendererCapabilityProfile::Supports(const RendererFeature feature) const
    {
        return GetFeature(feature).support == RendererFeatureSupport::Supported;
    }

    RendererLimitValue RendererCapabilityProfile::GetLimit(const RendererLimit limit) const
    {
        if (!ValidLimit(limit)) return {};
        return limits_[static_cast<std::size_t>(limit)];
    }

    RendererFormatSupport RendererCapabilityProfile::GetSurfaceFormatSupport(
        const std::uint32_t surfaceFormatOrdinal) const
    {
        if (surfaceFormatOrdinal >= formats_.size()) return {};
        return formats_[surfaceFormatOrdinal];
    }

    std::string_view RendererCapabilityProfile::GetAdditionalLimitationsText() const
    {
        return additionalLimitationsText_;
    }

    std::string_view RendererCapabilityProfile::GetEnglishReport() const
    {
        return englishReport_;
    }

    void RendererCapabilityProfile::SetFeature(const RendererFeature feature,
                                                const RendererFeatureSupport support,
                                                std::string note)
    {
        if (!ValidFeature(feature)) return;
        features_[static_cast<std::size_t>(feature)] = {support, std::move(note)};
    }

    void RendererCapabilityProfile::SetLimit(const RendererLimit limit, const bool known,
                                              const std::uint64_t value)
    {
        if (!ValidLimit(limit)) return;
        limits_[static_cast<std::size_t>(limit)] = {known, value};
    }

    void RendererCapabilityProfile::SetSurfaceFormat(const std::uint32_t ordinal,
                                                       const std::string_view name,
                                                       const RendererFormatSupport support)
    {
        if (ordinal >= formats_.size()) return;
        formats_[ordinal] = {support.knownUsages,
                             support.supportedUsages & support.knownUsages};
        formatNames_[ordinal] = name;
    }

    void RendererCapabilityProfile::BuildEnglishReport()
    {
        std::ostringstream out;
        out << "Renderer capability report\n"
            << "Renderer: " << (rendererName_.empty() ? "UNKNOWN" : rendererName_) << "\n"
            << "Profile schema: 1\n\n"
            << "Detailed features\n";

        for (const RendererFeature feature : Features)
        {
            const RendererFeatureInfo& info = GetFeature(feature);
            out << "- " << GetRendererFeatureName(feature) << ": "
                << SupportName(info.support) << " -- "
                << GetRendererFeatureDescription(feature);
            if (!info.note.empty()) out << " Note: " << info.note;
            out << '\n';
        }

        out << "\nNumeric limits\n";
        for (const RendererLimit limit : Limits)
        {
            const RendererLimitValue value = GetLimit(limit);
            out << "- " << GetRendererLimitName(limit) << ": ";
            if (value.known) out << value.value;
            else out << "unknown";
            out << '\n';
        }

        out << "\nSurface-format support\n";
        for (std::size_t i = 0; i < formats_.size(); ++i)
        {
            out << "- " << (formatNames_[i].empty() ? "ordinal" : formatNames_[i]);
            if (formatNames_[i].empty()) out << ' ' << i;
            out << ": ";
            bool first = true;
            AppendUsage(out, formats_[i], RendererFormatUsage::TextureStorage,
                        "texture-storage", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::Sampled, "sampled", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::Filterable, "filterable", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::RenderTarget,
                        "render-target", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::Blendable, "blendable", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::StorageRead,
                        "storage-read", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::StorageWrite,
                        "storage-write", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::StorageAtomic,
                        "storage-atomic", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::TransferSource,
                        "transfer-source", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::TransferDestination,
                        "transfer-destination", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::Mipmapped, "mipmapped", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::Multisample,
                        "multisample", first);
            AppendUsage(out, formats_[i], RendererFormatUsage::ColorTransfer,
                        "color-transfer", first);
            if (first) out << "all usages unknown";
            out << '\n';
        }

        out << "\nAdditional limitations\n" << additionalLimitationsText_ << '\n';
        englishReport_ = out.str();
    }

    std::span<const RendererFeature> AllRendererFeatures()
    {
        return Features;
    }

    std::string_view GetRendererFeatureName(const RendererFeature feature)
    {
        switch (feature)
        {
            case RendererFeature::ThreeDimensionalPipeline: return "ThreeDimensionalPipeline";
            case RendererFeature::DepthStencilBuffer: return "DepthStencilBuffer";
            case RendererFeature::MultiSampleAntiAliasing: return "MultiSampleAntiAliasing";
            case RendererFeature::MultipleRenderTargets: return "MultipleRenderTargets";
            case RendererFeature::AnisotropicFiltering: return "AnisotropicFiltering";
            case RendererFeature::WireFrameRasterization: return "WireFrameRasterization";
            case RendererFeature::OcclusionQueries: return "OcclusionQueries";
            case RendererFeature::ShaderEffects: return "ShaderEffects";
            case RendererFeature::ShaderEffectSourceExecution: return "ShaderEffectSourceExecution";
            case RendererFeature::Texture3DStorage: return "Texture3DStorage";
            case RendererFeature::MultiStreamVertexInput: return "MultiStreamVertexInput";
            case RendererFeature::InstancedDrawing: return "InstancedDrawing";
            case RendererFeature::StencilBuffer: return "StencilBuffer";
            case RendererFeature::AdditiveBlending: return "AdditiveBlending";
            case RendererFeature::CompiledXnaEffects: return "CompiledXnaEffects";
            case RendererFeature::Float32RenderTargets: return "Float32RenderTargets";
            case RendererFeature::Float16RenderTargets: return "Float16RenderTargets";
            case RendererFeature::Float16TextureLinearFiltering:
                return "Float16TextureLinearFiltering";
            case RendererFeature::ComputeShaders: return "ComputeShaders";
            case RendererFeature::ComputeImageBinding: return "ComputeImageBinding";
            case RendererFeature::IndirectDrawing: return "IndirectDrawing";
            case RendererFeature::ShadowSampling: return "ShadowSampling";
            case RendererFeature::ImageBasedLighting: return "ImageBasedLighting";
            case RendererFeature::GpuTimers: return "GpuTimers";
            case RendererFeature::ShaderDialectGlslDesktop: return "ShaderDialectGlslDesktop";
            case RendererFeature::ShaderDialectGlslEs: return "ShaderDialectGlslEs";
            case RendererFeature::ShaderDialectGlslVulkan: return "ShaderDialectGlslVulkan";
            case RendererFeature::ShaderDialectHlsl: return "ShaderDialectHlsl";
            case RendererFeature::ShaderDialectMsl: return "ShaderDialectMsl";
            case RendererFeature::ShaderDialectWgsl: return "ShaderDialectWgsl";
            case RendererFeature::Count: break;
        }
        return "UnknownRendererFeature";
    }

    std::string_view GetRendererFeatureDescription(const RendererFeature feature)
    {
        switch (feature)
        {
            case RendererFeature::ThreeDimensionalPipeline:
                return "Complete XNA-style 3D buffers, state, clears and draw submission.";
            case RendererFeature::DepthStencilBuffer:
                return "A complete depth/stencil attachment usable by graphics draws.";
            case RendererFeature::MultiSampleAntiAliasing:
                return "At least one multisample mode above one sample is available.";
            case RendererFeature::MultipleRenderTargets:
                return "More than one colour attachment can receive one draw.";
            case RendererFeature::AnisotropicFiltering:
                return "Anisotropic sampling produces the renderer's documented filtered result.";
            case RendererFeature::WireFrameRasterization:
                return "Wire-frame output is produced faithfully, natively or by exact emulation.";
            case RendererFeature::OcclusionQueries:
                return "Begin, end, completion and pixel-count query operations are real.";
            case RendererFeature::ShaderEffects:
                return "Source-based ShaderEffect objects can be created and used.";
            case RendererFeature::ShaderEffectSourceExecution:
                return "The supplied ShaderEffect source, rather than a fixed substitute, determines pixels.";
            case RendererFeature::Texture3DStorage:
                return "Texture3D upload and readback persist exact volume data; sampling is separate.";
            case RendererFeature::MultiStreamVertexInput:
                return "Several same-rate VertexBufferBindings can feed one draw without truncation.";
            case RendererFeature::InstancedDrawing:
                return "DrawInstancedPrimitives consumes per-instance input and instance count.";
            case RendererFeature::StencilBuffer:
                return "Stencil operations are available independently of a depth claim.";
            case RendererFeature::AdditiveBlending:
                return "BlendState::Additive does not silently degrade to source-over blending.";
            case RendererFeature::CompiledXnaEffects:
                return "Compiled Direct3D 9 Effect Framework bytecode controls shaders, passes and state.";
            case RendererFeature::Float32RenderTargets:
                return "32-bit floating-point colour values survive render-target storage.";
            case RendererFeature::Float16RenderTargets:
                return "16-bit floating-point colour values survive render-target storage.";
            case RendererFeature::Float16TextureLinearFiltering:
                return "Half-float colour textures support linear and mip filtering.";
            case RendererFeature::ComputeShaders:
                return "Compute programs and their storage-buffer dispatch path are implemented.";
            case RendererFeature::ComputeImageBinding:
                return "A Texture2D can be bound as an image for compute access.";
            case RendererFeature::IndirectDrawing:
                return "Graphics draw arguments can be read from a GPU buffer.";
            case RendererFeature::ShadowSampling:
                return "Lit renderer shaders consume configured shadow resources and parameters.";
            case RendererFeature::ImageBasedLighting:
                return "PBR renderer shaders consume irradiance, prefiltered specular and BRDF resources.";
            case RendererFeature::GpuTimers:
                return "GPU timestamp queries measure a submitted command range.";
            case RendererFeature::ShaderDialectGlslDesktop:
                return "ShaderEffect consumes desktop OpenGL GLSL source.";
            case RendererFeature::ShaderDialectGlslEs:
                return "ShaderEffect consumes OpenGL ES or WebGL GLSL source.";
            case RendererFeature::ShaderDialectGlslVulkan:
                return "ShaderEffect consumes Vulkan-oriented GLSL source.";
            case RendererFeature::ShaderDialectHlsl:
                return "ShaderEffect consumes Direct3D HLSL source.";
            case RendererFeature::ShaderDialectMsl:
                return "ShaderEffect consumes Metal Shading Language source.";
            case RendererFeature::ShaderDialectWgsl:
                return "ShaderEffect consumes WebGPU Shading Language source.";
            case RendererFeature::Count: break;
        }
        return "Invalid detailed renderer feature identity.";
    }

    std::span<const RendererLimit> AllRendererLimits()
    {
        return Limits;
    }

    std::string_view GetRendererLimitName(const RendererLimit limit)
    {
        switch (limit)
        {
            case RendererLimit::MaxTextureDimension: return "MaxTextureDimension";
            case RendererLimit::MaxVertexStreams: return "MaxVertexStreams";
            case RendererLimit::MaxComputeWorkGroupCountX: return "MaxComputeWorkGroupCountX";
            case RendererLimit::MaxComputeWorkGroupCountY: return "MaxComputeWorkGroupCountY";
            case RendererLimit::MaxComputeWorkGroupCountZ: return "MaxComputeWorkGroupCountZ";
            case RendererLimit::MaxComputeWorkGroupSizeX: return "MaxComputeWorkGroupSizeX";
            case RendererLimit::MaxComputeWorkGroupSizeY: return "MaxComputeWorkGroupSizeY";
            case RendererLimit::MaxComputeWorkGroupSizeZ: return "MaxComputeWorkGroupSizeZ";
            case RendererLimit::MaxComputeWorkGroupInvocations:
                return "MaxComputeWorkGroupInvocations";
            case RendererLimit::MaxVertexShaderStorageBlocks:
                return "MaxVertexShaderStorageBlocks";
            case RendererLimit::Count: break;
        }
        return "UnknownRendererLimit";
    }
} // namespace CNA
