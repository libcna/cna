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
#include "CNA/Content/Cnb/CnbReadLimits.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"

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
