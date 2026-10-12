// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_OUTPUT_HPP
#define CNA_C_API_OUTPUT_HPP

#include "CnaCApiDetail.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

// The output contracts the routes share: one value stored through a caller's pointer, and the
// size-then-copy protocol for text and arrays that docs/c-api/STRINGS_AND_BUFFERS.md defines.
// A route that writes outputs any other way says why where it does.

namespace CNA::C::Detail {

/**
 * @brief Computes one value inside the exception barrier and stores it through `output`.
 *
 * A null `output` is refused before anything is computed; a computation that throws leaves the
 * output unwritten.
 *
 * @param output The caller's output pointer.
 * @param nullMessage The diagnostic recorded when `output` is null.
 * @param compute Produces the value, as the type `output` points to.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT`, or what the barrier translated.
 */
template<typename TValue, typename TCompute>
[[nodiscard]] CNA_Result StoreOutput(
    TValue* const output,
    const std::string_view nullMessage,
    TCompute&& compute) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return InvalidArgument(nullMessage);
        }
        const TValue value = std::forward<TCompute>(compute)();
        *output = value;
        return CNA_RESULT_SUCCESS;
    });
}

/** @brief The `struct_version` of every version 1 ABI structure, which is all of them today. */
inline constexpr uint32_t StructureVersion = UINT32_C(1);

/**
 * @brief Whether a caller's versioned structure is there, large enough and current: non-null, its
 * `struct_size` covering this build's layout and its `struct_version` equal to `version`.
 */
template<typename TStructure>
[[nodiscard]] constexpr bool IsVersionedStructure(
    const TStructure* const structure,
    const uint32_t version = StructureVersion) noexcept
{
    return structure != nullptr && structure->struct_size >= sizeof(TStructure) &&
        structure->struct_version == version;
}

/** @brief Refuses, with `message`, a versioned structure IsVersionedStructure does not accept. */
template<typename TStructure>
[[nodiscard]] CNA_Result ValidateVersionedStructure(
    const TStructure* const structure,
    const std::string_view message,
    const uint32_t version = StructureVersion) noexcept
{
    if (!IsVersionedStructure(structure, version)) {
        return InvalidArgument(message);
    }
    return CNA_RESULT_SUCCESS;
}

/**
 * @brief The count half of the text protocol: the UTF-8 byte count, with no terminator counted.
 *
 * @param text The text whose size is reported.
 * @param outBytes Receives the byte count.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null `outBytes`.
 */
[[nodiscard]] inline CNA_Result ReportTextSize(
    const std::string_view text,
    uint64_t* const outBytes) noexcept
{
    if (outBytes == nullptr) {
        return InvalidArgument("The byte-count output is null.");
    }
    *outBytes = static_cast<uint64_t>(text.size());
    return CNA_RESULT_SUCCESS;
}

/**
 * @brief The copy half of the array protocol, for elements copied as they are.
 *
 * `outCount` always receives the full element count. A capacity short of it writes nothing and
 * fails with `CNA_RESULT_BUFFER_TOO_SMALL`; a null destination is valid only with zero capacity.
 *
 * @param values The elements to copy.
 * @param destination The caller's array, `capacity` elements long.
 * @param capacity How many elements `destination` holds.
 * @param outCount Receives the element count `values` has.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
template<typename TElement>
[[nodiscard]] CNA_Result CopyElements(
    const std::span<const TElement> values,
    TElement* const destination,
    const uint64_t capacity,
    uint64_t* const outCount) noexcept
{
    static_assert(std::is_trivially_copyable_v<TElement>);
    if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The destination or count output is invalid.");
    }
    *outCount = static_cast<uint64_t>(values.size());
    if (capacity < static_cast<uint64_t>(values.size())) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the data.");
    }
    if (!values.empty()) {
        std::memcpy(destination, values.data(), values.size_bytes());
    }
    return CNA_RESULT_SUCCESS;
}

/**
 * @brief The argument check of the copy protocols, made before anything else is looked at.
 *
 * @param destination The caller's buffer.
 * @param capacity How many elements `destination` holds.
 * @param outCount Receives the element count.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null `outCount` or a null
 *         `destination` with a non-zero `capacity`.
 */
[[nodiscard]] inline CNA_Result ValidateCopyOutput(
    const void* const destination,
    const uint64_t capacity,
    const uint64_t* const outCount) noexcept
{
    if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The destination or count output is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

/**
 * @brief The copy half of the text protocol: UTF-8 bytes, no terminator, no partial write.
 *
 * `outBytes` always receives the full byte count. A capacity short of it writes nothing and fails
 * with `CNA_RESULT_BUFFER_TOO_SMALL`; a null destination is valid only with zero capacity.
 *
 * @param text The text to copy.
 * @param destination The caller's buffer, `capacity` bytes long.
 * @param capacity How many bytes `destination` holds.
 * @param outBytes Receives the byte count `text` has.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
[[nodiscard]] inline CNA_Result CopyText(
    const std::string_view text,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes) noexcept
{
    return CopyElements(
        std::span<const char>(text.data(), text.size()), destination, capacity, outBytes);
}

/** @brief CopyText for bytes rather than text. */
[[nodiscard]] inline CNA_Result CopyBytes(
    const std::span<const uint8_t> bytes,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes) noexcept
{
    return CopyElements(bytes, destination, capacity, outBytes);
}

/**
 * @brief CopyText for text formatted on demand -- a value's ToString() -- inside the barrier.
 *
 * The output arguments are checked before `format` runs.
 */
template<typename TFormat>
[[nodiscard]] CNA_Result CopyFormattedText(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    TFormat&& format) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateCopyOutput(destination, capacity, outBytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto text = std::forward<TFormat>(format)();
        return CopyText(text, destination, capacity, outBytes);
    });
}

} // namespace CNA::C::Detail

#endif
