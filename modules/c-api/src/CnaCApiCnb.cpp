// SPDX-License-Identifier: MS-PL

#include "CNA/C/cnb.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbChunkCompression.hpp"
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelData.hpp"
#include "CNA/Content/Cnb/CnbModelFromCnj.hpp"
#include "CNA/Content/Cnb/CnbReadLimits.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureFormat.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"

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

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateBuffer;
using CNA::C::Detail::ValidateCanonicalBool;

namespace Cnb = CNA::Content::Cnb;

namespace {

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result NotSupported(const std::string& message)
{
    return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, message);
}

/// The count half of every count/copy pair in this family.
[[nodiscard]] CNA_Result ReportSize(const std::string_view value, uint64_t* const outBytes)
{
    if (outBytes == nullptr) {
        return InvalidArgument("The byte-count output is null.");
    }
    *outBytes = static_cast<uint64_t>(value.size());
    return CNA_RESULT_SUCCESS;
}

/// The copy half. No terminator, `out_bytes` always written, and a short capacity writes nothing.
[[nodiscard]] CNA_Result CopyText(
    const std::string_view value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The text output is invalid.");
    }
    *outBytes = static_cast<uint64_t>(value.size());
    if (capacity < static_cast<uint64_t>(value.size())) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

/// The same shape for bytes rather than text.
[[nodiscard]] CNA_Result CopyBytes(
    const std::vector<uint8_t>& value,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    *outBytes = static_cast<uint64_t>(value.size());
    if (capacity < static_cast<uint64_t>(value.size())) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the produced bytes.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

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
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The byte count does not fit in this host's size type.");
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
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::CnbDocument, outDocument);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), "The CNB document handle is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetReader(
    const CNA_CnbReaderHandle handle,
    std::shared_ptr<CnbReaderResource>* const outReader)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::CnbReader, outReader);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), "The CNB reader handle is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetByteWriter(
    const CNA_CnbByteWriterHandle handle,
    std::shared_ptr<CnbByteWriterResource>* const outWriter)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::CnbByteWriter, outWriter);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result), "The CNB byte-writer handle is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetWriter(
    const CNA_CnbWriterHandle handle,
    std::shared_ptr<CnbWriterResource>* const outWriter)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::CnbWriter, outWriter);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), "The CNB writer handle is invalid.");
    }
    return CNA_RESULT_SUCCESS;
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
    if (limits->struct_size < static_cast<uint32_t>(sizeof(CNA_CnbReadLimits)) ||
        limits->struct_version != CNA_CNB_READ_LIMITS_STRUCT_VERSION) {
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
[[nodiscard]] CNA_Result RequireIndex(
    const uint64_t index,
    const std::size_t count,
    const char* const what)
{
    if (index >= static_cast<uint64_t>(count)) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_RANGE, what);
    }
    return CNA_RESULT_SUCCESS;
}

/// Copies a caller string view into an owned std::string, applying this ABI's UTF-8 input rule.
[[nodiscard]] CNA_Result BorrowText(const CNA_StringView value, std::string* const out)
{
    return CopyStringView(value, false, out);
}

/// CBIND-108: the decoded texture description. Nested vectors, so a handle rather than a POD.
struct CnbTextureDataResource final {
    std::shared_ptr<Cnb::CnbTextureData> value;
};

[[nodiscard]] CNA_Result GetTextureData(
    const CNA_CnbTextureDataHandle handle,
    std::shared_ptr<CnbTextureDataResource>* const outTexture)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::CnbTextureData, outTexture);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result), "The CNB texture handle is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] Cnb::CnbTextureFormat ToTextureFormat(const CNA_CnbTextureFormat format) noexcept
{
    return static_cast<Cnb::CnbTextureFormat>(format);
}

[[nodiscard]] CNA_Result CreateTextureData(
    Cnb::CnbTextureData data,
    CNA_CnbTextureDataHandle* const outTexture)
{
    const auto resource = std::make_shared<CnbTextureDataResource>();
    resource->value = std::make_shared<Cnb::CnbTextureData>(std::move(data));
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::CnbTextureData, resource, outTexture);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The CNB texture handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
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
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::CnbModelData, outModel);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), "The CNB model handle is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetCompileResult(
    const CNA_CnbModelFromCnjHandle handle,
    std::shared_ptr<CnbModelFromCnjResource>* const outResult)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::CnbModelFromCnj, outResult);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result), "The CNB model compile handle is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreateModel(
    Cnb::CnbModelData data,
    CNA_CnbModelDataHandle* const outModel)
{
    const auto resource = std::make_shared<CnbModelResource>();
    resource->value = std::make_shared<Cnb::CnbModelData>(std::move(data));
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::CnbModelData, resource, outModel);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The CNB model handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
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
    if (value->struct_size < static_cast<uint32_t>(sizeof(TStruct)) ||
        value->struct_version != version) {
        return InvalidArgument(what);
    }
    return CNA_RESULT_SUCCESS;
}

/// The copy half of a count/copy pair whose element is a float rather than a byte or a character.
[[nodiscard]] CNA_Result CopyFloats(
    const std::vector<float>& value,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The float output is invalid.");
    }
    *outCount = static_cast<uint64_t>(value.size());
    if (capacity < static_cast<uint64_t>(value.size())) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the float count.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size() * sizeof(float));
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
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The element count does not fit in this host's size type.");
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
    info->vertex_color_enabled = part.vertexColorEnabled ? CNA_TRUE : CNA_FALSE;
    info->unlit = part.unlit ? CNA_TRUE : CNA_FALSE;
    info->reserved[0] = 0U;
    info->reserved[1] = 0U;
}

/// The largest number of seconds a native `TimeSpan` represents, matching the models family.
constexpr double MaxTimeSpanSeconds = 922337203685.0;

[[nodiscard]] bool IsValidTimeSpanSeconds(const double value) noexcept
{
    return std::isfinite(value) && value >= -MaxTimeSpanSeconds && value <= MaxTimeSpanSeconds;
}

} // namespace

CNA_Result cna_cnb_read_limits_init(CNA_CnbReadLimits* const outLimits)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLimits == nullptr) {
            return InvalidArgument("The read-limits output is null.");
        }
        if (outLimits->struct_size < static_cast<uint32_t>(sizeof(CNA_CnbReadLimits)) ||
            outLimits->struct_version != CNA_CNB_READ_LIMITS_STRUCT_VERSION) {
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
    });
}

CNA_Result cna_cnb_copy_format_magic(
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The magic-bytes output is invalid.");
        }
        const std::vector<uint8_t> magic(Cnb::Format::Magic.begin(), Cnb::Format::Magic.end());
        return CopyBytes(magic, destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_make_chunk_id(
    const uint8_t a,
    const uint8_t b,
    const uint8_t c,
    const uint8_t d,
    CNA_CnbChunkId* const outId)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outId == nullptr) {
            return InvalidArgument("The chunk-identifier output is null.");
        }
        *outId = Cnb::MakeChunkId(
                     static_cast<char>(a),
                     static_cast<char>(b),
                     static_cast<char>(c),
                     static_cast<char>(d))
                     .value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_get_chunk_id_string_size(
    const CNA_CnbChunkId id,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportSize(Cnb::ChunkIdToString(Cnb::CnbChunkId{id}), outByteCount);
    });
}

CNA_Result cna_cnb_copy_chunk_id_string(
    const CNA_CnbChunkId id,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyText(Cnb::ChunkIdToString(Cnb::CnbChunkId{id}),
                        destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_is_well_formed_chunk_id(
    const CNA_CnbChunkId id,
    CNA_Bool* const outWellFormed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWellFormed == nullptr) {
            return InvalidArgument("The chunk-identifier output is null.");
        }
        *outWellFormed = Cnb::IsWellFormedChunkId(Cnb::CnbChunkId{id}) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_asset_type_id_from_name(
    const CNA_StringView name,
    uint32_t* const outAssetTypeId)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAssetTypeId == nullptr) {
            return InvalidArgument("The asset-type-identifier output is null.");
        }
        std::string value;
        if (const CNA_Result result = CopyStringView(name, true, &value);
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
    });
}

CNA_Result cna_cnb_is_custom_asset_type_id(
    const uint32_t assetTypeId,
    CNA_Bool* const outCustom)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCustom == nullptr) {
            return InvalidArgument("The asset-type classification output is null.");
        }
        *outCustom = Cnb::IsCustomAssetTypeId(assetTypeId) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_get_asset_type_name_size(
    const uint32_t assetTypeId,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportSize(Cnb::AssetTypeIdToString(assetTypeId), outByteCount);
    });
}

