// SPDX-License-Identifier: MS-PL
#pragma once

// plan_wicked.md: CNA's WICKED graphics backend, implemented on top of Wicked Engine's render
// hardware interface (wi::graphics::GraphicsDevice), which itself dispatches to Vulkan on
// Linux/Windows and to D3D12 on Windows.
//
// Only the RHI layer of Wicked Engine is used. wi::renderer, wi::scene, wi::Application, wi::input
// and the physics/scripting layers are deliberately not linked into CNA's own runtime: CNA already
// owns the XNA 4.0 programming model above this boundary and needs a device abstraction below it,
// not a second engine (plan_wicked.md design decision 1).

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/CNAHelper.hpp"

#include "wiGraphics.h"
#include "wiGraphicsDevice.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Backends::Wicked
{
    namespace wig = wi::graphics;

    class WickedGraphicsBackend;

    /**
     * @brief NOXNA. Which of the backend's built-in shader programs a draw uses.
     *
     * CNA describes a vertex layout by its byte stride (see `IGraphicsBackend`'s own
     * `combinedVertexStride` contract), so the variant is chosen by stride exactly as the D3D11,
     * D3D12 and Vulkan backends choose theirs. `Basic24` doubles as the `SpriteBatch`, clear-quad
     * and present-blit layout: a sprite vertex is position + packed colour + UV, which is
     * byte-for-byte `VertexPositionColorTexture`.
     */
    NOXNA enum class WickedShaderVariant : std::uint32_t
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
     * @brief NOXNA. How many leading variants also have an instanced sibling.
     *
     * Instancing is offered for the four narrow stock layouts only. The wider tangent/skinned
     * layouts exist to carry `PbrEffect`/`SkinnedEffect` geometry, and neither of those effects has
     * an instanced form in CNA, so compiling four more entry points nothing can reach would be
     * dead weight. An instanced draw on a wider stride is refused rather than silently downgraded.
     */
    NOXNA inline constexpr std::size_t kWickedInstancedVariantCount = 4;

    /** @brief NOXNA. The first variant that carries blend weights, and so can be skinned. */
    NOXNA inline constexpr std::size_t kWickedFirstSkinnableVariant =
        static_cast<std::size_t>(WickedShaderVariant::Basic52);
    /** @brief NOXNA. How many variants can be skinned (strides 52, 56 and 68). */
    NOXNA inline constexpr std::size_t kWickedSkinnableVariantCount =
        static_cast<std::size_t>(WickedShaderVariant::Count) - kWickedFirstSkinnableVariant;

    /**
     * @brief NOXNA. `SkinnedEffect`'s bone palette, bound as its own constant buffer.
     *
     * Kept out of @ref WickedShaderConstants deliberately: at 4608 bytes of matrices it dwarfs
     * every other per-draw constant, and copying it into the frame allocator on draws that do no
     * skinning at all would be pure waste. It is uploaded only for a skinned draw.
     */
    NOXNA struct WickedBoneConstants
    {
        /** @brief Columns of up to 72 bone matrices, in CNA's raw `Matrix` byte order. */
        float bones[72 * 16] = {};
        /** @brief x = `WeightsPerVertex` (1, 2 or 4); the rest is padding. */
        float skinParams[4] = {4.0f, 0.0f, 0.0f, 0.0f};
    };

    /**
     * @brief NOXNA. The complete, hashable description of one graphics pipeline state.
     *
     * Wicked Engine's `PipelineStateDesc` holds raw pointers into caller-owned `BlendState` /
     * `RasterizerState` / `DepthStencilState` / `InputLayout` objects that must outlive the
     * pipeline, so the backend cannot build one from a stack temporary per draw. This POD carries
     * the raw CNA state ordinals the `Apply*State` methods already receive; the cache below keys on
     * it and owns the converted Wicked state structs for as long as the pipeline exists.
     *
     * It is compared and hashed byte-wise, so it must stay a POD with no padding holes left
     * uninitialised — always create one through value-initialisation (`WickedPipelineKey key{};`).
     */
    NOXNA struct WickedPipelineKey
    {
        std::uint32_t variant = 0;            ///< WickedShaderVariant ordinal.
        std::uint32_t instanced = 0;          ///< Non-zero when the per-instance input layout is used.
        std::uint32_t envMap = 0;             ///< Non-zero when the EnvironmentMapEffect program is used.
        std::uint32_t skinned = 0;            ///< Non-zero when the SkinnedEffect program is used.
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

        /** @brief Byte-wise equality, so a new state field cannot be forgotten here. */
        [[nodiscard]] bool operator==(const WickedPipelineKey& other) const noexcept;
    };

    /** @brief NOXNA. Byte-wise FNV-1a hash of a WickedPipelineKey. */
    NOXNA struct WickedPipelineKeyHash
    {
        /** @brief Hashes @p key byte-wise. */
        [[nodiscard]] std::size_t operator()(const WickedPipelineKey& key) const noexcept;
    };

    /**
     * @brief NOXNA. One cached pipeline plus the state objects Wicked Engine keeps pointers into.
     */
    NOXNA struct WickedPipelineEntry
    {
        wig::BlendState        blend;        ///< Owned blend state referenced by @ref pipeline.
        wig::RasterizerState   rasterizer;   ///< Owned rasterizer state referenced by @ref pipeline.
        wig::DepthStencilState depthStencil; ///< Owned depth/stencil state referenced by @ref pipeline.
        wig::PipelineState     pipeline;     ///< The compiled pipeline.
    };

    /**
     * @brief NOXNA. Constants shared by every built-in shader variant.
     *
     * The two matrices are stored as their four COLUMNS, so the shader evaluates XNA's own
     * row-vector product (`clip = position * matrix`) with four `dot()`s and no packing-convention
     * assumption at the HLSL boundary.
     */
    NOXNA struct WickedShaderConstants
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
    };

    /**
     * @brief NOXNA. A Wicked Engine texture exposed to CNA as an `ITextureBackend`.
     */
    class WickedTextureBackend : public ITextureBackend
    {
    public:
        /**
         * @brief Wraps an already-created Wicked Engine texture.
         *
         * @param owner   Backend that created @p texture; used for readback and uploads.
         * @param texture The GPU resource. Ownership moves into this object.
         */
        WickedTextureBackend(WickedGraphicsBackend* owner, wig::Texture texture);
        /** @brief Releases this backend's reference to the GPU texture. */
        ~WickedTextureBackend() override;

        WickedTextureBackend(const WickedTextureBackend&) = delete;
        WickedTextureBackend& operator=(const WickedTextureBackend&) = delete;

        /** @brief Width of mip level 0, in texels. */
        [[nodiscard]] int GetWidth() const override;
        /** @brief Height of mip level 0, in texels. */
        [[nodiscard]] int GetHeight() const override;
        /** @brief Always null — this backend never creates an SDL_Renderer texture. */
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

        /** @brief NOXNA. The underlying Wicked Engine resource, for the owning backend's binds. */
        [[nodiscard]] const wig::Texture& GetTextureEXT() const { return texture_; }

    protected:
        WickedGraphicsBackend* owner_ = nullptr; ///< Backend that owns the device.
        wig::Texture texture_;                   ///< The GPU resource.
    };

    /**
     * @brief NOXNA. A cube map exposed to CNA as an `ITextureCubeBackend`.
     *
     * Upload and readback only. This backend has no `EnvironmentMapEffect` shader variant yet
     * (plan_wicked.md WICKED-56), so a cube map cannot currently be sampled by a draw — it can be
     * filled and read back, and `SetData`/`GetData` report honestly whether they did so.
     */
    class WickedTextureCubeBackend final : public ITextureCubeBackend
    {
    public:
        /**
         * @brief Creates a six-face cube map.
         *
         * @param owner  Backend that owns the device.
         * @param size   Width and height of each face, in texels.
         * @param mipMap True when a full mip chain was requested.
         */
        WickedTextureCubeBackend(WickedGraphicsBackend* owner, int size, bool mipMap);
        /** @brief Releases the GPU texture. */
        ~WickedTextureCubeBackend() override;

        WickedTextureCubeBackend(const WickedTextureCubeBackend&) = delete;
        WickedTextureCubeBackend& operator=(const WickedTextureCubeBackend&) = delete;

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

        /** @brief NOXNA. The underlying Wicked Engine resource. */
        [[nodiscard]] const wig::Texture& GetTextureEXT() const { return texture_; }

    private:
        WickedGraphicsBackend* owner_ = nullptr;
        wig::Texture texture_;
        int size_ = 0;
        std::uint32_t mipLevels_ = 1;
    };

    /**
     * @brief NOXNA. A volume texture exposed to CNA as an `ITexture3DBackend`.
     */
    class WickedTexture3DBackend final : public ITexture3DBackend
    {
    public:
        /**
         * @brief Creates a volume texture.
         *
         * @param owner  Backend that owns the device.
         * @param width  Width in voxels.
         * @param height Height in voxels.
         * @param depth  Depth in voxels.
         * @param mipMap True when a full mip chain was requested.
         */
        WickedTexture3DBackend(WickedGraphicsBackend* owner, int width, int height, int depth,
                               bool mipMap);
        /** @brief Releases the GPU texture. */
        ~WickedTexture3DBackend() override;

        WickedTexture3DBackend(const WickedTexture3DBackend&) = delete;
        WickedTexture3DBackend& operator=(const WickedTexture3DBackend&) = delete;

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

        /** @brief NOXNA. The underlying Wicked Engine resource. */
        [[nodiscard]] const wig::Texture& GetTextureEXT() const { return texture_; }

    private:
        WickedGraphicsBackend* owner_ = nullptr;
        wig::Texture texture_;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        std::uint32_t mipLevels_ = 1;
    };

    /**
     * @brief NOXNA. An off-screen colour target (plus optional depth/stencil) CNA can render into.
     */
    class WickedRenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        /**
         * @brief Creates the colour texture and, when @p depthFormat is not `None`, its depth buffer.
         *
         * @param owner            Backend that owns the device.
         * @param width            Target width in pixels.
         * @param height           Target height in pixels.
         * @param depthFormat      Raw `Microsoft::Xna::Framework::Graphics::DepthFormat` ordinal.
         * @param preserveContents True when the target's `RenderTargetUsage` preserves contents.
         * @param mipMap           True when a full mip chain was requested.
         * @param multiSampleCount Requested MSAA sample count; 0 or 1 means none.
         */
        WickedRenderTargetBackend(WickedGraphicsBackend* owner, int width, int height,
                                  int depthFormat, bool preserveContents, bool mipMap,
                                  int multiSampleCount);
        /** @brief Releases the colour and depth resources. */
        ~WickedRenderTargetBackend() override;

        WickedRenderTargetBackend(const WickedRenderTargetBackend&) = delete;
        WickedRenderTargetBackend& operator=(const WickedRenderTargetBackend&) = delete;

        /** @brief Target width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Target height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }
        /** @brief Always null — this backend never creates an SDL_Renderer texture. */
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

        /** @brief NOXNA. The colour texture, for binding as a shader resource. */
        [[nodiscard]] const wig::Texture& GetColorTextureEXT() const { return color_; }
        /** @brief NOXNA. The depth texture, or an invalid handle when none was allocated. */
        [[nodiscard]] const wig::Texture& GetDepthTextureEXT() const { return depth_; }
        /** @brief NOXNA. Whether a bind must load the existing contents instead of discarding. */
        [[nodiscard]] bool PreservesContentsEXT() const { return preserveContents_; }
        /** @brief NOXNA. Marks that this target has been rendered into at least once. */
        void MarkRenderedEXT() { hasContent_ = true; }
        /** @brief NOXNA. Whether anything has been rendered into this target yet. */
        [[nodiscard]] bool HasContentEXT() const { return hasContent_; }

    private:
        WickedGraphicsBackend* owner_ = nullptr;
        wig::Texture color_;
        wig::Texture depth_;
        int width_ = 0;
        int height_ = 0;
        int multiSampleCount_ = 0;
        bool preserveContents_ = false;
        bool hasContent_ = false;
    };

    /**
     * @brief NOXNA. A cube map CNA can render each face of, and sample as a whole.
     *
     * One face is bound at a time through a per-face render-target subresource view, created once
     * at construction. The whole-cube shader-resource view is the resource's default, so a
     * dynamically rendered cube map is sampled by `EnvironmentMapEffect` exactly like an uploaded
     * one.
     */
    class WickedRenderTargetCubeBackend final : public IRenderTargetCubeBackend
    {
    public:
        /**
         * @brief Creates the cube colour target and, when requested, its depth buffer.
         *
         * @param owner            Backend that owns the device.
         * @param size             Width and height of each face, in pixels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents True when the target's `RenderTargetUsage` preserves contents.
         * @param mipMap           True when a full mip chain was requested.
         * @param multiSampleCount Requested MSAA sample count; 0 or 1 means none.
         */
        WickedRenderTargetCubeBackend(WickedGraphicsBackend* owner, int size, int depthFormat,
                                      bool preserveContents, bool mipMap, int multiSampleCount);
        /** @brief Releases the colour and depth resources. */
        ~WickedRenderTargetCubeBackend() override;

        WickedRenderTargetCubeBackend(const WickedRenderTargetCubeBackend&) = delete;
        WickedRenderTargetCubeBackend& operator=(const WickedRenderTargetCubeBackend&) = delete;

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

        /** @brief NOXNA. The cube colour texture, for binding as a shader resource. */
        [[nodiscard]] const wig::Texture& GetColorTextureEXT() const { return color_; }
        /** @brief NOXNA. The depth texture, or an invalid handle when none was allocated. */
        [[nodiscard]] const wig::Texture& GetDepthTextureEXT() const { return depth_; }
        /** @brief NOXNA. The render-target subresource index of @p face. */
        [[nodiscard]] int RenderTargetSubresourceEXT(int face) const;
        /** @brief NOXNA. Whether a bind must load the existing contents instead of discarding. */
        [[nodiscard]] bool PreservesContentsEXT() const { return preserveContents_; }
        /** @brief NOXNA. Marks that @p face has been rendered into at least once. */
        void MarkFaceRenderedEXT(int face);
        /** @brief NOXNA. Whether anything has been rendered into @p face yet. */
        [[nodiscard]] bool HasFaceContentEXT(int face) const;

    private:
        WickedGraphicsBackend* owner_ = nullptr;
        wig::Texture color_;
        wig::Texture depth_;
        std::array<int, 6> faceSubresources_{};
        std::array<bool, 6> faceHasContent_{};
        int size_ = 0;
        int multiSampleCount_ = 0;
        bool preserveContents_ = false;
    };

    /**
     * @brief NOXNA. A CPU-writable vertex buffer.
     */
    class WickedVertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        /**
         * @brief Allocates a buffer able to hold @p vertexCapacity vertices of any supported stride.
         *
         * @param owner          Backend that owns the device.
         * @param vertexCapacity Maximum number of vertices.
         */
        WickedVertexBufferBackend(WickedGraphicsBackend* owner, int vertexCapacity);
        /** @brief Releases the GPU buffer. */
        ~WickedVertexBufferBackend() override;

        WickedVertexBufferBackend(const WickedVertexBufferBackend&) = delete;
        WickedVertexBufferBackend& operator=(const WickedVertexBufferBackend&) = delete;

        /**
         * @brief Uploads @p vertexCount vertices of @p strideInBytes bytes each.
         *
         * @param data           Packed vertex data.
         * @param vertexCount    Number of vertices.
         * @param strideInBytes  Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;
        /**
         * @brief Records the caller's complete vertex declaration.
         *
         * The stride is what selects the shader variant and input layout; the element list is kept
         * so a future custom-layout path can consume it without another interface change.
         *
         * @param vertexDeclaration Full declaration, in declaration order.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;
        /** @brief Number of vertices most recently uploaded. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief NOXNA. The underlying Wicked Engine buffer. */
        [[nodiscard]] const wig::GPUBuffer& GetBufferEXT() const { return buffer_; }
        /** @brief NOXNA. Byte stride of the most recent upload, or 0 when nothing was uploaded. */
        [[nodiscard]] std::size_t GetStrideEXT() const { return stride_; }

    private:
        WickedGraphicsBackend* owner_ = nullptr;
        wig::GPUBuffer buffer_;
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::size_t declaredStride_ = 0;
    };

    /**
     * @brief NOXNA. A CPU-writable 16- or 32-bit index buffer.
     */
    class WickedIndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        /**
         * @brief Allocates a buffer able to hold @p indexCapacity indices.
         *
         * @param owner         Backend that owns the device.
         * @param indexCapacity Maximum number of indices.
         * @param thirtyTwoBit  True for 32-bit indices, false for 16-bit.
         */
        WickedIndexBufferBackend(WickedGraphicsBackend* owner, int indexCapacity, bool thirtyTwoBit);
        /** @brief Releases the GPU buffer. */
        ~WickedIndexBufferBackend() override;

        WickedIndexBufferBackend(const WickedIndexBufferBackend&) = delete;
        WickedIndexBufferBackend& operator=(const WickedIndexBufferBackend&) = delete;

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
        /** @brief Number of indices most recently uploaded. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        /** @brief Whether this buffer stores 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief NOXNA. The underlying Wicked Engine buffer. */
        [[nodiscard]] const wig::GPUBuffer& GetBufferEXT() const { return buffer_; }

    private:
        WickedGraphicsBackend* owner_ = nullptr;
        wig::GPUBuffer buffer_;
        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
    };

    /**
     * @brief NOXNA. A real GPU occlusion query backed by a Wicked Engine query heap.
     */
    class WickedOcclusionQueryBackend final : public IOcclusionQueryBackend
    {
    public:
        /**
         * @brief Allocates a one-slot occlusion query heap and its readback buffer.
         * @param owner Backend that owns the device.
         */
        explicit WickedOcclusionQueryBackend(WickedGraphicsBackend* owner);
        /** @brief Releases the query heap and readback buffer. */
        ~WickedOcclusionQueryBackend() override;

        WickedOcclusionQueryBackend(const WickedOcclusionQueryBackend&) = delete;
        WickedOcclusionQueryBackend& operator=(const WickedOcclusionQueryBackend&) = delete;

        /** @brief Starts counting samples that pass the depth test. */
        void Begin() override;
        /** @brief Stops counting and resolves the result into the readback buffer. */
        void End() override;
        /** @brief Whether the GPU has finished the frame the query was resolved in. */
        [[nodiscard]] bool IsComplete() const override;
        /** @brief Number of samples that passed the depth test between Begin() and End(). */
        [[nodiscard]] int PixelCount() const override;

    private:
        WickedGraphicsBackend* owner_ = nullptr;
        wig::GPUQueryHeap heap_;
        wig::GPUBuffer readback_;
        std::uint64_t resolvedFrame_ = 0;
        bool active_ = false;
        bool resolved_ = false;
    };

    /**
     * @brief NOXNA. The batched 2D sprite renderer.
     *
     * Sprites accumulate into a CPU-side vertex array between Begin() and End() and are flushed as
     * one draw per contiguous run of the same texture, exactly as the other GPU backends batch.
     */
    class WickedSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        /**
         * @brief Binds this sprite batch to its owning backend.
         * @param owner Backend that owns the device.
         */
        explicit WickedSpriteBatchBackend(WickedGraphicsBackend* owner);
        /** @brief Flushes anything still queued. */
        ~WickedSpriteBatchBackend() override;

        WickedSpriteBatchBackend(const WickedSpriteBatchBackend&) = delete;
        WickedSpriteBatchBackend& operator=(const WickedSpriteBatchBackend&) = delete;

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
        void Draw(const ITextureBackend& texture, float x, float y) override;
        /**
         * @brief Queues a tinted sprite stretched from @p sourceRectangle into @p destinationRectangle.
         * @param texture             Source texture.
         * @param destinationRectangle Destination rectangle, in logical pixels.
         * @param sourceRectangle     Source rectangle, in texels.
         * @param color               Tint colour.
         */
        void Draw(const ITextureBackend& texture,
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
        void Draw(const ITextureBackend& texture,
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
        void PushQuad(const ITextureBackend& texture,
                      const Rectangle& destinationRectangle,
                      const Rectangle& sourceRectangle,
                      const Color& color,
                      float rotation,
                      const Vector2& origin,
                      SpriteEffects effects,
                      float layerDepth);

        WickedGraphicsBackend* owner_ = nullptr;
        std::vector<SpriteVertex> vertices_;
        const ITextureBackend* batchTexture_ = nullptr;
        Matrix transform_;
        bool hasTransform_ = false;
        int filter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        bool active_ = false;
    };

    /**
     * @brief The CNA graphics backend implemented on Wicked Engine's `wi::graphics::GraphicsDevice`.
     *
     * Frame structure (plan_wicked.md design decision 5): all game rendering goes into an
     * off-screen "scene" colour target sized to the virtual resolution, and `Present()` blits that
     * target into the swap chain with the letterbox/overscan/stretch rectangle CNA's presentation
     * mode asks for. Wicked Engine's swap-chain render pass acquires an image and always clears, so
     * it can be entered only once per frame; rendering into an off-screen target instead is what
     * lets CNA switch freely between the back buffer and a `RenderTarget2D` mid-frame, and is the
     * same texture the virtual-resolution scaling needs anyway.
     */
    class WickedGraphicsBackend final : public IGraphicsBackend
    {
    public:
        /**
         * @brief Creates the Wicked Engine device, swap chain, scene target and built-in shaders.
         * @param args Window, virtual resolution, presentation mode, MSAA and VSync request.
         */
        explicit WickedGraphicsBackend(const GraphicsBackendCreateArgs& args);
        /** @brief Waits for the GPU to go idle and releases every device resource. */
        ~WickedGraphicsBackend() override;

        WickedGraphicsBackend(const WickedGraphicsBackend&) = delete;
        WickedGraphicsBackend& operator=(const WickedGraphicsBackend&) = delete;

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
        /** @brief The SDL window this backend presents to. */
        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        /** @brief Always null — this backend never creates an SDL_Renderer. */
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
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        /** @brief Creates the batched sprite renderer. */
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        /** @brief Creates a real GPU occlusion query. */
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        /** @brief Creates a vertex buffer able to hold @p vertex_capacity vertices. */
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        /** @brief Creates a 16-bit index buffer able to hold @p index_capacity indices. */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        /** @brief Creates a 32-bit index buffer able to hold @p index_capacity indices. */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;
        /**
         * @brief Creates a six-face cube map.
         * @param size          Width and height of each face, in texels.
         * @param mipMap        True when a full mip chain was requested.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal; only `Color` is stored today.
         * @return The new cube map.
         */
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap,
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
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap,
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
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                   bool preserveContents,
                                                                   bool mipMap,
                                                                   int multiSampleCount) override;
        /** @brief Binds @p rt as the draw destination, or the scene target when null. */
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        /**
         * @brief Creates a cube-map render target.
         * @param size             Width and height of each face, in pixels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents True when the target's usage preserves contents across binds.
         * @param mipMap           True when a full mip chain was requested.
         * @param multiSampleCount Requested MSAA sample count.
         * @return The new cube render target.
         */
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;
        /**
         * @brief Binds one face of a cube render target as the draw destination.
         * @param rt   Cube target, or null to restore the back buffer.
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         */
        void SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face) override;
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
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
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
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
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
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
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
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                     const IIndexBufferBackend& ib,
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
        void DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb,
                                       const IIndexBufferBackend& ib,
                                       const Matrix& world, const Matrix& view,
                                       const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount,
                                       const GpuDrawParams& params) override;

        /** @brief Inserts a named GPU debug marker into the command stream. */
        void SetStringMarkerEXT(const char* marker) override;
        /**
         * @brief Reports whether this backend supports @p capability.
         * @param capability The capability being queried.
         * @return True when the capability is genuinely available.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        /** @brief The largest single-axis texture dimension this device accepts. */
        [[nodiscard]] int GetMaxTextureDimension() const override;

        // ---- NOXNA: internals shared with this backend's resource classes ----

        /** @brief NOXNA. The Wicked Engine device every resource in this backend is created from. */
        [[nodiscard]] wig::GraphicsDevice* GetDeviceEXT() const { return device_.get(); }
        /** @brief NOXNA. The command list for the frame currently being recorded. */
        [[nodiscard]] wig::CommandList GetCommandListEXT();
        /** @brief NOXNA. Ends any open render pass so a copy/blit operation may be recorded. */
        void EndRenderPassEXT();
        /** @brief NOXNA. Submits everything recorded so far and waits for the GPU to finish. */
        void FlushAndWaitEXT();
        /** @brief NOXNA. The adapter name Wicked Engine reports, for diagnostics. */
        [[nodiscard]] std::string GetAdapterNameEXT() const;
        /** @brief NOXNA. The shader binary format the device consumes (SPIR-V or DXIL). */
        [[nodiscard]] wig::ShaderFormat GetShaderFormatEXT() const;
        /** @brief NOXNA. Records that @p rt is the render target currently bound, if any. */
        void NotifyRenderTargetDestroyedEXT(const WickedRenderTargetBackend* rt);
        /** @brief NOXNA. The cube-target counterpart of NotifyRenderTargetDestroyedEXT. */
        void NotifyRenderTargetCubeDestroyedEXT(const WickedRenderTargetCubeBackend* rt);
        /** @brief NOXNA. Submits a batch of sprite quads; used by WickedSpriteBatchBackend. */
        void DrawSpriteQuadsEXT(const void* vertices, int vertexCount,
                                const ITextureBackend& texture,
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

        void CreateDevice(const GraphicsBackendCreateArgs& args);
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
        void SubmitDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
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
        wig::Shader pixelShader_;
        std::array<wig::InputLayout, static_cast<std::size_t>(WickedShaderVariant::Count)> inputLayouts_;
        std::array<wig::InputLayout, kWickedInstancedVariantCount> instancedInputLayouts_;
        wig::Shader envMapVertexShader_;
        wig::Shader envMapPixelShader_;
        wig::InputLayout envMapInputLayout_;
        wig::Texture whiteTexture_;
        wig::Texture whiteCubeTexture_;

        std::unordered_map<WickedPipelineKey, WickedPipelineEntry, WickedPipelineKeyHash> pipelines_;
        std::unordered_map<std::uint64_t, wig::Sampler> samplers_;

        wig::CommandList cmd_{};
        bool frameActive_ = false;
        bool renderPassActive_ = false;
        bool sceneCleared_ = false;

        WickedRenderTargetBackend* currentRenderTarget_ = nullptr;
        WickedRenderTargetCubeBackend* currentRenderTargetCube_ = nullptr;
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
