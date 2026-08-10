#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaResourcePolicy.hpp"

#include "include/core/SkRefCnt.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

class SkRuntimeEffect;
class SkShader;

namespace CNA::Internal::Renderers::Skia
{
    /**
     * SKIA-154: language/stage discriminator for the explicit SkVertices mesh ABI -- distinct from
     * `kSkiaSkslSpriteEffectMarkerEXT` ("CNA_SKIA_SKSL_V1") so a mesh-mode program can never be
     * silently accepted down the sprite path or vice versa. Reuses `ShaderEffect`'s existing
     * two-opaque-string payload convention exactly: the first string is this marker, never source;
     * the second is a SkSL 100 fragment program with no mandatory reserved uniform or child (see
     * docs/skia-vertices-2d-effect-contract.md -- a mesh draw's vertex-colour contribution combines
     * externally, through the SkBlendMode passed to SkCanvas::drawVertices, not inside the shader).
     */
    inline constexpr std::string_view kSkiaSkslMeshEffectMarkerEXT = "CNA_SKIA_SKSL_MESH_V1";

    /** Immutable compiled mesh-effect program, shared read-only across every cache hit. */
    struct SkiaMeshEffectCompiledEXT final
    {
        sk_sp<SkRuntimeEffect> effect;
        std::array<int, 8> childIndexByUnit = {-1, -1, -1, -1, -1, -1, -1, -1};
    };

    /**
     * SKIA-154/156: source-keyed, bounded-growth compilation cache for the mesh ABI. A cache hit
     * returns the same immutable compiled program without invoking the SkSL compiler again; each
     * `SkiaMeshEffectRenderer` instance still owns its own independent mutable uniform-value buffer
     * and bound-texture weak references, so two instances sharing one cache entry never observe
     * each other's state (clone isolation). A failed compile/validation never inserts anything --
     * the cache and every other already-cached entry are unaffected, so one failure cannot poison a
     * later, different (or corrected) compile. The key is `marker + source`, not source alone
     * (SKIA-156's "cache keys include ABI" requirement): today there is exactly one accepted marker
     * (`kSkiaSkslMeshEffectMarkerEXT`), so this makes no observable difference yet, but it means an
     * identical fragment string can never silently collide across a future second marker/ABI. A
     * "mode" axis (raster vs. a future Ganesh/GPU mode) is deliberately not added: no second mode
     * exists yet to test against, and speculatively building one now would be untested, unreachable
     * code -- add it when SKIA-159+ actually introduces a second compilation target. Reflected
     * uniform/child layout is not an independent key component either: it is a deterministic
     * function of `source` alone, so keying on `source` already keys on layout.
     */
    class SkiaMeshEffectCacheEXT final
    {
    public:
        /**
         * Returns the cached compiled program for @p marker/@p source, compiling and inserting it
         * first if this exact (marker, source) pair has not been seen before. Returns null and sets
         * @p compileError on a genuine compile/validation failure; nothing is cached in that case,
         * and every already-cached entry is left exactly as it was. Evicts the least-recently-used
         * entry first if inserting a new program would exceed `kSkiaMeshEffectCacheMaxEntriesEXT`.
         */
        [[nodiscard]] std::shared_ptr<const SkiaMeshEffectCompiledEXT> GetOrCompileEXT(
            const std::string& marker, const std::string& source, std::string& compileError);

        /** NOXNA diagnostic accessor: number of distinct (marker, source) pairs currently cached. */
        [[nodiscard]] std::size_t SizeEXT() const noexcept { return entries_.size(); }

    private:
        struct EntryEXT final
        {
            std::shared_ptr<const SkiaMeshEffectCompiledEXT> compiled;
            std::uint64_t lastUsedTick = 0;
        };

        void EvictLeastRecentlyUsedIfFullEXT();

        std::map<std::string, EntryEXT> entries_;
        std::uint64_t clock_ = 0;
    };

