// SPDX-License-Identifier: MS-PL

#include "CnaCApiDetail.hpp"

#include <limits>

namespace CNA::C::Detail {

namespace {

thread_local LastError lastError;

[[nodiscard]] uint32_t NextGeneration(const uint32_t current) noexcept
{
    return current == std::numeric_limits<uint32_t>::max() ? 1U : current + 1U;
}

[[nodiscard]] CNA_Handle MakeHandle(const uint32_t slotIndex, const uint32_t generation) noexcept
{
    return (static_cast<uint64_t>(generation) << 32U) |
        static_cast<uint64_t>(slotIndex + 1U);
}

} // namespace

CNA_ErrorCategory ErrorCategoryForResult(const CNA_Result result) noexcept
{
    switch (result) {
        case CNA_RESULT_SUCCESS:
            return CNA_ERROR_CATEGORY_NONE;
        case CNA_RESULT_INVALID_ARGUMENT:
            return CNA_ERROR_CATEGORY_ARGUMENT;
        case CNA_RESULT_INVALID_HANDLE:
            return CNA_ERROR_CATEGORY_HANDLE;
        case CNA_RESULT_INVALID_STATE:
            return CNA_ERROR_CATEGORY_STATE;
        case CNA_RESULT_OUT_OF_MEMORY:
            return CNA_ERROR_CATEGORY_MEMORY;
        case CNA_RESULT_IO:
            return CNA_ERROR_CATEGORY_IO;
        case CNA_RESULT_NOT_SUPPORTED:
            return CNA_ERROR_CATEGORY_NOT_SUPPORTED;
        case CNA_RESULT_PLATFORM:
            return CNA_ERROR_CATEGORY_PLATFORM;
        case CNA_RESULT_THREAD:
            return CNA_ERROR_CATEGORY_THREAD;
        case CNA_RESULT_CALLBACK:
            return CNA_ERROR_CATEGORY_CALLBACK;
        case CNA_RESULT_OVERFLOW:
        case CNA_RESULT_BUFFER_TOO_SMALL:
            return CNA_ERROR_CATEGORY_RANGE;
        case CNA_RESULT_ENCODING:
            return CNA_ERROR_CATEGORY_ENCODING;
        case CNA_RESULT_SHUTTING_DOWN:
            return CNA_ERROR_CATEGORY_SHUTTING_DOWN;
        default:
            return CNA_ERROR_CATEGORY_INTERNAL;
    }
}

const LastError& GetLastError() noexcept
{
    return lastError;
}

void SetLastError(
    const CNA_Result result,
    const CNA_ErrorCategory category,
    const std::string_view message) noexcept
{
    lastError.result = result;
    lastError.category = category;
    try {
        if (message.empty()) {
            lastError.message.clear();
        } else {
            lastError.message.assign(message.data(), message.size());
        }
    } catch (...) {
        lastError.message.clear();
    }
}

CNA_Result Fail(
    const CNA_Result result,
    const CNA_ErrorCategory category,
    const std::string_view message) noexcept
{
    SetLastError(result, category, message);
    return result;
}

CNA_Result ValidateStringView(
    const CNA_StringView value,
    const bool rejectEmbeddedNul) noexcept
{
    if (value.data == nullptr) {
        return value.byte_length == 0U ? CNA_RESULT_SUCCESS : CNA_RESULT_INVALID_ARGUMENT;
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data);
    for (uint64_t index = 0U; index < value.byte_length;) {
        const unsigned char first = bytes[index];
        if (first <= 0x7FU) {
            if (rejectEmbeddedNul && first == 0U) {
                return CNA_RESULT_ENCODING;
            }
            ++index;
            continue;
        }

        uint64_t continuationCount = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            continuationCount = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuationCount = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuationCount = 3U;
        } else {
            return CNA_RESULT_ENCODING;
        }

        if (continuationCount > value.byte_length - index - 1U) {
            return CNA_RESULT_ENCODING;
        }

        const unsigned char second = bytes[index + 1U];
        if ((second & 0xC0U) != 0x80U ||
            (first == 0xE0U && second < 0xA0U) ||
            (first == 0xEDU && second > 0x9FU) ||
            (first == 0xF0U && second < 0x90U) ||
            (first == 0xF4U && second > 0x8FU)) {
            return CNA_RESULT_ENCODING;
        }

        for (uint64_t continuationIndex = 2U;
             continuationIndex <= continuationCount;
             ++continuationIndex) {
            if ((bytes[index + continuationIndex] & 0xC0U) != 0x80U) {
                return CNA_RESULT_ENCODING;
            }
        }
        index += continuationCount + 1U;
    }

    return CNA_RESULT_SUCCESS;
}

