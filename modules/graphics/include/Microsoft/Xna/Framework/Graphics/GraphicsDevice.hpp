// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
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
#include "CNA/DisplayColorSpace.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "CNA/Unsupported3DGraphicsCallBehavior.hpp"

namespace CNA::Platform
{
    class IPlatform;
    class IPlatformSurfacePresenter;
    class IPlatformWindow;
}

namespace CNA::Internal
{
    class Texture2DArrayGraphicsDeviceTestPeer;
    class StorageTexture2DGraphicsDeviceTestPeer;
    class StorageBufferGraphicsDeviceTestPeer;
    class GraphicsDevicePlatformWindowTestPeer;
    class EngineLayerFloatFilteringScope;
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
    class Texture;
}

namespace Microsoft::Xna::Framework::Content
{
    class ContentReader;
}

namespace CNA::Internal::Renderers
{
    class IGraphicsRenderer;
    class IRendererThreadContextLease;
    class IStorageBufferRenderer;

    // Mirrors the definition in CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp. Declared here
    // rather than including that header, which this one deliberately does not pull into every
    // consumer of the public GraphicsDevice API; the underlying type is fixed so the two agree.
    enum class ShaderDialectEXT : int;
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
        CNAEXT GraphicsDevice();

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
        /**
         * @brief The size of the surface the game actually draws into, in logical units.
         *
         * @note CNAEXT — CNA extension, not XNA API. Under a virtual resolution this is NOT the
         * window: `GraphicsDevice` letterboxes the logical surface inside the drawable, so the two
         * disagree whenever bars exist. Anything that reasons about the shape of what the game
         * draws — `GameWindow`'s orientation, for one — has to ask this rather than the window.
         *
         * @param width  Receives the logical width.
         * @param height Receives the logical height.
         * @return true when a renderer answered with usable dimensions.
         */
        CNAEXT [[nodiscard]] bool GetLogicalSizeEXT(int& width, int& height) const;

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
         * @throws System::NotSupportedException if the state uses a blend combination forbidden
         *         by the active graphics profile.
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
        CNAEXT void Clear(float r, float g, float b, float a);
        /**
         * @brief Clears the specified buffers.
         * @param options Flags indicating which buffers to clear.
         * @param color   Color value for the color buffer.
         * @param depth   Depth value for the depth buffer (0–1).
         * @param stencil Stencil value for the stencil buffer.
         */
        void Clear(ClearOptions options, const Color& color, float depth, int stencil);
        /**
         * @brief Clears the specified buffers after converting a floating-point color to Color.
         *
         * @param options Flags indicating which buffers to clear.
         * @param color   Floating-point color value converted through XNA's packed Color rules.
         * @param depth   Depth value for the depth buffer (0–1).
         * @param stencil Stencil value for the stencil buffer.
         */
        void Clear(ClearOptions options, const Vector4& color, float depth, int stencil);
        /**
         * @brief Clears the color and depth buffers.
         * @param color Color value for the color buffer.
         * @param depth Depth value for the depth buffer (0–1).
         */
        CNAEXT void Clear(const Color& color, float depth);

        /** @brief Presents the rendered frame to the display. */
        void Present();

        /**
         * @brief Presents part of the backbuffer, optionally to another window.
         *
         * The documented three-argument Present. A null source rectangle presents the whole
         * backbuffer and a null destination rectangle fills the whole client area, each clipped to
         * that surface when it is larger, and a zero @p overrideWindowHandle means the device's own
         * window -- which is what makes `Present(std::nullopt, std::nullopt, 0)` identical to
         * `Present()`, and CNA takes exactly that path for it.
         *
         * Anything else is a request CNA's renderers do not all have an equivalent for. XNA's maps
         * onto Direct3D 9's `Present`, which takes a source rectangle, a destination rectangle and a
         * window handle natively; CNA's renderers present through their own swap chains or through a
         * platform surface presenter, and neither contract carries a destination rectangle or a
         * foreign window. So the request is passed to the renderer, which honours what it can and
         * otherwise refuses -- and the refusal is explicit rather than the request being dropped,
         * because a present that silently ignored its rectangles would show the whole frame while
         * reporting that it had shown part of it.
         *
         * What is honoured today: HEADLESS accepts any request, because it presents nothing at all
         * and so has nothing to get wrong; SOFTWARE honours a source rectangle, presenting that
         * sub-rectangle of its backbuffer without copying it. No renderer honours a destination
         * rectangle or a foreign window.
         *
         * @param sourceRectangle The part of the backbuffer to present, or nullopt for all of it.
         * @param destinationRectangle Where in the window's client area to present it, in client
         *        pixels, or nullopt for the whole client area.
         * @param overrideWindowHandle The window to present to, or 0 for the device's own window.
         * @throws System::ObjectDisposedException if the device has been disposed.
         * @throws System::InvalidOperationException if render targets are bound.
         * @throws System::ArgumentException if a supplied rectangle has a non-positive extent, or
         *         lies entirely outside the surface it applies to, so that clipping leaves nothing.
         * @throws System::NotSupportedException if the active renderer cannot honour the request.
         */
        void Present(const std::optional<Rectangle>& sourceRectangle,
                     const std::optional<Rectangle>& destinationRectangle,
                     std::uintptr_t overrideWindowHandle);

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
        CNAEXT void Reset(const PresentationParameters& presentationParameters, GraphicsAdapter* adapter);

        /** @brief Releases all resources held by this device. */
        void Dispose() override;

        // --- Back-buffer readback ---
        /**
         * @brief Copies all back-buffer pixels into the provided Color array.
         * @param data         Output array to receive pixel data.
         * @param elementCount Number of Color elements to read.
         * @throws System::NotSupportedException under the Reach graphics profile.
         * @throws System::ArgumentNullException if @p data is null.
         * @throws System::ArgumentOutOfRangeException if @p elementCount is not positive.
         * @throws System::ArgumentException if @p elementCount does not equal the back-buffer pixel count.
         * @throws System::InvalidOperationException if a render target is active.
         */
        void GetBackBufferData(Color* data, int elementCount);
        /**
         * @brief Copies back-buffer pixels into the provided Color array starting at an offset.
         * @param data         Output array to receive pixel data.
         * @param startIndex   First element index in @p data to write to.
         * @param elementCount Number of Color elements to read.
         * @throws System::NotSupportedException under the Reach graphics profile.
         * @throws System::ArgumentNullException if @p data is null.
         * @throws System::ArgumentOutOfRangeException if the destination range is invalid.
         * @throws System::ArgumentException if @p elementCount does not equal the back-buffer pixel count.
         * @throws System::InvalidOperationException if a render target is active.
         */
        void GetBackBufferData(Color* data, int startIndex, int elementCount);
        /**
         * @brief Copies a rectangular region of the back buffer into the provided Color array.
         * @param rect         Source rectangle, or nullptr for the full back buffer.
         * @param data         Output array to receive pixel data.
         * @param startIndex   First element index in @p data to write to.
         * @param elementCount Number of Color elements to read.
         * @throws System::NotSupportedException under the Reach graphics profile.
         * @throws System::ArgumentNullException if @p data is null.
         * @throws System::ArgumentOutOfRangeException if the destination range is invalid.
         * @throws System::ArgumentException if @p rect is invalid or @p elementCount does not match it.
         * @throws System::InvalidOperationException if a render target is active.
         */
        void GetBackBufferData(const Rectangle* rect, Color* data, int startIndex, int elementCount);

