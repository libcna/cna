// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
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

struct SDL_Window;
struct SDL_Renderer;

namespace Microsoft::Xna::Framework
{
    class Game;
    class GameWindow;
    class GraphicsDeviceManager;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class BasicEffect;
    class RenderTarget2D;
    class RenderTargetCube;
    class RenderTargetCube;
}

namespace CNA::Internal::Backends
{
    class IGraphicsBackend;
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
        NOXNA ~GraphicsDevice() override;

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
         * @param vertexBuffers Vector of vertex buffer bindings to apply.
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
         * @brief Draws instanced indexed primitives.
         * @param primitiveType  The type of primitive to draw.
         * @param baseVertex     Offset added to each index.
         * @param minVertexIndex Minimum vertex index referenced.
         * @param numVertices    Number of vertices referenced.
         * @param startIndex     Start index in the index buffer.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances to draw.
         */
        void DrawInstancedPrimitives(PrimitiveType primitiveType,
                                     int baseVertex, int minVertexIndex,
                                     int numVertices, int startIndex,
                                     int primitiveCount, int instanceCount);
        /**
         * @brief Draws non-indexed primitives from a user-supplied raw vertex buffer.
         * @param primitiveType  The type of primitive to draw.
         * @param vertexData     Pointer to the raw vertex data.
         * @param vertexOffset   Offset into @p vertexData (in vertices) to start drawing from.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType, const void* vertexData,
                                int vertexOffset, int primitiveCount);
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

        // --- NOXNA helpers (not in XNA 4.0) ---
        /** @brief Enables or disables depth testing. */
        NOXNA void SetDepthTestEnabled(bool enabled);
        /** @brief Enables or disables blending. */
        NOXNA void SetBlendEnabled(bool enabled);
        /** @brief Enables or disables depth writes. */
        NOXNA void SetDepthWriteEnabled(bool enabled);
        /**
         * @brief Disables GL context-loss recovery (CPU shadow copies + ResourceRegistry).
         *
         * Must be called before the device is initialized. Safe on desktop where
         * context loss never occurs; saves approximately one copy of texture RAM per loaded texture.
         *
         * @param enabled Pass false to disable context recovery.
         */
        NOXNA void SetContextRecoveryEnabled(bool enabled);

        /** @brief Returns a reference to the active graphics backend. */
        NOXNA [[nodiscard]] CNA::Internal::Backends::IGraphicsBackend& GetBackend() const;
        /**
         * @brief Sets the currently active BasicEffect for draw calls.
         * @param effect The effect to use, or nullptr.
         */
        NOXNA void SetCurrentEffect(BasicEffect* effect);

        /** @brief Returns the currently bound index buffer. */
        [[nodiscard]] const IndexBuffer* Indices() const;
        /**
         * @brief Binds an index buffer.
         * @param indexBuffer The index buffer to bind.
         */
        void Indices(const IndexBuffer* indexBuffer);

        /** @brief Returns the fully qualified .NET type name of this class. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        SDL_Window* window_;
        bool ownsWindow_;
        std::unique_ptr<CNA::Internal::Backends::IGraphicsBackend> backend_;
        Viewport viewport_;
        const VertexBuffer* currentVertexBuffer_;
        const IndexBuffer* currentIndexBuffer_;
        BasicEffect* currentEffect_;
        int virtualWidth_;
        int virtualHeight_;
        bool contextRecoveryEnabled_ = true;
        GraphicsAdapter* adapter_;
        GraphicsProfile graphicsProfile_;
        PresentationParameters presentationParameters_;
        bool isDisposed_;

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
        std::vector<VertexBufferBinding> currentVertexBuffers_;

        [[nodiscard]] SDL_Renderer* GetRendererInternal() const;
        [[nodiscard]] SDL_Window* GetWindowInternal() const;

        void createOrAttachWindow();
        void createBackend();
        void destroyNativeResources();
        void UpdateViewportFromWindow();
        void SetVirtualResolution(int width, int height);
        void SetPresentationMode(int mode);
        void applyPresentationParametersToWindow();
        void applySamplerStatesToBackend();

        friend class Texture2D;
        friend class RenderTargetCube;
        friend class SpriteBatch;
        friend class Microsoft::Xna::Framework::GameWindow;
        friend class Microsoft::Xna::Framework::GraphicsDeviceManager;
        friend class Microsoft::Xna::Framework::Game;
    };
}
