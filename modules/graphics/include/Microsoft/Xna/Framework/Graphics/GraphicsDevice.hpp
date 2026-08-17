// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceCreatedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "CNA/CNAHelper.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Unsupported3DGraphicsCallBehavior.hpp"

namespace CNA::Platform
{
    class IPlatform;
    class IPlatformSurfacePresenter;
    class IPlatformWindow;
}

namespace Microsoft::Xna::Framework
{
    class Game;
    class GameWindow;
    class GraphicsDeviceManager;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class BasicEffect;
    class Effect;
    class RenderTarget2D;
    class RenderTargetCube;
    class RenderTargetCube;
}

namespace CNA::Internal::Renderers
{
    class IGraphicsRenderer;
    struct GpuDrawParams;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice : public System::Object, public System::IDisposable
    {
    public:
        // --- Events ---
        /** @brief Raised when this device is disposed. */
        System::EventHandler<System::EventArgs> Disposing;
        /** @brief Raised when the device is lost (XNA compliance; never raised on desktop). */
        System::EventHandler<System::EventArgs> DeviceLost;
        /** @brief Raised after the device has been reset. */
        System::EventHandler<System::EventArgs> DeviceReset;
        /** @brief Raised before the device is reset. */
        System::EventHandler<System::EventArgs> DeviceResetting;
        /** @brief Raised when a graphics resource is created. */
        System::EventHandler<ResourceCreatedEventArgs> ResourceCreated;
        /** @brief Raised when a graphics resource is destroyed. */
        System::EventHandler<ResourceDestroyedEventArgs> ResourceDestroyed;

        // --- Constructors ---
        /** @brief Initializes a GraphicsDevice with no window (headless mode). */
        GraphicsDevice();

        /**
         * @brief Initializes a new GraphicsDevice for the given adapter and presentation settings.
         *
         * @param adapter                The graphics adapter to use.
         * @param graphicsProfile        The graphics profile (Reach or HiDef).
         * @param presentationParameters The presentation options (back-buffer size, format, etc.).
         */
        GraphicsDevice(GraphicsAdapter& adapter, GraphicsProfile graphicsProfile,
                       const PresentationParameters& presentationParameters);

        /** @brief Destructor. */
        CNAEXT ~GraphicsDevice() override;

        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;
        GraphicsDevice(GraphicsDevice&&) = delete;
        GraphicsDevice& operator=(GraphicsDevice&&) = delete;

        // --- State properties ---
        /** @brief Returns true if this device has been disposed. */
        [[nodiscard]] bool getIsDisposedProperty() const;
        /** @brief Returns the current device status. */
        [[nodiscard]] GraphicsDeviceStatus getGraphicsDeviceStatusProperty() const;
        /** @brief Returns the graphics adapter associated with this device. */
        [[nodiscard]] GraphicsAdapter& getAdapterProperty() const;
        /** @brief Returns the graphics profile used to create this device. */
        [[nodiscard]] GraphicsProfile getGraphicsProfileProperty() const;
        /** @brief Returns the presentation parameters for this device (mutable). */
        [[nodiscard]] PresentationParameters& getPresentationParametersProperty();
        /** @brief Returns the presentation parameters for this device (const). */
        [[nodiscard]] const PresentationParameters& getPresentationParametersProperty() const;

        // --- Display ---
        /** @brief Returns the current display mode of this device. */
        [[nodiscard]] DisplayMode getDisplayModeProperty() const;

        // --- GL State ---
        /** @brief Returns the texture collection for pixel shader sampler slots. */
        [[nodiscard]] TextureCollection& getTexturesProperty();
        /** @brief Returns the sampler state collection for pixel shader slots. */
        [[nodiscard]] SamplerStateCollection& getSamplerStatesProperty();
        /** @brief Returns the texture collection for vertex shader sampler slots. */
        [[nodiscard]] TextureCollection& getVertexTexturesProperty();
        /** @brief Returns the sampler state collection for vertex shader slots. */
        [[nodiscard]] SamplerStateCollection& getVertexSamplerStatesProperty();

        /** @brief Returns the current blend state (mutable). */
        [[nodiscard]] BlendState& getBlendStateProperty();
        /**
         * @brief Sets the blend state.
         * @param value The new blend state to apply.
         */
        void setBlendStateProperty(const BlendState& value);
        /** @brief Returns the current blend state (const). */
        [[nodiscard]] const BlendState& getBlendStateProperty() const;

        /** @brief Returns the current depth-stencil state (mutable). */
        [[nodiscard]] DepthStencilState& getDepthStencilStateProperty();
        /**
         * @brief Sets the depth-stencil state.
         * @param value The new depth-stencil state to apply.
         */
        void setDepthStencilStateProperty(const DepthStencilState& value);
        /** @brief Returns the current depth-stencil state (const). */
        [[nodiscard]] const DepthStencilState& getDepthStencilStateProperty() const;

        /** @brief Returns the current rasterizer state (mutable). */
        [[nodiscard]] RasterizerState& getRasterizerStateProperty();
        /**
         * @brief Sets the rasterizer state.
         * @param value The new rasterizer state to apply.
         */
        void setRasterizerStateProperty(const RasterizerState& value);
        /** @brief Returns the current rasterizer state (const). */
        [[nodiscard]] const RasterizerState& getRasterizerStateProperty() const;

        /** @brief Returns the current scissor rectangle. */
        [[nodiscard]] Rectangle getScissorRectangleProperty() const;
        /**
         * @brief Sets the scissor rectangle.
         * @param value The rectangle to use for scissor clipping.
         */
        void setScissorRectangleProperty(const Rectangle& value);

        /** @brief Returns the current viewport. */
        [[nodiscard]] const Viewport& getViewportProperty() const;
        /**
         * @brief Sets the viewport.
         * @param value The viewport to set.
         */
        void setViewportProperty(const Viewport& value);

        /** @brief Returns the current blend factor color. */
        [[nodiscard]] Color getBlendFactorProperty() const;
        /**
         * @brief Sets the blend factor color.
         * @param value The color to use as blend factor.
         */
        void setBlendFactorProperty(const Color& value);

        /** @brief Returns the current multisample mask. */
        [[nodiscard]] int getMultiSampleMaskProperty() const;
        /**
         * @brief Sets the multisample mask.
         * @param value The bitmask for multisample anti-aliasing.
         */
        void setMultiSampleMaskProperty(int value);