        /**
         * @brief Copies the complete back buffer into arbitrary trivially-copyable elements.
         *
         * @tparam T Destination value type whose size divides the back-buffer texel size.
         * @param data Output elements.
         * @param elementCount Exact element count whose bytes equal the complete back buffer.
         */
        template<typename T> requires std::is_trivially_copyable_v<T>
        void GetBackBufferData(T* data, int elementCount)
        {
            GetBackBufferData(data, 0, elementCount);
        }

        /**
         * @brief Copies the complete back buffer into a destination element window.
         *
         * @tparam T Destination value type whose size divides the back-buffer texel size.
         * @param data Output elements.
         * @param startIndex First destination element to write.
         * @param elementCount Exact element count whose bytes equal the complete back buffer.
         */
        template<typename T> requires std::is_trivially_copyable_v<T>
        void GetBackBufferData(T* data, int startIndex, int elementCount)
        {
            GetBackBufferData(nullptr, data, startIndex, elementCount);
        }

        /**
         * @brief Copies a back-buffer rectangle into arbitrary trivially-copyable elements.
         *
         * @tparam T Destination value type whose size divides the back-buffer texel size.
         * @param rect Source rectangle, or null for the complete back buffer.
         * @param data Output elements.
         * @param startIndex First destination element to write.
         * @param elementCount Exact element count whose bytes equal the requested rectangle.
         */
        template<typename T> requires std::is_trivially_copyable_v<T>
        void GetBackBufferData(const Rectangle* rect, T* data,
                               int startIndex, int elementCount)
        {
            GetBackBufferDataCore(rect, data, startIndex, elementCount, sizeof(T), false);
        }

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
         * @param vertexBuffer The vertex buffer to bind, or nullptr to unbind.
         * @param vertexOffset Offset (in vertices) into the buffer; ignored when unbinding.
         */
        void SetVertexBuffer(const VertexBuffer* vertexBuffer, int vertexOffset);
        /**
         * @brief Binds multiple vertex buffers simultaneously.
         * @param vertexBuffers Vector of non-null vertex buffer bindings to apply. An empty
         *        vector is valid and unbinds all vertex buffers.
         * @throws System::ArgumentException if any binding contains a null vertex buffer.
         * @throws System::NotSupportedException if more than 16 bindings are supplied.
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
        CNAEXT void SetIndexBuffer(const IndexBuffer* indexBuffer);
        /**
         * @brief Returns the currently bound vertex buffer (first slot).
         * @return Pointer to the bound vertex buffer, or nullptr.
         */
        CNAEXT [[nodiscard]] const VertexBuffer* GetVertexBuffer() const;
        /**
         * @brief Returns the currently bound index buffer.
         * @return Pointer to the bound index buffer, or nullptr.
         */
        CNAEXT [[nodiscard]] const IndexBuffer* GetIndexBuffer() const;

        // --- Draw ---
        /**
         * @brief Draws non-indexed primitives from the bound vertex buffer.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexStart    Index of the first vertex to draw.
         * @param primitiveCount Number of primitives to draw.
         * @throws System::InvalidOperationException if no effect or vertex buffer is bound.
         * @throws System::NotSupportedException if @p primitiveCount exceeds the active graphics
         *         profile limit.
         */
        void DrawPrimitives(PrimitiveType primitiveType, int vertexStart, int primitiveCount);
        /**
         * @brief Draws indexed primitives from the bound vertex and index buffers.
         * @param primitiveType  The type of primitive to draw.
         * @param baseVertex     Signed offset added to each index before reading from the vertex
         *                       buffer. A negative value is valid when the resulting indices and
         *                       declared vertex window remain in range.
         * @param minVertexIndex Minimum vertex index among the referenced vertices.
         * @param numVertices    Number of vertices referenced.
         * @param startIndex     Location in the index buffer to start reading.
         * @param primitiveCount Number of primitives to draw.
         * @throws System::InvalidOperationException if no effect, index buffer, or vertex buffer
         *         is bound.
         * @throws System::NotSupportedException if @p primitiveCount exceeds the active graphics
         *         profile limit.
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
         * @param baseVertex     Signed offset added to each index before reading from the vertex
         *                       buffer. A negative value is valid when the resulting indices and
         *                       declared vertex window remain in range.
         * @param minVertexIndex Minimum vertex index among the referenced vertices.
         * @param numVertices    Number of vertices referenced.
         * @param startIndex     Location in the index buffer to start reading.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances to draw.
         * @throws System::InvalidOperationException if no effect, index buffer, or vertex buffer
         *         is bound.
         * @throws System::ArgumentOutOfRangeException if @p primitiveCount, @p numVertices or
         *         @p instanceCount is not positive; if @p minVertexIndex or @p startIndex is
         *         negative; if the requested index range leaves the bound index buffer; if the
         *         declared vertex range leaves the bound vertex buffer after its binding offset
         *         and @p baseVertex; or if the required per-instance element range leaves its
         *         bound buffer after applying binding offset and instance frequency.
         * @throws System::NotSupportedException if @p primitiveCount or @p instanceCount exceeds
         *         the active graphics profile limit.
         */
        void DrawInstancedPrimitives(PrimitiveType primitiveType,
                                     int baseVertex, int minVertexIndex,
                                     int numVertices, int startIndex,
                                     int primitiveCount, int instanceCount);

        /**
         * @brief Draws an indexed instance range beginning at @p firstInstance.
         *
         * This CNA extension preserves the complete XNA `DrawInstancedPrimitives` geometry,
         * binding and effect contract. Only the first logical instance changes; renderers never
         * expose a native command or native buffer handle through this API.
         *
         * @param primitiveType  The type of primitive to draw.
         * @param baseVertex     Offset added to each decoded index.
         * @param minVertexIndex Minimum referenced vertex index.
         * @param numVertices    Number of vertices in the declared referenced window.
         * @param startIndex     First index element to read.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances to draw.
         * @param firstInstance  First logical instance, which must be non-negative.
         * @throws System::NotSupportedException if the renderer does not report
         *         `CNA::RendererFeature::BaseInstanceDrawing`.
         * @throws System::ArgumentOutOfRangeException for the same invalid ranges as
         *         `DrawInstancedPrimitives`, or if @p firstInstance is negative or makes the
         *         required instance range exceed a bound per-instance stream.
         */
        CNAEXT void DrawInstancedPrimitivesBaseInstanceEXT(
            PrimitiveType primitiveType, int baseVertex, int minVertexIndex,
            int numVertices, int startIndex, int primitiveCount,
            int instanceCount, int firstInstance);

