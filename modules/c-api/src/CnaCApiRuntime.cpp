// SPDX-License-Identifier: MS-PL

#include "CNA/C/runtime.h"
#include "CNA/C/graphics.h"
#include "CnaCApiDetail.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/TimeSpan.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::Fail;
using CNA::C::Detail::HandleRegistry;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateStringView;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct RuntimeState final {
    std::mutex mutex;
    HandleRegistry handles;
    bool hasActiveGame = false;
};

[[nodiscard]] RuntimeState& GetRuntimeState()
{
    static RuntimeState state;
    return state;
}

struct BorrowedGraphicsDevice final {
    GraphicsDevice* value;
};

[[nodiscard]] CNA_ErrorCategory CategoryForResult(const CNA_Result result) noexcept
{
    switch (result) {
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

[[nodiscard]] bool IsSupportedGameCallbacks(const CNA_GameCallbacks* const callbacks) noexcept
{
    return callbacks == nullptr ||
        (callbacks->struct_size >= sizeof(CNA_GameCallbacks) &&
         callbacks->struct_version == StructureVersion);
}

[[nodiscard]] bool IsSupportedGameCreateInfo(const CNA_GameCreateInfo* const createInfo) noexcept
{
    return createInfo != nullptr &&
        createInfo->struct_size >= sizeof(CNA_GameCreateInfo) &&
        createInfo->struct_version == StructureVersion;
}

[[nodiscard]] CNA_GameTime MakeCGameTime(const GameTime& value) noexcept
{
    return CNA_GameTime{
        .total_game_time_ticks = value.getTotalGameTimeProperty().getTicksProperty(),
        .elapsed_game_time_ticks = value.getElapsedGameTimeProperty().getTicksProperty(),
        .is_running_slowly = value.getIsRunningSlowlyProperty() ? CNA_TRUE : CNA_FALSE,
        .reserved = {0U, 0U, 0U, 0U, 0U, 0U, 0U}
    };
}

class CGame final : public Game {
public:
    explicit CGame(const CNA_GameCallbacks* const callbacks)
        : callbacks_{}, handle_(CNA_INVALID_HANDLE), callbackFailure_(CNA_RESULT_SUCCESS),
          isInsideCallback_(false), hasLoadedContent_(false), hasExited_(false), isShutDown_(false)
    {
        if (callbacks != nullptr) {
            callbacks_ = *callbacks;
        } else {
            callbacks_.struct_size = sizeof(CNA_GameCallbacks);
            callbacks_.struct_version = StructureVersion;
        }
    }

    void SetHandle(const CNA_Handle handle) noexcept
    {
        handle_ = handle;
    }

    [[nodiscard]] bool IsInsideCallback() const noexcept
    {
        return isInsideCallback_;
    }

    [[nodiscard]] CNA_Result GetCallbackFailure() const noexcept
    {
        return callbackFailure_;
    }

    void SetWindowTitle(const std::string& title)
    {
        getWindowProperty().setTitleProperty(title);
    }

    void Clear(const CNA_Color color)
    {
        getGraphicsDeviceProperty().Clear(
            Microsoft::Xna::Framework::Color(color.r, color.g, color.b, color.a));
    }

    [[nodiscard]] CNA_Result BorrowGraphicsDevice(CNA_Handle* const outGraphicsDevice)
    {
        if (outGraphicsDevice == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics-device output handle is null.");
        }
        *outGraphicsDevice = CNA_INVALID_HANDLE;
        if (!isInsideCallback_) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The graphics device may be borrowed only during a game lifecycle callback.");
        }
        if (borrowedGraphicsDeviceHandle_ != CNA_INVALID_HANDLE) {
            *outGraphicsDevice = borrowedGraphicsDeviceHandle_;
            return CNA_RESULT_SUCCESS;
        }

        const auto reference = std::make_shared<BorrowedGraphicsDevice>(
            BorrowedGraphicsDevice{&getGraphicsDeviceProperty()});
        const CNA_Result result = GetRuntimeState().handles.Create(
            ObjectKind::GraphicsDevice,
            reference,
            &borrowedGraphicsDeviceHandle_);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                CategoryForResult(result),
                "The callback-scoped graphics-device handle could not be created.");
        }
        *outGraphicsDevice = borrowedGraphicsDeviceHandle_;
        return CNA_RESULT_SUCCESS;
    }

    void Shutdown()
    {
        if (isShutDown_) {
            return;
        }
        isShutDown_ = true;

        NotifyExit();
        if (hasLoadedContent_) {
            Invoke(callbacks_.unload_content, nullptr);
            hasLoadedContent_ = false;
        }
        Dispose();
    }