        /** @brief Returns the current reference stencil value. */
        [[nodiscard]] int getReferenceStencilProperty() const;
        /**
         * @brief Sets the reference stencil value.
         * @param value The reference value for stencil operations.
         */
        void setReferenceStencilProperty(int value);

        // --- Index buffer ---
        /** @brief Returns the currently bound index buffer, or nullptr if none. */
        [[nodiscard]] const IndexBuffer* getIndicesProperty() const;
        /**
         * @brief Sets the index buffer.
         * @param indexBuffer Pointer to the index buffer to bind, or nullptr to unbind.
         */
        void setIndicesProperty(const IndexBuffer* indexBuffer);

        // --- Core operations ---
        /**
         * @brief Clears the back buffer to the specified color.
         * @param color The color to clear to.
         */
        void Clear(const Color& color);
        /**
         * @brief Clears the back buffer to the specified RGBA components.
         * @param r Red channel (0–1).
         * @param g Green channel (0–1).
         * @param b Blue channel (0–1).
         * @param a Alpha channel (0–1).
         */
        void Clear(float r, float g, float b, float a);
        /**
         * @brief Clears the specified buffers.
         * @param options Flags indicating which buffers to clear.
         * @param color   Color value for the color buffer.
         * @param depth   Depth value for the depth buffer (0–1).
         * @param stencil Stencil value for the stencil buffer.
         */
        void Clear(ClearOptions options, const Color& color, float depth, int stencil);
        /**
         * @brief Clears the color and depth buffers.
         * @param color Color value for the color buffer.
         * @param depth Depth value for the depth buffer (0–1).
         */
        void Clear(const Color& color, float depth);

        /** @brief Presents the rendered frame to the display. */
        void Present();

        /** @brief Resets the device using the current presentation parameters. */
        void Reset();
        /**
         * @brief Resets the device with new presentation parameters.
         * @param presentationParameters The new presentation parameters.
         */
        void Reset(const PresentationParameters& presentationParameters);
        /**
         * @brief Resets the device with new presentation parameters and a specific adapter.
         * @param presentationParameters The new presentation parameters.
         * @param adapter                The graphics adapter to use.
         */
        void Reset(const PresentationParameters& presentationParameters, GraphicsAdapter& adapter);
        /**
         * @brief Resets the device with new presentation parameters and an optional adapter pointer.
         * @param presentationParameters The new presentation parameters.
         * @param adapter                Pointer to the graphics adapter, or nullptr to keep the current one.
         */
        void Reset(const PresentationParameters& presentationParameters, GraphicsAdapter* adapter);

        /** @brief Releases all resources held by this device. */
        void Dispose() override;

        // --- Back-buffer readback ---
        /**
         * @brief Copies all back-buffer pixels into the provided Color array.
         * @param data         Output array to receive pixel data.
         * @param elementCount Number of Color elements to read.
         */
        void GetBackBufferData(Color* data, int elementCount);
        /**
         * @brief Copies back-buffer pixels into the provided Color array starting at an offset.
         * @param data         Output array to receive pixel data.
         * @param startIndex   First element index in @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetBackBufferData(Color* data, int startIndex, int elementCount);
        /**
         * @brief Copies a rectangular region of the back buffer into the provided Color array.
         * @param rect         Source rectangle, or nullptr for the full back buffer.
         * @param data         Output array to receive pixel data.
         * @param startIndex   First element index in @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetBackBufferData(const Rectangle* rect, Color* data, int startIndex, int elementCount);

        // --- Render targets ---
        /**
         * @brief Sets a single 2D render target, or nullptr to restore the back buffer.
         * @param renderTarget The render target to bind, or nullptr.
         */
        void SetRenderTarget(RenderTarget2D* renderTarget);
        /**
         * @brief Sets a single cube-map face as the render target.
         * @param renderTarget The cube render target.
         * @param cubeMapFace  The cube face to render into.
         */
        void SetRenderTarget(RenderTargetCube* renderTarget, CubeMapFace cubeMapFace);
        /**
         * @brief Sets multiple render targets simultaneously.
         * @param renderTargets Vector of render target bindings to apply.
         */
        void SetRenderTargets(const std::vector<RenderTargetBinding>& renderTargets);
        /**
         * @brief Returns the currently bound render target bindings.
         * @return A vector of active RenderTargetBinding entries.
         */
        [[nodiscard]] std::vector<RenderTargetBinding> GetRenderTargets() const;

        // --- Vertex/index buffers ---
        /**
         * @brief Binds a vertex buffer with no vertex offset.
         * @param vertexBuffer The vertex buffer to bind, or nullptr to unbind.
         */
        void SetVertexBuffer(const VertexBuffer* vertexBuffer);
        /**
         * @brief Binds a vertex buffer with an explicit vertex offset.
         * @param vertexBuffer The vertex buffer to bind.
         * @param vertexOffset Offset (in vertices) into the buffer.
         */
        void SetVertexBuffer(const VertexBuffer* vertexBuffer, int vertexOffset);
        /**
         * @brief Binds multiple vertex buffers simultaneously.
         * @param vertexBuffers Vector of vertex buffer bindings to apply. A binding whose
         *        vertex buffer is null is a legal unused slot; an empty vector is valid and
         *        unbinds all vertex buffers.
         * @throws System::ArgumentOutOfRangeException if more than 16 bindings are supplied.
         */
        void SetVertexBuffers(const std::vector<VertexBufferBinding>& vertexBuffers);
        /**
         * @brief Returns the currently bound vertex buffer bindings.
         * @return A vector of active VertexBufferBinding entries.
         */
        [[nodiscard]] std::vector<VertexBufferBinding> GetVertexBuffers() const;

        /**
         * @brief Binds an index buffer.
         * @param indexBuffer The index buffer to bind, or nullptr to unbind.
         */
        void SetIndexBuffer(const IndexBuffer* indexBuffer);
        /**
         * @brief Returns the currently bound vertex buffer (first slot).
         * @return Pointer to the bound vertex buffer, or nullptr.
         */
        [[nodiscard]] const VertexBuffer* GetVertexBuffer() const;
        /**
         * @brief Returns the currently bound index buffer.
         * @return Pointer to the bound index buffer, or nullptr.
         */
        [[nodiscard]] const IndexBuffer* GetIndexBuffer() const;

