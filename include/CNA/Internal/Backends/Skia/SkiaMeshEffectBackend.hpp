#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"

#include "include/core/SkRefCnt.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class SkRuntimeEffect;
class SkShader;

namespace CNA::Internal::Backends::Skia
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
     * SKIA-154: basic source-keyed compilation cache for the mesh ABI. A cache hit returns the same
     * immutable compiled program without invoking the SkSL compiler again; each `SkiaMeshEffectBackend`
     * instance still owns its own independent mutable uniform-value buffer and bound-texture weak
     * references, so two instances sharing one cache entry never observe each other's state (clone
     * isolation). No eviction policy or growth bound in this task's scope -- hardening the cache
     * (bounding growth, time limits, malicious-input stress) is SKIA-156's job, not this one's.
     */
    class SkiaMeshEffectCacheEXT final
    {
    public:
        /**
         * Returns the cached compiled program for @p source, compiling and inserting it first if
         * this is the first time @p source has been seen. Returns null and sets @p compileError on
         * a genuine compile/validation failure; nothing is cached in that case.
         */
        [[nodiscard]] std::shared_ptr<const SkiaMeshEffectCompiledEXT> GetOrCompileEXT(
            const std::string& source, std::string& compileError);

        /** NOXNA diagnostic accessor: number of distinct source strings currently cached. */
        [[nodiscard]] std::size_t SizeEXT() const noexcept { return entries_.size(); }

    private:
        std::map<std::string, std::shared_ptr<const SkiaMeshEffectCompiledEXT>> entries_;
    };

    /**
     * SKIA-154: bounded `SkRuntimeEffect` adapter for the explicitly tagged `SkVertices` mesh ABI.
     * Deliberately not an `IEffectBackend` override: that interface's `MakeSpriteShaderEXT`-shaped
     * contract assumes a single reserved primary texture/tint every SpriteBatch draw supplies,
     * which a mesh draw has neither of (docs/skia-vertices-2d-effect-contract.md). Public
     * `ShaderEffect`/`SpriteBatch`/`SkVertices` draw integration is SKIA-157's job -- this class
     * only compiles, reflects, and builds the paint shader `SkCanvas::drawVertices` consumes
     * directly, matching the SKIA-93/145/147/153 "prove it below the API first" precedent.
     */
    class SkiaMeshEffectBackend final
    {
    public:
        /**
         * Compiles @p marker/@p source (mirroring `ShaderEffect`'s two-opaque-string payload
         * exactly like `SkiaEffectBackend::CompileProgram`) against @p cache, reusing a cached
         * compiled program when @p source was seen before. @p marker must equal
         * `kSkiaSkslMeshEffectMarkerEXT` exactly -- untagged GLSL, SPIR-V, arbitrary vertex stages,
         * and the sprite ABI's own `CNA_SKIA_SKSL_V1` marker are all rejected here, so a mesh-mode
         * program can never be silently accepted down a different path or vice versa.
         */
        bool CompileProgram(const std::string& marker, const std::string& source,
                            SkiaMeshEffectCacheEXT& cache);

        [[nodiscard]] bool IsValid() const noexcept { return compiled_ != nullptr; }
        [[nodiscard]] std::string GetCompileError() const { return compileError_; }

        void SetUniformFloat(const char* name, float value);
        void SetUniformInt(const char* name, int value);
        void SetUniformVec2(const char* name, float x, float y);
        void SetUniformVec3(const char* name, float x, float y, float z);
        void SetUniformVec4(const char* name, float x, float y, float z, float w);
        void SetUniformMat4(const char* name, const float* matrix);
        void SetUniformFloatArray(const char* name, const float* values, int count);
        void SetUniformVec2Array(const char* name, const float* values, int count);
        void BindTexture(int unit, ITextureBackend* texture);

        /** Throws before a draw if a declared `cnaTexture0`..`7` child has not been bound. */
        void ValidateMeshBindingsEXT() const;

        /**
         * Builds one draw's immutable paint shader, sampling any bound `cnaTexture0`..`7` children
         * fresh from their current backend pixels (live reference, not a snapshot -- identical
         * contract to `SkiaEffectBackend::BindTexture`). Returns null if a declared child is unbound
         * or its backend has expired; callers must call `ValidateMeshBindingsEXT()` first for an
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
        std::array<std::weak_ptr<ITextureBackend>, 8> boundTextureBackends_;
        std::string compileError_;
    };
} // namespace CNA::Internal::Backends::Skia