    /**
     * SKIA-154: bounded `SkRuntimeEffect` adapter for the explicitly tagged `SkVertices` mesh ABI.
     * Deliberately not an `IEffectRenderer` override: that interface's `MakeSpriteShaderEXT`-shaped
     * contract assumes a single reserved primary texture/tint every SpriteBatch draw supplies,
     * which a mesh draw has neither of (docs/skia-vertices-2d-effect-contract.md). Public
     * `ShaderEffect`/`SpriteBatch`/`SkVertices` draw integration is SKIA-157's job -- this class
     * only compiles, reflects, and builds the paint shader `SkCanvas::drawVertices` consumes
     * directly, matching the SKIA-93/145/147/153 "prove it below the API first" precedent.
     */
    class SkiaMeshEffectRenderer final
    {
    public:
        /**
         * Compiles @p marker/@p source (mirroring `ShaderEffect`'s two-opaque-string payload
         * exactly like `SkiaEffectRenderer::CompileProgram`) against @p cache, reusing a cached
         * compiled program when @p source was seen before. @p marker must equal
         * `kSkiaSkslMeshEffectMarkerEXT` exactly -- untagged GLSL, SPIR-V, arbitrary vertex stages,
         * and the sprite ABI's own `CNA_SKIA_SKSL_V1` marker are all rejected here, so a mesh-mode
         * program can never be silently accepted down a different path or vice versa.
         */
        bool CompileProgram(const std::string& marker, const std::string& source,
                            SkiaMeshEffectCacheEXT& cache);

        [[nodiscard]] bool IsValid() const noexcept { return compiled_ != nullptr; }
        [[nodiscard]] std::string GetCompileError() const { return compileError_; }

        /** NOXNA diagnostic accessor: raw identity of the shared compiled program, or null if
         * invalid. Two renderers with the same identity share one cache entry; a source that was
         * evicted and recompiled gets a new identity even if the source text is unchanged --
         * exists for test/diagnostic use, not part of the mesh effect ABI itself. */
        [[nodiscard]] const void* GetCompiledIdentityEXT() const noexcept { return compiled_.get(); }

        void SetUniformFloat(const char* name, float value);
        void SetUniformInt(const char* name, int value);
        void SetUniformVec2(const char* name, float x, float y);
        void SetUniformVec3(const char* name, float x, float y, float z);
        void SetUniformVec4(const char* name, float x, float y, float z, float w);
        void SetUniformMat4(const char* name, const float* matrix);
        void SetUniformFloatArray(const char* name, const float* values, int count);
        void SetUniformVec2Array(const char* name, const float* values, int count);
        void BindTexture(int unit, ITextureRenderer* texture);

        /** Throws before a draw if a declared `cnaTexture0`..`7` child has not been bound. */
        void ValidateMeshBindingsEXT() const;

        /**
         * Builds one draw's immutable paint shader, sampling any bound `cnaTexture0`..`7` children
         * fresh from their current renderer pixels (live reference, not a snapshot -- identical
         * contract to `SkiaEffectRenderer::BindTexture`). Returns null if a declared child is unbound
         * or its renderer has expired; callers must call `ValidateMeshBindingsEXT()` first for an
         * actionable diagnostic rather than a silent null.
         */
        [[nodiscard]] sk_sp<SkShader> MakeMeshShaderEXT() const;

    private:
        enum class UniformKind
        {
            Float,
            Int,
            Float2,
            Float3,
            Float4,
            Float4x4,
        };

        [[nodiscard]] static const char* UniformKindName(UniformKind kind) noexcept;
        [[nodiscard]] static int UniformTypeOrdinal(UniformKind kind) noexcept;
        void Fail(std::string message);
        void SetUniformData(const char* name, UniformKind kind, bool array, int count,
                            const void* data, std::size_t byteCount, const char* setter);

        std::shared_ptr<const SkiaMeshEffectCompiledEXT> compiled_;
        std::vector<std::uint8_t> uniformBytes_;
        std::array<std::weak_ptr<ITextureRenderer>, 8> boundTextureBackends_;
        std::string compileError_;
    };