        // --- Draw ---
        /**
         * @brief Draws non-indexed primitives from the bound vertex buffer.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexStart    Index of the first vertex to draw.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawPrimitives(PrimitiveType primitiveType, int vertexStart, int primitiveCount);
        /**
         * @brief Draws indexed primitives from the bound vertex and index buffers.
         * @param primitiveType  The type of primitive to draw.
         * @param baseVertex     Offset added to each index before reading from the vertex buffer.
         * @param minVertexIndex Minimum vertex index among the referenced vertices.
         * @param numVertices    Number of vertices referenced.
         * @param startIndex     Location in the index buffer to start reading.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawIndexedPrimitives(PrimitiveType primitiveType,
                                   int baseVertex, int minVertexIndex,
                                   int numVertices, int startIndex, int primitiveCount);
        /**
         * @brief Draws @p instanceCount instances of one indexed geometry range.
         *
         * The geometry range takes the same contract as `DrawIndexedPrimitives`: @p startIndex is
         * an index-element offset, @p primitiveCount fixes the exact topology-derived index count,
         * and @p baseVertex is added to every decoded index exactly once. @p instanceCount is
         * independent of that range — it never widens or narrows the consumed geometry, and the
         * geometry never changes how many instances are submitted. Each binding's vertex offset is
         * an element offset. Per-instance data comes from the bound `VertexBufferBinding` whose
         * instance frequency is greater than zero, advances once per frequency-sized group, and
         * begins at that binding's vertex offset.
         *
         * @param primitiveType  The type of primitive to draw.
         * @param baseVertex     Offset added to each index before reading from the vertex buffer.
         * @param minVertexIndex Minimum vertex index among the referenced vertices.
         * @param numVertices    Number of vertices referenced.
         * @param startIndex     Location in the index buffer to start reading.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances to draw.
         * @throws std::runtime_error if no vertex buffer, index buffer or effect is bound.
         * @throws System::ArgumentOutOfRangeException if @p primitiveCount, @p numVertices or
         *         @p instanceCount is not positive; if @p baseVertex, @p minVertexIndex or
         *         @p startIndex is negative; if the requested index range leaves the bound index
         *         buffer; if the declared vertex range leaves the bound vertex buffer after its
         *         binding offset and @p baseVertex; or if the required per-instance element range
         *         leaves its bound buffer after applying binding offset and instance frequency.
         */
        void DrawInstancedPrimitives(PrimitiveType primitiveType,
                                     int baseVertex, int minVertexIndex,
                                     int numVertices, int startIndex,
                                     int primitiveCount, int instanceCount);
        /**
         * @brief Draws non-indexed primitives from a user-supplied raw vertex buffer.
         *
         * The vertex data is assumed to be in VertexPositionColor packed layout (stride=16).
         * To supply a different layout use the overload that accepts a VertexDeclaration.
         *
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the raw vertex data (assumed VertexPositionColor layout).
         * @param vertexOffset   Offset into @p vertexData (in vertices) to start drawing from.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType, const void* vertexData,
                                int vertexOffset, int primitiveCount);

        /**
         * @brief Draws non-indexed primitives from a user-supplied raw vertex buffer with an
         *        explicit VertexDeclaration describing the vertex layout.
         *
         * Corresponds to FNA's DrawUserPrimitives&lt;T&gt;(primitiveType, T[], vertexOffset,
         * primitiveCount, VertexDeclaration) overload.  The raw bytes are uploaded to a
         * transient vertex buffer using the stride from @p vertexDeclaration.
         *
         * @p vertexData must already be a GPU vertex stream at the declared stride. The built-in
         * vertex structures are not such a stream — they carry vtable and padding bytes — so an
         * array of them is drawn through the matching typed overload below instead.
         *
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the raw vertex data.
         * @param vertexOffset       Offset into @p vertexData (in vertices).
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration has no elements, a
         *         non-positive stride, or an element reaching past its own stride.
         * @throws System::ArgumentOutOfRangeException if @p vertexOffset is negative, if
         *         @p primitiveCount is not positive, or if the source byte range overflows.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType, const void* vertexData,
                                int vertexOffset, int primitiveCount,
                                const VertexDeclaration& vertexDeclaration);

        /**
         * @brief Draws non-indexed primitives from a VertexPositionColor array with an explicit
         *        VertexDeclaration.
         *
         * The vertex values are converted into the GPU stream @p vertexDeclaration describes, so
         * no object-model byte ever reaches a renderer.
         *
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const VertexPositionColor* vertexData, int vertexOffset,
                                int primitiveCount, const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws non-indexed primitives from a VertexPositionTexture array with an explicit
         *        VertexDeclaration.
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const VertexPositionTexture* vertexData, int vertexOffset,
                                int primitiveCount, const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws non-indexed primitives from a VertexPositionColorTexture array with an
         *        explicit VertexDeclaration.
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const VertexPositionColorTexture* vertexData, int vertexOffset,
                                int primitiveCount, const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws non-indexed primitives from a VertexPositionNormalTexture array with an
         *        explicit VertexDeclaration.
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const VertexPositionNormalTexture* vertexData, int vertexOffset,
                                int primitiveCount, const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws non-indexed primitives from a user-supplied VertexPositionColor array.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const VertexPositionColor* vertexData, int vertexOffset, int primitiveCount);
        /**
         * @brief Draws non-indexed primitives from a user-supplied VertexPositionColorTexture array.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const VertexPositionColorTexture* vertexData, int vertexOffset, int primitiveCount);
        /**
         * @brief Draws non-indexed primitives from a user-supplied VertexPositionTexture array.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const VertexPositionTexture* vertexData, int vertexOffset, int primitiveCount);
        /**
         * @brief Draws non-indexed primitives from a user-supplied VertexPositionNormalTexture array.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const VertexPositionNormalTexture* vertexData, int vertexOffset, int primitiveCount);

        /**
         * @brief Draws indexed primitives from user-supplied raw vertex and index data.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the raw vertex array.
         * @param vertexOffset   Offset into @p vertexData (in vertices).
         * @param numVertices    Number of vertices in @p vertexData.
         * @param indexData      Pointer to the raw index data.
         * @param indexOffset    Offset into @p indexData (in indices).
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const void* vertexData, int vertexOffset, int numVertices,
                                       const void* indexData, int indexOffset, int primitiveCount);
        /**
         * @brief Draws indexed VertexPositionColor primitives from user-supplied arrays (16-bit indices).
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param numVertices    Number of vertices.
         * @param indexData      Pointer to the 16-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionColor* vertexData, int vertexOffset, int numVertices,
                                       const std::uint16_t* indexData, int indexOffset, int primitiveCount);
        /**
         * @brief Draws indexed VertexPositionColorTexture primitives from user-supplied arrays (16-bit indices).
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param numVertices    Number of vertices.
         * @param indexData      Pointer to the 16-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionColorTexture* vertexData, int vertexOffset, int numVertices,
                                       const std::uint16_t* indexData, int indexOffset, int primitiveCount);
        /**
         * @brief Draws indexed VertexPositionTexture primitives from user-supplied arrays (16-bit indices).
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param numVertices    Number of vertices.
         * @param indexData      Pointer to the 16-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionTexture* vertexData, int vertexOffset, int numVertices,
                                       const std::uint16_t* indexData, int indexOffset, int primitiveCount);
        /**
         * @brief Draws indexed VertexPositionNormalTexture primitives from user-supplied arrays (16-bit indices).
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param numVertices    Number of vertices.
         * @param indexData      Pointer to the 16-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionNormalTexture* vertexData, int vertexOffset, int numVertices,
                                       const std::uint16_t* indexData, int indexOffset, int primitiveCount);
        // 32-bit index overloads
        /**
         * @brief Draws indexed VertexPositionColor primitives from user-supplied arrays (32-bit indices).
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param numVertices    Number of vertices.
         * @param indexData      Pointer to the 32-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionColor* vertexData, int vertexOffset, int numVertices,
                                       const std::uint32_t* indexData, int indexOffset, int primitiveCount);
        /**
         * @brief Draws indexed VertexPositionColorTexture primitives from user-supplied arrays (32-bit indices).
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param numVertices    Number of vertices.
         * @param indexData      Pointer to the 32-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionColorTexture* vertexData, int vertexOffset, int numVertices,
                                       const std::uint32_t* indexData, int indexOffset, int primitiveCount);
        /**
         * @brief Draws indexed VertexPositionTexture primitives from user-supplied arrays (32-bit indices).
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param numVertices    Number of vertices.
         * @param indexData      Pointer to the 32-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionTexture* vertexData, int vertexOffset, int numVertices,
                                       const std::uint32_t* indexData, int indexOffset, int primitiveCount);
        /**
         * @brief Draws indexed VertexPositionNormalTexture primitives from user-supplied arrays (32-bit indices).
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param numVertices    Number of vertices.
         * @param indexData      Pointer to the 32-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionNormalTexture* vertexData, int vertexOffset, int numVertices,
                                       const std::uint32_t* indexData, int indexOffset, int primitiveCount);

        /**
         * @brief Draws indexed primitives from user-supplied arrays with an explicit vertex layout (16-bit indices).
         *
         * Matches FNA's second generic overload: `DrawUserIndexedPrimitives<T>(…, short[], …, VertexDeclaration)`.
         * Use this when the vertex type does not implement IVertexType and the layout must be supplied explicitly.
         *
         * @p vertexData must already be a GPU vertex stream at the declared stride; an array of a
         * built-in vertex structure is drawn through the matching typed overload below instead.
         *
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the raw vertex array.
         * @param vertexOffset       Offset into @p vertexData (in vertices).
         * @param numVertices        Number of vertices in @p vertexData.
         * @param indexData          Pointer to the 16-bit index array.
         * @param indexOffset        Offset into @p indexData (in indices).
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the vertex layout (stride and element semantics).
         * @throws System::ArgumentException if @p vertexDeclaration has no elements, a
         *         non-positive stride, or an element reaching past its own stride.
         * @throws System::ArgumentOutOfRangeException if an offset or count is negative, if
         *         @p primitiveCount is not positive, or if a source byte range overflows.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const void* vertexData, int vertexOffset, int numVertices,
                                       const std::uint16_t* indexData, int indexOffset, int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);

        /**
         * @brief Draws indexed VertexPositionColor primitives with an explicit VertexDeclaration
         *        (16-bit indices).
         *
         * The vertex values are converted into the GPU stream @p vertexDeclaration describes, so
         * no object-model byte ever reaches a renderer.
         *
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index; indices are relative to it.
         * @param numVertices        Number of vertices to convert.
         * @param indexData          Pointer to the 16-bit index array.
         * @param indexOffset        Starting index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionColor* vertexData, int vertexOffset,
                                       int numVertices, const std::uint16_t* indexData,
                                       int indexOffset, int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws indexed VertexPositionTexture primitives with an explicit
         *        VertexDeclaration (16-bit indices).
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index; indices are relative to it.
         * @param numVertices        Number of vertices to convert.
         * @param indexData          Pointer to the 16-bit index array.
         * @param indexOffset        Starting index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionTexture* vertexData, int vertexOffset,
                                       int numVertices, const std::uint16_t* indexData,
                                       int indexOffset, int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws indexed VertexPositionColorTexture primitives with an explicit
         *        VertexDeclaration (16-bit indices).
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index; indices are relative to it.
         * @param numVertices        Number of vertices to convert.
         * @param indexData          Pointer to the 16-bit index array.
         * @param indexOffset        Starting index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionColorTexture* vertexData,
                                       int vertexOffset, int numVertices,
                                       const std::uint16_t* indexData, int indexOffset,
                                       int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws indexed VertexPositionNormalTexture primitives with an explicit
         *        VertexDeclaration (16-bit indices).
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index; indices are relative to it.
         * @param numVertices        Number of vertices to convert.
         * @param indexData          Pointer to the 16-bit index array.
         * @param indexOffset        Starting index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionNormalTexture* vertexData,
                                       int vertexOffset, int numVertices,
                                       const std::uint16_t* indexData, int indexOffset,
                                       int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);

        /**
         * @brief Draws indexed primitives from user-supplied arrays with an explicit vertex layout (32-bit indices).
         *
         * Matches FNA's second generic overload: `DrawUserIndexedPrimitives<T>(…, int[], …, VertexDeclaration)`.
         * Use this when the vertex type does not implement IVertexType and the layout must be supplied explicitly.
         *
         * @p vertexData must already be a GPU vertex stream at the declared stride; an array of a
         * built-in vertex structure is drawn through the matching typed overload below instead.
         *
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the raw vertex array.
         * @param vertexOffset       Offset into @p vertexData (in vertices).
         * @param numVertices        Number of vertices in @p vertexData.
         * @param indexData          Pointer to the 32-bit index array.
         * @param indexOffset        Offset into @p indexData (in indices).
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the vertex layout (stride and element semantics).
         * @throws System::ArgumentException if @p vertexDeclaration has no elements, a
         *         non-positive stride, or an element reaching past its own stride.
         * @throws System::ArgumentOutOfRangeException if an offset or count is negative, if
         *         @p primitiveCount is not positive, or if a source byte range overflows.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const void* vertexData, int vertexOffset, int numVertices,
                                       const std::uint32_t* indexData, int indexOffset, int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);

        /**
         * @brief Draws indexed VertexPositionColor primitives with an explicit VertexDeclaration
         *        (32-bit indices).
         *
         * The vertex values are converted into the GPU stream @p vertexDeclaration describes, so
         * no object-model byte ever reaches a renderer.
         *
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index; indices are relative to it.
         * @param numVertices        Number of vertices to convert.
         * @param indexData          Pointer to the 32-bit index array.
         * @param indexOffset        Starting index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionColor* vertexData, int vertexOffset,
                                       int numVertices, const std::uint32_t* indexData,
                                       int indexOffset, int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws indexed VertexPositionTexture primitives with an explicit
         *        VertexDeclaration (32-bit indices).
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index; indices are relative to it.
         * @param numVertices        Number of vertices to convert.
         * @param indexData          Pointer to the 32-bit index array.
         * @param indexOffset        Starting index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionTexture* vertexData, int vertexOffset,
                                       int numVertices, const std::uint32_t* indexData,
                                       int indexOffset, int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws indexed VertexPositionColorTexture primitives with an explicit
         *        VertexDeclaration (32-bit indices).
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index; indices are relative to it.
         * @param numVertices        Number of vertices to convert.
         * @param indexData          Pointer to the 32-bit index array.
         * @param indexOffset        Starting index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionColorTexture* vertexData,
                                       int vertexOffset, int numVertices,
                                       const std::uint32_t* indexData, int indexOffset,
                                       int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);
        /**
         * @brief Draws indexed VertexPositionNormalTexture primitives with an explicit
         *        VertexDeclaration (32-bit indices).
         * @param primitiveType      The type of primitive to draw.
         * @param vertexData         Pointer to the vertex array.
         * @param vertexOffset       Starting vertex index; indices are relative to it.
         * @param numVertices        Number of vertices to convert.
         * @param indexData          Pointer to the 32-bit index array.
         * @param indexOffset        Starting index.
         * @param primitiveCount     Number of primitives to draw.
         * @param vertexDeclaration  Describes the layout and stride of each vertex.
         * @throws System::ArgumentException if @p vertexDeclaration's stride is not this vertex
         *         type's GPU stream stride, or an element reaches past that stride.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const VertexPositionNormalTexture* vertexData,
                                       int vertexOffset, int numVertices,
                                       const std::uint32_t* indexData, int indexOffset,
                                       int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration);

        // --- CNAEXT helpers (not in XNA 4.0) ---

        /**
         * @brief Returns the number of vertices required to draw @p primitiveCount primitives
         *        of the given @p primitiveType.
         *
         * Mirrors the FNA-internal PrimitiveVerts() helper. Exposed here so that tests and
         * calling code can validate vertex-array sizes without performing a draw call.
         *
         * @param primitiveType  The primitive topology.
         * @param primitiveCount Number of primitives.
         * @return Total vertex count.
         * @throws System::InvalidOperationException if @p primitiveType is unrecognised.
         */
        CNAEXT static int PrimitiveVerts(PrimitiveType primitiveType, int primitiveCount);