protected:
    void LoadContent() override
    {
        hasLoadedContent_ = true;
        Invoke(callbacks_.load_content, nullptr);
    }

    void UnloadContent() override
    {
        if (!hasLoadedContent_) {
            return;
        }
        Invoke(callbacks_.unload_content, nullptr);
        hasLoadedContent_ = false;
    }

    void Update(GameTime& gameTime) override
    {
        const CNA_GameTime cGameTime = MakeCGameTime(gameTime);
        Invoke(callbacks_.update, &cGameTime);
    }

    void Draw(const GameTime& gameTime) override
    {
        const CNA_GameTime cGameTime = MakeCGameTime(gameTime);
        Invoke(callbacks_.draw, &cGameTime);
    }

    void OnExiting(System::Object* sender, const System::EventArgs& args) override
    {
        NotifyExit();
        Game::OnExiting(sender, args);
    }

private:
    void NotifyExit()
    {
        if (hasExited_) {
            return;
        }
        hasExited_ = true;
        Invoke(callbacks_.exiting, nullptr);
    }

    void Invoke(
        const CNA_GameLifecycleCallback callback,
        const CNA_GameTime* const gameTime)
    {
        if (callback == nullptr || callbackFailure_ != CNA_RESULT_SUCCESS) {
            return;
        }

        CNA_CallbackError callbackError = {
            .struct_size = sizeof(CNA_CallbackError),
            .struct_version = StructureVersion,
            .message = {nullptr, 0U}
        };
        isInsideCallback_ = true;
        const CNA_Result result = CallWithExceptionBarrier([&]() {
            return callback(handle_, gameTime, callbacks_.context, &callbackError);
        });
        isInsideCallback_ = false;
        InvalidateBorrowedGraphicsDevice();
        if (result == CNA_RESULT_SUCCESS) {
            return;
        }

        std::string diagnostic;
        if (callbackError.struct_size >= sizeof(CNA_CallbackError) &&
            callbackError.struct_version == StructureVersion &&
            CopyStringView(callbackError.message, true, &diagnostic) == CNA_RESULT_SUCCESS &&
            !diagnostic.empty()) {
            static_cast<void>(Fail(CNA_RESULT_CALLBACK, CNA_ERROR_CATEGORY_CALLBACK, diagnostic));
        } else {
            static_cast<void>(Fail(
                CNA_RESULT_CALLBACK,
                CNA_ERROR_CATEGORY_CALLBACK,
                "A C game lifecycle callback failed."));
        }
        callbackFailure_ = CNA_RESULT_CALLBACK;
        Exit();
    }

    void InvalidateBorrowedGraphicsDevice() noexcept
    {
        if (borrowedGraphicsDeviceHandle_ == CNA_INVALID_HANDLE) {
            return;
        }
        static_cast<void>(GetRuntimeState().handles.Release(borrowedGraphicsDeviceHandle_));
        borrowedGraphicsDeviceHandle_ = CNA_INVALID_HANDLE;
    }

    CNA_GameCallbacks callbacks_;
    CNA_Handle handle_;
    CNA_Result callbackFailure_;
    bool isInsideCallback_;
    bool hasLoadedContent_;
    bool hasExited_;
    bool isShutDown_;
    CNA_Handle borrowedGraphicsDeviceHandle_ = CNA_INVALID_HANDLE;
};

