#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"

#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace CNA::Internal::Renderers::Headless
{
    namespace
    {
        int PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList: return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList: return primitiveCount * 2;
                case PrimitiveType::LineStrip: return primitiveCount + 1;
                case PrimitiveType::PointListEXT: return primitiveCount;
            }
            return 0;
        }

        int PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount)
        {
            return PrimitiveVertexCount(primitive, primitiveCount);
        }

        void Require(const std::shared_ptr<HeadlessSharedState>& state, bool condition, const std::string& message)
        {
            if (state->ValidationEnabled() && !condition)
                throw HeadlessValidationException(message);
        }
    }

    HeadlessMode ParseHeadlessModeFromEnvironment()
    {
        const char* raw = std::getenv("CNA_HEADLESS_MODE");
        if (raw == nullptr)
            return HeadlessMode::Validation;

        std::string value(raw);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (value == "fast") return HeadlessMode::Fast;
        if (value == "trace") return HeadlessMode::Trace;
        if (value == "validation") return HeadlessMode::Validation;
        return HeadlessMode::Validation;
    }

    void HeadlessSharedState::RecordTrace(const std::string& method, const std::string& argsSummary)
    {
        if (!TraceEnabled())
            return;
        HeadlessTraceEntry entry;
        entry.callIndex = nextCallIndex++;
        entry.frameIndex = frameIndex;
        entry.method = method;
        entry.argsSummary = argsSummary;
        traceLog.push_back(std::move(entry));
    }

    std::string HeadlessSharedState::CurrentDebugLabel() const
    {
        // Matches RecordTrace()'s own "only in HeadlessTrace mode" gating -- creation-site labels
        // are a HeadlessTrace-only diagnostic, not something HeadlessValidation/HeadlessFast pay
        // the string-building cost for.
        if (!TraceEnabled() || debugLabelStack.empty())
            return {};
        std::string joined = debugLabelStack.front();
        for (std::size_t i = 1; i < debugLabelStack.size(); ++i)
        {
            joined += '/';
            joined += debugLabelStack[i];
        }
        return joined;
    }

    // ---- HeadlessResourceRegistry ----

    std::uint64_t HeadlessResourceRegistry::Register(const std::string& typeName, const std::string& creationSite)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint64_t id = nextId_++;
        HeadlessResourceRecord record;
        record.id = id;
        record.typeName = typeName;
        record.creationSite = creationSite;
        records_[id] = std::move(record);
        return id;
    }

    void HeadlessResourceRegistry::Unregister(std::uint64_t id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.erase(id);
    }

    std::vector<HeadlessResourceRecord> HeadlessResourceRegistry::AliveResources() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<HeadlessResourceRecord> result;
        result.reserve(records_.size());
        for (const auto& [id, record] : records_)
            result.push_back(record);
        return result;
    }

    std::size_t HeadlessResourceRegistry::AliveCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_.size();
    }

    void HeadlessResourceRegistry::Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.clear();
    }

    // ---- HeadlessVertexBufferRenderer ----

    HeadlessVertexBufferRenderer::HeadlessVertexBufferRenderer(std::shared_ptr<HeadlessSharedState> state, int vertexCapacity)
        : state_(std::move(state)), capacity_(vertexCapacity)
    {
        resourceId_ = state_->registry.Register("VertexBuffer", state_->CurrentDebugLabel());
        state_->stats.vertexBuffersCreated++;
        state_->RecordTrace("CreateVertexBuffer", "capacity=" + std::to_string(vertexCapacity));
    }

    HeadlessVertexBufferRenderer::~HeadlessVertexBufferRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessVertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        Require(state_, vertex_count >= 0 && vertex_count <= capacity_,
               "HeadlessVertexBufferRenderer::SetData: vertex_count " + std::to_string(vertex_count) +
               " exceeds capacity " + std::to_string(capacity_));
        Require(state_, stride_in_bytes > 0, "HeadlessVertexBufferRenderer::SetData: stride_in_bytes must be > 0");

        vertexCount_ = vertex_count;
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        shadowData_.assign(static_cast<const std::uint8_t*>(data),
                          static_cast<const std::uint8_t*>(data) + byteCount);
        state_->RecordTrace("VertexBuffer::SetData", "vertexCount=" + std::to_string(vertex_count));
    }

    void HeadlessVertexBufferRenderer::SetDataWithOptions(const void* data, int vertex_count,
                                                      std::size_t stride_in_bytes, SetDataOptions /*options*/)
    {
        SetData(data, vertex_count, stride_in_bytes);
    }

    // ---- HeadlessIndexBufferRenderer ----

    HeadlessIndexBufferRenderer::HeadlessIndexBufferRenderer(std::shared_ptr<HeadlessSharedState> state, int indexCapacity,
                                                    bool thirtyTwoBit)
        : state_(std::move(state)), capacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
        resourceId_ = state_->registry.Register("IndexBuffer", state_->CurrentDebugLabel());
        state_->stats.indexBuffersCreated++;
        state_->RecordTrace("CreateIndexBuffer", "capacity=" + std::to_string(indexCapacity) +
                            " thirtyTwoBit=" + (thirtyTwoBit ? "true" : "false"));
    }

    HeadlessIndexBufferRenderer::~HeadlessIndexBufferRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessIndexBufferRenderer::Upload(const void* data, int index_count, bool dataIsThirtyTwoBit)
    {
        Require(state_, index_count >= 0 && index_count <= capacity_,
               "HeadlessIndexBufferRenderer: index_count " + std::to_string(index_count) +
               " exceeds capacity " + std::to_string(capacity_));
        Require(state_, dataIsThirtyTwoBit == thirtyTwoBit_,
               std::string("HeadlessIndexBufferRenderer: SetData") + (dataIsThirtyTwoBit ? "32" : "16") +
               " called on a buffer created as " + (thirtyTwoBit_ ? "32-bit" : "16-bit"));

        indexCount_ = index_count;
        const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t byteCount = static_cast<std::size_t>(index_count) * elementSize;
        shadowData_.assign(static_cast<const std::uint8_t*>(data),
                          static_cast<const std::uint8_t*>(data) + byteCount);
        state_->RecordTrace("IndexBuffer::SetData", "indexCount=" + std::to_string(index_count));
    }

    void HeadlessIndexBufferRenderer::SetData16(const void* data, int index_count) { Upload(data, index_count, false); }
    void HeadlessIndexBufferRenderer::SetData32(const void* data, int index_count) { Upload(data, index_count, true); }
    void HeadlessIndexBufferRenderer::SetData16WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, false); }
    void HeadlessIndexBufferRenderer::SetData32WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, true); }

    // ---- HeadlessTextureRenderer ----

    HeadlessTextureRenderer::HeadlessTextureRenderer(std::shared_ptr<HeadlessSharedState> state, const ImageData& data)
        : state_(std::move(state)), width_(data.width), height_(data.height)
    {
        resourceId_ = state_->registry.Register("Texture2D", state_->CurrentDebugLabel());
        state_->stats.texturesCreated++;
        pixels_.assign(data.pixels.begin(), data.pixels.end());
        state_->RecordTrace("CreateTexture", "size=" + std::to_string(width_) + "x" + std::to_string(height_));
    }

    HeadlessTextureRenderer::HeadlessTextureRenderer(std::shared_ptr<HeadlessSharedState> state, int width, int height,
                                           std::string typeNameOverride)
        : state_(std::move(state)), width_(width), height_(height)
    {
        resourceId_ = state_->registry.Register(typeNameOverride, state_->CurrentDebugLabel());
        pixels_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
    }

    HeadlessTextureRenderer::~HeadlessTextureRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        Require(state_, rgba != nullptr, "HeadlessTextureRenderer::UpdatePixels: rgba must not be null");
        const std::size_t rowBytes = static_cast<std::size_t>(width_) * 4u;
        const std::size_t effectiveStride = stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
        pixels_.resize(rowBytes * static_cast<std::size_t>(height_));
        for (int y = 0; y < height_; ++y)
        {
            std::copy(rgba + static_cast<std::size_t>(y) * effectiveStride,
                     rgba + static_cast<std::size_t>(y) * effectiveStride + rowBytes,
                     pixels_.begin() + static_cast<std::ptrdiff_t>(y) * static_cast<std::ptrdiff_t>(rowBytes));
        }
        state_->RecordTrace("Texture2D::UpdatePixels", "");
    }

    void HeadlessTextureRenderer::UpdatePixelsLevel(int /*level*/, const uint8_t* rgba, int levelW, int levelH)
    {
        Require(state_, rgba != nullptr, "HeadlessTextureRenderer::UpdatePixelsLevel: rgba must not be null");
        Require(state_, levelW >= 0 && levelH >= 0, "HeadlessTextureRenderer::UpdatePixelsLevel: negative dimensions");
        state_->RecordTrace("Texture2D::UpdatePixelsLevel", "");
    }

    // ---- HeadlessRenderTargetRenderer ----

    HeadlessRenderTargetRenderer::HeadlessRenderTargetRenderer(std::shared_ptr<HeadlessSharedState> state, int w, int h,
                                                      int depthFormat, bool mipMap, int multiSampleCount)
        : state_(std::move(state)), width_(w), height_(h), depthFormat_(depthFormat),
          mipMap_(mipMap), multiSampleCount_(multiSampleCount)
    {
        resourceId_ = state_->registry.Register("RenderTarget2D", state_->CurrentDebugLabel());
        state_->stats.renderTargetsCreated++;
        pixels_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0u);
        state_->RecordTrace("CreateRenderTarget2D", "size=" + std::to_string(w) + "x" + std::to_string(h));
    }

    HeadlessRenderTargetRenderer::~HeadlessRenderTargetRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessRenderTargetRenderer::UpdatePixels(const uint8_t* rgba, int /*stride*/)
    {
        if (rgba == nullptr) return;
        const std::size_t byteCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
        pixels_.assign(rgba, rgba + byteCount);
    }

    void HeadlessRenderTargetRenderer::BindAsRenderTarget() { bound_ = true; }
    void HeadlessRenderTargetRenderer::UnbindAsRenderTarget() { bound_ = false; }

    // ---- HeadlessRenderTargetCubeRenderer ----

    HeadlessRenderTargetCubeRenderer::HeadlessRenderTargetCubeRenderer(std::shared_ptr<HeadlessSharedState> state, int size,
                                                              int depthFormat, bool mipMap, int multiSampleCount)
        : state_(std::move(state)), size_(size), depthFormat_(depthFormat), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount)
    {
        resourceId_ = state_->registry.Register("RenderTargetCube", state_->CurrentDebugLabel());
        state_->stats.renderTargetCubesCreated++;
        state_->RecordTrace("CreateRenderTargetCube", "size=" + std::to_string(size));
    }

    HeadlessRenderTargetCubeRenderer::~HeadlessRenderTargetCubeRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        Require(state_, face >= 0 && face <= 5,
               "HeadlessRenderTargetCubeRenderer::BindAsRenderTargetFace: face must be 0..5, got " +
               std::to_string(face));
        boundFace_ = face;
    }

    void HeadlessRenderTargetCubeRenderer::UnbindAsRenderTarget() { boundFace_ = -1; }

    // ---- HeadlessTextureCubeRenderer ----

    HeadlessTextureCubeRenderer::HeadlessTextureCubeRenderer(std::shared_ptr<HeadlessSharedState> state, int size, bool mipMap,
                                                    int surfaceFormat)
        : state_(std::move(state)), size_(size), mipMap_(mipMap), surfaceFormat_(surfaceFormat)
    {
        resourceId_ = state_->registry.Register("TextureCube", state_->CurrentDebugLabel());
        state_->stats.textureCubesCreated++;
        // REMED-GFX-130: the six zero-filled face buffers this constructor used to allocate were
        // never written by SetData and existed only to be handed back by GetData, i.e. they were
        // the fabrication itself. GetData now refuses instead, so the storage has no purpose.
        state_->RecordTrace("CreateTextureCube", "size=" + std::to_string(size));
    }

    HeadlessTextureCubeRenderer::~HeadlessTextureCubeRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    bool HeadlessTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                         const void* data, int dataLength)
    {
        Require(state_, face >= 0 && face <= 5,
               "HeadlessTextureCubeRenderer::SetData: face must be 0..5, got " + std::to_string(face));
        Require(state_, level >= 0, "HeadlessTextureCubeRenderer::SetData: level must be >= 0");
        Require(state_, x >= 0 && y >= 0 && w >= 0 && h >= 0,
               "HeadlessTextureCubeRenderer::SetData: negative rectangle");
        Require(state_, dataLength >= 0, "HeadlessTextureCubeRenderer::SetData: negative dataLength");
        state_->RecordTrace("TextureCube::SetData", "face=" + std::to_string(face));
        // REMED-GFX-135: the trace entry above is the whole of what happens here -- no pixel data is
        // stored anywhere, so this call has never been a completed write and now says so.
        (void)data;
        return false;
    }

    // ---- HeadlessTexture3DRenderer ----

    HeadlessTexture3DRenderer::HeadlessTexture3DRenderer(std::shared_ptr<HeadlessSharedState> state, int w, int h, int depth,
                                               bool mipMap, int surfaceFormat)
        : state_(std::move(state)), width_(w), height_(h), depth_(depth), mipMap_(mipMap),
          surfaceFormat_(surfaceFormat)
    {
        resourceId_ = state_->registry.Register("Texture3D", state_->CurrentDebugLabel());
        state_->stats.textures3DCreated++;
        // REMED-GFX-130: same as the cube renderer above -- the zero-filled voxel buffer was never
        // written by SetData and existed only for GetData to return, so it is gone with it.
        state_->RecordTrace("CreateTexture3D", "size=" + std::to_string(w) + "x" + std::to_string(h) +
                            "x" + std::to_string(depth));
    }

    HeadlessTexture3DRenderer::~HeadlessTexture3DRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    bool HeadlessTexture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                       const void* data, int dataLength)
    {
        Require(state_, level >= 0, "HeadlessTexture3DRenderer::SetData: level must be >= 0");
        Require(state_, x >= 0 && y >= 0 && z >= 0 && w >= 0 && h >= 0 && depth >= 0,
               "HeadlessTexture3DRenderer::SetData: negative sub-volume");
        Require(state_, dataLength >= 0, "HeadlessTexture3DRenderer::SetData: negative dataLength");
        state_->RecordTrace("Texture3D::SetData", "");
        // REMED-GFX-135: see HeadlessTextureCubeRenderer::SetData -- trace only, never a store.
        (void)data;
        return false;
    }

    // ---- HeadlessEffectRenderer ----

    HeadlessEffectRenderer::HeadlessEffectRenderer(std::shared_ptr<HeadlessSharedState> state) : state_(std::move(state))
    {
        resourceId_ = state_->registry.Register("Effect", state_->CurrentDebugLabel());
        state_->stats.effectsCreated++;
    }

    HeadlessEffectRenderer::~HeadlessEffectRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    bool HeadlessEffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        // Never actually compiled -- accepts any source string (plan_headless.md HEADLESS-16). A genuinely
        // empty source on both stages is still flagged in Validation/Trace mode since that is
        // never a legitimate custom-effect call from anywhere in this codebase.
        Require(state_, !vertSrc.empty() || !fragSrc.empty(),
               "HeadlessEffectRenderer::CompileProgram: both vertSrc and fragSrc are empty");
        compiled_ = true;
        state_->RecordTrace("Effect::CompileProgram", "");
        return true;
    }

    void HeadlessEffectRenderer::Bind() { bound_ = true; }
    void HeadlessEffectRenderer::Unbind() { bound_ = false; }

    void HeadlessEffectRenderer::SetUniformFloat(const char* name, float value) { uniformValues_[name] = {value}; }
    void HeadlessEffectRenderer::SetUniformInt(const char* name, int value)
    { uniformValues_[name] = {static_cast<float>(value)}; }
    void HeadlessEffectRenderer::SetUniformVec2(const char* name, float x, float y) { uniformValues_[name] = {x, y}; }
    void HeadlessEffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    { uniformValues_[name] = {x, y, z}; }
    void HeadlessEffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    { uniformValues_[name] = {x, y, z, w}; }
    void HeadlessEffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    { uniformValues_[name].assign(matrix, matrix + 16); }
    void HeadlessEffectRenderer::SetUniformFloatArray(const char* name, const float* values, int count)
    { uniformValues_[name].assign(values, values + std::max(0, count)); }
    void HeadlessEffectRenderer::SetUniformVec2Array(const char* name, const float* values, int count)
    { uniformValues_[name].assign(values, values + std::max(0, count) * 2); }
    void HeadlessEffectRenderer::BindTexture(int unit, ITextureRenderer* texture) { boundTextures_[unit] = texture; }

    // ---- HeadlessSpriteBatchRenderer ----

    HeadlessSpriteBatchRenderer::HeadlessSpriteBatchRenderer(std::shared_ptr<HeadlessSharedState> state) : state_(std::move(state))
    {
        resourceId_ = state_->registry.Register("SpriteBatch", state_->CurrentDebugLabel());
        state_->stats.spriteBatchesCreated++;
        state_->RecordTrace("CreateSpriteBatch", "");
    }

    HeadlessSpriteBatchRenderer::~HeadlessSpriteBatchRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessSpriteBatchRenderer::Begin()
    {
        Require(state_, !begun_, "HeadlessSpriteBatchRenderer::Begin: Begin() called without a matching End()");
        begun_ = true;
        lastBatch_.clear();
    }

    void HeadlessSpriteBatchRenderer::End()
    {
        Require(state_, begun_, "HeadlessSpriteBatchRenderer::End: End() called without a matching Begin()");
        begun_ = false;
    }

    void HeadlessSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        HeadlessSpriteDrawRecord record;
        record.texture = &texture;
        record.destinationRectangle = Rectangle(static_cast<int>(x), static_cast<int>(y),
                                                texture.GetWidth(), texture.GetHeight());
        record.sourceRectangle = Rectangle(0, 0, texture.GetWidth(), texture.GetHeight());
        lastBatch_.push_back(record);
        state_->stats.drawCallCount++;
    }

    void HeadlessSpriteBatchRenderer::Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle, const Color& color)
    {
        HeadlessSpriteDrawRecord record;
        record.texture = &texture;
        record.destinationRectangle = destinationRectangle;
        record.sourceRectangle = sourceRectangle;
        record.color = color;
        lastBatch_.push_back(record);
        state_->stats.drawCallCount++;
    }

    void HeadlessSpriteBatchRenderer::Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle, const Color& color, float rotation,
                                      const Vector2& origin, SpriteEffects effects, float layerDepth)
    {
        HeadlessSpriteDrawRecord record;
        record.texture = &texture;
        record.destinationRectangle = destinationRectangle;
        record.sourceRectangle = sourceRectangle;
        record.color = color;
        record.rotation = rotation;
        record.origin = origin;
        record.effects = effects;
        record.layerDepth = layerDepth;
        lastBatch_.push_back(record);
        state_->stats.drawCallCount++;
    }

    // ---- HeadlessOcclusionQueryRenderer ----

    HeadlessOcclusionQueryRenderer::HeadlessOcclusionQueryRenderer(std::shared_ptr<HeadlessSharedState> state)
        : state_(std::move(state))
    {
        resourceId_ = state_->registry.Register("OcclusionQuery", state_->CurrentDebugLabel());
        state_->stats.occlusionQueriesCreated++;
        state_->RecordTrace("CreateOcclusionQuery", "");
    }

    HeadlessOcclusionQueryRenderer::~HeadlessOcclusionQueryRenderer()
    {
        state_->registry.Unregister(resourceId_);
    }

    // ---- HeadlessRenderer ----

    HeadlessRenderer::HeadlessRenderer(int virtualWidth, int virtualHeight)
        : virtualWidth_(virtualWidth), virtualHeight_(virtualHeight)
    {
        state_ = std::make_shared<HeadlessSharedState>();
        state_->mode = ParseHeadlessModeFromEnvironment();
    }

    HeadlessRenderer::~HeadlessRenderer() = default;

    void HeadlessRenderer::Clear(float r, float g, float b, float a)
    {
        clearColor_[0] = r; clearColor_[1] = g; clearColor_[2] = b; clearColor_[3] = a;
        state_->stats.clearCount++;
        state_->RecordTrace("Clear", "");
    }

    void HeadlessRenderer::Present()
    {
        state_->stats.presentCount++;
        state_->statsAtLastPresent = state_->stats;
        state_->frameIndex++;
        state_->RecordTrace("Present", "frameIndex=" + std::to_string(state_->frameIndex));
    }

    void HeadlessRenderer::GetViewportSize(int& width, int& height)
    {
        if (currentRenderTarget_ != nullptr)
        {
            width = currentRenderTarget_->GetWidth();
            height = currentRenderTarget_->GetHeight();
            return;
        }
        width = virtualWidth_ > 0 ? virtualWidth_ : 1024;
        height = virtualHeight_ > 0 ? virtualHeight_ : 768;
    }

    void HeadlessRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void HeadlessRenderer::SetPresentationMode(int /*mode*/) {}

    void HeadlessRenderer::ReadBackbuffer(int /*x*/, int /*y*/, int /*w*/, int /*h*/, uint8_t* /*pixels*/)
    {
        // REMED-GFX-162: Headless rasterizes nothing and owns no backbuffer pixel storage, so it
        // cannot read one back. It formerly fabricated a frame here -- filling the caller's buffer
        // with the last Clear() colour for every pixel -- which made a non-rasterizing device
        // indistinguishable from a real black (or last-cleared) frame. That is exactly the
        // fabricate-rather-than-refuse behaviour REMED-GFX-127/130 removed from the Texture/render-
        // target readbacks: a renderer with no honest pixel result must reject, not invent one.
        //
        // The refusal is raised HERE, after GraphicsDevice::GetBackBufferData has finished all of
        // its argument validation (null destination, rectangle bounds, element count, format), so
        // an invalid request still fails with its own std::invalid_argument/std::out_of_range/
        // std::runtime_error -- capability rejection stays LAST, matching the texture path. The
        // caller's destination is left completely untouched: this throws before GetBackBufferData's
        // unpack loop writes a single element.
        throw System::NotSupportedException(
            "GraphicsDevice::GetBackBufferData: the Headless renderer does not rasterize and has no "
            "backbuffer pixel storage to read back.");
    }

    std::unique_ptr<ITextureRenderer> HeadlessRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<HeadlessTextureRenderer>(state_, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> HeadlessRenderer::CreateSpriteBatch()
    {
        return std::make_unique<HeadlessSpriteBatchRenderer>(state_);
    }

    std::unique_ptr<IOcclusionQueryRenderer> HeadlessRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<HeadlessOcclusionQueryRenderer>(state_);
    }

    std::unique_ptr<ITexture3DRenderer> HeadlessRenderer::CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                                            int surfaceFormat)
    {
        return std::make_unique<HeadlessTexture3DRenderer>(state_, w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITextureCubeRenderer> HeadlessRenderer::CreateTextureCube(int size, bool mipMap,
                                                                                int surfaceFormat)
    {
        return std::make_unique<HeadlessTextureCubeRenderer>(state_, size, mipMap, surfaceFormat);
    }

    std::unique_ptr<IRenderTargetRenderer> HeadlessRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<HeadlessRenderTargetRenderer>(state_, w, h, depthFormat, mipMap, multiSampleCount);
    }

    void HeadlessRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->UnbindAsRenderTarget();
        currentRenderTarget_ = static_cast<HeadlessRenderTargetRenderer*>(rt);
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->BindAsRenderTarget();
    }

    std::unique_ptr<IRenderTargetCubeRenderer> HeadlessRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // REMED-GFX-136: consumed by being deliberately unused, exactly like this renderer's own
        // CreateRenderTarget2D above. Nothing is ever rasterized here, so there is no face content
        // either to preserve or to discard -- REMED-GFX-130 already made the readback say so
        // instead of fabricating one.
        (void) preserveContents;
        return std::make_unique<HeadlessRenderTargetCubeRenderer>(state_, size, depthFormat, mipMap, multiSampleCount);
    }

    void HeadlessRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (!renderTargets || count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count > 1)
            throw std::runtime_error(
                "HeadlessRenderer does not execute multiple simultaneous render targets.");
        if (renderTargets[0].IsRenderTargetCubeFace())
            SetRenderTargetCubeFace(
                renderTargets[0].GetRenderTargetCube(),
                renderTargets[0].GetCubeFace());
        else
            SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
    }

    std::unique_ptr<IEffectRenderer> HeadlessRenderer::CreateEffectRenderer(const std::string& vertSrc,
                                                                              const std::string& fragSrc)
    {
        auto effect = std::make_unique<HeadlessEffectRenderer>(state_);
        effect->CompileProgram(vertSrc, fragSrc);
        return effect;
    }

    bool HeadlessRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            // REMED-CONTENT-004: HeadlessTexture3DRenderer validates arguments and records a trace
            // but never actually stores pixel data -- Headless has no real GPU resource of any
            // kind by design. Reported here so Texture3D's own constructor can fail cleanly instead
            // of silently discarding every SetData()/GetData() call.
            case CNA::GraphicsCapability::Texture3D:
            case CNA::GraphicsCapability::AdditiveBlending:
                return false;
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                // REMED-GFX-201: Headless rasterizes nothing, so claiming this would fabricate
                // success for a draw no other renderer of its kind can perform. Its trace still
                // records the complete binding set, which is what a Headless assertion checks.
                return false;
            default:
                return true;
        }
    }

    void HeadlessRenderer::ApplyBlendState(int, int, int, int, int, int,
                                                  const BlendWriteState& writeState)
    {
        state_->stats.blendStateChangeCount++;
        // REMED-GFX-077: Headless renders nothing, but it now records the write state into the
        // trace payload so tests can assert the ColorWriteChannels/MultiSampleMask actually
        // reached the renderer (previously the trace string was empty).
        char buf[96];
        std::snprintf(buf, sizeof(buf), "cw=%d,%d,%d,%d msm=0x%08X",
                      writeState.colorWriteChannels[0], writeState.colorWriteChannels[1],
                      writeState.colorWriteChannels[2], writeState.colorWriteChannels[3],
                      writeState.multiSampleMask);
        state_->RecordTrace("ApplyBlendState", buf);
    }

    void HeadlessRenderer::ApplyDepthStencilState(bool, bool, int, bool, int, int, int, int, int, int, int,
                                                      bool, int, int, int, int)
    {
        state_->stats.depthStencilStateChangeCount++;
        state_->RecordTrace("ApplyDepthStencilState", "");
    }

    void HeadlessRenderer::ApplyRasterizerState(int, int, bool, float, float)
    {
        state_->stats.rasterizerStateChangeCount++;
        state_->RecordTrace("ApplyRasterizerState", "");
    }

    void HeadlessRenderer::ApplySamplerState(int slot, int, int, int, int)
    {
        Require(state_, slot >= 0 && slot < 16,
               "HeadlessRenderer::ApplySamplerState: slot must be 0..15, got " + std::to_string(slot));
        state_->stats.samplerStateChangeCount++;
        state_->RecordTrace("ApplySamplerState", "slot=" + std::to_string(slot));
    }

    void HeadlessRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        Require(state_, w >= 0 && h >= 0,
               "HeadlessRenderer::SetScissorRect: negative width/height (" + std::to_string(w) + "x" +
               std::to_string(h) + ")");
        Require(state_, x >= 0 && y >= 0,
               "HeadlessRenderer::SetScissorRect: negative origin (" + std::to_string(x) + "," +
               std::to_string(y) + ")");
        // HEADLESS-23: cross-reference the currently-bound target's real size (GetViewportSize()
        // already resolves to the bound RenderTarget2D's size, or the virtual/default backbuffer
        // size when none is bound -- reused rather than duplicating that resolution logic). Skipped
        // in HeadlessFast (via ValidationEnabled()) so this renderer doesn't pay for a
        // GetViewportSize() call on every scissor change when validation is off.
        if (state_->ValidationEnabled())
        {
            int targetWidth = 0, targetHeight = 0;
            GetViewportSize(targetWidth, targetHeight);
            Require(state_, x + w <= targetWidth && y + h <= targetHeight,
                   "HeadlessRenderer::SetScissorRect: rectangle (" + std::to_string(x) + "," +
                   std::to_string(y) + "," + std::to_string(w) + "x" + std::to_string(h) +
                   ") exceeds the current target's bounds (" + std::to_string(targetWidth) + "x" +
                   std::to_string(targetHeight) + ")");
        }
        state_->stats.scissorChangeCount++;
        state_->RecordTrace("SetScissorRect", std::to_string(x) + "," + std::to_string(y) + "," +
                            std::to_string(w) + "x" + std::to_string(h));
    }

    void HeadlessRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        Require(state_, w >= 0 && h >= 0,
               "HeadlessRenderer::SetViewport: negative width/height (" + std::to_string(w) + "x" +
               std::to_string(h) + ")");
        Require(state_, x >= 0 && y >= 0,
               "HeadlessRenderer::SetViewport: negative origin (" + std::to_string(x) + "," +
               std::to_string(y) + ")");
        Require(state_, minDepth <= maxDepth,
               "HeadlessRenderer::SetViewport: minDepth must be <= maxDepth");
        if (state_->ValidationEnabled())
        {
            int targetWidth = 0, targetHeight = 0;
            GetViewportSize(targetWidth, targetHeight);
            Require(state_, x + w <= targetWidth && y + h <= targetHeight,
                   "HeadlessRenderer::SetViewport: rectangle (" + std::to_string(x) + "," +
                   std::to_string(y) + "," + std::to_string(w) + "x" + std::to_string(h) +
                   ") exceeds the current target's bounds (" + std::to_string(targetWidth) + "x" +
                   std::to_string(targetHeight) + ")");
        }
        state_->stats.viewportChangeCount++;
        state_->RecordTrace("SetViewport", std::to_string(x) + "," + std::to_string(y) + "," +
                            std::to_string(w) + "x" + std::to_string(h));
    }

    void HeadlessRenderer::ClearColorAndDepth(float r, float g, float b, float a, float /*depth*/)
    { Clear(r, g, b, a); }
    void HeadlessRenderer::ClearDepth(float /*depth*/)
    {
        state_->stats.clearCount++;
        state_->RecordTrace("ClearDepth", "");
    }
    void HeadlessRenderer::ClearStencil(int /*stencil*/)
    {
        state_->stats.clearCount++;
        state_->RecordTrace("ClearStencil", "");
    }
    void HeadlessRenderer::ClearDepthAndStencil(float /*depth*/, int /*stencil*/)
    {
        state_->stats.clearCount++;
        state_->RecordTrace("ClearDepthAndStencil", "");
    }
    void HeadlessRenderer::ClearColorAndStencil(float r, float g, float b, float a, int /*stencil*/)
    { Clear(r, g, b, a); }
    void HeadlessRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float /*depth*/,
                                                        int /*stencil*/)
    { Clear(r, g, b, a); }

    void HeadlessRenderer::SetDepthTestEnabled(bool enabled)
    {
        state_->stats.depthStencilStateChangeCount++;
        state_->RecordTrace("SetDepthTestEnabled", enabled ? "true" : "false");
    }
    void HeadlessRenderer::SetBlendEnabled(bool enabled)
    {
        state_->stats.blendStateChangeCount++;
        state_->RecordTrace("SetBlendEnabled", enabled ? "true" : "false");
    }
    void HeadlessRenderer::SetDepthWriteEnabled(bool enabled)
    {
        state_->stats.depthStencilStateChangeCount++;
        state_->RecordTrace("SetDepthWriteEnabled", enabled ? "true" : "false");
    }

    std::unique_ptr<IVertexBufferRenderer> HeadlessRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<HeadlessVertexBufferRenderer>(state_, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> HeadlessRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<HeadlessIndexBufferRenderer>(state_, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> HeadlessRenderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<HeadlessIndexBufferRenderer>(state_, index_capacity, true);
    }

    void HeadlessRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix&, const Matrix&,
                                                     const Matrix&, PrimitiveType primitive, int primitiveCount)
    {
        Require(state_, primitiveCount > 0,
               "HeadlessRenderer::DrawColoredPrimitives: primitiveCount must be > 0, got " +
               std::to_string(primitiveCount));
        const int neededVertices = PrimitiveVertexCount(primitive, primitiveCount);
        Require(state_, neededVertices <= vb.GetVertexCount(),
               "HeadlessRenderer::DrawColoredPrimitives: primitiveCount " + std::to_string(primitiveCount) +
               " needs " + std::to_string(neededVertices) + " vertices but the bound buffer only has " +
               std::to_string(vb.GetVertexCount()));
        state_->stats.drawCallCount++;
        state_->stats.primitiveCount += static_cast<std::uint64_t>(primitiveCount);
        state_->RecordTrace("DrawColoredPrimitives", "primitiveCount=" + std::to_string(primitiveCount));
    }

    void HeadlessRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer& ib,
                                                            const Matrix&, const Matrix&, const Matrix&,
                                                            PrimitiveType primitive, int primitiveCount)
    {
        Require(state_, primitiveCount > 0,
               "HeadlessRenderer::DrawIndexedColoredPrimitives: primitiveCount must be > 0, got " +
               std::to_string(primitiveCount));
        const int neededIndices = PrimitiveIndexCount(primitive, primitiveCount);
        Require(state_, neededIndices <= ib.GetIndexCount(),
               "HeadlessRenderer::DrawIndexedColoredPrimitives: primitiveCount " +
               std::to_string(primitiveCount) + " needs " + std::to_string(neededIndices) +
               " indices but the bound buffer only has " + std::to_string(ib.GetIndexCount()));
        state_->stats.drawCallCount++;
        state_->stats.primitiveCount += static_cast<std::uint64_t>(primitiveCount);
        state_->RecordTrace("DrawIndexedColoredPrimitives", "primitiveCount=" + std::to_string(primitiveCount));
    }

    void HeadlessRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world,
                                               const Matrix& view, const Matrix& projection,
                                               PrimitiveType primitive, int primitiveCount,
                                               const GpuDrawParams& params)
    {
        Require(state_, !(params.textureEnabled && params.texture0 == nullptr),
               "HeadlessRenderer::DrawPrimitivesEx: TextureEnabled=true but texture0 is null");
        Require(state_, !(params.dualTexture && params.texture1 == nullptr),
               "HeadlessRenderer::DrawPrimitivesEx: DualTexture=true but texture1 is null");
        Require(state_, !(params.envMapping && params.envMap == nullptr),
               "HeadlessRenderer::DrawPrimitivesEx: EnvMapping=true but envMap is null");
        Require(state_, !(params.skinned && params.boneCount <= 0),
               "HeadlessRenderer::DrawPrimitivesEx: Skinned=true but boneCount <= 0");
        DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
    }

    void HeadlessRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                       const Matrix& world, const Matrix& view,
                                                       const Matrix& projection, PrimitiveType primitive,
                                                       int primitiveCount, const GpuDrawParams& params)
    {
        Require(state_, !(params.textureEnabled && params.texture0 == nullptr),
               "HeadlessRenderer::DrawIndexedPrimitivesEx: TextureEnabled=true but texture0 is null");
        Require(state_, !(params.dualTexture && params.texture1 == nullptr),
               "HeadlessRenderer::DrawIndexedPrimitivesEx: DualTexture=true but texture1 is null");
        Require(state_, !(params.envMapping && params.envMap == nullptr),
               "HeadlessRenderer::DrawIndexedPrimitivesEx: EnvMapping=true but envMap is null");
        Require(state_, !(params.skinned && params.boneCount <= 0),
               "HeadlessRenderer::DrawIndexedPrimitivesEx: Skinned=true but boneCount <= 0");
        DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
    }

    void HeadlessRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                        const Matrix& world, const Matrix& view,
                                                        const Matrix& projection, PrimitiveType primitive,
                                                        int primitiveCount, int instanceCount,
                                                        const GpuDrawParams& params)
    {
        Require(state_, instanceCount > 0,
               "HeadlessRenderer::DrawInstancedPrimitivesEx: instanceCount must be > 0, got " +
               std::to_string(instanceCount));
        DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount, params);
        state_->stats.drawCallCount += static_cast<std::uint64_t>(instanceCount - 1);
    }

    HeadlessStatistics HeadlessRenderer::GetLastFrameStatistics() const
    {
        const HeadlessStatistics& current = state_->stats;
        const HeadlessStatistics& previous = state_->statsAtLastPresent;
        HeadlessStatistics diff;
        diff.drawCallCount = current.drawCallCount - previous.drawCallCount;
        diff.primitiveCount = current.primitiveCount - previous.primitiveCount;
        diff.clearCount = current.clearCount - previous.clearCount;
        diff.presentCount = current.presentCount - previous.presentCount;
        diff.blendStateChangeCount = current.blendStateChangeCount - previous.blendStateChangeCount;
        diff.depthStencilStateChangeCount = current.depthStencilStateChangeCount - previous.depthStencilStateChangeCount;
        diff.rasterizerStateChangeCount = current.rasterizerStateChangeCount - previous.rasterizerStateChangeCount;
        diff.samplerStateChangeCount = current.samplerStateChangeCount - previous.samplerStateChangeCount;
        diff.viewportChangeCount = current.viewportChangeCount - previous.viewportChangeCount;
        diff.scissorChangeCount = current.scissorChangeCount - previous.scissorChangeCount;
        diff.vertexBuffersCreated = current.vertexBuffersCreated - previous.vertexBuffersCreated;
        diff.indexBuffersCreated = current.indexBuffersCreated - previous.indexBuffersCreated;
        diff.texturesCreated = current.texturesCreated - previous.texturesCreated;
        diff.textureCubesCreated = current.textureCubesCreated - previous.textureCubesCreated;
        diff.textures3DCreated = current.textures3DCreated - previous.textures3DCreated;
        diff.renderTargetsCreated = current.renderTargetsCreated - previous.renderTargetsCreated;
        diff.renderTargetCubesCreated = current.renderTargetCubesCreated - previous.renderTargetCubesCreated;
        diff.effectsCreated = current.effectsCreated - previous.effectsCreated;
        diff.spriteBatchesCreated = current.spriteBatchesCreated - previous.spriteBatchesCreated;
        diff.occlusionQueriesCreated = current.occlusionQueriesCreated - previous.occlusionQueriesCreated;
        return diff;
    }

    void HeadlessRenderer::AssertNoLeaks() const
    {
        const std::vector<HeadlessResourceRecord> alive = state_->registry.AliveResources();
        if (alive.empty())
            return;

        std::ostringstream message;
        message << "HeadlessRenderer::AssertNoLeaks: " << alive.size() << " resource(s) still alive:\n";
        for (const HeadlessResourceRecord& record : alive)
        {
            message << "  #" << record.id << " " << record.typeName;
            if (!record.creationSite.empty())
                message << " (created at " << record.creationSite << ")";
            message << "\n";
        }
        throw HeadlessValidationException(message.str());
    }

    std::string HeadlessRenderer::FormatTraceLog() const
    {
        std::ostringstream out;
        for (const HeadlessTraceEntry& entry : state_->traceLog)
        {
            out << "[frame " << entry.frameIndex << " #" << entry.callIndex << "] " << entry.method;
            if (!entry.argsSummary.empty())
                out << ": " << entry.argsSummary;
            out << "\n";
        }
        return out.str();
    }

    void HeadlessRenderer::DumpTraceLog(std::FILE* out) const
    {
        const std::string text = FormatTraceLog();
        std::fwrite(text.data(), 1, text.size(), out);
    }

    HeadlessTraceLogDiff CompareTraceLogs(const std::vector<HeadlessTraceEntry>& baseline,
                                          const std::vector<HeadlessTraceEntry>& current)
    {
        const std::size_t n = std::min(baseline.size(), current.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            const HeadlessTraceEntry& a = baseline[i];
            const HeadlessTraceEntry& b = current[i];
            if (a.frameIndex != b.frameIndex || a.method != b.method || a.argsSummary != b.argsSummary)
                return HeadlessTraceLogDiff{false, i};
        }
        if (baseline.size() != current.size())
            return HeadlessTraceLogDiff{false, n};
        return HeadlessTraceLogDiff{true, 0};
    }

    std::string FormatTraceLogDiff(const std::vector<HeadlessTraceEntry>& baseline,
                                   const std::vector<HeadlessTraceEntry>& current)
    {
        const HeadlessTraceLogDiff diff = CompareTraceLogs(baseline, current);
        std::ostringstream out;
        if (diff.identical)
        {
            out << "Trace logs are identical (" << baseline.size() << " entries).\n";
            return out.str();
        }

        auto formatEntry = [](const std::vector<HeadlessTraceEntry>& log, std::size_t i) -> std::string {
            if (i >= log.size())
                return "<end of log>";
            const HeadlessTraceEntry& e = log[i];
            std::ostringstream s;
            s << "[frame " << e.frameIndex << " #" << e.callIndex << "] " << e.method;
            if (!e.argsSummary.empty())
                s << ": " << e.argsSummary;
            return s.str();
        };

        out << "Trace logs diverge at entry #" << diff.firstDivergingIndex << ":\n";
        out << "  baseline: " << formatEntry(baseline, diff.firstDivergingIndex) << "\n";
        out << "  current:  " << formatEntry(current, diff.firstDivergingIndex) << "\n";
        if (baseline.size() != current.size())
            out << "  (baseline has " << baseline.size() << " entries, current has " << current.size() << ")\n";
        return out.str();
    }
}

namespace CNA::Internal::Renderers
{
    // plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Headless { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Headless::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Headless::HeadlessRenderer>(args.virtualWidth, args.virtualHeight);
    }
}
