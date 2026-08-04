#include "CNA/Internal/Backends/Stub/StubGraphicsBackend.hpp"

namespace CNA::Internal::Backends::Stub
{
    StubGraphicsBackend::StubGraphicsBackend(int virtualWidth, int virtualHeight)
        : virtualWidth_(virtualWidth), virtualHeight_(virtualHeight)
    {
    }

    void StubGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        width = virtualWidth_ > 0 ? virtualWidth_ : 1024;
        height = virtualHeight_ > 0 ? virtualHeight_ : 768;
    }

    void StubGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    std::unique_ptr<ITextureBackend> StubGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<StubTextureBackend>(data.width, data.height);
    }

    std::unique_ptr<ISpriteBatchBackend> StubGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<StubSpriteBatchBackend>();
    }

    std::unique_ptr<IVertexBufferBackend> StubGraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<StubVertexBufferBackend>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> StubGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<StubIndexBufferBackend>(index_capacity);
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Stub::StubGraphicsBackend>(args.virtualWidth, args.virtualHeight);
    }
}
