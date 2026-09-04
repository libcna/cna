// SPDX-License-Identifier: MS-PL

#include "Sdl3Camera.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace CNA::Platform::Sdl3 {

    namespace {

        PlatformCameraPosition FromSdlPosition(const SDL_CameraPosition position)
        {
            switch (position)
            {
                case SDL_CAMERA_POSITION_FRONT_FACING:
                    return PlatformCameraPosition::FrontFacing;
                case SDL_CAMERA_POSITION_BACK_FACING:
                    return PlatformCameraPosition::BackFacing;
                case SDL_CAMERA_POSITION_UNKNOWN:
                default:
                    return PlatformCameraPosition::Unknown;
            }
        }

        SDL_CameraSpec MakeRequestedSpec(const SDL_CameraID id)
        {
            // Starting from a format the device advertises preserves its preferred dimensions and
            // frame rate. An empty list is legal (notably on web before permission), in which case
            // zero asks SDL to choose while RGBA32 still requests the conversion CNA consumes.
            int count = 0;
            SDL_CameraSpec** formats = SDL_GetCameraSupportedFormats(id, &count);
            SDL_CameraSpec requested{};
            if (formats != nullptr && count > 0 && formats[0] != nullptr)
            {
                requested = *formats[0];
            }
            SDL_free(formats);
            requested.format = SDL_PIXELFORMAT_RGBA32;
            return requested;
        }

        class Sdl3Camera final : public IPlatformCamera
        {
        public:
            explicit Sdl3Camera(SDL_Camera* camera)
                : camera_(camera)
            {
            }

            ~Sdl3Camera() override { SDL_CloseCamera(camera_); }

            [[nodiscard]] PlatformCameraState GetState() override
            {
                if (state_ == PlatformCameraState::Denied || state_ == PlatformCameraState::Ready ||
                    state_ == PlatformCameraState::Lost)
                {
                    return state_;
                }

                switch (SDL_GetCameraPermissionState(camera_))
                {
                    case SDL_CAMERA_PERMISSION_STATE_DENIED:
                        state_ = PlatformCameraState::Denied;
                        break;
                    case SDL_CAMERA_PERMISSION_STATE_APPROVED:
                        ResolveFormat();
                        break;
                    case SDL_CAMERA_PERMISSION_STATE_PENDING:
                    default:
                        state_ = PlatformCameraState::Opening;
                        break;
                }
                return state_;
            }

            [[nodiscard]] int GetFrameWidth() override
            {
                (void)GetState();
                return frameWidth_;
            }

            [[nodiscard]] int GetFrameHeight() override
            {
                (void)GetState();
                return frameHeight_;
            }

            bool TryAcquireFrame(PlatformCameraFrame& frame) override
            {
                if (GetState() != PlatformCameraState::Ready)
                {
                    return false;
                }

                std::uint64_t timestamp = 0;
                SDL_Surface* acquired = SDL_AcquireCameraFrame(camera_, &timestamp);
                if (acquired == nullptr)
                {
                    // No frame between polls is the normal steady-state answer. SDL reports
                    // physical removal separately and may continue returning blank frames.
                    return false;
                }

                struct FrameReleaser
                {
                    SDL_Camera* camera;
                    void operator()(SDL_Surface* surface) const
                    {
                        SDL_ReleaseCameraFrame(camera, surface);
                    }
                };
                const std::unique_ptr<SDL_Surface, FrameReleaser> lease(
                    acquired, FrameReleaser{camera_});

                if (acquired->format != SDL_PIXELFORMAT_RGBA32 || acquired->w <= 0 ||
                    acquired->h <= 0 || acquired->pixels == nullptr)
                {
                    return false;
                }

                const std::size_t width = static_cast<std::size_t>(acquired->w);
                const std::size_t height = static_cast<std::size_t>(acquired->h);
                if (width > std::numeric_limits<std::size_t>::max() / 4u)
                {
                    return false;
                }
                const std::size_t rowBytes = width * 4u;
                if (height > std::numeric_limits<std::size_t>::max() / rowBytes ||
                    acquired->pitch < 0 || static_cast<std::size_t>(acquired->pitch) < rowBytes)
                {
                    return false;
                }

                PlatformCameraFrame next;
                next.width = acquired->w;
                next.height = acquired->h;
                next.timestampNanoseconds = timestamp;
                next.rgbaPixels.resize(rowBytes * height);

                const auto* source = static_cast<const std::uint8_t*>(acquired->pixels);
                for (std::size_t row = 0; row < height; ++row)
                {
                    std::memcpy(next.rgbaPixels.data() + row * rowBytes,
                                source + row * static_cast<std::size_t>(acquired->pitch),
                                rowBytes);
                }

                frame = std::move(next);
                return true;
            }

        private:
            void ResolveFormat()
            {
                if (state_ == PlatformCameraState::Ready)
                {
                    return;
                }

                SDL_CameraSpec format{};
                if (!SDL_GetCameraFormat(camera_, &format))
                {
                    // Approval and format negotiation can become observable on adjacent polls.
                    // Keep waiting rather than misreporting a supported camera as permanently bad.
                    state_ = PlatformCameraState::Opening;
                    return;
                }
                if (format.format != SDL_PIXELFORMAT_RGBA32 || format.width <= 0 ||
                    format.height <= 0)
                {
                    state_ = PlatformCameraState::Lost;
                    return;
                }

                frameWidth_ = format.width;
                frameHeight_ = format.height;
                state_ = PlatformCameraState::Ready;
            }

            SDL_Camera* camera_ = nullptr;
            PlatformCameraState state_ = PlatformCameraState::Opening;
            int frameWidth_ = 0;
            int frameHeight_ = 0;
        };

    } // namespace

    namespace
    {
        /// Starts SDL's camera subsystem once, and answers whether it is up.
        ///
        /// SDL_GetCameras returns NULL with "Camera subsystem is not initialized" until
        /// SDL_INIT_CAMERA has run, and SDL_INIT_CAMERA appeared nowhere in this tree. So
        /// IsSupported answered true -- it reads the compiled-in driver table, which needs no
        /// subsystem -- while GetCameras answered an empty list, and a caller was told the
        /// platform supports cameras and handed nothing. That reads as "no camera is attached",
        /// which is the one thing it did not mean. Measured in a browser with a fake capture
        /// device present and permission granted, and it is not browser-specific: this provider
        /// serves every SDL3 platform.
        ///
        /// Bracketed the way Sdl3AudioDevice::Open brackets SDL_INIT_AUDIO, with one difference:
        /// the subsystem is not quit again. A SDL_CameraID is only meaningful inside the session
        /// that issued it, so quitting between the enumeration and the SDL_OpenCamera that follows
        /// it would hand back ids that name nothing -- the same trap the graphics adapter cache
        /// fell into with displays. PLAT-4 leaves global SDL lifetime to the host application, so
        /// nothing here calls SDL_Quit either way.
        bool EnsureCameraSubsystem()
        {
            static const bool started = []() {
                if (SDL_WasInit(SDL_INIT_CAMERA) != 0)
                {
                    return true;
                }
                return SDL_InitSubSystem(SDL_INIT_CAMERA) != false;
            }();
            return started;
        }
    } // namespace

    bool Sdl3CameraProvider::IsSupported()
    {
        // Both halves, in this order: a build with no camera driver cannot start the subsystem,
        // and a driver that cannot start is not support a caller can act on.
        return SDL_GetNumCameraDrivers() > 0 && EnsureCameraSubsystem();
    }

    std::vector<PlatformCameraInfo> Sdl3CameraProvider::GetCameras() const
    {
        if (!EnsureCameraSubsystem())
        {
            return {};
        }

        int count = 0;
        SDL_CameraID* ids = SDL_GetCameras(&count);
        if (ids == nullptr || count <= 0)
        {
            SDL_free(ids);
            return {};
        }

        std::vector<PlatformCameraInfo> cameras;
        cameras.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            PlatformCameraInfo info;
            info.id = static_cast<CameraId>(ids[i]);
            const char* name = SDL_GetCameraName(ids[i]);
            info.name = name != nullptr ? name : "";
            info.position = FromSdlPosition(SDL_GetCameraPosition(ids[i]));
            cameras.push_back(std::move(info));
        }
        SDL_free(ids);
        return cameras;
    }

    std::unique_ptr<IPlatformCamera> Sdl3CameraProvider::OpenCamera(const CameraId id)
    {
        if (!EnsureCameraSubsystem())
        {
            return nullptr;
        }

        if (id == 0 || id > std::numeric_limits<SDL_CameraID>::max())
        {
            return nullptr;
        }

        const auto sdlId = static_cast<SDL_CameraID>(id);
        const SDL_CameraSpec requested = MakeRequestedSpec(sdlId);
        SDL_Camera* camera = SDL_OpenCamera(sdlId, &requested);
        return camera != nullptr ? std::make_unique<Sdl3Camera>(camera) : nullptr;
    }

} // namespace CNA::Platform::Sdl3