        /**
         * @brief Fires ResourceCreated for the given resource.
         *
         * Called by GraphicsResource constructors when a device is attached.
         * @param resource The newly created resource.
         */
        CNAEXT void OnResourceCreated(System::Object* resource);

        /**
         * @brief Fires ResourceDestroyed with the given name and tag.
         *
         * Called by GraphicsResource::Dispose before the resource is marked disposed.
         * @param name The Name of the resource being destroyed.
         * @param tag  The Tag of the resource being destroyed (may be nullptr).
         */
        CNAEXT void OnResourceDestroyed(const std::string& name, System::Object* tag);

        /**
         * @brief Registers a resource for tracking.
         *
         * Called by GraphicsResource constructor. The device will dispose registered
         * resources before its own renderer is destroyed, preventing use-after-free.
         * @param resource The resource to track.
         */
        CNAEXT void AddResourceReference(GraphicsResource* resource);

        /**
         * @brief Unregisters a previously tracked resource.
         *
         * Called by GraphicsResource::Dispose(bool). Safe to call during device disposal
         * (the tracking list is cleared before iteration).
         * @param resource The resource to remove.
         */
        CNAEXT void RemoveResourceReference(GraphicsResource* resource);

        /**
         * @brief Returns the number of live resources currently tracked by this device.
         *
         * Intended for debug and test use only.
         * @return Count of registered, not-yet-disposed resources.
         */
        CNAEXT [[nodiscard]] std::size_t GetTrackedResourceCount() const { return resources_.size(); }

