#include "CNA/Internal/Backends/Skia/SkiaEffectBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaCubeSampling.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaVolumeSampling.hpp"

#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkString.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkRuntimeEffect.h"

#include "System/NotSupportedException.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

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

        // SKIA-149: cnaCubeFace0-5, matching docs/skia-cube-volume-sampling-contract.md's reserved
        // child namespace -- orthogonal to ParseTextureChild's cnaTexture0-7, never colliding with
        // it (the prefixes differ).
        [[nodiscard]] bool ParseCubeFaceChild(std::string_view name, int& face)
        {
            constexpr std::string_view prefix = "cnaCubeFace";
            if (!name.starts_with(prefix) || name.size() != prefix.size() + 1u)
                return false;
            const char digit = name.back();
            if (digit < '0' || digit > '5')
                return false;
            face = digit - '0';
            return true;
        }

        // SKIA-149: names the preamble(s) reserve for their own internal state -- an author's own
        // SetUniformXxx call is rejected for these, matching the existing cnaTint precedent, and
        // they are skipped by the general "is this a supported user uniform" type check below since
        // CompileProgram validates their exact declared type separately while capturing offsets.
        [[nodiscard]] bool IsReservedPreambleUniformName(std::string_view name) noexcept
        {
            return name == "cnaCubeFaceSizeEXT" || name == "cnaVolumeAtlasMeta0"
                || name == "cnaVolumeAtlasMeta1" || name == "cnaVolumeAddressModesEXT";
        }

        // SKIA-149: builds an immutable straight-RGBA8 SkImage directly from tightly packed bytes
        // (top row first), matching every cube face and the packed volume atlas -- both are always
        // exact `SurfaceFormat::Color` per docs/skia-cube-volume-sampling-contract.md's storage
        // decision, so no conversion or shadow is needed here.
        [[nodiscard]] sk_sp<SkImage> MakeStraightRgba8ImageEXT(
            int width, int height, const std::uint8_t* rgba)
        {
            if (width <= 0 || height <= 0 || !rgba)
                return nullptr;
            const SkImageInfo info =
                SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
            const SkPixmap pixmap(info, rgba, static_cast<std::size_t>(width) * 4u);
            return SkImages::RasterFromPixmapCopy(pixmap);
        }

        [[nodiscard]] sk_sp<SkShader> MakeClampLinearShaderEXT(const sk_sp<SkImage>& image)
        {
            return image ? image->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                             SkSamplingOptions(SkFilterMode::kLinear))
                         : nullptr;
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
        cubeFaceChildIndices_.fill(-1);
        volumeAtlasChildIndex_ = -1;
        cubeFaceSizeOffset_ = 0;
        volumeMeta0Offset_ = 0;
        volumeMeta1Offset_ = 0;
        volumeAddressModesOffset_ = 0;
        boundCubeBackend_.reset();
        boundVolumeBackend_.reset();
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

        // SKIA-149: prepend the confirmed cube/volume preambles only when the author's own source
        // actually calls the corresponding helper (docs/skia-cube-volume-sampling-contract.md).
        // An effect that never calls cnaSampleCubeEXT/cnaSampleVolumeEXT pays no extra child or
        // uniform budget and is byte-for-byte the same compile as before this task.
        const bool wantsCube = fragSrc.find("cnaSampleCubeEXT") != std::string::npos;
        const bool wantsVolume = fragSrc.find("cnaSampleVolumeEXT") != std::string::npos;
        std::string fullSource;
        fullSource.reserve(fragSrc.size()
            + (wantsCube ? kCnaSampleCubePreambleEXT.size() : 0u)
            + (wantsVolume ? kCnaSampleVolumePreambleEXT.size() : 0u));
        if (wantsCube) fullSource.append(kCnaSampleCubePreambleEXT);
        if (wantsVolume) fullSource.append(kCnaSampleVolumePreambleEXT);
        fullSource.append(fragSrc);
        if (fullSource.size() > kSkiaSkslMaxSourceBytesEXT)
        {
            Fail("Skia SkSL effect source (including any cube/volume preamble) exceeds the "
                 "65536-byte safety limit.");
            return false;
        }

        auto result = SkRuntimeEffect::MakeForShader(SkString(fullSource));
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
        constexpr std::size_t kMaxChildrenEXT = 8u + 6u + 1u; // cnaTexture0-7 + cnaCubeFace0-5 + cnaVolumeAtlas0.
        if (children.size() > kMaxChildrenEXT)
        {
            Fail("Skia SkSL v1 requires at most 15 named shader children "
                 "(cnaTexture0-7, cnaCubeFace0-5, cnaVolumeAtlas0).");
            return false;
        }
        std::array<int, 8> childIndices = {-1, -1, -1, -1, -1, -1, -1, -1};
        std::array<int, 6> cubeFaceIndices = {-1, -1, -1, -1, -1, -1};
        int volumeAtlasIndex = -1;
        for (const auto& child : children)
        {
            int unit = -1;
            int face = -1;
            if (child.type != SkRuntimeEffect::ChildType::kShader)
            {
                Fail("Skia SkSL v1 children must be unique `uniform shader cnaTexture0` through "
                     "`cnaTexture7`, `cnaCubeFace0` through `cnaCubeFace5`, or `cnaVolumeAtlas0` "
                     "declarations; filters, blenders, and other names are unsupported.");
                return false;
            }
            if (ParseTextureChild(child.name, unit))
            {
                if (childIndices[static_cast<std::size_t>(unit)] != -1)
                {
                    Fail("Skia SkSL v1 children must be unique `uniform shader cnaTexture0` "
                         "through `cnaTexture7` declarations.");
                    return false;
                }
                childIndices[static_cast<std::size_t>(unit)] = child.index;
            }
            else if (ParseCubeFaceChild(child.name, face))
            {
                // Always unique: cnaCubeFace0-5 come entirely from CNA's own source-controlled
                // preamble, never from author text, so a duplicate is impossible by construction.
                cubeFaceIndices[static_cast<std::size_t>(face)] = child.index;
            }
            else if (child.name == "cnaVolumeAtlas0")
            {
                volumeAtlasIndex = child.index;
            }
            else
            {
                Fail("Skia SkSL v1 children must be unique `uniform shader cnaTexture0` through "
                     "`cnaTexture7`, `cnaCubeFace0` through `cnaCubeFace5`, or `cnaVolumeAtlas0` "
                     "declarations; filters, blenders, and other names are unsupported.");
                return false;
            }
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
        std::size_t cubeFaceSizeOffset = 0;
        std::size_t volumeMeta0Offset = 0;
        std::size_t volumeMeta1Offset = 0;
        std::size_t volumeAddressModesOffset = 0;
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
            else if (uniform.name == "cnaCubeFaceSizeEXT")
            {
                cubeFaceSizeOffset = uniform.offset;
            }
            else if (uniform.name == "cnaVolumeAtlasMeta0")
            {
                volumeMeta0Offset = uniform.offset;
            }
            else if (uniform.name == "cnaVolumeAtlasMeta1")
            {
                volumeMeta1Offset = uniform.offset;
            }
            else if (uniform.name == "cnaVolumeAddressModesEXT")
            {
                volumeAddressModesOffset = uniform.offset;
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
        cubeFaceChildIndices_ = cubeFaceIndices;
        volumeAtlasChildIndex_ = volumeAtlasIndex;
        cubeFaceSizeOffset_ = cubeFaceSizeOffset;
        volumeMeta0Offset_ = volumeMeta0Offset;
        volumeMeta1Offset_ = volumeMeta1Offset;
        volumeAddressModesOffset_ = volumeAddressModesOffset;
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
        if (IsReservedPreambleUniformName(name))
        {
            throw std::invalid_argument(std::string("Skia `") + name + "` is reserved by the "
                "cube/volume sampling preamble and supplied automatically by SetTexture(1, "
                "TextureCube/Texture3D).");
        }
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

    void SkiaEffectBackend::BindTextureCube(int unit, ITextureCubeBackend* texture)
    {
        if (!effect_)
            throw std::runtime_error("Skia BindTextureCube requires a valid SkSL effect.");
        // SKIA-144: only unit 1 is supported for cube sampling, reusing the same numbering space
        // SetTexture(unit, Texture2D) already uses -- a second bound cube map is out of scope.
        if (unit != 1)
        {
            throw std::out_of_range(
                "Skia SkSL cube sampling supports only SetTexture(1, TextureCube).");
        }
        if (cubeFaceChildIndices_[0] < 0)
        {
            throw std::invalid_argument(
                "Skia effect does not call cnaSampleCubeEXT; no `cnaCubeFace*` children are "
                "declared.");
        }
        if (!texture)
            throw std::invalid_argument("Skia BindTextureCube received a null TextureCube backend.");
        std::weak_ptr<ITextureCubeBackend> weakTexture = texture->weak_from_this();
        if (weakTexture.expired())
        {
            throw std::invalid_argument(
                "Skia BindTextureCube requires a shared, lifetime-tracked TextureCube backend.");
        }
        // Retain no raw pointer or immutable image. Draw locks this weak backend and reads its
        // current six faces, so later SetData/rendering is visible and Dispose expires safely --
        // identical contract to BindTexture above.
        boundCubeBackend_ = std::move(weakTexture);
    }

    void SkiaEffectBackend::BindTexture3D(int unit, ITexture3DBackend* texture)
    {
        if (!effect_)
            throw std::runtime_error("Skia BindTexture3D requires a valid SkSL effect.");
        if (unit != 1)
        {
            throw std::out_of_range(
                "Skia SkSL volume sampling supports only SetTexture(1, Texture3D).");
        }
        if (volumeAtlasChildIndex_ < 0)
        {
            throw std::invalid_argument(
                "Skia effect does not call cnaSampleVolumeEXT; no `cnaVolumeAtlas0` child is "
                "declared.");
        }
        if (!texture)
            throw std::invalid_argument("Skia BindTexture3D received a null Texture3D backend.");
        std::weak_ptr<ITexture3DBackend> weakTexture = texture->weak_from_this();
        if (weakTexture.expired())
        {
            throw std::invalid_argument(
                "Skia BindTexture3D requires a shared, lifetime-tracked Texture3D backend.");
        }
        // SKIA-150: the packed grid atlas (docs/skia-cube-volume-sampling-contract.md's "Resource
        // limits") is a new, additional allocation distinct from the volume's own already-budgeted
        // linear voxel storage -- padding overhead means a volume that fits its plain storage
        // budget can still produce an over-budget atlas. Reject transactionally, before storing
        // any bound state, exactly like every other Skia CPU allocation's 256 MiB ceiling.
        int width = 0;
        int height = 0;
        int depth = 0;
        texture->GetDimensionsEXT(width, height, depth);
        const VolumeAtlasLayoutEXT layout = ComputeVolumeAtlasLayoutEXT(width, height, depth);
        std::size_t atlasBytes = 0;
        if (!CheckedTexelBytes2D(static_cast<std::size_t>(layout.atlasWidth),
                                 static_cast<std::size_t>(layout.atlasHeight), 4u, atlasBytes)
            || atlasBytes > kSkiaCpuTextureStorageLimitBytes)
        {
            throw System::NotSupportedException(
                "Skia volume sampling atlas exceeds the 256 MiB per-resource CPU storage limit.");
        }
        boundVolumeBackend_ = std::move(weakTexture);
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
        if (cubeFaceChildIndices_[0] >= 0 && boundCubeBackend_.expired())
        {
            throw std::runtime_error(
                "Skia SkSL effect calls cnaSampleCubeEXT and requires SetTexture(1, TextureCube) "
                "before SpriteBatch::Begin.");
        }
        if (volumeAtlasChildIndex_ >= 0 && boundVolumeBackend_.expired())
        {
            throw std::runtime_error(
                "Skia SkSL effect calls cnaSampleVolumeEXT and requires SetTexture(1, Texture3D) "
                "before SpriteBatch::Begin.");
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

        constexpr std::size_t kMaxChildrenEXT = 8u + 6u + 1u;
        std::array<sk_sp<SkShader>, kMaxChildrenEXT> children;
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

        // SKIA-149: cube sampling reads the bound TextureCube/RenderTargetCube's six CURRENT
        // faces fresh on every draw (via the shared ITextureCubeBackend::GetData contract, which
        // already resynchronizes a dirty RenderTargetCube face before reading), matching
        // BindTexture's "live reference, not a snapshot" semantic above -- a SetData or render
        // between SetTexture(1, ...) and this draw is visible, and an expired weak backend fails
        // this draw rather than sampling stale or fabricated pixels.
        if (cubeFaceChildIndices_[0] >= 0)
        {
            const std::shared_ptr<ITextureCubeBackend> cube = boundCubeBackend_.lock();
            const int size = cube ? cube->GetSizeEXT() : 0;
            if (!cube || size <= 0)
                return nullptr;
            const float faceSize = static_cast<float>(size);
            if (cubeFaceSizeOffset_ + sizeof(faceSize) > drawUniforms.size())
                return nullptr;
            std::memcpy(drawUniforms.data() + cubeFaceSizeOffset_, &faceSize, sizeof(faceSize));

            std::vector<std::uint8_t> faceBytes(
                static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);
            for (int face = 0; face < 6; ++face)
            {
                if (!cube->GetData(face, 0, 0, 0, size, size, faceBytes.data(),
                                   static_cast<int>(faceBytes.size())))
                {
                    return nullptr;
                }
                const sk_sp<SkImage> image =
                    MakeStraightRgba8ImageEXT(size, size, faceBytes.data());
                const sk_sp<SkShader> shader = MakeClampLinearShaderEXT(image);
                if (!shader)
                    return nullptr;
                children[static_cast<std::size_t>(cubeFaceChildIndices_[static_cast<std::size_t>(face)])]
                    = shader;
            }
        }

        // SKIA-149: volume sampling repacks the bound Texture3D's CURRENT level-0 voxels into a
        // fresh atlas every draw, for the same "live reference" reason as cube sampling above.
        // Address modes are fixed to Clamp (0, 0, 0) for this integration task; wiring the active
        // SamplerState's AddressU/V/W through to cnaVolumeAddressModesEXT is a bounded follow-up
        // that does not change this preamble's already-confirmed formula (SKIA-148).
        if (volumeAtlasChildIndex_ >= 0)
        {
            const std::shared_ptr<ITexture3DBackend> volume = boundVolumeBackend_.lock();
            int width = 0, height = 0, depth = 0;
            if (volume)
                volume->GetDimensionsEXT(width, height, depth);
            if (!volume || width <= 0 || height <= 0 || depth <= 0)
                return nullptr;

            std::vector<std::uint8_t> voxelBytes(
                static_cast<std::size_t>(width) * height * depth * 4u);
            if (!volume->GetData(0, 0, 0, 0, width, height, depth, voxelBytes.data(),
                                 static_cast<int>(voxelBytes.size())))
            {
                return nullptr;
            }
            const VolumeAtlasLayoutEXT layout = ComputeVolumeAtlasLayoutEXT(width, height, depth);
            const std::vector<std::uint8_t> atlasBytes =
                PackVolumeAtlasEXT(width, height, depth, voxelBytes.data(), voxelBytes.size());
            if (atlasBytes.empty())
                return nullptr;
            const sk_sp<SkImage> atlasImage =
                MakeStraightRgba8ImageEXT(layout.atlasWidth, layout.atlasHeight, atlasBytes.data());
            const sk_sp<SkShader> atlasShader = MakeClampLinearShaderEXT(atlasImage);
            if (!atlasShader)
                return nullptr;

            const float meta0[4] = {static_cast<float>(layout.cols), static_cast<float>(layout.rows),
                                    static_cast<float>(depth), 0.0f};
            const float meta1[4] = {static_cast<float>(layout.tileWidth),
                                    static_cast<float>(layout.tileHeight), 0.0f, 0.0f};
            const float addressModes[3] = {0.0f, 0.0f, 0.0f}; // Clamp/Clamp/Clamp for this task.
            if (volumeMeta0Offset_ + sizeof(meta0) > drawUniforms.size()
                || volumeMeta1Offset_ + sizeof(meta1) > drawUniforms.size()
                || volumeAddressModesOffset_ + sizeof(addressModes) > drawUniforms.size())
            {
                return nullptr;
            }
            std::memcpy(drawUniforms.data() + volumeMeta0Offset_, meta0, sizeof(meta0));
            std::memcpy(drawUniforms.data() + volumeMeta1Offset_, meta1, sizeof(meta1));
            std::memcpy(drawUniforms.data() + volumeAddressModesOffset_, addressModes,
                       sizeof(addressModes));
            children[static_cast<std::size_t>(volumeAtlasChildIndex_)] = atlasShader;
        }

        sk_sp<const SkData> uniforms = SkData::MakeWithCopy(
            drawUniforms.data(), drawUniforms.size());
        return effect_->makeShader(std::move(uniforms), children.data(), effect_->children().size());
    }
} // namespace CNA::Internal::Backends::Skia
