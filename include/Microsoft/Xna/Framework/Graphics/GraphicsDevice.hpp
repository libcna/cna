#pragma once

#include <memory>
#include <string>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

struct SDL_Window;
struct SDL_Renderer;

namespace Microsoft::Xna::Framework
{
    class GameWindow;
    class GraphicsDeviceManager;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class BasicEffect;
}

namespace CNA::Internal::Backends
{
    class IGraphicsBackend;
}

namespace Microsoft::Xna::Framework::Graphics
{
    /// Main graphics device used for clearing, presenting and drawing primitives.
    class GraphicsDevice : public System::Object, public System::IDisposable
    {
    public:
        /// Raised before the device is disposed.
        System::EventHandler<System::EventArgs> Disposing;

        /// Raised after the device has been reset.
        System::EventHandler<System::EventArgs> DeviceReset;

        /// Raised before the device is reset.
        System::EventHandler<System::EventArgs> DeviceResetting;

        /// Creates a graphics device using default adapter and presentation parameters.
        GraphicsDevice();

        /// Creates a graphics device using explicit adapter, profile and presentation parameters.
        GraphicsDevice(GraphicsAdapter& adapter, GraphicsProfile graphicsProfile, const PresentationParameters& presentationParameters);

        ~GraphicsDevice() override;

        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;
        GraphicsDevice(GraphicsDevice&&) = delete;
        GraphicsDevice& operator=(GraphicsDevice&&) = delete;

        /// Gets the adapter used by this graphics device.
        [[nodiscard]] GraphicsAdapter& getAdapterProperty() const;

        /// Gets the requested graphics profile.
        [[nodiscard]] GraphicsProfile getGraphicsProfileProperty() const;

        /// Gets the active presentation parameters.
        [[nodiscard]] PresentationParameters& getPresentationParametersProperty();

        /// Gets the active presentation parameters.
        [[nodiscard]] const PresentationParameters& getPresentationParametersProperty() const;

        /// Gets the current viewport.
        [[nodiscard]] const Viewport& getViewportProperty() const;

        /// Sets the current viewport.
        void setViewportProperty(const Viewport& value);

        /// Gets the currently bound index buffer.
        [[nodiscard]] const IndexBuffer* getIndicesProperty() const;

        /// Sets the currently bound index buffer.
        void setIndicesProperty(const IndexBuffer* indexBuffer);

        /// Gets whether this graphics device has been disposed.
        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Clears the color buffer with the specified color.
        void Clear(const Color& color);

        /// Clears the color buffer with the specified RGBA components in the range 0..1.
        void Clear(float r, float g, float b, float a);

        /// Clears the selected buffers.
        void Clear(ClearOptions options, const Color& color, float depth, int stencil);

        /// Clears the color and depth buffers.
        void Clear(const Color& color, float depth);

        /// Presents the rendered frame.
        void Present();

        /// Resets this device with new presentation parameters and adapter.
        void Reset(const PresentationParameters& presentationParameters, GraphicsAdapter& adapter);

        /// Resets this device with new presentation parameters and optional adapter.
        void Reset(const PresentationParameters& presentationParameters, GraphicsAdapter* adapter);

        /// Releases the device resources.
        void Dispose() override;

        /// Enables or disables depth testing in the backend.
        void SetDepthTestEnabled(bool enabled);

        /// Binds a vertex buffer.
        void SetVertexBuffer(const VertexBuffer* vertexBuffer);

        /// Binds an index buffer.
        void SetIndexBuffer(const IndexBuffer* indexBuffer);

        /// Gets the currently bound vertex buffer.
        [[nodiscard]] const VertexBuffer* GetVertexBuffer() const;

        /// Gets the currently bound index buffer.
        [[nodiscard]] const IndexBuffer* GetIndexBuffer() const;

        /// Backward-compatible XNA-shaped getter used by older CNA code.
        [[nodiscard]] const IndexBuffer* Indices() const;

        /// Backward-compatible XNA-shaped setter used by older CNA code.
        void Indices(const IndexBuffer* indexBuffer);

        /// Draws non-indexed primitives from the current vertex buffer.
        void DrawPrimitives(PrimitiveType primitiveType, int vertexStart, int primitiveCount);

        /// Draws indexed primitives from the current vertex and index buffers.
        void DrawIndexedPrimitives(
            PrimitiveType primitiveType,
            int baseVertex,
            int minVertexIndex,
            int numVertices,
            int startIndex,
            int primitiveCount
        );

        /// Draws user-provided vertex data. This overload is present but awaits backend support.
        void DrawUserPrimitives(PrimitiveType primitiveType, const void* vertexData, int vertexOffset, int primitiveCount);

        /// Draws user-provided indexed vertex data. This overload is present but awaits backend support.
        void DrawUserIndexedPrimitives(
            PrimitiveType primitiveType,
            const void* vertexData,
            int vertexOffset,
            int numVertices,
            const void* indexData,
            int indexOffset,
            int primitiveCount
        );

        /// Gets the active backend. Throws if the backend is not available.
        [[nodiscard]] CNA::Internal::Backends::IGraphicsBackend& GetBackend() const;

        /// Internal hook used by BasicEffect::Apply.
        void SetCurrentEffect(BasicEffect* effect);

        [[nodiscard]] const std::string& GetTypeName() const override;

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
        GraphicsAdapter* adapter_;
        GraphicsProfile graphicsProfile_;
        PresentationParameters presentationParameters_;
        bool isDisposed_;

        [[nodiscard]] SDL_Renderer* GetRendererInternal() const;
        [[nodiscard]] SDL_Window* GetWindowInternal() const;

        void createOrAttachWindow();
        void createBackend();
        void destroyNativeResources();
        void UpdateViewportFromWindow();
        void SetVirtualResolution(int width, int height);
        void SetPresentationMode(int mode);
        void applyPresentationParametersToWindow();

        friend class Texture2D;
        friend class SpriteBatch;
        friend class Microsoft::Xna::Framework::GameWindow;
        friend class Microsoft::Xna::Framework::GraphicsDeviceManager;
    };
}