        /** @brief Enables or disables depth testing. */
        CNAEXT void SetDepthTestEnabled(bool enabled);
        /** @brief Enables or disables blending. */
        CNAEXT void SetBlendEnabled(bool enabled);
        /** @brief Enables or disables depth writes. */
        CNAEXT void SetDepthWriteEnabled(bool enabled);
        /**
         * @brief Task/plan_dx9.md D9-103 finding: real XNA fixes GraphicsProfile at device
         * construction (the public GraphicsDevice.GraphicsProfile property is read-only), but
         * CNA's own GraphicsDeviceManager architecture eagerly default-constructs Game's
         * GraphicsDevice_ member (hardcoded GraphicsProfile::Reach) BEFORE GraphicsDeviceManager
         * -- and therefore before a game's own GraphicsDeviceManager.GraphicsProfile request --
         * even exists. Without this, GraphicsDeviceManager::CreateDevice()/ApplyChanges() has no
         * way to make the FIRST real device creation honor a non-Reach request at all: a game's
         * `graphics.GraphicsProfile = GraphicsProfile.HiDef; graphics.ApplyChanges();` would
         * silently keep using Reach. Called once, internally, from
         * GraphicsDeviceManager::applyToExistingRenderer() right before Reset() -- not exposed as
         * a general public runtime profile-switch (real XNA has none either).
         */
        CNAEXT void SetGraphicsProfileEXT(GraphicsProfile profile);
        /**
         * @brief Disables GL context-loss recovery (CPU shadow copies + ResourceRegistry).
         *
         * Must be called before the device is initialized. Safe on desktop where
         * context loss never occurs; saves approximately one copy of texture RAM per loaded texture.
         *
         * @param enabled Pass false to disable context recovery.
         */
        CNAEXT void SetContextRecoveryEnabled(bool enabled);

