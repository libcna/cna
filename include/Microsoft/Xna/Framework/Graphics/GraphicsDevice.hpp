#pragma once

#include <array>
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
        System::EventHandler<System::EventArgs> Disposing;
        System::EventHandler<System::EventArgs> DeviceLost;
        System::EventHandler<System::EventArgs> DeviceReset;
        System::EventHandler<System::EventArgs> DeviceResetting;
        System::EventHandler<ResourceCreatedEventArgs> ResourceCreated;
        System::EventHandler<ResourceDestroyedEventArgs> ResourceDestroyed;

        // --- Constructors ---
        GraphicsDevice();
        GraphicsDevice(GraphicsAdapter& adapter, GraphicsProfile graphicsProfile,
                       const PresentationParameters& presentationParameters);
        ~GraphicsDevice() override;

        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;
        GraphicsDevice(GraphicsDevice&&) = delete;
        GraphicsDevice& operator=(GraphicsDevice&&) = delete;

        // --- State properties ---
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] GraphicsDeviceStatus getGraphicsDeviceStatusProperty() const;
        [[nodiscard]] GraphicsAdapter& getAdapterProperty() const;
        [[nodiscard]] GraphicsProfile getGraphicsProfileProperty() const;
        [[nodiscard]] PresentationParameters& getPresentationParametersProperty();
        [[nodiscard]] const PresentationParameters& getPresentationParametersProperty() const;

        // --- Display ---
        [[nodiscard]] DisplayMode getDisplayModeProperty() const;

        // --- GL State ---
        [[nodiscard]] TextureCollection& getTexturesProperty();
        [[nodiscard]] SamplerStateCollection& getSamplerStatesProperty();
        [[nodiscard]] TextureCollection& getVertexTexturesProperty();
        [[nodiscard]] SamplerStateCollection& getVertexSamplerStatesProperty();

        [[nodiscard]] BlendState& getBlendStateProperty();
        void setBlendStateProperty(const BlendState& value);
        [[nodiscard]] const BlendState& getBlendStateProperty() const;

        [[nodiscard]] DepthStencilState& getDepthStencilStateProperty();
        void setDepthStencilStateProperty(const DepthStencilState& value);
        [[nodiscard]] const DepthStencilState& getDepthStencilStateProperty() const;

        [[nodiscard]] RasterizerState& getRasterizerStateProperty();
        void setRasterizerStateProperty(const RasterizerState& value);
        [[nodiscard]] const RasterizerState& getRasterizerStateProperty() const;

        [[nodiscard]] Rectangle getScissorRectangleProperty() const;
        void setScissorRectangleProperty(const Rectangle& value);

        [[nodiscard]] const Viewport& getViewportProperty() const;
        void setViewportProperty(const Viewport& value);

        [[nodiscard]] Color getBlendFactorProperty() const;
        void setBlendFactorProperty(const Color& value);

        [[nodiscard]] int getMultiSampleMaskProperty() const;
        void setMultiSampleMaskProperty(int value);

        [[nodiscard]] int getReferenceStencilProperty() const;
        void setReferenceStencilProperty(int value);

        // --- Index buffer ---
        [[nodiscard]] const IndexBuffer* getIndicesProperty() const;
        void setIndicesProperty(const IndexBuffer* indexBuffer);

        // --- Core operations ---
        void Clear(const Color& color);
        void Clear(float r, float g, float b, float a);
        void Clear(ClearOptions options, const Color& color, float depth, int stencil);
        void Clear(const Color& color, float depth);

        void Present();

        void Reset();
        void Reset(const PresentationParameters& presentationParameters);
        void Reset(const PresentationParameters& presentationParameters, GraphicsAdapter& adapter);
        void Reset(const PresentationParameters& presentationParameters, GraphicsAdapter* adapter);

        void Dispose() override;

        // --- Render targets ---
        void SetRenderTarget(RenderTarget2D* renderTarget);
        void SetRenderTarget(RenderTargetCube* renderTarget, CubeMapFace cubeMapFace);
        void SetRenderTargets(const std::vector<RenderTargetBinding>& renderTargets);
        [[nodiscard]] std::vector<RenderTargetBinding> GetRenderTargets() const;

        // --- Vertex/index buffers ---
        void SetVertexBuffer(const VertexBuffer* vertexBuffer);
        void SetVertexBuffer(const VertexBuffer* vertexBuffer, int vertexOffset);
        void SetVertexBuffers(const std::vector<VertexBufferBinding>& vertexBuffers);
        [[nodiscard]] std::vector<VertexBufferBinding> GetVertexBuffers() const;

        void SetIndexBuffer(const IndexBuffer* indexBuffer);
        [[nodiscard]] const VertexBuffer* GetVertexBuffer() const;
        [[nodiscard]] const IndexBuffer* GetIndexBuffer() const;

        // --- Draw ---
        void DrawPrimitives(PrimitiveType primitiveType, int vertexStart, int primitiveCount);
        void DrawIndexedPrimitives(PrimitiveType primitiveType,
                                   int baseVertex, int minVertexIndex,
                                   int numVertices, int startIndex, int primitiveCount);
        void DrawUserPrimitives(PrimitiveType primitiveType, const void* vertexData,
                                int vertexOffset, int primitiveCount);
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const void* vertexData, int vertexOffset, int numVertices,
                                       const void* indexData, int indexOffset, int primitiveCount);

        // --- NOXNA helpers (not in XNA 4.0) ---
        NOXNA void SetDepthTestEnabled(bool enabled);
        NOXNA void SetBlendEnabled(bool enabled);
        NOXNA void SetDepthWriteEnabled(bool enabled);

        [[nodiscard]] CNA::Internal::Backends::IGraphicsBackend& GetBackend() const;
        void SetCurrentEffect(BasicEffect* effect);

        // Backward compat
        [[nodiscard]] const IndexBuffer* Indices() const;
        void Indices(const IndexBuffer* indexBuffer);

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

        friend class Texture2D;
        friend class SpriteBatch;
        friend class Microsoft::Xna::Framework::GameWindow;
        friend class Microsoft::Xna::Framework::GraphicsDeviceManager;
        friend class Microsoft::Xna::Framework::Game;
    };
}
