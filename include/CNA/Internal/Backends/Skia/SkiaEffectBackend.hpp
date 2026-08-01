#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include "include/core/SkRefCnt.h"

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

        /**
         * Creates one draw's runtime shader. The accepted v1 ABI has exactly one shader child
         * named `cnaTexture0` and one non-array float4 uniform named `cnaTint`.
         */
        [[nodiscard]] sk_sp<SkShader> MakeSpriteShaderEXT(
            sk_sp<SkShader> primaryTexture, const float tint[4]) const;

    private:
        void Fail(std::string message);

        sk_sp<SkRuntimeEffect> effect_;
        std::vector<std::uint8_t> uniformBytes_;
        std::size_t tintOffset_ = 0;
        std::string compileError_;
        bool bound_ = false;
    };
} // namespace CNA::Internal::Backends::Skia
