#include "CNA/Internal/Backends/Skia/SkiaEffectBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"

#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkString.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkRuntimeEffect.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        [[nodiscard]] bool ParseTextureChild(std::string_view name, int& unit)
        {
            constexpr std::string_view prefix = "cnaTexture";
            if (!name.starts_with(prefix) || name.size() != prefix.size() + 1u)
                return false;
            const char digit = name.back();
            if (digit < '0' || digit > '7')
                return false;
            unit = digit - '0';
            return true;
        }

        [[nodiscard]] bool IsSupportedUserUniform(const SkRuntimeEffect::Uniform& uniform)
        {
            using Type = SkRuntimeEffect::Uniform::Type;
            if (uniform.flags == 0u)
            {
                return uniform.type == Type::kFloat
                    || uniform.type == Type::kInt
                    || uniform.type == Type::kFloat2
                    || uniform.type == Type::kFloat3
                    || uniform.type == Type::kFloat4
                    || uniform.type == Type::kFloat4x4;
            }
            if (uniform.flags == SkRuntimeEffect::Uniform::kArray_Flag)
                return uniform.type == Type::kFloat || uniform.type == Type::kFloat2;
            return false;
        }

    }

    SkiaEffectBackend::~SkiaEffectBackend() = default;

    const char* SkiaEffectBackend::UniformKindName(UniformKind kind) noexcept
    {
        switch (kind)
        {
            case UniformKind::Float: return "float";
            case UniformKind::Int: return "int";
            case UniformKind::Float2: return "float2";
            case UniformKind::Float3: return "float3";
            case UniformKind::Float4: return "float4";
            case UniformKind::Float4x4: return "float4x4";
        }
        return "unknown";
    }

    int SkiaEffectBackend::UniformTypeOrdinal(UniformKind kind) noexcept
    {
        using Type = SkRuntimeEffect::Uniform::Type;
        switch (kind)
        {
            case UniformKind::Float: return static_cast<int>(Type::kFloat);
            case UniformKind::Int: return static_cast<int>(Type::kInt);
            case UniformKind::Float2: return static_cast<int>(Type::kFloat2);
            case UniformKind::Float3: return static_cast<int>(Type::kFloat3);
            case UniformKind::Float4: return static_cast<int>(Type::kFloat4);
            case UniformKind::Float4x4: return static_cast<int>(Type::kFloat4x4);
        }
        return static_cast<int>(Type::kFloat);
    }

    void SkiaEffectBackend::Fail(std::string message)
    {
        effect_.reset();
        uniformBytes_.clear();
        childIndexByUnit_.fill(-1);
        for (auto& texture : boundTextureBackends_)
            texture.reset();
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
        if (children.empty())
        {
            Fail("Skia SkSL v1 requires the primary `uniform shader cnaTexture0` child.");
            return false;
        }
        if (children.size() > childIndexByUnit_.size())
        {
            Fail("Skia SkSL v1 requires between one and eight named 2D shader children.");
            return false;
        }
        std::array<int, 8> childIndices = {-1, -1, -1, -1, -1, -1, -1, -1};
        for (const auto& child : children)
        {
            int unit = -1;
            if (child.type != SkRuntimeEffect::ChildType::kShader
                || !ParseTextureChild(child.name, unit)
                || childIndices[static_cast<std::size_t>(unit)] != -1)
            {
                Fail("Skia SkSL v1 children must be unique `uniform shader cnaTexture0` through "
                     "`cnaTexture7` declarations; filters, blenders, and other names are unsupported.");
                return false;
            }
            childIndices[static_cast<std::size_t>(unit)] = child.index;
        }
        if (childIndices[0] < 0)
        {
            Fail("Skia SkSL v1 requires the primary `uniform shader cnaTexture0` child.");
            return false;
        }

        const auto uniforms = result.effect->uniforms();
        if (uniforms.empty() || uniforms.size() > kSkiaSkslMaxUniformCountEXT)
        {
            Fail("Skia SkSL v1 requires 1..64 reflected uniforms including cnaTint.");
            return false;
        }
        const SkRuntimeEffect::Uniform* tintUniform = nullptr;
        for (const auto& uniform : uniforms)
        {
            if (uniform.name == "cnaTint")
            {
                if (uniform.type != SkRuntimeEffect::Uniform::Type::kFloat4
                    || uniform.flags != 0u)
                {
                    Fail("Skia SkSL v1 requires non-array `uniform float4 cnaTint` without layout flags.");
                    return false;
                }
                tintUniform = &uniform;
            }
            else if (!IsSupportedUserUniform(uniform))
            {
                Fail(std::string("Skia SkSL v1 cannot map reflected uniform `")
                     + std::string(uniform.name)
                     + "`; supported user types are float, int, float2/3/4, float4x4, "
                       "float arrays, and float2 arrays without layout/precision flags.");
                return false;
            }
        }
        if (!tintUniform)
        {
            Fail("Skia SkSL v1 requires the reserved non-array `uniform float4 cnaTint`.");
            return false;
        }

        tintOffset_ = tintUniform->offset;
        childIndexByUnit_ = childIndices;
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

    void SkiaEffectBackend::SetUniformData(
        const char* name, UniformKind kind, bool array, int count,
        const void* data, std::size_t byteCount, const char* setter)
    {
        if (!effect_)
            throw std::runtime_error(std::string("Skia ") + setter + " requires a valid SkSL effect.");
        if (!name || !*name)
            throw std::invalid_argument(std::string("Skia ") + setter + " requires a non-empty uniform name.");
        if (std::string_view(name) == "cnaTint")
            throw std::invalid_argument("Skia cnaTint is reserved and supplied by each SpriteBatch draw.");
        if (!data)
            throw std::invalid_argument(std::string("Skia ") + setter + " received null uniform data.");
        if (count < 0)
            throw std::out_of_range(std::string("Skia ") + setter + " count must not be negative.");

        const SkRuntimeEffect::Uniform* uniform = effect_->findUniform(name);
        if (!uniform)
            throw std::invalid_argument(std::string("Skia ") + setter + " cannot find uniform `" + name + "`.");
        const std::uint32_t expectedFlags = array
            ? SkRuntimeEffect::Uniform::kArray_Flag
            : 0u;
        if (static_cast<int>(uniform->type) != UniformTypeOrdinal(kind)
            || uniform->flags != expectedFlags)
        {
            throw std::invalid_argument(
                std::string("Skia ") + setter + " requires `" + name + "` to be "
                + (array ? "an array of " : "a non-array ") + UniformKindName(kind) + ".");
        }
        if (array && uniform->count != count)
        {
            throw std::invalid_argument(
                std::string("Skia ") + setter + " count for `" + name + "` must equal the declared "
                + std::to_string(uniform->count) + ".");
        }
        if (byteCount != uniform->sizeInBytes()
            || uniform->offset > uniformBytes_.size()
            || byteCount > uniformBytes_.size() - uniform->offset)
        {
            throw std::runtime_error(std::string("Skia reflected byte layout is invalid for uniform `")
                                     + name + "`.");
        }
        std::memcpy(uniformBytes_.data() + uniform->offset, data, byteCount);
    }

    void SkiaEffectBackend::SetUniformFloat(const char* name, float value)
    {
        SetUniformData(name, UniformKind::Float, false, 0, &value, sizeof(value), "SetUniformFloat");
    }

    void SkiaEffectBackend::SetUniformInt(const char* name, int value)
    {
        static_assert(sizeof(std::int32_t) == sizeof(int));
        const std::int32_t normalized = static_cast<std::int32_t>(value);
        SetUniformData(name, UniformKind::Int, false, 0, &normalized, sizeof(normalized), "SetUniformInt");
    }

    void SkiaEffectBackend::SetUniformVec2(const char* name, float x, float y)
    {
        const float value[2] = {x, y};
        SetUniformData(name, UniformKind::Float2, false, 0, value, sizeof(value), "SetUniformVec2");
    }

    void SkiaEffectBackend::SetUniformVec3(const char* name, float x, float y, float z)
    {
        const float value[3] = {x, y, z};
        SetUniformData(name, UniformKind::Float3, false, 0, value, sizeof(value), "SetUniformVec3");
    }

    void SkiaEffectBackend::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        const float value[4] = {x, y, z, w};
        SetUniformData(name, UniformKind::Float4, false, 0, value, sizeof(value), "SetUniformVec4");
    }

    void SkiaEffectBackend::SetUniformMat4(const char* name, const float* matrix)
    {
        SetUniformData(name, UniformKind::Float4x4, false, 0, matrix, sizeof(float) * 16u,
                       "SetUniformMat4");
    }

    void SkiaEffectBackend::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        const std::size_t byteCount = count >= 0
            ? static_cast<std::size_t>(count) * sizeof(float)
            : 0u;
        SetUniformData(name, UniformKind::Float, true, count, values, byteCount,
                       "SetUniformFloatArray");
    }

    void SkiaEffectBackend::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        const std::size_t byteCount = count >= 0
            ? static_cast<std::size_t>(count) * sizeof(float) * 2u
            : 0u;
        SetUniformData(name, UniformKind::Float2, true, count, values, byteCount,
                       "SetUniformVec2Array");
    }

    void SkiaEffectBackend::BindTexture(int unit, ITextureBackend* texture)
    {
        if (!effect_)
            throw std::runtime_error("Skia BindTexture requires a valid SkSL effect.");
        if (unit <= 0 || unit > kSkiaSkslMaxTextureUnitEXT)
            throw std::out_of_range("Skia SkSL cnaTexture0 is SpriteBatch-reserved; additional texture units must be 1..7.");
        if (childIndexByUnit_[static_cast<std::size_t>(unit)] < 0)
            throw std::invalid_argument(std::string("Skia effect does not declare `cnaTexture")
                                        + std::to_string(unit) + "`.");
        if (!texture)
            throw std::invalid_argument("Skia BindTexture received a null Texture2D backend.");
        const auto* source = dynamic_cast<const SkiaImageSource*>(texture);
        if (!source || !source->SnapshotImage(SkiaSourceAlphaConvention::Straight))
            throw std::invalid_argument("Skia BindTexture requires a live Skia Texture2D or RenderTarget2D.");
        std::weak_ptr<ITextureBackend> weakTexture = texture->weak_from_this();
        if (weakTexture.expired())
            throw std::invalid_argument("Skia BindTexture requires a shared, lifetime-tracked Texture2D backend.");
        // Retain no raw pointer or immutable image. Draw locks this weak backend and snapshots its
        // current pixels, so later SetData/rendering is visible and Dispose expires safely.
        boundTextureBackends_[static_cast<std::size_t>(unit)] = std::move(weakTexture);
    }

    void SkiaEffectBackend::BindTextureCube(int, ITextureCubeBackend*)
    {
        throw std::runtime_error("Skia SkSL effects do not support TextureCube children.");
    }

    void SkiaEffectBackend::BindTexture3D(int, ITexture3DBackend*)
    {
        throw std::runtime_error("Skia SkSL effects do not support Texture3D children.");
    }

    void SkiaEffectBackend::ValidateSpriteBindingsEXT() const
    {
        if (!effect_)
            throw std::runtime_error("Skia cannot validate texture children for an invalid SkSL effect.");
        for (int unit = 1; unit <= kSkiaSkslMaxTextureUnitEXT; ++unit)
        {
            if (childIndexByUnit_[static_cast<std::size_t>(unit)] >= 0
                && boundTextureBackends_[static_cast<std::size_t>(unit)].expired())
            {
                throw std::runtime_error(std::string("Skia SkSL effect requires SetTexture(")
                                         + std::to_string(unit) + ", ...) for `cnaTexture"
                                         + std::to_string(unit) + "` before SpriteBatch::Begin.");
            }
        }
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
        std::array<sk_sp<SkShader>, 8> children;
        children[static_cast<std::size_t>(childIndexByUnit_[0])] = std::move(primaryTexture);
        for (int unit = 1; unit <= kSkiaSkslMaxTextureUnitEXT; ++unit)
        {
            const int childIndex = childIndexByUnit_[static_cast<std::size_t>(unit)];
            if (childIndex < 0)
                continue;
            const std::shared_ptr<ITextureBackend> texture
                = boundTextureBackends_[static_cast<std::size_t>(unit)].lock();
            const auto* source = texture
                ? dynamic_cast<const SkiaImageSource*>(texture.get())
                : nullptr;
            const sk_sp<SkImage> image = source
                ? source->SnapshotImage(SkiaSourceAlphaConvention::Straight)
                : nullptr;
            if (!image)
                return nullptr;
            children[static_cast<std::size_t>(childIndex)] = image->makeShader(
                SkTileMode::kClamp, SkTileMode::kClamp,
                SkSamplingOptions(SkFilterMode::kLinear));
            if (!children[static_cast<std::size_t>(childIndex)])
                return nullptr;
        }
        return effect_->makeShader(std::move(uniforms), children.data(), effect_->children().size());
    }
} // namespace CNA::Internal::Backends::Skia