        /**
         * @brief Inserts a named debug marker at the current point in the GPU command stream.
         *
         * On Vulkan with VK_EXT_debug_utils, calls vkCmdInsertDebugUtilsLabelEXT so the
         * label appears in GPU profilers (RenderDoc, NVIDIA Nsight, etc.).
         * On all other renderers this is a no-op.
         *
         * @param marker The label string to insert.
         */
        CNAEXT void SetStringMarkerEXT(const std::string& marker);

        /** @brief Returns a reference to the active graphics renderer. */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::IGraphicsRenderer& GetRenderer() const;

        /**
         * @brief Returns which graphics renderer THIS DEVICE is using.
         *
         * plan_runtimerenderer.md RTR-P7-3. This used to be `constexpr`, returning
         * CNA::getCurrentGraphicsRendererType() and ignoring `this` entirely -- correct while a
         * build could contain only one renderer, and wrong the moment it can contain several: it
         * would report the build's default even on a device that resolved to something else.
         *
         * The `constexpr` had to go with it. That is a deliberate, documented deviation: a
         * compile-time answer cannot describe a runtime choice. A caller that genuinely wants the
         * build's compile-time identity still has CNA::getCurrentGraphicsRendererType(), which
         * remains a constant expression.
         *
         * @return The renderer identity backing this device.
         */
        CNAEXT [[nodiscard]] CNA::GraphicsRendererType GetGraphicsRendererType() const;

        /**
         * @brief Returns the human-readable name of the renderer THIS DEVICE is using.
         *
         * Matches the CNA_GRAPHICS_RENDERER spelling exactly (e.g. "OPENGLES3", "DIRECTX9"). See
         * GetGraphicsRendererType() for why this is no longer `constexpr`.
         *
         * @return The active renderer's name.
         */
        CNAEXT [[nodiscard]] std::string_view GetGraphicsRendererName() const;