[[nodiscard]] CNA_Result GetGame(
    const CNA_Handle handle,
    std::shared_ptr<CGame>* const outGame)
{
    const CNA_Result result = GetRuntimeState().handles.Get(handle, ObjectKind::Game, outGame);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(result, CategoryForResult(result), "The CNA game handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetCallableGame(
    const CNA_Handle handle,
    std::shared_ptr<CGame>* const outGame)
{
    const CNA_Result result = GetGame(handle, outGame);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((*outGame)->IsInsideCallback()) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "A game-driving or destruction operation cannot be called from a lifecycle callback.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetGraphicsDevice(
    const CNA_Handle handle,
    std::shared_ptr<BorrowedGraphicsDevice>* const outGraphicsDevice)
{
    const CNA_Result result = GetRuntimeState().handles.Get(
        handle,
        ObjectKind::GraphicsDevice,
        outGraphicsDevice);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        CategoryForResult(result),
        "The callback-scoped graphics-device handle is invalid for this call.");
}

[[nodiscard]] bool TryMapGraphicsCapability(
    const CNA_GraphicsCapability capability,
    CNA::GraphicsCapability* const outCapability) noexcept
{
    if (outCapability == nullptr) {
        return false;
    }
    switch (capability) {
        case CNA_GRAPHICS_CAPABILITY_THREE_D:
            *outCapability = CNA::GraphicsCapability::ThreeD;
            return true;
        case CNA_GRAPHICS_CAPABILITY_DEPTH_STENCIL_BUFFER:
            *outCapability = CNA::GraphicsCapability::DepthStencilBuffer;
            return true;
        case CNA_GRAPHICS_CAPABILITY_MULTI_SAMPLE_ANTI_ALIASING:
            *outCapability = CNA::GraphicsCapability::MultiSampleAntiAliasing;
            return true;
        case CNA_GRAPHICS_CAPABILITY_MULTIPLE_RENDER_TARGETS:
            *outCapability = CNA::GraphicsCapability::MultipleRenderTargets;
            return true;
        case CNA_GRAPHICS_CAPABILITY_ANISOTROPIC_FILTERING:
            *outCapability = CNA::GraphicsCapability::AnisotropicFiltering;
            return true;
        case CNA_GRAPHICS_CAPABILITY_WIRE_FRAME:
            *outCapability = CNA::GraphicsCapability::WireFrame;
            return true;
        case CNA_GRAPHICS_CAPABILITY_OCCLUSION_QUERY:
            *outCapability = CNA::GraphicsCapability::OcclusionQuery;
            return true;
        case CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS:
            *outCapability = CNA::GraphicsCapability::CustomEffects;
            return true;
        case CNA_GRAPHICS_CAPABILITY_TEXTURE_3D:
            *outCapability = CNA::GraphicsCapability::Texture3D;
            return true;
        case CNA_GRAPHICS_CAPABILITY_MULTI_STREAM_VERTEX_INPUT:
            *outCapability = CNA::GraphicsCapability::MultiStreamVertexInput;
            return true;
        case CNA_GRAPHICS_CAPABILITY_INSTANCING:
            *outCapability = CNA::GraphicsCapability::Instancing;
            return true;
        case CNA_GRAPHICS_CAPABILITY_STENCIL_BUFFER:
            *outCapability = CNA::GraphicsCapability::StencilBuffer;
            return true;
        case CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING:
            *outCapability = CNA::GraphicsCapability::AdditiveBlending;
            return true;
        default:
            return false;
    }
}