CNA_Result cna_cnb_copy_asset_type_name(
    const uint32_t assetTypeId,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyText(Cnb::AssetTypeIdToString(assetTypeId), destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_get_logical_name_problem_size(
    const CNA_StringView logicalName,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        // Deliberately not ValidateStringView: "is not well-formed UTF-8" is one of the verdicts
        // this function exists to report, so refusing malformed input here would withhold the
        // answer the caller asked for. Only the pointer/length pair itself is checked.
        if (logicalName.data == nullptr && logicalName.byte_length != 0U) {
            return InvalidArgument("The logical name is null with a non-zero length.");
        }
        const std::string_view view(
            logicalName.data == nullptr ? "" : logicalName.data,
            static_cast<std::size_t>(logicalName.byte_length));
        return ReportSize(Cnb::CnbLogicalNameProblem(view), outByteCount);
    });
}

CNA_Result cna_cnb_copy_logical_name_problem(
    const CNA_StringView logicalName,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (logicalName.data == nullptr && logicalName.byte_length != 0U) {
            return InvalidArgument("The logical name is null with a non-zero length.");
        }
        const std::string_view view(
            logicalName.data == nullptr ? "" : logicalName.data,
            static_cast<std::size_t>(logicalName.byte_length));
        return CopyText(Cnb::CnbLogicalNameProblem(view), destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_checked_add(const uint64_t a, const uint64_t b, uint64_t* const outSum)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSum == nullptr) {
            return InvalidArgument("The sum output is null.");
        }
        // Mirrors CheckedAdd's own guard exactly, so the answer is CNA_RESULT_OVERFLOW rather than
        // whatever the firewall makes of a ContentLoadException. The canonical function still
        // performs the arithmetic: if this condition ever drifted from its guard, the canonical
        // throw would fire and the disagreement would surface as CNA_RESULT_IO rather than hide.
        if (a > std::numeric_limits<uint64_t>::max() - b) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The declared offset and size overflow a 64-bit byte count.");
        }
        *outSum = Cnb::CheckedAdd(a, b, "cna_cnb_checked_add");
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_checked_multiply(const uint64_t a, const uint64_t b, uint64_t* const outProduct)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outProduct == nullptr) {
            return InvalidArgument("The product output is null.");
        }
        if (a != 0U && b > std::numeric_limits<uint64_t>::max() / a) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The declared element count and element size overflow a 64-bit byte count.");
        }
        *outProduct = Cnb::CheckedMultiply(a, b, "cna_cnb_checked_multiply");
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_crc32c(
    const uint8_t* const data,
    const uint64_t byteCount,
    uint32_t* const outChecksum)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outChecksum == nullptr) {
            return InvalidArgument("The checksum output is null.");
        }
        std::span<const uint8_t> bytes;
        if (const CNA_Result result = BorrowBytes(data, byteCount, &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outChecksum = Cnb::Crc32c(bytes);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_crc32c_continue(
    const uint32_t previous,
    const uint8_t* const data,
    const uint64_t byteCount,
    uint32_t* const outChecksum)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outChecksum == nullptr) {
            return InvalidArgument("The checksum output is null.");
        }
        std::span<const uint8_t> bytes;
        if (const CNA_Result result = BorrowBytes(data, byteCount, &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outChecksum = Cnb::Crc32cContinue(previous, bytes);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_crc32c_uses_hardware(CNA_Bool* const outUsesHardware)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outUsesHardware == nullptr) {
            return InvalidArgument("The hardware-path output is null.");
        }
        *outUsesHardware = Cnb::Crc32cUsesHardwareEXT() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_crc32c_portable(
    const uint8_t* const data,
    const uint64_t byteCount,
    uint32_t* const outChecksum)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outChecksum == nullptr) {
            return InvalidArgument("The checksum output is null.");
        }
        std::span<const uint8_t> bytes;
        if (const CNA_Result result = BorrowBytes(data, byteCount, &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outChecksum = Cnb::Crc32cPortableEXT(bytes);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_is_compression_supported(
    const CNA_CnbCompression codec,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidArgument("The compression-support output is null.");
        }
        *outSupported = Cnb::IsCnbCompressionSupported(ToCodec(codec)) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_get_compression_name_size(
    const CNA_CnbCompression codec,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportSize(Cnb::CnbCompressionToString(ToCodec(codec)), outByteCount);
    });
}

CNA_Result cna_cnb_copy_compression_name(
    const CNA_CnbCompression codec,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyText(
            Cnb::CnbCompressionToString(ToCodec(codec)), destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_get_compressed_byte_count(
    const uint8_t* const raw,
    const uint64_t rawByteCount,
    const CNA_CnbCompression codec,
    const int32_t level,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The compressed byte-count output is null.");
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
        *outByteCount = static_cast<uint64_t>(compressed.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_copy_compressed(
    const uint8_t* const raw,
    const uint64_t rawByteCount,
    const CNA_CnbCompression codec,
    const int32_t level,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
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
    });
}

CNA_Result cna_cnb_copy_decompressed(
    const uint8_t* const stored,
    const uint64_t storedByteCount,
    const CNA_CnbCompression codec,
    const uint64_t uncompressedSize,
    const uint64_t maxUncompressedSize,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The decompressed output buffer is invalid.");
        }
        std::span<const uint8_t> bytes;
        if (const CNA_Result result = BorrowBytes(stored, storedByteCount, &bytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const Cnb::CnbCompression nativeCodec = ToCodec(codec);
        // The order below is DecompressCnbChunk's own, and the order is contract rather than
        // style: an unsupported codec with an over-ceiling size is reported as the ceiling, and a
        // stored chunk consults neither size at all. Reversing any pair would answer a different
        // question than the canonical reader answers about the same file.
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
        const std::vector<uint8_t> raw = Cnb::DecompressCnbChunk(
            bytes, nativeCodec, uncompressedSize, maxUncompressedSize, "The CNB chunk");
        return CopyBytes(raw, destination, capacity, outByteCount);
    });
}

/* --- CBIND-107: the parsed document ---------------------------------------------------------- */

CNA_Result cna_cnb_chunk_entry_is_mandatory(
    const CNA_CnbChunkEntry* const entry,
    CNA_Bool* const outMandatory)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (entry == nullptr || outMandatory == nullptr) {
            return InvalidArgument("The chunk-entry input or output is null.");
        }
        if (entry->struct_size < static_cast<uint32_t>(sizeof(CNA_CnbChunkEntry)) ||
            entry->struct_version != CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION) {
            return InvalidArgument("The chunk entry is not a known size and version.");
        }
        Cnb::CnbChunkEntry native;
        native.flags = entry->flags;
        *outMandatory = native.IsMandatory() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_has_magic(
    const uint8_t* const bytes,
    const uint64_t byteCount,
    CNA_Bool* const outHasMagic)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasMagic == nullptr) {
            return InvalidArgument("The magic-check output is null.");
        }
        std::span<const uint8_t> span;
        if (const CNA_Result result = BorrowBytes(bytes, byteCount, &span);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasMagic = Cnb::CnbDocument::HasMagic(span) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

[[nodiscard]] CNA_Result CreateDocument(
    Cnb::CnbDocument document,
    CNA_CnbDocumentHandle* const outDocument)
{
    const auto resource = std::make_shared<CnbDocumentResource>();
    resource->value = std::make_shared<Cnb::CnbDocument>(std::move(document));
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::CnbDocument, resource, outDocument);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result, ErrorCategoryForResult(result),
            "The CNB document handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_cnb_document_parse(
    const uint8_t* const bytes,
    const uint64_t byteCount,
    const CNA_StringView origin,
    const CNA_CnbReadLimits* const limits,
    CNA_CnbDocumentHandle* const outDocument)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDocument == nullptr) {
            return InvalidArgument("The CNB document output handle is null.");
        }
        *outDocument = CNA_INVALID_HANDLE;
        std::span<const uint8_t> span;
        if (const CNA_Result result = BorrowBytes(bytes, byteCount, &span);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string originText;
        if (const CNA_Result result = BorrowText(origin, &originText);
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
    });
}

CNA_Result cna_cnb_document_parse_file(
    const CNA_StringView path,
    const CNA_CnbReadLimits* const limits,
    CNA_CnbDocumentHandle* const outDocument)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDocument == nullptr) {
            return InvalidArgument("The CNB document output handle is null.");
        }
        *outDocument = CNA_INVALID_HANDLE;
        std::string pathText;
        if (const CNA_Result result = BorrowText(path, &pathText);
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
    });
}

