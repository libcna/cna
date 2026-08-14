// SPDX-License-Identifier: MS-PL
#pragma once

// plan_wicked.md: CNA's WICKED graphics renderer, implemented on top of Wicked Engine's render
// hardware interface (wi::graphics::GraphicsDevice), which itself dispatches to Vulkan on
// Linux/Windows and to D3D12 on Windows.
//
// Only the RHI layer of Wicked Engine is used. wi::renderer, wi::scene, wi::Application, wi::input
// and the physics/scripting layers are deliberately not linked into CNA's own runtime: CNA already
// owns the XNA 4.0 programming model above this boundary and needs a device abstraction below it,
// not a second engine (plan_wicked.md design decision 1).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

#include "wiGraphics.h"
#include "wiGraphicsDevice.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::Wicked
{
    namespace wig = wi::graphics;

    class WickedRenderer;

    /**
     * @brief CNAEXT. Which of the renderer's built-in shader programs a draw uses.
     *
     * CNA describes a vertex layout by its byte stride (see `IGraphicsRenderer`'s own
     * `combinedVertexStride` contract), so the variant is chosen by stride exactly as the D3D11,
     * D3D12 and Vulkan renderers choose theirs. `Basic24` doubles as the `SpriteBatch`, clear-quad
     * and present-blit layout: a sprite vertex is position + packed colour + UV, which is
     * byte-for-byte `VertexPositionColorTexture`.
     */
    CNAEXT enum class WickedShaderVariant : std::uint32_t
    {
        /** @brief Stride 16 — `VertexPositionColor`. */
        Basic16 = 0,
        /** @brief Stride 20 — `VertexPositionTexture`. */
        Basic20 = 1,
        /** @brief Stride 24 — `VertexPositionColorTexture`, and every internal quad route. */
        Basic24 = 2,
        /** @brief Stride 32 — `VertexPositionNormalTexture`. */
        Basic32 = 3,
        /** @brief Stride 48 — `VertexPositionNormalTangentTexture`. */
        Basic48 = 4,
        /** @brief Stride 52 — `VertexPositionNormalTextureSkinned`. */
        Basic52 = 5,
        /** @brief Stride 56 — the stride-52 layout with a per-vertex `Color` appended. */
        Basic56 = 6,
        /** @brief Stride 68 — `VertexPositionNormalTangentTextureSkinned`. */
        Basic68 = 7,
        /** @brief Number of variants. */
        Count = 8
    };

    /**
     * @brief CNAEXT. How many leading variants also have an instanced sibling.
     *
     * Instancing is offered for the four narrow stock layouts only. The wider tangent/skinned
     * layouts exist to carry `PbrEffect`/`SkinnedEffect` geometry, and neither of those effects has
     * an instanced form in CNA, so compiling four more entry points nothing can reach would be
     * dead weight. An instanced draw on a wider stride is refused rather than silently downgraded.
     */
    CNAEXT inline constexpr std::size_t kWickedInstancedVariantCount = 4;

    /**
     * @brief CNAEXT. How many simultaneous colour targets this renderer binds.
     *
     * XNA 4.0 HiDef's own ceiling. The stock pixel shaders write `SV_Target` only, so slots 1..3
     * receive whatever their `BlendState.ColorWriteChannels` mask lets through and nothing more --
     * the same thing XNA does when a stock effect is drawn into an MRT set.
     */
    CNAEXT inline constexpr int kWickedMaxRenderTargets = 4;

    /**
     * @brief CNAEXT. How many independent regions a CPU-writable buffer is split into.
     *
     * The renderer's vertex and index buffers live in `Usage::UPLOAD` memory the CPU writes
     * directly, so writing one the GPU may still be reading is a real hazard. Each write (other
     * than `SetDataOptions::NoOverwrite`) moves to the next region instead — buffer orphaning,
     * which is exactly what `Discard` asks for and a valid way to satisfy `None`.
     *
     * Wicked Engine keeps `GraphicsDevice::GetBufferCount()` (2) frames in flight, so two regions
     * is the minimum that works; the third is slack for a second write within one frame. A game
     * that rewrites the same buffer more than three times per frame can still outrun it — that
     * limitation is documented rather than hidden, since detecting it needs per-region fences.
     */
    CNAEXT inline constexpr int kWickedBufferRegions = 3;

    /** @brief CNAEXT. The first variant that carries blend weights, and so can be skinned. */
    CNAEXT inline constexpr std::size_t kWickedFirstSkinnableVariant =
        static_cast<std::size_t>(WickedShaderVariant::Basic52);
    /** @brief CNAEXT. How many variants can be skinned (strides 52, 56 and 68). */
    CNAEXT inline constexpr std::size_t kWickedSkinnableVariantCount =
        static_cast<std::size_t>(WickedShaderVariant::Count) - kWickedFirstSkinnableVariant;

    /**
     * @brief CNAEXT. `SkinnedEffect`'s bone palette, bound as its own constant buffer.
     *
     * Kept out of @ref WickedShaderConstants deliberately: at 4608 bytes of matrices it dwarfs
     * every other per-draw constant, and copying it into the frame allocator on draws that do no
     * skinning at all would be pure waste. It is uploaded only for a skinned draw.
     */
    CNAEXT struct WickedBoneConstants
    {
        /** @brief Columns of up to 72 bone matrices, in CNA's raw `Matrix` byte order. */
        float bones[72 * 16] = {};
        /** @brief x = `WeightsPerVertex` (1, 2 or 4); the rest is padding. */
        float skinParams[4] = {4.0f, 0.0f, 0.0f, 0.0f};
    };

    /**
     * @brief CNAEXT. The complete, hashable description of one graphics pipeline state.
     *
     * Wicked Engine's `PipelineStateDesc` holds raw pointers into caller-owned `BlendState` /
     * `RasterizerState` / `DepthStencilState` / `InputLayout` objects that must outlive the
     * pipeline, so the renderer cannot build one from a stack temporary per draw. This POD carries
     * the raw CNA state ordinals the `Apply*State` methods already receive; the cache below keys on
     * it and owns the converted Wicked state structs for as long as the pipeline exists.
     *
     * It is compared and hashed byte-wise, so it must stay a POD with no padding holes left
     * uninitialised — always create one through value-initialisation (`WickedPipelineKey key{};`).
     */
    CNAEXT struct WickedPipelineKey
    {
        std::uint32_t variant = 0;            ///< WickedShaderVariant ordinal.
        std::uint32_t instanced = 0;          ///< Non-zero when the per-instance input layout is used.
        std::uint32_t envMap = 0;             ///< Non-zero when the EnvironmentMapEffect program is used.
        std::uint32_t skinned = 0;            ///< Non-zero when the SkinnedEffect program is used.
        std::uint32_t pbr = 0;                ///< Non-zero when the PbrEffect program is used.
        std::uint32_t topology = 0;           ///< wi::graphics::PrimitiveTopology ordinal.
        std::uint32_t blendEnabled = 0;       ///< Non-zero when colour blending is on.
        std::int32_t  colorSrcBlend = 0;      ///< Raw XNA Blend ordinal.
        std::int32_t  alphaSrcBlend = 0;      ///< Raw XNA Blend ordinal.
        std::int32_t  colorDstBlend = 0;      ///< Raw XNA Blend ordinal.
        std::int32_t  alphaDstBlend = 0;      ///< Raw XNA Blend ordinal.
        std::int32_t  colorBlendFunc = 0;     ///< Raw XNA BlendFunction ordinal.
        std::int32_t  alphaBlendFunc = 0;     ///< Raw XNA BlendFunction ordinal.
        std::int32_t  colorWriteChannels[4] = {15, 15, 15, 15}; ///< Per-MRT-slot XNA ColorWriteChannels.
        std::uint32_t multiSampleMask = 0xFFFFFFFFu;            ///< XNA BlendState.MultiSampleMask.
        std::uint32_t depthEnable = 0;        ///< Non-zero when the depth test is on.
        std::uint32_t depthWriteEnable = 0;   ///< Non-zero when depth writes are on.
        std::int32_t  depthFunc = 0;          ///< Raw XNA CompareFunction ordinal.
        std::uint32_t stencilEnable = 0;      ///< Non-zero when stencil testing is on.
        std::int32_t  stencilFunc = 0;        ///< Raw XNA CompareFunction ordinal.
        std::int32_t  stencilPass = 0;        ///< Raw XNA StencilOperation ordinal.
        std::int32_t  stencilFail = 0;        ///< Raw XNA StencilOperation ordinal.
        std::int32_t  stencilDepthFail = 0;   ///< Raw XNA StencilOperation ordinal.
        std::int32_t  stencilMask = 0xFF;     ///< XNA DepthStencilState.StencilMask.
        std::int32_t  stencilWriteMask = 0xFF;///< XNA DepthStencilState.StencilWriteMask.
        std::uint32_t twoSidedStencil = 0;    ///< Non-zero when the CCW face uses its own ops.
        std::int32_t  ccwStencilFunc = 0;     ///< Raw XNA CompareFunction ordinal.
        std::int32_t  ccwStencilPass = 0;     ///< Raw XNA StencilOperation ordinal.
        std::int32_t  ccwStencilFail = 0;     ///< Raw XNA StencilOperation ordinal.
        std::int32_t  ccwStencilDepthFail = 0;///< Raw XNA StencilOperation ordinal.
        std::int32_t  cullMode = 0;           ///< Raw XNA CullMode ordinal.
        std::int32_t  fillMode = 0;           ///< Raw XNA FillMode ordinal.
        float         depthBias = 0.0f;       ///< XNA RasterizerState.DepthBias.
        float         slopeScaleDepthBias = 0.0f; ///< XNA RasterizerState.SlopeScaleDepthBias.
        /**
         * @brief Bound per-vertex streams, or 0 for the single-stream layouts.
         *
         * A `VertexDeclaration` whose elements are split across several buffers needs its own
         * input layout, so the split has to be part of what selects a pipeline. Zero here means
         * "the variant's own single-slot layout", which is what every draw that binds one buffer
         * uses and keeps its key byte-identical to what it was before multi-stream input existed.
         */
        std::uint32_t streamCount = 0;
        /** @brief Each stream's byte offset inside the combined vertex. */
        std::int32_t  streamBase[kMaxVertexStreams] = {};
        /** @brief Each stream's own declaration stride, in bytes. */
        std::int32_t  streamStride[kMaxVertexStreams] = {};

        /** @brief Byte-wise equality, so a new state field cannot be forgotten here. */
        [[nodiscard]] bool operator==(const WickedPipelineKey& other) const noexcept;
    };

    /** @brief CNAEXT. Byte-wise FNV-1a hash of a WickedPipelineKey. */
    CNAEXT struct WickedPipelineKeyHash
    {
        /** @brief Hashes @p key byte-wise. */
        [[nodiscard]] std::size_t operator()(const WickedPipelineKey& key) const noexcept;
    };

    /**
     * @brief CNAEXT. One cached pipeline plus the state objects Wicked Engine keeps pointers into.
     */
    CNAEXT struct WickedPipelineEntry
    {
        wig::BlendState        blend;        ///< Owned blend state referenced by @ref pipeline.
        wig::RasterizerState   rasterizer;   ///< Owned rasterizer state referenced by @ref pipeline.
        wig::DepthStencilState depthStencil; ///< Owned depth/stencil state referenced by @ref pipeline.
        /// Re-slotted input layout, used only when the key describes a multi-stream draw.
        wig::InputLayout       inputLayout;
        wig::PipelineState     pipeline;     ///< The compiled pipeline.
    };

    /**
     * @brief CNAEXT. Constants shared by every built-in shader variant.
     *
     * The two matrices are stored as their four COLUMNS, so the shader evaluates XNA's own
     * row-vector product (`clip = position * matrix`) with four `dot()`s and no packing-convention
     * assumption at the HLSL boundary.
     */
    CNAEXT struct WickedShaderConstants
    {
        float mvp[16] = {};              ///< Columns of `world * view * projection`.
        float world[16] = {};            ///< Columns of the world matrix (lighting/fog space).
        float diffuse[4] = {1, 1, 1, 1}; ///< Material diffuse RGB + alpha.
        float emissive[4] = {};          ///< Emissive RGB + specular power in `w`.
        float specular[4] = {};          ///< Material specular RGB + `w` unused.
        float alphaTest[4] = {0, 0, 1, 1}; ///< x=ref, y=tolerance, z=pass weight, w=fail weight.
        float fogColor[4] = {};          ///< Fog RGB + `w` non-zero when fog is enabled.
        float fogVector[4] = {};         ///< FNA's `EffectHelpers.SetFogVector` fog vector.
        float ambient[4] = {};           ///< Ambient RGB + `w` unused.
        float lightDir[3][4] = {};       ///< Directional light directions (world space).
        float lightDiffuse[3][4] = {};   ///< Directional light diffuse colours.
        float lightSpecular[3][4] = {};  ///< Directional light specular colours.
        float eyePosition[4] = {};       ///< Camera world position + `w` unused.
        float flags[4] = {};             ///< x=texture, y=vertex colour, z=lighting, w=dual texture.
        float worldInverseTranspose[16] = {}; ///< Columns of the world inverse-transpose.
        float envMapParams[4] = {};      ///< x=amount, y=Fresnel enabled, z=Fresnel factor.
        float envMapSpecular[4] = {};    ///< `EnvironmentMapEffect.EnvironmentMapSpecular` RGB.
        float pbrFactors[4] = {1.0f, 1.0f, 1.0f, 1.0f}; ///< metallic, roughness, normal scale, occlusion strength.
        float pbrSrgb[4] = {};           ///< decode base, decode emissive, encode PBR output.
        float pbrDielectricFresnel[4] = {}; ///< xyz=dielectric F0, w=dielectric F90.
    };

    /**
     * @brief CNAEXT. A Wicked Engine texture exposed to CNA as an `ITextureRenderer`.
     */
    class WickedTextureRenderer : public ITextureRenderer
    {
    public:
        /**
         * @brief Wraps an already-created Wicked Engine texture.
         *
         * @param owner   Renderer that created @p texture; used for readback and uploads.
         * @param texture The GPU resource. Ownership moves into this object.
         */
        WickedTextureRenderer(WickedRenderer* owner, wig::Texture texture);
        /** @brief Releases this renderer's reference to the GPU texture. */
        ~WickedTextureRenderer() override;

        WickedTextureRenderer(const WickedTextureRenderer&) = delete;
        WickedTextureRenderer& operator=(const WickedTextureRenderer&) = delete;

        /** @brief Width of mip level 0, in texels. */
        [[nodiscard]] int GetWidth() const override;
        /** @brief Height of mip level 0, in texels. */
        [[nodiscard]] int GetHeight() const override;
        /** @brief Always null — this renderer never creates an SDL_Renderer texture. */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        /** @brief Replaces every texel of mip level 0 with tightly packed RGBA8 rows. */
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        /** @brief Replaces every texel of mip level @p level. */
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelW, int levelH) override;
        /**
         * @brief Reads a sub-rectangle of @p level back into @p data as tightly packed RGBA8 rows.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in texels.
         * @param y          Top edge of the region, in texels.
         * @param w          Width of the region, in texels.
         * @param h          Height of the region, in texels.
         * @param data       Destination buffer, top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * 4`.
         * @return True when the whole region was written; false when nothing was read back.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. The underlying Wicked Engine resource, for the owning renderer's binds. */
        [[nodiscard]] const wig::Texture& GetTextureEXT() const { return texture_; }

    protected:
        WickedRenderer* owner_ = nullptr; ///< Renderer that owns the device.
        wig::Texture texture_;                   ///< The GPU resource.
    };

    /**
     * @brief CNAEXT. A cube map exposed to CNA as an `ITextureCubeRenderer`.
     *
     * Upload and readback only. This renderer has no `EnvironmentMapEffect` shader variant yet
     * (plan_wicked.md WICKED-56), so a cube map cannot currently be sampled by a draw — it can be
     * filled and read back, and `SetData`/`GetData` report honestly whether they did so.
     */
    class WickedTextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        /**
         * @brief Creates a six-face cube map.
         *
         * @param owner  Renderer that owns the device.
         * @param size   Width and height of each face, in texels.
         * @param mipMap True when a full mip chain was requested.
         */
        WickedTextureCubeRenderer(WickedRenderer* owner, int size, bool mipMap);
        /** @brief Releases the GPU texture. */
        ~WickedTextureCubeRenderer() override;

        WickedTextureCubeRenderer(const WickedTextureCubeRenderer&) = delete;
        WickedTextureCubeRenderer& operator=(const WickedTextureCubeRenderer&) = delete;

        /**
         * @brief Uploads tightly packed RGBA8 rows into a sub-rectangle of one cube face.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to write.
         * @param x          Left edge of the region, in texels.
         * @param y          Top edge of the region, in texels.
         * @param w          Width of the region, in texels.
         * @param h          Height of the region, in texels.
         * @param data       Source pixels, top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * 4`.
         * @return True when the whole region was stored; false when nothing was stored.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /**
         * @brief Reads a sub-rectangle of one cube face back as tightly packed RGBA8 rows.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in texels.
         * @param y          Top edge of the region, in texels.
         * @param w          Width of the region, in texels.
         * @param h          Height of the region, in texels.
         * @param data       Destination buffer, top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * 4`.
         * @return True when the whole region was written; false when nothing was read back.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. The underlying Wicked Engine resource. */
        [[nodiscard]] const wig::Texture& GetTextureEXT() const { return texture_; }

    private:
        WickedRenderer* owner_ = nullptr;
        wig::Texture texture_;
        int size_ = 0;
        std::uint32_t mipLevels_ = 1;
    };

    /**
     * @brief CNAEXT. A volume texture exposed to CNA as an `ITexture3DRenderer`.
     */
    class WickedTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        /**
         * @brief Creates a volume texture.
         *
         * @param owner  Renderer that owns the device.
         * @param width  Width in voxels.
         * @param height Height in voxels.
         * @param depth  Depth in voxels.
         * @param mipMap True when a full mip chain was requested.
         */
        WickedTexture3DRenderer(WickedRenderer* owner, int width, int height, int depth,
                               bool mipMap);
        /** @brief Releases the GPU texture. */
        ~WickedTexture3DRenderer() override;

        WickedTexture3DRenderer(const WickedTexture3DRenderer&) = delete;
        WickedTexture3DRenderer& operator=(const WickedTexture3DRenderer&) = delete;

        /**
         * @brief Uploads a tightly packed RGBA8 box into the given mip level.
         *
         * @param level      Mip level to write.
         * @param x          Left edge of the box, in voxels.
         * @param y          Top edge of the box, in voxels.
         * @param z          Front edge of the box, in voxels.
         * @param w          Width of the box, in voxels.
         * @param h          Height of the box, in voxels.
         * @param depth      Depth of the box, in voxels.
         * @param data       Source voxels, slice by slice, each slice top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * depth * 4`.
         * @return True when the whole box was stored; false when nothing was stored.
         */
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /**
         * @brief Reads a box of the given mip level back as tightly packed RGBA8 voxels.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the box, in voxels.
         * @param y          Top edge of the box, in voxels.
         * @param z          Front edge of the box, in voxels.
         * @param w          Width of the box, in voxels.
         * @param h          Height of the box, in voxels.
         * @param depth      Depth of the box, in voxels.
         * @param data       Destination buffer, slice by slice, each slice top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * depth * 4`.
         * @return True when the whole box was written; false when nothing was read back.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. The underlying Wicked Engine resource. */
        [[nodiscard]] const wig::Texture& GetTextureEXT() const { return texture_; }

    private:
        WickedRenderer* owner_ = nullptr;
        wig::Texture texture_;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        std::uint32_t mipLevels_ = 1;
    };

    /**
     * @brief CNAEXT. An off-screen colour target (plus optional depth/stencil) CNA can render into.
     */
    class WickedRenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        /**
         * @brief Creates the colour texture and, when @p depthFormat is not `None`, its depth buffer.
         *
         * @param owner            Renderer that owns the device.
         * @param width            Target width in pixels.
         * @param height           Target height in pixels.
         * @param depthFormat      Raw `Microsoft::Xna::Framework::Graphics::DepthFormat` ordinal.
         * @param preserveContents True when the target's `RenderTargetUsage` preserves contents.
         * @param mipMap           True when a full mip chain was requested.
         * @param multiSampleCount Requested MSAA sample count; 0 or 1 means none.
         */
        WickedRenderTargetRenderer(WickedRenderer* owner, int width, int height,
                                  int depthFormat, bool preserveContents, bool mipMap,
                                  int multiSampleCount);
        /** @brief Releases the colour and depth resources. */
        ~WickedRenderTargetRenderer() override;

        WickedRenderTargetRenderer(const WickedRenderTargetRenderer&) = delete;
        WickedRenderTargetRenderer& operator=(const WickedRenderTargetRenderer&) = delete;

        /** @brief Target width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Target height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }
        /** @brief Always null — this renderer never creates an SDL_Renderer texture. */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        /** @brief Makes this target the destination of subsequent draws. */
        void BindAsRenderTarget() override;
        /** @brief Restores the back buffer as the destination of subsequent draws. */
        void UnbindAsRenderTarget() override;
        /** @brief The device-clamped MSAA sample count this target was created with; 0 if none. */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        /** @brief Whether a real depth/stencil buffer backs this target. */
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return depth_.IsValid();
        }
        /**
         * @brief Reads a sub-rectangle of @p level back into @p data as tightly packed RGBA8 rows.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in pixels.
         * @param y          Top edge of the region, in pixels.
         * @param w          Width of the region, in pixels.
         * @param h          Height of the region, in pixels.
         * @param data       Destination buffer, top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * 4`.
         * @return True when the whole region was written; false when nothing was read back.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. The colour attachment this target renders into. */
        [[nodiscard]] const wig::Texture& GetColorTextureEXT() const { return color_; }
        /**
         * @brief CNAEXT. The texture to sample or copy from.
         *
         * For a multisampled target this is the single-sample resolve destination, not the
         * attachment: a multisampled image can be neither sampled by the ordinary shaders nor
         * copied by a plain `CopyTexture`.
         */
        [[nodiscard]] const wig::Texture& GetSampleableTextureEXT() const
        {
            return resolve_.IsValid() ? resolve_ : color_;
        }
        /** @brief CNAEXT. The resolve destination, or an invalid handle when MSAA is off. */
        [[nodiscard]] const wig::Texture& GetResolveTextureEXT() const { return resolve_; }
        /** @brief CNAEXT. The depth texture, or an invalid handle when none was allocated. */
        [[nodiscard]] const wig::Texture& GetDepthTextureEXT() const { return depth_; }
        /** @brief CNAEXT. Whether a bind must load the existing contents instead of discarding. */
        [[nodiscard]] bool PreservesContentsEXT() const { return preserveContents_; }
        /** @brief CNAEXT. Marks that this target has been rendered into at least once. */
        void MarkRenderedEXT() { hasContent_ = true; }
        /** @brief CNAEXT. Whether anything has been rendered into this target yet. */
        [[nodiscard]] bool HasContentEXT() const { return hasContent_; }

    private:
        WickedRenderer* owner_ = nullptr;
        wig::Texture color_;
        wig::Texture resolve_;
        wig::Texture depth_;
        int width_ = 0;
        int height_ = 0;
        int multiSampleCount_ = 0;
        bool preserveContents_ = false;
        bool hasContent_ = false;
    };

    /**
     * @brief CNAEXT. A cube map CNA can render each face of, and sample as a whole.
     *
     * One face is bound at a time through a per-face render-target subresource view, created once
     * at construction. The whole-cube shader-resource view is the resource's default, so a
     * dynamically rendered cube map is sampled by `EnvironmentMapEffect` exactly like an uploaded
     * one.
     */
    class WickedRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
    {
    public:
        /**
         * @brief Creates the cube colour target and, when requested, its depth buffer.
         *
         * @param owner            Renderer that owns the device.
         * @param size             Width and height of each face, in pixels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents True when the target's `RenderTargetUsage` preserves contents.
         * @param mipMap           True when a full mip chain was requested.
         * @param multiSampleCount Requested MSAA sample count; 0 or 1 means none.
         */
        WickedRenderTargetCubeRenderer(WickedRenderer* owner, int size, int depthFormat,
                                      bool preserveContents, bool mipMap, int multiSampleCount);
        /** @brief Releases the colour and depth resources. */
        ~WickedRenderTargetCubeRenderer() override;

        WickedRenderTargetCubeRenderer(const WickedRenderTargetCubeRenderer&) = delete;
        WickedRenderTargetCubeRenderer& operator=(const WickedRenderTargetCubeRenderer&) = delete;

        /** @brief Width and height of each cube face, in pixels. */
        [[nodiscard]] int GetSize() const override { return size_; }
        /** @brief Makes face @p face the destination of subsequent draws. */
        void BindAsRenderTargetFace(int face) override;
        /** @brief Restores the back buffer as the destination of subsequent draws. */
        void UnbindAsRenderTarget() override;
        /** @brief The device-clamped MSAA sample count this target was created with; 0 if none. */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        /** @brief Whether a real depth/stencil buffer backs this target. */
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return depth_.IsValid();
        }
        /**
         * @brief Uploads tightly packed RGBA8 rows into a sub-rectangle of one rendered face.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to write.
         * @param x          Left edge of the region, in pixels.
         * @param y          Top edge of the region, in pixels.
         * @param w          Width of the region, in pixels.
         * @param h          Height of the region, in pixels.
         * @param data       Source pixels, top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * 4`.
         * @return True when the whole region was stored; false when nothing was stored.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /**
         * @brief Reads a sub-rectangle of one rendered face back as tightly packed RGBA8 rows.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in pixels.
         * @param y          Top edge of the region, in pixels.
         * @param w          Width of the region, in pixels.
         * @param h          Height of the region, in pixels.
         * @param data       Destination buffer, top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * 4`.
         * @return True when the whole region was written; false when nothing was read back.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. The cube colour texture, for binding as a shader resource. */
        [[nodiscard]] const wig::Texture& GetColorTextureEXT() const { return color_; }
        /** @brief CNAEXT. The depth texture, or an invalid handle when none was allocated. */
        [[nodiscard]] const wig::Texture& GetDepthTextureEXT() const { return depth_; }
        /** @brief CNAEXT. The render-target subresource index of @p face. */
        [[nodiscard]] int RenderTargetSubresourceEXT(int face) const;
        /** @brief CNAEXT. Whether a bind must load the existing contents instead of discarding. */
        [[nodiscard]] bool PreservesContentsEXT() const { return preserveContents_; }
        /** @brief CNAEXT. Marks that @p face has been rendered into at least once. */
        void MarkFaceRenderedEXT(int face);
        /** @brief CNAEXT. Whether anything has been rendered into @p face yet. */
        [[nodiscard]] bool HasFaceContentEXT(int face) const;

    private:
        WickedRenderer* owner_ = nullptr;
        wig::Texture color_;
        wig::Texture depth_;
        std::array<int, 6> faceSubresources_{};
        std::array<bool, 6> faceHasContent_{};
        int size_ = 0;
        int multiSampleCount_ = 0;
        bool preserveContents_ = false;
    };

    /**
     * @brief CNAEXT. A CPU-writable vertex buffer.
     */
    class WickedVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        /**
         * @brief Allocates a buffer able to hold @p vertexCapacity vertices of any supported stride.
         *
         * @param owner          Renderer that owns the device.
         * @param vertexCapacity Maximum number of vertices.
         */
        WickedVertexBufferRenderer(WickedRenderer* owner, int vertexCapacity);
        /** @brief Releases the GPU buffer. */
        ~WickedVertexBufferRenderer() override;

        WickedVertexBufferRenderer(const WickedVertexBufferRenderer&) = delete;
        WickedVertexBufferRenderer& operator=(const WickedVertexBufferRenderer&) = delete;

        /**
         * @brief Uploads @p vertexCount vertices of @p strideInBytes bytes each.
         *
         * Equivalent to SetDataWithOptions() with `SetDataOptions::None`.
         *
         * @param data           Packed vertex data.
         * @param vertexCount    Number of vertices.
         * @param strideInBytes  Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;
        /**
         * @brief Uploads vertex data, honouring the caller's streaming hint.
         *
         * `Discard` and `None` both move to the next buffer region so the write cannot land on
         * memory a queued draw may still read. `NoOverwrite` keeps the current region, which is
         * the caller's explicit promise that it will not.
         *
         * @param data           Packed vertex data.
         * @param vertexCount    Number of vertices.
         * @param strideInBytes  Size of one vertex in bytes.
         * @param options        Streaming hint.
         */
        void SetDataWithOptions(const void* data, int vertexCount, std::size_t strideInBytes,
                                SetDataOptions options) override;
        /**
         * @brief Records the caller's complete vertex declaration.
         *
         * The stride is what selects the shader variant and input layout; the element list is
         * retained so `RequireFaithfulDeclarationEXT()` can refuse, at draw time, a declaration
         * that this renderer's stride table would otherwise reinterpret (REMED-GFX-DECL-GUARD).
         *
         * @param vertexDeclaration Full declaration, in declaration order.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;
        /** @brief Number of vertices most recently uploaded. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief CNAEXT. The underlying Wicked Engine buffer. */
        [[nodiscard]] const wig::GPUBuffer& GetBufferEXT() const { return buffer_; }
        /** @brief CNAEXT. Byte stride of the most recent upload, or 0 when nothing was uploaded. */
        [[nodiscard]] std::size_t GetStrideEXT() const { return stride_; }
        /** @brief CNAEXT. Byte offset of the region the most recent upload landed in. */
        [[nodiscard]] std::uint64_t GetByteOffsetEXT() const;
        /** @brief CNAEXT. The declaration this buffer carries, for the draw-time fidelity check. */
        [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& GetDeclarationEXT() const
        {
            return declaration_;
        }

    private:
        void EnsureStorage(std::size_t strideInBytes);

        WickedRenderer* owner_ = nullptr;
        wig::GPUBuffer buffer_;
        int capacity_ = 0;
        int vertexCount_ = 0;
        int regionIndex_ = 0;
        std::size_t regionSizeBytes_ = 0;
        std::size_t stride_ = 0;
        std::size_t declaredStride_ = 0;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    /**
     * @brief CNAEXT. REMED-GFX-DECL-GUARD: refuses a vertex declaration this renderer cannot
     * represent faithfully.
     *
     * `VariantForStride()` picks the input layout and vertex program from the buffer's byte
     * stride alone, so a declaration packing different semantics into one of the eight known
     * widths would be read from the wrong bytes and rendered without any error. The check is
     * **asymmetric** — only what the caller actually declared is verified, never equality against
     * this renderer's own template — so a declaration that omits attributes the template carries
     * still draws. It is pure: nothing is created, queued or bound before it runs, so a rejected
     * draw cannot leave the device in a partial state. An unlisted stride is left to
     * `VariantForStride()`'s own established refusal.
     *
     * Header-only by necessity: `cna_renderer_wicked` links only
     * `cna_renderer_common` and SharpRuntime, never the CNA library.
     *
     * @param vb    The vertex buffer whose declaration is being checked.
     * @param route Name of the draw route, for the diagnostic message.
     * @throws System::NotSupportedException When the declaration cannot be represented.
     */
    inline void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
    {
        const auto& wickedVb = static_cast<const WickedVertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            wickedVb.GetDeclarationEXT(), static_cast<int>(wickedVb.GetStrideEXT()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt, "Wicked", route);
    }

    /**
     * @brief CNAEXT. A CPU-writable 16- or 32-bit index buffer.
     */
    class WickedIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Allocates a buffer able to hold @p indexCapacity indices.
         *
         * @param owner         Renderer that owns the device.
         * @param indexCapacity Maximum number of indices.
         * @param thirtyTwoBit  True for 32-bit indices, false for 16-bit.
         */
        WickedIndexBufferRenderer(WickedRenderer* owner, int indexCapacity, bool thirtyTwoBit);
        /** @brief Releases the GPU buffer. */
        ~WickedIndexBufferRenderer() override;

        WickedIndexBufferRenderer(const WickedIndexBufferRenderer&) = delete;
        WickedIndexBufferRenderer& operator=(const WickedIndexBufferRenderer&) = delete;

        /**
         * @brief Uploads @p indexCount 16-bit indices.
         * @param data       Packed index data.
         * @param indexCount Number of indices.
         */
        void SetData16(const void* data, int indexCount) override;
        /**
         * @brief Uploads @p indexCount 32-bit indices.
         * @param data       Packed index data.
         * @param indexCount Number of indices.
         */
        void SetData32(const void* data, int indexCount) override;
        /**
         * @brief Uploads 16-bit indices, honouring the caller's streaming hint.
         * @param data       Packed index data.
         * @param indexCount Number of indices.
         * @param options    Streaming hint; see WickedVertexBufferRenderer::SetDataWithOptions.
         */
        void SetData16WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        /**
         * @brief Uploads 32-bit indices, honouring the caller's streaming hint.
         * @param data       Packed index data.
         * @param indexCount Number of indices.
         * @param options    Streaming hint; see WickedVertexBufferRenderer::SetDataWithOptions.
         */
        void SetData32WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        /** @brief Number of indices most recently uploaded. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        /** @brief Whether this buffer stores 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief CNAEXT. The underlying Wicked Engine buffer. */
        [[nodiscard]] const wig::GPUBuffer& GetBufferEXT() const { return buffer_; }
        /** @brief CNAEXT. Byte offset of the region the most recent upload landed in. */
        [[nodiscard]] std::uint64_t GetByteOffsetEXT() const;

    private:
        void Upload(const void* data, int indexCount, std::size_t indexSize,
                    SetDataOptions options);

        WickedRenderer* owner_ = nullptr;
        wig::GPUBuffer buffer_;
        int capacity_ = 0;
        int indexCount_ = 0;
        int regionIndex_ = 0;
        std::size_t regionSizeBytes_ = 0;
        bool thirtyTwoBit_ = false;
    };

    /**
     * @brief CNAEXT. A real GPU occlusion query backed by a Wicked Engine query heap.
     */
    class WickedOcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        /**
         * @brief Allocates a one-slot occlusion query heap and its readback buffer.
         * @param owner Renderer that owns the device.
         */
        explicit WickedOcclusionQueryRenderer(WickedRenderer* owner);
        /** @brief Releases the query heap and readback buffer. */
        ~WickedOcclusionQueryRenderer() override;

        WickedOcclusionQueryRenderer(const WickedOcclusionQueryRenderer&) = delete;
        WickedOcclusionQueryRenderer& operator=(const WickedOcclusionQueryRenderer&) = delete;

        /** @brief Starts counting samples that pass the depth test. */
        void Begin() override;
        /** @brief Stops counting and resolves the result into the readback buffer. */
        void End() override;
        /** @brief Whether the GPU has finished the frame the query was resolved in. */
        [[nodiscard]] bool IsComplete() const override;
        /** @brief Number of samples that passed the depth test between Begin() and End(). */
        [[nodiscard]] int PixelCount() const override;

    private:
        WickedRenderer* owner_ = nullptr;
        wig::GPUQueryHeap heap_;
        wig::GPUBuffer readback_;
        std::uint64_t resolvedFrame_ = 0;
        bool active_ = false;
        bool resolved_ = false;
    };

    /**
     * @brief CNAEXT. The batched 2D sprite renderer.
     *
     * Sprites accumulate into a CPU-side vertex array between Begin() and End() and are flushed as
     * one draw per contiguous run of the same texture, exactly as the other GPU renderers batch.
     */
    class WickedSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /**
         * @brief Binds this sprite batch to its owning renderer.
         * @param owner Renderer that owns the device.
         */
        explicit WickedSpriteBatchRenderer(WickedRenderer* owner);
        /** @brief Flushes anything still queued. */
        ~WickedSpriteBatchRenderer() override;

        WickedSpriteBatchRenderer(const WickedSpriteBatchRenderer&) = delete;
        WickedSpriteBatchRenderer& operator=(const WickedSpriteBatchRenderer&) = delete;

        /** @brief Starts a batch; resets the queued sprite list. */
        void Begin() override;
        /** @brief Ends the batch, submitting every queued sprite. */
        void End() override;
        /** @brief Sets the transform applied on top of the 2D orthographic projection. */
        void SetTransformMatrix(const Matrix& m) override;
        /** @brief Sets the sampler filter applied to each draw (raw XNA TextureFilter ordinal). */
        void SetSamplerFilter(int textureFilter) override;
        /**
         * @brief Sets the sampler address modes applied to each draw.
         * @param addressU Raw XNA TextureAddressMode ordinal for U.
         * @param addressV Raw XNA TextureAddressMode ordinal for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;
        /**
         * @brief Queues an unscaled, untinted sprite at @p x, @p y.
         * @param texture Source texture.
         * @param x       Destination left edge, in logical pixels.
         * @param y       Destination top edge, in logical pixels.
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;
        /**
         * @brief Queues a tinted sprite stretched from @p sourceRectangle into @p destinationRectangle.
         * @param texture             Source texture.
         * @param destinationRectangle Destination rectangle, in logical pixels.
         * @param sourceRectangle     Source rectangle, in texels.
         * @param color               Tint colour.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        /**
         * @brief Queues a tinted, rotated, flipped sprite.
         * @param texture             Source texture.
         * @param destinationRectangle Destination rectangle, in logical pixels.
         * @param sourceRectangle     Source rectangle, in texels.
         * @param color               Tint colour.
         * @param rotation            Rotation in radians, about @p origin.
         * @param origin              Rotation origin, in source-texture pixels.
         * @param effects             Horizontal/vertical flip flags.
         * @param layerDepth          Sort depth, forwarded as the vertex Z.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        struct SpriteVertex
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            std::uint8_t r = 255, g = 255, b = 255, a = 255;
            float u = 0.0f, v = 0.0f;
        };
        static_assert(sizeof(SpriteVertex) == 24,
                      "The sprite vertex must stay byte-identical to VertexPositionColorTexture.");

        void Flush();
        void PushQuad(const ITextureRenderer& texture,
                      const Rectangle& destinationRectangle,
                      const Rectangle& sourceRectangle,
                      const Color& color,
                      float rotation,
                      const Vector2& origin,
                      SpriteEffects effects,
                      float layerDepth);

        WickedRenderer* owner_ = nullptr;
        std::vector<SpriteVertex> vertices_;
        const ITextureRenderer* batchTexture_ = nullptr;
        Matrix transform_;
        bool hasTransform_ = false;
        int filter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        bool active_ = false;
    };

    /**
     * @brief The CNA graphics renderer implemented on Wicked Engine's `wi::graphics::GraphicsDevice`.
     *
     * Frame structure (plan_wicked.md design decision 5): all game rendering goes into an
     * off-screen "scene" colour target sized to the virtual resolution, and `Present()` blits that
     * target into the swap chain with the letterbox/overscan/stretch rectangle CNA's presentation
     * mode asks for. Wicked Engine's swap-chain render pass acquires an image and always clears, so
     * it can be entered only once per frame; rendering into an off-screen target instead is what
     * lets CNA switch freely between the back buffer and a `RenderTarget2D` mid-frame, and is the
     * same texture the virtual-resolution scaling needs anyway.
     */
    class WickedRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates the Wicked Engine device, swap chain, scene target and built-in shaders.
         * @param args Window, virtual resolution, presentation mode, MSAA and VSync request.
         */
        explicit WickedRenderer(const GraphicsRendererCreateArgs& args);
        /** @brief Waits for the GPU to go idle and releases every device resource. */
        ~WickedRenderer() override;

        WickedRenderer(const WickedRenderer&) = delete;
        WickedRenderer& operator=(const WickedRenderer&) = delete;

        // ---- Frame ----

        /** @brief Clears the active target's colour to the given premultiplied-alpha-free RGBA. */
        void Clear(float r, float g, float b, float a) override;
        /** @brief Blits the scene target into the swap chain and submits the frame. */
        void Present() override;
        /**
         * @brief Reports the logical (virtual) render size.
         * @param width  Receives the logical width.
         * @param height Receives the logical height.
         */
        void GetViewportSize(int& width, int& height) override;
        /**
         * @brief Changes the logical render size at runtime.
         * @param width  New logical width.
         * @param height New logical height.
         */
        void SetVirtualResolution(int width, int height) override;
        /** @brief Changes the presentation/scaling policy (a `CnaPresentationMode` ordinal). */
        void SetPresentationMode(int mode) override;
        /** @brief Changes the swap interval (0 = immediate, 1 = VSync, 2 = half rate). */
        void SetSwapInterval(int interval) override;
        /**
         * @brief Reconfigures the scene target's MSAA sample count.
         * @param requestedMultiSampleCount Requested sample count; 0 or 1 disables MSAA.
         * @return The applied, device-clamped sample count.
         */
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        /** @brief The scene target's applied MSAA sample count; 0 when MSAA is off. */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        /**
         * @brief Converts a physical window point into logical game coordinates.
         * @param windowX Window-space X.
         * @param windowY Window-space Y.
         * @param logX    Receives the logical X.
         * @param logY    Receives the logical Y.
         * @return True when the conversion was performed.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        /**
         * @brief Converts a logical game point into physical window coordinates.
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Receives the window-space X.
         * @param windowY Receives the window-space Y.
         * @return True when the conversion was performed.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;
        /** @brief The SDL window this renderer presents to. */
        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        /** @brief Always null — this renderer never creates an SDL_Renderer. */
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }
        /**
         * @brief Reads rendered scene-target pixels back into @p pixels as RGBA8.
         * @param x      Left edge, in logical pixels.
         * @param y      Top edge, in logical pixels.
         * @param w      Width, in logical pixels.
         * @param h      Height, in logical pixels.
         * @param pixels Destination for `w * h * 4` bytes, top row first.
         */
        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;

        // ---- Resources ----

        /** @brief Creates a sampleable RGBA8 texture from CPU pixels. */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        /** @brief Creates the batched sprite renderer. */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        /** @brief Creates a real GPU occlusion query. */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        /** @brief Creates a vertex buffer able to hold @p vertex_capacity vertices. */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        /** @brief Creates a 16-bit index buffer able to hold @p index_capacity indices. */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        /** @brief Creates a 32-bit index buffer able to hold @p index_capacity indices. */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;
        /**
         * @brief Creates a six-face cube map.
         * @param size          Width and height of each face, in texels.
         * @param mipMap        True when a full mip chain was requested.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal; only `Color` is stored today.
         * @return The new cube map.
         */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                               int surfaceFormat) override;
        /**
         * @brief Creates a volume texture.
         * @param w             Width in voxels.
         * @param h             Height in voxels.
         * @param depth         Depth in voxels.
         * @param mipMap        True when a full mip chain was requested.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal; only `Color` is stored today.
         * @return The new volume texture.
         */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;
        /**
         * @brief Creates an off-screen render target.
         * @param w                Width in pixels.
         * @param h                Height in pixels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents True when the target's usage preserves contents across binds.
         * @param mipMap           True when a full mip chain was requested.
         * @param multiSampleCount Requested MSAA sample count.
         * @return The new render target.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                   bool preserveContents,
                                                                   bool mipMap,
                                                                   int multiSampleCount) override;
        /** @brief Binds @p rt as the draw destination, or the scene target when null. */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        /**
         * @brief Creates a cube-map render target.
         * @param size             Width and height of each face, in pixels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents True when the target's usage preserves contents across binds.
         * @param mipMap           True when a full mip chain was requested.
         * @param multiSampleCount Requested MSAA sample count.
         * @return The new cube render target.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;
        /**
         * @brief Binds one face of a cube render target as the draw destination.
         * @param rt   Cube target, or null to restore the back buffer.
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         */
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        /**
         * @brief Binds a normalized render-target set.
         * @param renderTargets Ordered descriptors, or null to restore the back buffer.
         * @param count         Number of descriptors.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        // ---- State ----

        /**
         * @brief Applies a `BlendState`.
         * @param colorSrcBlend  Raw XNA `Blend` ordinal for the colour source factor.
         * @param alphaSrcBlend  Raw XNA `Blend` ordinal for the alpha source factor.
         * @param colorDstBlend  Raw XNA `Blend` ordinal for the colour destination factor.
         * @param alphaDstBlend  Raw XNA `Blend` ordinal for the alpha destination factor.
         * @param colorBlendFunc Raw XNA `BlendFunction` ordinal for colour.
         * @param alphaBlendFunc Raw XNA `BlendFunction` ordinal for alpha.
         * @param writeState     Per-MRT colour write masks and the coverage sample mask.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        /**
         * @brief Applies a `DepthStencilState`.
         * @param depthEnable       Whether the depth test is enabled.
         * @param depthWriteEnable  Whether depth writes are enabled.
         * @param depthFunc         Raw XNA `CompareFunction` ordinal.
         * @param stencilEnable     Whether stencil testing is enabled.
         * @param stencilFunc       Raw XNA `CompareFunction` ordinal for the front face.
         * @param stencilPass       Raw XNA `StencilOperation` ordinal for a passing test.
         * @param stencilFail       Raw XNA `StencilOperation` ordinal for a failing test.
         * @param stencilDepthFail  Raw XNA `StencilOperation` ordinal for a failing depth test.
         * @param stencilMask       Stencil read mask.
         * @param stencilWriteMask  Stencil write mask.
         * @param referenceStencil  Stencil reference value.
         * @param twoSidedStencilMode Whether the back face uses its own operations.
         * @param ccwStencilFunc    Raw XNA `CompareFunction` ordinal for the back face.
         * @param ccwStencilPass    Raw XNA `StencilOperation` ordinal for the back face.
         * @param ccwStencilFail    Raw XNA `StencilOperation` ordinal for the back face.
         * @param ccwStencilDepthFail Raw XNA `StencilOperation` ordinal for the back face.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        /**
         * @brief Applies a `RasterizerState`.
         * @param cullMode            Raw XNA `CullMode` ordinal.
         * @param fillMode            Raw XNA `FillMode` ordinal.
         * @param scissorTestEnable   Whether the scissor test is enabled.
         * @param depthBias           Constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;
        /**
         * @brief Applies a `SamplerState` to one texture slot.
         * @param slot          Texture unit index.
         * @param filter        Raw XNA `TextureFilter` ordinal.
         * @param addressU      Raw XNA `TextureAddressMode` ordinal for U.
         * @param addressV      Raw XNA `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy Maximum anisotropy.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;
        /** @brief Sets the constant blend colour used by the `BlendFactor` blend modes. */
        void SetBlendFactor(float r, float g, float b, float a) override;
        /** @brief Sets the standalone stencil reference value. */
        void SetReferenceStencil(int value) override;
        /**
         * @brief Sets the scissor clip rectangle, in logical pixels.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Width.
         * @param h Height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;
        /**
         * @brief Sets the GPU viewport and depth range, in logical pixels.
         * @param x        Left edge.
         * @param y        Top edge.
         * @param w        Width.
         * @param h        Height.
         * @param minDepth Near depth-range bound.
         * @param maxDepth Far depth-range bound.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        // ---- Clears ----

        /** @brief Clears colour and depth in one call. */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        /** @brief Clears depth only. */
        void ClearDepth(float depth) override;
        /** @brief Clears stencil only. */
        void ClearStencil(int stencil) override;
        /** @brief Clears depth and stencil in one call. */
        void ClearDepthAndStencil(float depth, int stencil) override;
        /** @brief Clears colour and stencil in one call. */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        /** @brief Clears colour, depth and stencil in one call. */
        void ClearColorDepthAndStencil(float r, float g, float b, float a,
                                       float depth, int stencil) override;

        /** @brief Enables or disables the depth test. */
        void SetDepthTestEnabled(bool enabled) override;
        /** @brief Enables or disables colour blending. */
        void SetBlendEnabled(bool enabled) override;
        /** @brief Enables or disables depth writes. */
        void SetDepthWriteEnabled(bool enabled) override;

        // ---- Draws ----

        /**
         * @brief Draws colour-only primitives with the built-in vertex-colour shader.
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view,
                                   const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        /**
         * @brief Indexed counterpart of DrawColoredPrimitives().
         * @param vb             Vertex buffer to read from.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        /**
         * @brief Effect-aware draw; selects the shader variant from the combined vertex stride.
         * @param vb             Vertex buffer named by `params.vertexStreams[0]`.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        /**
         * @brief Indexed counterpart of DrawPrimitivesEx().
         * @param vb             Vertex buffer named by `params.vertexStreams[0]`.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                     const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view,
                                     const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Instanced indexed draw.
         *
         * The per-instance stream is CNA's established 64-byte column-major `Matrix` world
         * transform, bound at its own input slot with per-instance step rate. Only
         * `InstanceFrequency == 1` is expressible -- Wicked Engine's `InputLayout` has no
         * instance-step-rate field -- so any other frequency is refused rather than silently
         * treated as 1.
         *
         * @param vb             Vertex buffer named by `params.vertexStreams[0]`.
         * @param ib             Index buffer to read from.
         * @param world          World matrix; folded into the view/projection transform.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances to draw.
         * @param params         Per-draw effect parameters, carrying every bound stream.
         */
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view,
                                       const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount,
                                       const GpuDrawParams& params) override;

        /** @brief Inserts a named GPU debug marker into the command stream. */
        void SetStringMarkerEXT(const char* marker) override;
        /**
         * @brief Reports whether this renderer supports @p capability.
         * @param capability The capability being queried.
         * @return True when the capability is genuinely available.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        /** @brief The largest single-axis texture dimension this device accepts. */
        [[nodiscard]] int GetMaxTextureDimension() const override;
        /** @brief How many per-vertex `VertexBufferBinding`s one draw may bind. */
        [[nodiscard]] int GetMaxVertexStreams() const override;

        // ---- CNAEXT: internals shared with this renderer's resource classes ----

        /** @brief CNAEXT. The Wicked Engine device every resource in this renderer is created from. */
        [[nodiscard]] wig::GraphicsDevice* GetDeviceEXT() const { return device_.get(); }
        /** @brief CNAEXT. The command list for the frame currently being recorded. */
        [[nodiscard]] wig::CommandList GetCommandListEXT();
        /** @brief CNAEXT. Ends any open render pass so a copy/blit operation may be recorded. */
        void EndRenderPassEXT();
        /** @brief CNAEXT. Submits everything recorded so far and waits for the GPU to finish. */
        void FlushAndWaitEXT();
        /** @brief CNAEXT. The adapter name Wicked Engine reports, for diagnostics. */
        [[nodiscard]] std::string GetAdapterNameEXT() const;
        /** @brief CNAEXT. The shader binary format the device consumes (SPIR-V or DXIL). */
        [[nodiscard]] wig::ShaderFormat GetShaderFormatEXT() const;
        /** @brief CNAEXT. Records that @p rt is the render target currently bound, if any. */
        void NotifyRenderTargetDestroyedEXT(const WickedRenderTargetRenderer* rt);
        /** @brief CNAEXT. The cube-target counterpart of NotifyRenderTargetDestroyedEXT. */
        void NotifyRenderTargetCubeDestroyedEXT(const WickedRenderTargetCubeRenderer* rt);
        /** @brief CNAEXT. Submits a batch of sprite quads; used by WickedSpriteBatchRenderer. */
        void DrawSpriteQuadsEXT(const void* vertices, int vertexCount,
                                const ITextureRenderer& texture,
                                const Matrix* transform,
                                int filter, int addressU, int addressV);

    private:
        struct SceneTarget
        {
            wig::Texture color;
            wig::Texture depth;
            wig::Texture resolve;
            int width = 0;
            int height = 0;
            int sampleCount = 1;
        };

        struct PresentRect
        {
            float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        };

        void CreateDevice(const GraphicsRendererCreateArgs& args);
        void CreateSwapChainResources();
        void CreateSceneTarget(int width, int height, int sampleCount);
        void CreateBuiltinShaders();
        void CompileShader(wig::ShaderStage stage, const char* entryPoint, wig::Shader& out);
        void ResolveVirtualResolution();

        void BeginFrame();
        void BeginRenderPass();
        void EnsureRenderPass();
        void ApplyViewportAndScissor();
        void ClearActiveTarget(bool color, const float rgba[4],
                               bool depth, float depthValue,
                               bool stencil, int stencilValue);
        void DrawFullTargetQuad(const float rgba[4], float depthValue,
                                bool writeColor, bool writeDepth,
                                bool writeStencil, int stencilValue);
        [[nodiscard]] PresentRect ComputePresentRect() const;
        [[nodiscard]] wig::PipelineState* GetPipeline(const WickedPipelineKey& key);
        [[nodiscard]] const wig::Sampler* GetSampler(int filter, int addressU, int addressV,
                                                     int maxAnisotropy);
        void BindCommonState(wig::CommandList cmd, const WickedPipelineKey& key,
                             const WickedShaderConstants& constants);
        [[nodiscard]] int ActiveTargetWidth() const;
        [[nodiscard]] int ActiveTargetHeight() const;
        void SubmitDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                        const Matrix& world, const Matrix& view, const Matrix& projection,
                        PrimitiveType primitive, int primitiveCount,
                        const GpuDrawParams* params, int instanceCount = 1);

        SDL_Window* window_ = nullptr;
        std::unique_ptr<wig::GraphicsDevice> device_;
        wig::SwapChain swapChain_;
        wig::SwapChainDesc swapChainDesc_;
        SceneTarget scene_;

        std::array<wig::Shader, static_cast<std::size_t>(WickedShaderVariant::Count)> vertexShaders_;
        std::array<wig::Shader, kWickedInstancedVariantCount> instancedVertexShaders_;
        std::array<wig::Shader, kWickedSkinnableVariantCount> skinnedVertexShaders_;
        /// Pbr48VS, Pbr68VS and PbrSkinned68VS, in that order.
        std::array<wig::Shader, 3> pbrVertexShaders_;
        wig::Shader pbrPixelShader_;
        wig::Shader pixelShader_;
        std::array<wig::InputLayout, static_cast<std::size_t>(WickedShaderVariant::Count)> inputLayouts_;
        std::array<wig::InputLayout, kWickedInstancedVariantCount> instancedInputLayouts_;
        wig::Shader envMapVertexShader_;
        wig::Shader envMapPixelShader_;
        wig::InputLayout envMapInputLayout_;
        wig::Texture whiteTexture_;
        wig::Texture whiteCubeTexture_;
        wig::Texture flatNormalTexture_;

        std::unordered_map<WickedPipelineKey, WickedPipelineEntry, WickedPipelineKeyHash> pipelines_;
        std::unordered_map<std::uint64_t, wig::Sampler> samplers_;

        wig::CommandList cmd_{};
        bool frameActive_ = false;
        bool renderPassActive_ = false;
        bool sceneCleared_ = false;

        std::array<WickedRenderTargetRenderer*, kWickedMaxRenderTargets> currentRenderTargets_{};
        int currentRenderTargetCount_ = 0;
        WickedRenderTargetCubeRenderer* currentRenderTargetCube_ = nullptr;
        int currentCubeFace_ = 0;

        /// One slot's `SamplerState`, kept as the raw XNA ordinals `ApplySamplerState` receives.
        /// The state is recorded rather than bound immediately: XNA sets sampler state outside a
        /// draw, so binding at `ApplySamplerState` time would either land on the wrong command list
        /// or (before the first frame exists) be dropped entirely.
        struct SamplerSlotState
        {
            int filter = 0;         ///< Raw XNA TextureFilter ordinal (0 = Linear).
            int addressU = 1;       ///< Raw XNA TextureAddressMode ordinal (1 = Clamp).
            int addressV = 1;       ///< Raw XNA TextureAddressMode ordinal (1 = Clamp).
            int maxAnisotropy = 4;  ///< XNA SamplerState.MaxAnisotropy default.
        };

        WickedPipelineKey state_{};
        std::array<SamplerSlotState, 16> samplerStates_{};
        float blendFactor_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        int referenceStencil_ = 0;
        bool scissorEnabled_ = false;
        int scissorRect_[4] = {0, 0, 0, 0};
        bool viewportSet_ = false;
        float viewport_[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        int preferredWidth_ = 0;
        int preferredHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int multiSampleCount_ = 0;
        int requestedMultiSampleCount_ = 1;
        int swapInterval_ = 1;

        std::string shaderDirectory_;
        bool shaderDirectoryOwned_ = false;
    };
}