        /**
         * @brief Draws with the counts and offsets read out of a GPU buffer rather than passed in.
         *
         * plans/plan_modern.md `MOD-2090`. @p argumentBuffer holds a `CNA::IndirectDrawArguments` at
         * @p argumentByteOffset, written by whatever produced it -- usually a compute shader, in
         * which case the numbers never reach the CPU at all. That is the point: reading them back
         * to pass them as arguments is a pipeline stall, not merely a copy.
         *
         * **The range checks every other draw route performs are impossible here**, because the
         * range is in GPU memory when the draw is issued. A count that leaves the bound buffer is
         * undefined behaviour rather than an exception, and the shader that wrote it owns that
         * obligation. What can still be checked is: a vertex buffer is bound, an effect is applied,
         * the argument buffer is real, and the arguments lie inside it.
         *
         * @param primitiveType      The topology; the buffer supplies counts, never this.
         * @param argumentBuffer     The buffer holding the arguments.
         * @param argumentByteOffset Where in it they start, in bytes. Must be a multiple of 4.
         * @throws System::NotSupportedException If the renderer does not report
         *         `CNA::GraphicsCapability::IndirectDraw`, naming it, or if the buffer lacks the
         *         declared indirect-argument usage.
         * @throws std::runtime_error If no vertex buffer or no effect is bound.
         * @throws System::ArgumentOutOfRangeException If @p argumentByteOffset is negative, not a
         *         multiple of 4, or leaves no room for the arguments in @p argumentBuffer.
         */
        CNAEXT void DrawPrimitivesIndirectEXT(
            PrimitiveType primitiveType,
            const CNA::Internal::Renderers::IStorageBufferRenderer& argumentBuffer,
            int argumentByteOffset);