CNA_Result cna_cnb_document_destroy(const CNA_CnbDocumentHandle documentHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (document->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "Every reader opened from this CNB document must be destroyed before it.");
        }
        const CNA_Result result = GetRuntimeHandles().Release(documentHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB document handle could not be destroyed.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_origin_size(
    const CNA_CnbDocumentHandle documentHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(document->value->Origin(), outByteCount);
    });
}

CNA_Result cna_cnb_document_copy_origin(
    const CNA_CnbDocumentHandle documentHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(document->value->Origin(), destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_document_get_container_major(
    const CNA_CnbDocumentHandle documentHandle,
    uint16_t* const outMajor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outMajor == nullptr) {
            return InvalidArgument("The container-version output is null.");
        }
        *outMajor = document->value->ContainerMajor();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_container_minor(
    const CNA_CnbDocumentHandle documentHandle,
    uint16_t* const outMinor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outMinor == nullptr) {
            return InvalidArgument("The container-version output is null.");
        }
        *outMinor = document->value->ContainerMinor();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_asset_type_id(
    const CNA_CnbDocumentHandle documentHandle,
    uint32_t* const outAssetTypeId)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outAssetTypeId == nullptr) {
            return InvalidArgument("The asset-type-identifier output is null.");
        }
        *outAssetTypeId = document->value->AssetTypeId();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_asset_schema_version(
    const CNA_CnbDocumentHandle documentHandle,
    uint32_t* const outSchemaVersion)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outSchemaVersion == nullptr) {
            return InvalidArgument("The schema-version output is null.");
        }
        *outSchemaVersion = document->value->AssetSchemaVersion();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_chunk_count(
    const CNA_CnbDocumentHandle documentHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr) {
            return InvalidArgument("The chunk-count output is null.");
        }
        *outCount = static_cast<uint64_t>(document->value->ChunkCount());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_chunk(
    const CNA_CnbDocumentHandle documentHandle,
    const uint64_t index,
    CNA_CnbChunkEntry* const outEntry)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outEntry == nullptr) {
            return InvalidArgument("The chunk-entry output is null.");
        }
        if (outEntry->struct_size < static_cast<uint32_t>(sizeof(CNA_CnbChunkEntry)) ||
            outEntry->struct_version != CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION) {
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
    });
}

CNA_Result cna_cnb_document_copy_chunk_data(
    const CNA_CnbDocumentHandle documentHandle,
    const uint64_t index,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The chunk-data output buffer is invalid.");
        }
        if (const CNA_Result result = RequireIndex(
                index, document->value->ChunkCount(),
                "The chunk index is outside this document's table of contents.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::span<const uint8_t> data =
            document->value->ChunkData(static_cast<std::size_t>(index));
        *outByteCount = static_cast<uint64_t>(data.size());
        if (capacity < static_cast<uint64_t>(data.size())) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the chunk.");
        }
        if (!data.empty()) {
            std::memcpy(destination, data.data(), data.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_open_chunk(
    const CNA_CnbDocumentHandle documentHandle,
    const uint64_t index,
    CNA_CnbReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReader == nullptr) {
            return InvalidArgument("The CNB reader output handle is null.");
        }
        *outReader = CNA_INVALID_HANDLE;
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
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::CnbReader, resource, outReader);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB reader handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_find_all(
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_CnbChunkId type,
    uint64_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The chunk-index output buffer is invalid.");
        }
        const std::vector<std::size_t> found = document->value->FindAll(Cnb::CnbChunkId{type});
        *outCount = static_cast<uint64_t>(found.size());
        if (capacity < static_cast<uint64_t>(found.size())) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the number of matching chunks.");
        }
        for (std::size_t i = 0; i < found.size(); ++i) {
            destination[i] = static_cast<uint64_t>(found[i]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_find_single(
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_CnbChunkId type,
    CNA_Bool* const outFound,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outFound == nullptr || outIndex == nullptr) {
            return InvalidArgument("The chunk-lookup output is null.");
        }
        const std::optional<std::size_t> found =
            document->value->FindSingle(Cnb::CnbChunkId{type});
        // Absence is an ordinary answer, never a failure, so it is reported beside the value and
        // the value is left alone -- the availability convention every optional query here uses.
        *outFound = found.has_value() ? CNA_TRUE : CNA_FALSE;
        if (found.has_value()) {
            *outIndex = static_cast<uint64_t>(*found);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_require_single(
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_CnbChunkId type,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outIndex == nullptr) {
            return InvalidArgument("The chunk-index output is null.");
        }
        *outIndex = static_cast<uint64_t>(document->value->RequireSingle(Cnb::CnbChunkId{type}));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_require_mandatory_chunks_understood(
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_CnbChunkId* const knownTypes,
    const uint64_t knownTypeCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (knownTypes == nullptr && knownTypeCount != 0U) {
            return InvalidArgument("The known chunk-identifier array is null with a non-zero count.");
        }
        if (knownTypeCount > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
                "The known chunk-identifier count does not fit in this host's size type.");
        }
        std::vector<Cnb::CnbChunkId> native;
        native.reserve(static_cast<std::size_t>(knownTypeCount));
        for (uint64_t i = 0; i < knownTypeCount; ++i) {
            native.push_back(Cnb::CnbChunkId{knownTypes[i]});
        }
        document->value->RequireMandatoryChunksUnderstood(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_metadata(
    const CNA_CnbDocumentHandle documentHandle,
    CNA_CnbMetadata* const outMetadata)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outMetadata == nullptr) {
            return InvalidArgument("The metadata output is null.");
        }
        if (outMetadata->struct_size < static_cast<uint32_t>(sizeof(CNA_CnbMetadata)) ||
            outMetadata->struct_version != CNA_CNB_METADATA_STRUCT_VERSION) {
            return InvalidArgument("The metadata structure is not a known size and version.");
        }
        const Cnb::CnbMetadata& metadata = document->value->Metadata();
        outMetadata->present = metadata.present ? CNA_TRUE : CNA_FALSE;
        outMetadata->reserved[0] = 0U;
        outMetadata->reserved[1] = 0U;
        outMetadata->reserved[2] = 0U;
        outMetadata->flags = metadata.flags;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_metadata_asset_type_name_size(
    const CNA_CnbDocumentHandle documentHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(document->value->Metadata().assetTypeName, outByteCount);
    });
}

CNA_Result cna_cnb_document_copy_metadata_asset_type_name(
    const CNA_CnbDocumentHandle documentHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            document->value->Metadata().assetTypeName, destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_document_get_metadata_content_name_size(
    const CNA_CnbDocumentHandle documentHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(document->value->Metadata().contentName, outByteCount);
    });
}

CNA_Result cna_cnb_document_copy_metadata_content_name(
    const CNA_CnbDocumentHandle documentHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            document->value->Metadata().contentName, destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_document_get_external_reference_count(
    const CNA_CnbDocumentHandle documentHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr) {
            return InvalidArgument("The external-reference count output is null.");
        }
        *outCount = static_cast<uint64_t>(document->value->ExternalReferences().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_external_reference(
    const CNA_CnbDocumentHandle documentHandle,
    const uint64_t index,
    const CNA_StringView whatForDiagnostics,
    CNA_CnbExternalReference* const outReference)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outReference == nullptr) {
            return InvalidArgument("The external-reference output is null.");
        }
        if (outReference->struct_size <
                static_cast<uint32_t>(sizeof(CNA_CnbExternalReference)) ||
            outReference->struct_version != CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION) {
            return InvalidArgument(
                "The external-reference structure is not a known size and version.");
        }
        std::string what;
        if (const CNA_Result result = BorrowText(whatForDiagnostics, &what);
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
    });
}

CNA_Result cna_cnb_document_get_external_reference_name_size(
    const CNA_CnbDocumentHandle documentHandle,
    const uint64_t index,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireIndex(
                index, document->value->ExternalReferences().size(),
                "The external-reference index is outside the table.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(
            document->value->ExternalReferences()[static_cast<std::size_t>(index)].logicalName,
            outByteCount);
    });
}

