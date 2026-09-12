// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformGlContext.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    struct VertexAttributeBinding;
}

namespace CNA::Internal::Renderers
{
    struct GpuDrawParams;
}

namespace CNA::Internal::Renderers::Rlgl::Bridge
{
    /** @brief Native GL sampler values exposed only to focused renderer validation. */
    struct SamplerSnapshot
    {
        int minFilter = 0;
        int magFilter = 0;
        int wrapS = 0;
        int wrapT = 0;
        int wrapR = 0;
        int compareMode = 0;
        float minLod = 0.0f;
        float maxLod = 0.0f;
        float lodBias = 0.0f;
        float anisotropy = 1.0f;
    };

    /** @brief Complete native pipeline-state snapshot exposed only to focused validation. */
    struct PipelineSnapshot
    {
        bool blendEnabled = false;
        int colorSourceBlend = 0;
        int colorDestinationBlend = 0;
        int alphaSourceBlend = 0;
        int alphaDestinationBlend = 0;
        int colorBlendFunction = 0;
        int alphaBlendFunction = 0;
        std::array<int, 4> colorWriteMasks{};
        bool sampleMaskEnabled = false;
        unsigned int sampleMask = 0;
        std::array<float, 4> blendFactor{};
        bool depthTestEnabled = false;
        bool depthWriteEnabled = false;
        int depthFunction = 0;
        bool stencilTestEnabled = false;
        int frontStencilFunction = 0;
        int frontStencilReference = 0;
        unsigned int frontStencilReadMask = 0;
        unsigned int frontStencilWriteMask = 0;
        int frontStencilFail = 0;
        int frontStencilDepthFail = 0;
        int frontStencilPass = 0;
        int backStencilFunction = 0;
        int backStencilReference = 0;
        unsigned int backStencilReadMask = 0;
        unsigned int backStencilWriteMask = 0;
        int backStencilFail = 0;
        int backStencilDepthFail = 0;
        int backStencilPass = 0;
        bool cullEnabled = false;
        int cullFace = 0;
        bool scissorEnabled = false;
        std::array<int, 4> scissorBox{};
        int polygonMode = 0;
        bool polygonOffsetFillEnabled = false;
        float polygonOffsetFactor = 0.0f;
        float polygonOffsetUnits = 0.0f;
    };

    /** @brief rlgl-owned low-level resources for the production sprite scheduler. */
    struct SpritePipeline
    {
        unsigned int program = 0;
        unsigned int vertexArray = 0;
        unsigned int vertexBuffer = 0;
        unsigned int indexBuffer = 0;
        int projectionLocation = -1;
        int textureLocation = -1;
        int vertexCapacity = 0;
        int indexCapacity = 0;
    };

    /** @brief Native storage facts for one renderer-owned buffer. */
    struct BufferSnapshot
    {
        int byteSize = 0;
        int usage = 0;
        std::vector<std::uint8_t> bytes;
    };

    /** @brief rlgl-owned names forming one resolved or multisampled RenderTarget2D. */
    struct RenderTargetStorage
    {
        unsigned int framebuffer = 0;
        unsigned int resolveFramebuffer = 0;
        unsigned int colorTexture = 0;
        unsigned int multisampleColorRenderbuffer = 0;
        unsigned int depthStencilRenderbuffer = 0;
        int multiSampleCount = 0;
    };

    /** @brief Native TextureCube storage facts exposed only to focused validation. */
    struct TextureCubeSnapshot
    {
        unsigned int texture = 0;
        std::array<int, 6> levelZeroWidths{};
        std::array<int, 6> finalLevelWidths{};
        int baseLevel = 0;
        int maxLevel = 0;
        int internalFormat = 0;
    };

    /** @brief Borrowed native storage attached to one slot of an MRT framebuffer. */
    struct MrtAttachment
    {
        unsigned int colorTexture = 0;
        unsigned int multisampleColorRenderbuffer = 0;
        unsigned int depthStencilRenderbuffer = 0;
        int depthFormat = 0;
        int multiSampleCount = 0;
    };

    /** @brief Native MRT framebuffer state exposed only to focused validation. */
    struct MrtFramebufferSnapshot
    {
        unsigned int framebuffer = 0;
        std::array<unsigned int, 4> colorObjects{};
        unsigned int depthObject = 0;
        unsigned int stencilObject = 0;
        std::array<int, 4> drawBuffers{};
        int readBuffer = 0;
        bool complete = false;
    };