[[nodiscard]] CNA_GraphicsRendererType MapGraphicsRendererType(
    const CNA::GraphicsRendererType rendererType) noexcept
{
    switch (rendererType) {
        case CNA::GraphicsRendererType::SdlRenderer: return CNA_GRAPHICS_RENDERER_SDL_RENDERER;
        case CNA::GraphicsRendererType::OpenGLES2: return CNA_GRAPHICS_RENDERER_OPENGLES2;
        case CNA::GraphicsRendererType::OpenGLES3: return CNA_GRAPHICS_RENDERER_OPENGLES3;
        case CNA::GraphicsRendererType::OpenGL33: return CNA_GRAPHICS_RENDERER_OPENGL33;
        case CNA::GraphicsRendererType::WebGL1: return CNA_GRAPHICS_RENDERER_WEBGL1;
        case CNA::GraphicsRendererType::WebGL2: return CNA_GRAPHICS_RENDERER_WEBGL2;
        case CNA::GraphicsRendererType::Bgfx: return CNA_GRAPHICS_RENDERER_BGFX;
        case CNA::GraphicsRendererType::Vulkan: return CNA_GRAPHICS_RENDERER_VULKAN;
        case CNA::GraphicsRendererType::WebGPU: return CNA_GRAPHICS_RENDERER_WEBGPU;
        case CNA::GraphicsRendererType::Magnum: return CNA_GRAPHICS_RENDERER_MAGNUM;
        case CNA::GraphicsRendererType::Headless: return CNA_GRAPHICS_RENDERER_HEADLESS;
        case CNA::GraphicsRendererType::Software: return CNA_GRAPHICS_RENDERER_SOFTWARE;
        case CNA::GraphicsRendererType::Stub: return CNA_GRAPHICS_RENDERER_STUB;
        case CNA::GraphicsRendererType::DirectX11: return CNA_GRAPHICS_RENDERER_DIRECTX11;
        case CNA::GraphicsRendererType::DirectX12: return CNA_GRAPHICS_RENDERER_DIRECTX12;
        case CNA::GraphicsRendererType::Direct2D: return CNA_GRAPHICS_RENDERER_DIRECT2D;
        case CNA::GraphicsRendererType::Canvas: return CNA_GRAPHICS_RENDERER_CANVAS;
        case CNA::GraphicsRendererType::HtmlDom: return CNA_GRAPHICS_RENDERER_HTML_DOM;
        case CNA::GraphicsRendererType::Skia: return CNA_GRAPHICS_RENDERER_SKIA;
        case CNA::GraphicsRendererType::Blend2D: return CNA_GRAPHICS_RENDERER_BLEND2D;
        case CNA::GraphicsRendererType::FreeDirect: return CNA_GRAPHICS_RENDERER_FREEDIRECT;
        case CNA::GraphicsRendererType::DirectX9: return CNA_GRAPHICS_RENDERER_DIRECTX9;
        case CNA::GraphicsRendererType::DirectX1: return CNA_GRAPHICS_RENDERER_DIRECTX1;
        case CNA::GraphicsRendererType::DirectX2: return CNA_GRAPHICS_RENDERER_DIRECTX2;
        case CNA::GraphicsRendererType::DirectX3: return CNA_GRAPHICS_RENDERER_DIRECTX3;
        case CNA::GraphicsRendererType::DirectX5: return CNA_GRAPHICS_RENDERER_DIRECTX5;
        case CNA::GraphicsRendererType::DirectX6: return CNA_GRAPHICS_RENDERER_DIRECTX6;
        case CNA::GraphicsRendererType::DirectX7: return CNA_GRAPHICS_RENDERER_DIRECTX7;
        case CNA::GraphicsRendererType::DirectX8: return CNA_GRAPHICS_RENDERER_DIRECTX8;
        case CNA::GraphicsRendererType::DirectX10: return CNA_GRAPHICS_RENDERER_DIRECTX10;
        case CNA::GraphicsRendererType::SdlGpu: return CNA_GRAPHICS_RENDERER_SDL_GPU;
        case CNA::GraphicsRendererType::OpenGLES1: return CNA_GRAPHICS_RENDERER_OPENGLES1;
        case CNA::GraphicsRendererType::OpenGL4: return CNA_GRAPHICS_RENDERER_OPENGL4;
        case CNA::GraphicsRendererType::OpenGL1: return CNA_GRAPHICS_RENDERER_OPENGL1;
        case CNA::GraphicsRendererType::OpenGL2: return CNA_GRAPHICS_RENDERER_OPENGL2;
        case CNA::GraphicsRendererType::Wicked: return CNA_GRAPHICS_RENDERER_WICKED;
        case CNA::GraphicsRendererType::Sokol: return CNA_GRAPHICS_RENDERER_SOKOL;
        case CNA::GraphicsRendererType::Diligent: return CNA_GRAPHICS_RENDERER_DILIGENT;
        case CNA::GraphicsRendererType::Glide: return CNA_GRAPHICS_RENDERER_GLIDE;
        case CNA::GraphicsRendererType::Gdi: return CNA_GRAPHICS_RENDERER_GDI;
        case CNA::GraphicsRendererType::Llgl: return CNA_GRAPHICS_RENDERER_LLGL;
        case CNA::GraphicsRendererType::Metal: return CNA_GRAPHICS_RENDERER_METAL;
        case CNA::GraphicsRendererType::Fna3d: return CNA_GRAPHICS_RENDERER_FNA3D;
        case CNA::GraphicsRendererType::SvgDom: return CNA_GRAPHICS_RENDERER_SVG_DOM;
        case CNA::GraphicsRendererType::OpenVg: return CNA_GRAPHICS_RENDERER_OPENVG;
        case CNA::GraphicsRendererType::PortableGL: return CNA_GRAPHICS_RENDERER_PORTABLEGL;
    }
    return CNA_GRAPHICS_RENDERER_UNKNOWN;
}