CNA_Result cna_cnb_document_copy_external_reference_name(
    const CNA_CnbDocumentHandle documentHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireIndex(
                index, document->value->ExternalReferences().size(),
                "The external-reference index is outside the table.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            document->value->ExternalReferences()[static_cast<std::size_t>(index)].logicalName,
            destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_document_require_asset(
    const CNA_CnbDocumentHandle documentHandle,
    const uint32_t expectedAssetTypeId,
    const uint32_t maxSchemaVersion)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        document->value->RequireAsset(expectedAssetTypeId, maxSchemaVersion);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_get_limits(
    const CNA_CnbDocumentHandle documentHandle,
    CNA_CnbReadLimits* const outLimits)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outLimits == nullptr) {
            return InvalidArgument("The read-limits output is null.");
        }
        if (outLimits->struct_size < static_cast<uint32_t>(sizeof(CNA_CnbReadLimits)) ||
            outLimits->struct_version != CNA_CNB_READ_LIMITS_STRUCT_VERSION) {
            return InvalidArgument("The read-limits structure is not a known size and version.");
        }
        WriteLimits(document->value->Limits(), outLimits);
        return CNA_RESULT_SUCCESS;
    });
}

/* --- CBIND-107: the bounded reader ------------------------------------------------------------ */

CNA_Result cna_cnb_reader_create(
    const uint8_t* const data,
    const uint64_t byteCount,
    const CNA_StringView context,
    const CNA_CnbReadLimits* const limits,
    CNA_CnbReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReader == nullptr) {
            return InvalidArgument("The CNB reader output handle is null.");
        }
        *outReader = CNA_INVALID_HANDLE;
        std::span<const uint8_t> span;
        if (const CNA_Result result = BorrowBytes(data, byteCount, &span);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string contextText;
        if (const CNA_Result result = BorrowText(context, &contextText);
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
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::CnbReader, resource, outReader);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB reader handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_destroy(const CNA_CnbReaderHandle readerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(readerHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB reader handle could not be destroyed.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_get_remaining(
    const CNA_CnbReaderHandle readerHandle,
    uint64_t* const outRemaining)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outRemaining == nullptr) {
            return InvalidArgument("The remaining-bytes output is null.");
        }
        *outRemaining = static_cast<uint64_t>(reader->value->Remaining());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_get_position(
    const CNA_CnbReaderHandle readerHandle,
    uint64_t* const outPosition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outPosition == nullptr) {
            return InvalidArgument("The position output is null.");
        }
        *outPosition = static_cast<uint64_t>(reader->value->Position());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_get_size(
    const CNA_CnbReaderHandle readerHandle,
    uint64_t* const outSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outSize == nullptr) {
            return InvalidArgument("The region-size output is null.");
        }
        *outSize = static_cast<uint64_t>(reader->value->Size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_get_context_size(
    const CNA_CnbReaderHandle readerHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(reader->value->Context(), outByteCount);
    });
}

CNA_Result cna_cnb_reader_copy_context(
    const CNA_CnbReaderHandle readerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(reader->value->Context(), destination, capacity, outByteCount);
    });
}

/*
 * The seven fixed-width reads are one shape repeated. Each leaves its output untouched on a
 * truncation, because the canonical read throws before it assigns and the assignment here is the
 * last thing that happens.
 */
#define CNA_CNB_DEFINE_READ(routeName, cType, canonicalCall, outputName)                       \
    CNA_Result routeName(const CNA_CnbReaderHandle readerHandle, cType* const outValue)        \
    {                                                                                          \
        return CallWithExceptionBarrier([&]() -> CNA_Result {                                  \
            std::shared_ptr<CnbReaderResource> reader;                                         \
            if (const CNA_Result result = GetReader(readerHandle, &reader);                    \
                result != CNA_RESULT_SUCCESS) {                                                \
                return result;                                                                 \
            }                                                                                  \
            if (outValue == nullptr) {                                                         \
                return InvalidArgument(outputName);                                            \
            }                                                                                  \
            *outValue = reader->value->canonicalCall();                                        \
            return CNA_RESULT_SUCCESS;                                                         \
        });                                                                                    \
    }

CNA_CNB_DEFINE_READ(cna_cnb_reader_read_u8, uint8_t, ReadU8, "The byte output is null.")
CNA_CNB_DEFINE_READ(cna_cnb_reader_read_u16, uint16_t, ReadU16, "The value output is null.")
CNA_CNB_DEFINE_READ(cna_cnb_reader_read_u32, uint32_t, ReadU32, "The value output is null.")
CNA_CNB_DEFINE_READ(cna_cnb_reader_read_u64, uint64_t, ReadU64, "The value output is null.")
CNA_CNB_DEFINE_READ(cna_cnb_reader_read_i32, int32_t, ReadI32, "The value output is null.")
CNA_CNB_DEFINE_READ(cna_cnb_reader_read_f32, float, ReadF32, "The value output is null.")
CNA_CNB_DEFINE_READ(cna_cnb_reader_read_f64, double, ReadF64, "The value output is null.")

#undef CNA_CNB_DEFINE_READ

