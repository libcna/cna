#include "CNA/Internal/Renderers/Stub/StubRenderer.hpp"

#include "CNA/Internal/Renderers/Common/NoOp3DResources.hpp"

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

    std::unique_ptr<ITexture3DRenderer> StubRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        // CNA-DEMOS-ISSUE-104: WarnAndStub promises a usable no-op resource; the inherited
        // nullptr left Texture3D refusing its first SetData instead.
        if (!ShouldStubUnsupported3DResource())
        {
            return IGraphicsRenderer::CreateTexture3D(w, h, depth, mipMap, surfaceFormat);
        }
        HandleUnsupported3DCall("STUB", "CreateTexture3D");
        return std::make_unique<NoOpTexture3DRenderer>(w, h, depth);
    }
}

namespace CNA::Internal::Renderers
{
    // docs/runtime-renderer-selection.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Stub { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Stub::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Stub::StubRenderer>(args.virtualWidth, args.virtualHeight);
    }
}
