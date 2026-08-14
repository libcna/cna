// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_DETAIL_HPP
#define CNA_C_API_DETAIL_HPP

#include "CNA/C/core.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace CNA::C::Detail {

enum class ObjectKind : uint32_t {
    Unknown = 0,
    Runtime = 1,
    Game = 2,
    GraphicsDevice = 3,
    Texture2D = 4,
    SpriteBatch = 5,
    EventRegistration = 6,
    Test = UINT32_MAX
};

struct LastError final {
    CNA_Result result = CNA_RESULT_SUCCESS;
    CNA_ErrorCategory category = CNA_ERROR_CATEGORY_NONE;
    const char* message = "";
    uint64_t messageByteLength = 0;
};

[[nodiscard]] const LastError& GetLastError() noexcept;

void SetLastError(
    CNA_Result result,
    CNA_ErrorCategory category,
    const char* message,
    uint64_t messageByteLength) noexcept;

class HandleRegistry final {
public:
    CNA_Result Create(
        ObjectKind kind,
        std::shared_ptr<void> object,
        CNA_Handle* outHandle);

    CNA_Result GetKind(CNA_Handle handle, ObjectKind* outKind) const;

    CNA_Result Release(CNA_Handle handle);

private:
    struct Slot final {
        uint32_t generation = 1;
        ObjectKind kind = ObjectKind::Unknown;
        std::shared_ptr<void> object;
        std::thread::id creationThread;
    };

    [[nodiscard]] CNA_Result FindSlotLocked(CNA_Handle handle, Slot** outSlot) const;

    mutable std::mutex mutex_;
    mutable std::vector<Slot> slots_;
};

} // namespace CNA::C::Detail

#endif