    /** @brief rlgl-owned shader and VAO used by the BasicEffect/AlphaTestEffect path. */
    struct PrimitivePipeline
    {
        unsigned int program = 0;
        unsigned int vertexArray = 0;
        int worldViewProjectionLocation = -1;
        int worldLocation = -1;
        std::array<int, 3> normalMatrixLocations{-1, -1, -1};
        int diffuseColorLocation = -1;
        int vertexColorEnabledLocation = -1;
        int textureLocation = -1;
        int texture1Location = -1;
        int textureFlipVLocation = -1;
        int textureEnabledLocation = -1;
        int dualTextureLocation = -1;
        int alphaTestLocation = -1;
        int fogVectorLocation = -1;
        int fogColorLocation = -1;
        int lightingEnabledLocation = -1;
        int preferPerPixelLightingLocation = -1;
        int skinnedLocation = -1;
        int boneRowsLocation = -1;
        int weightsPerVertexLocation = -1;
        int ambientColorLocation = -1;
        int emissiveColorLocation = -1;
        int eyePositionLocation = -1;
        std::array<int, 3> lightDirectionLocations{-1, -1, -1};
        std::array<int, 3> lightDiffuseLocations{-1, -1, -1};
        std::array<int, 3> lightSpecularLocations{-1, -1, -1};
        int specularColorLocation = -1;
        int specularPowerLocation = -1;
    };

    /** @brief Last native primitive submission, exposed only to focused validation. */
    struct PrimitiveDrawSnapshot
    {
        int primitiveMode = 0;
        int elementCount = 0;
        int firstVertex = 0;
        int startIndex = 0;
        int baseVertex = 0;
        int indexType = 0;
        unsigned int texture = 0;
        unsigned int texture1 = 0;
        bool indexed = false;
        bool textureEnabled = false;
        bool dualTexture = false;
        bool skinned = false;
        bool usedRlglDrawWrapper = false;
    };

    /** @brief Native vertex-attribute state exposed only to focused validation. */
    struct VertexAttributeSnapshot
    {
        bool enabled = false;
        int componentCount = 0;
        int scalarType = 0;
        bool normalized = false;
        int stride = 0;
        int offset = 0;
        unsigned int buffer = 0;
    };

    enum ClearPlane : unsigned int
    {
        /** @brief Selects the active framebuffer's color planes. */
        ColorPlane = 1u << 0u,
        /** @brief Selects the active framebuffer's depth plane. */
        DepthPlane = 1u << 1u,
        /** @brief Selects the active framebuffer's stencil plane. */
        StencilPlane = 1u << 2u,
    };

    /**
     * @brief Loads rlgl's GL dispatch and creates its default resources.
     * @param loader CNA platform entry-point loader.
     * @param width Initial drawable width.
     * @param height Initial drawable height.
     * @return The driver OpenGL version string.
     */
    [[nodiscard]] std::string Initialize(
        CNA::Platform::GlProcAddressLoader loader, int width, int height);

    /** @brief Releases rlgl's default resources while its context is current. */
    void Shutdown() noexcept;

    /**
     * @brief Updates rlgl's physical framebuffer bookkeeping.
     * @param width Drawable width.
     * @param height Drawable height.
     */
    void SetFramebufferSize(int width, int height);

    /**
     * @brief Clears selected framebuffer planes while preserving their write masks.
     * @param planes Bitwise ClearPlane selection.
     * @param r Red clear component.
     * @param g Green clear component.
     * @param b Blue clear component.
     * @param a Alpha clear component.
     * @param depth Depth clear value.
     * @param stencil Stencil clear value.
     */
    void Clear(unsigned int planes, float r, float g, float b, float a,
               float depth, int stencil);

    /**
     * @brief Enables or disables depth testing through rlgl.
     * @param enabled True to enable the test.
     */
    void SetDepthTestEnabled(bool enabled);

    /**
     * @brief Enables or disables color blending through rlgl.
     * @param enabled True to enable blending.
     */
    void SetBlendEnabled(bool enabled);

    /**
     * @brief Enables or disables depth writes through rlgl.
     * @param enabled True to enable depth writes.
     */
    void SetDepthWriteEnabled(bool enabled);