CNA_Result cna_cnb_reader_read_string(
    const CNA_CnbReaderHandle readerHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outByteCount == nullptr) {
            return InvalidArgument("The string byte-count output is null.");
        }
        // Read first, store second: a throw leaves the previous string in place rather than
        // clearing it, so a failed read cannot make an earlier successful one look like it never
        // happened.
        std::string value = reader->value->ReadString();
        *outByteCount = static_cast<uint64_t>(value.size());
        reader->lastString = std::move(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_copy_string(
    const CNA_CnbReaderHandle readerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!reader->lastString.has_value()) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "This CNB reader has not read a string yet.");
        }
        return CopyText(*reader->lastString, destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_reader_read_count(
    const CNA_CnbReaderHandle readerHandle,
    const uint64_t elementSize,
    const CNA_StringView whatIsBeingCounted,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr) {
            return InvalidArgument("The element-count output is null.");
        }
        std::string what;
        if (const CNA_Result result = BorrowText(whatIsBeingCounted, &what);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = reader->value->ReadCount(elementSize, what.c_str());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_read_bytes(
    const CNA_CnbReaderHandle readerHandle,
    const uint64_t byteCount,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The byte output buffer is invalid.");
        }
        *outByteCount = byteCount;
        // The size is the caller's own argument rather than something the file declares, so a
        // capacity that cannot hold it is settled BEFORE the cursor advances: a refused call
        // consumes nothing and can simply be repeated with a larger buffer.
        if (capacity < byteCount) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the requested byte count.");
        }
        const std::span<const uint8_t> bytes = reader->value->ReadBytes(byteCount);
        if (!bytes.empty()) {
            std::memcpy(destination, bytes.data(), bytes.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_skip(
    const CNA_CnbReaderHandle readerHandle,
    const uint64_t byteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->Skip(byteCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_require_exhausted(const CNA_CnbReaderHandle readerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->RequireExhausted();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_reader_fail(
    const CNA_CnbReaderHandle readerHandle,
    const CNA_StringView detail)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string detailText;
        if (const CNA_Result result = BorrowText(detail, &detailText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->Fail(detailText);
    });
}

CNA_Result cna_cnb_is_well_formed_utf8(
    const CNA_StringView text,
    CNA_Bool* const outWellFormed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWellFormed == nullptr) {
            return InvalidArgument("The UTF-8 validity output is null.");
        }
        // Deliberately not validated as UTF-8 by the ABI first: whether it is well-formed is the
        // question being asked, so refusing malformed bytes here would answer nothing.
        if (text.data == nullptr && text.byte_length != 0U) {
            return InvalidArgument("The text is null with a non-zero length.");
        }
        const std::string_view view(
            text.data == nullptr ? "" : text.data,
            static_cast<std::size_t>(text.byte_length));
        *outWellFormed = Cnb::CnbByteReader::IsWellFormedUtf8(view) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

/* --- CBIND-107: the primitive writer ---------------------------------------------------------- */

CNA_Result cna_cnb_byte_writer_create(CNA_CnbByteWriterHandle* const outWriter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWriter == nullptr) {
            return InvalidArgument("The CNB byte-writer output handle is null.");
        }
        *outWriter = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<CnbByteWriterResource>();
        resource->value = std::make_shared<Cnb::CnbByteWriter>();
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::CnbByteWriter, resource, outWriter);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB byte-writer handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_byte_writer_create_from_bytes(
    const uint8_t* const initial,
    const uint64_t byteCount,
    CNA_CnbByteWriterHandle* const outWriter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWriter == nullptr) {
            return InvalidArgument("The CNB byte-writer output handle is null.");
        }
        *outWriter = CNA_INVALID_HANDLE;
        std::span<const uint8_t> span;
        if (const CNA_Result result = BorrowBytes(initial, byteCount, &span);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<CnbByteWriterResource>();
        resource->value = std::make_shared<Cnb::CnbByteWriter>(
            std::vector<uint8_t>(span.begin(), span.end()));
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::CnbByteWriter, resource, outWriter);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB byte-writer handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_byte_writer_destroy(const CNA_CnbByteWriterHandle writerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbByteWriterResource> writer;
        if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(writerHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB byte-writer handle could not be destroyed.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

/* The seven fixed-width writes are one shape repeated, exactly as the seven reads are. */
#define CNA_CNB_DEFINE_WRITE(routeName, cType, canonicalCall)                                  \
    CNA_Result routeName(const CNA_CnbByteWriterHandle writerHandle, const cType value)        \
    {                                                                                          \
        return CallWithExceptionBarrier([&]() -> CNA_Result {                                  \
            std::shared_ptr<CnbByteWriterResource> writer;                                     \
            if (const CNA_Result result = GetByteWriter(writerHandle, &writer);                \
                result != CNA_RESULT_SUCCESS) {                                                \
                return result;                                                                 \
            }                                                                                  \
            writer->value->canonicalCall(value);                                               \
            return CNA_RESULT_SUCCESS;                                                         \
        });                                                                                    \
    }

CNA_CNB_DEFINE_WRITE(cna_cnb_byte_writer_write_u8, uint8_t, WriteU8)
CNA_CNB_DEFINE_WRITE(cna_cnb_byte_writer_write_u16, uint16_t, WriteU16)
CNA_CNB_DEFINE_WRITE(cna_cnb_byte_writer_write_u32, uint32_t, WriteU32)
CNA_CNB_DEFINE_WRITE(cna_cnb_byte_writer_write_u64, uint64_t, WriteU64)
CNA_CNB_DEFINE_WRITE(cna_cnb_byte_writer_write_i32, int32_t, WriteI32)
CNA_CNB_DEFINE_WRITE(cna_cnb_byte_writer_write_f32, float, WriteF32)
CNA_CNB_DEFINE_WRITE(cna_cnb_byte_writer_write_f64, double, WriteF64)

#undef CNA_CNB_DEFINE_WRITE

CNA_Result cna_cnb_byte_writer_write_string(
    const CNA_CnbByteWriterHandle writerHandle,
    const CNA_StringView value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbByteWriterResource> writer;
        if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string text;
        if (const CNA_Result result = BorrowText(value, &text);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        writer->value->WriteString(text);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_byte_writer_write_bytes(
    const CNA_CnbByteWriterHandle writerHandle,
    const uint8_t* const bytes,
    const uint64_t byteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_byte_writer_write_zeros(
    const CNA_CnbByteWriterHandle writerHandle,
    const uint64_t byteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbByteWriterResource> writer;
        if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (byteCount > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE,
                "The zero count does not fit in this host's size type.");
        }
        writer->value->WriteZeros(static_cast<std::size_t>(byteCount));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_byte_writer_get_size(
    const CNA_CnbByteWriterHandle writerHandle,
    uint64_t* const outSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbByteWriterResource> writer;
        if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outSize == nullptr) {
            return InvalidArgument("The written-size output is null.");
        }
        *outSize = static_cast<uint64_t>(writer->value->Size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_byte_writer_copy_bytes(
    const CNA_CnbByteWriterHandle writerHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbByteWriterResource> writer;
        if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The written-bytes output buffer is invalid.");
        }
        const std::span<const uint8_t> view = writer->value->View();
        *outByteCount = static_cast<uint64_t>(view.size());
        if (capacity < static_cast<uint64_t>(view.size())) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the written bytes.");
        }
        if (!view.empty()) {
            std::memcpy(destination, view.data(), view.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_byte_writer_take(
    const CNA_CnbByteWriterHandle writerHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbByteWriterResource> writer;
        if (const CNA_Result result = GetByteWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The written-bytes output buffer is invalid.");
        }
        *outByteCount = static_cast<uint64_t>(writer->value->Size());
        // Taking is destructive, so the capacity is settled first: a refused take leaves the bytes
        // where they were and can simply be repeated with a larger buffer.
        if (capacity < *outByteCount) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the written bytes.");
        }
        const std::vector<uint8_t> taken = writer->value->Take();
        if (!taken.empty()) {
            std::memcpy(destination, taken.data(), taken.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

/* --- CBIND-107: the container writer ---------------------------------------------------------- */

CNA_Result cna_cnb_writer_create(
    const uint32_t assetTypeId,
    const uint32_t assetSchemaVersion,
    CNA_CnbWriterHandle* const outWriter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWriter == nullptr) {
            return InvalidArgument("The CNB writer output handle is null.");
        }
        *outWriter = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<CnbWriterResource>();
        resource->value = std::make_shared<Cnb::CnbWriter>(assetTypeId, assetSchemaVersion);
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::CnbWriter, resource, outWriter);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB writer handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_writer_destroy(const CNA_CnbWriterHandle writerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(writerHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB writer handle could not be destroyed.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_writer_set_metadata(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_StringView assetTypeName,
    const CNA_StringView contentName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string typeName;
        std::string content;
        if (const CNA_Result result = BorrowText(assetTypeName, &typeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowText(contentName, &content);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        writer->value->SetMetadata(std::move(typeName), std::move(content));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_writer_add_external_reference(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_CnbExternalReference* const reference,
    const CNA_StringView logicalName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (reference == nullptr) {
            return InvalidArgument("The external-reference input is null.");
        }
        if (reference->struct_size < static_cast<uint32_t>(sizeof(CNA_CnbExternalReference)) ||
            reference->struct_version != CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION) {
            return InvalidArgument(
                "The external-reference structure is not a known size and version.");
        }
        std::string name;
        if (const CNA_Result result = BorrowText(logicalName, &name);
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
    });
}

CNA_Result cna_cnb_writer_clear_external_references(const CNA_CnbWriterHandle writerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        writer->externalReferences.clear();
        writer->value->SetExternalReferences({});
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_writer_add_chunk(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_CnbChunkId type,
    const uint8_t* const data,
    const uint64_t byteCount,
    const uint32_t flags,
    const uint32_t alignment)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_writer_get_schema_chunk_count(
    const CNA_CnbWriterHandle writerHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr) {
            return InvalidArgument("The schema-chunk count output is null.");
        }
        *outCount = static_cast<uint64_t>(writer->value->SchemaChunkCount());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_writer_set_compression(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_CnbCompression codec,
    const int32_t level)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_writer_set_limits(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_CnbReadLimits* const limits)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_writer_get_limits(
    const CNA_CnbWriterHandle writerHandle,
    CNA_CnbReadLimits* const outLimits)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outLimits == nullptr) {
            return InvalidArgument("The read-limits output is null.");
        }
        if (outLimits->struct_size < static_cast<uint32_t>(sizeof(CNA_CnbReadLimits)) ||
            outLimits->struct_version != CNA_CNB_READ_LIMITS_STRUCT_VERSION) {
            return InvalidArgument("The read-limits structure is not a known size and version.");
        }
        WriteLimits(writer->value->Limits(), outLimits);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_writer_build(
    const CNA_CnbWriterHandle writerHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The built-image output buffer is invalid.");
        }
        const std::vector<uint8_t> image = writer->value->Build();
        return CopyBytes(image, destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_writer_write_to_file(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_StringView path)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string pathText;
        if (const CNA_Result result = BorrowText(path, &pathText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (pathText.empty()) {
            return InvalidArgument("The CNB file path must not be empty.");
        }
        writer->value->WriteToFile(pathText);
        return CNA_RESULT_SUCCESS;
    });
}

/* --- CBIND-108: texture pixel formats ---------------------------------------------------------- */

CNA_Result cna_cnb_is_known_texture_format(const uint32_t value, CNA_Bool* const outKnown)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outKnown == nullptr) {
            return InvalidArgument("The format-known output is null.");
        }
        *outKnown = Cnb::IsKnownCnbTextureFormat(value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_get_texture_format_name_size(
    const CNA_CnbTextureFormat format,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportSize(Cnb::CnbTextureFormatToString(ToTextureFormat(format)), outByteCount);
    });
}

CNA_Result cna_cnb_copy_texture_format_name(
    const CNA_CnbTextureFormat format,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyText(
            Cnb::CnbTextureFormatToString(ToTextureFormat(format)),
            destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_is_block_compressed_texture_format(
    const CNA_CnbTextureFormat format,
    CNA_Bool* const outBlockCompressed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBlockCompressed == nullptr) {
            return InvalidArgument("The block-compression output is null.");
        }
        *outBlockCompressed =
            Cnb::IsBlockCompressedCnbTextureFormat(ToTextureFormat(format)) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_get_texture_format_unit_bytes(
    const CNA_CnbTextureFormat format,
    uint32_t* const outUnitBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outUnitBytes == nullptr) {
            return InvalidArgument("The unit-size output is null.");
        }
        *outUnitBytes = Cnb::CnbTextureFormatUnitBytes(ToTextureFormat(format));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_get_texture_level_byte_size(
    const CNA_CnbTextureFormat format,
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth,
    uint64_t* const outByteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteSize == nullptr) {
            return InvalidArgument("The level byte-size output is null.");
        }
        *outByteSize = Cnb::CnbTextureLevelByteSize(ToTextureFormat(format), width, height, depth);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_texture_format_to_surface_format(
    const CNA_CnbTextureFormat format,
    CNA_SurfaceFormat* const outSurfaceFormat)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSurfaceFormat == nullptr) {
            return InvalidArgument("The surface-format output is null.");
        }
        *outSurfaceFormat = static_cast<CNA_SurfaceFormat>(
            Cnb::CnbTextureFormatToSurfaceFormat(ToTextureFormat(format)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_texture_format_from_surface_format(
    const CNA_SurfaceFormat surfaceFormat,
    CNA_CnbTextureFormat* const outFormat)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFormat == nullptr) {
            return InvalidArgument("The CNB texture-format output is null.");
        }
        *outFormat = static_cast<CNA_CnbTextureFormat>(
            Cnb::SurfaceFormatToCnbTextureFormat(
                static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(surfaceFormat)));
        return CNA_RESULT_SUCCESS;
    });
}

/* --- CBIND-108: the texture schemas ------------------------------------------------------------ */

CNA_Result cna_cnb_texture_data_create(
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth,
    const uint32_t faceCount,
    const uint32_t mipCount,
    CNA_CnbTextureDataHandle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The CNB texture output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
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
    });
}

CNA_Result cna_cnb_texture_data_create_rgba8(
    const uint32_t width,
    const uint32_t height,
    const uint8_t* const rgba,
    const uint64_t byteCount,
    CNA_CnbTextureDataHandle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The CNB texture output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        std::span<const uint8_t> span;
        if (const CNA_Result result = BorrowBytes(rgba, byteCount, &span);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateTextureData(
            Cnb::MakeRgba8Texture2DData(
                width, height, std::vector<uint8_t>(span.begin(), span.end())),
            outTexture);
    });
}

CNA_Result cna_cnb_texture_data_destroy(const CNA_CnbTextureDataHandle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(textureHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB texture handle could not be destroyed.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_texture_data_get_info(
    const CNA_CnbTextureDataHandle textureHandle,
    CNA_CnbTextureInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outInfo == nullptr) {
            return InvalidArgument("The texture-info output is null.");
        }
        if (outInfo->struct_size < static_cast<uint32_t>(sizeof(CNA_CnbTextureInfo)) ||
            outInfo->struct_version != CNA_CNB_TEXTURE_INFO_STRUCT_VERSION) {
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
    });
}

CNA_Result cna_cnb_texture_data_get_level_dimensions(
    const CNA_CnbTextureDataHandle textureHandle,
    const uint32_t level,
    uint32_t* const outWidth,
    uint32_t* const outHeight,
    uint32_t* const outDepth)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outWidth == nullptr || outHeight == nullptr || outDepth == nullptr) {
            return InvalidArgument("A level-dimension output is null.");
        }
        Cnb::CnbTextureLevelDimensions(*texture->value, level, *outWidth, *outHeight, *outDepth);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_texture_data_add_representation(
    const CNA_CnbTextureDataHandle textureHandle,
    const CNA_CnbTextureFormat format,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outIndex == nullptr) {
            return InvalidArgument("The representation-index output is null.");
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
    });
}

CNA_Result cna_cnb_texture_data_get_representation_count(
    const CNA_CnbTextureDataHandle textureHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr) {
            return InvalidArgument("The representation-count output is null.");
        }
        *outCount = static_cast<uint64_t>(texture->value->representations.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_texture_data_get_representation_format(
    const CNA_CnbTextureDataHandle textureHandle,
    const uint64_t representation,
    CNA_CnbTextureFormat* const outFormat)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outFormat == nullptr) {
            return InvalidArgument("The representation-format output is null.");
        }
        if (const CNA_Result result = RequireIndex(
                representation, texture->value->representations.size(),
                "The representation index is outside this texture's list.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outFormat = static_cast<CNA_CnbTextureFormat>(
            texture->value->representations[static_cast<std::size_t>(representation)].format);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_texture_data_get_level_count(
    const CNA_CnbTextureDataHandle textureHandle,
    const uint64_t representation,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr) {
            return InvalidArgument("The level-count output is null.");
        }
        if (const CNA_Result result = RequireIndex(
                representation, texture->value->representations.size(),
                "The representation index is outside this texture's list.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(
            texture->value->representations[static_cast<std::size_t>(representation)].levels.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_texture_data_set_level(
    const CNA_CnbTextureDataHandle textureHandle,
    const uint64_t representation,
    const uint64_t level,
    const uint8_t* const bytes,
    const uint64_t byteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_texture_data_copy_level(
    const CNA_CnbTextureDataHandle textureHandle,
    const uint64_t representation,
    const uint64_t level,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
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
    });
}

CNA_Result cna_cnb_texture_data_select_representation(
    const CNA_CnbTextureDataHandle textureHandle,
    const CNA_CnbTextureFormatSupportedFn supported,
    void* const context,
    CNA_Bool* const outFound,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (supported == nullptr || outFound == nullptr || outIndex == nullptr) {
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
        *outFound = chosen < data.representations.size() ? CNA_TRUE : CNA_FALSE;
        if (*outFound == CNA_TRUE) {
            *outIndex = static_cast<uint64_t>(chosen);
        }
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

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
    if (const CNA_Result result = BorrowText(contentName, &name);
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

} // namespace

CNA_Result cna_cnb_encode_texture2d(
    const CNA_CnbTextureDataHandle texture,
    const CNA_StringView contentName,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return EncodeTexture(
            texture, contentName, &Cnb::EncodeTexture2DToCnb, destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_encode_texture_cube(
    const CNA_CnbTextureDataHandle texture,
    const CNA_StringView contentName,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return EncodeTexture(
            texture, contentName, &Cnb::EncodeTextureCubeToCnb, destination, capacity,
            outByteCount);
    });
}

CNA_Result cna_cnb_encode_texture3d(
    const CNA_CnbTextureDataHandle texture,
    const CNA_StringView contentName,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return EncodeTexture(
            texture, contentName, &Cnb::EncodeTexture3DToCnb, destination, capacity, outByteCount);
    });
}

CNA_Result cna_cnb_decode_texture2d(
    const CNA_CnbDocumentHandle document,
    CNA_CnbTextureDataHandle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return DecodeTexture(document, &Cnb::DecodeTexture2DFromCnb, outTexture);
    });
}

CNA_Result cna_cnb_decode_texture_cube(
    const CNA_CnbDocumentHandle document,
    CNA_CnbTextureDataHandle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return DecodeTexture(document, &Cnb::DecodeTextureCubeFromCnb, outTexture);
    });
}

CNA_Result cna_cnb_decode_texture3d(
    const CNA_CnbDocumentHandle document,
    CNA_CnbTextureDataHandle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return DecodeTexture(document, &Cnb::DecodeTexture3DFromCnb, outTexture);
    });
}

CNA_Result cna_cnb_writer_append_embedded_texture2d(
    const CNA_CnbWriterHandle writerHandle,
    const CNA_CnbTextureDataHandle textureHandle,
    const CNA_StringView label)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CnbTextureDataResource> texture;
        if (const CNA_Result result = GetTextureData(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string labelText;
        if (const CNA_Result result = BorrowText(label, &labelText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Cnb::AppendEmbeddedTexture2DChunks(*writer->value, *texture->value, labelText.c_str());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_document_read_embedded_texture2d(
    const CNA_CnbDocumentHandle documentHandle,
    const CNA_StringView label,
    CNA_CnbTextureDataHandle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The CNB texture output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string labelText;
        if (const CNA_Result result = BorrowText(label, &labelText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateTextureData(
            Cnb::ReadEmbeddedTexture2DChunks(*document->value, labelText.c_str()), outTexture);
    });
}

/* --- CBIND-109: the model schema ---------------------------------------------------------------- */

CNA_Result cna_cnb_model_create(CNA_CnbModelDataHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outModel == nullptr) {
            return InvalidArgument("The CNB model output handle is null.");
        }
        *outModel = CNA_INVALID_HANDLE;
        return CreateModel(Cnb::CnbModelData{}, outModel);
    });
}

CNA_Result cna_cnb_model_destroy(const CNA_CnbModelDataHandle modelHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(modelHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB model handle could not be destroyed.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_get_info(
    const CNA_CnbModelDataHandle modelHandle,
    CNA_CnbModelInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        outInfo->has_skeleton = data.skeleton.has_value() ? CNA_TRUE : CNA_FALSE;
        outInfo->applies_gltf_lighting_policy =
            data.appliesGltfLightingPolicy ? CNA_TRUE : CNA_FALSE;
        outInfo->has_bone_hierarchy = data.hasBoneHierarchy ? CNA_TRUE : CNA_FALSE;
        outInfo->reserved = 0U;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_set_flags(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_Bool appliesGltfLightingPolicy,
    const CNA_Bool hasBoneHierarchy)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        // CBIND-067's discipline: a non-canonical CNA_Bool is refused **before** the handle is
        // resolved, so the answer is the same whatever handle came with it. Validating it after
        // the lookup is what CApiBoolContractSmoke catches -- with an invalid handle the route
        // answers a handle error, which reads as "accepted the byte".
        if (const CNA_Result result = ValidateCanonicalBool(
                appliesGltfLightingPolicy, "applies_gltf_lighting_policy");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ValidateCanonicalBool(hasBoneHierarchy, "has_bone_hierarchy");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->value->appliesGltfLightingPolicy = appliesGltfLightingPolicy != CNA_FALSE;
        model->value->hasBoneHierarchy = hasBoneHierarchy != CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_add_bone(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_StringView name,
    const int32_t parent,
    const float* const transform,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (transform == nullptr) {
            return InvalidArgument("The bone transform is null.");
        }
        std::string nameText;
        if (const CNA_Result result = BorrowText(name, &nameText);
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
    });
}

CNA_Result cna_cnb_model_get_bone(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    CNA_CnbModelBone* const outBone)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_get_bone_name_size(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                RequireIndex(index, model->value->bones.size(), "The bone index is out of range.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(model->value->bones[static_cast<std::size_t>(index)].name, outBytes);
    });
}

CNA_Result cna_cnb_model_copy_bone_name(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                RequireIndex(index, model->value->bones.size(), "The bone index is out of range.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            model->value->bones[static_cast<std::size_t>(index)].name,
            destination, capacity, outBytes);
    });
}


CNA_Result cna_cnb_model_add_part(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_CnbModelPartInfo* const info,
    const CNA_StringView name,
    const CNA_StringView externalEffect,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        if (const CNA_Result result = BorrowText(name, &nameText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string effectText;
        if (const CNA_Result result = BorrowText(externalEffect, &effectText);
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
    });
}

CNA_Result cna_cnb_model_get_part(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    CNA_CnbModelPartInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_set_part(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const CNA_CnbModelPartInfo* const info)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_get_part_name_size(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return ReportSize(part->name, outBytes);
    });
}

CNA_Result cna_cnb_model_copy_part_name(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return CopyText(part->name, destination, capacity, outBytes);
    });
}

CNA_Result cna_cnb_model_get_part_external_effect_size(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return ReportSize(part->externalEffect, outBytes);
    });
}

