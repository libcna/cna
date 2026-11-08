// SPDX-License-Identifier: MS-PL
#include "../../../src/Sdl3/Sdl3Camera.hpp"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

// The actual Sdl3Camera.cpp is compiled into this isolated executable. Only its
// native boundary is controlled; no hardware, event push or replacement session.
struct SDL_Camera { int unused; };
namespace {
SDL_Camera camera;
SDL_CameraPermissionState permission;
int permissionReads, opens, closes, acquisitions, watches, unwatches;
}
namespace CNA::Platform::Sdl3 {
bool EnsureCameraSubsystem() { return true; }
bool CameraSupported() { return true; }
}
extern "C" {
SDL_CameraSpec** SDLCALL SDL_GetCameraSupportedFormats(SDL_CameraID, int* count)
{
    *count = 0;
    return nullptr;
}
SDL_Camera* SDLCALL SDL_OpenCamera(SDL_CameraID id, const SDL_CameraSpec* spec)
{
    EXPECT_EQ(id, 4242u);
    EXPECT_EQ(spec->format, SDL_PIXELFORMAT_RGBA32);
    ++opens;
    return &camera;
}
SDL_CameraID SDLCALL SDL_GetCameraID(SDL_Camera* value)
{
    EXPECT_EQ(value, &camera);
    return 4242u;
}
SDL_CameraPermissionState SDLCALL SDL_GetCameraPermissionState(SDL_Camera* value)
{
    EXPECT_EQ(value, &camera);
    ++permissionReads;
    return permission;
}
bool SDLCALL SDL_GetCameraFormat(SDL_Camera* value, SDL_CameraSpec* spec)
{
    EXPECT_EQ(value, &camera);
    *spec = {};
    spec->format = SDL_PIXELFORMAT_RGBA32;
    spec->width = 320;
    spec->height = 240;
    return true;
}
void SDLCALL SDL_CloseCamera(SDL_Camera* value)
{
    EXPECT_EQ(value, &camera);
    ++closes;
}
SDL_Surface* SDLCALL SDL_AcquireCameraFrame(SDL_Camera*, Uint64*)
{
    ++acquisitions;
    return nullptr;
}
bool SDLCALL SDL_AddEventWatch(SDL_EventFilter, void*) { ++watches; return true; }
void SDLCALL SDL_RemoveEventWatch(SDL_EventFilter, void*) { ++unwatches; }
}

class Sdl3CameraPermissionTest : public testing::TestWithParam<bool> {
protected:
    void SetUp() override
    {
        permission = SDL_CAMERA_PERMISSION_STATE_PENDING;
        permissionReads = opens = closes = acquisitions = watches = unwatches = 0;
    }
};

TEST_P(Sdl3CameraPermissionTest, CachedDenialAfterReadyEndsTheSessionWithoutAnEvent)
{
    using namespace CNA::Platform;
    // Oracle: the platform camera contract ends an approved session on denial
    // (CNA-FIX-L18; docs/cna-devices-camera-design.md, lines 40-48).
    // Literal 320x240 is the controlled native format above.
    // SDL's cached permission is changed directly; the installed watch is never called.
    Sdl3::Sdl3CameraProvider provider;
    auto session = provider.OpenCamera(4242);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->GetState(), PlatformCameraState::Opening);
    permission = SDL_CAMERA_PERMISSION_STATE_APPROVED;
    ASSERT_EQ(session->GetState(), PlatformCameraState::Ready);
    EXPECT_EQ(session->GetFrameWidth(), 320);
    EXPECT_EQ(session->GetFrameHeight(), 240);
    ASSERT_EQ(session->GetState(), PlatformCameraState::Ready);
    const int readsBeforeDenial = permissionReads;

    permission = SDL_CAMERA_PERMISSION_STATE_DENIED;
    PlatformCameraFrame frame;
    if (GetParam())
        EXPECT_FALSE(session->TryAcquireFrame(frame));
    else
        EXPECT_EQ(session->GetState(), PlatformCameraState::Denied);
    EXPECT_GT(permissionReads, readsBeforeDenial);
    EXPECT_EQ(session->GetState(), PlatformCameraState::Denied);
    EXPECT_FALSE(session->TryAcquireFrame(frame));
    EXPECT_EQ(acquisitions, 0) << "denial must prevent even an attempted frame acquisition";
    permission = SDL_CAMERA_PERMISSION_STATE_APPROVED;
    EXPECT_EQ(session->GetState(), PlatformCameraState::Denied) << "a terminal session stays ended";
    EXPECT_EQ(opens, 1);
    session.reset();
    EXPECT_EQ(closes, 1) << "the wrapper still owns and closes the native handle exactly once";
    EXPECT_EQ(watches, 1);
    EXPECT_EQ(unwatches, 1);
}
INSTANTIATE_TEST_SUITE_P(PollAndAcquire, Sdl3CameraPermissionTest, testing::Bool());
