// SPDX-License-Identifier: MS-PL

#include "CNA/C/core_ext.h"
#include "CnaCApiDetail.hpp"

#include "CNA/DesktopOS.hpp"
#include "CNA/GraphicsBackendCategory.hpp"
#include "CNA/GraphicsBackendMaturity.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/LogCategory.hpp"
#include "CNA/LogLevel.hpp"
#include "CNA/Logger.hpp"
#include "CNA/TargetPlatform.hpp"

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;

namespace {

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result MapLogLevel(const CNA_LogLevel level, CNA::LogLevel* const outLevel)
{
    switch (level) {
        case CNA_LOG_LEVEL_FATAL: *outLevel = CNA::LogLevel::FATAL; return CNA_RESULT_SUCCESS;
        case CNA_LOG_LEVEL_ERROR: *outLevel = CNA::LogLevel::ERROR; return CNA_RESULT_SUCCESS;
        case CNA_LOG_LEVEL_WARN: *outLevel = CNA::LogLevel::WARN; return CNA_RESULT_SUCCESS;
        case CNA_LOG_LEVEL_INFO: *outLevel = CNA::LogLevel::INFO; return CNA_RESULT_SUCCESS;
        case CNA_LOG_LEVEL_DEBUG: *outLevel = CNA::LogLevel::DEBUG; return CNA_RESULT_SUCCESS;
        case CNA_LOG_LEVEL_TRACE: *outLevel = CNA::LogLevel::TRACE; return CNA_RESULT_SUCCESS;
        case CNA_LOG_LEVEL_EXPERIMENT:
            *outLevel = CNA::LogLevel::EXPERIMENT;
            return CNA_RESULT_SUCCESS;
        default: return InvalidArgument("The requested log level is not a canonical identity.");
    }
}

[[nodiscard]] CNA_LogLevel MapLogLevelToC(const CNA::LogLevel level) noexcept
{
    switch (level) {
        case CNA::LogLevel::FATAL: return CNA_LOG_LEVEL_FATAL;
        case CNA::LogLevel::ERROR: return CNA_LOG_LEVEL_ERROR;
        case CNA::LogLevel::WARN: return CNA_LOG_LEVEL_WARN;
        case CNA::LogLevel::INFO: return CNA_LOG_LEVEL_INFO;
        case CNA::LogLevel::DEBUG: return CNA_LOG_LEVEL_DEBUG;
        case CNA::LogLevel::TRACE: return CNA_LOG_LEVEL_TRACE;
        case CNA::LogLevel::EXPERIMENT: return CNA_LOG_LEVEL_EXPERIMENT;
    }
    return CNA_LOG_LEVEL_INFO;
}

[[nodiscard]] CNA_Result MapLogCategory(
    const CNA_LogCategory category,
    CNA::LogCategory* const outCategory)
{
    switch (category) {
        case CNA_LOG_CATEGORY_APPLICATION:
            *outCategory = CNA::LogCategory::APPLICATION;
            return CNA_RESULT_SUCCESS;
        case CNA_LOG_CATEGORY_ERROR:
            *outCategory = CNA::LogCategory::ERROR;
            return CNA_RESULT_SUCCESS;
        case CNA_LOG_CATEGORY_SYSTEM:
            *outCategory = CNA::LogCategory::SYSTEM;
            return CNA_RESULT_SUCCESS;
        case CNA_LOG_CATEGORY_AUDIO:
            *outCategory = CNA::LogCategory::AUDIO;
            return CNA_RESULT_SUCCESS;
        case CNA_LOG_CATEGORY_VIDEO:
            *outCategory = CNA::LogCategory::VIDEO;
            return CNA_RESULT_SUCCESS;
        case CNA_LOG_CATEGORY_RENDER:
            *outCategory = CNA::LogCategory::RENDER;
            return CNA_RESULT_SUCCESS;
        case CNA_LOG_CATEGORY_INPUT:
            *outCategory = CNA::LogCategory::INPUT;
            return CNA_RESULT_SUCCESS;
        case CNA_LOG_CATEGORY_TEST:
            *outCategory = CNA::LogCategory::TEST;
            return CNA_RESULT_SUCCESS;
        case CNA_LOG_CATEGORY_GPU:
            *outCategory = CNA::LogCategory::GPU;
            return CNA_RESULT_SUCCESS;
        default:
            return InvalidArgument("The requested log category is not a canonical identity.");
    }
}

// The canonical logger takes a std::string_view, so the borrowed C view is copied into an owned
// std::string first: a view is only valid for the duration of the call, and the copy also runs the
// project's embedded-NUL rejection.
[[nodiscard]] CNA_Result CopyMessage(const CNA_StringView message, std::string* const outValue)
{
    const CNA_Result result = CopyStringView(message, true, outValue);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(result, ErrorCategoryForResult(result), "The log message is invalid.");
}

using LoggerRoute = void (*)(std::string_view, CNA::LogCategory);

[[nodiscard]] CNA_Result LogWithCategory(
    const LoggerRoute route,
    const CNA_StringView message,
    const CNA_LogCategory category)
{
    CNA::LogCategory nativeCategory = CNA::LogCategory::APPLICATION;
    if (const CNA_Result result = MapLogCategory(category, &nativeCategory);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string text;
    if (const CNA_Result result = CopyMessage(message, &text); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    route(text, nativeCategory);
    return CNA_RESULT_SUCCESS;
}

using ConditionalLoggerRoute = void (*)(std::string_view, bool);

[[nodiscard]] CNA_Result LogWithCondition(
    const ConditionalLoggerRoute route,
    const CNA_StringView message,
    const CNA_Bool condition)
{
    std::string text;
    if (const CNA_Result result = CopyMessage(message, &text); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    route(text, condition != CNA_FALSE);
    return CNA_RESULT_SUCCESS;
}

// Every public renderer identity, paired explicitly so neither side depends on the other's
// declaration order.
constexpr std::array<std::pair<CNA_GraphicsRendererType, CNA::GraphicsRendererType>, 46>
    RendererIdentities{{
        {CNA_GRAPHICS_RENDERER_SDL_RENDERER, CNA::GraphicsRendererType::SdlRenderer},
        {CNA_GRAPHICS_RENDERER_OPENGLES2, CNA::GraphicsRendererType::OpenGLES2},
        {CNA_GRAPHICS_RENDERER_OPENGLES3, CNA::GraphicsRendererType::OpenGLES3},
        {CNA_GRAPHICS_RENDERER_OPENGL33, CNA::GraphicsRendererType::OpenGL33},
        {CNA_GRAPHICS_RENDERER_WEBGL1, CNA::GraphicsRendererType::WebGL1},
        {CNA_GRAPHICS_RENDERER_WEBGL2, CNA::GraphicsRendererType::WebGL2},
        {CNA_GRAPHICS_RENDERER_BGFX, CNA::GraphicsRendererType::Bgfx},
        {CNA_GRAPHICS_RENDERER_VULKAN, CNA::GraphicsRendererType::Vulkan},
        {CNA_GRAPHICS_RENDERER_WEBGPU, CNA::GraphicsRendererType::WebGPU},
        {CNA_GRAPHICS_RENDERER_MAGNUM, CNA::GraphicsRendererType::Magnum},
        {CNA_GRAPHICS_RENDERER_HEADLESS, CNA::GraphicsRendererType::Headless},
        {CNA_GRAPHICS_RENDERER_SOFTWARE, CNA::GraphicsRendererType::Software},
        {CNA_GRAPHICS_RENDERER_STUB, CNA::GraphicsRendererType::Stub},
        {CNA_GRAPHICS_RENDERER_DIRECTX11, CNA::GraphicsRendererType::DirectX11},
        {CNA_GRAPHICS_RENDERER_DIRECTX12, CNA::GraphicsRendererType::DirectX12},
        {CNA_GRAPHICS_RENDERER_DIRECT2D, CNA::GraphicsRendererType::Direct2D},
        {CNA_GRAPHICS_RENDERER_CANVAS, CNA::GraphicsRendererType::Canvas},
        {CNA_GRAPHICS_RENDERER_HTML_DOM, CNA::GraphicsRendererType::HtmlDom},
        {CNA_GRAPHICS_RENDERER_SKIA, CNA::GraphicsRendererType::Skia},
        {CNA_GRAPHICS_RENDERER_BLEND2D, CNA::GraphicsRendererType::Blend2D},
        {CNA_GRAPHICS_RENDERER_FREEDIRECT, CNA::GraphicsRendererType::FreeDirect},
        {CNA_GRAPHICS_RENDERER_DIRECTX9, CNA::GraphicsRendererType::DirectX9},
        {CNA_GRAPHICS_RENDERER_DIRECTX1, CNA::GraphicsRendererType::DirectX1},
        {CNA_GRAPHICS_RENDERER_DIRECTX2, CNA::GraphicsRendererType::DirectX2},
        {CNA_GRAPHICS_RENDERER_DIRECTX3, CNA::GraphicsRendererType::DirectX3},
        {CNA_GRAPHICS_RENDERER_DIRECTX5, CNA::GraphicsRendererType::DirectX5},
        {CNA_GRAPHICS_RENDERER_DIRECTX6, CNA::GraphicsRendererType::DirectX6},
        {CNA_GRAPHICS_RENDERER_DIRECTX7, CNA::GraphicsRendererType::DirectX7},
        {CNA_GRAPHICS_RENDERER_DIRECTX8, CNA::GraphicsRendererType::DirectX8},
        {CNA_GRAPHICS_RENDERER_DIRECTX10, CNA::GraphicsRendererType::DirectX10},
        {CNA_GRAPHICS_RENDERER_SDL_GPU, CNA::GraphicsRendererType::SdlGpu},
        {CNA_GRAPHICS_RENDERER_OPENGLES1, CNA::GraphicsRendererType::OpenGLES1},
        {CNA_GRAPHICS_RENDERER_OPENGL4, CNA::GraphicsRendererType::OpenGL4},
        {CNA_GRAPHICS_RENDERER_OPENGL1, CNA::GraphicsRendererType::OpenGL1},
        {CNA_GRAPHICS_RENDERER_OPENGL2, CNA::GraphicsRendererType::OpenGL2},
        {CNA_GRAPHICS_RENDERER_WICKED, CNA::GraphicsRendererType::Wicked},
        {CNA_GRAPHICS_RENDERER_SOKOL, CNA::GraphicsRendererType::Sokol},
        {CNA_GRAPHICS_RENDERER_DILIGENT, CNA::GraphicsRendererType::Diligent},
        {CNA_GRAPHICS_RENDERER_GLIDE, CNA::GraphicsRendererType::Glide},
        {CNA_GRAPHICS_RENDERER_GDI, CNA::GraphicsRendererType::Gdi},
        {CNA_GRAPHICS_RENDERER_LLGL, CNA::GraphicsRendererType::Llgl},
        {CNA_GRAPHICS_RENDERER_METAL, CNA::GraphicsRendererType::Metal},
        {CNA_GRAPHICS_RENDERER_FNA3D, CNA::GraphicsRendererType::Fna3d},
        {CNA_GRAPHICS_RENDERER_SVG_DOM, CNA::GraphicsRendererType::SvgDom},
        {CNA_GRAPHICS_RENDERER_OPENVG, CNA::GraphicsRendererType::OpenVg},
        {CNA_GRAPHICS_RENDERER_PORTABLEGL, CNA::GraphicsRendererType::PortableGL},
    }};

[[nodiscard]] CNA_Result MapRendererType(
    const CNA_GraphicsRendererType type,
    CNA::GraphicsRendererType* const outType)
{
    for (const auto& [identity, native] : RendererIdentities) {
        if (identity == type) {
            *outType = native;
            return CNA_RESULT_SUCCESS;
        }
    }
    return InvalidArgument("The requested renderer is not a public CNA renderer identity.");
}

[[nodiscard]] CNA_GraphicsRendererType MapRendererTypeToC(
    const CNA::GraphicsRendererType type) noexcept
{
    for (const auto& [identity, native] : RendererIdentities) {
        if (native == type) {
            return identity;
        }
    }
    return CNA_GRAPHICS_RENDERER_UNKNOWN;
}

[[nodiscard]] CNA_GraphicsBackendCategory MapBackendCategoryToC(
    const CNA::GraphicsBackendCategory category) noexcept
{
    switch (category) {
        case CNA::GraphicsBackendCategory::Native:
            return CNA_GRAPHICS_BACKEND_CATEGORY_NATIVE;
        case CNA::GraphicsBackendCategory::TranslationLayer:
            return CNA_GRAPHICS_BACKEND_CATEGORY_TRANSLATION_LAYER;
        case CNA::GraphicsBackendCategory::Software:
            return CNA_GRAPHICS_BACKEND_CATEGORY_SOFTWARE;
        case CNA::GraphicsBackendCategory::Web:
            return CNA_GRAPHICS_BACKEND_CATEGORY_WEB;
        case CNA::GraphicsBackendCategory::Diagnostic:
            return CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC;
    }
    return CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC;
}

[[nodiscard]] CNA_Result MapBackendCategory(
    const CNA_GraphicsBackendCategory category,
    CNA::GraphicsBackendCategory* const outCategory)
{
    switch (category) {
        case CNA_GRAPHICS_BACKEND_CATEGORY_NATIVE:
            *outCategory = CNA::GraphicsBackendCategory::Native;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_CATEGORY_TRANSLATION_LAYER:
            *outCategory = CNA::GraphicsBackendCategory::TranslationLayer;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_CATEGORY_SOFTWARE:
            *outCategory = CNA::GraphicsBackendCategory::Software;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_CATEGORY_WEB:
            *outCategory = CNA::GraphicsBackendCategory::Web;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC:
            *outCategory = CNA::GraphicsBackendCategory::Diagnostic;
            return CNA_RESULT_SUCCESS;
        default:
            return InvalidArgument(
                "The requested backend category is not a canonical identity.");
    }
}

[[nodiscard]] CNA_GraphicsBackendMaturity MapBackendMaturityToC(
    const CNA::GraphicsBackendMaturity maturity) noexcept
{
    switch (maturity) {
        case CNA::GraphicsBackendMaturity::Production:
            return CNA_GRAPHICS_BACKEND_MATURITY_PRODUCTION;
        case CNA::GraphicsBackendMaturity::Supported:
            return CNA_GRAPHICS_BACKEND_MATURITY_SUPPORTED;
        case CNA::GraphicsBackendMaturity::Experimental:
            return CNA_GRAPHICS_BACKEND_MATURITY_EXPERIMENTAL;
        case CNA::GraphicsBackendMaturity::Historical:
            return CNA_GRAPHICS_BACKEND_MATURITY_HISTORICAL;
        case CNA::GraphicsBackendMaturity::Deprecated:
            return CNA_GRAPHICS_BACKEND_MATURITY_DEPRECATED;
    }
    return CNA_GRAPHICS_BACKEND_MATURITY_EXPERIMENTAL;
}

[[nodiscard]] CNA_Result MapBackendMaturity(
    const CNA_GraphicsBackendMaturity maturity,
    CNA::GraphicsBackendMaturity* const outMaturity)
{
    switch (maturity) {
        case CNA_GRAPHICS_BACKEND_MATURITY_PRODUCTION:
            *outMaturity = CNA::GraphicsBackendMaturity::Production;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_MATURITY_SUPPORTED:
            *outMaturity = CNA::GraphicsBackendMaturity::Supported;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_MATURITY_EXPERIMENTAL:
            *outMaturity = CNA::GraphicsBackendMaturity::Experimental;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_MATURITY_HISTORICAL:
            *outMaturity = CNA::GraphicsBackendMaturity::Historical;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_MATURITY_DEPRECATED:
            *outMaturity = CNA::GraphicsBackendMaturity::Deprecated;
            return CNA_RESULT_SUCCESS;
        default:
            return InvalidArgument(
                "The requested backend maturity is not a canonical identity.");
    }
}

[[nodiscard]] CNA_Result ReportSize(const std::string_view value, uint64_t* const outBytes)
{
    if (outBytes == nullptr) {
        return InvalidArgument("The byte-count output is null.");
    }
    *outBytes = static_cast<uint64_t>(value.size());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyName(
    const std::string_view value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The name output is invalid.");
    }
    *outBytes = static_cast<uint64_t>(value.size());
    if (capacity < static_cast<uint64_t>(value.size())) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the name.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_logger_log(
    const CNA_LogLevel level,
    const CNA_StringView message,
    const CNA_LogCategory category,
    const CNA_Bool condition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CNA::LogLevel nativeLevel = CNA::LogLevel::INFO;
        if (const CNA_Result result = MapLogLevel(level, &nativeLevel);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        CNA::LogCategory nativeCategory = CNA::LogCategory::APPLICATION;
        if (const CNA_Result result = MapLogCategory(category, &nativeCategory);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string text;
        if (const CNA_Result result = CopyMessage(message, &text); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        CNA::Logger::Log(nativeLevel, text, nativeCategory, condition != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_logger_fatal(const CNA_StringView message, const CNA_LogCategory category)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCategory(&CNA::Logger::Fatal, message, category);
    });
}

CNA_Result cna_logger_error(const CNA_StringView message, const CNA_LogCategory category)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCategory(&CNA::Logger::Error, message, category);
    });
}

CNA_Result cna_logger_warn(const CNA_StringView message, const CNA_LogCategory category)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCategory(&CNA::Logger::Warn, message, category);
    });
}

CNA_Result cna_logger_info(const CNA_StringView message, const CNA_LogCategory category)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCategory(&CNA::Logger::Info, message, category);
    });
}