[[nodiscard]] CNA_GraphicsCapabilityFlags GetGraphicsCapabilityFlags(
    GraphicsDevice& graphicsDevice)
{
    CNA_GraphicsCapabilityFlags flags = UINT64_C(0);
    for (CNA_GraphicsCapability capability = CNA_GRAPHICS_CAPABILITY_THREE_D;
         capability <= CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING;
         ++capability) {
        CNA::GraphicsCapability nativeCapability{};
        if (TryMapGraphicsCapability(capability, &nativeCapability) &&
            graphicsDevice.SupportsCapability(nativeCapability)) {
            flags |= UINT64_C(1) << capability;
        }
    }
    return flags;
}

} // namespace

CNA_Result cna_game_create(
    const CNA_GameCreateInfo* const createInfo,
    CNA_Handle* const outGame)
{
    return CallWithExceptionBarrier([&]() {
        if (!IsSupportedGameCreateInfo(createInfo) || outGame == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The game creation structure or output handle is invalid.");
        }
        *outGame = CNA_INVALID_HANDLE;
        if ((createInfo->is_fixed_time_step != CNA_FALSE &&
             createInfo->is_fixed_time_step != CNA_TRUE) ||
            createInfo->target_elapsed_time_ticks <= 0 ||
            !IsSupportedGameCallbacks(createInfo->callbacks)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The game creation configuration is invalid.");
        }
        if (const CNA_Result titleResult = ValidateStringView(createInfo->window_title, true);
            titleResult != CNA_RESULT_SUCCESS) {
            return Fail(titleResult, CategoryForResult(titleResult), "The initial window title is not valid UTF-8.");
        }

        std::string title;
        if (const CNA_Result titleResult = CopyStringView(createInfo->window_title, true, &title);
            titleResult != CNA_RESULT_SUCCESS) {
            return Fail(titleResult, CategoryForResult(titleResult), "The initial window title could not be copied.");
        }

        RuntimeState& state = GetRuntimeState();
        std::lock_guard lock(state.mutex);
        if (state.hasActiveGame) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "Only one C-owned CNA game may be active at a time.");
        }

        const auto game = std::make_shared<CGame>(createInfo->callbacks);
        game->setIsFixedTimeStepProperty(createInfo->is_fixed_time_step == CNA_TRUE);
        game->setTargetElapsedTimeProperty(
            System::TimeSpan::FromTicks(createInfo->target_elapsed_time_ticks));
        game->SetWindowTitle(title);

        const CNA_Result createResult = state.handles.Create(ObjectKind::Game, game, outGame);
        if (createResult != CNA_RESULT_SUCCESS) {
            return Fail(
                createResult,
                CategoryForResult(createResult),
                "The native game handle could not be created.");
        }
        game->SetHandle(*outGame);
        state.hasActiveGame = true;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_run_one_frame(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetCallableGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->RunOneFrame();
        return game->GetCallbackFailure();
    });
}

