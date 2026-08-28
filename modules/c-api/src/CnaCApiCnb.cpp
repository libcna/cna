// SPDX-License-Identifier: MS-PL

#include "CNA/C/cnb.h"
#include "CnaCApiDetail.hpp"

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbChunkCompression.hpp"
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbReadLimits.hpp"

#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::Fail;
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
