// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CNA
{
    /**
     * @brief Identifies one atomic, observable renderer feature.
     *
     * Unlike the legacy `GraphicsCapability` summary, these entries deliberately separate
     * contracts that real renderers can answer differently. Numeric values are public and
     * append-only so the same identities can be exposed through CNA's C ABI.
     */
    enum class RendererFeature : std::uint32_t
    {
        /** @brief The complete XNA-style 3D vertex/index drawing pipeline. */
        ThreeDimensionalPipeline = 0,
        /** @brief A complete depth/stencil attachment usable by graphics draws. */
        DepthStencilBuffer = 1,
        /** @brief Any multisample anti-aliasing mode above one sample. */
        MultiSampleAntiAliasing = 2,
        /** @brief More than one simultaneous colour render target. */
        MultipleRenderTargets = 3,
        /** @brief Anisotropic texture filtering with an observable result. */
        AnisotropicFiltering = 4,
        /** @brief Wire-frame rasterization, whether native or exact emulation. */
        WireFrameRasterization = 5,
        /** @brief Occlusion-query begin/end/result operations. */
        OcclusionQueries = 6,
        /** @brief Creation and use of source-based `ShaderEffect` objects. */
        ShaderEffects = 7,
        /** @brief Supplied `ShaderEffect` source determines the rendered pixels. */
        ShaderEffectSourceExecution = 8,
        /** @brief Persistent `Texture3D` upload and readback storage. */
        Texture3DStorage = 9,
        /** @brief Multiple vertex streams with the same input rate in one draw. */
        MultiStreamVertexInput = 10,
        /** @brief Instanced drawing through `DrawInstancedPrimitives`. */
        InstancedDrawing = 11,
        /** @brief A stencil plane usable independently of depth. */
        StencilBuffer = 12,
        /** @brief Faithful `BlendState::Additive` compositing. */
        AdditiveBlending = 13,
        /** @brief XNA/FNA Direct3D 9 Effect Framework bytecode execution. */
        CompiledXnaEffects = 14,
        /** @brief Faithful 32-bit-per-channel floating-point render targets. */
        Float32RenderTargets = 15,
        /** @brief Faithful 16-bit-per-channel floating-point render targets. */
        Float16RenderTargets = 16,
        /** @brief Linear or mip filtering of half-float colour textures. */
        Float16TextureLinearFiltering = 17,
        /** @brief Compute shaders together with their storage-buffer path. */
        ComputeShaders = 18,
        /** @brief Binding a two-dimensional texture as a compute image. */
        ComputeImageBinding = 19,
        /** @brief GPU-buffer-driven indirect graphics draws. */
        IndirectDrawing = 20,
        /** @brief Lit stock/PBR shaders sample configured shadow state. */
        ShadowSampling = 21,
        /** @brief PBR shaders consume configured image-based lighting resources. */
        ImageBasedLighting = 22,
        /** @brief GPU timestamp queries can measure a command range. */
        GpuTimers = 23,
        /** @brief Source-based effects consume desktop OpenGL GLSL. */
        ShaderDialectGlslDesktop = 24,
        /** @brief Source-based effects consume OpenGL ES/WebGL GLSL. */
        ShaderDialectGlslEs = 25,
        /** @brief Source-based effects consume Vulkan-oriented GLSL. */
        ShaderDialectGlslVulkan = 26,
        /** @brief Source-based effects consume HLSL. */
        ShaderDialectHlsl = 27,
        /** @brief Source-based effects consume Metal Shading Language. */
        ShaderDialectMsl = 28,
        /** @brief Source-based effects consume WebGPU Shading Language. */
        ShaderDialectWgsl = 29,
        /** @brief Number of declared feature identities; not itself a queryable feature. */
        Count = 30
    };

    /** @brief The renderer's classified answer for one detailed feature. */
    enum class RendererFeatureSupport : std::uint8_t
    {
        /** @brief The feature has not yet been audited or runtime-probed. */
        Unknown = 0,
        /** @brief The feature is unavailable and its public operation must refuse deterministically. */
        Unsupported = 1,
        /** @brief The complete documented feature contract is available. */
        Supported = 2,
        /** @brief Only a structured, explicitly described subset is available. */
        Restricted = 3
    };

    /** @brief Identifies one numeric renderer/device limit. */
    enum class RendererLimit : std::uint32_t
    {
        /** @brief Maximum width or height of a two-dimensional texture. */
        MaxTextureDimension = 0,
        /** @brief Maximum number of same-rate vertex streams in one draw. */
        MaxVertexStreams = 1,
        /** @brief Maximum compute work-group count on the X axis. */
        MaxComputeWorkGroupCountX = 2,
        /** @brief Maximum compute work-group count on the Y axis. */
        MaxComputeWorkGroupCountY = 3,
        /** @brief Maximum compute work-group count on the Z axis. */
        MaxComputeWorkGroupCountZ = 4,
        /** @brief Maximum compute local size on the X axis. */
        MaxComputeWorkGroupSizeX = 5,
        /** @brief Maximum compute local size on the Y axis. */
        MaxComputeWorkGroupSizeY = 6,
        /** @brief Maximum compute local size on the Z axis. */
        MaxComputeWorkGroupSizeZ = 7,
        /** @brief Maximum product of all compute local sizes. */
        MaxComputeWorkGroupInvocations = 8,
        /** @brief Maximum storage-buffer bindings readable by a vertex shader. */
        MaxVertexShaderStorageBlocks = 9,
        /** @brief Number of declared limit identities; not itself a queryable limit. */
        Count = 10
    };

    /**
     * @brief Describes one possible use of a `SurfaceFormat`.
     *
     * A format result carries separate known and supported masks. A missing known bit means that
     * the active renderer has not classified that use; it must not be interpreted as unsupported.
     */
    enum class RendererFormatUsage : std::uint32_t
    {
        /** @brief Creating faithful `Texture2D` storage in this format. */
        TextureStorage = UINT32_C(1) << 0,
        /** @brief Sampling the format in a graphics shader. */
        Sampled = UINT32_C(1) << 1,
        /** @brief Applying linear or mip filtering while sampling. */
        Filterable = UINT32_C(1) << 2,
        /** @brief Creating and binding a render target in this format. */
        RenderTarget = UINT32_C(1) << 3,
        /** @brief Blending graphics output into this format. */
        Blendable = UINT32_C(1) << 4,
        /** @brief Reading this format through a compute/storage-image binding. */
        StorageRead = UINT32_C(1) << 5,
        /** @brief Writing this format through a compute/storage-image binding. */
        StorageWrite = UINT32_C(1) << 6,
        /** @brief Performing storage-image atomic operations in this format. */
        StorageAtomic = UINT32_C(1) << 7,
        /** @brief Copying this format from a resource into another destination. */
        TransferSource = UINT32_C(1) << 8,
        /** @brief Copying data into a resource of this format. */
        TransferDestination = UINT32_C(1) << 9,
        /** @brief Owning or generating more than one mip level in this format. */
        Mipmapped = UINT32_C(1) << 10,
        /** @brief Creating a multisampled image in this format. */
        Multisample = UINT32_C(1) << 11,
        /** @brief Transferring the format through a `Color`-shaped element. */
        ColorTransfer = UINT32_C(1) << 12
    };

    /**
     * @brief Combines two renderer-format usage flags.
     * @param left First flag or mask.
     * @param right Second flag or mask.
     * @return The combined mask.
     */
    [[nodiscard]] constexpr RendererFormatUsage operator|(RendererFormatUsage left,
                                                           RendererFormatUsage right)
    {
        return static_cast<RendererFormatUsage>(static_cast<std::uint32_t>(left) |
                                                 static_cast<std::uint32_t>(right));
    }

    /** @brief One detailed feature result and its optional stable English qualification. */
    struct RendererFeatureInfo
    {
        /** @brief Classified support state. */
        RendererFeatureSupport support = RendererFeatureSupport::Unknown;
        /** @brief Optional renderer/device-specific English qualification. */
        std::string note;
    };

    /** @brief One numeric limit with an explicit known/unknown distinction. */
    struct RendererLimitValue
    {
        /** @brief True when `value` is an audited or runtime-probed answer. */
        bool known = false;
        /** @brief Limit value; meaningful only when `known` is true. */
        std::uint64_t value = 0;
    };

    /** @brief Known and supported usage masks for one surface format. */
    struct RendererFormatSupport
    {
        /** @brief Usage bits for which the renderer has a classified answer. */
        std::uint32_t knownUsages = 0;
        /** @brief Supported usage bits; always a subset of `knownUsages`. */
        std::uint32_t supportedUsages = 0;

        /**
         * @brief Returns whether this usage has been classified.
         * @param usage Usage to inspect.
         * @return True when the support answer is known.
         */
        [[nodiscard]] bool IsKnown(RendererFormatUsage usage) const;

        /**
         * @brief Returns whether this usage is both known and supported.
         * @param usage Usage to inspect.
         * @return True only for a classified supported usage.
         */
        [[nodiscard]] bool Supports(RendererFormatUsage usage) const;
    };

    /**
     * @brief Immutable snapshot of the active renderer and device's detailed capabilities.
     *
     * The owning `GraphicsDevice` builds and caches this value after native renderer creation.
     * All features default to `Unknown`; every populated answer therefore represents an explicit
     * mapping from an existing canonical renderer/device query rather than a renderer-name guess.
     */
    class RendererCapabilityProfile
    {
    public:
        /** @brief Constructs an empty profile whose entries are all unknown. */
        RendererCapabilityProfile();

        /**
         * @brief Returns the stable active renderer name captured by this snapshot.
         * @return Renderer name such as `"OPENGLES3"`.
         */
        [[nodiscard]] std::string_view GetRendererName() const;

        /**
         * @brief Returns one detailed feature result.
         * @param feature Feature to inspect.
         * @return The classified result, or `Unknown` for an invalid identity.
         */
        [[nodiscard]] const RendererFeatureInfo& GetFeature(RendererFeature feature) const;

        /**
         * @brief Tests whether the complete feature contract is supported.
         * @param feature Feature to inspect.
         * @return True only for `RendererFeatureSupport::Supported`.
         */
        [[nodiscard]] bool Supports(RendererFeature feature) const;

        /**
         * @brief Returns one numeric limit.
         * @param limit Limit to inspect.
         * @return The value and its known/unknown state.
         */
        [[nodiscard]] RendererLimitValue GetLimit(RendererLimit limit) const;

        /**
         * @brief Returns support masks for a `SurfaceFormat` ordinal.
         * @param surfaceFormatOrdinal Ordinal from `SurfaceFormat`.
         * @return Known/supported masks, or an all-unknown result for an invalid ordinal.
         */
        [[nodiscard]] RendererFormatSupport GetSurfaceFormatSupport(
            std::uint32_t surfaceFormatOrdinal) const;

        /**
         * @brief Returns renderer-supplied English limitations not represented as machine fields.
         * @return Stable UTF-8 text owned by this snapshot.
         */
        [[nodiscard]] std::string_view GetAdditionalLimitationsText() const;

        /**
         * @brief Returns the generated complete English capability report.
         * @return Stable UTF-8 text owned by this snapshot.
         */
        [[nodiscard]] std::string_view GetEnglishReport() const;

    private:
        friend class Microsoft::Xna::Framework::Graphics::GraphicsDevice;

        static constexpr std::size_t FeatureCount =
            static_cast<std::size_t>(RendererFeature::Count);
        static constexpr std::size_t LimitCount =
            static_cast<std::size_t>(RendererLimit::Count);
        static constexpr std::size_t SurfaceFormatCount = 27;

        void SetFeature(RendererFeature feature, RendererFeatureSupport support,
                        std::string note = {});
        void SetLimit(RendererLimit limit, bool known, std::uint64_t value);
        void SetSurfaceFormat(std::uint32_t ordinal, std::string_view name,
                              RendererFormatSupport support);
        void BuildEnglishReport();

        std::string rendererName_;
        std::array<RendererFeatureInfo, FeatureCount> features_{};
        std::array<RendererLimitValue, LimitCount> limits_{};
        std::array<RendererFormatSupport, SurfaceFormatCount> formats_{};
        std::array<std::string, SurfaceFormatCount> formatNames_{};
        std::string additionalLimitationsText_;
        std::string englishReport_;
    };

    /**
     * @brief Returns every detailed renderer feature in stable numeric order.
     * @return Complete immutable feature span.
     */
    [[nodiscard]] std::span<const RendererFeature> AllRendererFeatures();

    /**
     * @brief Returns the stable English identifier of one feature.
     * @param feature Feature to name.
     * @return Identifier, or `"UnknownRendererFeature"` for an invalid value.
     */
    [[nodiscard]] std::string_view GetRendererFeatureName(RendererFeature feature);

    /**
     * @brief Returns the stable English description of one feature contract.
     * @param feature Feature to describe.
     * @return Description, or an invalid-feature description.
     */
    [[nodiscard]] std::string_view GetRendererFeatureDescription(RendererFeature feature);

    /**
     * @brief Returns every numeric renderer limit in stable order.
     * @return Complete immutable limit span.
     */
    [[nodiscard]] std::span<const RendererLimit> AllRendererLimits();

    /**
     * @brief Returns the stable English identifier of one numeric limit.
     * @param limit Limit to name.
     * @return Identifier, or `"UnknownRendererLimit"` for an invalid value.
     */
    [[nodiscard]] std::string_view GetRendererLimitName(RendererLimit limit);
} // namespace CNA