        /**
         * @brief Indexed counterpart of @ref DrawPrimitivesIndirectEXT.
         *
         * @param primitiveType      The topology.
         * @param argumentBuffer     The buffer holding a `CNA::IndirectDrawIndexedArguments`.
         * @param argumentByteOffset Where in it they start, in bytes. Must be a multiple of 4.
         * @throws System::NotSupportedException If the renderer does not report
         *         `CNA::GraphicsCapability::IndirectDraw`, naming it, or if the buffer lacks the
         *         declared indirect-argument usage.
         * @throws std::runtime_error If no vertex buffer, index buffer or effect is bound.
         * @throws System::ArgumentOutOfRangeException If @p argumentByteOffset is negative, not a
         *         multiple of 4, or leaves no room for the arguments in @p argumentBuffer.
         */
        CNAEXT void DrawIndexedPrimitivesIndirectEXT(
            PrimitiveType primitiveType,
            const CNA::Internal::Renderers::IStorageBufferRenderer& argumentBuffer,
            int argumentByteOffset);
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
         * @throws System::InvalidOperationException if no effect has been applied.
         * @throws System::NotSupportedException if @p primitiveCount exceeds the active graphics
         *         profile limit.
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
         * @brief Draws non-indexed primitives from a user-supplied vertex array.
         *
         * The documented `DrawUserPrimitives<T>(PrimitiveType, T[], Int32, Int32)`. The array
         * carries its own length, so no separate count is passed, and the vertex type is retained
         * rather than erased to bytes -- which is what lets the layout come from the type.
         *
         * XNA constrains `T` to `struct, IVertexType` and reads the layout from
         * `VertexDeclarationCache<T>`; the static assertion below is that constraint, and the
         * layout comes from the type's own declaration the same way.
         *
         * @tparam TVertex The vertex structure type; must derive from IVertexType.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     The vertex array.
         * @param vertexOffset   Starting vertex index.
         * @param primitiveCount Number of primitives to draw.
         * @throws System::ArgumentOutOfRangeException if the requested range is outside
         *         @p vertexData.
         * @throws System::InvalidOperationException if no effect has been applied.
         * @throws System::NotSupportedException if @p TVertex is an IVertexType structure CNA
         *         cannot convert from its object form into a GPU stream -- the four built-in vertex
         *         structures are the ones it can, and a structure of its own should be a plain one
         *         drawn through the VertexDeclaration overload.
         */
        template <typename TVertex>
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const std::vector<TVertex>& vertexData,
                                int vertexOffset, int primitiveCount)
        {
            static_assert(std::is_base_of_v<IVertexType, TVertex>,
                          "XNA constrains this overload to a vertex structure implementing "
                          "IVertexType, because the layout comes from the type; pass a "
                          "VertexDeclaration explicitly for any other vertex type");
            CheckUserVertexArrayRange(vertexData.size(), vertexOffset, primitiveType,
                                      primitiveCount);
            DrawUserVertexArray<TVertex>(primitiveType, vertexData.data(), vertexOffset,
                                         primitiveCount, nullptr);
        }

        /**
         * @brief Draws non-indexed primitives from a user-supplied vertex array with an explicit
         *        VertexDeclaration.
         *
         * The documented
         * `DrawUserPrimitives<T>(PrimitiveType, T[], Int32, Int32, VertexDeclaration)`.
         *
         * A built-in vertex structure is converted from its object form into the stream
         * @p vertexDeclaration describes, exactly as the matching pointer overload does. A plain
         * vertex structure of the caller's own -- one that is not polymorphic, and so carries no
         * vtable pointer -- already *is* that stream, and its array is used directly.
         *
         * @tparam TVertex The vertex structure type.
         * @param primitiveType     The type of primitive to draw.
         * @param vertexData        The vertex array.
         * @param vertexOffset      Starting vertex index.
         * @param primitiveCount    Number of primitives to draw.
         * @param vertexDeclaration Describes the layout and stride of each vertex.
         * @throws System::ArgumentOutOfRangeException if the requested range is outside
         *         @p vertexData.
         * @throws System::ArgumentException if @p vertexDeclaration does not describe a stream.
         * @throws System::InvalidOperationException if no effect has been applied.
         * @throws System::NotSupportedException if @p TVertex is a polymorphic type CNA cannot
         *         convert, whose raw bytes are therefore not a vertex stream.
         */
        template <typename TVertex>
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const std::vector<TVertex>& vertexData,
                                int vertexOffset, int primitiveCount,
                                const VertexDeclaration& vertexDeclaration)
        {
            CheckUserVertexArrayRange(vertexData.size(), vertexOffset, primitiveType,
                                      primitiveCount);
            DrawUserVertexArray<TVertex>(primitiveType, vertexData.data(), vertexOffset,
                                         primitiveCount, &vertexDeclaration);
        }

        /**
         * @brief Draws indexed primitives from user-supplied raw vertex and index data.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the raw vertex array.
         * @param vertexOffset   Offset into @p vertexData (in vertices).
         * @param numVertices    Number of vertices in @p vertexData.
         * @param indexData      Pointer to the raw index data.
         * @param indexOffset    Offset into @p indexData (in indices).
         * @param primitiveCount Number of primitives to draw.
         * @throws System::InvalidOperationException if no effect has been applied.
         * @throws System::NotSupportedException if @p primitiveCount exceeds the active graphics
         *         profile limit.
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

        /**
         * @brief Draws indexed primitives from user-supplied vertex and 16-bit index arrays.
         *
         * The documented
         * `DrawUserIndexedPrimitives<T>(PrimitiveType, T[], Int32, Int32, Int16[], Int32, Int32)`.
         * Both arrays carry their own length, and the vertex type is retained, so the layout comes
         * from the type exactly as XNA's `VertexDeclarationCache<T>` provides it.
         *
         * @tparam TVertex The vertex structure type; must derive from IVertexType.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     The vertex array.
         * @param vertexOffset   Starting vertex index; indices are relative to it.
         * @param numVertices    Number of vertices the indices may address.
         * @param indexData      The 16-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         * @throws System::ArgumentOutOfRangeException if either requested range is outside its
         *         array.
         * @throws System::InvalidOperationException if no effect has been applied.
         * @throws System::NotSupportedException if @p TVertex is an IVertexType structure CNA
         *         cannot convert from its object form into a GPU stream.
         */
        template <typename TVertex>
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const std::vector<TVertex>& vertexData,
                                       int vertexOffset, int numVertices,
                                       const std::vector<std::int16_t>& indexData,
                                       int indexOffset, int primitiveCount)
        {
            static_assert(std::is_base_of_v<IVertexType, TVertex>,
                          "XNA constrains this overload to a vertex structure implementing "
                          "IVertexType, because the layout comes from the type; pass a "
                          "VertexDeclaration explicitly for any other vertex type");
            CheckUserIndexedArrayRanges(vertexData.size(), vertexOffset, numVertices,
                                        indexData.size(), indexOffset, primitiveType,
                                        primitiveCount);
            DrawUserIndexedVertexArray<TVertex, std::int16_t>(
                primitiveType, vertexData.data(), vertexOffset, numVertices, indexData.data(),
                indexOffset, primitiveCount, nullptr);
        }

        /**
         * @brief Draws indexed primitives from user-supplied vertex and 16-bit index arrays with
         *        an explicit VertexDeclaration.
         *
         * The documented `DrawUserIndexedPrimitives<T>(PrimitiveType, T[], Int32, Int32, Int16[],
         * Int32, Int32, VertexDeclaration)`. A built-in vertex structure is converted into the
         * stream @p vertexDeclaration describes; a plain structure of the caller's own already is
         * that stream.
         *
         * @tparam TVertex The vertex structure type.
         * @param primitiveType     The type of primitive to draw.
         * @param vertexData        The vertex array.
         * @param vertexOffset      Starting vertex index; indices are relative to it.
         * @param numVertices       Number of vertices the indices may address.
         * @param indexData         The 16-bit index array.
         * @param indexOffset       Starting index.
         * @param primitiveCount    Number of primitives to draw.
         * @param vertexDeclaration Describes the layout and stride of each vertex.
         * @throws System::ArgumentOutOfRangeException if either requested range is outside its
         *         array.
         * @throws System::ArgumentException if @p vertexDeclaration does not describe a stream.
         * @throws System::InvalidOperationException if no effect has been applied.
         * @throws System::NotSupportedException if @p TVertex is a polymorphic type CNA cannot
         *         convert, whose raw bytes are therefore not a vertex stream.
         */
        template <typename TVertex>
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const std::vector<TVertex>& vertexData,
                                       int vertexOffset, int numVertices,
                                       const std::vector<std::int16_t>& indexData,
                                       int indexOffset, int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration)
        {
            CheckUserIndexedArrayRanges(vertexData.size(), vertexOffset, numVertices,
                                        indexData.size(), indexOffset, primitiveType,
                                        primitiveCount);
            DrawUserIndexedVertexArray<TVertex, std::int16_t>(
                primitiveType, vertexData.data(), vertexOffset, numVertices, indexData.data(),
                indexOffset, primitiveCount, &vertexDeclaration);
        }

        /**
         * @brief Draws indexed primitives from user-supplied vertex and 32-bit index arrays.
         *
         * The documented
         * `DrawUserIndexedPrimitives<T>(PrimitiveType, T[], Int32, Int32, Int32[], Int32, Int32)`.
         * Both arrays carry their own length, and the vertex type is retained, so the layout comes
         * from the type exactly as XNA's `VertexDeclarationCache<T>` provides it.
         *
         * @tparam TVertex The vertex structure type; must derive from IVertexType.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     The vertex array.
         * @param vertexOffset   Starting vertex index; indices are relative to it.
         * @param numVertices    Number of vertices the indices may address.
         * @param indexData      The 32-bit index array.
         * @param indexOffset    Starting index.
         * @param primitiveCount Number of primitives to draw.
         * @throws System::ArgumentOutOfRangeException if either requested range is outside its
         *         array.
         * @throws System::InvalidOperationException if no effect has been applied.
         * @throws System::NotSupportedException if @p TVertex is an IVertexType structure CNA
         *         cannot convert from its object form into a GPU stream.
         */
        template <typename TVertex>
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const std::vector<TVertex>& vertexData,
                                       int vertexOffset, int numVertices,
                                       const std::vector<std::int32_t>& indexData,
                                       int indexOffset, int primitiveCount)
        {
            static_assert(std::is_base_of_v<IVertexType, TVertex>,
                          "XNA constrains this overload to a vertex structure implementing "
                          "IVertexType, because the layout comes from the type; pass a "
                          "VertexDeclaration explicitly for any other vertex type");
            CheckUserIndexedArrayRanges(vertexData.size(), vertexOffset, numVertices,
                                        indexData.size(), indexOffset, primitiveType,
                                        primitiveCount);
            DrawUserIndexedVertexArray<TVertex, std::int32_t>(
                primitiveType, vertexData.data(), vertexOffset, numVertices, indexData.data(),
                indexOffset, primitiveCount, nullptr);
        }

        /**
         * @brief Draws indexed primitives from user-supplied vertex and 32-bit index arrays with
         *        an explicit VertexDeclaration.
         *
         * The documented `DrawUserIndexedPrimitives<T>(PrimitiveType, T[], Int32, Int32, Int32[],
         * Int32, Int32, VertexDeclaration)`. A built-in vertex structure is converted into the
         * stream @p vertexDeclaration describes; a plain structure of the caller's own already is
         * that stream.
         *
         * @tparam TVertex The vertex structure type.
         * @param primitiveType     The type of primitive to draw.
         * @param vertexData        The vertex array.
         * @param vertexOffset      Starting vertex index; indices are relative to it.
         * @param numVertices       Number of vertices the indices may address.
         * @param indexData         The 32-bit index array.
         * @param indexOffset       Starting index.
         * @param primitiveCount    Number of primitives to draw.
         * @param vertexDeclaration Describes the layout and stride of each vertex.
         * @throws System::ArgumentOutOfRangeException if either requested range is outside its
         *         array.
         * @throws System::ArgumentException if @p vertexDeclaration does not describe a stream.
         * @throws System::InvalidOperationException if no effect has been applied.
         * @throws System::NotSupportedException if @p TVertex is a polymorphic type CNA cannot
         *         convert, whose raw bytes are therefore not a vertex stream.
         */
        template <typename TVertex>
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const std::vector<TVertex>& vertexData,
                                       int vertexOffset, int numVertices,
                                       const std::vector<std::int32_t>& indexData,
                                       int indexOffset, int primitiveCount,
                                       const VertexDeclaration& vertexDeclaration)
        {
            CheckUserIndexedArrayRanges(vertexData.size(), vertexOffset, numVertices,
                                        indexData.size(), indexOffset, primitiveType,
                                        primitiveCount);
            DrawUserIndexedVertexArray<TVertex, std::int32_t>(
                primitiveType, vertexData.data(), vertexOffset, numVertices, indexData.data(),
                indexOffset, primitiveCount, &vertexDeclaration);
        }

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
        /**
         * @brief Raises ContentLost on every tracked resource whose contents a reset destroys.
         *
         * Called only from the renderer-reported device-reset transition. See
         * plans/plan_cabi.md CABI-15 for why it is not called from GraphicsDevice::Reset.
         */
        CNAEXT void NotifyContentLostResourcesEXT();

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
         * @brief Task/plans/plan_dx9.md D9-103 finding: real XNA fixes GraphicsProfile at device
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
         * @brief Enables or disables native context/device-loss resource recovery.
         *
         * Must be called before the device is initialized. Disabling recovery avoids the renderer's
         * CPU shadows on platforms where context or device loss cannot occur.
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
         * @brief The shading dialect a custom `ShaderEffect`'s sources must be written in. CNAEXT.
         *
         * A `ShaderEffect` is renderer-specific source text, and until now an application had no
         * supported way to ask which dialect to supply -- it had to infer one from the build's
         * renderer identity, which is wrong in a multi-renderer build and meaningless for a
         * renderer that chooses its native API per process (IGL, LLGL, Diligent).
         *
         * @return The active renderer's dialect, or `ShaderDialectEXT::Unknown` where the renderer
         *         has not declared one -- which means "do not guess", not "no shaders".
         */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::ShaderDialectEXT GetShaderDialectEXT() const;

        /**
         * @brief Returns whether the active renderer consumes an explicit shader payload pair.
         *
         * plans/plan_modern.md `MOD-2210`. This is a live renderer query, not a mapping from the
         * renderer's name. Unknown, sentinel and invalid enum values return false.
         *
         * @param language The payload's declared source language or binary format.
         * @param stage The programmable stage implemented by the payload.
         * @return True only when the active renderer's implemented path consumes that exact pair.
         */
        CNAEXT [[nodiscard]] bool SupportsShaderLanguageEXT(
            CNA::ShaderLanguageEXT language, CNA::ShaderStageEXT stage) const;

        /**
         * @brief Returns which graphics renderer THIS DEVICE is using.
         *
         * plans/plan_runtimerenderer.md RTR-P7-3. This used to be `constexpr`, returning
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
         * @brief Returns the cached detailed capability snapshot for the active renderer/device.
         *
         * The snapshot separates atomic feature support, numeric limits and per-format usage.
         * It is rebuilt when the native renderer is reconstructed or a capability-affecting
         * presentation setting is applied.
         *
         * @return Immutable capability profile owned by this graphics device. The reference
         * remains valid until renderer reconstruction or device destruction; copy it to retain a
         * snapshot across either operation.
         */
        CNAEXT [[nodiscard]] const CNA::RendererCapabilityProfile&
        GetRendererCapabilityProfileEXT() const;

        /**
         * @brief Returns the detailed classified answer for one renderer feature.
         * @param feature Feature to inspect.
         * @return Supported, unsupported, restricted or unknown.
         */
        CNAEXT [[nodiscard]] CNA::RendererFeatureSupport GetRendererFeatureSupportEXT(
            CNA::RendererFeature feature) const;

        /**
         * @brief Tests whether the complete detailed feature contract is supported.
         * @param feature Feature to inspect.
         * @return True only for an unqualified supported answer.
         */
        CNAEXT [[nodiscard]] bool SupportsRendererFeatureEXT(CNA::RendererFeature feature) const;

        /**
         * @brief Returns one numeric renderer/device limit from the cached profile.
         * @param limit Limit to inspect.
         * @return Limit value with an explicit known/unknown flag.
         */
        CNAEXT [[nodiscard]] CNA::RendererLimitValue GetRendererLimitEXT(
            CNA::RendererLimit limit) const;

        /**
         * @brief Returns known and supported usage masks for one surface format.
         * @param format Surface format to inspect.
         * @return Per-usage support facts; missing known bits remain explicitly unknown.
         */
        CNAEXT [[nodiscard]] CNA::RendererFormatSupport GetRendererSurfaceFormatSupportEXT(
            SurfaceFormat format) const;

        /**
         * @brief Returns a generated English report of features, limits, formats and extra notes.
         * @return UTF-8 text owned by the cached profile and valid until profile invalidation or
         * device destruction.
         */
        CNAEXT [[nodiscard]] std::string_view GetRendererCapabilityReportEXT() const;

        /**
         * @brief Returns whether the active renderer really creates a render target of the given
         *        surface format, instead of substituting an 8-bit Color target for it.
         *
         * `RenderTarget2D` has always accepted a `SurfaceFormat`, but every renderer that has not
         * implemented additional formats quietly produces a `Color` target regardless -- so a
         * caller that needs an HDR target (`HdrBlendable`, `HalfVector4`, `Vector4`, ...) has no way
         * to tell whether values above 1.0 will survive. This is that way, per format;
         * `GraphicsCapability::FloatRenderTargets` and `HalfFloatRenderTargets` are the coarse
         * summaries derived from it.
         *
         * @param format The surface format to ask about.
         * @return True when a render target of that format is created faithfully.
         */
        CNAEXT [[nodiscard]] bool SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat format) const;

        /**
         * @brief Returns the largest number of compute work groups a dispatch may request.
         *
         * plans/plan_modern.md `MOD-1505`. Zero on every renderer without compute, which is also what a
         * renderer that supports it but has not been asked yet reports -- so a caller checks
         * `GraphicsCapability::ComputeShaders` first and reads these to size its dispatch.
         *
         * @param axis 0 for x, 1 for y, 2 for z.
         * @return The limit, or 0 where compute is unsupported or the axis is out of range.
         */
        /**
         * @brief Returns whether a `ShaderEffect`'s source text really determines the pixels.
         *
         * plans/plan_modern.md `MOD-1699`. `GraphicsCapability::CustomEffects` says a renderer accepts a
         * custom effect; this says the shader you wrote is what runs. They differ on real
         * renderers: SOFTWARE and HEADLESS accept any source and render with their own fixed path,
         * and Vulkan takes SPIR-V bytecode rather than GLSL text. A pass that assumes the first
         * answer covers the second copies its input through and reports success.
         *
         * @return True when the supplied shader source is executed.
         */
        CNAEXT [[nodiscard]] bool ExecutesShaderEffectSourceEXT() const;

        /**
         * @brief Returns whether this renderer's lit shaders really sample the shadow state.
         *
         * plans/plan_modern.md `MOD-1699`. An effect accepts `IShadowReceiverEXT`'s state on every
         * renderer -- that is what keeps a shadow-configured draw working where there is no shadow
         * shader -- but only some renderers *use* it. This is the difference, asked of the
         * renderer rather than inferred from a frame that came out unshadowed.
         *
         * @return True when a shadow-configured draw will actually be shadowed.
         */
        CNAEXT [[nodiscard]] bool SupportsShadowSamplingEXT() const;

        /**
         * @brief Returns whether this renderer's PBR shader honours an `ImageBasedLightEXT`.
         *
         * plans/plan_modern.md `MOD-1699`. Same distinction as @ref SupportsShadowSamplingEXT: the
         * bundle is carried everywhere and shaded with in some places.
         *
         * @return True when a bound environment will actually light the surface.
         */
        CNAEXT [[nodiscard]] bool SupportsImageBasedLightingEXT() const;

        /**
         * @brief Returns the colour space the swap chain is presenting in.
         *
         * plans/plan_modern.md `MOD-2092`. `Srgb` on every CNA renderer today; see
         * @ref SetDisplayColorSpaceEXT for why that is an answer rather than a gap.
         *
         * @return The current display colour space.
         */
        CNAEXT [[nodiscard]] CNA::DisplayColorSpace GetDisplayColorSpaceEXT() const;

        /**
         * @brief Asks the swap chain to present in a different colour space.
         *
         * An HDR swap chain is a property of the presentation path -- DXGI, a Vulkan surface
         * format, a platform's own HDR opt-in -- rather than of a drawing API, and no CNA platform
         * back end offers one yet. So this returns false for anything but `Srgb` today, which is
         * the truth: a renderer that accepted the request without reconfiguring anything would have
         * its caller encode for a display that is not there, and PQ pixels shown as sRGB are washed
         * out and grey.
         *
         * @param space The space to present in.
         * @return True when the swap chain now presents in that space.
         */
        CNAEXT bool SetDisplayColorSpaceEXT(CNA::DisplayColorSpace space);

        /**
         * @brief Returns whether the swap chain can present in a given colour space.
         *
         * @param space The space to ask about.
         * @return True when @ref SetDisplayColorSpaceEXT would accept it.
         */
        CNAEXT [[nodiscard]] bool SupportsDisplayColorSpaceEXT(CNA::DisplayColorSpace space) const;

        CNAEXT [[nodiscard]] int GetMaxComputeWorkGroupCountEXT(int axis) const;

        /**
         * @brief Returns the largest local size a compute shader may declare on one axis.
         *
         * @param axis 0 for x, 1 for y, 2 for z.
         * @return The limit, or 0 where compute is unsupported or the axis is out of range.
         */
        CNAEXT [[nodiscard]] int GetMaxComputeWorkGroupSizeEXT(int axis) const;

        /**
         * @brief Returns the largest product of a compute shader's declared local sizes.
         *
         * @return The limit, or 0 where compute is unsupported.
         */
        CNAEXT [[nodiscard]] int GetMaxComputeWorkGroupInvocationsEXT() const;

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
        CNAEXT [[nodiscard]] const IndexBuffer* Indices() const;
        /**
         * @brief Binds an index buffer.
         * @param indexBuffer The index buffer to bind.
         */
        CNAEXT void Indices(const IndexBuffer* indexBuffer);

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

    protected:
        /**
         * @brief Releases this device's resources, optionally only the native ones.
         *
         * The documented protected disposal hook: `Dispose()` routes here, and a derived class
         * overrides this rather than the public method, so one path tears the device down however
         * it is reached. Idempotent, and it preserves the order the public disposer established --
         * the device is marked disposed first, then `Disposing` is raised, then owned resources are
         * disposed, then the window and the video subsystem are released.
         *
         * A derived class that overrides this must also write
         * `using GraphicsDevice::Dispose;`, because declaring the name hides the public `Dispose()`
         * from its own callers -- a C++ name-lookup consequence with no C# counterpart.
         *
         * XNA's finalizer calls `Dispose(false)`. C++ has no separate finalizer, so the destructor
         * is the only other caller and it passes @c true: every member is still alive in a
         * destructor body, which is the condition the flag exists to distinguish.
         *
         * @param disposing True when called from Dispose() or the destructor; false when only
         *        native resources may be touched, in which case Disposing is not raised.
         */
        virtual void Dispose(bool disposing);

    private:
        // Borrowed from Game's enclosing platform (or the ambient lazy default for a bare
        // GraphicsDevice). The platform outlives both the window and this device.
        CNA::Platform::IPlatform* platform_;
        std::unique_ptr<CNA::Platform::IPlatformWindow> platformWindow_;
        /// MERGE (plans/plan_runtimerenderer.md RTR-P5-12 x plans/plan_platform.md PLAT-8): the platform wrapper
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
        /// Detailed device-specific capabilities, populated lazily and invalidated with renderer
        /// reconstruction or an applied capability-affecting setting.
        mutable std::optional<CNA::RendererCapabilityProfile> rendererCapabilityProfile_;
        /// plans/plan_runtimerenderer.md RTR-P5: the descriptor this device actually resolved to, which
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
        /// Open CNA::Internal::EngineLayerFloatFilteringScope instances (VMG-0006); zero for every
        /// ordinary XNA draw, which then keeps XNA's point-filter-only rule for float formats.
        int engineLayerFloatFilteringDepth_ = 0;
        PresentationParameters presentationParameters_;
        bool isDisposed_;
        /// plans/plan_dx9.md D9-34: tracks the real device-lifecycle state reported by a renderer via
        /// GraphicsRendererCreateArgs::deviceEventCallback (RendererDeviceEvent::Lost -> Lost,
        /// Resetting -> NotReset, Reset -> Normal). Every renderer except D3D9 never calls that
        /// callback, so this stays Normal there, matching the pre-existing hardcoded behavior.
        GraphicsDeviceStatus deviceStatus_ = GraphicsDeviceStatus::Normal;

        // State/sampler collection construction binds public resource identities to this device.
        // Declare the lifetime token first so that binding never observes it before construction.
        std::shared_ptr<void> resourceDeviceLifetime_ = std::make_shared<int>(0);

        BlendState blendState_;
        DepthStencilState depthStencilState_;
        RasterizerState rasterizerState_;
        Rectangle scissorRectangle_;
        Color blendFactor_;
        int multiSampleMask_ = -1;
        int referenceStencil_ = 0;
        bool blendStateDirty_ = false;
        bool depthStencilStateDirty_ = false;
        std::uint16_t spriteBeginCount_ = 0;
        std::uint16_t spriteImmediateBeginCount_ = 0;

        TextureCollection textures_;
        SamplerStateCollection samplerStates_;
        TextureCollection vertexTextures_;
        SamplerStateCollection vertexSamplerStates_;

        std::vector<RenderTargetBinding> currentRenderTargets_;
        bool renderTargetBound_ = false;
        // plans/plan_directx12_parity.md DX12-0023: a bound target was destroyed. Its binding is gone
        // (no dangling pointer to compare or dereference) but the device stays bound until the next
        // SetRenderTargets, which is therefore never skipped as unchanged.
        bool boundRenderTargetDestroyed_ = false;
        std::vector<VertexBufferBinding> currentVertexBuffers_;
        std::vector<GraphicsResource*> resources_;

        // Reusable byte buffers for DrawUserPrimitives / DrawUserIndexedPrimitives staging,
        // avoiding a heap allocation on every draw call once capacity has grown to fit.
        std::vector<std::uint8_t> userVertexScratch_;
        std::vector<std::uint8_t> userIndexScratch_;
        void ThrowIfDisposed() const;
        void GetBackBufferDataCore(const Rectangle* rect, void* data,
                                   int startIndex, int elementCount,
                                   std::size_t elementSizeInBytes, bool colorObjects);
        void GetActiveRenderDimensions(int& width, int& height) const;
        [[nodiscard]] ClearOptions GetDefaultClearOptions() const;
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

        // A binding remains cached when its resource is disposed. Validate before any renderer
        // handle is dereferenced so that use-after-dispose is a public exception, not null UB.
        void ThrowIfBoundVertexBufferDisposed() const;
        void ThrowIfBoundIndexBufferDisposed() const;
        void DetachDestroyedVertexBuffer(const VertexBuffer* vertexBuffer) noexcept;
        void DetachDestroyedIndexBuffer(const IndexBuffer* indexBuffer) noexcept;
        void DetachMovedTexture(const Texture* texture) noexcept;
        // plans/plan_directx12_parity.md DX12-0023: called by a render target's destruction while its
        // backend still exists. A binding that names it is dropped and the renderer returns to the back
        // buffer; the device stays bound (see boundRenderTargetDestroyed_).
        void DetachDestroyedRenderTarget(const Texture* texture) noexcept;
        void TransferMovedVertexBuffer(const VertexBuffer* source,
                                       const VertexBuffer* destination) noexcept;
        void TransferMovedIndexBuffer(const IndexBuffer* source,
                                      const IndexBuffer* destination) noexcept;
        void TransferMovedTexture(const Texture* source, Texture* destination) noexcept;
        void TransferResourceReference(GraphicsResource* source,
                                       GraphicsResource* destination) noexcept;

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

        // Compatibility guard for renderers that stage unchecked CPU copies. Microsoft XNA does
        // not perform this validation for classic buffered draws, so GraphicsDevice calls it only
        // when the renderer explicitly requires protection from out-of-range host-memory access.
        void ValidateVertexStreamRanges(
            const CNA::Internal::Renderers::GpuDrawParams& p,
            std::int64_t startElement,
            int elementCount,
            const char* parameterName,
            const std::string& parameterValue) const;

        /** Clears a selected effect only when the exact resource is being disposed. */
        void ClearCurrentEffectIf(const Effect* effect) noexcept;

        // REMED-GFX-202: REMED-GFX-118's instance-range gate widened from the first per-instance
        // binding to EVERY one of them. `instanceCount` instances consume
        // `1 + (firstInstance + instanceCount - 1) / InstanceFrequency` records of each
        // per-instance stream, beginning at that stream's own VertexOffset -- all in vertex
        // ELEMENTS of that stream's own declaration, never bytes. A stream too short is rejected
        // here, naming the offending slot, even when another per-instance stream is long enough.
        void ValidateInstanceStreamRanges(
            const CNA::Internal::Renderers::GpuDrawParams& p,
            int instanceCount, int firstInstance = 0) const;

        void DrawInstancedPrimitivesCore(
            PrimitiveType primitiveType, int baseVertex, int minVertexIndex,
            int numVertices, int startIndex, int primitiveCount,
            int instanceCount, int firstInstance);

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

        // The documented generic array draw overloads' shared range check and dispatch. The range
        // check is here rather than in each overload because an array's own length is the one thing
        // the pointer overloads cannot see, so it is the one check they cannot make; everything
        // after it is theirs.
        void CheckUserVertexArrayRange(std::size_t available, int vertexOffset,
                                       PrimitiveType primitiveType, int primitiveCount) const;
        void CheckUserIndexedArrayRanges(std::size_t verticesAvailable, int vertexOffset,
                                         int numVertices, std::size_t indicesAvailable,
                                         int indexOffset, PrimitiveType primitiveType,
                                         int primitiveCount) const;

        /// Whether CNA can convert TVertex from its object form into the GPU stream its
        /// VertexDeclaration describes. True for the four built-in vertex structures, which have a
        /// packing function; a caller's own vertex structure has none, because C++ cannot derive one
        /// from the declaration without reflection.
        template <typename TVertex>
        static constexpr bool IsConvertibleVertex =
            std::is_same_v<TVertex, VertexPositionColor> ||
            std::is_same_v<TVertex, VertexPositionTexture> ||
            std::is_same_v<TVertex, VertexPositionColorTexture> ||
            std::is_same_v<TVertex, VertexPositionNormalTexture>;

        template <typename TVertex>
        void DrawUserVertexArray(PrimitiveType primitiveType, const TVertex* vertexData,
                                 int vertexOffset, int primitiveCount,
                                 const VertexDeclaration* vertexDeclaration)
        {
            if constexpr (IsConvertibleVertex<TVertex>)
            {
                // Straight into the pointer overload that already converts and validates.
                if (vertexDeclaration != nullptr)
                {
                    DrawUserPrimitives(primitiveType, vertexData, vertexOffset, primitiveCount,
                                       *vertexDeclaration);
                }
                else
                {
                    DrawUserPrimitives(primitiveType, vertexData, vertexOffset, primitiveCount);
                }
            }
            else if constexpr (!std::is_polymorphic_v<TVertex>)
            {
                // A plain vertex structure's array is already the GPU stream, so the raw overload
                // consumes it directly at the declared stride.
                ThrowUnsupportedUserVertexType(vertexDeclaration != nullptr);
                DrawUserPrimitives(primitiveType, static_cast<const void*>(vertexData),
                                   vertexOffset, primitiveCount, *vertexDeclaration);
            }
            else
            {
                // A polymorphic structure carries a vtable pointer, so its bytes are not a stream
                // and there is no conversion for it.
                (void)vertexData;
                (void)vertexOffset;
                (void)primitiveCount;
                (void)vertexDeclaration;
                ThrowUnconvertibleUserVertexType();
            }
        }

        template <typename TVertex, typename TIndex>
        void DrawUserIndexedVertexArray(PrimitiveType primitiveType, const TVertex* vertexData,
                                        int vertexOffset, int numVertices, const TIndex* indexData,
                                        int indexOffset, int primitiveCount,
                                        const VertexDeclaration* vertexDeclaration)
        {
            // XNA's index arrays are Int16[]/Int32[]; CNA's pointer overloads take the unsigned
            // variants of the same types, which is the same object representation read through the
            // accompanying signed/unsigned type.
            using TUnsigned = std::make_unsigned_t<TIndex>;
            const auto* indices = reinterpret_cast<const TUnsigned*>(indexData);

            if constexpr (IsConvertibleVertex<TVertex>)
            {
                if (vertexDeclaration != nullptr)
                {
                    DrawUserIndexedPrimitives(primitiveType, vertexData, vertexOffset, numVertices,
                                              indices, indexOffset, primitiveCount,
                                              *vertexDeclaration);
                }
                else
                {
                    DrawUserIndexedPrimitives(primitiveType, vertexData, vertexOffset, numVertices,
                                              indices, indexOffset, primitiveCount);
                }
            }
            else if constexpr (!std::is_polymorphic_v<TVertex>)
            {
                ThrowUnsupportedUserVertexType(vertexDeclaration != nullptr);
                DrawUserIndexedPrimitives(primitiveType, static_cast<const void*>(vertexData),
                                          vertexOffset, numVertices, indices, indexOffset,
                                          primitiveCount, *vertexDeclaration);
            }
            else
            {
                (void)vertexData;
                (void)vertexOffset;
                (void)numVertices;
                (void)indices;
                (void)indexOffset;
                (void)primitiveCount;
                ThrowUnconvertibleUserVertexType();
            }
        }

        /// Refuses a plain vertex structure drawn without a declaration: nothing describes its
        /// layout, because it implements no IVertexType.
        void ThrowUnsupportedUserVertexType(bool haveDeclaration) const;

        /// Refuses a polymorphic vertex structure that is not one of the built-in ones.
        [[noreturn]] void ThrowUnconvertibleUserVertexType() const;

        [[nodiscard]] std::uintptr_t GetWindowHandleInternal() const;
        [[nodiscard]] CNA::Platform::IPlatformWindow* GetPlatformWindowInternal() const;

        void createOrAttachWindow();
        void createRenderer();
        [[nodiscard]] CNA::RendererCapabilityProfile BuildRendererCapabilityProfileEXT() const;
        void InvalidateRendererCapabilityProfileEXT() const;

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
         * plans/plan_runtimerenderer.md design decisions 6 and 7. Creates the window and the renderer
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
        void validateDrawState(
            const CNA::Internal::Renderers::GpuDrawParams* drawParams = nullptr) const;
        /**
         * @brief Pushes SamplerStates[firstSlot..MaxSamplers-1] down to the renderer.
         *
         * Called from every draw entry point with the default, and from SpriteBatch's flush
         * with firstSlot == 1 (plan_vulkan.md VULKAN-166): a sprite batch owns slot 0 through
         * ISpriteBatchRenderer::SetSamplerFilter/SetSamplerAddressMode/SetSamplerAddressModeWEXT,
         * so re-publishing it here would give one slot two writers with no ordering between them
         * across the renderer families. Slots 1 and up have no other writer at all, which is why
         * a ShaderEffect's second texture unit could not be sampled differently from its first.
         *
         * @param firstSlot Lowest sampler slot to publish; slots below it are left alone.
        */
        void applySamplerStatesToRenderer(int firstSlot = 0);

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

        /**
         * @brief Maps a game-supplied Viewport/ScissorRectangle into the rectangle the renderer
         *        seam expects.
         *
         * `GraphicsDevice.Viewport` and `GraphicsDevice.ScissorRectangle` are public XNA state in
         * the game's LOGICAL (virtual-resolution) space, but `IGraphicsRenderer::SetViewport` and
         * `SetScissorRect` are drawable-space seams for the renderers that present the logical
         * content into a sub-rectangle of the window — EasyGL, Magnum and OpenGL2 all Y-flip
         * against their physical size and program the rectangle verbatim. Only
         * `UpdateViewportFromWindow()` used to perform that mapping, so a game that assigned
         * either property (split-screen, a scissored HUD, or simply re-applying the value it just
         * read) programmed the GPU in logical space and bypassed the letterbox/scale placement.
         *
         * The mapping is derived from the same pair `UpdateViewportFromWindow()` uses:
         * `GetViewportSize()` (logical) and `GetDefaultViewportRect()` (physical). It is the
         * identity whenever those agree, which is exactly the case for every renderer that does
         * not override `GetDefaultViewportRect()` — including the family that deliberately treats
         * the pushed rectangle as logical and rescales internally (Diligent, Sokol, LLGL, SDL_GPU,
         * WebGPU). It is also the identity while a render target is bound, because a render
         * target's viewport and scissor are in that target's own pixel space.
         *
         * @param x       Logical left edge; receives the mapped left edge.
         * @param y       Logical top edge; receives the mapped top edge.
         * @param width   Logical width; receives the mapped width.
         * @param height  Logical height; receives the mapped height.
         */
        void MapLogicalRectToPresentation(int& x, int& y, int& width, int& height) const;

        /**
         * @brief Acquires the renderer context for one bounded framework operation.
         *
         * @return A lifetime token, or null when the renderer needs no explicit context lease.
         */
        CNAEXT [[nodiscard]] std::unique_ptr<CNA::Internal::Renderers::IRendererThreadContextLease>
            AcquireRendererThreadContextLease();

        /** @brief Acquires a frame lease that may release this renderer's own prior binding. */
        CNAEXT [[nodiscard]] std::unique_ptr<CNA::Internal::Renderers::IRendererThreadContextLease>
            AcquireRendererThreadContextLeaseForFrame();

        friend class Texture2D;
        friend class Texture3D;
        friend class TextureCube;
        friend class RenderTargetCube;
        friend class GraphicsResource;
        friend class VertexBuffer;
        friend class IndexBuffer;
        friend class ShaderEffect;
        friend class Effect;
        friend class SpriteBatch;
        friend class Microsoft::Xna::Framework::GameWindow;
        friend class Microsoft::Xna::Framework::GraphicsDeviceManager;
        friend class Microsoft::Xna::Framework::Game;
        friend class Microsoft::Xna::Framework::Content::ContentReader;
        friend class CNA::Internal::Texture2DArrayGraphicsDeviceTestPeer;
        friend class CNA::Internal::StorageTexture2DGraphicsDeviceTestPeer;
        friend class CNA::Internal::StorageBufferGraphicsDeviceTestPeer;
        // plans/plan_vulkan_modern_graphics.md VMG-0006: the CNAEXT engine layer's own draws may
        // filter a float/half source the live renderer can filter, which XNA's VerifyCanDraw
        // refuses for XNA draws (SOFTWARE-217). The scope is the only way to open that exemption.
        friend class CNA::Internal::EngineLayerFloatFilteringScope;
        // plans/plan_x11.md X11-0104. GraphicsDevicePlatformWindowTests reproduces what
        // GameWindow.ClientSizeChanged runs -- a viewport refresh driven from the frame's event
        // pump -- and GameWindow is a friend above precisely because that call is internal. The
        // test reached it directly, which compiled only for an SDL3 selection (where its own
        // `#if` skips the body) and was a hard error under every other platform. Found by
        // building the suite with CNA_PLATFORM=X11; reproduced identically with
        // CNA_PLATFORM=HEADLESS, so the defect is the test's access, not the X11 backend.
        friend class CNA::Internal::GraphicsDevicePlatformWindowTestPeer;
    };
}
