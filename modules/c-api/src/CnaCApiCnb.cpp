// SPDX-License-Identifier: MS-PL

#include "CNA/C/cnb.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbChunkCompression.hpp"
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelData.hpp"
#include "CNA/Content/Cnb/CnbModelFromCnj.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "CNA/Content/ForeignContentObjectEXT.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "CNA/Content/Cnb/CnbReadLimits.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureFormat.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"

#include <any>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InvalidStateIf;
using CNA::C::Detail::IoError;
using CNA::C::Detail::IsValidTimeSpanSeconds;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::Overflow;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::RequireIndex;
using CNA::C::Detail::ValidateBuffer;
using CNA::C::Detail::ValidateCanonicalBool;

namespace Cnb = CNA::Content::Cnb;

namespace {

using CNA::C::Detail::CopyBytes;
using CNA::C::Detail::CopyElements;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ReportTextSize;
using CNA::C::Detail::ToCBool;

/// The count half of every count/copy pair in this family.

/// The copy half. No terminator, `out_bytes` always written, and a short capacity writes nothing.

/// The same shape for bytes rather than text.

/**
 * Validates a caller-supplied byte range and narrows its count to the host's own size type.
 *
 * A count above `size_t` cannot describe a real buffer, and letting it wrap would hand a span a
 * length the caller never allocated.
 */
[[nodiscard]] CNA_Result BorrowBytes(
    const uint8_t* const data,
    const uint64_t byteCount,
    std::span<const uint8_t>* const outSpan)
{
    if (const CNA_Result result = ValidateBuffer(data, byteCount);
        result != CNA_RESULT_SUCCESS) {
        return InvalidArgument("The byte range is null with a non-zero count.");
    }
    if (byteCount > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return Overflow("The byte count does not fit in this host's size type.");
    }
    *outSpan = byteCount == 0U
        ? std::span<const uint8_t>{}
        : std::span<const uint8_t>(data, static_cast<std::size_t>(byteCount));
    return CNA_RESULT_SUCCESS;
}

/// `CnbCompression` is an open wire value: a corrupt file can name any 32-bit codec, and the
/// canonical layer renders and refuses one without pretending it is a different mistake.
[[nodiscard]] Cnb::CnbCompression ToCodec(const CNA_CnbCompression codec) noexcept
{
    return static_cast<Cnb::CnbCompression>(codec);
}

/**
 * Compresses into @p out, deciding the two canonical refusals here rather than recovering them
 * from an exception type.
 *
 * The order mirrors `CompressCnbChunk` exactly: `None` is answered before anything else is asked,
 * so a caller passing it never depends on which codecs this build carries.
 */
[[nodiscard]] CNA_Result CompressInto(
    const std::span<const uint8_t> raw,
    const CNA_CnbCompression codec,
    const int32_t level,
    std::vector<uint8_t>* const out)
{
    const Cnb::CnbCompression nativeCodec = ToCodec(codec);
    if (nativeCodec != Cnb::CnbCompression::None &&
        !Cnb::IsCnbCompressionSupported(nativeCodec)) {
        return NotSupported(
            "CNB chunk compression uses codec " + Cnb::CnbCompressionToString(nativeCodec) +
            ", which this build does not implement.");
    }
    *out = Cnb::CompressCnbChunk(raw, nativeCodec, static_cast<int>(level));
    return CNA_RESULT_SUCCESS;
}

/* --- CBIND-107: the objects the container family owns --------------------------------------- */

/**
 * A parsed document, plus a count of the readers currently borrowing its bytes.
 *
 * A reader opened from a document points into memory the document owns. Retaining the document
 * through the reader would already be enough to keep that memory alive, but this ABI's rule is that
 * a borrow also blocks the owner's release: a leaked reader then shows up as a refused destroy
 * rather than as a document that quietly outlives its handle.
 */
struct CnbDocumentResource final {
    std::shared_ptr<Cnb::CnbDocument> value;
    uint64_t activeBorrowCount = 0U;
};

/// Increments an owner's borrow count for as long as it lives, and keeps the owner alive.
template<typename TOwner>
class CountedBorrow final {
public:
    explicit CountedBorrow(std::shared_ptr<TOwner> owner)
        : owner_(std::move(owner))
    {
        if (owner_->activeBorrowCount == std::numeric_limits<uint64_t>::max()) {
            throw std::overflow_error("The CNB document borrow count cannot be incremented.");
        }
        ++owner_->activeBorrowCount;
    }

    CountedBorrow(const CountedBorrow&) = delete;
    CountedBorrow& operator=(const CountedBorrow&) = delete;

