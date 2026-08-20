#include "CNA/Internal/Renderers/Stub/StubRenderer.hpp"

namespace CNA::Internal::Renderers::Stub
{
    StubRenderer::StubRenderer(int virtualWidth, int virtualHeight)
        : virtualWidth_(virtualWidth), virtualHeight_(virtualHeight)
    {
    }

    void StubRenderer::GetViewportSize(int& width, int& height)
    {
        width = virtualWidth_ > 0 ? virtualWidth_ : 1024;
        height = virtualHeight_ > 0 ? virtualHeight_ : 768;
    }

    void StubRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    std::unique_ptr<ITextureRenderer> StubRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<StubTextureRenderer>(data.width, data.height);
    }

    std::unique_ptr<ISpriteBatchRenderer> StubRenderer::CreateSpriteBatch()
    {
        return std::make_unique<StubSpriteBatchRenderer>();
    }

    std::unique_ptr<IVertexBufferRenderer> StubRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<StubVertexBufferRenderer>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> StubRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<StubIndexBufferRenderer>(index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> StubRenderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<StubIndexBufferRenderer>(index_capacity, true);
    }
}

namespace CNA::Internal::Renderers
{
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Stub { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Stub::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Stub::StubRenderer>(args.virtualWidth, args.virtualHeight);
    }
}
