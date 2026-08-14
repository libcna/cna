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

const LastError& GetLastError() noexcept
{
    return lastError;
}

void SetLastError(
    const CNA_Result result,
    const CNA_ErrorCategory category,
    const char* const message,
    const uint64_t messageByteLength) noexcept
{
    lastError.result = result;
    lastError.category = category;
    lastError.message = message == nullptr ? "" : message;
    lastError.messageByteLength = message == nullptr ? 0U : messageByteLength;
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
