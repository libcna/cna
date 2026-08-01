#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

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
    inline constexpr std::size_t kSkiaSkslMaxSourceBytesEXT = 64u * 1024u;
    inline constexpr std::size_t kSkiaSkslMaxUniformBytesEXT = 16u * 1024u;
    inline constexpr std::size_t kSkiaSkslMaxUniformCountEXT = 64u;
    inline constexpr int kSkiaSkslMaxTextureUnitEXT = 7;

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
    };
} // namespace CNA::Internal::Backends::Skia