CNA_Result cna_cnb_model_copy_part_external_effect(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return CopyText(part->externalEffect, destination, capacity, outBytes);
    });
}

CNA_Result cna_cnb_model_set_part_vertex_bytes(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const uint8_t* const bytes,
    const uint64_t byteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_copy_part_vertex_bytes(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The vertex byte output is invalid.");
        }
        Cnb::CnbModelPart* part = nullptr;
        if (const CNA_Result result = GetPart(model, index, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyBytes(part->vertexBytes, destination, capacity, outBytes);
    });
}

CNA_Result cna_cnb_model_set_part_index_bytes(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const uint8_t* const bytes,
    const uint64_t byteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_copy_part_index_bytes(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The index byte output is invalid.");
        }
        Cnb::CnbModelPart* part = nullptr;
        if (const CNA_Result result = GetPart(model, index, &part);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyBytes(part->indexBytes, destination, capacity, outBytes);
    });
}

CNA_Result cna_cnb_model_get_material(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    CNA_CnbMaterialInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        outInfo->double_sided = material.doubleSided ? CNA_TRUE : CNA_FALSE;
        outInfo->reserved[0] = 0U;
        outInfo->reserved[1] = 0U;
        outInfo->reserved[2] = 0U;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_set_material(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMaterialInfo* const info)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_get_material_texture_size(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMaterialTextureSlot slot,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return ReportSize(*name, outBytes);
    });
}

