#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"

#include "include/core/SkRefCnt.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class SkRuntimeEffect;
class SkShader;

namespace CNA::Internal::Backends::Skia
{
    /**
     * Explicit language/stage discriminator for CNA's raster-only SkSL SpriteBatch extension.
     *
     * ShaderEffect's two strings are backend-specific opaque payloads. Existing EasyGL content
     * places a GLSL vertex program in the first string, so Skia accepts SkSL only when that string
     * is exactly this marker. The second string is then a SkSL 100 runtime-shader program.
     */
    inline constexpr std::string_view kSkiaSkslSpriteEffectMarkerEXT = "CNA_SKIA_SKSL_V1";
    inline constexpr SkiaSourceAlphaConvention kSkiaSkslEffectOutputAlphaConventionEXT
        = SkiaSourceAlphaConvention::Premultiplied;
    /** Bounded SkRuntimeEffect adapter for the explicitly tagged 2D SpriteBatch ABI. */
    class SkiaEffectBackend final : public IEffectBackend
    {
    public:
        ~SkiaEffectBackend() override;
        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        void Bind() override;
        void Unbind() override;
        [[nodiscard]] bool IsValid() const override { return effect_ != nullptr; }
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformFloatArray(const char* name, const float* values, int count) override;
        void SetUniformVec2Array(const char* name, const float* values, int count) override;
        void BindTexture(int unit, ITextureBackend* texture) override;
        void BindTextureCube(int unit, ITextureCubeBackend* texture) override;
        void BindTexture3D(int unit, ITexture3DBackend* texture) override;

        /** Throws before Begin if a declared cnaTexture1..7 child has not been bound. */
        void ValidateSpriteBindingsEXT() const;

        /**
         * Creates one draw's runtime shader. The v1 ABI has a reserved `cnaTexture0` primary
         * child and `cnaTint`; reflected user uniforms and bound cnaTexture1..7 children are
         * copied into the immutable shader instance.
         */
        [[nodiscard]] sk_sp<SkShader> MakeSpriteShaderEXT(
            sk_sp<SkShader> primaryTexture, const float tint[4]) const;

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

        sk_sp<SkRuntimeEffect> effect_;
        std::vector<std::uint8_t> uniformBytes_;
        std::array<int, 8> childIndexByUnit_ = {-1, -1, -1, -1, -1, -1, -1, -1};
        std::array<std::weak_ptr<ITextureBackend>, 8> boundTextureBackends_;
        std::size_t tintOffset_ = 0;
        std::string compileError_;
        bool bound_ = false;

        // SKIA-149: cube/volume sampling wiring. `cnaCubeFace0-5`/`cnaVolumeAtlas0` live in their
        // own reserved child namespace (docs/skia-cube-volume-sampling-contract.md), orthogonal to
        // `childIndexByUnit_`'s cnaTexture0-7 budget, and are only present when `CompileProgram`
        // detected the author's source actually calling cnaSampleCubeEXT/cnaSampleVolumeEXT and
        // prepended the matching confirmed preamble (SKIA-145/147/148).
        std::array<int, 6> cubeFaceChildIndices_ = {-1, -1, -1, -1, -1, -1};
        int volumeAtlasChildIndex_ = -1;
        std::size_t cubeFaceSizeOffset_ = 0;
        std::size_t volumeMeta0Offset_ = 0;
        std::size_t volumeMeta1Offset_ = 0;
        std::size_t volumeAddressModesOffset_ = 0;
        std::weak_ptr<ITextureCubeBackend> boundCubeBackend_;
        std::weak_ptr<ITexture3DBackend> boundVolumeBackend_;
    };
} // namespace CNA::Internal::Backends::Skia
