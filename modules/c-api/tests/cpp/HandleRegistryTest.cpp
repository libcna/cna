// SPDX-License-Identifier: MS-PL

#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <memory>
#include <limits>
#include <thread>
#include <cstdio>

static bool platformOwnerTeardown(bool retain, bool explicitRelease)
{
    using namespace CNA::C::Detail;
    bool cleanupSawLivePlatform = false;
    HandleRegistry registry;
    std::shared_ptr<CNA::Platform::IPlatform> platform = CNA::Platform::PlatformFactory::Create("Headless");
    const std::weak_ptr<CNA::Platform::IPlatform> observer = platform;
    RegisterPlatformOwner(*platform, platform);
    platform->AcquireSubsystem(CNA::Platform::PlatformSubsystem::Video);
    CNA_Handle parent = CNA_INVALID_HANDLE;
    CNA_Handle device = CNA_INVALID_HANDLE;
    auto owned = std::make_shared<OwnedGraphicsDevice>();
    if (retain) owned->platformOwner = RetainPlatformOwner(*platform);
    // The native device releases its captured platform's video subsystem on destruction.
    // Use its real platform with a checked weak reference to witness the old exit order safely.
    owned->view = std::shared_ptr<BorrowedGraphicsDevice>(new BorrowedGraphicsDevice{nullptr, 0U},
        [&cleanupSawLivePlatform, observer](BorrowedGraphicsDevice* view) {
            if (auto live = observer.lock()) {
                live->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video);
                cleanupSawLivePlatform = !live->IsSubsystemInitialized(CNA::Platform::PlatformSubsystem::Video);
            }
            delete view;
        });
    if (registry.Create(ObjectKind::Game, platform, &parent) != CNA_RESULT_SUCCESS ||
        registry.Create(ObjectKind::OwnedGraphicsDevice, owned, &device) != CNA_RESULT_SUCCESS)
        return false;
    platform.reset();
    owned.reset();
    if (explicitRelease && registry.Release(parent) != CNA_RESULT_SUCCESS) return false;
    registry.DestroyAll();
    return cleanupSawLivePlatform == retain && observer.expired();
}

static bool foreignThreadExitCleanup()
{
    using namespace CNA::C::Detail;
    HandleRegistry registry;
    std::weak_ptr<int> observer;
    CNA_Handle handle = CNA_INVALID_HANDLE;
    CNA_Result created = CNA_RESULT_INTERNAL;
    std::thread creator([&] {
        auto object = std::make_shared<int>(17);
        observer = object;
        created = registry.Create(ObjectKind::Test, object, &handle);
    });
    creator.join();
    if (created != CNA_RESULT_SUCCESS) return false;
    registry.DestroyAll();
    registry.DestroyAll();
    ObjectKind kind = ObjectKind::Unknown;
    // Early exit cleanup must preserve the destructor's rule: do not tear down another
    // thread's native context after that creating thread and its function table are gone.
    return !observer.expired() && registry.GetKind(handle, &kind) == CNA_RESULT_INVALID_HANDLE;
}

int main()
{
    if (!platformOwnerTeardown(false, false) || !platformOwnerTeardown(false, true) || !platformOwnerTeardown(true, false) ||
        !platformOwnerTeardown(true, true)) return 8;
    std::puts("platform lifetime: unretained exit/explicit controls expired; retained exit/explicit teardown passed");
    if (!foreignThreadExitCleanup()) return 9;
    std::puts("foreign-thread exit cleanup: object abandoned, handle invalid, repeated cleanup safe");
    using CNA::C::Detail::NextHandleGeneration;
    const uint32_t lastGeneration = std::numeric_limits<uint32_t>::max();
    if (NextHandleGeneration(1U) != 2U ||
        NextHandleGeneration(lastGeneration - 1U) != lastGeneration ||
        NextHandleGeneration(lastGeneration) != 0U || NextHandleGeneration(0U) != 0U) {
        return 7;
    }

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
