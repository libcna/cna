// SPDX-License-Identifier: MS-PL

#include "CnaCApiDetail.hpp"

#include <memory>
#include <thread>

int main()
{
    CNA::C::Detail::HandleRegistry registry;
    CNA_Handle firstHandle = CNA_INVALID_HANDLE;
    if (registry.Create(
            CNA::C::Detail::ObjectKind::Test,
            std::make_shared<int>(7),
            &firstHandle) != CNA_RESULT_SUCCESS ||
        firstHandle == CNA_INVALID_HANDLE) {
        return 1;
    }

    CNA::C::Detail::ObjectKind kind = CNA::C::Detail::ObjectKind::Unknown;
    uint64_t tag = UINT64_MAX;
    if (registry.GetKind(firstHandle, &kind) != CNA_RESULT_SUCCESS ||
        kind != CNA::C::Detail::ObjectKind::Test ||
        registry.GetUserTag(firstHandle, &tag) != CNA_RESULT_SUCCESS || tag != 0U ||
        registry.SetUserTag(firstHandle, UINT64_C(0xfedcba9876543210)) !=
            CNA_RESULT_SUCCESS ||
        registry.GetUserTag(firstHandle, &tag) != CNA_RESULT_SUCCESS ||
        tag != UINT64_C(0xfedcba9876543210)) {
        return 2;
    }

    CNA_Result offThreadResult = CNA_RESULT_SUCCESS;
    CNA_Result offThreadTagResult = CNA_RESULT_SUCCESS;
    std::thread otherThread([&registry, firstHandle, &offThreadResult, &offThreadTagResult]() {
        uint64_t otherTag = 0U;
        offThreadTagResult = registry.GetUserTag(firstHandle, &otherTag);
        offThreadResult = registry.Release(firstHandle);
    });
    otherThread.join();
    if (offThreadResult != CNA_RESULT_THREAD || offThreadTagResult != CNA_RESULT_THREAD) {
        return 3;
    }

    if (registry.Release(firstHandle) != CNA_RESULT_SUCCESS ||
        registry.Release(firstHandle) != CNA_RESULT_INVALID_HANDLE) {
        return 4;
    }

    CNA_Handle secondHandle = CNA_INVALID_HANDLE;
    if (registry.Create(
            CNA::C::Detail::ObjectKind::Test,
            std::make_shared<int>(11),
            &secondHandle) != CNA_RESULT_SUCCESS ||
        secondHandle == firstHandle ||
        registry.GetKind(firstHandle, &kind) != CNA_RESULT_INVALID_HANDLE ||
        registry.GetUserTag(firstHandle, &tag) != CNA_RESULT_INVALID_HANDLE ||
        registry.GetUserTag(secondHandle, &tag) != CNA_RESULT_SUCCESS || tag != 0U) {
        return 5;
    }

    return registry.Release(secondHandle) == CNA_RESULT_SUCCESS ? 0 : 6;
}