CNA_Result cna_cnb_model_copy_material_texture(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMaterialTextureSlot slot,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_set_material_texture(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMaterialTextureSlot slot,
    const CNA_StringView assetName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        if (const CNA_Result result = BorrowText(assetName, &assetText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *name = std::move(assetText);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_get_material_texture_coordinate_set(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    uint8_t* const outSet)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outSet == nullptr) {
            return InvalidArgument("The coordinate-set output is null.");
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
        *outSet = target->material.textureCoordinateSets[static_cast<std::size_t>(slot)];
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_set_material_texture_coordinate_set(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    const uint8_t coordinateSet)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        target->material.textureCoordinateSets[static_cast<std::size_t>(slot)] = coordinateSet;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_get_material_texture_transform(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    CNA_CnbTextureTransform* const outTransform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outTransform == nullptr) {
            return InvalidArgument("The texture-transform output is null.");
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
    });
}

CNA_Result cna_cnb_model_set_material_texture_transform(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    const CNA_CnbTextureTransform* const transform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_get_material_sampler(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    CNA_CnbSamplerState* const outSampler)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outSampler == nullptr) {
            return InvalidArgument("The sampler output is null.");
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
        outSampler->declared = sampler.declared ? CNA_TRUE : CNA_FALSE;
        outSampler->reserved[0] = 0U;
        outSampler->reserved[1] = 0U;
        outSampler->reserved[2] = 0U;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_set_material_sampler(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t slot,
    const CNA_CnbSamplerState* const sampler)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_has_morph(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    CNA_Bool* const outPresent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outPresent == nullptr) {
            return InvalidArgument("The morph-presence output is null.");
        }
        Cnb::CnbModelPart* target = nullptr;
        if (const CNA_Result result = GetPart(model, part, &target);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPresent = target->morph.has_value() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_get_morph(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    CNA_CnbMorphInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        outInfo->recompute_flat_normals = morph->recomputeFlatNormals ? CNA_TRUE : CNA_FALSE;
        outInfo->weight_track_step_interpolation =
            morph->weightTrackStepInterpolation ? CNA_TRUE : CNA_FALSE;
        outInfo->weight_track_cubic_spline = morph->weightTrackCubicSpline ? CNA_TRUE : CNA_FALSE;
        std::memset(outInfo->reserved2, 0, sizeof(outInfo->reserved2));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_set_morph(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const CNA_CnbMorphInfo* const info)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_clear_morph(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        target->morph.reset();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_add_morph_target(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_set_morph_target_deltas(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t target,
    const CNA_CnbMorphDeltaStream stream,
    const float* const values,
    const uint64_t valueCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_copy_morph_target_deltas(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t target,
    const CNA_CnbMorphDeltaStream stream,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return CopyFloats(*source, destination, capacity, outCount);
    });
}

CNA_Result cna_cnb_model_set_morph_weights(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const float* const values,
    const uint64_t valueCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_copy_morph_weights(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return CopyFloats(morph->weights, destination, capacity, outCount);
    });
}

CNA_Result cna_cnb_model_add_morph_weight_key(
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
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_get_morph_weight_key(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t key,
    CNA_CnbMorphWeightKeyInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_copy_morph_weight_key_values(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t part,
    const uint64_t key,
    const CNA_CnbMorphKeyStream stream,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return CopyFloats(*source, destination, capacity, outCount);
    });
}

CNA_Result cna_cnb_model_add_mesh(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_StringView name,
    const int32_t parentBone,
    const uint32_t* const partIndices,
    const uint64_t partIndexCount,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nameText;
        if (const CNA_Result result = BorrowText(name, &nameText);
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
    });
}

CNA_Result cna_cnb_model_get_mesh(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    CNA_CnbMeshInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_get_mesh_name_size(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                RequireIndex(index, model->value->meshes.size(), "The mesh index is out of range.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(model->value->meshes[static_cast<std::size_t>(index)].name, outBytes);
    });
}

CNA_Result cna_cnb_model_copy_mesh_name(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                RequireIndex(index, model->value->meshes.size(), "The mesh index is out of range.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            model->value->meshes[static_cast<std::size_t>(index)].name,
            destination, capacity, outBytes);
    });
}

CNA_Result cna_cnb_model_copy_mesh_part_indices(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    uint32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
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
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the part-index count.");
        }
        if (!indices.empty()) {
            std::memcpy(destination, indices.data(), indices.size() * sizeof(uint32_t));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_set_skeleton(
    const CNA_CnbModelDataHandle modelHandle,
    const int32_t* const hierarchy,
    const uint64_t jointCount,
    const float* const bindPose,
    const float* const inverseBindPose,
    const float* const rootPrefix)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
            return InvalidArgument("The skeleton pose arrays are null with a non-zero joint count.");
        }
        uint64_t floatCount = 0U;
        if (const CNA_Result result = cna_cnb_checked_multiply(jointCount, 16U, &floatCount);
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
    });
}