CNA_Result cna_game_run(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetCallableGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->Run();
        return game->GetCallbackFailure();
    });
}

CNA_Result cna_game_request_exit(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->Exit();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_clear(const CNA_Handle gameHandle, const CNA_Color color)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        game->Clear(color);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_set_window_title(
    const CNA_Handle gameHandle,
    const CNA_StringView title)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
            return result;
        }

        std::string titleCopy;
        if (const CNA_Result titleResult = CopyStringView(title, true, &titleCopy);
            titleResult != CNA_RESULT_SUCCESS) {
            return Fail(titleResult, CategoryForResult(titleResult), "The window title is not valid UTF-8.");
        }
        game->SetWindowTitle(titleCopy);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_game_destroy(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetCallableGame(gameHandle, &game);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        game->Shutdown();
        const CNA_Result callbackResult = game->GetCallbackFailure();
        RuntimeState& state = GetRuntimeState();
        const CNA_Result releaseResult = state.handles.Release(gameHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                CategoryForResult(releaseResult),
                "The native game handle could not be released.");
        }
        {
            std::lock_guard lock(state.mutex);
            state.hasActiveGame = false;
        }
        return callbackResult;
    });
}

CNA_Result cna_game_get_graphics_device(
    const CNA_Handle gameHandle,
    CNA_Handle* const outGraphicsDevice)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<CGame> game;
        if (const CNA_Result result = GetGame(gameHandle, &game); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return game->BorrowGraphicsDevice(outGraphicsDevice);
    });
}

CNA_Result cna_graphics_device_get_renderer_info(
    const CNA_Handle graphicsDeviceHandle,
    CNA_RendererInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_RendererInfo) ||
            outInfo->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-info output structure is invalid.");
        }

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        GraphicsDevice& nativeDevice = *graphicsDevice->value;
        const std::string_view rendererName = nativeDevice.GetGraphicsRendererName();
        const int maxTextureDimension = nativeDevice.GetMaxTextureDimension();
        if (maxTextureDimension < 0) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The native renderer reported a negative texture-dimension limit.");
        }
        const CNA_RendererInfo info = {
            .struct_size = sizeof(CNA_RendererInfo),
            .struct_version = StructureVersion,
            .renderer_name_byte_length = rendererName.size(),
            .capability_flags = GetGraphicsCapabilityFlags(nativeDevice),
            .renderer_type = MapGraphicsRendererType(nativeDevice.GetGraphicsRendererType()),
            .max_texture_dimension = static_cast<uint32_t>(maxTextureDimension)
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_renderer_name_size(
    const CNA_Handle graphicsDeviceHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-name size output is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = graphicsDevice->value->GetGraphicsRendererName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_copy_renderer_name(
    const CNA_Handle graphicsDeviceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-name output buffer is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const std::string_view rendererName = graphicsDevice->value->GetGraphicsRendererName();
        *outBytes = rendererName.size();
        if (capacity < rendererName.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The renderer-name output buffer is too small.");
        }
        if (!rendererName.empty()) {
            std::memcpy(destination, rendererName.data(), rendererName.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_supports_capability(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsCapability capability,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() {
        CNA::GraphicsCapability nativeCapability{};
        if (outSupported == nullptr || !TryMapGraphicsCapability(capability, &nativeCapability)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics-capability query arguments are invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = graphicsDevice->value->SupportsCapability(nativeCapability)
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}
