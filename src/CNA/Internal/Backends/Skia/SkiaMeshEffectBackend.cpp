#include "CNA/Internal/Backends/Skia/SkiaMeshEffectBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"

#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkString.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkRuntimeEffect.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        // Mirrors SkiaEffectBackend's own ParseTextureChild exactly (docs/skia-vertices-2d-effect-
        // contract.md deliberately reuses the same cnaTexture0-7 naming for implementation
        // consistency) -- but the mesh ABI has no reserved primary, so unit 0 is an ordinary,
        // optional child here, unlike the sprite ABI's SpriteBatch-supplied cnaTexture0.
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

    std::shared_ptr<const SkiaMeshEffectCompiledEXT> SkiaMeshEffectCacheEXT::GetOrCompileEXT(
        const std::string& source, std::string& compileError)
    {
        compileError.clear();
        const auto cached = entries_.find(source);
        if (cached != entries_.end())
            return cached->second;

        if (source.empty())
        {
            compileError = "Skia SkSL mesh effect source must not be empty.";
            return nullptr;
        }
        if (source.size() > kSkiaSkslMaxSourceBytesEXT)
        {
            compileError = "Skia SkSL mesh effect source exceeds the 65536-byte safety limit.";
            return nullptr;
        }

        auto result = SkRuntimeEffect::MakeForShader(SkString(source));
        if (!result.effect)
        {
            compileError = std::string("Skia SkSL mesh effect compilation failed: ")
                + result.errorText.c_str();
            return nullptr;
        }
        if (result.effect->uniformSize() > kSkiaSkslMaxUniformBytesEXT)
        {
            compileError = "Skia SkSL mesh effect reflected uniform block exceeds the "
                "16384-byte safety limit.";
            return nullptr;
        }

        const auto children = result.effect->children();
        if (children.size() > kSkiaSkslMaxTextureUnitEXT + 1u)
        {
            compileError = "Skia SkSL mesh effect requires at most 8 named shader children "
                "(cnaTexture0-7).";
            return nullptr;
        }
        auto compiled = std::make_shared<SkiaMeshEffectCompiledEXT>();
        for (const auto& child : children)
        {
            int unit = -1;
            if (child.type != SkRuntimeEffect::ChildType::kShader || !ParseTextureChild(child.name, unit))
            {
                compileError = "Skia SkSL mesh effect children must be unique `uniform shader "
                    "cnaTexture0` through `cnaTexture7` declarations; filters, blenders, and "
                    "other names are unsupported.";
                return nullptr;
            }
            if (compiled->childIndexByUnit[static_cast<std::size_t>(unit)] != -1)
            {
                compileError = "Skia SkSL mesh effect children must be unique `uniform shader "
                    "cnaTexture0` through `cnaTexture7` declarations.";
                return nullptr;
            }
            compiled->childIndexByUnit[static_cast<std::size_t>(unit)] = child.index;
        }

        for (const auto& uniform : result.effect->uniforms())
        {
            if (!IsSupportedUserUniform(uniform))
            {
                compileError = std::string("Skia SkSL mesh effect uniform `") + std::string(uniform.name)
                    + "` has an unsupported reflected type; only non-array float/int/float2/"
                      "float3/float4/float4x4 and float/float2 arrays are accepted.";
                return nullptr;
            }
        }

        compiled->effect = std::move(result.effect);
        entries_.emplace(source, compiled);
        return compiled;
    }

    const char* SkiaMeshEffectBackend::UniformKindName(UniformKind kind) noexcept
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

    int SkiaMeshEffectBackend::UniformTypeOrdinal(UniformKind kind) noexcept
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

    void SkiaMeshEffectBackend::Fail(std::string message)
    {
        compiled_.reset();
        uniformBytes_.clear();
        for (auto& texture : boundTextureBackends_)
            texture.reset();
        compileError_ = std::move(message);
    }

    bool SkiaMeshEffectBackend::CompileProgram(
        const std::string& marker, const std::string& source, SkiaMeshEffectCacheEXT& cache)
    {
        Fail({});
        if (marker != kSkiaSkslMeshEffectMarkerEXT)
        {
            Fail("Skia SkSL mesh effects require the exact marker CNA_SKIA_SKSL_MESH_V1; "
                 "untagged GLSL, SPIR-V, the sprite ABI's own CNA_SKIA_SKSL_V1 marker, and "
                 "arbitrary vertex stages are unsupported.");
            return false;
        }
        std::string error;
        std::shared_ptr<const SkiaMeshEffectCompiledEXT> compiled = cache.GetOrCompileEXT(source, error);
        if (!compiled)
        {
            Fail(std::move(error));
            return false;
        }
        // Every backend instance owns its own independent uniform buffer and texture bindings even
        // on a cache hit (docs/skia-vertices-2d-effect-contract.md's clone-isolation requirement) --
        // only the immutable compiled program itself is shared.
        uniformBytes_.assign(compiled->effect->uniformSize(), 0u);
        compiled_ = std::move(compiled);
        compileError_.clear();
        return true;
    }

    void SkiaMeshEffectBackend::SetUniformData(
        const char* name, UniformKind kind, bool array, int count,
        const void* data, std::size_t byteCount, const char* setter)
    {
        if (!compiled_)
            throw std::runtime_error(std::string("Skia ") + setter + " requires a valid SkSL mesh effect.");
        if (!name || !*name)
            throw std::invalid_argument(std::string("Skia ") + setter + " requires a non-empty uniform name.");
        if (!data)
            throw std::invalid_argument(std::string("Skia ") + setter + " received null uniform data.");
        if (count < 0)
            throw std::out_of_range(std::string("Skia ") + setter + " count must not be negative.");

        const SkRuntimeEffect::Uniform* uniform = compiled_->effect->findUniform(name);
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

    void SkiaMeshEffectBackend::SetUniformFloat(const char* name, float value)
    {
        SetUniformData(name, UniformKind::Float, false, 0, &value, sizeof(value), "SetUniformFloat");
    }

    void SkiaMeshEffectBackend::SetUniformInt(const char* name, int value)
    {
        static_assert(sizeof(std::int32_t) == sizeof(int));
        const std::int32_t normalized = static_cast<std::int32_t>(value);
        SetUniformData(name, UniformKind::Int, false, 0, &normalized, sizeof(normalized), "SetUniformInt");
    }

    void SkiaMeshEffectBackend::SetUniformVec2(const char* name, float x, float y)
    {
        const float value[2] = {x, y};
        SetUniformData(name, UniformKind::Float2, false, 0, value, sizeof(value), "SetUniformVec2");
    }

    void SkiaMeshEffectBackend::SetUniformVec3(const char* name, float x, float y, float z)
    {
        const float value[3] = {x, y, z};
        SetUniformData(name, UniformKind::Float3, false, 0, value, sizeof(value), "SetUniformVec3");
    }

    void SkiaMeshEffectBackend::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        const float value[4] = {x, y, z, w};
        SetUniformData(name, UniformKind::Float4, false, 0, value, sizeof(value), "SetUniformVec4");
    }

    void SkiaMeshEffectBackend::SetUniformMat4(const char* name, const float* matrix)
    {
        SetUniformData(name, UniformKind::Float4x4, false, 0, matrix, sizeof(float) * 16u,
                       "SetUniformMat4");
    }

    void SkiaMeshEffectBackend::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        const std::size_t byteCount = count >= 0
            ? static_cast<std::size_t>(count) * sizeof(float)
            : 0u;
        SetUniformData(name, UniformKind::Float, true, count, values, byteCount,
                       "SetUniformFloatArray");
    }

    void SkiaMeshEffectBackend::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        const std::size_t byteCount = count >= 0
            ? static_cast<std::size_t>(count) * sizeof(float) * 2u
            : 0u;
        SetUniformData(name, UniformKind::Float2, true, count, values, byteCount,
                       "SetUniformVec2Array");
    }

    void SkiaMeshEffectBackend::BindTexture(int unit, ITextureBackend* texture)
    {
        if (!compiled_)
            throw std::runtime_error("Skia mesh BindTexture requires a valid SkSL mesh effect.");
        if (unit < 0 || unit > kSkiaSkslMaxTextureUnitEXT)
            throw std::out_of_range("Skia SkSL mesh texture units must be 0..7.");
        if (compiled_->childIndexByUnit[static_cast<std::size_t>(unit)] < 0)
            throw std::invalid_argument(std::string("Skia mesh effect does not declare `cnaTexture")
                                        + std::to_string(unit) + "`.");
        if (!texture)
            throw std::invalid_argument("Skia mesh BindTexture received a null Texture2D backend.");
        const auto* source = dynamic_cast<const SkiaImageSource*>(texture);
        if (!source || !source->SnapshotImage(SkiaSourceAlphaConvention::Straight))
            throw std::invalid_argument("Skia mesh BindTexture requires a live Skia Texture2D or RenderTarget2D.");
        std::weak_ptr<ITextureBackend> weakTexture = texture->weak_from_this();
        if (weakTexture.expired())
            throw std::invalid_argument("Skia mesh BindTexture requires a shared, lifetime-tracked Texture2D backend.");
        // Retain no raw pointer or immutable image. Draw locks this weak backend and snapshots its
        // current pixels, so later SetData/rendering is visible and Dispose expires safely --
        // identical contract to SkiaEffectBackend::BindTexture.
        boundTextureBackends_[static_cast<std::size_t>(unit)] = std::move(weakTexture);
    }

    void SkiaMeshEffectBackend::ValidateMeshBindingsEXT() const
    {
        if (!compiled_)
            throw std::runtime_error("Skia cannot validate texture children for an invalid SkSL mesh effect.");
        for (int unit = 0; unit <= kSkiaSkslMaxTextureUnitEXT; ++unit)
        {
            if (compiled_->childIndexByUnit[static_cast<std::size_t>(unit)] >= 0
                && boundTextureBackends_[static_cast<std::size_t>(unit)].expired())
            {
                throw std::runtime_error(std::string("Skia SkSL mesh effect requires BindTexture(")
                                         + std::to_string(unit) + ", ...) for `cnaTexture"
                                         + std::to_string(unit) + "` before drawing.");
            }
        }
    }

    sk_sp<SkShader> SkiaMeshEffectBackend::MakeMeshShaderEXT() const
    {
        if (!compiled_)
            return nullptr;

        std::array<sk_sp<SkShader>, 8> children;
        for (int unit = 0; unit <= kSkiaSkslMaxTextureUnitEXT; ++unit)
        {
            const int childIndex = compiled_->childIndexByUnit[static_cast<std::size_t>(unit)];
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

        const sk_sp<SkData> uniformData =
            SkData::MakeWithCopy(uniformBytes_.data(), uniformBytes_.size());
        return compiled_->effect->makeShader(
            uniformData, children.data(), compiled_->effect->children().size());
    }
} // namespace CNA::Internal::Backends::Skia