CNA_Result cna_logger_debug(const CNA_StringView message, const CNA_LogCategory category)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCategory(&CNA::Logger::Debug, message, category);
    });
}

CNA_Result cna_logger_trace(const CNA_StringView message, const CNA_LogCategory category)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCategory(&CNA::Logger::Trace, message, category);
    });
}

CNA_Result cna_logger_experiment(const CNA_StringView message, const CNA_LogCategory category)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCategory(&CNA::Logger::Experiment, message, category);
    });
}

CNA_Result cna_logger_fatal_if(const CNA_StringView message, const CNA_Bool condition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCondition(&CNA::Logger::FatalIf, message, condition);
    });
}

CNA_Result cna_logger_error_if(const CNA_StringView message, const CNA_Bool condition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCondition(&CNA::Logger::ErrorIf, message, condition);
    });
}

CNA_Result cna_logger_warn_if(const CNA_StringView message, const CNA_Bool condition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCondition(&CNA::Logger::WarnIf, message, condition);
    });
}

CNA_Result cna_logger_info_if(const CNA_StringView message, const CNA_Bool condition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCondition(&CNA::Logger::InfoIf, message, condition);
    });
}

CNA_Result cna_logger_debug_if(const CNA_StringView message, const CNA_Bool condition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCondition(&CNA::Logger::DebugIf, message, condition);
    });
}