CNA_Result cna_cnb_model_clear_skeleton(const CNA_CnbModelDataHandle modelHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        model->value->skeleton.reset();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_get_skeleton(
    const CNA_CnbModelDataHandle modelHandle,
    CNA_CnbSkeletonInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_copy_skeleton_hierarchy(
    const CNA_CnbModelDataHandle modelHandle,
    int32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The hierarchy output is invalid.");
        }
        if (!model->value->skeleton.has_value()) {
            return InvalidArgument("The model carries no skinning skeleton.");
        }
        const std::vector<int32_t>& hierarchy = model->value->skeleton.value().hierarchy;
        *outCount = static_cast<uint64_t>(hierarchy.size());
        if (capacity < static_cast<uint64_t>(hierarchy.size())) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the joint count.");
        }
        if (!hierarchy.empty()) {
            std::memcpy(destination, hierarchy.data(), hierarchy.size() * sizeof(int32_t));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_copy_skeleton_matrices(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_CnbSkeletonMatrixSet set,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
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
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the matrix float count.");
        }
        for (std::size_t joint = 0U; joint < source->size(); ++joint) {
            std::memcpy(
                destination + (joint * 16U), (*source)[joint].data(), 16U * sizeof(float));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_add_animation(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_StringView name,
    const CNA_AnimationClipEXTDescriptor* const clip,
    const CNA_ClipTargetSpaceEXT targetSpace,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        if (const CNA_Result result = BorrowText(name, &nameText);
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
    });
}

CNA_Result cna_cnb_model_get_animation_name_size(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return ReportSize(model->value->animations[static_cast<std::size_t>(index)].name, outBytes);
    });
}

CNA_Result cna_cnb_model_copy_animation_name(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
        return CopyText(
            model->value->animations[static_cast<std::size_t>(index)].name,
            destination, capacity, outBytes);
    });
}

CNA_Result cna_cnb_model_get_animation(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    double* const outDurationSeconds,
    uint64_t* const outTrackCount,
    CNA_ClipTargetSpaceEXT* const outTargetSpace)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_get_animation_track(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const uint64_t track,
    int32_t* const outBoneIndex,
    uint64_t* const outKeyframeCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_copy_animation_keyframes(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    const uint64_t track,
    CNA_KeyframeEXT* const destination,
    const uint64_t capacity,
    uint64_t* const outKeyframeCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outKeyframeCount == nullptr || (destination == nullptr && capacity != 0U)) {
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
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the keyframe count.");
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
    });
}

CNA_Result cna_cnb_model_add_light(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_CnbModelLight* const light,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_cnb_model_get_light(
    const CNA_CnbModelDataHandle modelHandle,
    const uint64_t index,
    CNA_CnbModelLight* const outLight)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outLight == nullptr) {
            return InvalidArgument("The light output is null.");
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
    });
}

CNA_Result cna_cnb_encode_model(
    const CNA_CnbModelDataHandle modelHandle,
    const CNA_StringView contentName,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelResource> model;
        if (const CNA_Result result = GetModel(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The encoded model output is invalid.");
        }
        std::string nameText;
        if (const CNA_Result result = BorrowText(contentName, &nameText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<uint8_t> encoded = Cnb::EncodeModelToCnb(*model->value, nameText);
        return CopyBytes(encoded, destination, capacity, outBytes);
    });
}

CNA_Result cna_cnb_decode_model(
    const CNA_CnbDocumentHandle documentHandle,
    CNA_CnbModelDataHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbDocumentResource> document;
        if (const CNA_Result result = GetDocument(documentHandle, &document);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outModel == nullptr) {
            return InvalidArgument("The CNB model output handle is null.");
        }
        *outModel = CNA_INVALID_HANDLE;
        return CreateModel(Cnb::DecodeModelFromCnb(*document->value), outModel);
    });
}

CNA_Result cna_cnb_build_model_from_cnj(
    const CNA_StringView cnjPath,
    const CNA_StringView contentRoot,
    CNA_CnbModelFromCnjHandle* const outResult)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outResult == nullptr) {
            return InvalidArgument("The CNB model compile output handle is null.");
        }
        *outResult = CNA_INVALID_HANDLE;
        std::string pathText;
        if (const CNA_Result result = BorrowText(cnjPath, &pathText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string rootText;
        if (const CNA_Result result = BorrowText(contentRoot, &rootText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<CnbModelFromCnjResource>();
        resource->value = std::make_shared<Cnb::CnbModelFromCnjResult>(
            Cnb::BuildCnbModelFromCnj(pathText, rootText));
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::CnbModelFromCnj, resource, outResult);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB model compile handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_from_cnj_destroy(const CNA_CnbModelFromCnjHandle resultHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelFromCnjResource> compiled;
        if (const CNA_Result result = GetCompileResult(resultHandle, &compiled);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(resultHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result, ErrorCategoryForResult(result),
                "The CNB model compile handle could not be destroyed.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_from_cnj_take_model(
    const CNA_CnbModelFromCnjHandle resultHandle,
    CNA_CnbModelDataHandle* const outModel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelFromCnjResource> compiled;
        if (const CNA_Result result = GetCompileResult(resultHandle, &compiled);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outModel == nullptr) {
            return InvalidArgument("The CNB model output handle is null.");
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
    });
}

CNA_Result cna_cnb_model_from_cnj_get_absorbed_file_count(
    const CNA_CnbModelFromCnjHandle resultHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelFromCnjResource> compiled;
        if (const CNA_Result result = GetCompileResult(resultHandle, &compiled);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr) {
            return InvalidArgument("The absorbed-file count output is null.");
        }
        *outCount = static_cast<uint64_t>(compiled->value->absorbedFiles.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_from_cnj_get_absorbed_file_size(
    const CNA_CnbModelFromCnjHandle resultHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelFromCnjResource> compiled;
        if (const CNA_Result result = GetCompileResult(resultHandle, &compiled);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireIndex(
                index, compiled->value->absorbedFiles.size(),
                "The absorbed-file index is out of range.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(
            compiled->value->absorbedFiles[static_cast<std::size_t>(index)], outBytes);
    });
}

CNA_Result cna_cnb_model_from_cnj_copy_absorbed_file(
    const CNA_CnbModelFromCnjHandle resultHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelFromCnjResource> compiled;
        if (const CNA_Result result = GetCompileResult(resultHandle, &compiled);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireIndex(
                index, compiled->value->absorbedFiles.size(),
                "The absorbed-file index is out of range.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            compiled->value->absorbedFiles[static_cast<std::size_t>(index)],
            destination, capacity, outBytes);
    });
}

CNA_Result cna_cnb_model_from_cnj_get_external_reference_count(
    const CNA_CnbModelFromCnjHandle resultHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelFromCnjResource> compiled;
        if (const CNA_Result result = GetCompileResult(resultHandle, &compiled);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outCount == nullptr) {
            return InvalidArgument("The external-reference count output is null.");
        }
        *outCount = static_cast<uint64_t>(compiled->value->externalReferences.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cnb_model_from_cnj_get_external_reference_size(
    const CNA_CnbModelFromCnjHandle resultHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelFromCnjResource> compiled;
        if (const CNA_Result result = GetCompileResult(resultHandle, &compiled);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireIndex(
                index, compiled->value->externalReferences.size(),
                "The external-reference index is out of range.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(
            compiled->value->externalReferences[static_cast<std::size_t>(index)], outBytes);
    });
}

CNA_Result cna_cnb_model_from_cnj_copy_external_reference(
    const CNA_CnbModelFromCnjHandle resultHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CnbModelFromCnjResource> compiled;
        if (const CNA_Result result = GetCompileResult(resultHandle, &compiled);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireIndex(
                index, compiled->value->externalReferences.size(),
                "The external-reference index is out of range.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            compiled->value->externalReferences[static_cast<std::size_t>(index)],
            destination, capacity, outBytes);
    });
}
