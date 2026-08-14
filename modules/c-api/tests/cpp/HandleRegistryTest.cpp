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
    if (registry.GetKind(firstHandle, &kind) != CNA_RESULT_SUCCESS ||
        kind != CNA::C::Detail::ObjectKind::Test) {
        return 2;
    }

    CNA_Result offThreadResult = CNA_RESULT_SUCCESS;
    std::thread otherThread([&registry, firstHandle, &offThreadResult]() {
        offThreadResult = registry.Release(firstHandle);
    });
    otherThread.join();
    if (offThreadResult != CNA_RESULT_THREAD) {
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
        registry.GetKind(firstHandle, &kind) != CNA_RESULT_INVALID_HANDLE) {
        return 5;
    }

    return registry.Release(secondHandle) == CNA_RESULT_SUCCESS ? 0 : 6;
}