    /**
     * @brief Applies the complete XNA blend/output-write state.
     * @param colorSourceBlend Raw color source `Blend` ordinal.
     * @param alphaSourceBlend Raw alpha source `Blend` ordinal.
     * @param colorDestinationBlend Raw color destination `Blend` ordinal.
     * @param alphaDestinationBlend Raw alpha destination `Blend` ordinal.
     * @param colorBlendFunction Raw color `BlendFunction` ordinal.
     * @param alphaBlendFunction Raw alpha `BlendFunction` ordinal.
     * @param colorWriteMasks Four raw `ColorWriteChannels` masks.
     * @param sampleMask XNA multisample coverage mask.
     */
    void ApplyBlendState(
        int colorSourceBlend, int alphaSourceBlend,
        int colorDestinationBlend, int alphaDestinationBlend,
        int colorBlendFunction, int alphaBlendFunction,
        const int* colorWriteMasks, unsigned int sampleMask);

    /**
     * @brief Sets the constant blend color.
     * @param r Red component.
     * @param g Green component.
     * @param b Blue component.
     * @param a Alpha component.
     */
    void SetBlendFactor(float r, float g, float b, float a);

    /**
     * @brief Applies the complete XNA depth/stencil state.
     * @param depthEnable Whether depth testing is enabled.
     * @param depthWriteEnable Whether depth writes are enabled.
     * @param depthFunction Raw depth `CompareFunction` ordinal.
     * @param stencilEnable Whether stencil testing is enabled.
     * @param stencilFunction Raw clockwise/front stencil comparison.
     * @param stencilPass Raw clockwise/front depth-pass operation.
     * @param stencilFail Raw clockwise/front stencil-fail operation.
     * @param stencilDepthFail Raw clockwise/front depth-fail operation.
     * @param stencilReadMask Stencil comparison mask.
     * @param stencilWriteMask Stencil write mask.
     * @param referenceStencil Stencil reference value.
     * @param twoSidedStencilMode Whether front and back faces use different state.
     * @param counterClockwiseStencilFunction Raw back-face comparison.
     * @param counterClockwiseStencilPass Raw back-face depth-pass operation.
     * @param counterClockwiseStencilFail Raw back-face stencil-fail operation.
     * @param counterClockwiseStencilDepthFail Raw back-face depth-fail operation.
     */
    void ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int depthFunction,
        bool stencilEnable, int stencilFunction,
        int stencilPass, int stencilFail, int stencilDepthFail,
        int stencilReadMask, int stencilWriteMask, int referenceStencil,
        bool twoSidedStencilMode, int counterClockwiseStencilFunction,
        int counterClockwiseStencilPass, int counterClockwiseStencilFail,
        int counterClockwiseStencilDepthFail);

    /**
     * @brief Reissues the cached stencil comparisons with a new reference value.
     * @param stencilEnable Whether stencil testing is currently enabled.
     * @param twoSidedStencilMode Whether front and back comparisons differ.
     * @param stencilFunction Raw front comparison ordinal.
     * @param counterClockwiseStencilFunction Raw back comparison ordinal.
     * @param stencilReadMask Stencil comparison mask.
     * @param referenceStencil New reference value.
     */
    void SetStencilReference(
        bool stencilEnable, bool twoSidedStencilMode,
        int stencilFunction, int counterClockwiseStencilFunction,
        int stencilReadMask, int referenceStencil);

    /**
     * @brief Applies XNA culling, fill, scissor-enable, and polygon-offset state.
     * @param cullMode Raw `CullMode` ordinal.
     * @param fillMode Raw `FillMode` ordinal.
     * @param scissorTestEnable Whether scissor testing is enabled.
     * @param depthBiasUnits Depth-format-scaled polygon offset units.
     * @param slopeScaleDepthBias Polygon offset slope factor.
     */
    void ApplyRasterizerState(
        int cullMode, int fillMode, bool scissorTestEnable,
        float depthBiasUnits, float slopeScaleDepthBias);

    /**
     * @brief Captures the native pipeline state for focused transition validation.
     * @return Current GL pipeline state.
     */
    [[nodiscard]] PipelineSnapshot GetPipelineSnapshotForTesting();

    /**
     * @brief Creates the fixed-layout shader and dynamic buffers used by SpriteBatch.
     * @param vertexCapacity Maximum vertices accepted by one upload.
     * @param indexCapacity Maximum 16-bit indices accepted by one upload.
     * @return Complete rlgl-owned sprite pipeline.
     */
    [[nodiscard]] SpritePipeline CreateSpritePipeline(
        int vertexCapacity, int indexCapacity);

    /** @brief Drains rlgl's global immediate batch before a CNA-owned low-level draw. */
    void FlushImmediateBatch();

    /**
     * @brief Releases a complete sprite pipeline while the rlgl context is current.
     * @param pipeline Pipeline to release and clear.
     */
    void DestroySpritePipeline(SpritePipeline& pipeline) noexcept;

    /**
     * @brief Uploads and draws one triangle-list sprite texture group.
     * @param pipeline Live sprite pipeline.
     * @param vertices Interleaved position, UV, and color float data.
     * @param vertexCount Number of eight-float vertices.
     * @param indices Triangle-list 16-bit indices.
     * @param indexCount Number of indices.
     * @param projectionColumnMajor XNA matrix flattened for a GL column-major upload.
     */
    void DrawSpriteGeometry(
        const SpritePipeline& pipeline,
        const float* vertices, int vertexCount,
        const std::uint16_t* indices, int indexCount,
        const float* projectionColumnMajor);

    /**
     * @brief Allocates fixed-capacity vertex storage through rlgl.
     * @param byteCapacity Storage size in bytes.
     * @return Non-zero GL buffer name.
     */
    [[nodiscard]] unsigned int CreateVertexBuffer(int byteCapacity);

    /**
     * @brief Allocates fixed-capacity index storage through rlgl.
     * @param byteCapacity Storage size in bytes.
     * @return Non-zero GL buffer name.
     */
    [[nodiscard]] unsigned int CreateIndexBuffer(int byteCapacity);

    /**
     * @brief Releases a vertex or index buffer through rlgl.
     * @param id Buffer name, or zero.
     */
    void DestroyBuffer(unsigned int id) noexcept;

    /**
     * @brief Updates a prefix of a vertex buffer through rlgl.
     * @param id Buffer name.
     * @param data Source bytes.
     * @param byteCount Number of bytes to write.
     */
    void UpdateVertexBuffer(unsigned int id, const void* data, int byteCount);

    /**
     * @brief Updates a prefix of an index buffer through rlgl.
     * @param id Buffer name.
     * @param data Source bytes.
     * @param byteCount Number of bytes to write.
     */
    void UpdateIndexBuffer(unsigned int id, const void* data, int byteCount);

    /**
     * @brief Replaces a buffer's storage without changing its name.
     * @param id Buffer name.
     * @param indexBuffer True for element-array storage.
     * @param byteCapacity New fixed capacity in bytes.
     */
    void OrphanBuffer(unsigned int id, bool indexBuffer, int byteCapacity);

    /**
     * @brief Reads native buffer allocation facts for focused validation.
     * @param id Buffer name.
     * @param indexBuffer True for element-array storage.
     * @return Size, usage, and exact native bytes.
     */
    [[nodiscard]] BufferSnapshot GetBufferSnapshotForTesting(
        unsigned int id, bool indexBuffer);

    /** @brief Creates the BasicEffect/AlphaTestEffect stock pipeline. */
    [[nodiscard]] PrimitivePipeline CreatePrimitivePipeline();

    /**
     * @brief Releases the baseline primitive pipeline through rlgl.
     * @param pipeline Live or empty pipeline record.
     */
    void DestroyPrimitivePipeline(PrimitivePipeline& pipeline) noexcept;

    /**
     * @brief Submits one declaration-driven primitive draw through the rlgl-owned pipeline.
     * @param pipeline Live primitive pipeline.
     * @param vertexBuffer Vertex buffer name.
     * @param indexBuffer Index buffer name, or zero for a non-indexed draw.
     * @param attributes Semantic-selected vertex attributes.
     * @param attributeCount Number of attribute records.
     * @param worldViewProjectionColumnMajor Transform matrix in GL upload order.
     * @param texture Texture2D name, or zero for rlgl's default white texture.
     * @param texture1 Second Texture2D name, or zero for rlgl's default white texture.
     * @param params Complete stock-effect draw parameters.
     * @param primitiveType Raw XNA PrimitiveType ordinal.
     * @param elementCount Vertex or index count.
     * @param firstVertex First vertex for non-indexed draws.
     * @param startIndex First index for indexed draws.
     * @param baseVertex Value added to decoded indices.
     * @param thirtyTwoBitIndices Whether indexed data uses unsigned 32-bit elements.
     */
    void DrawPrimitiveGeometry(
        const PrimitivePipeline& pipeline,
        unsigned int vertexBuffer, unsigned int indexBuffer,
        const VertexAttributeBinding* attributes, int attributeCount,
        const float* worldViewProjectionColumnMajor,
        unsigned int texture, unsigned int texture1,
        const CNA::Internal::Renderers::GpuDrawParams& params,
        int primitiveType, int elementCount,
        int firstVertex, int startIndex, int baseVertex, bool thirtyTwoBitIndices);

    /**
     * @brief Returns the last primitive-call parameters for focused validation.
     * @return Most recent draw snapshot.
     */
    [[nodiscard]] PrimitiveDrawSnapshot GetPrimitiveDrawSnapshotForTesting();

    /**
     * @brief Returns the live vertex-uniform component limit for palette validation.
     * @return GL_MAX_VERTEX_UNIFORM_COMPONENTS reported by the current context.
     */
    [[nodiscard]] int GetMaxVertexUniformComponentsForTesting();

    /**
     * @brief Applies one generic attribute through rlgl and reads its native VAO state.
     * @param vertexBuffer Vertex buffer name.
     * @param attribute Attribute description to validate.
     * @return Native pointer state captured from a temporary VAO.
     */
    [[nodiscard]] VertexAttributeSnapshot GetVertexAttributeSnapshotForTesting(
        unsigned int vertexBuffer, const VertexAttributeBinding& attribute);

    /**
     * @brief Reads one default-framebuffer stencil value for focused validation.
     * @param x Pixel X coordinate.
     * @param y Top-left-origin pixel Y coordinate.
     * @param framebufferHeight Current framebuffer height.
     * @return The eight least-significant stencil bits.
     */
    [[nodiscard]] std::uint8_t ReadStencilForTesting(
        int x, int y, int framebufferHeight);

    /**
     * @brief Sets the GL viewport and depth range.
     * @param x Left edge in GL coordinates.
     * @param y Bottom edge in GL coordinates.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param minDepth Minimum depth value.
     * @param maxDepth Maximum depth value.
     */
    void SetViewport(int x, int y, int width, int height, float minDepth, float maxDepth);

    /**
     * @brief Sets the GL scissor rectangle.
     * @param x Left edge in GL coordinates.
     * @param y Bottom edge in GL coordinates.
     * @param width Width in pixels.
     * @param height Height in pixels.
     */
    void SetScissor(int x, int y, int width, int height);

    /**
     * @brief Reads an RGBA8 backbuffer rectangle and normalizes it to top-left row order.
     * @param x Left edge in top-left-origin coordinates.
     * @param y Top edge in top-left-origin coordinates.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param framebufferHeight Current physical framebuffer height.
     * @param pixels Destination holding at least width * height * 4 bytes.
     */
    void ReadBackbuffer(
        int x, int y, int width, int height, int framebufferHeight, unsigned char* pixels);

    /**
     * @brief Returns the current context's maximum two-dimensional texture edge.
     * @return The value reported by `GL_MAX_TEXTURE_SIZE`.
     */
    [[nodiscard]] int GetMaxTextureSize();

    /**
     * @brief Reports whether rlgl's live extension probe exposes exact DXT storage.
     * @param surfaceFormat Raw DXT `SurfaceFormat` ordinal.
     * @return True only when the requested DXT format maps to a native GL internal format and the
     * deterministic fallback test override is not active.
     */
    [[nodiscard]] bool SupportsDxtTexture2D(int surfaceFormat) noexcept;

    /**
     * @brief Creates a supported Texture2D and allocates its declared mip chain.
     * @param surfaceFormat Raw `SurfaceFormat` ordinal.
     * @param width Level-zero width.
     * @param height Level-zero height.
     * @param mipLevels Number of mip levels to allocate.
     * @param pixels Tightly packed level-zero format-native bytes.
     * @return The non-zero GL texture name owned by rlgl.
     */
    [[nodiscard]] unsigned int CreateTexture2D(
        int surfaceFormat, int width, int height, int mipLevels,
        const std::uint8_t* pixels);

    /**
     * @brief Releases a texture through rlgl while the device is live.
     * @param id Texture name, or zero.
     */
    void DestroyTexture2D(unsigned int id) noexcept;

    /**
     * @brief Replaces one complete supported Texture2D mip level.
     * @param id Texture name.
     * @param surfaceFormat Raw `SurfaceFormat` ordinal.
     * @param level Mip level.
     * @param width Level width.
     * @param height Level height.
     * @param pixels Tightly packed format-native bytes.
     */
    void UpdateTexture2D(
        unsigned int id, int surfaceFormat, int level,
        int width, int height, const std::uint8_t* pixels);

    /**
     * @brief Reads a format-native rectangle from one texture mip level.
     * @param id Texture name.
     * @param surfaceFormat Raw `SurfaceFormat` ordinal.
     * @param level Mip level.
     * @param levelWidth Complete level width.
     * @param levelHeight Complete level height.
     * @param x Rectangle left edge.
     * @param y Rectangle top edge in upload-memory order.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param pixels Destination holding the format-native rectangle bytes.
     */
    void ReadTexture2D(
        unsigned int id, int surfaceFormat, int level, int levelWidth, int levelHeight,
        int x, int y, int width, int height, std::uint8_t* pixels);

    /**
     * @brief Allocates all faces and mip levels of an RGBA8 cube through rlgl.
     * @param size Width and height of every face.
     * @param mipLevels Number of mip levels to allocate.
     * @return Non-zero rlgl-owned cube texture name.
     */
    [[nodiscard]] unsigned int CreateTextureCubeColor(int size, int mipLevels);

    /**
     * @brief Reports whether rlgl mapped one DXT format to native cube storage.
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     * @return True when the live GL context exposes the exact S3TC internal format.
     */
    [[nodiscard]] bool SupportsDxtTextureCube(int surfaceFormat) noexcept;

    /**
     * @brief Allocates all faces and levels of a DXT cube through rlgl.
     * @param surfaceFormat Dxt1, Dxt3, or Dxt5 ordinal.
     * @param size Width and height of every face.
     * @param mipLevels Number of mip levels to allocate.
     * @param nativeCompressed Whether to request native S3TC rather than RGBA8 fallback storage.
     * @return Non-zero rlgl-owned cube texture name.
     */
    [[nodiscard]] unsigned int CreateTextureCubeDxt(
        int surfaceFormat, int size, int mipLevels, bool nativeCompressed);

    /**
     * @brief Releases a cube texture through rlgl while the device is live.
     * @param id Texture name, or zero.
     */
    void DestroyTextureCube(unsigned int id) noexcept;

    /**
     * @brief Uploads tightly packed RGBA8 pixels to one cube face region.
     * @param id Cube texture name.
     * @param face Cube face index in XNA order.
     * @param level Mip level.
     * @param x Region left edge.
     * @param y Region top edge in upload-memory order.
     * @param width Region width.
     * @param height Region height.
     * @param pixels Source RGBA8 bytes.
     */
    void UpdateTextureCubeColor(
        unsigned int id, int face, int level, int x, int y,
        int width, int height, const std::uint8_t* pixels);

    /**
     * @brief Uploads one block-aligned DXT region to native or decoded cube storage.
     * @param id Cube texture name.
     * @param surfaceFormat Dxt1, Dxt3, or Dxt5 ordinal.
     * @param nativeCompressed Whether @p id uses native S3TC storage.
     * @param face Cube face index in XNA order.
     * @param level Mip level.
     * @param x Region left edge in texels.
     * @param y Region top edge in texels.
     * @param width Region width in texels.
     * @param height Region height in texels.
     * @param blocks Exact tightly packed DXT blocks.
     * @param byteCount Number of source bytes.
     */
    void UpdateTextureCubeDxt(
        unsigned int id, int surfaceFormat, bool nativeCompressed,
        int face, int level, int x, int y, int width, int height,
        const std::uint8_t* blocks, std::size_t byteCount);

    /**
     * @brief Reads one RGBA8 cube-face rectangle without framebuffer row inversion.
     * @param id Cube texture name.
     * @param face Cube face index in XNA order.
     * @param level Mip level.
     * @param levelSize Complete mip edge length.
     * @param x Region left edge.
     * @param y Region top edge in upload-memory order.
     * @param width Region width.
     * @param height Region height.
     * @param pixels Destination RGBA8 bytes.
     */
    void ReadTextureCubeColor(
        unsigned int id, int face, int level, int levelSize,
        int x, int y, int width, int height, std::uint8_t* pixels);

    /**
     * @brief Binds a cube texture to one rlgl texture unit.
     * @param id Cube texture name, or zero.
     * @param unit Texture unit index.
     */
    void BindTextureCube(unsigned int id, int unit);

    /**
     * @brief Returns the cube texture bound to one unit for focused validation.
     * @param unit Texture unit index.
     * @return Native cube texture name.
     */
    [[nodiscard]] unsigned int GetBoundTextureCubeForTesting(int unit);

    /**
     * @brief Captures the allocated faces and mip range of a cube texture.
     * @param id Cube texture name.
     * @param levelCount Declared number of levels.
     * @return Native allocation facts without disturbing caller bindings.
     */
    [[nodiscard]] TextureCubeSnapshot GetTextureCubeSnapshotForTesting(
        unsigned int id, int levelCount);

    /**
     * @brief Creates a framebuffer with a Color texture and exact XNA depth/stencil storage.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param levelCount Number of allocated color-texture mip levels.
     * @param depthFormat Raw XNA DepthFormat ordinal.
     * @param multiSampleCount Requested multisample count, or zero.
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     * @return Complete framebuffer storage whose color texture is rlgl-owned.
     */
    [[nodiscard]] RenderTargetStorage CreateRenderTarget2D(
        int width, int height, int levelCount, int depthFormat,
        int multiSampleCount, int surfaceFormat);

    /**
     * @brief Returns the usable simultaneous-color-target count of the live GL context.
     * @return The smaller of the draw-buffer and color-attachment limits, capped at XNA's four.
     */
    [[nodiscard]] int GetMaxRenderTargets();

    /**
     * @brief Creates and validates a new MRT framebuffer from borrowed target storage.
     * @param attachments Ordered color attachments; slot zero also supplies depth/stencil.
     * @param count Number of attachments in the range two through four.
     * @return A complete rlgl-created framebuffer that owns no attachment resources.
     */
    [[nodiscard]] unsigned int CreateMrtFramebuffer(
        const MrtAttachment* attachments, int count);

    /**
     * @brief Deletes an MRT framebuffer without deleting its borrowed target attachments.
     * @param framebuffer Live or zero framebuffer name, cleared on return.
     */
    void DestroyMrtFramebuffer(unsigned int& framebuffer) noexcept;

    /**
     * @brief Captures slot, draw-buffer, depth, stencil, and completeness state for a live MRT FBO.
     * @param framebuffer Non-zero MRT framebuffer name.
     * @return Native framebuffer state restored without disturbing the active target.
     */
    [[nodiscard]] MrtFramebufferSnapshot GetMrtFramebufferSnapshotForTesting(
        unsigned int framebuffer);

    /**
     * @brief Captures the currently bound draw framebuffer as an MRT validation snapshot.
     * @return Native state for the non-default active draw framebuffer.
     */
    [[nodiscard]] MrtFramebufferSnapshot GetBoundMrtFramebufferSnapshotForTesting();

    /**
     * @brief Probes whether the live context can attach one exact XNA color format.
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     * @return True only when the exact mapped single-sample framebuffer is complete.
     */
    [[nodiscard]] bool ProbeRenderTargetFormat(int surfaceFormat);

    /**
     * @brief Returns the native texel width of a supported render-target format.
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     * @return Format-native byte count per texel.
     */
    [[nodiscard]] int RenderTargetBytesPerTexel(int surfaceFormat);

    /**
     * @brief Releases a RenderTarget2D while preserving an unrelated active framebuffer.
     * @param storage Live or empty target storage.
     */
    void DestroyRenderTarget2D(RenderTargetStorage& storage) noexcept;

    /**
     * @brief Binds one renderer-owned framebuffer for drawing.
     * @param framebuffer Non-zero framebuffer name.
     */
    void BindFramebuffer(unsigned int framebuffer);

    /**
     * @brief Resolves a multisampled Color target into its sampleable texture through rlgl.
     * @param storage Multisampled target storage.
     * @param width Target width.
     * @param height Target height.
     */
    void ResolveRenderTarget2D(
        const RenderTargetStorage& storage, int width, int height);

    /**
     * @brief Regenerates a Color render target's full mip chain through rlgl.
     * @param texture rlgl-owned Color texture name.
     * @param width Base-level width.
     * @param height Base-level height.
     * @param expectedLevelCount Required resulting mip count.
     */
    void GenerateRenderTargetMipmaps(
        unsigned int texture, int width, int height, int expectedLevelCount);

    /**
     * @brief Reads an RGBA8 target rectangle in XNA top-row-first order.
     * @param framebuffer Level-zero framebuffer name.
     * @param texture Color texture name used for non-zero levels.
     * @param level Mip level.
     * @param levelWidth Complete mip width.
     * @param levelHeight Complete mip height.
     * @param x Rectangle left edge.
     * @param y Rectangle top edge.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param pixels Destination holding the format-native rectangle bytes.
     * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
     */
    void ReadRenderTarget2D(
        unsigned int framebuffer, unsigned int texture, int level,
        int levelWidth, int levelHeight, int x, int y, int width, int height,
        std::uint8_t* pixels, int surfaceFormat);

    /**
     * @brief Binds a two-dimensional texture through rlgl.
     * @param id Texture name, or zero to unbind.
     * @param unit Texture unit.
     */
    void BindTexture2D(unsigned int id, int unit);

    /**
     * @brief Returns the two-dimensional texture bound to a unit for focused validation.
     * @param unit Texture unit.
     * @return The current GL texture name.
     */
    [[nodiscard]] unsigned int GetBoundTexture2DForTesting(int unit);

    /**
     * @brief Returns the number of fragment texture units available to XNA samplers.
     * @return The live `GL_MAX_TEXTURE_IMAGE_UNITS` value.
     */
    [[nodiscard]] int GetMaxSamplerSlots();

    /**
     * @brief Returns the driver anisotropy ceiling found by rlgl's extension probe.
     * @return Maximum supported anisotropy, or one when the extension is unavailable.
     */
    [[nodiscard]] float GetMaxSamplerAnisotropy();

    /**
     * @brief Creates one GL 3.3 sampler object for a CNA texture slot.
     * @return A non-zero sampler name.
     */
    [[nodiscard]] unsigned int CreateSampler();

    /**
     * @brief Releases sampler objects while the rlgl device is live.
     * @param samplers Contiguous sampler names.
     * @param count Number of names in the array.
     */
    void DestroySamplers(const unsigned int* samplers, std::size_t count) noexcept;

    /**
     * @brief Applies one complete XNA sampler description and binds it to a texture unit.
     * @param sampler Sampler name.
     * @param slot Texture unit.
     * @param filter Raw `TextureFilter` ordinal.
     * @param addressU Raw U `TextureAddressMode` ordinal.
     * @param addressV Raw V `TextureAddressMode` ordinal.
     * @param addressW Raw W `TextureAddressMode` ordinal.
     * @param maxAnisotropy Requested anisotropy.
     * @param maxMipLevel Most detailed mip level the sampler may select.
     * @param lodBias Mipmap level-of-detail bias.
     */
    void ApplySampler(
        unsigned int sampler, int slot, int filter,
        int addressU, int addressV, int addressW,
        int maxAnisotropy, int maxMipLevel, float lodBias);

    /**
     * @brief Reads a sampler object's native state for focused validation.
     * @param sampler Sampler name.
     * @return Complete state snapshot.
     */
    [[nodiscard]] SamplerSnapshot GetSamplerSnapshotForTesting(unsigned int sampler);

    /**
     * @brief Returns the sampler object bound to a texture unit for focused validation.
     * @param slot Texture unit.
     * @return Bound sampler name, or zero.
     */
    [[nodiscard]] unsigned int GetBoundSamplerForTesting(int slot);

    /**
     * @brief Draws the unit-zero texture with a constant coordinate through rlgl's test batch.
     * @param u Horizontal texture coordinate.
     * @param v Vertical texture coordinate.
     * @param width Backbuffer width.
     * @param height Backbuffer height.
     */
    void DrawBoundTextureSampleForTesting(float u, float v, int width, int height);

    /**
     * @brief Draws a constant-coordinate rectangle without overriding pipeline state.
     * @param u Horizontal texture coordinate.
     * @param v Vertical texture coordinate.
     * @param x Rectangle left edge.
     * @param y Rectangle top edge.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param framebufferWidth Backbuffer width.
     * @param framebufferHeight Backbuffer height.
     */
    void DrawBoundTextureRectangleForTesting(
        float u, float v, float x, float y, float width, float height,
        int framebufferWidth, int framebufferHeight);

    /** @brief Restores the platform default framebuffer as the active draw target. */
    void BindDefaultFramebuffer();
}