    /**
     * SKIA-157: `IEffectRenderer`-conforming wrapper around `SkiaMeshEffectRenderer`, letting a mesh
     * effect flow through `ShaderEffect`'s existing, unmodified public surface (`effectRenderer_` is
     * already `unique_ptr<IEffectRenderer>`; `SetUniformX`/`SetTexture`/`Clone`/disposal all keep
     * working unchanged once this class conforms to the interface). Mirrors the exact extensibility
     * pattern `SkiaEffectRenderer`/`MakeSpriteShaderEXT` already established: the interface itself
     * stays generic, while `SkiaSpriteBatchRenderer::DrawMeshEXT` reaches this class's own extra
     * `MakeMeshShaderEXT()`/`ValidateMeshBindingsEXT()` methods through a `dynamic_cast` on
     * `Effect::GetEffectRendererPtr()`'s return value, matching `SkiaSpriteBatchRenderer::
     * SetCustomEffect`'s existing `dynamic_cast<SkiaEffectRenderer*>` precedent exactly.
     */
    class SkiaMeshEffectAdapterEXT final : public IEffectRenderer
    {
    public:
        explicit SkiaMeshEffectAdapterEXT(SkiaMeshEffectCacheEXT& cache) : cache_(cache) {}

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override
        {
            return renderer_.CompileProgram(vertSrc, fragSrc, cache_);
        }
        void Bind() override {}
        void Unbind() override {}
        [[nodiscard]] bool IsValid() const override { return renderer_.IsValid(); }
        [[nodiscard]] std::string GetCompileError() const override { return renderer_.GetCompileError(); }

        void SetUniformFloat(const char* name, float value) override { renderer_.SetUniformFloat(name, value); }
        void SetUniformInt(const char* name, int value) override { renderer_.SetUniformInt(name, value); }
        void SetUniformVec2(const char* name, float x, float y) override { renderer_.SetUniformVec2(name, x, y); }
        void SetUniformVec3(const char* name, float x, float y, float z) override
        {
            renderer_.SetUniformVec3(name, x, y, z);
        }
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override
        {
            renderer_.SetUniformVec4(name, x, y, z, w);
        }
        void SetUniformMat4(const char* name, const float* matrix) override
        {
            renderer_.SetUniformMat4(name, matrix);
        }
        void SetUniformFloatArray(const char* name, const float* values, int count) override
        {
            renderer_.SetUniformFloatArray(name, values, count);
        }
        void SetUniformVec2Array(const char* name, const float* values, int count) override
        {
            renderer_.SetUniformVec2Array(name, values, count);
        }
        void BindTexture(int unit, ITextureRenderer* texture) override { renderer_.BindTexture(unit, texture); }

        /** The mesh ABI has no cube/volume sampling at all -- reject explicitly rather than
         * silently inherit the interface's no-op default, which would misleadingly suggest the
         * call was accepted. */
        void BindTextureCube(int /*unit*/, ITextureCubeRenderer* /*texture*/) override
        {
            throw std::runtime_error(
                "Skia SkSL mesh effects do not support cube texture binding.");
        }
        void BindTexture3D(int /*unit*/, ITexture3DRenderer* /*texture*/) override
        {
            throw std::runtime_error(
                "Skia SkSL mesh effects do not support volume texture binding.");
        }

        /** See `SkiaMeshEffectRenderer::ValidateMeshBindingsEXT()`. */
        void ValidateMeshBindingsEXT() const { renderer_.ValidateMeshBindingsEXT(); }
        /** See `SkiaMeshEffectRenderer::MakeMeshShaderEXT()`. Defined out of line: `SkShader` is
         * only forward-declared here, so an inline-returned `sk_sp<SkShader>`'s destructor cannot
         * be instantiated in this header. */
        [[nodiscard]] sk_sp<SkShader> MakeMeshShaderEXT() const;

    private:
        SkiaMeshEffectRenderer renderer_;
        SkiaMeshEffectCacheEXT& cache_;
    };
} // namespace CNA::Internal::Renderers::Skia