CNA_Result cna_logger_trace_if(const CNA_StringView message, const CNA_Bool condition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCondition(&CNA::Logger::TraceIf, message, condition);
    });
}

CNA_Result cna_logger_experiment_if(const CNA_StringView message, const CNA_Bool condition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LogWithCondition(&CNA::Logger::ExperimentIf, message, condition);
    });
}

CNA_Result cna_logger_set_minimum_level(const CNA_LogLevel level)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CNA::LogLevel nativeLevel = CNA::LogLevel::INFO;
        if (const CNA_Result result = MapLogLevel(level, &nativeLevel);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        CNA::Logger::SetMinimumLevel(nativeLevel);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_logger_get_minimum_level(CNA_LogLevel* const outLevel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLevel == nullptr) {
            return InvalidArgument("The log-level output is null.");
        }
        *outLevel = MapLogLevelToC(CNA::Logger::GetMinimumLevel());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_platform_get_current(CNA_Platform* const outPlatform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPlatform == nullptr) {
            return InvalidArgument("The platform output is null.");
        }
        switch (CNA::getCurrentPlatform()) {
            case CNA::TargetPlatform::Desktop: *outPlatform = CNA_PLATFORM_DESKTOP; break;
            case CNA::TargetPlatform::Android: *outPlatform = CNA_PLATFORM_ANDROID; break;
            case CNA::TargetPlatform::iOS: *outPlatform = CNA_PLATFORM_IOS; break;
            case CNA::TargetPlatform::Web: *outPlatform = CNA_PLATFORM_WEB; break;
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_desktop_os_get_current(CNA_DesktopOS* const outOs)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOs == nullptr) {
            return InvalidArgument("The desktop operating-system output is null.");
        }
        switch (CNA::getCurrentDesktopOS()) {
            case CNA::DesktopOS::Windows: *outOs = CNA_DESKTOP_OS_WINDOWS; break;
            case CNA::DesktopOS::Linux: *outOs = CNA_DESKTOP_OS_LINUX; break;
            case CNA::DesktopOS::MacOSX: *outOs = CNA_DESKTOP_OS_MACOSX; break;
            case CNA::DesktopOS::Other: *outOs = CNA_DESKTOP_OS_OTHER; break;
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_backend_get_category(
    const CNA_GraphicsRendererType type,
    CNA_GraphicsBackendCategory* const outCategory)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCategory == nullptr) {
            return InvalidArgument("The backend-category output is null.");
        }
        CNA::GraphicsRendererType nativeType = CNA::GraphicsRendererType::Headless;
        if (const CNA_Result result = MapRendererType(type, &nativeType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCategory = MapBackendCategoryToC(CNA::getGraphicsBackendCategory(nativeType));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_backend_get_current_category(
    CNA_GraphicsBackendCategory* const outCategory)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCategory == nullptr) {
            return InvalidArgument("The backend-category output is null.");
        }
        *outCategory = MapBackendCategoryToC(CNA::getCurrentGraphicsBackendCategory());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_backend_category_get_name_size(
    const CNA_GraphicsBackendCategory category,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CNA::GraphicsBackendCategory nativeCategory = CNA::GraphicsBackendCategory::Native;
        if (const CNA_Result result = MapBackendCategory(category, &nativeCategory);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(CNA::toStringView(nativeCategory), outBytes);
    });
}

CNA_Result cna_graphics_backend_category_copy_name(
    const CNA_GraphicsBackendCategory category,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CNA::GraphicsBackendCategory nativeCategory = CNA::GraphicsBackendCategory::Native;
        if (const CNA_Result result = MapBackendCategory(category, &nativeCategory);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyName(CNA::toStringView(nativeCategory), destination, capacity, outBytes);
    });
}

