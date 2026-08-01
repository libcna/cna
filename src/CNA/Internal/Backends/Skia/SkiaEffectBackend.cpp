#include "CNA/Internal/Backends/Skia/SkiaEffectBackend.hpp"

#include "include/core/SkData.h"
#include "include/core/SkShader.h"
#include "include/core/SkString.h"
#include "include/effects/SkRuntimeEffect.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    SkiaEffectBackend::~SkiaEffectBackend() = default;

    void SkiaEffectBackend::Fail(std::string message)
    {
        effect_.reset();
        uniformBytes_.clear();
        tintOffset_ = 0;
        bound_ = false;
        compileError_ = std::move(message);
    }

    bool SkiaEffectBackend::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        Fail({});
        if (vertSrc != kSkiaSkslSpriteEffectMarkerEXT)
        {
            Fail("Skia SkSL effects require the exact vertex marker CNA_SKIA_SKSL_V1; "
                 "untagged GLSL, SPIR-V, and arbitrary vertex stages are unsupported.");
            return false;
        }
        if (fragSrc.empty())
        {
            Fail("Skia SkSL effect source must not be empty.");
            return false;
        }
        if (fragSrc.size() > kSkiaSkslMaxSourceBytesEXT)
        {
            Fail("Skia SkSL effect source exceeds the 65536-byte safety limit.");
            return false;
        }

        auto result = SkRuntimeEffect::MakeForShader(SkString(fragSrc));
        if (!result.effect)
        {
            Fail(std::string("Skia SkSL compilation failed: ") + result.errorText.c_str());
            return false;
        }
        if (result.effect->uniformSize() > kSkiaSkslMaxUniformBytesEXT)
        {
            Fail("Skia SkSL reflected uniform block exceeds the 16384-byte safety limit.");
            return false;
        }

        const auto children = result.effect->children();
        if (children.size() != 1
            || children[0].name != "cnaTexture0"
            || children[0].type != SkRuntimeEffect::ChildType::kShader)
        {
            Fail("Skia SkSL v1 requires exactly one `uniform shader cnaTexture0` child; "
                 "additional, renamed, color-filter, and blender children are unsupported.");
            return false;
        }

        const auto uniforms = result.effect->uniforms();
        if (uniforms.size() != 1
            || uniforms[0].name != "cnaTint"
            || uniforms[0].type != SkRuntimeEffect::Uniform::Type::kFloat4
            || uniforms[0].flags != 0u)
        {
            Fail("Skia SkSL v1 requires exactly one non-array `uniform float4 cnaTint`; "
                 "general reflected uniforms are reserved for SKIA-92.");
            return false;
        }

        tintOffset_ = uniforms[0].offset;
        uniformBytes_.assign(result.effect->uniformSize(), 0u);
        effect_ = std::move(result.effect);
        compileError_.clear();
        return true;
    }

    void SkiaEffectBackend::Bind()
    {
        if (!effect_)
            throw std::runtime_error("Cannot bind an invalid Skia SkSL effect: " + compileError_);
        bound_ = true;
    }

    void SkiaEffectBackend::Unbind()
    {
        bound_ = false;
    }

    sk_sp<SkShader> SkiaEffectBackend::MakeSpriteShaderEXT(
        sk_sp<SkShader> primaryTexture, const float tint[4]) const
    {
        if (!effect_ || !primaryTexture || !tint)
            return nullptr;

        std::vector<std::uint8_t> drawUniforms = uniformBytes_;
        constexpr std::size_t tintBytes = sizeof(float) * 4u;
        if (tintOffset_ > drawUniforms.size() || tintBytes > drawUniforms.size() - tintOffset_)
            return nullptr;
        std::memcpy(drawUniforms.data() + tintOffset_, tint, tintBytes);

        sk_sp<const SkData> uniforms = SkData::MakeWithCopy(
            drawUniforms.data(), drawUniforms.size());
        sk_sp<SkShader> children[] = {std::move(primaryTexture)};
        return effect_->makeShader(std::move(uniforms), children, 1u);
    }
} // namespace CNA::Internal::Backends::Skia
