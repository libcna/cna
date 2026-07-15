#include "CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.hpp"

#include <stdexcept>

namespace CNA::Internal::Backends::Canvas
{
    namespace
    {
        // Phase C1 placeholder for methods a later phase makes real (Clear/Present in C2,
        // textures/render targets in C3, SpriteBatch in C4). Distinct from ThrowNo3D, which
        // Phase C7 wires up permanently for the inherently-3D-only surface.
        [[noreturn]] void NotYetImplemented(const char* methodName)
        {
            throw std::runtime_error(
                std::string("CanvasGraphicsBackend::") + methodName + " not yet implemented (see plan_canvas.md)");
        }

        [[noreturn]] void ThrowNo3D(const char* methodName)
        {
            throw std::runtime_error(
                std::string("Canvas (HTML Canvas 2D) does not support 3D: ") + methodName);
        }
    }

    CanvasGraphicsBackend::CanvasGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                  CnaPresentationMode mode)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
    {
        if (!window_) throw std::runtime_error("CanvasGraphicsBackend initialized with null window.");
        IGraphicsBackend::RegisterForWindow(window_, this);
    }

    CanvasGraphicsBackend::~CanvasGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
    }

    void CanvasGraphicsBackend::Clear(float, float, float, float) { NotYetImplemented("Clear"); }
    void CanvasGraphicsBackend::Present() { NotYetImplemented("Present"); }

    void CanvasGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        width = virtualWidth_;
        height = virtualHeight_;
    }

    void CanvasGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void CanvasGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    std::unique_ptr<ITextureBackend> CanvasGraphicsBackend::CreateTexture(const ImageData&)
    {
        NotYetImplemented("CreateTexture");
    }

    std::unique_ptr<ISpriteBatchBackend> CanvasGraphicsBackend::CreateSpriteBatch()
    {
        NotYetImplemented("CreateSpriteBatch");
    }

    void CanvasGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth"); }
    void CanvasGraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void CanvasGraphicsBackend::ClearStencil(int) { ThrowNo3D("ClearStencil"); }
    void CanvasGraphicsBackend::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil"); }
    void CanvasGraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil"); }
    void CanvasGraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil"); }
    void CanvasGraphicsBackend::SetDepthTestEnabled(bool) { ThrowNo3D("SetDepthTestEnabled"); }
    void CanvasGraphicsBackend::SetBlendEnabled(bool) { ThrowNo3D("SetBlendEnabled"); }
    void CanvasGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferBackend> CanvasGraphicsBackend::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer");
    }

    std::unique_ptr<IIndexBufferBackend> CanvasGraphicsBackend::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16");
    }

    void CanvasGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                      const Matrix&, const Matrix&, const Matrix&,
                                                      PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives"); }

    void CanvasGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                                             const Matrix&, const Matrix&, const Matrix&,
                                                             PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Canvas::CanvasGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
    }
}
