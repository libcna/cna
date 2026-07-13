#include "CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace CNA::Internal::Backends::Headless
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

    // ---- HeadlessVertexBufferBackend ----

    HeadlessVertexBufferBackend::HeadlessVertexBufferBackend(std::shared_ptr<HeadlessSharedState> state, int vertexCapacity)
        : state_(std::move(state)), capacity_(vertexCapacity)
    {
        resourceId_ = state_->registry.Register("VertexBuffer", state_->CurrentDebugLabel());
        state_->stats.vertexBuffersCreated++;
        state_->RecordTrace("CreateVertexBuffer", "capacity=" + std::to_string(vertexCapacity));
    }

    HeadlessVertexBufferBackend::~HeadlessVertexBufferBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessVertexBufferBackend::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        Require(state_, vertex_count >= 0 && vertex_count <= capacity_,
               "HeadlessVertexBufferBackend::SetData: vertex_count " + std::to_string(vertex_count) +
               " exceeds capacity " + std::to_string(capacity_));
        Require(state_, stride_in_bytes > 0, "HeadlessVertexBufferBackend::SetData: stride_in_bytes must be > 0");

        vertexCount_ = vertex_count;
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        shadowData_.assign(static_cast<const std::uint8_t*>(data),
                          static_cast<const std::uint8_t*>(data) + byteCount);
        state_->RecordTrace("VertexBuffer::SetData", "vertexCount=" + std::to_string(vertex_count));
    }

    void HeadlessVertexBufferBackend::SetDataWithOptions(const void* data, int vertex_count,
                                                      std::size_t stride_in_bytes, SetDataOptions /*options*/)
    {
        SetData(data, vertex_count, stride_in_bytes);
    }

    // ---- HeadlessIndexBufferBackend ----

    HeadlessIndexBufferBackend::HeadlessIndexBufferBackend(std::shared_ptr<HeadlessSharedState> state, int indexCapacity,
                                                    bool thirtyTwoBit)
        : state_(std::move(state)), capacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
        resourceId_ = state_->registry.Register("IndexBuffer", state_->CurrentDebugLabel());
        state_->stats.indexBuffersCreated++;
        state_->RecordTrace("CreateIndexBuffer", "capacity=" + std::to_string(indexCapacity) +
                            " thirtyTwoBit=" + (thirtyTwoBit ? "true" : "false"));
    }

    HeadlessIndexBufferBackend::~HeadlessIndexBufferBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessIndexBufferBackend::Upload(const void* data, int index_count, bool dataIsThirtyTwoBit)
    {
        Require(state_, index_count >= 0 && index_count <= capacity_,
               "HeadlessIndexBufferBackend: index_count " + std::to_string(index_count) +
               " exceeds capacity " + std::to_string(capacity_));
        Require(state_, dataIsThirtyTwoBit == thirtyTwoBit_,
               std::string("HeadlessIndexBufferBackend: SetData") + (dataIsThirtyTwoBit ? "32" : "16") +
               " called on a buffer created as " + (thirtyTwoBit_ ? "32-bit" : "16-bit"));

        indexCount_ = index_count;
        const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t byteCount = static_cast<std::size_t>(index_count) * elementSize;
        shadowData_.assign(static_cast<const std::uint8_t*>(data),
                          static_cast<const std::uint8_t*>(data) + byteCount);
        state_->RecordTrace("IndexBuffer::SetData", "indexCount=" + std::to_string(index_count));
    }

    void HeadlessIndexBufferBackend::SetData16(const void* data, int index_count) { Upload(data, index_count, false); }
    void HeadlessIndexBufferBackend::SetData32(const void* data, int index_count) { Upload(data, index_count, true); }
    void HeadlessIndexBufferBackend::SetData16WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, false); }
    void HeadlessIndexBufferBackend::SetData32WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, true); }

    // ---- HeadlessTextureBackend ----

    HeadlessTextureBackend::HeadlessTextureBackend(std::shared_ptr<HeadlessSharedState> state, const ImageData& data)
        : state_(std::move(state)), width_(data.width), height_(data.height)
    {
        resourceId_ = state_->registry.Register("Texture2D", state_->CurrentDebugLabel());
        state_->stats.texturesCreated++;
        pixels_.assign(data.pixels.begin(), data.pixels.end());
        state_->RecordTrace("CreateTexture", "size=" + std::to_string(width_) + "x" + std::to_string(height_));
    }

    HeadlessTextureBackend::HeadlessTextureBackend(std::shared_ptr<HeadlessSharedState> state, int width, int height,
                                           std::string typeNameOverride)
        : state_(std::move(state)), width_(width), height_(height)
    {
        resourceId_ = state_->registry.Register(typeNameOverride, state_->CurrentDebugLabel());
        pixels_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
    }

    HeadlessTextureBackend::~HeadlessTextureBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        Require(state_, rgba != nullptr, "HeadlessTextureBackend::UpdatePixels: rgba must not be null");
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

    void HeadlessTextureBackend::UpdatePixelsLevel(int /*level*/, const uint8_t* rgba, int levelW, int levelH)
    {
        Require(state_, rgba != nullptr, "HeadlessTextureBackend::UpdatePixelsLevel: rgba must not be null");
        Require(state_, levelW >= 0 && levelH >= 0, "HeadlessTextureBackend::UpdatePixelsLevel: negative dimensions");
        state_->RecordTrace("Texture2D::UpdatePixelsLevel", "");
    }

    // ---- HeadlessRenderTargetBackend ----

    HeadlessRenderTargetBackend::HeadlessRenderTargetBackend(std::shared_ptr<HeadlessSharedState> state, int w, int h,
                                                      int depthFormat, bool mipMap, int multiSampleCount)
        : state_(std::move(state)), width_(w), height_(h), depthFormat_(depthFormat),
          mipMap_(mipMap), multiSampleCount_(multiSampleCount)
    {
        resourceId_ = state_->registry.Register("RenderTarget2D", state_->CurrentDebugLabel());
        state_->stats.renderTargetsCreated++;
        pixels_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0u);
        state_->RecordTrace("CreateRenderTarget2D", "size=" + std::to_string(w) + "x" + std::to_string(h));
    }

    HeadlessRenderTargetBackend::~HeadlessRenderTargetBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessRenderTargetBackend::UpdatePixels(const uint8_t* rgba, int /*stride*/)
    {
        if (rgba == nullptr) return;
        const std::size_t byteCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
        pixels_.assign(rgba, rgba + byteCount);
    }

    void HeadlessRenderTargetBackend::BindAsRenderTarget() { bound_ = true; }
    void HeadlessRenderTargetBackend::UnbindAsRenderTarget() { bound_ = false; }

    // ---- HeadlessRenderTargetCubeBackend ----

    HeadlessRenderTargetCubeBackend::HeadlessRenderTargetCubeBackend(std::shared_ptr<HeadlessSharedState> state, int size,
                                                              int depthFormat, bool mipMap, int multiSampleCount)
        : state_(std::move(state)), size_(size), depthFormat_(depthFormat), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount)
    {
        resourceId_ = state_->registry.Register("RenderTargetCube", state_->CurrentDebugLabel());
        state_->stats.renderTargetCubesCreated++;
        state_->RecordTrace("CreateRenderTargetCube", "size=" + std::to_string(size));
    }

    HeadlessRenderTargetCubeBackend::~HeadlessRenderTargetCubeBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessRenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        Require(state_, face >= 0 && face <= 5,
               "HeadlessRenderTargetCubeBackend::BindAsRenderTargetFace: face must be 0..5, got " +
               std::to_string(face));
        boundFace_ = face;
    }

    void HeadlessRenderTargetCubeBackend::UnbindAsRenderTarget() { boundFace_ = -1; }

    // ---- HeadlessTextureCubeBackend ----

    HeadlessTextureCubeBackend::HeadlessTextureCubeBackend(std::shared_ptr<HeadlessSharedState> state, int size, bool mipMap,
                                                    int surfaceFormat)
        : state_(std::move(state)), size_(size), mipMap_(mipMap), surfaceFormat_(surfaceFormat)
    {
        resourceId_ = state_->registry.Register("TextureCube", state_->CurrentDebugLabel());
        state_->stats.textureCubesCreated++;
        for (auto& face : facePixels_)
            face.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u, 0u);
        state_->RecordTrace("CreateTextureCube", "size=" + std::to_string(size));
    }

    HeadlessTextureCubeBackend::~HeadlessTextureCubeBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                         const void* data, int dataLength)
    {
        Require(state_, face >= 0 && face <= 5,
               "HeadlessTextureCubeBackend::SetData: face must be 0..5, got " + std::to_string(face));
        Require(state_, level >= 0, "HeadlessTextureCubeBackend::SetData: level must be >= 0");
        Require(state_, x >= 0 && y >= 0 && w >= 0 && h >= 0,
               "HeadlessTextureCubeBackend::SetData: negative rectangle");
        Require(state_, dataLength >= 0, "HeadlessTextureCubeBackend::SetData: negative dataLength");
        state_->RecordTrace("TextureCube::SetData", "face=" + std::to_string(face));
    }

    void HeadlessTextureCubeBackend::GetData(int face, int /*level*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                                         void* data, int dataLength) const
    {
        if (data == nullptr || dataLength <= 0) return;
        std::fill_n(static_cast<std::uint8_t*>(data), dataLength, std::uint8_t{0});
    }

    // ---- HeadlessTexture3DBackend ----

    HeadlessTexture3DBackend::HeadlessTexture3DBackend(std::shared_ptr<HeadlessSharedState> state, int w, int h, int depth,
                                               bool mipMap, int surfaceFormat)
        : state_(std::move(state)), width_(w), height_(h), depth_(depth), mipMap_(mipMap),
          surfaceFormat_(surfaceFormat)
    {
        resourceId_ = state_->registry.Register("Texture3D", state_->CurrentDebugLabel());
        state_->stats.textures3DCreated++;
        voxels_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                       static_cast<std::size_t>(depth) * 4u, 0u);
        state_->RecordTrace("CreateTexture3D", "size=" + std::to_string(w) + "x" + std::to_string(h) +
                            "x" + std::to_string(depth));
    }

    HeadlessTexture3DBackend::~HeadlessTexture3DBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessTexture3DBackend::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                       const void* data, int dataLength)
    {
        Require(state_, level >= 0, "HeadlessTexture3DBackend::SetData: level must be >= 0");
        Require(state_, x >= 0 && y >= 0 && z >= 0 && w >= 0 && h >= 0 && depth >= 0,
               "HeadlessTexture3DBackend::SetData: negative sub-volume");
        Require(state_, dataLength >= 0, "HeadlessTexture3DBackend::SetData: negative dataLength");
        state_->RecordTrace("Texture3D::SetData", "");
    }

    void HeadlessTexture3DBackend::GetData(int, int, int, int, int, int, int, void* data, int dataLength) const
    {
        if (data == nullptr || dataLength <= 0) return;
        std::fill_n(static_cast<std::uint8_t*>(data), dataLength, std::uint8_t{0});
    }

    // ---- HeadlessEffectBackend ----

    HeadlessEffectBackend::HeadlessEffectBackend(std::shared_ptr<HeadlessSharedState> state) : state_(std::move(state))
    {
        resourceId_ = state_->registry.Register("Effect", state_->CurrentDebugLabel());
        state_->stats.effectsCreated++;
    }

    HeadlessEffectBackend::~HeadlessEffectBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    bool HeadlessEffectBackend::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        // Never actually compiled -- accepts any source string (plan_headless.md HEADLESS-16). A genuinely
        // empty source on both stages is still flagged in Validation/Trace mode since that is
        // never a legitimate custom-effect call from anywhere in this codebase.
        Require(state_, !vertSrc.empty() || !fragSrc.empty(),
               "HeadlessEffectBackend::CompileProgram: both vertSrc and fragSrc are empty");
        compiled_ = true;
        state_->RecordTrace("Effect::CompileProgram", "");
        return true;
    }

    void HeadlessEffectBackend::Bind() { bound_ = true; }
    void HeadlessEffectBackend::Unbind() { bound_ = false; }

    void HeadlessEffectBackend::SetUniformFloat(const char* name, float value) { uniformValues_[name] = {value}; }
    void HeadlessEffectBackend::SetUniformInt(const char* name, int value)
    { uniformValues_[name] = {static_cast<float>(value)}; }
    void HeadlessEffectBackend::SetUniformVec2(const char* name, float x, float y) { uniformValues_[name] = {x, y}; }
    void HeadlessEffectBackend::SetUniformVec3(const char* name, float x, float y, float z)
    { uniformValues_[name] = {x, y, z}; }
    void HeadlessEffectBackend::SetUniformVec4(const char* name, float x, float y, float z, float w)
    { uniformValues_[name] = {x, y, z, w}; }
    void HeadlessEffectBackend::SetUniformMat4(const char* name, const float* matrix)
    { uniformValues_[name].assign(matrix, matrix + 16); }
    void HeadlessEffectBackend::SetUniformFloatArray(const char* name, const float* values, int count)
    { uniformValues_[name].assign(values, values + std::max(0, count)); }
    void HeadlessEffectBackend::SetUniformVec2Array(const char* name, const float* values, int count)
    { uniformValues_[name].assign(values, values + std::max(0, count) * 2); }
    void HeadlessEffectBackend::BindTexture(int unit, ITextureBackend* texture) { boundTextures_[unit] = texture; }

    // ---- HeadlessSpriteBatchBackend ----

    HeadlessSpriteBatchBackend::HeadlessSpriteBatchBackend(std::shared_ptr<HeadlessSharedState> state) : state_(std::move(state))
    {
        resourceId_ = state_->registry.Register("SpriteBatch", state_->CurrentDebugLabel());
        state_->stats.spriteBatchesCreated++;
        state_->RecordTrace("CreateSpriteBatch", "");
    }

    HeadlessSpriteBatchBackend::~HeadlessSpriteBatchBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    void HeadlessSpriteBatchBackend::Begin()
    {
        Require(state_, !begun_, "HeadlessSpriteBatchBackend::Begin: Begin() called without a matching End()");
        begun_ = true;
        lastBatch_.clear();
    }

    void HeadlessSpriteBatchBackend::End()
    {
        Require(state_, begun_, "HeadlessSpriteBatchBackend::End: End() called without a matching Begin()");
        begun_ = false;
    }

    void HeadlessSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        HeadlessSpriteDrawRecord record;
        record.texture = &texture;
        record.destinationRectangle = Rectangle(static_cast<int>(x), static_cast<int>(y),
                                                texture.GetWidth(), texture.GetHeight());
        record.sourceRectangle = Rectangle(0, 0, texture.GetWidth(), texture.GetHeight());
        lastBatch_.push_back(record);
        state_->stats.drawCallCount++;
    }

    void HeadlessSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
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

    void HeadlessSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
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

    // ---- HeadlessOcclusionQueryBackend ----

    HeadlessOcclusionQueryBackend::HeadlessOcclusionQueryBackend(std::shared_ptr<HeadlessSharedState> state)
        : state_(std::move(state))
    {
        resourceId_ = state_->registry.Register("OcclusionQuery", state_->CurrentDebugLabel());
        state_->stats.occlusionQueriesCreated++;
        state_->RecordTrace("CreateOcclusionQuery", "");
    }

    HeadlessOcclusionQueryBackend::~HeadlessOcclusionQueryBackend()
    {
        state_->registry.Unregister(resourceId_);
    }

    // ---- HeadlessGraphicsBackend ----

    HeadlessGraphicsBackend::HeadlessGraphicsBackend(int virtualWidth, int virtualHeight)
        : virtualWidth_(virtualWidth), virtualHeight_(virtualHeight)
    {
        state_ = std::make_shared<HeadlessSharedState>();
        state_->mode = ParseHeadlessModeFromEnvironment();
    }

    HeadlessGraphicsBackend::~HeadlessGraphicsBackend() = default;

    void HeadlessGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        clearColor_[0] = r; clearColor_[1] = g; clearColor_[2] = b; clearColor_[3] = a;
        state_->stats.clearCount++;
        state_->RecordTrace("Clear", "");
    }

    void HeadlessGraphicsBackend::Present()
    {
        state_->stats.presentCount++;
        state_->statsAtLastPresent = state_->stats;
        state_->frameIndex++;
        state_->RecordTrace("Present", "frameIndex=" + std::to_string(state_->frameIndex));
    }

    void HeadlessGraphicsBackend::GetViewportSize(int& width, int& height)
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

    void HeadlessGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void HeadlessGraphicsBackend::SetPresentationMode(int /*mode*/) {}

    void HeadlessGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        Require(state_, w >= 0 && h >= 0, "HeadlessGraphicsBackend::ReadBackbuffer: negative width/height");
        // Nothing was ever rendered -- reports the last Clear() colour for every pixel, which is
        // the closest honest answer a backend that draws nothing can give (matches what a real
        // backend would show if every draw call were a no-op but Clear() still worked).
        const std::uint8_t r = static_cast<std::uint8_t>(std::clamp(clearColor_[0], 0.0f, 1.0f) * 255.0f);
        const std::uint8_t g = static_cast<std::uint8_t>(std::clamp(clearColor_[1], 0.0f, 1.0f) * 255.0f);
        const std::uint8_t b = static_cast<std::uint8_t>(std::clamp(clearColor_[2], 0.0f, 1.0f) * 255.0f);
        const std::uint8_t a = static_cast<std::uint8_t>(std::clamp(clearColor_[3], 0.0f, 1.0f) * 255.0f);
        for (int i = 0; i < w * h; ++i)
        {
            pixels[i * 4 + 0] = r; pixels[i * 4 + 1] = g; pixels[i * 4 + 2] = b; pixels[i * 4 + 3] = a;
        }
        (void)x; (void)y;
    }

    std::unique_ptr<ITextureBackend> HeadlessGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<HeadlessTextureBackend>(state_, data);
    }

    std::unique_ptr<ISpriteBatchBackend> HeadlessGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<HeadlessSpriteBatchBackend>(state_);
    }

    std::unique_ptr<IOcclusionQueryBackend> HeadlessGraphicsBackend::CreateOcclusionQuery()
    {
        return std::make_unique<HeadlessOcclusionQueryBackend>(state_);
    }

    std::unique_ptr<ITexture3DBackend> HeadlessGraphicsBackend::CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                                            int surfaceFormat)
    {
        return std::make_unique<HeadlessTexture3DBackend>(state_, w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITextureCubeBackend> HeadlessGraphicsBackend::CreateTextureCube(int size, bool mipMap,
                                                                                int surfaceFormat)
    {
        return std::make_unique<HeadlessTextureCubeBackend>(state_, size, mipMap, surfaceFormat);
    }

    std::unique_ptr<IRenderTargetBackend> HeadlessGraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<HeadlessRenderTargetBackend>(state_, w, h, depthFormat, mipMap, multiSampleCount);
    }

    void HeadlessGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->UnbindAsRenderTarget();
        currentRenderTarget_ = static_cast<HeadlessRenderTargetBackend*>(rt);
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->BindAsRenderTarget();
    }

    std::unique_ptr<IRenderTargetCubeBackend> HeadlessGraphicsBackend::CreateRenderTargetCube(
        int size, int depthFormat, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<HeadlessRenderTargetCubeBackend>(state_, size, depthFormat, mipMap, multiSampleCount);
    }

    std::unique_ptr<IEffectBackend> HeadlessGraphicsBackend::CreateEffectBackend(const std::string& vertSrc,
                                                                              const std::string& fragSrc)
    {
        auto effect = std::make_unique<HeadlessEffectBackend>(state_);
        effect->CompileProgram(vertSrc, fragSrc);
        return effect;
    }

    void HeadlessGraphicsBackend::ApplyBlendState(int, int, int, int, int, int)
    {
        state_->stats.blendStateChangeCount++;
        state_->RecordTrace("ApplyBlendState", "");
    }

    void HeadlessGraphicsBackend::ApplyDepthStencilState(bool, bool, int, bool, int, int, int, int, int, int, int,
                                                      bool, int, int, int, int)
    {
        state_->stats.depthStencilStateChangeCount++;
        state_->RecordTrace("ApplyDepthStencilState", "");
    }

    void HeadlessGraphicsBackend::ApplyRasterizerState(int, int, bool, float, float)
    {
        state_->stats.rasterizerStateChangeCount++;
        state_->RecordTrace("ApplyRasterizerState", "");
    }

    void HeadlessGraphicsBackend::ApplySamplerState(int slot, int, int, int, int)
    {
        Require(state_, slot >= 0 && slot < 16,
               "HeadlessGraphicsBackend::ApplySamplerState: slot must be 0..15, got " + std::to_string(slot));
        state_->stats.samplerStateChangeCount++;
        state_->RecordTrace("ApplySamplerState", "slot=" + std::to_string(slot));
    }

    void HeadlessGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        Require(state_, w >= 0 && h >= 0,
               "HeadlessGraphicsBackend::SetScissorRect: negative width/height (" + std::to_string(w) + "x" +
               std::to_string(h) + ")");
        Require(state_, x >= 0 && y >= 0,
               "HeadlessGraphicsBackend::SetScissorRect: negative origin (" + std::to_string(x) + "," +
               std::to_string(y) + ")");
        // HEADLESS-23: cross-reference the currently-bound target's real size (GetViewportSize()
        // already resolves to the bound RenderTarget2D's size, or the virtual/default backbuffer
        // size when none is bound -- reused rather than duplicating that resolution logic). Skipped
        // in HeadlessFast (via ValidationEnabled()) so this backend doesn't pay for a
        // GetViewportSize() call on every scissor change when validation is off.
        if (state_->ValidationEnabled())
        {
            int targetWidth = 0, targetHeight = 0;
            GetViewportSize(targetWidth, targetHeight);
            Require(state_, x + w <= targetWidth && y + h <= targetHeight,
                   "HeadlessGraphicsBackend::SetScissorRect: rectangle (" + std::to_string(x) + "," +
                   std::to_string(y) + "," + std::to_string(w) + "x" + std::to_string(h) +
                   ") exceeds the current target's bounds (" + std::to_string(targetWidth) + "x" +
                   std::to_string(targetHeight) + ")");
        }
        state_->stats.scissorChangeCount++;
        state_->RecordTrace("SetScissorRect", std::to_string(x) + "," + std::to_string(y) + "," +
                            std::to_string(w) + "x" + std::to_string(h));
    }

    void HeadlessGraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        Require(state_, w >= 0 && h >= 0,
               "HeadlessGraphicsBackend::SetViewport: negative width/height (" + std::to_string(w) + "x" +
               std::to_string(h) + ")");
        Require(state_, x >= 0 && y >= 0,
               "HeadlessGraphicsBackend::SetViewport: negative origin (" + std::to_string(x) + "," +
               std::to_string(y) + ")");
        Require(state_, minDepth <= maxDepth,
               "HeadlessGraphicsBackend::SetViewport: minDepth must be <= maxDepth");
        if (state_->ValidationEnabled())
        {
            int targetWidth = 0, targetHeight = 0;
            GetViewportSize(targetWidth, targetHeight);
            Require(state_, x + w <= targetWidth && y + h <= targetHeight,
                   "HeadlessGraphicsBackend::SetViewport: rectangle (" + std::to_string(x) + "," +
                   std::to_string(y) + "," + std::to_string(w) + "x" + std::to_string(h) +
                   ") exceeds the current target's bounds (" + std::to_string(targetWidth) + "x" +
                   std::to_string(targetHeight) + ")");
        }
        state_->stats.viewportChangeCount++;
        state_->RecordTrace("SetViewport", std::to_string(x) + "," + std::to_string(y) + "," +
                            std::to_string(w) + "x" + std::to_string(h));
    }

    void HeadlessGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float /*depth*/)
    { Clear(r, g, b, a); }
    void HeadlessGraphicsBackend::ClearDepth(float /*depth*/)
    {
        state_->stats.clearCount++;
        state_->RecordTrace("ClearDepth", "");
    }
    void HeadlessGraphicsBackend::ClearStencil(int /*stencil*/)
    {
        state_->stats.clearCount++;
        state_->RecordTrace("ClearStencil", "");
    }
    void HeadlessGraphicsBackend::ClearDepthAndStencil(float /*depth*/, int /*stencil*/)
    {
        state_->stats.clearCount++;
        state_->RecordTrace("ClearDepthAndStencil", "");
    }
    void HeadlessGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int /*stencil*/)
    { Clear(r, g, b, a); }
    void HeadlessGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float /*depth*/,
                                                        int /*stencil*/)
    { Clear(r, g, b, a); }

    void HeadlessGraphicsBackend::SetDepthTestEnabled(bool enabled)
    {
        state_->stats.depthStencilStateChangeCount++;
        state_->RecordTrace("SetDepthTestEnabled", enabled ? "true" : "false");
    }
    void HeadlessGraphicsBackend::SetBlendEnabled(bool enabled)
    {
        state_->stats.blendStateChangeCount++;
        state_->RecordTrace("SetBlendEnabled", enabled ? "true" : "false");
    }
    void HeadlessGraphicsBackend::SetDepthWriteEnabled(bool enabled)
    {
        state_->stats.depthStencilStateChangeCount++;
        state_->RecordTrace("SetDepthWriteEnabled", enabled ? "true" : "false");
    }

    std::unique_ptr<IVertexBufferBackend> HeadlessGraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<HeadlessVertexBufferBackend>(state_, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> HeadlessGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<HeadlessIndexBufferBackend>(state_, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> HeadlessGraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<HeadlessIndexBufferBackend>(state_, index_capacity, true);
    }

    void HeadlessGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix&, const Matrix&,
                                                     const Matrix&, PrimitiveType primitive, int primitiveCount)
    {
        Require(state_, primitiveCount > 0,
               "HeadlessGraphicsBackend::DrawColoredPrimitives: primitiveCount must be > 0, got " +
               std::to_string(primitiveCount));
        const int neededVertices = PrimitiveVertexCount(primitive, primitiveCount);
        Require(state_, neededVertices <= vb.GetVertexCount(),
               "HeadlessGraphicsBackend::DrawColoredPrimitives: primitiveCount " + std::to_string(primitiveCount) +
               " needs " + std::to_string(neededVertices) + " vertices but the bound buffer only has " +
               std::to_string(vb.GetVertexCount()));
        state_->stats.drawCallCount++;
        state_->stats.primitiveCount += static_cast<std::uint64_t>(primitiveCount);
        state_->RecordTrace("DrawColoredPrimitives", "primitiveCount=" + std::to_string(primitiveCount));
    }

    void HeadlessGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend& ib,
                                                            const Matrix&, const Matrix&, const Matrix&,
                                                            PrimitiveType primitive, int primitiveCount)
    {
        Require(state_, primitiveCount > 0,
               "HeadlessGraphicsBackend::DrawIndexedColoredPrimitives: primitiveCount must be > 0, got " +
               std::to_string(primitiveCount));
        const int neededIndices = PrimitiveIndexCount(primitive, primitiveCount);
        Require(state_, neededIndices <= ib.GetIndexCount(),
               "HeadlessGraphicsBackend::DrawIndexedColoredPrimitives: primitiveCount " +
               std::to_string(primitiveCount) + " needs " + std::to_string(neededIndices) +
               " indices but the bound buffer only has " + std::to_string(ib.GetIndexCount()));
        state_->stats.drawCallCount++;
        state_->stats.primitiveCount += static_cast<std::uint64_t>(primitiveCount);
        state_->RecordTrace("DrawIndexedColoredPrimitives", "primitiveCount=" + std::to_string(primitiveCount));
    }

    void HeadlessGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb, const Matrix& world,
                                               const Matrix& view, const Matrix& projection,
                                               PrimitiveType primitive, int primitiveCount,
                                               const GpuDrawParams& params)
    {
        Require(state_, !(params.textureEnabled && params.texture0 == nullptr),
               "HeadlessGraphicsBackend::DrawPrimitivesEx: TextureEnabled=true but texture0 is null");
        Require(state_, !(params.dualTexture && params.texture1 == nullptr),
               "HeadlessGraphicsBackend::DrawPrimitivesEx: DualTexture=true but texture1 is null");
        Require(state_, !(params.envMapping && params.envMap == nullptr),
               "HeadlessGraphicsBackend::DrawPrimitivesEx: EnvMapping=true but envMap is null");
        Require(state_, !(params.skinned && params.boneCount <= 0),
               "HeadlessGraphicsBackend::DrawPrimitivesEx: Skinned=true but boneCount <= 0");
        DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
    }

    void HeadlessGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                                       const Matrix& world, const Matrix& view,
                                                       const Matrix& projection, PrimitiveType primitive,
                                                       int primitiveCount, const GpuDrawParams& params)
    {
        Require(state_, !(params.textureEnabled && params.texture0 == nullptr),
               "HeadlessGraphicsBackend::DrawIndexedPrimitivesEx: TextureEnabled=true but texture0 is null");
        Require(state_, !(params.dualTexture && params.texture1 == nullptr),
               "HeadlessGraphicsBackend::DrawIndexedPrimitivesEx: DualTexture=true but texture1 is null");
        Require(state_, !(params.envMapping && params.envMap == nullptr),
               "HeadlessGraphicsBackend::DrawIndexedPrimitivesEx: EnvMapping=true but envMap is null");
        Require(state_, !(params.skinned && params.boneCount <= 0),
               "HeadlessGraphicsBackend::DrawIndexedPrimitivesEx: Skinned=true but boneCount <= 0");
        DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
    }

    void HeadlessGraphicsBackend::DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                                        const Matrix& world, const Matrix& view,
                                                        const Matrix& projection, PrimitiveType primitive,
                                                        int primitiveCount, int instanceCount,
                                                        const GpuDrawParams& params)
    {
        Require(state_, instanceCount > 0,
               "HeadlessGraphicsBackend::DrawInstancedPrimitivesEx: instanceCount must be > 0, got " +
               std::to_string(instanceCount));
        DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount, params);
        state_->stats.drawCallCount += static_cast<std::uint64_t>(instanceCount - 1);
    }

    HeadlessStatistics HeadlessGraphicsBackend::GetLastFrameStatistics() const
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

    void HeadlessGraphicsBackend::AssertNoLeaks() const
    {
        const std::vector<HeadlessResourceRecord> alive = state_->registry.AliveResources();
        if (alive.empty())
            return;

        std::ostringstream message;
        message << "HeadlessGraphicsBackend::AssertNoLeaks: " << alive.size() << " resource(s) still alive:\n";
        for (const HeadlessResourceRecord& record : alive)
        {
            message << "  #" << record.id << " " << record.typeName;
            if (!record.creationSite.empty())
                message << " (created at " << record.creationSite << ")";
            message << "\n";
        }
        throw HeadlessValidationException(message.str());
    }

    std::string HeadlessGraphicsBackend::FormatTraceLog() const
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

    void HeadlessGraphicsBackend::DumpTraceLog(std::FILE* out) const
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

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Headless::HeadlessGraphicsBackend>(args.virtualWidth, args.virtualHeight);
    }
}