    ~CountedBorrow()
    {
        if (owner_ != nullptr && owner_->activeBorrowCount != 0U) {
            --owner_->activeBorrowCount;
        }
    }

private:
    std::shared_ptr<TOwner> owner_;
};

/**
 * A cursor, plus whatever keeps the bytes under it alive.
 *
 * Exactly one of `ownedBytes` and `documentBorrow` is engaged. A cursor created over a caller's
 * buffer owns a copy, because C has no way to be told "keep that alive"; a cursor opened from a
 * document borrows instead, which costs no copy and is policed by the borrow count.
 *
 * `lastString` is where a decoded string waits between the read that consumed it and the copy that
 * hands it over -- see cna_cnb_reader_read_string for why those are two routes.
 */
struct CnbReaderResource final {
    std::shared_ptr<Cnb::CnbByteReader> value;
    std::shared_ptr<std::vector<uint8_t>> ownedBytes;
    std::shared_ptr<CountedBorrow<CnbDocumentResource>> documentBorrow;
    std::optional<std::string> lastString;
};

struct CnbByteWriterResource final {
    std::shared_ptr<Cnb::CnbByteWriter> value;
};

struct CnbWriterResource final {
    std::shared_ptr<Cnb::CnbWriter> value;
    /// Names live beside the POD entries because a C structure cannot hold a string; the two
    /// vectors are appended to together and are always the same length.
    std::vector<Cnb::CnbExternalReference> externalReferences;
};

[[nodiscard]] CNA_Result GetDocument(
    const CNA_CnbDocumentHandle handle,
    std::shared_ptr<CnbDocumentResource>* const outDocument)
{
    return LookupHandle(handle, ObjectKind::CnbDocument, outDocument);
}

[[nodiscard]] CNA_Result GetReader(
    const CNA_CnbReaderHandle handle,
    std::shared_ptr<CnbReaderResource>* const outReader)
{
    return LookupHandle(handle, ObjectKind::CnbReader, outReader);
}

[[nodiscard]] CNA_Result GetByteWriter(
    const CNA_CnbByteWriterHandle handle,
    std::shared_ptr<CnbByteWriterResource>* const outWriter)
{
    return LookupHandle(handle, ObjectKind::CnbByteWriter, outWriter);
}

[[nodiscard]] CNA_Result GetWriter(
    const CNA_CnbWriterHandle handle,
    std::shared_ptr<CnbWriterResource>* const outWriter)
{
    return LookupHandle(handle, ObjectKind::CnbWriter, outWriter);
}

/// Resolves the caller's optional limits pointer, applying the prefix rule when one is supplied.
[[nodiscard]] CNA_Result ResolveLimits(
    const CNA_CnbReadLimits* const limits,
    Cnb::CnbReadLimits* const outLimits)
{
    if (limits == nullptr) {
        *outLimits = Cnb::DefaultCnbReadLimits();
        return CNA_RESULT_SUCCESS;
    }
    if (!IsVersionedStructure(limits, CNA_CNB_READ_LIMITS_STRUCT_VERSION)) {
        return InvalidArgument("The read-limits structure is not a known size and version.");
    }
    outLimits->maxFileSize = limits->max_file_size;
    outLimits->maxChunkSize = limits->max_chunk_size;
    outLimits->maxTotalUncompressedSize = limits->max_total_uncompressed_size;
    outLimits->maxChunkCount = limits->max_chunk_count;
    outLimits->maxStringBytes = limits->max_string_bytes;
    outLimits->maxArrayElementCount = limits->max_array_element_count;
    outLimits->maxChunkAlignment = limits->max_chunk_alignment;
    return CNA_RESULT_SUCCESS;
}

void WriteLimits(const Cnb::CnbReadLimits& limits, CNA_CnbReadLimits* const out)
{
    out->max_file_size = limits.maxFileSize;
    out->max_chunk_size = limits.maxChunkSize;
    out->max_total_uncompressed_size = limits.maxTotalUncompressedSize;
    out->max_chunk_count = limits.maxChunkCount;
    out->max_string_bytes = limits.maxStringBytes;
    out->max_array_element_count = limits.maxArrayElementCount;
    out->max_chunk_alignment = limits.maxChunkAlignment;
}

/**
 * An index a caller supplied is an argument, not a corrupt file.
 *
 * The canonical accessors throw ContentLoadException for an out-of-range index, which the firewall
 * would report as CNA_RESULT_IO -- the answer for a bad file. A caller fixes an index; it does not
 * re-download the asset. The distinction is therefore decided here.
 */
/// The file path the DDS and WAV import routes take, read as their spec group's lookup.
[[nodiscard]] CNA_Result ReadImportPath(const CNA_StringView value, std::string* const out)
{
    return ReadText(value, "The import path is not valid UTF-8.", out);
}

/// CBIND-108: the decoded texture description. Nested vectors, so a handle rather than a POD.
struct CnbTextureDataResource final {
    std::shared_ptr<Cnb::CnbTextureData> value;
};

[[nodiscard]] CNA_Result GetTextureData(
    const CNA_CnbTextureDataHandle handle,
    std::shared_ptr<CnbTextureDataResource>* const outTexture)
{
    return LookupHandle(handle, ObjectKind::CnbTextureData, outTexture);
}

[[nodiscard]] Cnb::CnbTextureFormat ToTextureFormat(const CNA_CnbTextureFormat format) noexcept
{
    return static_cast<Cnb::CnbTextureFormat>(format);
}

[[nodiscard]] CNA_Result CreateTextureData(
    Cnb::CnbTextureData data,
    CNA_CnbTextureDataHandle* const outTexture)
{
    return CreateResourceHandle<CnbTextureDataResource>(
        ObjectKind::CnbTextureData,
        std::make_shared<Cnb::CnbTextureData>(std::move(data)),
        outTexture);
}

/// CBIND-109: the model graph. One handle for the whole of it; its nodes are reached by index.
struct CnbModelResource final {
    std::shared_ptr<Cnb::CnbModelData> value;
};

/// CBIND-109: a `.cnj` compile, holding the model until a caller takes it out.
struct CnbModelFromCnjResource final {
    std::shared_ptr<Cnb::CnbModelFromCnjResult> value;
    bool modelTaken = false;
};

[[nodiscard]] CNA_Result GetModel(
    const CNA_CnbModelDataHandle handle,
    std::shared_ptr<CnbModelResource>* const outModel)
{
    return LookupHandle(handle, ObjectKind::CnbModelData, outModel);
}

[[nodiscard]] CNA_Result GetCompileResult(
    const CNA_CnbModelFromCnjHandle handle,
    std::shared_ptr<CnbModelFromCnjResource>* const outResult)
{
    return LookupHandle(handle, ObjectKind::CnbModelFromCnj, outResult);
}

[[nodiscard]] CNA_Result CreateModel(
    Cnb::CnbModelData data,
    CNA_CnbModelDataHandle* const outModel)
{
    return CreateResourceHandle<CnbModelResource>(
        ObjectKind::CnbModelData, std::make_shared<Cnb::CnbModelData>(std::move(data)), outModel);
}

/// The prefix rule, in the one place every versioned model structure can share it.
template<typename TStruct>
[[nodiscard]] CNA_Result RequireStruct(
    const TStruct* const value,
    const uint32_t version,
    const char* const what)
{
    if (value == nullptr) {
        return InvalidArgument(what);
    }
    if (!IsVersionedStructure(value, version)) {
        return InvalidArgument(what);
    }
    return CNA_RESULT_SUCCESS;
}

/// The input half: validate a caller's array and narrow its count to the host's own size type.
template<typename TElement>
[[nodiscard]] CNA_Result BorrowElements(
    const TElement* const data,
    const uint64_t count,
    std::span<const TElement>* const outSpan)
{
    if (data == nullptr && count != 0U) {
        return InvalidArgument("The element range is null with a non-zero count.");
    }
    if (count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max() / sizeof(TElement))) {
        return Overflow("The element count does not fit in this host's size type.");
    }
    *outSpan = count == 0U
        ? std::span<const TElement>{}
        : std::span<const TElement>(data, static_cast<std::size_t>(count));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetPart(
    const std::shared_ptr<CnbModelResource>& model,
    const uint64_t index,
    Cnb::CnbModelPart** const outPart)
{
    if (const CNA_Result result =
            RequireIndex(index, model->value->parts.size(), "The part index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outPart = &model->value->parts[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

/// A part's morph data is optional, so reaching it is a two-step check the routes share.
[[nodiscard]] CNA_Result GetMorph(
    const std::shared_ptr<CnbModelResource>& model,
    const uint64_t part,
    Cnb::CnbMorphData** const outMorph)
{
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!target->morph.has_value()) {
        return InvalidArgument("The part carries no morph-target data.");
    }
    *outMorph = &target->morph.value();
    return CNA_RESULT_SUCCESS;
}

// The two lookups the part and morph routes generate from: the model handle and an index, with
// the model kept alive beside the part (or its morph data) the route works on.
[[nodiscard]] CNA_Result GetModelPart(
    const CNA_CnbModelDataHandle handle,
    const uint64_t index,
    std::shared_ptr<CnbModelResource>* const outModel,
    Cnb::CnbModelPart** const outPart)
{
    if (const CNA_Result result = GetModel(handle, outModel); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return GetPart(*outModel, index, outPart);
}

[[nodiscard]] CNA_Result GetModelMorph(
    const CNA_CnbModelDataHandle handle,
    const uint64_t part,
    std::shared_ptr<CnbModelResource>* const outModel,
    Cnb::CnbMorphData** const outMorph)
{
    if (const CNA_Result result = GetModel(handle, outModel); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return GetMorph(*outModel, part, outMorph);
}

/// The eight named texture slots, in the order `CNA_CnbMaterialTextureSlot` declares them.
[[nodiscard]] std::string* MaterialTextureSlot(
    Cnb::CnbMaterial& material,
    const CNA_CnbMaterialTextureSlot slot) noexcept
{
    switch (slot) {
    case CNA_CNB_MATERIAL_TEXTURE_BASE_COLOR: return &material.baseColorTexture;
    case CNA_CNB_MATERIAL_TEXTURE_SECOND: return &material.texture2;
    case CNA_CNB_MATERIAL_TEXTURE_NORMAL: return &material.normalMap;
    case CNA_CNB_MATERIAL_TEXTURE_METALLIC_ROUGHNESS: return &material.metallicRoughnessMap;
    case CNA_CNB_MATERIAL_TEXTURE_EMISSIVE: return &material.emissiveMap;
    case CNA_CNB_MATERIAL_TEXTURE_OCCLUSION: return &material.occlusionMap;
    case CNA_CNB_MATERIAL_TEXTURE_SPECULAR: return &material.specularMap;
    case CNA_CNB_MATERIAL_TEXTURE_SPECULAR_COLOR: return &material.specularColorMap;
    default: return nullptr;
    }
}

/// Reaching a per-slot array element. The index space is the importer's seven, not the eight above.
[[nodiscard]] CNA_Result RequireTextureSlot(const uint64_t slot)
{
    return RequireIndex(
        slot, Cnb::CnbTextureSlotCount, "The importer texture slot index is out of range.");
}

/// A morph target's three delta streams, and a weight key's three float streams.
[[nodiscard]] std::vector<float>* MorphDeltaStream(
    Cnb::CnbMorphTarget& target,
    const CNA_CnbMorphDeltaStream stream) noexcept
{
    switch (stream) {
    case CNA_CNB_MORPH_DELTA_POSITION: return &target.positionDeltas;
    case CNA_CNB_MORPH_DELTA_NORMAL: return &target.normalDeltas;
    case CNA_CNB_MORPH_DELTA_TANGENT: return &target.tangentDeltas;
    default: return nullptr;
    }
}

[[nodiscard]] std::vector<float>* MorphKeyStream(
    Cnb::CnbMorphWeightKey& key,
    const CNA_CnbMorphKeyStream stream) noexcept
{
    switch (stream) {
    case CNA_CNB_MORPH_KEY_WEIGHTS: return &key.weights;
    case CNA_CNB_MORPH_KEY_IN_TANGENT: return &key.inTangent;
    case CNA_CNB_MORPH_KEY_OUT_TANGENT: return &key.outTangent;
    default: return nullptr;
    }
}

/// The numeric half of a part: shared by the route that appends one and the one that replaces one.
[[nodiscard]] CNA_Result ApplyPartInfo(
    const CNA_CnbModelPartInfo& info,
    Cnb::CnbModelPart* const part)
{
    if (info.effect_kind > CNA_CNB_EFFECT_KIND_MAXIMUM) {
        return InvalidArgument("The effect kind is not a CnbEffectKind value.");
    }
    part->vertexStride = info.vertex_stride;
    part->vertexCount = info.vertex_count;
    part->indexCount = info.index_count;
    part->indexElementSize = info.index_element_size;
    part->primitiveTopology = info.primitive_topology;
    part->primitiveCount = info.primitive_count;
    part->effectKind = static_cast<Cnb::CnbEffectKind>(info.effect_kind);
    part->vertexColorEnabled = info.vertex_color_enabled != CNA_FALSE;
    part->unlit = info.unlit != CNA_FALSE;
    return CNA_RESULT_SUCCESS;
}

void ReadPartInfo(const Cnb::CnbModelPart& part, CNA_CnbModelPartInfo* const info)
{
    info->vertex_stride = part.vertexStride;
    info->vertex_count = part.vertexCount;
    info->index_count = part.indexCount;
    info->index_element_size = part.indexElementSize;
    info->primitive_topology = part.primitiveTopology;
    info->primitive_count = part.primitiveCount;
    info->effect_kind = static_cast<CNA_CnbEffectKind>(part.effectKind);
    info->vertex_color_enabled = ToCBool(part.vertexColorEnabled);
    info->unlit = ToCBool(part.unlit);
    info->reserved[0] = 0U;
    info->reserved[1] = 0U;
}

/// The largest number of seconds a native `TimeSpan` represents, matching the models family.

/// CBIND-110: the compiled font. A handle because it owns its atlas as well as its glyph table.
struct CnbSpriteFontResource final {
    std::shared_ptr<Cnb::CnbSpriteFontData> value;
};

/// CBIND-110: the compiled sound. A handle because it owns bulk sample bytes.
struct CnbSoundEffectResource final {
    std::shared_ptr<Cnb::CnbSoundEffectData> value;
};

/// CBIND-110: a decoded standalone clip, whose keyframes it owns.
struct CnbAnimationClipResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::AnimationClipEXT> value;
};

[[nodiscard]] CNA_Result GetSpriteFont(
    const CNA_CnbSpriteFontDataHandle handle,
    std::shared_ptr<CnbSpriteFontResource>* const outFont)
{
    return LookupHandle(handle, ObjectKind::CnbSpriteFontData, outFont);
}

[[nodiscard]] CNA_Result GetSoundEffect(
    const CNA_CnbSoundEffectDataHandle handle,
    std::shared_ptr<CnbSoundEffectResource>* const outSound)
{
    return LookupHandle(handle, ObjectKind::CnbSoundEffectData, outSound);
}

[[nodiscard]] CNA_Result GetAnimationClip(
    const CNA_CnbAnimationClipHandle handle,
    std::shared_ptr<CnbAnimationClipResource>* const outClip)
{
    return LookupHandle(handle, ObjectKind::CnbAnimationClip, outClip);
}

[[nodiscard]] CNA_Result CreateSpriteFont(
    Cnb::CnbSpriteFontData data,
    CNA_CnbSpriteFontDataHandle* const outFont)
{
    return CreateResourceHandle<CnbSpriteFontResource>(
        ObjectKind::CnbSpriteFontData,
        std::make_shared<Cnb::CnbSpriteFontData>(std::move(data)),
        outFont);
}

[[nodiscard]] CNA_Result CreateSoundEffect(
    Cnb::CnbSoundEffectData data,
    CNA_CnbSoundEffectDataHandle* const outSound)
{
    return CreateResourceHandle<CnbSoundEffectResource>(
        ObjectKind::CnbSoundEffectData,
        std::make_shared<Cnb::CnbSoundEffectData>(std::move(data)),
        outSound);
}

[[nodiscard]] CNA_Result CreateAnimationClip(
    Microsoft::Xna::Framework::Graphics::AnimationClipEXT clip,
    CNA_CnbAnimationClipHandle* const outClip)
{
    return CreateResourceHandle<CnbAnimationClipResource>(
        ObjectKind::CnbAnimationClip,
        std::make_shared<Microsoft::Xna::Framework::Graphics::AnimationClipEXT>(std::move(clip)),
        outClip);
}

/**
 * The clip conversion both `add_animation` and `encode_animation_clip` need.
 *
 * Kept here rather than duplicated because the two directions of one wire format must agree about
 * what a representable clip is: a time the encoder would accept and the model builder would not
 * would be two different formats sharing a keyframe layout.
 */
[[nodiscard]] CNA_Result BorrowClip(
    const CNA_AnimationClipEXTDescriptor* const descriptor,
    const CNA_ClipTargetSpaceEXT targetSpace,
    Microsoft::Xna::Framework::Graphics::AnimationClipEXT* const outClip)
{
    if (descriptor == nullptr) {
        return InvalidArgument("The animation clip descriptor is null.");
    }
    if (targetSpace > CNA_CLIP_TARGET_SPACE_MAXIMUM_EXT) {
        return InvalidArgument("The clip target space is not a ClipTargetSpaceEXT value.");
    }
    if (!IsValidTimeSpanSeconds(descriptor->duration_seconds)) {
        return InvalidArgument("The clip duration must fit a finite TimeSpan.");
    }
    std::span<const CNA_BoneTrackEXTDescriptor> trackSpan;
    if (const CNA_Result result =
            BorrowElements(descriptor->tracks, descriptor->track_count, &trackSpan);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Microsoft::Xna::Framework::Graphics::AnimationClipEXT copied;
    copied.Duration = System::TimeSpan::FromSeconds(descriptor->duration_seconds);
    copied.TargetSpace =
        static_cast<Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT>(targetSpace);
    copied.Tracks.reserve(trackSpan.size());
    for (const CNA_BoneTrackEXTDescriptor& sourceTrack : trackSpan) {
        if (sourceTrack.reserved != 0U) {
            return InvalidArgument("CNA_BoneTrackEXTDescriptor.reserved must be zero.");
        }
        std::span<const CNA_KeyframeEXT> keySpan;
        if (const CNA_Result result =
                BorrowElements(sourceTrack.keyframes, sourceTrack.keyframe_count, &keySpan);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::Graphics::BoneTrackEXT track;
        track.BoneIndex = sourceTrack.bone_index;
        track.Keys.reserve(keySpan.size());
        for (const CNA_KeyframeEXT& sourceKey : keySpan) {
            if (!IsValidTimeSpanSeconds(sourceKey.time_seconds)) {
                return InvalidArgument("A keyframe time must fit a finite TimeSpan.");
            }
            Microsoft::Xna::Framework::Graphics::KeyframeEXT key;
            key.Time = System::TimeSpan::FromSeconds(sourceKey.time_seconds);
            key.Translation = {
                sourceKey.translation.x, sourceKey.translation.y, sourceKey.translation.z};
            key.Rotation = {
                sourceKey.rotation.x, sourceKey.rotation.y, sourceKey.rotation.z,
                sourceKey.rotation.w};
            key.Scale = {sourceKey.scale.x, sourceKey.scale.y, sourceKey.scale.z};
            track.Keys.push_back(key);
        }
        copied.Tracks.push_back(std::move(track));
    }
    *outClip = std::move(copied);
    return CNA_RESULT_SUCCESS;
}

/// Reading one track's keyframes out of a clip, shared by the model and the standalone clip.
[[nodiscard]] CNA_Result CopyClipKeyframes(
    const Microsoft::Xna::Framework::Graphics::AnimationClipEXT& clip,
    const uint64_t track,
    CNA_KeyframeEXT* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The keyframe output is invalid.");
    }
    if (const CNA_Result result =
            RequireIndex(track, clip.Tracks.size(), "The animation track index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& keys = clip.Tracks[static_cast<std::size_t>(track)].Keys;
    *outCount = static_cast<uint64_t>(keys.size());
    if (capacity < static_cast<uint64_t>(keys.size())) {
        return BufferTooSmall("The destination capacity is smaller than the keyframe count.");
    }
    for (std::size_t keyIndex = 0U; keyIndex < keys.size(); ++keyIndex) {
        const auto& key = keys[keyIndex];
        CNA_KeyframeEXT& out = destination[keyIndex];
        out.time_seconds = key.Time.getTotalSecondsProperty();
        out.translation.x = key.Translation.X;
        out.translation.y = key.Translation.Y;
        out.translation.z = key.Translation.Z;
        out.rotation.x = key.Rotation.X;
        out.rotation.y = key.Rotation.Y;
        out.rotation.z = key.Rotation.Z;
        out.rotation.w = key.Rotation.W;
        out.scale.x = key.Scale.X;
        out.scale.y = key.Scale.Y;
        out.scale.z = key.Scale.Z;
    }
    return CNA_RESULT_SUCCESS;
}

/// CBIND-111: a resolved loader is a **copy** of the registered function, so it is an object.
struct CnbLoaderResource final {
    CNA::Content::CnbLoaderRegistry::LoaderFn value;
};

/// CBIND-111: what compiling one `.cnj` produced, plus the two lists a build system needs.
struct CnjToCnbResultResource final {
    std::shared_ptr<Cnb::CnjToCnbResult> value;
};

[[nodiscard]] CNA_Result GetLoader(
    const CNA_CnbLoaderHandle handle,
    std::shared_ptr<CnbLoaderResource>* const outLoader)
{
    return LookupHandle(handle, ObjectKind::CnbLoader, outLoader);
}

[[nodiscard]] CNA_Result GetCompiledCnj(
    const CNA_CnjToCnbResultHandle handle,
    std::shared_ptr<CnjToCnbResultResource>* const outResult)
{
    return LookupHandle(handle, ObjectKind::CnjToCnbResult, outResult);
}

[[nodiscard]] CNA_Result CreateLoader(
    CNA::Content::CnbLoaderRegistry::LoaderFn loader,
    CNA_CnbLoaderHandle* const outLoader)
{
    return CreateResourceHandle<CnbLoaderResource>(
        ObjectKind::CnbLoader, std::move(loader), outLoader);
}

/**
 * Wraps a C loader callback as the canonical `LoaderFn`.
 *
 * Two things here are the established discipline rather than a choice. The document and the
 * content manager reach the callback as **callback-scoped borrowed handles** -- created over the
 * canonical references with a no-op deleter and released on every path before returning -- so a
 * callback that caches one and uses it later gets an invalid-handle answer rather than a dangling
 * reference. And the produced pointer is boxed as `ForeignContentObjectEXT`, the same wrapper a
 * caller-supplied `.xnb` reader's object goes into, so a C++ load asking for another type fails
 * its unboxing cleanly instead of reinterpreting an unrelated pointer.
 */
[[nodiscard]] CNA::Content::CnbLoaderRegistry::LoaderFn WrapLoaderCallback(
    const CNA_CnbLoaderCallback callback,
    void* const context,
    std::string canonicalTypeName)
{
    return [callback, context, name = std::move(canonicalTypeName)](
               const Cnb::CnbDocument& document,
               Microsoft::Xna::Framework::Content::ContentManager& contentManager,
               const std::string& assetName) -> std::any {
        const auto documentResource = std::make_shared<CnbDocumentResource>();
        documentResource->value =
            std::shared_ptr<Cnb::CnbDocument>(const_cast<Cnb::CnbDocument*>(&document),
                                              [](Cnb::CnbDocument*) {});
        CNA_CnbDocumentHandle documentHandle = CNA_INVALID_HANDLE;
        if (GetRuntimeHandles().Create(
                ObjectKind::CnbDocument, documentResource, &documentHandle) !=
            CNA_RESULT_SUCCESS) {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "A borrowed CNB document handle could not be created for the loader registered as "
                "'" + name + "'.");
        }
        CNA_Handle managerHandle = CNA_INVALID_HANDLE;
        if (CNA::C::Detail::BorrowContentManagerForCallback(contentManager, &managerHandle) !=
            CNA_RESULT_SUCCESS) {
            static_cast<void>(GetRuntimeHandles().Release(documentHandle));
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "A borrowed content-manager handle could not be created for the loader registered "
                "as '" + name + "'.");
        }

        void* object = nullptr;
        const CNA_StringView assetNameView{
            assetName.empty() ? nullptr : assetName.data(),
            static_cast<uint64_t>(assetName.size())};
        const CNA_Result result =
            callback(context, documentHandle, managerHandle, assetNameView, &object);
        static_cast<void>(GetRuntimeHandles().Release(managerHandle));
        static_cast<void>(GetRuntimeHandles().Release(documentHandle));
        if (result != CNA_RESULT_SUCCESS) {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "The caller-supplied CNB loader registered as '" + name +
                "' failed to load (CNA_Result " + std::to_string(result) + ").");
        }
        return std::any(CNA::Content::ForeignContentObjectEXT{object});
    };
}

[[nodiscard]] CNA_Result CreateCompiledCnj(
    Cnb::CnjToCnbResult compiled,
    CNA_CnjToCnbResultHandle* const outResult)
{
    return CreateResourceHandle<CnjToCnbResultResource>(
        ObjectKind::CnjToCnbResult,
        std::make_shared<Cnb::CnjToCnbResult>(std::move(compiled)),
        outResult);
}

[[nodiscard]] CNA_Result CnbReadLimitsInit(CNA_CnbReadLimits* const outLimits)
{
    if (!IsVersionedStructure(outLimits, CNA_CNB_READ_LIMITS_STRUCT_VERSION)) {
        return InvalidArgument("The read-limits structure is not a known size and version.");
    }
    const Cnb::CnbReadLimits& limits = Cnb::DefaultCnbReadLimits();
    outLimits->max_file_size = limits.maxFileSize;
    outLimits->max_chunk_size = limits.maxChunkSize;
    outLimits->max_total_uncompressed_size = limits.maxTotalUncompressedSize;
    outLimits->max_chunk_count = limits.maxChunkCount;
    outLimits->max_string_bytes = limits.maxStringBytes;
    outLimits->max_array_element_count = limits.maxArrayElementCount;
    outLimits->max_chunk_alignment = limits.maxChunkAlignment;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbCopyFormatMagic(
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The magic-bytes output is invalid.");
    }
    const std::vector<uint8_t> magic(Cnb::Format::Magic.begin(), Cnb::Format::Magic.end());
    return CopyBytes(magic, destination, capacity, outByteCount);
}

[[nodiscard]] CNA_Result CnbAssetTypeIdFromNameRoute(
    const CNA_StringView name,
    uint32_t* const outAssetTypeId)
{
    std::string value;
    if (const CNA_Result result = ReadText(
            name, "The asset type name is not valid UTF-8.", &value, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The canonical guard, decided here so the result is a named refusal rather than whatever
    // the firewall makes of std::invalid_argument.
    if (value.empty()) {
        return InvalidArgument("A custom asset type name must not be empty.");
    }
    *outAssetTypeId = Cnb::CnbAssetTypeIdFromName(value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbGetLogicalNameProblemSize(
    const CNA_StringView logicalName,
    uint64_t* const outByteCount)
{
    // Deliberately not ValidateStringView: "is not well-formed UTF-8" is one of the verdicts
    // this function exists to report, so refusing malformed input here would withhold the
    // answer the caller asked for. Only the pointer/length pair itself is checked.
    if (logicalName.data == nullptr && logicalName.byte_length != 0U) {
        return InvalidArgument("The logical name is null with a non-zero length.");
    }
    const std::string_view view(
        logicalName.data == nullptr ? "" : logicalName.data,
        static_cast<std::size_t>(logicalName.byte_length));
    return ReportTextSize(Cnb::CnbLogicalNameProblem(view), outByteCount);
}

[[nodiscard]] CNA_Result CnbCopyLogicalNameProblem(
    const CNA_StringView logicalName,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if (logicalName.data == nullptr && logicalName.byte_length != 0U) {
        return InvalidArgument("The logical name is null with a non-zero length.");
    }
    const std::string_view view(
        logicalName.data == nullptr ? "" : logicalName.data,
        static_cast<std::size_t>(logicalName.byte_length));
    return CopyText(Cnb::CnbLogicalNameProblem(view), destination, capacity, outByteCount);
}

[[nodiscard]] CNA_Result CnbCheckedAdd(const uint64_t a, const uint64_t b, uint64_t* const outSum)
{
    // Mirrors CheckedAdd's own guard exactly, so the answer is CNA_RESULT_OVERFLOW rather than
    // whatever the firewall makes of a ContentLoadException. The canonical function still
    // performs the arithmetic: if this condition ever drifted from its guard, the canonical
    // throw would fire and the disagreement would surface as CNA_RESULT_IO rather than hide.
    if (a > std::numeric_limits<uint64_t>::max() - b) {
        return Overflow("The declared offset and size overflow a 64-bit byte count.");
    }
    *outSum = Cnb::CheckedAdd(a, b, "cna_cnb_checked_add");
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbCheckedMultiply(
    const uint64_t a,
    const uint64_t b,
    uint64_t* const outProduct)
{
    if (a != 0U && b > std::numeric_limits<uint64_t>::max() / a) {
        return Overflow(
            "The declared element count and element size overflow a 64-bit byte count.");
    }
    *outProduct = Cnb::CheckedMultiply(a, b, "cna_cnb_checked_multiply");
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbCrc32cContinue(
    const uint32_t previous,
    const uint8_t* const data,
    const uint64_t byteCount,
    uint32_t* const outChecksum)
{
    std::span<const uint8_t> bytes;
    if (const CNA_Result result = BorrowBytes(data, byteCount, &bytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outChecksum = Cnb::Crc32cContinue(previous, bytes);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbGetCompressedByteCount(
    const uint8_t* const raw,
    const uint64_t rawByteCount,
    const CNA_CnbCompression codec,
    const int32_t level,
    uint64_t* const outByteCount)
{
    std::span<const uint8_t> bytes;
    if (const CNA_Result result = BorrowBytes(raw, rawByteCount, &bytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::vector<uint8_t> compressed;
    if (const CNA_Result result = CompressInto(bytes, codec, level, &compressed);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outByteCount = static_cast<uint64_t>(compressed.size());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbCopyCompressed(
    const uint8_t* const raw,
    const uint64_t rawByteCount,
    const CNA_CnbCompression codec,
    const int32_t level,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The compressed output buffer is invalid.");
    }
    std::span<const uint8_t> bytes;
    if (const CNA_Result result = BorrowBytes(raw, rawByteCount, &bytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::vector<uint8_t> compressed;
    if (const CNA_Result result = CompressInto(bytes, codec, level, &compressed);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CopyBytes(compressed, destination, capacity, outByteCount);
}

[[nodiscard]] CNA_Result CnbCopyDecompressed(
    const uint8_t* const stored,
    const uint64_t storedByteCount,
    const CNA_CnbCompression codec,
    const uint64_t uncompressedSize,
    const uint64_t maxUncompressedSize,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The decompressed output buffer is invalid.");
    }
    std::span<const uint8_t> bytes;
    if (const CNA_Result result = BorrowBytes(stored, storedByteCount, &bytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbCompression nativeCodec = ToCodec(codec);
    // For compressed chunks, preserve ceiling-before-codec diagnostics. The C ABI separately
    // promises that None ignores both declared sizes (docs/c-api/CNB.md, CBIND-106), unlike
    // the strict C++ helper. Adapt that established ABI contract without weakening the helper.
    if (nativeCodec != Cnb::CnbCompression::None) {
        if (uncompressedSize > maxUncompressedSize) {
            return InvalidArgument(
                "The declared unpacked size is above the configured limit; refused before "
                "allocating anything.");
        }
        if (!Cnb::IsCnbCompressionSupported(nativeCodec)) {
            return NotSupported(
                "The chunk uses compression codec " +
                Cnb::CnbCompressionToString(nativeCodec) +
                ", which this build does not implement.");
        }
    }
    const bool storedOnly = nativeCodec == Cnb::CnbCompression::None;
    const std::vector<uint8_t> raw = Cnb::DecompressCnbChunk(
        bytes, nativeCodec, storedOnly ? storedByteCount : uncompressedSize,
        storedOnly ? storedByteCount : maxUncompressedSize, "The CNB chunk");
    return CopyBytes(raw, destination, capacity, outByteCount);
}

} // namespace

/* --- CBIND-107: the parsed document ---------------------------------------------------------- */

namespace {

[[nodiscard]] CNA_Result CnbChunkEntryIsMandatory(
    const CNA_CnbChunkEntry* const entry,
    CNA_Bool* const outMandatory)
{
    if (entry == nullptr) {
        return InvalidArgument("The chunk-entry input or output is null.");
    }
    if (!IsVersionedStructure(entry, CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION)) {
        return InvalidArgument("The chunk entry is not a known size and version.");
    }
    Cnb::CnbChunkEntry native;
    native.flags = entry->flags;
    *outMandatory = ToCBool(native.IsMandatory());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreateDocument(
    Cnb::CnbDocument document,
    CNA_CnbDocumentHandle* const outDocument)
{
    return CreateResourceHandle<CnbDocumentResource>(
        ObjectKind::CnbDocument,
        std::make_shared<Cnb::CnbDocument>(std::move(document)),
        outDocument);
}

[[nodiscard]] CNA_Result CnbDocumentParse(
    const uint8_t* const bytes,
    const uint64_t byteCount,
    const CNA_StringView origin,
    const CNA_CnbReadLimits* const limits,
    CNA_CnbDocumentHandle* const outDocument)
{
    std::span<const uint8_t> span;
    if (const CNA_Result result = BorrowBytes(bytes, byteCount, &span);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string originText;
    if (const CNA_Result result =
            ReadText(origin, "The origin text is not valid UTF-8.", &originText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbReadLimits nativeLimits;
    if (const CNA_Result result = ResolveLimits(limits, &nativeLimits);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateDocument(
        Cnb::CnbDocument::Parse(
            std::vector<uint8_t>(span.begin(), span.end()), originText, nativeLimits),
        outDocument);
}

[[nodiscard]] CNA_Result CnbDocumentParseFile(
    const CNA_StringView path,
    const CNA_CnbReadLimits* const limits,
    CNA_CnbDocumentHandle* const outDocument)
{
    std::string pathText;
    if (const CNA_Result result =
            ReadText(path, "The path text is not valid UTF-8.", &pathText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (pathText.empty()) {
        return InvalidArgument("The CNB file path must not be empty.");
    }
    Cnb::CnbReadLimits nativeLimits;
    if (const CNA_Result result = ResolveLimits(limits, &nativeLimits);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateDocument(Cnb::CnbDocument::ParseFile(pathText, nativeLimits), outDocument);
}

[[nodiscard]] CNA_Result CnbDocumentGetChunk(
    const CNA_CnbDocumentHandle documentHandle,
    const uint64_t index,
    CNA_CnbChunkEntry* const outEntry)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!IsVersionedStructure(outEntry, CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION)) {
        return InvalidArgument("The chunk entry is not a known size and version.");
    }
    if (const CNA_Result result = RequireIndex(
            index, document->value->ChunkCount(),
            "The chunk index is outside this document's table of contents.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbChunkEntry& entry =
        document->value->ChunkAt(static_cast<std::size_t>(index));
    outEntry->offset = entry.offset;
    outEntry->stored_size = entry.storedSize;
    outEntry->uncompressed_size = entry.uncompressedSize;
    outEntry->type = entry.type.value;
    outEntry->flags = entry.flags;
    outEntry->checksum = entry.checksum;
    outEntry->compression = static_cast<CNA_CnbCompression>(entry.compression);
    outEntry->alignment = entry.alignment;
    outEntry->reserved = 0U;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbDocumentOpenChunk(
    const CNA_CnbDocumentHandle documentHandle,
    const uint64_t index,
    CNA_CnbReaderHandle* const outReader)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            index, document->value->ChunkCount(),
            "The chunk index is outside this document's table of contents.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto resource = std::make_shared<CnbReaderResource>();
    // The borrow is taken before the cursor exists, so a failure to create the handle below
    // releases it with the resource rather than leaving the document permanently blocked.
    resource->documentBorrow = std::make_shared<CountedBorrow<CnbDocumentResource>>(document);
    resource->value = std::make_shared<Cnb::CnbByteReader>(
        document->value->OpenChunk(static_cast<std::size_t>(index)));
    return CreateHandle(ObjectKind::CnbReader, resource, outReader);
}

[[nodiscard]] CNA_Result CnbDocumentFindAll(
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_CnbChunkId type,
    uint64_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The chunk-index output buffer is invalid.");
    }
    const std::vector<std::size_t> found = document->value->FindAll(Cnb::CnbChunkId{type});
    *outCount = static_cast<uint64_t>(found.size());
    if (capacity < static_cast<uint64_t>(found.size())) {
        return BufferTooSmall(
            "The destination capacity is smaller than the number of matching chunks.");
    }
    for (std::size_t i = 0; i < found.size(); ++i) {
        destination[i] = static_cast<uint64_t>(found[i]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbDocumentFindSingle(
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_CnbChunkId type,
    CNA_Bool* const outFound,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::optional<std::size_t> found =
        document->value->FindSingle(Cnb::CnbChunkId{type});
    // Absence is an ordinary answer, never a failure, so it is reported beside the value and
    // the value is left alone -- the availability convention every optional query here uses.
    *outFound = ToCBool(found.has_value());
    if (found.has_value()) {
        *outIndex = static_cast<uint64_t>(*found);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbDocumentRequireMandatoryChunksUnderstood(
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_CnbChunkId* const knownTypes,
    const uint64_t knownTypeCount)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (knownTypes == nullptr && knownTypeCount != 0U) {
        return InvalidArgument(
            "The known chunk-identifier array is null with a non-zero count.");
    }
    if (knownTypeCount > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return Overflow(
            "The known chunk-identifier count does not fit in this host's size type.");
    }
    std::vector<Cnb::CnbChunkId> native;
    native.reserve(static_cast<std::size_t>(knownTypeCount));
    for (uint64_t i = 0; i < knownTypeCount; ++i) {
        native.push_back(Cnb::CnbChunkId{knownTypes[i]});
    }
    document->value->RequireMandatoryChunksUnderstood(native);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbDocumentGetMetadata(
    const CNA_CnbDocumentHandle documentHandle,
    CNA_CnbMetadata* const outMetadata)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!IsVersionedStructure(outMetadata, CNA_CNB_METADATA_STRUCT_VERSION)) {
        return InvalidArgument("The metadata structure is not a known size and version.");
    }
    const Cnb::CnbMetadata& metadata = document->value->Metadata();
    outMetadata->present = ToCBool(metadata.present);
    outMetadata->reserved[0] = 0U;
    outMetadata->reserved[1] = 0U;
    outMetadata->reserved[2] = 0U;
    outMetadata->flags = metadata.flags;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbDocumentGetExternalReference(
    const CNA_CnbDocumentHandle documentHandle,
    const uint64_t index,
    const CNA_StringView whatForDiagnostics,
    CNA_CnbExternalReference* const outReference)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (outReference->struct_size <
            static_cast<uint32_t>(sizeof(CNA_CnbExternalReference)) ||
        outReference->struct_version != CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION) {
        return InvalidArgument(
            "The external-reference structure is not a known size and version.");
    }
    std::string what;
    if (const CNA_Result result = ReadText(
            whatForDiagnostics, "The what_for_diagnostics text is not valid UTF-8.", &what);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (index > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        return InvalidArgument("The external-reference index is outside the table.");
    }
    if (const CNA_Result result = RequireIndex(
            index, document->value->ExternalReferences().size(),
            "The external-reference index is outside the table.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbExternalReference& reference = document->value->ExternalReferenceAt(
        static_cast<uint32_t>(index), what.c_str());
    outReference->flags = reference.flags;
    outReference->expected_asset_type_id = reference.expectedAssetTypeId;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbDocumentGetLimits(
    const CNA_CnbDocumentHandle documentHandle,
    CNA_CnbReadLimits* const outLimits)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!IsVersionedStructure(outLimits, CNA_CNB_READ_LIMITS_STRUCT_VERSION)) {
        return InvalidArgument("The read-limits structure is not a known size and version.");
    }
    WriteLimits(document->value->Limits(), outLimits);
    return CNA_RESULT_SUCCESS;
}

} // namespace

/* --- CBIND-107: the bounded reader ------------------------------------------------------------ */

namespace {

[[nodiscard]] CNA_Result CnbReaderCreate(
    const uint8_t* const data,
    const uint64_t byteCount,
    const CNA_StringView context,
    const CNA_CnbReadLimits* const limits,
    CNA_CnbReaderHandle* const outReader)
{
    std::span<const uint8_t> span;
    if (const CNA_Result result = BorrowBytes(data, byteCount, &span);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string contextText;
    if (const CNA_Result result =
            ReadText(context, "The context text is not valid UTF-8.", &contextText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbReadLimits nativeLimits;
    if (const CNA_Result result = ResolveLimits(limits, &nativeLimits);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto resource = std::make_shared<CnbReaderResource>();
    // The copy is the deviation this route documents: the canonical cursor never copies, and a
    // C caller cannot be told to keep its buffer alive, so the ABI owns the bytes instead of
    // leaving a lifetime rule it has no way to police.
    resource->ownedBytes =
        std::make_shared<std::vector<uint8_t>>(span.begin(), span.end());
    resource->value = std::make_shared<Cnb::CnbByteReader>(
        std::span<const uint8_t>(*resource->ownedBytes), std::move(contextText), nativeLimits);
    return CreateHandle(ObjectKind::CnbReader, resource, outReader);
}

[[nodiscard]] CNA_Result CnbReaderReadString(
    const CNA_CnbReaderHandle readerHandle,
    uint64_t* const outByteCount)
{
    std::shared_ptr<CnbReaderResource> reader;
    if (const CNA_Result result = GetReader(readerHandle, &reader);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // Read first, store second: a throw leaves the previous string in place rather than
    // clearing it, so a failed read cannot make an earlier successful one look like it never
    // happened.
    std::string value = reader->value->ReadString();
    *outByteCount = static_cast<uint64_t>(value.size());
    reader->lastString = std::move(value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbReaderReadBytes(
    const CNA_CnbReaderHandle readerHandle,
    const uint64_t byteCount,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    std::shared_ptr<CnbReaderResource> reader;
    if (const CNA_Result result = GetReader(readerHandle, &reader);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The byte output buffer is invalid.");
    }
    *outByteCount = byteCount;
    // The size is the caller's own argument rather than something the file declares, so a
    // capacity that cannot hold it is settled BEFORE the cursor advances: a refused call
    // consumes nothing and can simply be repeated with a larger buffer.
    if (capacity < byteCount) {
        return BufferTooSmall(
            "The destination capacity is smaller than the requested byte count.");
    }
    const std::span<const uint8_t> bytes = reader->value->ReadBytes(byteCount);
    if (!bytes.empty()) {
        std::memcpy(destination, bytes.data(), bytes.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbReaderFail(
    const CNA_CnbReaderHandle readerHandle,
    const CNA_StringView detail)
{
    std::shared_ptr<CnbReaderResource> reader;
    if (const CNA_Result result = GetReader(readerHandle, &reader);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string detailText;
    if (const CNA_Result result =
            ReadText(detail, "The detail text is not valid UTF-8.", &detailText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    reader->value->Fail(detailText);
}

[[nodiscard]] CNA_Result CnbIsWellFormedUtf8(
    const CNA_StringView text,
    CNA_Bool* const outWellFormed)
{
    // Deliberately not validated as UTF-8 by the ABI first: whether it is well-formed is the
    // question being asked, so refusing malformed bytes here would answer nothing.
    if (text.data == nullptr && text.byte_length != 0U) {
        return InvalidArgument("The text is null with a non-zero length.");
    }
    const std::string_view view(
        text.data == nullptr ? "" : text.data,
        static_cast<std::size_t>(text.byte_length));
    *outWellFormed = ToCBool(Cnb::CnbByteReader::IsWellFormedUtf8(view));
    return CNA_RESULT_SUCCESS;
}

} // namespace

/* --- CBIND-107: the primitive writer ---------------------------------------------------------- */

namespace {

[[nodiscard]] CNA_Result CnbByteWriterWriteBytes(
    const CNA_CnbByteWriterHandle writerHandle,
    const uint8_t* const bytes,
    const uint64_t byteCount)
{
    std::shared_ptr<CnbByteWriterResource> writer;
    if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const uint8_t> span;
    if (const CNA_Result result = BorrowBytes(bytes, byteCount, &span);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    writer->value->WriteBytes(span);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbByteWriterWriteZeros(
    const CNA_CnbByteWriterHandle writerHandle,
    const uint64_t byteCount)
{
    std::shared_ptr<CnbByteWriterResource> writer;
    if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (byteCount > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return Overflow("The zero count does not fit in this host's size type.");
    }
    writer->value->WriteZeros(static_cast<std::size_t>(byteCount));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbByteWriterTake(
    const CNA_CnbByteWriterHandle writerHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    std::shared_ptr<CnbByteWriterResource> writer;
    if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The written-bytes output buffer is invalid.");
    }
    *outByteCount = static_cast<uint64_t>(writer->value->Size());
    // Taking is destructive, so the capacity is settled first: a refused take leaves the bytes
    // where they were and can simply be repeated with a larger buffer.
    if (capacity < *outByteCount) {
        return BufferTooSmall("The destination capacity is smaller than the written bytes.");
    }
    const std::vector<uint8_t> taken = writer->value->Take();
    if (!taken.empty()) {
        std::memcpy(destination, taken.data(), taken.size());
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

/* --- CBIND-107: the container writer ---------------------------------------------------------- */

namespace {

[[nodiscard]] CNA_Result CnbWriterAddExternalReference(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_CnbExternalReference* const reference,
    const CNA_StringView logicalName)
{
    std::shared_ptr<CnbWriterResource> writer;
    if (const CNA_Result result = GetWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (reference == nullptr) {
        return InvalidArgument("The external-reference input is null.");
    }
    if (!IsVersionedStructure(reference, CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION)) {
        return InvalidArgument(
            "The external-reference structure is not a known size and version.");
    }
    std::string name;
    if (const CNA_Result result =
            ReadText(logicalName, "The logical_name text is not valid UTF-8.", &name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbExternalReference native;
    native.flags = reference->flags;
    native.expectedAssetTypeId = reference->expected_asset_type_id;
    native.logicalName = std::move(name);
    writer->externalReferences.push_back(std::move(native));
    // The canonical setter takes the whole table, so the accumulated one is handed over each
    // time rather than kept only here: the writer's own state stays the single source of truth
    // and a build sees exactly what was appended.
    writer->value->SetExternalReferences(writer->externalReferences);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbWriterClearExternalReferences(const CNA_CnbWriterHandle writerHandle)
{
    std::shared_ptr<CnbWriterResource> writer;
    if (const CNA_Result result = GetWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    writer->externalReferences.clear();
    writer->value->SetExternalReferences({});
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbWriterAddChunk(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_CnbChunkId type,
    const uint8_t* const data,
    const uint64_t byteCount,
    const uint32_t flags,
    const uint32_t alignment)
{
    std::shared_ptr<CnbWriterResource> writer;
    if (const CNA_Result result = GetWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const uint8_t> span;
    if (const CNA_Result result = BorrowBytes(data, byteCount, &span);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    writer->value->AddChunk(
        Cnb::CnbChunkId{type},
        std::vector<uint8_t>(span.begin(), span.end()),
        flags,
        alignment);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbWriterSetCompression(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_CnbCompression codec,
    const int32_t level)
{
    std::shared_ptr<CnbWriterResource> writer;
    if (const CNA_Result result = GetWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbCompression nativeCodec = ToCodec(codec);
    // The canonical setter throws std::invalid_argument, which the firewall would report as a
    // bad argument. "This build does not implement that codec" is not a bad argument -- the
    // caller retries with another codec rather than correcting this one -- so it is decided
    // here, exactly as the compression routes decide it.
    if (nativeCodec != Cnb::CnbCompression::None &&
        !Cnb::IsCnbCompressionSupported(nativeCodec)) {
        return NotSupported(
            "The CNB writer was asked for codec " + Cnb::CnbCompressionToString(nativeCodec) +
            ", which this build does not implement.");
    }
    writer->value->SetCompression(nativeCodec, static_cast<int>(level));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbWriterSetLimits(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_CnbReadLimits* const limits)
{
    std::shared_ptr<CnbWriterResource> writer;
    if (const CNA_Result result = GetWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (limits == nullptr) {
        return InvalidArgument("The read-limits input is null.");
    }
    Cnb::CnbReadLimits nativeLimits;
    if (const CNA_Result result = ResolveLimits(limits, &nativeLimits);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    writer->value->SetLimits(nativeLimits);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbWriterGetLimits(
    const CNA_CnbWriterHandle writerHandle,
    CNA_CnbReadLimits* const outLimits)
{
    std::shared_ptr<CnbWriterResource> writer;
    if (const CNA_Result result = GetWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!IsVersionedStructure(outLimits, CNA_CNB_READ_LIMITS_STRUCT_VERSION)) {
        return InvalidArgument("The read-limits structure is not a known size and version.");
    }
    WriteLimits(writer->value->Limits(), outLimits);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbWriterWriteToFile(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_StringView path)
{
    std::shared_ptr<CnbWriterResource> writer;
    if (const CNA_Result result = GetWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string pathText;
    if (const CNA_Result result =
            ReadText(path, "The path text is not valid UTF-8.", &pathText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (pathText.empty()) {
        return InvalidArgument("The CNB file path must not be empty.");
    }
    writer->value->WriteToFile(pathText);
    return CNA_RESULT_SUCCESS;
}

} // namespace

/* --- CBIND-108: texture pixel formats ---------------------------------------------------------- */

/* --- CBIND-108: the texture schemas ------------------------------------------------------------ */

namespace {

[[nodiscard]] CNA_Result CnbTextureDataCreate(
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth,
    const uint32_t faceCount,
    const uint32_t mipCount,
    CNA_CnbTextureDataHandle* const outTexture)
{
    // The shape is checked by the encoder, which owns the rule and states it once. What is
    // refused here is only what would make the description unusable before it ever gets there:
    // a zero count leaves a representation with no levels to fill.
    if (faceCount == 0U || mipCount == 0U) {
        return InvalidArgument("A CNB texture needs at least one face and one mip level.");
    }
    Cnb::CnbTextureData data;
    data.width = width;
    data.height = height;
    data.depth = depth;
    data.faceCount = faceCount;
    data.mipCount = mipCount;
    return CreateTextureData(std::move(data), outTexture);
}

[[nodiscard]] CNA_Result CnbTextureDataCreateRgba8(
    const uint32_t width,
    const uint32_t height,
    const uint8_t* const rgba,
    const uint64_t byteCount,
    CNA_CnbTextureDataHandle* const outTexture)
{
    std::span<const uint8_t> span;
    if (const CNA_Result result = BorrowBytes(rgba, byteCount, &span);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateTextureData(
        Cnb::MakeRgba8Texture2DData(
            width, height, std::vector<uint8_t>(span.begin(), span.end())),
        outTexture);
}

[[nodiscard]] CNA_Result CnbTextureDataGetInfo(
    const CNA_CnbTextureDataHandle textureHandle,
    CNA_CnbTextureInfo* const outInfo)
{
    std::shared_ptr<CnbTextureDataResource> texture;
    if (const CNA_Result result = GetTextureData(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!IsVersionedStructure(outInfo, CNA_CNB_TEXTURE_INFO_STRUCT_VERSION)) {
        return InvalidArgument("The texture-info structure is not a known size and version.");
    }
    const Cnb::CnbTextureData& data = *texture->value;
    outInfo->width = data.width;
    outInfo->height = data.height;
    outInfo->depth = data.depth;
    outInfo->face_count = data.faceCount;
    outInfo->mip_count = data.mipCount;
    outInfo->representation_count = static_cast<uint32_t>(data.representations.size());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbTextureDataGetLevelDimensions(
    const CNA_CnbTextureDataHandle textureHandle,
    const uint32_t level,
    uint32_t* const outWidth,
    uint32_t* const outHeight,
    uint32_t* const outDepth)
{
    std::shared_ptr<CnbTextureDataResource> texture;
    if (const CNA_Result result = GetTextureData(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbTextureLevelDimensions(*texture->value, level, *outWidth, *outHeight, *outDepth);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbTextureDataAddRepresentation(
    const CNA_CnbTextureDataHandle textureHandle,
    const CNA_CnbTextureFormat format,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbTextureDataResource> texture;
    if (const CNA_Result result = GetTextureData(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbTextureData& data = *texture->value;
    Cnb::CnbTextureRepresentation representation;
    representation.format = ToTextureFormat(format);
    // Sized for the shape the description already declares, so a caller fills levels by index
    // rather than pushing them in an order the format would then have to trust.
    representation.levels.resize(
        static_cast<std::size_t>(data.faceCount) * static_cast<std::size_t>(data.mipCount));
    data.representations.push_back(std::move(representation));
    *outIndex = static_cast<uint64_t>(data.representations.size() - 1U);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbTextureDataSetLevel(
    const CNA_CnbTextureDataHandle textureHandle,
    const uint64_t representation,
    const uint64_t level,
    const uint8_t* const bytes,
    const uint64_t byteCount)
{
    std::shared_ptr<CnbTextureDataResource> texture;
    if (const CNA_Result result = GetTextureData(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const uint8_t> span;
    if (const CNA_Result result = BorrowBytes(bytes, byteCount, &span);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            representation, texture->value->representations.size(),
            "The representation index is outside this texture's list.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto& levels =
        texture->value->representations[static_cast<std::size_t>(representation)].levels;
    if (const CNA_Result result = RequireIndex(
            level, levels.size(),
            "The level index is outside this representation's levels.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    levels[static_cast<std::size_t>(level)].assign(span.begin(), span.end());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbTextureDataCopyLevel(
    const CNA_CnbTextureDataHandle textureHandle,
    const uint64_t representation,
    const uint64_t level,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    std::shared_ptr<CnbTextureDataResource> texture;
    if (const CNA_Result result = GetTextureData(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The level output buffer is invalid.");
    }
    if (const CNA_Result result = RequireIndex(
            representation, texture->value->representations.size(),
            "The representation index is outside this texture's list.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& levels =
        texture->value->representations[static_cast<std::size_t>(representation)].levels;
    if (const CNA_Result result = RequireIndex(
            level, levels.size(),
            "The level index is outside this representation's levels.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CopyBytes(
        levels[static_cast<std::size_t>(level)], destination, capacity, outByteCount);
}

[[nodiscard]] CNA_Result CnbTextureDataSelectRepresentation(
    const CNA_CnbTextureDataHandle textureHandle,
    const CNA_CnbTextureFormatSupportedFn supported,
    void* const context,
    CNA_Bool* const outFound,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbTextureDataResource> texture;
    if (const CNA_Result result = GetTextureData(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (supported == nullptr) {
        return InvalidArgument("The representation-selection callback or output is null.");
    }
    const Cnb::CnbTextureData& data = *texture->value;
    const std::size_t chosen = Cnb::SelectCnbTextureRepresentation(
        data,
        [&](const Cnb::CnbTextureFormat format) {
            return supported(static_cast<CNA_CnbTextureFormat>(format), context) == CNA_TRUE;
        });
    // The canonical function reports "none" by returning the list size. In C that would be a
    // sentinel a caller has to know to compare against, so it becomes the availability pair
    // every optional query in this ABI uses.
    *outFound = ToCBool(chosen < data.representations.size());
    if (*outFound == CNA_TRUE) {
        *outIndex = static_cast<uint64_t>(chosen);
    }
    return CNA_RESULT_SUCCESS;
}

using TextureEncoder =
    std::vector<uint8_t> (*)(const Cnb::CnbTextureData&, const std::string&);

[[nodiscard]] CNA_Result EncodeTexture(
    const CNA_CnbTextureDataHandle textureHandle,
    const CNA_StringView contentName,
    const TextureEncoder encoder,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    std::shared_ptr<CnbTextureDataResource> texture;
    if (const CNA_Result result = GetTextureData(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The encoded-texture output buffer is invalid.");
    }
    std::string name;
    if (const CNA_Result result =
            ReadText(contentName, "The content_name text is not valid UTF-8.", &name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<uint8_t> image = encoder(*texture->value, name);
    return CopyBytes(image, destination, capacity, outByteCount);
}

using TextureDecoder = Cnb::CnbTextureData (*)(const Cnb::CnbDocument&);

[[nodiscard]] CNA_Result DecodeTexture(
    const CNA_CnbDocumentHandle documentHandle,
    const TextureDecoder decoder,
    CNA_CnbTextureDataHandle* const outTexture)
{
    if (outTexture == nullptr) {
        return InvalidArgument("The CNB texture output handle is null.");
    }
    *outTexture = CNA_INVALID_HANDLE;
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateTextureData(decoder(*document->value), outTexture);
}

[[nodiscard]] CNA_Result CnbDocumentReadEmbeddedTexture2d(
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_StringView label,
    CNA_CnbTextureDataHandle* const outTexture)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string labelText;
    if (const CNA_Result result =
            ReadText(label, "The label text is not valid UTF-8.", &labelText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateTextureData(
        Cnb::ReadEmbeddedTexture2DChunks(*document->value, labelText.c_str()), outTexture);
}

} // namespace

/* --- CBIND-109: the model schema ---------------------------------------------------------------- */

namespace {

[[nodiscard]] CNA_Result CnbModelGetInfo(
    const CNA_CnbModelDataHandle modelHandle,
    CNA_CnbModelInfo* const outInfo)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_MODEL_INFO_STRUCT_VERSION,
            "The model-info structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbModelData& data = *model->value;
    outInfo->bone_count = static_cast<uint64_t>(data.bones.size());
    outInfo->part_count = static_cast<uint64_t>(data.parts.size());
    outInfo->mesh_count = static_cast<uint64_t>(data.meshes.size());
    outInfo->animation_count = static_cast<uint64_t>(data.animations.size());
    outInfo->light_count = static_cast<uint64_t>(data.lights.size());
    outInfo->has_skeleton = ToCBool(data.skeleton.has_value());
    outInfo->applies_gltf_lighting_policy =
        ToCBool(data.appliesGltfLightingPolicy);
    outInfo->has_bone_hierarchy = ToCBool(data.hasBoneHierarchy);
    outInfo->reserved = 0U;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelSetFlags(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_Bool appliesGltfLightingPolicy,
    const CNA_Bool hasBoneHierarchy)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    model->value->appliesGltfLightingPolicy = appliesGltfLightingPolicy != CNA_FALSE;
    model->value->hasBoneHierarchy = hasBoneHierarchy != CNA_FALSE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelAddBone(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_StringView name,
    const int32_t parent,
    const float* const transform,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (transform == nullptr) {
        return InvalidArgument("The bone transform is null.");
    }
    std::string nameText;
    if (const CNA_Result result =
            ReadText(name, "The name text is not valid UTF-8.", &nameText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelBone bone;
    bone.name = std::move(nameText);
    bone.parent = parent;
    std::memcpy(bone.transform.data(), transform, bone.transform.size() * sizeof(float));
    model->value->bones.push_back(std::move(bone));
    if (outIndex != nullptr) {
        *outIndex = static_cast<uint64_t>(model->value->bones.size() - 1U);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetBone(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    CNA_CnbModelBone* const outBone)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outBone, CNA_CNB_MODEL_BONE_STRUCT_VERSION,
            "The model-bone structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            RequireIndex(index, model->value->bones.size(), "The bone index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbModelBone& bone = model->value->bones[static_cast<std::size_t>(index)];
    outBone->parent = bone.parent;
    outBone->reserved = 0U;
    std::memcpy(outBone->transform, bone.transform.data(), bone.transform.size() * sizeof(float));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelAddPart(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_CnbModelPartInfo* const info,
    const CNA_StringView name,
    const CNA_StringView externalEffect,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            info, CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION,
            "The model-part structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nameText;
    if (const CNA_Result result =
            ReadText(name, "The name text is not valid UTF-8.", &nameText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string effectText;
    if (const CNA_Result result = ReadText(
            externalEffect, "The external_effect text is not valid UTF-8.", &effectText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart part;
    if (const CNA_Result result = ApplyPartInfo(*info, &part);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    part.name = std::move(nameText);
    part.externalEffect = std::move(effectText);
    model->value->parts.push_back(std::move(part));
    if (outIndex != nullptr) {
        *outIndex = static_cast<uint64_t>(model->value->parts.size() - 1U);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetPart(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    CNA_CnbModelPartInfo* const outInfo)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION,
            "The model-part structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* part = nullptr;
    if (const CNA_Result result = GetPart(model, index, &part);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    ReadPartInfo(*part, outInfo);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelSetPart(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const CNA_CnbModelPartInfo* const info)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            info, CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION,
            "The model-part structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* part = nullptr;
    if (const CNA_Result result = GetPart(model, index, &part);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return ApplyPartInfo(*info, part);
}

[[nodiscard]] CNA_Result CnbModelSetPartVertexBytes(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const uint8_t* const bytes,
    const uint64_t byteCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* part = nullptr;
    if (const CNA_Result result = GetPart(model, index, &part);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const uint8_t> source;
    if (const CNA_Result result = BorrowBytes(bytes, byteCount, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    part->vertexBytes.assign(source.begin(), source.end());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelSetPartIndexBytes(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const uint8_t* const bytes,
    const uint64_t byteCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* part = nullptr;
    if (const CNA_Result result = GetPart(model, index, &part);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const uint8_t> source;
    if (const CNA_Result result = BorrowBytes(bytes, byteCount, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    part->indexBytes.assign(source.begin(), source.end());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetMaterial(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    CNA_CnbMaterialInfo* const outInfo)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_MATERIAL_INFO_STRUCT_VERSION,
            "The material structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbMaterial& material = target->material;
    std::memcpy(outInfo->base_color_factor, material.baseColorFactor.data(), 4U * sizeof(float));
    std::memcpy(outInfo->emissive_factor, material.emissiveFactor.data(), 3U * sizeof(float));
    std::memcpy(
        outInfo->specular_color_factor, material.specularColorFactor.data(), 3U * sizeof(float));
    outInfo->metallic_factor = material.metallicFactor;
    outInfo->roughness_factor = material.roughnessFactor;
    outInfo->ior = material.ior;
    outInfo->specular_factor = material.specularFactor;
    outInfo->normal_scale = material.normalScale;
    outInfo->occlusion_strength = material.occlusionStrength;
    outInfo->alpha_cutoff = material.alphaCutoff;
    outInfo->alpha_mode = material.alphaMode;
    outInfo->double_sided = ToCBool(material.doubleSided);
    outInfo->reserved[0] = 0U;
    outInfo->reserved[1] = 0U;
    outInfo->reserved[2] = 0U;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelSetMaterial(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMaterialInfo* const info)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            info, CNA_CNB_MATERIAL_INFO_STRUCT_VERSION,
            "The material structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMaterial& material = target->material;
    std::memcpy(material.baseColorFactor.data(), info->base_color_factor, 4U * sizeof(float));
    std::memcpy(material.emissiveFactor.data(), info->emissive_factor, 3U * sizeof(float));
    std::memcpy(
        material.specularColorFactor.data(), info->specular_color_factor, 3U * sizeof(float));
    material.metallicFactor = info->metallic_factor;
    material.roughnessFactor = info->roughness_factor;
    material.ior = info->ior;
    material.specularFactor = info->specular_factor;
    material.normalScale = info->normal_scale;
    material.occlusionStrength = info->occlusion_strength;
    material.alphaCutoff = info->alpha_cutoff;
    material.alphaMode = info->alpha_mode;
    material.doubleSided = info->double_sided != CNA_FALSE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetMaterialTextureSize(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMaterialTextureSlot slot,
    uint64_t* const outBytes)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::string* const name = MaterialTextureSlot(target->material, slot);
    if (name == nullptr) {
        return InvalidArgument("The material texture slot is not a named slot.");
    }
    return ReportTextSize(*name, outBytes);
}

[[nodiscard]] CNA_Result CnbModelCopyMaterialTexture(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMaterialTextureSlot slot,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::string* const name = MaterialTextureSlot(target->material, slot);
    if (name == nullptr) {
        return InvalidArgument("The material texture slot is not a named slot.");
    }
    return CopyText(*name, destination, capacity, outBytes);
}

[[nodiscard]] CNA_Result CnbModelSetMaterialTexture(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMaterialTextureSlot slot,
    const CNA_StringView assetName)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string* const name = MaterialTextureSlot(target->material, slot);
    if (name == nullptr) {
        return InvalidArgument("The material texture slot is not a named slot.");
    }
    std::string assetText;
    if (const CNA_Result result =
            ReadText(assetName, "The asset_name text is not valid UTF-8.", &assetText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *name = std::move(assetText);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetMaterialTextureTransform(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    CNA_CnbTextureTransform* const outTransform)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireTextureSlot(slot);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbTextureTransform& transform =
        target->material.textureTransforms[static_cast<std::size_t>(slot)];
    outTransform->offset_x = transform.offsetX;
    outTransform->offset_y = transform.offsetY;
    outTransform->scale_x = transform.scaleX;
    outTransform->scale_y = transform.scaleY;
    outTransform->rotation = transform.rotation;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelSetMaterialTextureTransform(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    const CNA_CnbTextureTransform* const transform)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (transform == nullptr) {
        return InvalidArgument("The texture transform is null.");
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireTextureSlot(slot);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbTextureTransform& stored =
        target->material.textureTransforms[static_cast<std::size_t>(slot)];
    stored.offsetX = transform->offset_x;
    stored.offsetY = transform->offset_y;
    stored.scaleX = transform->scale_x;
    stored.scaleY = transform->scale_y;
    stored.rotation = transform->rotation;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetMaterialSampler(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    CNA_CnbSamplerState* const outSampler)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireTextureSlot(slot);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbSamplerState& sampler =
        target->material.samplers[static_cast<std::size_t>(slot)];
    outSampler->filter = sampler.filter;
    outSampler->address_u = sampler.addressU;
    outSampler->address_v = sampler.addressV;
    outSampler->declared = ToCBool(sampler.declared);
    outSampler->reserved[0] = 0U;
    outSampler->reserved[1] = 0U;
    outSampler->reserved[2] = 0U;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelSetMaterialSampler(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    const CNA_CnbSamplerState* const sampler)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (sampler == nullptr) {
        return InvalidArgument("The sampler state is null.");
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireTextureSlot(slot);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbSamplerState& stored = target->material.samplers[static_cast<std::size_t>(slot)];
    stored.filter = sampler->filter;
    stored.addressU = sampler->address_u;
    stored.addressV = sampler->address_v;
    stored.declared = sampler->declared != CNA_FALSE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelHasMorph(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    CNA_Bool* const outPresent)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outPresent = ToCBool(target->morph.has_value());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetMorph(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    CNA_CnbMorphInfo* const outInfo)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_MORPH_INFO_STRUCT_VERSION,
            "The morph structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMorphData* morph = nullptr;
    if (const CNA_Result result = GetMorph(model, part, &morph);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    outInfo->vertex_count = morph->vertexCount;
    outInfo->reserved = 0U;
    outInfo->target_count = static_cast<uint64_t>(morph->targets.size());
    outInfo->weight_count = static_cast<uint64_t>(morph->weights.size());
    outInfo->weight_track_key_count = static_cast<uint64_t>(morph->weightTrackKeys.size());
    outInfo->recompute_flat_normals = ToCBool(morph->recomputeFlatNormals);
    outInfo->weight_track_step_interpolation =
        ToCBool(morph->weightTrackStepInterpolation);
    outInfo->weight_track_cubic_spline = ToCBool(morph->weightTrackCubicSpline);
    std::memset(outInfo->reserved2, 0, sizeof(outInfo->reserved2));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelSetMorph(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMorphInfo* const info)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            info, CNA_CNB_MORPH_INFO_STRUCT_VERSION,
            "The morph structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelPart* target = nullptr;
    if (const CNA_Result result = GetPart(model, part, &target);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!target->morph.has_value()) {
        target->morph.emplace();
    }
    Cnb::CnbMorphData& morph = target->morph.value();
    morph.vertexCount = info->vertex_count;
    morph.recomputeFlatNormals = info->recompute_flat_normals != CNA_FALSE;
    morph.weightTrackStepInterpolation = info->weight_track_step_interpolation != CNA_FALSE;
    morph.weightTrackCubicSpline = info->weight_track_cubic_spline != CNA_FALSE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelAddMorphTarget(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMorphData* morph = nullptr;
    if (const CNA_Result result = GetMorph(model, part, &morph);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    morph->targets.emplace_back();
    if (outIndex != nullptr) {
        *outIndex = static_cast<uint64_t>(morph->targets.size() - 1U);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelSetMorphTargetDeltas(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t target,
    const CNA_CnbMorphDeltaStream stream,
    const float* const values,
    const uint64_t valueCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMorphData* morph = nullptr;
    if (const CNA_Result result = GetMorph(model, part, &morph);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            target, morph->targets.size(), "The morph target index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::vector<float>* const destination =
        MorphDeltaStream(morph->targets[static_cast<std::size_t>(target)], stream);
    if (destination == nullptr) {
        return InvalidArgument("The morph delta stream is not a named stream.");
    }
    std::span<const float> source;
    if (const CNA_Result result = BorrowElements(values, valueCount, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    destination->assign(source.begin(), source.end());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelCopyMorphTargetDeltas(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t target,
    const CNA_CnbMorphDeltaStream stream,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMorphData* morph = nullptr;
    if (const CNA_Result result = GetMorph(model, part, &morph);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            target, morph->targets.size(), "The morph target index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<float>* const source =
        MorphDeltaStream(morph->targets[static_cast<std::size_t>(target)], stream);
    if (source == nullptr) {
        return InvalidArgument("The morph delta stream is not a named stream.");
    }
    return CopyElements<float>(*source, destination, capacity, outCount);
}

[[nodiscard]] CNA_Result CnbModelSetMorphWeights(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const float* const values,
    const uint64_t valueCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMorphData* morph = nullptr;
    if (const CNA_Result result = GetMorph(model, part, &morph);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const float> source;
    if (const CNA_Result result = BorrowElements(values, valueCount, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    morph->weights.assign(source.begin(), source.end());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelAddMorphWeightKey(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const double timeSeconds,
    const float* const weights,
    const uint64_t weightCount,
    const float* const inTangents,
    const uint64_t inTangentCount,
    const float* const outTangents,
    const uint64_t outTangentCount,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMorphData* morph = nullptr;
    if (const CNA_Result result = GetMorph(model, part, &morph);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!std::isfinite(timeSeconds)) {
        return InvalidArgument("The morph weight key time must be finite.");
    }
    std::span<const float> weightSpan;
    if (const CNA_Result result = BorrowElements(weights, weightCount, &weightSpan);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const float> inSpan;
    if (const CNA_Result result = BorrowElements(inTangents, inTangentCount, &inSpan);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const float> outSpan;
    if (const CNA_Result result = BorrowElements(outTangents, outTangentCount, &outSpan);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMorphWeightKey key;
    key.timeSeconds = timeSeconds;
    key.weights.assign(weightSpan.begin(), weightSpan.end());
    key.inTangent.assign(inSpan.begin(), inSpan.end());
    key.outTangent.assign(outSpan.begin(), outSpan.end());
    morph->weightTrackKeys.push_back(std::move(key));
    if (outIndex != nullptr) {
        *outIndex = static_cast<uint64_t>(morph->weightTrackKeys.size() - 1U);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetMorphWeightKey(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t key,
    CNA_CnbMorphWeightKeyInfo* const outInfo)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_MORPH_WEIGHT_KEY_INFO_STRUCT_VERSION,
            "The morph weight key structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMorphData* morph = nullptr;
    if (const CNA_Result result = GetMorph(model, part, &morph);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            key, morph->weightTrackKeys.size(), "The morph weight key index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbMorphWeightKey& stored =
        morph->weightTrackKeys[static_cast<std::size_t>(key)];
    outInfo->time_seconds = stored.timeSeconds;
    outInfo->weight_count = static_cast<uint64_t>(stored.weights.size());
    outInfo->in_tangent_count = static_cast<uint64_t>(stored.inTangent.size());
    outInfo->out_tangent_count = static_cast<uint64_t>(stored.outTangent.size());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelCopyMorphWeightKeyValues(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t key,
    const CNA_CnbMorphKeyStream stream,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbMorphData* morph = nullptr;
    if (const CNA_Result result = GetMorph(model, part, &morph);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            key, morph->weightTrackKeys.size(), "The morph weight key index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<float>* const source =
        MorphKeyStream(morph->weightTrackKeys[static_cast<std::size_t>(key)], stream);
    if (source == nullptr) {
        return InvalidArgument("The morph key stream is not a named stream.");
    }
    return CopyElements<float>(*source, destination, capacity, outCount);
}

[[nodiscard]] CNA_Result CnbModelAddMesh(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_StringView name,
    const int32_t parentBone,
    const uint32_t* const partIndices,
    const uint64_t partIndexCount,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nameText;
    if (const CNA_Result result =
            ReadText(name, "The name text is not valid UTF-8.", &nameText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const uint32_t> source;
    if (const CNA_Result result = BorrowElements(partIndices, partIndexCount, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelMesh mesh;
    mesh.name = std::move(nameText);
    mesh.parentBone = parentBone;
    mesh.partIndices.assign(source.begin(), source.end());
    model->value->meshes.push_back(std::move(mesh));
    if (outIndex != nullptr) {
        *outIndex = static_cast<uint64_t>(model->value->meshes.size() - 1U);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetMesh(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    CNA_CnbMeshInfo* const outInfo)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_MESH_INFO_STRUCT_VERSION,
            "The mesh structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            RequireIndex(index, model->value->meshes.size(), "The mesh index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbModelMesh& mesh = model->value->meshes[static_cast<std::size_t>(index)];
    outInfo->parent_bone = mesh.parentBone;
    outInfo->reserved = 0U;
    outInfo->part_index_count = static_cast<uint64_t>(mesh.partIndices.size());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelCopyMeshPartIndices(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    uint32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The part-index output is invalid.");
    }
    if (const CNA_Result result =
            RequireIndex(index, model->value->meshes.size(), "The mesh index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<uint32_t>& indices =
        model->value->meshes[static_cast<std::size_t>(index)].partIndices;
    *outCount = static_cast<uint64_t>(indices.size());
    if (capacity < static_cast<uint64_t>(indices.size())) {
        return BufferTooSmall("The destination capacity is smaller than the part-index count.");
    }
    if (!indices.empty()) {
        std::memcpy(destination, indices.data(), indices.size() * sizeof(uint32_t));
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelSetSkeleton(
    const CNA_CnbModelDataHandle modelHandle,
    const int32_t* const hierarchy,
    const uint64_t jointCount,
    const float* const bindPose,
    const float* const inverseBindPose,
    const float* const rootPrefix)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const int32_t> hierarchySpan;
    if (const CNA_Result result = BorrowElements(hierarchy, jointCount, &hierarchySpan);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (jointCount != 0U && (bindPose == nullptr || inverseBindPose == nullptr)) {
        return InvalidArgument(
            "The skeleton pose arrays are null with a non-zero joint count.");
    }
    uint64_t floatCount = 0U;
    if (const CNA_Result result = CnbCheckedMultiply(jointCount, 16U, &floatCount);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const float> bindSpan;
    if (const CNA_Result result = BorrowElements(bindPose, floatCount, &bindSpan);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const float> inverseSpan;
    if (const CNA_Result result = BorrowElements(inverseBindPose, floatCount, &inverseSpan);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const float> prefixSpan;
    if (rootPrefix != nullptr) {
        if (const CNA_Result result = BorrowElements(rootPrefix, floatCount, &prefixSpan);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }
    const auto joints = static_cast<std::size_t>(jointCount);
    Cnb::CnbModelSkeleton skeleton;
    skeleton.hierarchy.assign(hierarchySpan.begin(), hierarchySpan.end());
    skeleton.bindPose.resize(joints);
    skeleton.inverseBindPose.resize(joints);
    for (std::size_t joint = 0U; joint < joints; ++joint) {
        std::memcpy(
            skeleton.bindPose[joint].data(), bindSpan.data() + (joint * 16U), 16U * sizeof(float));
        std::memcpy(
            skeleton.inverseBindPose[joint].data(), inverseSpan.data() + (joint * 16U),
            16U * sizeof(float));
    }
    if (rootPrefix != nullptr) {
        skeleton.rootPrefix.resize(joints);
        for (std::size_t joint = 0U; joint < joints; ++joint) {
            std::memcpy(
                skeleton.rootPrefix[joint].data(), prefixSpan.data() + (joint * 16U),
                16U * sizeof(float));
        }
    }
    model->value->skeleton = std::move(skeleton);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetSkeleton(
    const CNA_CnbModelDataHandle modelHandle,
    CNA_CnbSkeletonInfo* const outInfo)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_SKELETON_INFO_STRUCT_VERSION,
            "The skeleton structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!model->value->skeleton.has_value()) {
        return InvalidArgument("The model carries no skinning skeleton.");
    }
    const Cnb::CnbModelSkeleton& skeleton = model->value->skeleton.value();
    outInfo->joint_count = static_cast<uint64_t>(skeleton.hierarchy.size());
    outInfo->has_root_prefix = skeleton.rootPrefix.empty() ? CNA_FALSE : CNA_TRUE;
    std::memset(outInfo->reserved, 0, sizeof(outInfo->reserved));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelCopySkeletonHierarchy(
    const CNA_CnbModelDataHandle modelHandle,
    int32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The hierarchy output is invalid.");
    }
    if (!model->value->skeleton.has_value()) {
        return InvalidArgument("The model carries no skinning skeleton.");
    }
    const std::vector<int32_t>& hierarchy = model->value->skeleton.value().hierarchy;
    *outCount = static_cast<uint64_t>(hierarchy.size());
    if (capacity < static_cast<uint64_t>(hierarchy.size())) {
        return BufferTooSmall("The destination capacity is smaller than the joint count.");
    }
    if (!hierarchy.empty()) {
        std::memcpy(destination, hierarchy.data(), hierarchy.size() * sizeof(int32_t));
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelCopySkeletonMatrices(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_CnbSkeletonMatrixSet set,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The matrix output is invalid.");
    }
    if (!model->value->skeleton.has_value()) {
        return InvalidArgument("The model carries no skinning skeleton.");
    }
    const Cnb::CnbModelSkeleton& skeleton = model->value->skeleton.value();
    const std::vector<std::array<float, 16>>* source = nullptr;
    switch (set) {
    case CNA_CNB_SKELETON_MATRIX_BIND_POSE: source = &skeleton.bindPose; break;
    case CNA_CNB_SKELETON_MATRIX_INVERSE_BIND_POSE: source = &skeleton.inverseBindPose; break;
    case CNA_CNB_SKELETON_MATRIX_ROOT_PREFIX: source = &skeleton.rootPrefix; break;
    default: return InvalidArgument("The skeleton matrix set is not a named set.");
    }
    const uint64_t required = static_cast<uint64_t>(source->size()) * 16U;
    *outCount = required;
    if (capacity < required) {
        return BufferTooSmall(
            "The destination capacity is smaller than the matrix float count.");
    }
    for (std::size_t joint = 0U; joint < source->size(); ++joint) {
        std::memcpy(
            destination + (joint * 16U), (*source)[joint].data(), 16U * sizeof(float));
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelAddAnimation(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_StringView name,
    const CNA_AnimationClipEXTDescriptor* const clip,
    const CNA_ClipTargetSpaceEXT targetSpace,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (clip == nullptr) {
        return InvalidArgument("The animation clip descriptor is null.");
    }
    if (targetSpace > CNA_CLIP_TARGET_SPACE_MAXIMUM_EXT) {
        return InvalidArgument("The clip target space is not a ClipTargetSpaceEXT value.");
    }
    if (!IsValidTimeSpanSeconds(clip->duration_seconds)) {
        return InvalidArgument("The clip duration must fit a finite TimeSpan.");
    }
    std::string nameText;
    if (const CNA_Result result =
            ReadText(name, "The name text is not valid UTF-8.", &nameText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const CNA_BoneTrackEXTDescriptor> trackSpan;
    if (const CNA_Result result =
            BorrowElements(clip->tracks, clip->track_count, &trackSpan);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbModelAnimation animation;
    animation.name = std::move(nameText);
    animation.clip.Duration = System::TimeSpan::FromSeconds(clip->duration_seconds);
    animation.clip.TargetSpace =
        static_cast<Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT>(targetSpace);
    animation.clip.Tracks.reserve(trackSpan.size());
    for (const CNA_BoneTrackEXTDescriptor& sourceTrack : trackSpan) {
        if (sourceTrack.reserved != 0U) {
            return InvalidArgument("CNA_BoneTrackEXTDescriptor.reserved must be zero.");
        }
        std::span<const CNA_KeyframeEXT> keySpan;
        if (const CNA_Result result = BorrowElements(
                sourceTrack.keyframes, sourceTrack.keyframe_count, &keySpan);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::Graphics::BoneTrackEXT track;
        track.BoneIndex = sourceTrack.bone_index;
        track.Keys.reserve(keySpan.size());
        for (const CNA_KeyframeEXT& sourceKey : keySpan) {
            if (!IsValidTimeSpanSeconds(sourceKey.time_seconds)) {
                return InvalidArgument("A keyframe time must fit a finite TimeSpan.");
            }
            Microsoft::Xna::Framework::Graphics::KeyframeEXT key;
            key.Time = System::TimeSpan::FromSeconds(sourceKey.time_seconds);
            key.Translation = {
                sourceKey.translation.x, sourceKey.translation.y, sourceKey.translation.z};
            key.Rotation = {
                sourceKey.rotation.x, sourceKey.rotation.y, sourceKey.rotation.z,
                sourceKey.rotation.w};
            key.Scale = {sourceKey.scale.x, sourceKey.scale.y, sourceKey.scale.z};
            track.Keys.push_back(key);
        }
        animation.clip.Tracks.push_back(std::move(track));
    }
    model->value->animations.push_back(std::move(animation));
    if (outIndex != nullptr) {
        *outIndex = static_cast<uint64_t>(model->value->animations.size() - 1U);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetAnimation(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount,
    CNA_ClipTargetSpaceEXT* const outTargetSpace)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            index, model->value->animations.size(), "The animation index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbModelAnimation& animation =
        model->value->animations[static_cast<std::size_t>(index)];
    if (outDurationSeconds != nullptr) {
        *outDurationSeconds = animation.clip.Duration.getTotalSecondsProperty();
    }
    if (outTrackCount != nullptr) {
        *outTrackCount = static_cast<uint64_t>(animation.clip.Tracks.size());
    }
    if (outTargetSpace != nullptr) {
        *outTargetSpace = static_cast<CNA_ClipTargetSpaceEXT>(animation.clip.TargetSpace);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetAnimationTrack(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const uint64_t track,
    int32_t* const outBoneIndex,
    uint64_t* const outKeyframeCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            index, model->value->animations.size(), "The animation index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& tracks = model->value->animations[static_cast<std::size_t>(index)].clip.Tracks;
    if (const CNA_Result result =
            RequireIndex(track, tracks.size(), "The animation track index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& stored = tracks[static_cast<std::size_t>(track)];
    if (outBoneIndex != nullptr) {
        *outBoneIndex = stored.BoneIndex;
    }
    if (outKeyframeCount != nullptr) {
        *outKeyframeCount = static_cast<uint64_t>(stored.Keys.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelCopyAnimationKeyframes(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const uint64_t track,
    CNA_KeyframeEXT* const destination,
    const uint64_t capacity,
    uint64_t* const outKeyframeCount)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The keyframe output is invalid.");
    }
    if (const CNA_Result result = RequireIndex(
            index, model->value->animations.size(), "The animation index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& tracks = model->value->animations[static_cast<std::size_t>(index)].clip.Tracks;
    if (const CNA_Result result =
            RequireIndex(track, tracks.size(), "The animation track index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& keys = tracks[static_cast<std::size_t>(track)].Keys;
    *outKeyframeCount = static_cast<uint64_t>(keys.size());
    if (capacity < static_cast<uint64_t>(keys.size())) {
        return BufferTooSmall("The destination capacity is smaller than the keyframe count.");
    }
    for (std::size_t keyIndex = 0U; keyIndex < keys.size(); ++keyIndex) {
        const auto& key = keys[keyIndex];
        CNA_KeyframeEXT& out = destination[keyIndex];
        out.time_seconds = key.Time.getTotalSecondsProperty();
        out.translation.x = key.Translation.X;
        out.translation.y = key.Translation.Y;
        out.translation.z = key.Translation.Z;
        out.rotation.x = key.Rotation.X;
        out.rotation.y = key.Rotation.Y;
        out.rotation.z = key.Rotation.Z;
        out.rotation.w = key.Rotation.W;
        out.scale.x = key.Scale.X;
        out.scale.y = key.Scale.Y;
        out.scale.z = key.Scale.Z;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelAddLight(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_CnbModelLight* const light,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (light == nullptr) {
        return InvalidArgument("The light is null.");
    }
    Cnb::CnbModelLight stored;
    std::memcpy(stored.direction.data(), light->direction, 3U * sizeof(float));
    std::memcpy(stored.diffuseColor.data(), light->diffuse_color, 3U * sizeof(float));
    model->value->lights.push_back(stored);
    if (outIndex != nullptr) {
        *outIndex = static_cast<uint64_t>(model->value->lights.size() - 1U);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbModelGetLight(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    CNA_CnbModelLight* const outLight)
{
    std::shared_ptr<CnbModelResource> model;
    if (const CNA_Result result = GetModel(modelHandle, &model);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            index, model->value->lights.size(), "The light index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbModelLight& light = model->value->lights[static_cast<std::size_t>(index)];
    std::memcpy(outLight->direction, light.direction.data(), 3U * sizeof(float));
    std::memcpy(outLight->diffuse_color, light.diffuseColor.data(), 3U * sizeof(float));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbBuildModelFromCnj(
    const CNA_StringView cnjPath,
    const CNA_StringView contentRoot,
    CNA_CnbModelFromCnjHandle* const outResult)
{
    std::string pathText;
    if (const CNA_Result result =
            ReadText(cnjPath, "The cnj_path text is not valid UTF-8.", &pathText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string rootText;
    if (const CNA_Result result =
            ReadText(contentRoot, "The content_root text is not valid UTF-8.", &rootText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateResourceHandle<CnbModelFromCnjResource>(
        ObjectKind::CnbModelFromCnj,
        std::make_shared<Cnb::CnbModelFromCnjResult>(
            Cnb::BuildCnbModelFromCnj(pathText, rootText)),
        outResult);
}

[[nodiscard]] CNA_Result CnbModelFromCnjTakeModel(
    const CNA_CnbModelFromCnjHandle resultHandle,
    CNA_CnbModelDataHandle* const outModel)
{
    std::shared_ptr<CnbModelFromCnjResource> compiled;
    if (const CNA_Result result = GetCompileResult(resultHandle, &compiled);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outModel = CNA_INVALID_HANDLE;
    if (compiled->modelTaken) {
        return InvalidArgument("The compiled model has already been taken.");
    }
    if (const CNA_Result result = CreateModel(std::move(compiled->value->model), outModel);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    compiled->modelTaken = true;
    return CNA_RESULT_SUCCESS;
}

} // namespace

/* --- CBIND-110: the font, audio, media, curve and animation schemas ---------------------------- */

namespace {

/// The version `CNA_SpriteFontGlyph` carries. Not a public macro: the `cna_sprite_font_*` family
/// does not publish one either, and inventing one here would be a second spelling of the same
/// number that could drift from the family's own.
constexpr uint32_t SpriteFontGlyphStructVersion = UINT32_C(1);

/// A glyph row carries what the canonical description keeps as four parallel arrays.
[[nodiscard]] CNA_Result ReadGlyphRow(
    const CNA_SpriteFontGlyph& glyph,
    Cnb::CnbSpriteFontData* const font,
    const std::size_t index)
{
    if (glyph.reserved != 0U) {
        return InvalidArgument("CNA_SpriteFontGlyph.reserved must be zero.");
    }
    font->glyphBounds[index] = Microsoft::Xna::Framework::Rectangle(
        glyph.glyph_bounds.x, glyph.glyph_bounds.y,
        glyph.glyph_bounds.width, glyph.glyph_bounds.height);
    font->cropping[index] = Microsoft::Xna::Framework::Rectangle(
        glyph.cropping.x, glyph.cropping.y, glyph.cropping.width, glyph.cropping.height);
    font->kerning[index] =
        Microsoft::Xna::Framework::Vector3(glyph.kerning.x, glyph.kerning.y, glyph.kerning.z);
    font->characters[index] = static_cast<SharpRuntime::charcs>(glyph.character);
    return CNA_RESULT_SUCCESS;
}

void WriteGlyphRow(
    const Cnb::CnbSpriteFontData& font,
    const std::size_t index,
    CNA_SpriteFontGlyph* const glyph)
{
    const Microsoft::Xna::Framework::Rectangle& bounds = font.glyphBounds[index];
    glyph->glyph_bounds.x = bounds.X;
    glyph->glyph_bounds.y = bounds.Y;
    glyph->glyph_bounds.width = bounds.Width;
    glyph->glyph_bounds.height = bounds.Height;
    const Microsoft::Xna::Framework::Rectangle& crop = font.cropping[index];
    glyph->cropping.x = crop.X;
    glyph->cropping.y = crop.Y;
    glyph->cropping.width = crop.Width;
    glyph->cropping.height = crop.Height;
    glyph->kerning.x = font.kerning[index].X;
    glyph->kerning.y = font.kerning[index].Y;
    glyph->kerning.z = font.kerning[index].Z;
    glyph->character = static_cast<CNA_Char16>(font.characters[index]);
    glyph->reserved = 0U;
}

/// The four arrays are parallel and stay that way; growing one grows all four.
void ResizeGlyphArrays(Cnb::CnbSpriteFontData* const font, const std::size_t count)
{
    font->glyphBounds.resize(count);
    font->cropping.resize(count);
    font->kerning.resize(count);
    font->characters.resize(count);
}

[[nodiscard]] CNA_Result CnbSpriteFontDataGetInfo(
    const CNA_CnbSpriteFontDataHandle fontHandle,
    CNA_CnbSpriteFontInfo* const outInfo)
{
    std::shared_ptr<CnbSpriteFontResource> font;
    if (const CNA_Result result = GetSpriteFont(fontHandle, &font);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_SPRITE_FONT_INFO_STRUCT_VERSION,
            "The font-info structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbSpriteFontData& data = *font->value;
    outInfo->glyph_count = static_cast<uint64_t>(data.characters.size());
    outInfo->line_spacing = data.lineSpacing;
    outInfo->spacing = data.spacing;
    outInfo->default_character = data.defaultCharacter.has_value()
        ? static_cast<CNA_Char16>(data.defaultCharacter.value())
        : static_cast<CNA_Char16>(0);
    outInfo->has_default_character = ToCBool(data.defaultCharacter.has_value());
    std::memset(outInfo->reserved, 0, sizeof(outInfo->reserved));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbSpriteFontDataSetInfo(
    const CNA_CnbSpriteFontDataHandle fontHandle,
    const CNA_CnbSpriteFontInfo* const info)
{
    if (info == nullptr) {
        return InvalidArgument("The font-info structure is null.");
    }
    if (const CNA_Result result =
            ValidateCanonicalBool(info->has_default_character, "has_default_character");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<CnbSpriteFontResource> font;
    if (const CNA_Result result = GetSpriteFont(fontHandle, &font);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            info, CNA_CNB_SPRITE_FONT_INFO_STRUCT_VERSION,
            "The font-info structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbSpriteFontData& data = *font->value;
    data.lineSpacing = info->line_spacing;
    data.spacing = info->spacing;
    if (info->has_default_character != CNA_FALSE) {
        data.defaultCharacter = static_cast<SharpRuntime::charcs>(info->default_character);
    } else {
        data.defaultCharacter.reset();
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbSpriteFontDataAddGlyph(
    const CNA_CnbSpriteFontDataHandle fontHandle,
    const CNA_SpriteFontGlyph* const glyph,
    uint64_t* const outIndex)
{
    std::shared_ptr<CnbSpriteFontResource> font;
    if (const CNA_Result result = GetSpriteFont(fontHandle, &font);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            glyph, SpriteFontGlyphStructVersion,
            "The glyph structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbSpriteFontData& data = *font->value;
    const std::size_t index = data.characters.size();
    ResizeGlyphArrays(&data, index + 1U);
    if (const CNA_Result result = ReadGlyphRow(*glyph, &data, index);
        result != CNA_RESULT_SUCCESS) {
        ResizeGlyphArrays(&data, index);
        return result;
    }
    if (outIndex != nullptr) {
        *outIndex = static_cast<uint64_t>(index);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbSpriteFontDataGetGlyph(
    const CNA_CnbSpriteFontDataHandle fontHandle,
    const uint64_t index,
    CNA_SpriteFontGlyph* const outGlyph)
{
    std::shared_ptr<CnbSpriteFontResource> font;
    if (const CNA_Result result = GetSpriteFont(fontHandle, &font);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outGlyph, SpriteFontGlyphStructVersion,
            "The glyph structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            index, font->value->characters.size(), "The glyph index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    WriteGlyphRow(*font->value, static_cast<std::size_t>(index), outGlyph);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbSpriteFontDataSetGlyph(
    const CNA_CnbSpriteFontDataHandle fontHandle,
    const uint64_t index,
    const CNA_SpriteFontGlyph* const glyph)
{
    std::shared_ptr<CnbSpriteFontResource> font;
    if (const CNA_Result result = GetSpriteFont(fontHandle, &font);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            glyph, SpriteFontGlyphStructVersion,
            "The glyph structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            index, font->value->characters.size(), "The glyph index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return ReadGlyphRow(*glyph, font->value.get(), static_cast<std::size_t>(index));
}

[[nodiscard]] CNA_Result CnbSoundEffectDataCreate(
    const CNA_CnbSoundEffectInfo* const info,
    const uint8_t* const samples,
    const uint64_t byteCount,
    CNA_CnbSoundEffectDataHandle* const outSound)
{
    if (const CNA_Result result = RequireStruct(
            info, CNA_CNB_SOUND_EFFECT_INFO_STRUCT_VERSION,
            "The sound-info structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::span<const uint8_t> source;
    if (const CNA_Result result = BorrowBytes(samples, byteCount, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbSoundEffectData data;
    data.format = static_cast<Cnb::CnbAudioFormat>(info->format);
    data.sampleRate = info->sample_rate;
    data.channels = info->channels;
    data.frameCount = info->frame_count;
    data.loopStart = info->loop_start;
    data.loopLength = info->loop_length;
    data.samples.assign(source.begin(), source.end());
    return CreateSoundEffect(std::move(data), outSound);
}

[[nodiscard]] CNA_Result CnbSoundEffectDataGetInfo(
    const CNA_CnbSoundEffectDataHandle soundHandle,
    CNA_CnbSoundEffectInfo* const outInfo)
{
    std::shared_ptr<CnbSoundEffectResource> sound;
    if (const CNA_Result result = GetSoundEffect(soundHandle, &sound);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_SOUND_EFFECT_INFO_STRUCT_VERSION,
            "The sound-info structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbSoundEffectData& data = *sound->value;
    outInfo->format = static_cast<CNA_CnbAudioFormat>(data.format);
    outInfo->sample_rate = data.sampleRate;
    outInfo->channels = data.channels;
    outInfo->frame_count = data.frameCount;
    outInfo->loop_start = data.loopStart;
    outInfo->loop_length = data.loopLength;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbEncodeVideo(
    const CNA_StringView streamReference,
    const CNA_CnbVideoInfo* const info,
    const CNA_StringView contentName,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The encoded video output is invalid.");
    }
    if (const CNA_Result result = RequireStruct(
            info, CNA_CNB_VIDEO_INFO_STRUCT_VERSION,
            "The video-info structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Cnb::CnbVideoData data;
    if (const CNA_Result result = ReadText(
            streamReference, "The stream_reference text is not valid UTF-8.",
            &data.streamReference);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    data.durationMs = info->duration_milliseconds;
    data.width = info->width;
    data.height = info->height;
    data.framesPerSecond = info->frames_per_second;
    data.soundtrackType = info->soundtrack_type;
    std::string contentText;
    if (const CNA_Result result =
            ReadText(contentName, "The content_name text is not valid UTF-8.", &contentText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<uint8_t> encoded = Cnb::EncodeVideoToCnb(data, contentText);
    return CopyBytes(encoded, destination, capacity, outBytes);
}

[[nodiscard]] CNA_Result CnbDecodeVideo(
    const CNA_CnbDocumentHandle documentHandle,
    CNA_CnbVideoInfo* const outInfo)
{
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireStruct(
            outInfo, CNA_CNB_VIDEO_INFO_STRUCT_VERSION,
            "The video-info structure is not a known size and version.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Cnb::CnbVideoData data = Cnb::DecodeVideoFromCnb(*document->value);
    outInfo->duration_milliseconds = data.durationMs;
    outInfo->width = data.width;
    outInfo->height = data.height;
    outInfo->frames_per_second = data.framesPerSecond;
    outInfo->soundtrack_type = static_cast<CNA_VideoSoundtrackType>(data.soundtrackType);
    outInfo->reserved = 0U;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbEncodeCurve(
    const CNA_CurveHandle curveHandle,
    const CNA_StringView contentName,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    std::shared_ptr<Microsoft::Xna::Framework::Curve> curve;
    if (const CNA_Result result = LookupHandle(curveHandle, ObjectKind::Curve, &curve);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The encoded curve output is invalid.");
    }
    std::string nameText;
    if (const CNA_Result result =
            ReadText(contentName, "The content_name text is not valid UTF-8.", &nameText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<uint8_t> encoded = Cnb::EncodeCurveToCnb(*curve, nameText);
    return CopyBytes(encoded, destination, capacity, outBytes);
}

[[nodiscard]] CNA_Result CnbEncodeAnimationClip(
    const CNA_AnimationClipEXTDescriptor* const descriptor,
    const CNA_ClipTargetSpaceEXT targetSpace,
    const CNA_StringView contentName,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The encoded clip output is invalid.");
    }
    Microsoft::Xna::Framework::Graphics::AnimationClipEXT clip;
    if (const CNA_Result result = BorrowClip(descriptor, targetSpace, &clip);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nameText;
    if (const CNA_Result result =
            ReadText(contentName, "The content_name text is not valid UTF-8.", &nameText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<uint8_t> encoded = Cnb::EncodeAnimationClipToCnb(clip, nameText);
    return CopyBytes(encoded, destination, capacity, outBytes);
}

[[nodiscard]] CNA_Result CnbAnimationClipGet(
    const CNA_CnbAnimationClipHandle clipHandle,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount,
    CNA_ClipTargetSpaceEXT* const outTargetSpace)
{
    std::shared_ptr<CnbAnimationClipResource> clip;
    if (const CNA_Result result = GetAnimationClip(clipHandle, &clip);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (outDurationSeconds != nullptr) {
        *outDurationSeconds = clip->value->Duration.getTotalSecondsProperty();
    }
    if (outTrackCount != nullptr) {
        *outTrackCount = static_cast<uint64_t>(clip->value->Tracks.size());
    }
    if (outTargetSpace != nullptr) {
        *outTargetSpace = static_cast<CNA_ClipTargetSpaceEXT>(clip->value->TargetSpace);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbAnimationClipGetTrack(
    const CNA_CnbAnimationClipHandle clipHandle,
    const uint64_t track,
    int32_t* const outBoneIndex,
    uint64_t* const outKeyframeCount)
{
    std::shared_ptr<CnbAnimationClipResource> clip;
    if (const CNA_Result result = GetAnimationClip(clipHandle, &clip);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireIndex(
            track, clip->value->Tracks.size(), "The animation track index is out of range.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& stored = clip->value->Tracks[static_cast<std::size_t>(track)];
    if (outBoneIndex != nullptr) {
        *outBoneIndex = stored.BoneIndex;
    }
    if (outKeyframeCount != nullptr) {
        *outKeyframeCount = static_cast<uint64_t>(stored.Keys.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbReaderReadKeyframe(
    const CNA_CnbReaderHandle readerHandle,
    CNA_KeyframeEXT* const outKeyframe)
{
    std::shared_ptr<CnbReaderResource> reader;
    if (const CNA_Result result = GetReader(readerHandle, &reader);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Microsoft::Xna::Framework::Graphics::KeyframeEXT key =
        Cnb::ReadCnbKeyframe(*reader->value);
    outKeyframe->time_seconds = key.Time.getTotalSecondsProperty();
    outKeyframe->translation.x = key.Translation.X;
    outKeyframe->translation.y = key.Translation.Y;
    outKeyframe->translation.z = key.Translation.Z;
    outKeyframe->rotation.x = key.Rotation.X;
    outKeyframe->rotation.y = key.Rotation.Y;
    outKeyframe->rotation.z = key.Rotation.Z;
    outKeyframe->rotation.w = key.Rotation.W;
    outKeyframe->scale.x = key.Scale.X;
    outKeyframe->scale.y = key.Scale.Y;
    outKeyframe->scale.z = key.Scale.Z;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbByteWriterWriteKeyframe(
    const CNA_CnbByteWriterHandle writerHandle,
    const CNA_KeyframeEXT* const keyframe)
{
    std::shared_ptr<CnbByteWriterResource> writer;
    if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (keyframe == nullptr) {
        return InvalidArgument("The keyframe is null.");
    }
    if (!IsValidTimeSpanSeconds(keyframe->time_seconds)) {
        return InvalidArgument("A keyframe time must fit a finite TimeSpan.");
    }
    Microsoft::Xna::Framework::Graphics::KeyframeEXT key;
    key.Time = System::TimeSpan::FromSeconds(keyframe->time_seconds);
    key.Translation = {
        keyframe->translation.x, keyframe->translation.y, keyframe->translation.z};
    key.Rotation = {
        keyframe->rotation.x, keyframe->rotation.y, keyframe->rotation.z,
        keyframe->rotation.w};
    key.Scale = {keyframe->scale.x, keyframe->scale.y, keyframe->scale.z};
    Cnb::WriteCnbKeyframe(*writer->value, key);
    return CNA_RESULT_SUCCESS;
}

} // namespace

/* --- CBIND-111: the loader registry and the two compilation front ends ------------------------- */

namespace {

[[nodiscard]] CNA_Result CnbLoaderRegistryRegister(
    const uint32_t assetTypeId,
    const CNA_StringView canonicalTypeName,
    const CNA_CnbLoaderCallback callback,
    void* const context)
{
    if (callback == nullptr) {
        return InvalidArgument("The CNB loader callback is null.");
    }
    std::string nameText;
    if (const CNA_Result result = ReadText(
            canonicalTypeName, "The canonical_type_name text is not valid UTF-8.", &nameText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    CNA::Content::CnbLoaderRegistry::Register(
        assetTypeId, nameText, WrapLoaderCallback(callback, context, nameText));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbLoaderRegistryRemove(
    const uint32_t assetTypeId,
    CNA_Bool* const outRemoved)
{
    const bool removed = CNA::Content::CnbLoaderRegistry::Remove(assetTypeId);
    if (outRemoved != nullptr) {
        *outRemoved = ToCBool(removed);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbLoaderRegistryFind(
    const uint32_t assetTypeId,
    CNA_Bool* const outFound,
    CNA_CnbLoaderHandle* const outLoader)
{
    std::optional<CNA::Content::CnbLoaderRegistry::LoaderFn> found =
        CNA::Content::CnbLoaderRegistry::Find(assetTypeId);
    if (outFound != nullptr) {
        *outFound = ToCBool(found.has_value());
    }
    if (!found.has_value()) {
        return CNA_RESULT_SUCCESS;
    }
    return CreateLoader(std::move(found).value(), outLoader);
}

[[nodiscard]] CNA_Result CnbLoaderInvoke(
    const CNA_CnbLoaderHandle loaderHandle,
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_Handle contentManagerHandle,
    const CNA_StringView assetName,
    void** const outObject)
{
    std::shared_ptr<CnbLoaderResource> loader;
    if (const CNA_Result result = GetLoader(loaderHandle, &loader);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<CnbDocumentResource> document;
    if (const CNA_Result result = GetDocument(documentHandle, &document);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outObject = nullptr;
    std::string assetNameText;
    if (const CNA_Result result =
            ReadText(assetName, "The asset_name text is not valid UTF-8.", &assetNameText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The canonical signature takes the manager by reference, so there is no "no manager" to
    // pass. Manufacturing a placeholder one would be worse than requiring it: a content manager
    // constructor installs the built-in loaders, and inventing that side effect to satisfy a
    // reference would make an invoke do something a caller never asked for.
    Microsoft::Xna::Framework::Content::ContentManager* manager = nullptr;
    if (const CNA_Result result = CNA::C::Detail::GetContentManagerObject(
            contentManagerHandle, &manager);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::any produced = loader->value(*document->value, *manager, assetNameText);
    const auto* const foreign =
        std::any_cast<CNA::Content::ForeignContentObjectEXT>(&produced);
    if (foreign == nullptr) {
        return NotSupported(
            "The loader produced a C++ object rather than one this ABI can hand to C; only a "
            "loader registered through cna_cnb_loader_registry_register produces the latter.");
    }
    *outObject = foreign->value;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CnbImportImageAsTexture2d(
    const CNA_StringView imagePath,
    const CNA_CnbImageImportOptions* const options,
    CNA_CnbTextureDataHandle* const outTexture)
{
    Cnb::CnbImageImportOptions native;
    if (options != nullptr) {
        if (const CNA_Result result = RequireStruct(
                options, CNA_CNB_IMAGE_IMPORT_OPTIONS_STRUCT_VERSION,
                "The image-import options are not a known size and version.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ValidateCanonicalBool(options->has_color_key, "has_color_key");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (options->has_color_key != CNA_FALSE) {
            native.colorKey = std::array<uint8_t, 3>{
                options->color_key[0], options->color_key[1], options->color_key[2]};
        }
    }
    std::string pathText;
    if (const CNA_Result result =
            ReadText(imagePath, "The image_path text is not valid UTF-8.", &pathText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // CBIND-111: the canonical importer documents a content failure "if the file cannot be
    // read or decoded", but the shared image decoder raises a plain `std::runtime_error` for an
    // unreadable file and the importer translates only its own two checks. Left alone, the
    // firewall would answer CNA_RESULT_INTERNAL -- "a bug in CNA" -- for a caller whose file is
    // simply not there. The C layer answers the documented contract instead; the canonical gap
    // is recorded in plans/plan_binding.md rather than patched here.
    try {
        return CreateTextureData(Cnb::ImportImageAsCnbTexture2D(pathText, native), outTexture);
    } catch (const Microsoft::Xna::Framework::Content::ContentLoadException&) {
        throw;
    } catch (const std::runtime_error& exception) {
        return IoError(exception.what());
    }
}

[[nodiscard]] CNA_Result CnbDecodeDdsAsTextureCube(
    const uint8_t* const bytes,
    const uint64_t byteCount,
    const CNA_StringView origin,
    CNA_CnbTextureDataHandle* const outTexture)
{
    std::span<const uint8_t> source;
    if (const CNA_Result result = BorrowBytes(bytes, byteCount, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string originText;
    if (const CNA_Result result =
            ReadText(origin, "The origin text is not valid UTF-8.", &originText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateTextureData(
        Cnb::DecodeDdsAsCnbTextureCube(source, originText), outTexture);
}

[[nodiscard]] CNA_Result CnbDecodeWavAsSoundEffect(
    const uint8_t* const bytes,
    const uint64_t byteCount,
    const CNA_StringView origin,
    CNA_CnbSoundEffectDataHandle* const outSound)
{
    std::span<const uint8_t> source;
    if (const CNA_Result result = BorrowBytes(bytes, byteCount, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string originText;
    if (const CNA_Result result =
            ReadText(origin, "The origin text is not valid UTF-8.", &originText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateSoundEffect(
        Cnb::DecodeWavAsCnbSoundEffect(source, originText), outSound);
}

[[nodiscard]] CNA_Result CnbCompileCnj(
    const CNA_StringView cnjPath,
    const CNA_StringView contentRoot,
    const CNA_StringView contentName,
    CNA_CnjToCnbResultHandle* const outResult)
{
    std::string pathText;
    if (const CNA_Result result =
            ReadText(cnjPath, "The cnj_path text is not valid UTF-8.", &pathText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string rootText;
    if (const CNA_Result result =
            ReadText(contentRoot, "The content_root text is not valid UTF-8.", &rootText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nameText;
    if (const CNA_Result result =
            ReadText(contentName, "The content_name text is not valid UTF-8.", &nameText);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CreateCompiledCnj(
        Cnb::CompileCnjToCnb(pathText, rootText, nameText), outResult);
}

} // namespace

// The routes generated from modules/c-api/spec/Cnb.toml.
#include "generated/Cnb.inc"