CNA_Result CopyStringView(
    const CNA_StringView value,
    const bool rejectEmbeddedNul,
    std::string* const outValue) noexcept
{
    if (outValue == nullptr) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }

    const CNA_Result validationResult = ValidateStringView(value, rejectEmbeddedNul);
    if (validationResult != CNA_RESULT_SUCCESS) {
        return validationResult;
    }

    try {
        outValue->assign(value.data == nullptr ? "" : value.data, value.byte_length);
    } catch (const std::bad_alloc&) {
        return CNA_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return CNA_RESULT_INTERNAL;
    }
    return CNA_RESULT_SUCCESS;
}

CNA_Result ValidateBuffer(const void* const data, const uint64_t count) noexcept
{
    return data == nullptr && count != 0U ? CNA_RESULT_INVALID_ARGUMENT : CNA_RESULT_SUCCESS;
}

CNA_Result CheckedElementByteCount(
    const void* const data,
    const uint64_t elementCount,
    const uint64_t elementByteSize,
    std::size_t* const outByteCount) noexcept
{
    if (outByteCount == nullptr || elementByteSize == 0U) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }
    if (const CNA_Result pointerResult = ValidateBuffer(data, elementCount);
        pointerResult != CNA_RESULT_SUCCESS) {
        return pointerResult;
    }
    if (elementCount > std::numeric_limits<uint64_t>::max() / elementByteSize) {
        return CNA_RESULT_OVERFLOW;
    }

    const uint64_t byteCount = elementCount * elementByteSize;
    if (byteCount > std::numeric_limits<std::size_t>::max()) {
        return CNA_RESULT_OVERFLOW;
    }
    *outByteCount = static_cast<std::size_t>(byteCount);
    return CNA_RESULT_SUCCESS;
}

CNA_Result HandleRegistry::Create(
    const ObjectKind kind,
    std::shared_ptr<void> object,
    CNA_Handle* const outHandle)
{
    if (!object || outHandle == nullptr || kind == ObjectKind::Unknown) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard lock(mutex_);
    for (uint32_t index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        if (slot.object) {
            continue;
        }

        slot.kind = kind;
        slot.object = std::move(object);
        slot.creationThread = std::this_thread::get_id();
        *outHandle = MakeHandle(index, slot.generation);
        return CNA_RESULT_SUCCESS;
    }

    if (slots_.size() >= std::numeric_limits<uint32_t>::max()) {
        return CNA_RESULT_OVERFLOW;
    }

    slots_.push_back(Slot{
        .generation = 1,
        .kind = kind,
        .object = std::move(object),
        .creationThread = std::this_thread::get_id()
    });
    *outHandle = MakeHandle(static_cast<uint32_t>(slots_.size() - 1U), 1U);
    return CNA_RESULT_SUCCESS;
}

CNA_Result HandleRegistry::GetKind(
    const CNA_Handle handle,
    ObjectKind* const outKind) const
{
    if (outKind == nullptr) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard lock(mutex_);
    Slot* slot = nullptr;
    const CNA_Result result = FindSlotLocked(handle, &slot);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }

    *outKind = slot->kind;
    return CNA_RESULT_SUCCESS;
}

CNA_Result HandleRegistry::Release(const CNA_Handle handle)
{
    std::shared_ptr<void> releasedObject;
    {
        std::lock_guard lock(mutex_);
        Slot* slot = nullptr;
        const CNA_Result result = FindSlotLocked(handle, &slot);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (slot->creationThread != std::this_thread::get_id()) {
            return CNA_RESULT_THREAD;
        }

        releasedObject = std::move(slot->object);
        slot->kind = ObjectKind::Unknown;
        slot->creationThread = {};
        slot->generation = NextGeneration(slot->generation);
    }
    return CNA_RESULT_SUCCESS;
}

CNA_Result HandleRegistry::FindSlotLocked(
    const CNA_Handle handle,
    Slot** const outSlot) const
{
    if (handle == CNA_INVALID_HANDLE || outSlot == nullptr) {
        return CNA_RESULT_INVALID_HANDLE;
    }

    const uint32_t encodedSlot = static_cast<uint32_t>(handle);
    const uint32_t generation = static_cast<uint32_t>(handle >> 32U);
    if (encodedSlot == 0U || generation == 0U) {
        return CNA_RESULT_INVALID_HANDLE;
    }

    const uint32_t slotIndex = encodedSlot - 1U;
    if (slotIndex >= slots_.size()) {
        return CNA_RESULT_INVALID_HANDLE;
    }

    Slot& slot = slots_[slotIndex];
    if (!slot.object || slot.generation != generation) {
        return CNA_RESULT_INVALID_HANDLE;
    }

    *outSlot = &slot;
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail
