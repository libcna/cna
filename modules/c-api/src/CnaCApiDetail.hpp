// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_DETAIL_HPP
#define CNA_C_API_DETAIL_HPP

#include "CNA/C/core.h"

#include "System/ArgumentException.hpp"
#include "System/NotImplementedException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdint>
#include <cstddef>
#include <exception>
#include <ios>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
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
    ContentManager = 7,
    SoundEffect = 8,
    SoundEffectInstance = 9,
    RenderTarget2D = 10,
    RenderTargetCube = 11,
    SpriteFont = 12,
    CurveKeyCollection = 13,
    Curve = 14,
    Test = UINT32_MAX
};

struct LastError final {
    CNA_Result result = CNA_RESULT_SUCCESS;
    CNA_ErrorCategory category = CNA_ERROR_CATEGORY_NONE;
    std::string message;
};

[[nodiscard]] const LastError& GetLastError() noexcept;

[[nodiscard]] CNA_ErrorCategory ErrorCategoryForResult(CNA_Result result) noexcept;

void SetLastError(
    CNA_Result result,
    CNA_ErrorCategory category,
    std::string_view message) noexcept;

[[nodiscard]] CNA_Result Fail(
    CNA_Result result,
    CNA_ErrorCategory category,
    std::string_view message) noexcept;

template<typename TCallable>
[[nodiscard]] CNA_Result CallWithExceptionBarrier(TCallable&& callable) noexcept
{
    try {
        return callable();
    } catch (const std::bad_alloc&) {
        return Fail(
            CNA_RESULT_OUT_OF_MEMORY,
            CNA_ERROR_CATEGORY_MEMORY,
            "Native allocation failed.");
    } catch (const std::overflow_error& exception) {
        return Fail(CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE, exception.what());
    } catch (const std::range_error& exception) {
        return Fail(CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE, exception.what());
    } catch (const std::out_of_range& exception) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_RANGE, exception.what());
    } catch (const std::invalid_argument& exception) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, exception.what());
    } catch (const std::ios_base::failure& exception) {
        return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
    } catch (const System::ArgumentException& exception) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, exception.what());
    } catch (const System::NotImplementedException& exception) {
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
    } catch (const System::NotSupportedException& exception) {
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
    } catch (const System::InvalidOperationException& exception) {
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (const std::exception& exception) {
        return Fail(CNA_RESULT_INTERNAL, CNA_ERROR_CATEGORY_INTERNAL, exception.what());
    } catch (...) {
        return Fail(
            CNA_RESULT_INTERNAL,
            CNA_ERROR_CATEGORY_INTERNAL,
            "An unknown native failure occurred.");
    }
}

[[nodiscard]] CNA_Result ValidateStringView(
    CNA_StringView value,
    bool rejectEmbeddedNul) noexcept;

[[nodiscard]] CNA_Result CopyStringView(
    CNA_StringView value,
    bool rejectEmbeddedNul,
    std::string* outValue) noexcept;

[[nodiscard]] CNA_Result ValidateBuffer(
    const void* data,
    uint64_t count) noexcept;

[[nodiscard]] CNA_Result CheckedElementByteCount(
    const void* data,
    uint64_t elementCount,
    uint64_t elementByteSize,
    std::size_t* outByteCount) noexcept;

class HandleRegistry final {
public:
    CNA_Result Create(
        ObjectKind kind,
        std::shared_ptr<void> object,
        CNA_Handle* outHandle);

    CNA_Result GetKind(CNA_Handle handle, ObjectKind* outKind) const;

    template<typename TObject>
    CNA_Result Get(
        const CNA_Handle handle,
        const ObjectKind expectedKind,
        std::shared_ptr<TObject>* const outObject) const
    {
        if (outObject == nullptr || expectedKind == ObjectKind::Unknown) {
            return CNA_RESULT_INVALID_ARGUMENT;
        }

        std::lock_guard lock(mutex_);
        Slot* slot = nullptr;
        const CNA_Result result = FindSlotLocked(handle, &slot);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (slot->kind != expectedKind) {
            return CNA_RESULT_INVALID_HANDLE;
        }
        if (slot->creationThread != std::this_thread::get_id()) {
            return CNA_RESULT_THREAD;
        }

        *outObject = std::static_pointer_cast<TObject>(slot->object);
        return CNA_RESULT_SUCCESS;
    }

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