        /**
         * @brief Returns whether the active renderer (and, for device-dependent entries, the
         * current runtime device/driver) supports the given CNA::GraphicsCapability.
         *
         * Query this before relying on a feature that isn't universally supported (e.g. 3D on
         * the 2D-only native/DIRECTX3/Canvas/GDI renderers), instead of calling it and handling the
         * resulting exception.
         *
         * @param capability The capability to check.
         * @return True if supported by the active renderer/device.
         */
        CNAEXT [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const;

        /**
         * @brief Returns the active renderer's real maximum single-axis texture dimension.
         *
         * REMED-CONTENT-001: query this before creating or accepting a texture of
         * caller-/file-supplied size, instead of trusting the value and letting the native
         * graphics API fail (or, on some renderers, corrupt memory) at creation time.
         *
         * @return The maximum width or height, in pixels, this device's renderer supports.
         */
        CNAEXT [[nodiscard]] int GetMaxTextureDimension() const;

        /**
         * @brief Configures how permanently unsupported 3D calls are handled by a 2D-only
         * graphics renderer.
         *
         * Throw is the default and preserves the renderer's established exceptions/null results.
         * WarnAndStub logs each unsupported operation once and substitutes a safe no-op or
         * null-object resource. This setting does not change SupportsCapability() results and
         * does not suppress argument, lifetime, driver, or implementation errors.
         */
        CNAEXT void SetUnsupported3DGraphicsCallBehavior(
            CNA::Unsupported3DGraphicsCallBehavior behavior);

        /** @brief Returns the active unsupported-3D-call policy (Throw by default). */
        CNAEXT [[nodiscard]] CNA::Unsupported3DGraphicsCallBehavior
        GetUnsupported3DGraphicsCallBehavior() const;

        /**
         * @brief Sets the currently active Effect for draw calls.
         *
         * Called automatically by Effect::Apply(). Accepts any Effect subclass;
         * the renderer uses virtual dispatch via FillGpuDrawParams() to obtain
         * shader parameters for the specific effect type.
         *
         * @param effect The effect to use, or nullptr.
         */
        CNAEXT void SetCurrentEffect(Effect* effect);

        /** @brief Returns the currently bound index buffer. */
        [[nodiscard]] const IndexBuffer* Indices() const;
        /**
         * @brief Binds an index buffer.
         * @param indexBuffer The index buffer to bind.
         */
        void Indices(const IndexBuffer* indexBuffer);

        /** @brief Returns the fully qualified .NET type name of this class. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Stores the given presentation parameters without triggering a full device reset.
         *
         * Used by GraphicsDeviceManager::applyToExistingRenderer so that callers can
         * read the current parameters back via getPresentationParametersProperty() after
         * applying preferred settings.
         *
         * @param pp The presentation parameters to store.
         */
        CNAEXT void SetPresentationParameters(const PresentationParameters& pp);

        /**
         * @brief Test-only: tears down and rebuilds the active graphics renderer (same window)
         * with a new PresentationParameters.MultiSampleCount, so a renderer-level property
         * (e.g. Vulkan's own backbuffer sampleCount_, picked once at renderer-construction time)
         * can be changed after the device already exists.
         *
         * This exists because GraphicsDeviceManager.PreferMultiSampling/ApplyChanges() does NOT
         * reach here: Game's own GraphicsDevice member is unconditionally default-constructed
         * (MultiSampleCount=0) before any derived Game subclass or GraphicsDeviceManager code can
         * run, and SetPresentationParameters() (the only thing GraphicsDeviceManager's existing
         * apply path calls) deliberately does not trigger a full device reset either — real
         * mid-game device reset/recreation (FNA's GraphicsDevice.Reset) is a separate, not-yet-
         * implemented feature (see the commented-out Reset() call in
         * GraphicsDeviceManager::applyToExistingRenderer). This method is a narrow, test-scoped
         * substitute for that missing feature: safe only when called before any GPU resources
         * (textures, buffers, render targets) have been created against the current renderer, since
         * it destroys and replaces renderer_ outright rather than performing a real, resource-
         * preserving device reset.
         *
         * @param multiSampleCount The new preferred MultiSampleCount to request from the renderer.
         */
        CNAEXT void RecreateRendererForMultiSampleCount(int multiSampleCount);

    private:
        // Borrowed from Game's enclosing platform (or the ambient lazy default for a bare
        // GraphicsDevice). The platform outlives both the window and this device.
        CNA::Platform::IPlatform* platform_;
        std::unique_ptr<CNA::Platform::IPlatformWindow> platformWindow_;
        /// MERGE (plan_runtimerenderer.md RTR-P5-12 x plan_platform.md PLAT-8): the platform wrapper
        /// already records whether destroying it destroys the underlying window, so this is no longer
        /// a lifetime flag. It survives as a policy flag: a caller-supplied window must not be torn
        /// down and rebuilt for a fallback candidate needing a different window kind, so the device
        /// still has to know which windows it is allowed to replace.
        bool ownsWindow_ = false;
        /// Whether this device currently holds ONE reference on the platform's video subsystem.
        ///
        /// The count it stands for is 0 or 1 and nothing else: every acquisition and release goes
        /// through setVideoSubsystemAcquired(), which is what makes that a structural property
        /// rather than a rule the call sites have to remember. See that method's own comment for
        /// why a plain bool beside two independent AcquireSubsystem() calls could not be right.
        bool videoSubsystemAcquired_;
        // Declared before renderer_: reverse destruction keeps presentation alive through the
        // raster renderer's final destructor calls.
        std::unique_ptr<CNA::Platform::IPlatformSurfacePresenter> surfacePresenter_;
        std::unique_ptr<CNA::Internal::Renderers::IGraphicsRenderer> renderer_;
        /// plan_runtimerenderer.md RTR-P5: the descriptor this device actually resolved to, which
        /// may differ from the selected one when a fallback chain substituted another renderer.
        /// Pinned at construction so a later reconstruction (Reset, multisample change) rebuilds
        /// the SAME renderer rather than re-running resolution against a changed environment.
        const CNA::Internal::Renderers::GraphicsRendererDescriptor* activeDescriptor_ = nullptr;
        /// The window kind window_ was created for, so a fallback candidate needing a different one
        /// can be detected before it is handed an incompatible window (design decision 8).
        CNA::Internal::Renderers::RendererWindowKind activeWindowKind_ =
            CNA::Internal::Renderers::RendererWindowKind::None;
        bool rendererStartupNameLogged_ = false;
        Viewport viewport_;
        const VertexBuffer* currentVertexBuffer_;
        const IndexBuffer* currentIndexBuffer_;
        Effect* currentEffect_;
        int virtualWidth_;
        int virtualHeight_;
        int lastKnownViewportWidth_ = -1;
        int lastKnownViewportHeight_ = -1;
        // Tracks the PHYSICAL viewport rectangle separately from the logical width/height above:
        // under CnaPresentationMode::Letterbox/Overscan, the logical size can stay fixed (the
        // game's own virtual resolution never changes) while the physical rectangle
        // GetDefaultViewportRect() returns still needs to be re-applied after a window resize --
        // comparing logical size alone would silently skip that re-application.
        int lastKnownViewportPhysX_ = -1;
        int lastKnownViewportPhysY_ = -1;
        int lastKnownViewportPhysWidth_ = -1;
        int lastKnownViewportPhysHeight_ = -1;
        bool contextRecoveryEnabled_ = true;
        GraphicsAdapter* adapter_;
        GraphicsProfile graphicsProfile_;
        PresentationParameters presentationParameters_;
        bool isDisposed_;
        /// plan_dx9.md D9-34: tracks the real device-lifecycle state reported by a renderer via
        /// GraphicsRendererCreateArgs::deviceEventCallback (RendererDeviceEvent::Lost -> Lost,
        /// Resetting -> NotReset, Reset -> Normal). Every renderer except D3D9 never calls that
        /// callback, so this stays Normal there, matching the pre-existing hardcoded behavior.
        GraphicsDeviceStatus deviceStatus_ = GraphicsDeviceStatus::Normal;

        BlendState blendState_;
        DepthStencilState depthStencilState_;
        RasterizerState rasterizerState_;
        Rectangle scissorRectangle_;
        Color blendFactor_;
        int multiSampleMask_ = -1;
        int referenceStencil_ = 0;

        TextureCollection textures_;
        SamplerStateCollection samplerStates_;
        TextureCollection vertexTextures_;
        SamplerStateCollection vertexSamplerStates_;

        std::vector<RenderTargetBinding> currentRenderTargets_;
        bool renderTargetBound_ = false;
        std::vector<VertexBufferBinding> currentVertexBuffers_;
        std::vector<GraphicsResource*> resources_;

        // Reusable byte buffers for DrawUserPrimitives / DrawUserIndexedPrimitives staging,
        // avoiding a heap allocation on every draw call once capacity has grown to fit.
        std::vector<std::uint8_t> userVertexScratch_;
        std::vector<std::uint8_t> userIndexScratch_;
        [[nodiscard]] void* AcquireUserVertexScratch(std::size_t bytes);
        [[nodiscard]] void* AcquireUserIndexScratch(std::size_t bytes);

        // REMED-GFX-200: the per-vertex geometry stream's VertexBufferBinding.VertexOffset, in
        // vertex ELEMENTS. Binding 0 is that stream on every draw route -- SetVertexBuffer() and
        // SetVertexBuffers() both make it so, and Draw*PrimitivesEx hand the renderer exactly one
        // per-vertex stream -- so a later binding never supplies this offset. Returns 0 when no
        // binding describes the bound buffer, which is the only value that leaves a
        // no-VertexBufferBinding draw byte-identical to its pre-REMED-GFX-200 behavior.
        [[nodiscard]] int CurrentVertexBufferOffset() const;

        // REMED-GFX-201: the element offset shared by every per-vertex stream of the current
        // binding set -- the smallest VertexBufferBinding.VertexOffset among them, 0 when none.
        // The ordinary routes fold this into vertexStart/baseVertex, the element-unit channel each
        // renderer already multiplies by *each stream's own* stride exactly once, and hand every
        // stream the non-negative remainder. With one stream the fold is the whole offset and the
        // remainder is 0, which is byte-for-byte REMED-GFX-200's behaviour; with several it is the
        // only split that keeps every per-stream native byte offset non-negative while still
        // giving each stream `binding.VertexOffset + vertexStart` of its own elements.
        [[nodiscard]] int FoldedVertexStreamOffset() const;

        // REMED-GFX-201: copies every active declared VertexBufferBinding into `p.vertexStreams`,
        // in public slot order, and computes `p.combinedVertexStride`. `foldedOffset` is subtracted
        // from each per-vertex stream's VertexOffset (see FoldedVertexStreamOffset above). Captured
        // by value: a deferred renderer replays a draw long after currentVertexBuffers_ has been
        // reassigned. On ordinary draws, REMED-GFX-233's exact one-buffer empty-declaration
        // compatibility shape leaves the stream list empty so the named renderer buffer's uploaded
        // stride remains authoritative; instanced submission retains its complete stream contract.
        void FillVertexStreamBindings(
            CNA::Internal::Renderers::GpuDrawParams& p, int foldedOffset,
            bool allowLegacyEmptyDeclarationFallback) const;

        // REMED-GFX-201/202: rejects a draw whose binding set is wider than the running renderer can
        // express -- more than one per-vertex stream, more than one per-instance stream, or more
        // per-vertex streams than its native input-slot ceiling. Deterministic and before native
        // submission: a renderer that has not been taught to re-slot its stride-derived input
        // elements across several bindings would otherwise render from a subset of them, which
        // looks like a correct draw of the wrong data. The classic shapes -- one per-vertex stream,
        // and one per-vertex plus one per-instance stream -- never reach any of these checks.
        void ValidateVertexStreamCapability(
            const CNA::Internal::Renderers::GpuDrawParams& p) const;

        // REMED-GFX-201: REMED-GFX-113's range gate widened from stream 0 to every per-vertex
        // stream. `startElement` is vertexStart for the non-indexed route and
        // baseVertex + minVertexIndex for the indexed one; `elementCount` is the topology-derived
        // vertex count or numVertices respectively. A stream too short for the requested window is
        // rejected here even when stream 0 is long enough, naming the offending slot.
        void ValidateVertexStreamRanges(
            const CNA::Internal::Renderers::GpuDrawParams& p,
            int startElement,
            int elementCount,
            const char* parameterName,
            const std::string& parameterValue) const;

        /** Clears a selected effect only when the exact resource is being disposed. */
        void ClearCurrentEffectIf(const Effect* effect) noexcept;

        // REMED-GFX-202: REMED-GFX-118's instance-range gate widened from the first per-instance
        // binding to EVERY one of them. `instanceCount` instances consume
        // `1 + (instanceCount - 1) / InstanceFrequency` records of each per-instance stream,
        // beginning at that stream's own VertexOffset -- all in vertex ELEMENTS of that stream's own
        // declaration, never bytes. A stream too short is rejected here, naming the offending slot,
        // even when another per-instance stream is long enough.
        void ValidateInstanceStreamRanges(
            const CNA::Internal::Renderers::GpuDrawParams& p,
            int instanceCount) const;

        // The one object-to-GPU-stream conversion behind every built-in vertex type's explicit
        // VertexDeclaration draw: the values are packed into the stream that type's declaration
        // describes, and the packed stream is then consumed by the raw overloads exactly like a
        // caller-packed one, so no path can apply the conversion twice.
        template <typename VertexT>
        void DrawUserPrimitivesFromObjects(PrimitiveType primitiveType, const VertexT* vertexData,
                                           int vertexOffset, int primitiveCount,
                                           const VertexDeclaration& vertexDeclaration);
        template <typename VertexT, typename IndexT>
        void DrawUserIndexedPrimitivesFromObjects(
            PrimitiveType primitiveType, const VertexT* vertexData, int vertexOffset,
            int numVertices, const IndexT* indexData, int indexOffset, int primitiveCount,
            const VertexDeclaration& vertexDeclaration);

        [[nodiscard]] std::uintptr_t GetWindowHandleInternal() const;
        [[nodiscard]] CNA::Platform::IPlatformWindow* GetPlatformWindowInternal() const;

        void createOrAttachWindow();
        void createRenderer();

        /**
         * @brief Brings this device's platform video-subsystem reference to @p acquired.
         *
         * The single owner of that reference. Idempotent in both directions, so this device holds
         * either zero or one reference no matter how many times, or from how many places, the
         * question is asked -- which is the whole point: `AcquireSubsystem()` is reference
         * counted, so a second acquisition that is never matched by a second release leaves the
         * video subsystem up for the rest of the process.
         *
         * @param acquired True to hold a reference, false to give it up.
         */
        void setVideoSubsystemAcquired(bool acquired);

        /**
         * @brief Resolves which renderer this device uses, honouring any configured fallback chain.
         *
         * plan_runtimerenderer.md design decisions 6 and 7. Creates the window and the renderer
         * together, because the window's flags depend on which renderer is being attempted: a
         * candidate that needs a different window kind cannot reuse the previous candidate's
         * window. Runs once, from the constructor. Reconstruction paths use createRenderer()
         * directly and never re-resolve.
         *
         * @throws System::InvalidOperationException when no candidate could be created.
         */
        void resolveRenderer();

        /**
         * @brief Destroys this device's SDL window when it owns one, leaving window_ null.
         *
         * Used between fallback attempts that need different window kinds.
         */
        void discardOwnedWindow();
        void destroyNativeResources();
        void UpdateViewportFromWindow();
        void SetVirtualResolution(int width, int height);
        void SetPresentationMode(int mode);
        void applyPresentationParametersToWindow();
        void applySamplerStatesToRenderer();

        /**
         * @brief Resets Viewport and ScissorRectangle to (0, 0, width, height).
         *
         * Matches FNA's GraphicsDevice.SetRenderTargets: every call to
         * SetRenderTarget/SetRenderTargets resets Viewport/ScissorRectangle to the new
         * render target's dimensions (or the backbuffer's, when unbinding) — a game's
         * previously-set Viewport/ScissorRectangle never survives a render target switch.
         *
         * @param width  New viewport/scissor width in pixels.
         * @param height New viewport/scissor height in pixels.
         */
        void ResetViewportAndScissorForRenderTarget(int width, int height);

        friend class Texture2D;
        friend class RenderTargetCube;
        friend class ShaderEffect;
        friend class Effect;
        friend class SpriteBatch;
        friend class Microsoft::Xna::Framework::GameWindow;
        friend class Microsoft::Xna::Framework::GraphicsDeviceManager;
        friend class Microsoft::Xna::Framework::Game;
    };
}