CNA_Result cna_graphics_backend_get_maturity(
    const CNA_GraphicsRendererType type,
    CNA_GraphicsBackendMaturity* const outMaturity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMaturity == nullptr) {
            return InvalidArgument("The backend-maturity output is null.");
        }
        CNA::GraphicsRendererType nativeType = CNA::GraphicsRendererType::Headless;
        if (const CNA_Result result = MapRendererType(type, &nativeType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outMaturity = MapBackendMaturityToC(CNA::getGraphicsBackendMaturity(nativeType));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_backend_get_current_maturity(
    CNA_GraphicsBackendMaturity* const outMaturity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMaturity == nullptr) {
            return InvalidArgument("The backend-maturity output is null.");
        }
        *outMaturity = MapBackendMaturityToC(CNA::getCurrentGraphicsBackendMaturity());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_backend_maturity_get_name_size(
    const CNA_GraphicsBackendMaturity maturity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CNA::GraphicsBackendMaturity nativeMaturity = CNA::GraphicsBackendMaturity::Production;
        if (const CNA_Result result = MapBackendMaturity(maturity, &nativeMaturity);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReportSize(CNA::toStringView(nativeMaturity), outBytes);
    });
}

CNA_Result cna_graphics_backend_maturity_copy_name(
    const CNA_GraphicsBackendMaturity maturity,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CNA::GraphicsBackendMaturity nativeMaturity = CNA::GraphicsBackendMaturity::Production;
        if (const CNA_Result result = MapBackendMaturity(maturity, &nativeMaturity);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyName(CNA::toStringView(nativeMaturity), destination, capacity, outBytes);
    });
}

CNA_Result cna_graphics_renderer_get_current_type(CNA_GraphicsRendererType* const outType)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outType == nullptr) {
            return InvalidArgument("The renderer-identity output is null.");
        }
        *outType = MapRendererTypeToC(CNA::getCurrentGraphicsRendererType());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_renderer_get_current_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportSize(CNA::getCurrentGraphicsRendererName(), outBytes);
    });
}

CNA_Result cna_graphics_renderer_copy_current_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyName(
            CNA::getCurrentGraphicsRendererName(),
            destination,
            capacity,
            outBytes);
    });
}
