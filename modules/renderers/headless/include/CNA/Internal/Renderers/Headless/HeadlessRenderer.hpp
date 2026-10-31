#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::Headless
{
    /**
     * Runtime strictness dial for the Headless renderer (docs/headless-renderer.md, "Runtime modes"). Selected via
     * the CNA_HEADLESS_MODE environment variable (Fast/Validation/Trace, default Validation) and
     * overridable programmatically before Game::Run() via HeadlessRenderer::SetMode().
     */
    enum class HeadlessMode
    {
        /// Accepts everything, does the minimum bookkeeping for counters, skips argument/bounds
        /// validation entirely. For test runs that just need the game loop to execute quickly.
        Fast,
        /// Full argument validation (bounds, resource-lifetime, state consistency) on top of
        /// Fast's counters; throws HeadlessValidationException for the same misuse a real renderer is
        /// expected to reject. Default mode.
        Validation,
        /// Everything Validation does, plus a structured call log and creation-site tracking for
        /// every resource.
        Trace,
    };

    /// Parses the CNA_HEADLESS_MODE environment variable ("Fast"/"Validation"/"Trace",
    /// case-insensitive); returns Validation for an unset or unrecognized value.
    [[nodiscard]] HeadlessMode ParseHeadlessModeFromEnvironment();

    /// Thrown by HeadlessValidation/HeadlessTrace mode when a caller violates an argument or API-contract
    /// rule (see HEADLESS-20..25). Carries the specific rule that was violated, not a
    /// generic message, so a failing test points directly at the actual mistake.
    class HeadlessValidationException : public std::runtime_error
    {
    public:
        explicit HeadlessValidationException(const std::string& message) : std::runtime_error(message) {}
    };

    /// Cumulative and per-resource-type counters (HEADLESS-30..35). Read via
    /// HeadlessRenderer::GetStatistics()/GetLastFrameStatistics().
    struct HeadlessStatistics
    {
        std::uint64_t drawCallCount = 0;
        std::uint64_t primitiveCount = 0;
        std::uint64_t clearCount = 0;
        std::uint64_t presentCount = 0;

        std::uint64_t blendStateChangeCount = 0;
        std::uint64_t depthStencilStateChangeCount = 0;
        std::uint64_t rasterizerStateChangeCount = 0;
        std::uint64_t samplerStateChangeCount = 0;
        std::uint64_t viewportChangeCount = 0;
        std::uint64_t scissorChangeCount = 0;

        std::uint64_t vertexBuffersCreated = 0;
        std::uint64_t indexBuffersCreated = 0;
        std::uint64_t texturesCreated = 0;
        std::uint64_t textureCubesCreated = 0;
        std::uint64_t textures3DCreated = 0;
        std::uint64_t renderTargetsCreated = 0;
        std::uint64_t renderTargetCubesCreated = 0;
        std::uint64_t effectsCreated = 0;
        std::uint64_t spriteBatchesCreated = 0;
        std::uint64_t occlusionQueriesCreated = 0;
    };

    /// One entry in the shared resource registry (HEADLESS-18). `id` is a monotonic
    /// debug ID, not a GPU handle -- Headless resources never touch a GPU at all.
    struct HeadlessResourceRecord
    {
        std::uint64_t id = 0;
        std::string typeName;
        std::string creationSite;   ///< populated only in HeadlessTrace mode (HEADLESS-41)
        std::uint64_t creationCallIndex = 0;
    };

    /// Backbone for leak detection (HEADLESS-34/35) and the "resources currently alive" counters
    /// (HEADLESS-33). Every Headless*Renderer registers itself on construction and unregisters in its
    /// destructor; shared (via shared_ptr) across every resource renderer created by one
    /// HeadlessRenderer so leaks can be detected regardless of which resource type leaked.
    class HeadlessResourceRegistry
    {
    public:
        std::uint64_t Register(const std::string& typeName, const std::string& creationSite = {});
        void Unregister(std::uint64_t id);
        [[nodiscard]] std::vector<HeadlessResourceRecord> AliveResources() const;
        [[nodiscard]] std::size_t AliveCount() const;
        /// Removes every tracked record without reporting them as leaks -- used by tests that
        /// intentionally want a clean slate rather than a real leak assertion.
        void Clear();

    private:
        mutable std::mutex mutex_;
        std::unordered_map<std::uint64_t, HeadlessResourceRecord> records_;
        std::uint64_t nextId_ = 1;
    };

    /// One entry in HeadlessTrace mode's structured call log (HEADLESS-40).
    struct HeadlessTraceEntry
    {
        std::uint64_t callIndex = 0;
        std::uint64_t frameIndex = 0;
        std::string method;
        std::string argsSummary;
    };

    /// Result of comparing two HeadlessTrace logs (HEADLESS-43). `firstDivergingIndex` is only
    /// meaningful when `identical` is false: it names the position (0-indexed) of the first entry
    /// that differs, or -- if one log is a strict prefix of the other -- the position where the
    /// shorter log ends.
    struct HeadlessTraceLogDiff
    {
        bool identical = true;
        std::size_t firstDivergingIndex = 0;
    };

    /// Compares two HeadlessTrace logs entry-by-entry (frameIndex/method/argsSummary; `callIndex`
    /// is deliberately excluded since it is a redundant position counter, not meaningful call
    /// content). Intended for catching behavioral drift between two runs of the same
    /// (ideally deterministic) game across commits, independent of any pixel output --
    /// HEADLESS-43.
    [[nodiscard]] HeadlessTraceLogDiff CompareTraceLogs(const std::vector<HeadlessTraceEntry>& baseline,
                                                         const std::vector<HeadlessTraceEntry>& current);

    /// Renders CompareTraceLogs()'s result as human-readable text: either a one-line "identical"
    /// summary, or the first diverging entry from each log side-by-side plus an entry-count note
    /// if the logs are different lengths.
    [[nodiscard]] std::string FormatTraceLogDiff(const std::vector<HeadlessTraceEntry>& baseline,
                                                  const std::vector<HeadlessTraceEntry>& current);

    /// State shared by a HeadlessRenderer and every Headless*Renderer resource it creates: the
    /// active mode, the resource registry, and the running statistics. Held via shared_ptr so a
    /// resource renderer's lifetime is never coupled to the owning HeadlessRenderer outliving it
    /// (defensive; in practice resources are always destroyed at or before device teardown).
    struct HeadlessSharedState
    {
        HeadlessMode mode = HeadlessMode::Validation;
        HeadlessResourceRegistry registry;
        HeadlessStatistics stats;
        HeadlessStatistics statsAtLastPresent;
        std::uint64_t frameIndex = 0;
        std::vector<HeadlessTraceEntry> traceLog;   ///< only populated in HeadlessTrace mode
        std::uint64_t nextCallIndex = 0;
        /// HEADLESS-41: a real `std::source_location` captured somewhere inside this renderer would
        /// always point at the same Create*() call site per resource type -- no more informative
        /// than the type name string already is, since every renderer method that reaches a real
        /// game's own `new VertexBuffer(...)` call site would require adding a defaulted
        /// `std::source_location` parameter to `IGraphicsRenderer`'s virtual Create* methods, a
        /// shared-interface change touching all 5 other renderers for a Headless-only diagnostic
        /// feature. This debug-label stack is the renderer-local alternative the plan's own wording
        /// anticipated ("an explicit debug-label parameter, whichever is more useful"): a test can
        /// wrap a block of resource creation in Push/PopDebugLabel() and get that label back in a
        /// leak report, entirely within this renderer.
        std::vector<std::string> debugLabelStack;

        [[nodiscard]] bool ValidationEnabled() const { return mode != HeadlessMode::Fast; }
        [[nodiscard]] bool TraceEnabled() const { return mode == HeadlessMode::Trace; }
        void RecordTrace(const std::string& method, const std::string& argsSummary);
        /// Returns the joined debug-label stack ("outer/inner/...") or empty if nothing is pushed.
        [[nodiscard]] std::string CurrentDebugLabel() const;
    };

    class HeadlessVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        HeadlessVertexBufferRenderer(std::shared_ptr<HeadlessSharedState> state, int vertexCapacity);
        ~HeadlessVertexBufferRenderer() override;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        [[nodiscard]] int Capacity() const { return capacity_; }
        [[nodiscard]] std::size_t Stride() const { return stride_; }
        /// Raw bytes from the most recent SetData() call -- kept (not discarded) so tests can
        /// assert on actual buffer contents, not just that SetData() was called (HEADLESS-10).
        [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }

    private:
        /// Unbinds every currently bound render target and forgets them.
        void UnbindAllRenderTargets();

        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<std::uint8_t> shadowData_;
    };

    class HeadlessIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        HeadlessIndexBufferRenderer(std::shared_ptr<HeadlessSharedState> state, int indexCapacity, bool thirtyTwoBit);
        ~HeadlessIndexBufferRenderer() override;

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        [[nodiscard]] int Capacity() const { return capacity_; }
        [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }

    private:
        void Upload(const void* data, int index_count, bool dataIsThirtyTwoBit);

        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::vector<std::uint8_t> shadowData_;
    };

    class HeadlessTextureRenderer : public ITextureRenderer
    {
    public:
        HeadlessTextureRenderer(std::shared_ptr<HeadlessSharedState> state, const ImageData& data);
        HeadlessTextureRenderer(std::shared_ptr<HeadlessSharedState> state, int width, int height,
                           std::string typeNameOverride);
        ~HeadlessTextureRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        [[nodiscard]] const std::vector<std::uint8_t>& Pixels() const { return pixels_; }
        /**
         * @brief Returns the format-native bytes retained for one mip level.
         *
         * @param level Zero-based mip level.
         * @return Stored bytes for the requested level.
         */
        [[nodiscard]] const std::vector<std::uint8_t>& MipPixels(int level) const
        {
            return mipPixels_.at(static_cast<std::size_t>(level));
        }

    protected:
        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        int width_ = 0;
        int height_ = 0;
        int mipLevelCount_ = 1;
        int surfaceFormat_ = 0;
        std::vector<std::uint8_t> pixels_;
        std::vector<std::vector<std::uint8_t>> mipPixels_;
    };

    class HeadlessRenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        HeadlessRenderTargetRenderer(std::shared_ptr<HeadlessSharedState> state, int w, int h, int depthFormat,
                                bool mipMap, int multiSampleCount);
        ~HeadlessRenderTargetRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /**
         * @brief Reports that this renderer cannot read a render target's colour attachment back.
         *
         * Stated explicitly rather than inherited, because the answer is a reviewed
         * capability decision, not an oversight: the Headless renderer deliberately executes no
         * rasterization at all -- it records API usage and resource state for validation/tracing --
         * so a render target's colour content does not exist here in any form it could return.
         * The alternative, handing back this object's zeroed `pixels_`, is precisely the fabricated
         * transparent-black frame this finding removes: it would be indistinguishable from a real
         * readback of an empty target. Returning false makes `Texture2D::GetData` raise
         * `System::NotSupportedException` with the caller's destination untouched.
         *
         * @return Always false.
         */
        [[nodiscard]] bool GetData(int /*level*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                                   void* /*data*/, int /*dataLength*/) const override
        {
            return false;
        }
        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        { return depthFormatWasRequested; }

        [[nodiscard]] bool IsBound() const { return bound_; }

    private:
        /// Unbinds every currently bound render target and forgets them.
        void UnbindAllRenderTargets();

        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        int width_ = 0;
        int height_ = 0;
        int depthFormat_ = 0;
        bool mipMap_ = false;
        int multiSampleCount_ = 0;
        bool bound_ = false;
        std::vector<std::uint8_t> pixels_;
    };

    class HeadlessRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
    {
    public:
        HeadlessRenderTargetCubeRenderer(std::shared_ptr<HeadlessSharedState> state, int size, int depthFormat,
                                    bool mipMap, int multiSampleCount);
        ~HeadlessRenderTargetCubeRenderer() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

    private:
        /// Unbinds every currently bound render target and forgets them.
        void UnbindAllRenderTargets();

        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        int size_ = 0;
        int depthFormat_ = 0;
        bool mipMap_ = false;
        int multiSampleCount_ = 0;
        int boundFace_ = -1;
    };

    class HeadlessTextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        HeadlessTextureCubeRenderer(std::shared_ptr<HeadlessSharedState> state, int size, bool mipMap, int surfaceFormat);
        ~HeadlessTextureCubeRenderer() override;

        /** @brief Stores a complete format-native cube rectangle in CPU validation storage. */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        [[nodiscard]] bool SetDataBytesEXT(
            int face, int level, int x, int y, int w, int h,
            const void* data, int dataLength) override
        {
            return SetData(face, level, x, y, w, h, data, dataLength);
        }
        [[nodiscard]] bool SetCompressedDataEXT(
            int face, int level, int x, int y, int w, int h,
            const void* data, int dataLength) override
        {
            return SetData(face, level, x, y, w, h, data, dataLength);
        }
        /** @brief Reads a complete format-native cube rectangle from CPU validation storage. */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
        [[nodiscard]] bool GetDataBytesEXT(
            int face, int level, int x, int y, int w, int h,
            void* data, int dataLength) const override
        {
            return GetData(face, level, x, y, w, h, data, dataLength);
        }
        [[nodiscard]] bool GetCompressedDataEXT(
            int face, int level, int x, int y, int w, int h,
            void* data, int dataLength) const override
        {
            return GetData(face, level, x, y, w, h, data, dataLength);
        }

        [[nodiscard]] int Size() const { return size_; }
        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }
        [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override { return surfaceFormat_; }

    private:
        /// Unbinds every currently bound render target and forgets them.
        void UnbindAllRenderTargets();

        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        int size_ = 0;
        int mipLevels_ = 1;
        bool mipMap_ = false;
        int surfaceFormat_ = 0;
        std::vector<std::vector<std::uint8_t>> mipFacePixels_;
    };

    class HeadlessTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        HeadlessTexture3DRenderer(std::shared_ptr<HeadlessSharedState> state, int w, int h, int depth,
                             bool mipMap, int surfaceFormat);
        ~HeadlessTexture3DRenderer() override;

        /**
         * @brief Validates and traces a volume upload, then reports the unsupported operation.
         *
         * Plain cube textures have CPU validation storage; volume textures do not. This class is
         * unreachable through the normal public path because Headless reports no Texture3D
         * capability, but the direct renderer boundary still refuses rather than discarding data.
         *
         * @return Always false.
         */
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /**
         * @brief Reports that this renderer cannot read a volume texture back.
         *
         * This class is unreachable through the public API: the content reader fix made
         * `Texture3D`'s constructor fail on a device that reports no
         * `GraphicsCapability::Texture3D`, which this renderer does not, and under
         * `WarnAndStub` -- the one policy that lets the constructor ask -- the factory returns
         * the shared null object instead. It states its refusal anyway so the answer does not
         * depend on those gates staying in place.
         *
         * @return Always false.
         */
        [[nodiscard]] bool GetData(int /*level*/, int /*x*/, int /*y*/, int /*z*/,
                                   int /*w*/, int /*h*/, int /*depth*/,
                                   void* /*data*/, int /*dataLength*/) const override
        {
            return false;
        }

        [[nodiscard]] int Width() const { return width_; }
        [[nodiscard]] int Height() const { return height_; }
        [[nodiscard]] int Depth() const { return depth_; }

    private:
        /// Unbinds every currently bound render target and forgets them.
        void UnbindAllRenderTargets();

        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        int width_ = 0, height_ = 0, depth_ = 0;
        bool mipMap_ = false;
        int surfaceFormat_ = 0;
    };

    class HeadlessEffectRenderer final : public IEffectRenderer
    {
    public:
        explicit HeadlessEffectRenderer(std::shared_ptr<HeadlessSharedState> state);
        ~HeadlessEffectRenderer() override;

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        void Bind() override;
        void Unbind() override;
        [[nodiscard]] bool IsValid() const override { return compiled_; }
        [[nodiscard]] std::string GetCompileError() const override { return {}; }
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformFloatArray(const char* name, const float* values, int count) override;
        void SetUniformVec2Array(const char* name, const float* values, int count) override;
        void BindTexture(int unit, ITextureRenderer* texture) override;

        /// Last scalar/vector value set for each uniform name, keyed by name -- lets a test assert
        /// "did the game set this uniform" without a real shader compiler (HEADLESS-16).
        [[nodiscard]] const std::unordered_map<std::string, std::vector<float>>& UniformValues() const
        { return uniformValues_; }
        [[nodiscard]] bool IsBound() const { return bound_; }

    private:
        /// Unbinds every currently bound render target and forgets them.
        void UnbindAllRenderTargets();

        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        bool compiled_ = false;
        bool bound_ = false;
        std::unordered_map<std::string, std::vector<float>> uniformValues_;
        std::unordered_map<int, ITextureRenderer*> boundTextures_;
    };

    /// One recorded SpriteBatch::Draw() call (HEADLESS-17).
    struct HeadlessSpriteDrawRecord
    {
        const ITextureRenderer* texture = nullptr;
        Rectangle destinationRectangle;
        Rectangle sourceRectangle;
        Color color = Color(255, 255, 255, 255);
        float rotation = 0.0f;
        Vector2 origin;
        SpriteEffects effects = SpriteEffects::None;
        float layerDepth = 0.0f;
    };

    class HeadlessSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit HeadlessSpriteBatchRenderer(std::shared_ptr<HeadlessSharedState> state);
        ~HeadlessSpriteBatchRenderer() override;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override { transformMatrix_ = m; }
        void SetCustomEffect(Effect* effect) override { customEffect_ = effect; }
        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        { addressU_ = addressU; addressV_ = addressV; }
        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

        /// All draws recorded since the most recent Begin() (cleared on the next Begin()).
        [[nodiscard]] const std::vector<HeadlessSpriteDrawRecord>& LastBatch() const { return lastBatch_; }
        [[nodiscard]] bool IsBegun() const { return begun_; }

    private:
        /// Unbinds every currently bound render target and forgets them.
        void UnbindAllRenderTargets();

        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        bool begun_ = false;
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
        Effect* customEffect_ = nullptr;
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        std::vector<HeadlessSpriteDrawRecord> lastBatch_;
    };

    class HeadlessOcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        explicit HeadlessOcclusionQueryRenderer(std::shared_ptr<HeadlessSharedState> state);
        ~HeadlessOcclusionQueryRenderer() override;

        void Begin() override { active_ = true; }
        void End() override { active_ = false; complete_ = true; }
        [[nodiscard]] bool IsComplete() const override { return complete_; }
        /// Always reports "everything visible" (matches a renderer that never actually culls
        /// anything, i.e. never renders at all) -- a game that branches on occlusion results will
        /// always take the "visible" path under Headless, which is the conservative/safe choice for a
        /// renderer whose entire purpose is running game logic without skipping any of it.
        [[nodiscard]] int PixelCount() const override { return 1; }

    private:
        /// Unbinds every currently bound render target and forgets them.
        void UnbindAllRenderTargets();

        std::shared_ptr<HeadlessSharedState> state_;
        std::uint64_t resourceId_;
        bool active_ = false;
        bool complete_ = false;
    };

    /**
     * @brief Headless graphics renderer: touches no GPU and no window at all.
     *
     * See docs/headless-renderer.md for its purpose, modes and limitations. In short: every method
     * either does real bookkeeping (resource lifecycle, draw-call/state-change counters) or is a
     * genuine no-op, but nothing here ever allocates a GPU resource, compiles a real shader, or
     * requires SDL's video subsystem to be initialised.
     */
    class HeadlessRenderer final : public IGraphicsRenderer
    {
    public:
        HeadlessRenderer(int virtualWidth, int virtualHeight);
        ~HeadlessRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;

        /**
         * @brief Accepts any presentation request, because this renderer presents nothing.
         *
         * HEADLESS draws and displays nothing at all, so a source rectangle, a destination rectangle
         * and a foreign window are all equally inert here -- there is no frame to show part of and no
         * window to show it in. Accepting is therefore honest rather than generous: nothing about the
         * request could be got wrong. The request is recorded in the trace, so a test can see what
         * GraphicsDevice resolved and clipped it to.
         *
         * @param region What was asked for, already clipped.
         * @return Always @c true.
         */
        bool PresentRegionEXT(const RendererPresentRegionEXT& region) override;
        void GetViewportSize(int& width, int& height) override;
        /**
         * @brief Changes the virtual back-buffer dimensions and records the resolved size.
         *
         * @param width New virtual width in pixels.
         * @param height New virtual height in pixels.
         */
        void SetVirtualResolution(int width, int height) override;
        /**
         * @brief Records a presentation-mode change without creating a real presentation surface.
         *
         * @param mode Renderer-neutral presentation-mode value.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Backbuffer readback for `GraphicsDevice::GetBackBufferData`. Headless rasterizes
         *        nothing and owns no backbuffer pixel storage, so it cannot honestly return one.
         *
         * This raises `System::NotSupportedException` instead of fabricating a frame
         * (it formerly filled the caller's buffer with the last `Clear()` colour, which a caller
         * could not tell apart from a real black frame). It is the backbuffer counterpart of the
         * refuse-rather-than-fabricate contract established for the Texture and
         * render-target readbacks. The throw happens after `GetBackBufferData`'s own argument
         * validation, so an invalid request still fails with its own exception, and it leaves the
         * caller's destination completely untouched.
         *
         * @param x,y,w,h the requested region (ignored -- the request is always refused).
         * @param pixels the destination (never written).
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;


        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        [[nodiscard]] RendererFormatVerdict ClassifyTextureCubeFormatEXT(
            int surfaceFormat) const override;
        [[nodiscard]] bool IsCompressedCubeTransferFormatEXT(
            int surfaceFormat) const override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool preserveContents = false,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
        /**
         * @brief Binds one Headless cube-target face or restores the virtual back buffer.
         *
         * Replaces every prior 2D, cube or MRT binding so viewport/scissor validation observes
         * the selected cube's dimensions rather than stale target or back-buffer dimensions.
         *
         * @param rt Cube render target, or null to restore the virtual back buffer.
         * @param face Cube face index in the range 0..5.
         */
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;

        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        /** @brief Headless multisamples nothing: every requested count is applied as 0. */
        [[nodiscard]] int GetAppliedMultiSampleCountEXT(int /*requestedMultiSampleCount*/) const override
        {
            return 0;
        }
        /**
         * @brief Headless registers no adapter format query, so GraphicsAdapter selects Color for
         *        every back-buffer request; Reset applies the same answer rather than echoing the
         *        request.
         */
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int /*requestedFormat*/) const override
        {
            return 0;
        }

        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc, int stencilPass, int stencilFail,
                                    int stencilDepthFail, int stencilMask, int stencilWriteMask,
                                    int referenceStencil, bool twoSidedStencilMode, int ccwStencilFunc,
                                    int ccwStencilPass, int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        /**
         * @brief Records the constant blend colour used by BlendFactor blend modes.
         *
         * @param r Red component.
         * @param g Green component.
         * @param b Blue component.
         * @param a Alpha component.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;
        /**
         * @brief Records the independently mutable stencil reference value.
         *
         * @param value New stencil reference value.
         */
        void SetReferenceStencil(int value) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                              const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

        // ---- Headless-specific, CNAEXT-equivalent debug/testing API (HEADLESS-30..43) ----

        /// Sets the runtime validation strictness. Safe to call before or during a run; takes
        /// effect for all subsequent calls.
        void SetMode(HeadlessMode mode) { state_->mode = mode; }
        [[nodiscard]] HeadlessMode GetMode() const { return state_->mode; }

        /// Cumulative statistics since this renderer was constructed.
        [[nodiscard]] const HeadlessStatistics& GetStatistics() const { return state_->stats; }
        /// Statistics for the frame ending at the most recent Present() call (diffed against the
        /// cumulative totals as of the Present() before that).
        [[nodiscard]] HeadlessStatistics GetLastFrameStatistics() const;

        /// Walks the resource registry and returns every resource still alive. Empty means clean.
        [[nodiscard]] std::vector<HeadlessResourceRecord> AliveResources() const { return state_->registry.AliveResources(); }
        /// Throws HeadlessValidationException listing every still-alive resource, or returns
        /// normally if none remain (HEADLESS-34/35). Callable mid-run, not just at teardown. It is
        /// explicit rather than a destructor check because at an ordinary Game shutdown SpriteBatch and
        /// texture members are often still alive, so an automatic check would report false leaks.
        void AssertNoLeaks() const;

        /// The structured call log accumulated in HeadlessTrace mode (empty in Fast/Validation).
        [[nodiscard]] const std::vector<HeadlessTraceEntry>& TraceLog() const { return state_->traceLog; }
        /// Renders TraceLog() as human-readable text, one call per line
        /// ("[frame N #callIndex] method: argsSummary"), for CI logs or diffing between runs
        /// (HEADLESS-42).
        [[nodiscard]] std::string FormatTraceLog() const;
        /// Convenience wrapper: writes FormatTraceLog() to @p out (stdout by default).
        void DumpTraceLog(std::FILE* out = stdout) const;

        /// HEADLESS-41: pushes a label onto the debug-label stack; every resource created while it
        /// is on the stack records the joined stack ("outer/inner/...") as its creation site in
        /// HeadlessTrace mode (empty string in Fast/Validation, matching the plan's own "only in
        /// HeadlessTrace mode" wording -- see HeadlessResourceRecord::creationSite). Must be paired
        /// with PopDebugLabel(); typically used via a scope guard in test code.
        void PushDebugLabel(const std::string& label) { state_->debugLabelStack.push_back(label); }
        /// Pops the most recently pushed debug label. No-op (not a throw) if the stack is already
        /// empty, so a mispaired Pop in test cleanup code doesn't itself become a spurious failure.
        void PopDebugLabel() { if (!state_->debugLabelStack.empty()) state_->debugLabelStack.pop_back(); }

        [[nodiscard]] const std::shared_ptr<HeadlessSharedState>& SharedState() const { return state_; }

    private:
        /// Unbinds every currently bound render target and forgets them.
        void UnbindAllRenderTargets();

        struct BoundRenderTarget
        {
            HeadlessRenderTargetRenderer* target2D = nullptr;
            HeadlessRenderTargetCubeRenderer* targetCube = nullptr;
            int cubeFace = -1;
            int width = 0;
            int height = 0;
        };

        std::shared_ptr<HeadlessSharedState> state_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        float clearColor_[4] = {0, 0, 0, 1};
        /// Every active target in slot order. Keeping type, face and dimensions together prevents
        /// a cube binding from inheriting a prior 2D target's viewport bounds.
        std::vector<BoundRenderTarget> boundRenderTargets_;
    };
}
