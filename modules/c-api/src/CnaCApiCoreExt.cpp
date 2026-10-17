// SPDX-License-Identifier: MS-PL

#include "CNA/C/core_ext.h"
#include "CNA/AssemblyInfo.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"

#include "CNA/DesktopOS.hpp"
#include "CNA/GraphicsRendererCategory.hpp"
#include "CNA/GraphicsRendererMaturity.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/GraphicsRendererFallbackRecord.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Internal/RetiredGraphicsRenderers.hpp"
#include "CNA/LogCategory.hpp"
#include "CNA/LogLevel.hpp"
#include "CNA/Logger.hpp"
#include "CNA/TargetPlatform.hpp"

#include <array>
#include <cstring>
#include <string>
#include <mutex>
#include <string_view>
#include <vector>
#include <span>
#include <utility>

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ValidateCanonicalBool;

namespace {

using CNA::C::Detail::CopyText;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ReportTextSize;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateVersionedStructure;

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

[[nodiscard]] CNA_LogCategory MapLogCategoryToC(const CNA::LogCategory category) noexcept
{
    switch (category) {
    case CNA::LogCategory::APPLICATION:
        return CNA_LOG_CATEGORY_APPLICATION;
    case CNA::LogCategory::ERROR:
        return CNA_LOG_CATEGORY_ERROR;
    case CNA::LogCategory::SYSTEM:
        return CNA_LOG_CATEGORY_SYSTEM;
    case CNA::LogCategory::AUDIO:
        return CNA_LOG_CATEGORY_AUDIO;
    case CNA::LogCategory::VIDEO:
        return CNA_LOG_CATEGORY_VIDEO;
    case CNA::LogCategory::RENDER:
        return CNA_LOG_CATEGORY_RENDER;
    case CNA::LogCategory::INPUT:
        return CNA_LOG_CATEGORY_INPUT;
    case CNA::LogCategory::TEST:
        return CNA_LOG_CATEGORY_TEST;
    case CNA::LogCategory::GPU:
        return CNA_LOG_CATEGORY_GPU;
    }
    return CNA_LOG_CATEGORY_APPLICATION;
}

// The canonical logger takes a std::string_view, so the borrowed C view is copied into an owned
// std::string first: a view is only valid for the duration of the call, and the copy also runs the
// project's embedded-NUL rejection.
[[nodiscard]] CNA_Result CopyMessage(const CNA_StringView message, std::string* const outValue)
{
    return ReadText(message, "The log message is invalid.", outValue, EmbeddedNul::Refused);
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

// The number of renderers CNA::GraphicsRendererType actually declares, counted through the
// canonical namer rather than written down: it answers "UNKNOWN" for an ordinal past the last
// enumerator, and the enumeration is dense.
[[nodiscard]] consteval std::size_t CanonicalRendererCount()
{
    std::size_t count = 0;
    while (CNA::getGraphicsRendererName(static_cast<CNA::GraphicsRendererType>(count)) !=
           std::string_view{"UNKNOWN"}) {
        ++count;
    }
    return count;
}

// Every public renderer identity, paired explicitly so neither side depends on the other's
// declaration order.
constexpr std::array<std::pair<CNA_GraphicsRendererType, CNA::GraphicsRendererType>, 10>
    RendererIdentities{{
        {CNA_GRAPHICS_RENDERER_OPENGLES3, CNA::GraphicsRendererType::OpenGLES3},
        {CNA_GRAPHICS_RENDERER_OPENGL33, CNA::GraphicsRendererType::OpenGL33},
        {CNA_GRAPHICS_RENDERER_WEBGL2, CNA::GraphicsRendererType::WebGL2},
        {CNA_GRAPHICS_RENDERER_WEBGPU, CNA::GraphicsRendererType::WebGPU},
        {CNA_GRAPHICS_RENDERER_HEADLESS, CNA::GraphicsRendererType::Headless},
        {CNA_GRAPHICS_RENDERER_SOFTWARE, CNA::GraphicsRendererType::Software},
        {CNA_GRAPHICS_RENDERER_STUB, CNA::GraphicsRendererType::Stub},
        {CNA_GRAPHICS_RENDERER_DIRECTX9, CNA::GraphicsRendererType::DirectX9},
        {CNA_GRAPHICS_RENDERER_SDL_GPU, CNA::GraphicsRendererType::SdlGpu},
        {CNA_GRAPHICS_RENDERER_FNA3D, CNA::GraphicsRendererType::Fna3d},
    }};

// A retired identity's numeric value is never reused (see docs/c-api/ABI_VERSIONING.md), so
// removing a renderer leaves a gap instead of renumbering survivors, and MAXIMUM no longer
// equals the surviving count.
[[nodiscard]] consteval CNA_GraphicsRendererType HighestPublishedIdentity()
{
    CNA_GraphicsRendererType highest = CNA_GRAPHICS_RENDERER_UNKNOWN;
    for (const auto& [identity, native] : RendererIdentities) {
        if (identity > highest) {
            highest = identity;
        }
    }
    return highest;
}

// A renderer this table has never heard of resolves to CNA_GRAPHICS_RENDERER_UNKNOWN and is
// refused by every route that takes an identity, which is how three renderers were once
// unreachable from C while the coverage matrix still called them mapped. Neither the table nor
// the published identity range may fall behind the canonical enumeration again.
static_assert(RendererIdentities.size() == CanonicalRendererCount(),
              "A renderer was added to CNA::GraphicsRendererType without a C identity.");
static_assert(CNA_GRAPHICS_RENDERER_MAXIMUM == HighestPublishedIdentity(),
              "CNA_GRAPHICS_RENDERER_MAXIMUM must name the highest-valued published renderer "
              "identity.");

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
    // A retired value is reported as retired, so an old caller learns what happened to its
    // renderer rather than being told it passed something meaningless; nothing aliases it.
    if (const CNA::Internal::RetiredGraphicsRenderer* retired =
            CNA::Internal::FindRetiredGraphicsRendererByValue(type)) {
        return InvalidArgument(
            "The requested renderer value " + std::to_string(retired->value) + " belongs to " +
            std::string(retired->name) +
            ", a graphics renderer CNA has retired; the value stays permanently reserved and no "
            "surviving renderer stands in for it (docs/removed-renderers.md).");
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
    const CNA::GraphicsRendererCategory category) noexcept
{
    switch (category) {
        case CNA::GraphicsRendererCategory::Native:
            return CNA_GRAPHICS_BACKEND_CATEGORY_NATIVE;
        case CNA::GraphicsRendererCategory::TranslationLayer:
            return CNA_GRAPHICS_BACKEND_CATEGORY_TRANSLATION_LAYER;
        case CNA::GraphicsRendererCategory::Software:
            return CNA_GRAPHICS_BACKEND_CATEGORY_SOFTWARE;
        case CNA::GraphicsRendererCategory::Web:
            return CNA_GRAPHICS_BACKEND_CATEGORY_WEB;
        case CNA::GraphicsRendererCategory::Diagnostic:
            return CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC;
    }
    return CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC;
}

[[nodiscard]] CNA_Result MapBackendCategory(
    const CNA_GraphicsBackendCategory category,
    CNA::GraphicsRendererCategory* const outCategory)
{
    switch (category) {
        case CNA_GRAPHICS_BACKEND_CATEGORY_NATIVE:
            *outCategory = CNA::GraphicsRendererCategory::Native;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_CATEGORY_TRANSLATION_LAYER:
            *outCategory = CNA::GraphicsRendererCategory::TranslationLayer;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_CATEGORY_SOFTWARE:
            *outCategory = CNA::GraphicsRendererCategory::Software;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_CATEGORY_WEB:
            *outCategory = CNA::GraphicsRendererCategory::Web;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC:
            *outCategory = CNA::GraphicsRendererCategory::Diagnostic;
            return CNA_RESULT_SUCCESS;
        default:
            return InvalidArgument(
                "The requested backend category is not a canonical identity.");
    }
}

[[nodiscard]] CNA_GraphicsBackendMaturity MapBackendMaturityToC(
    const CNA::GraphicsRendererMaturity maturity) noexcept
{
    switch (maturity) {
        case CNA::GraphicsRendererMaturity::Production:
            return CNA_GRAPHICS_BACKEND_MATURITY_PRODUCTION;
        case CNA::GraphicsRendererMaturity::Supported:
            return CNA_GRAPHICS_BACKEND_MATURITY_SUPPORTED;
        case CNA::GraphicsRendererMaturity::Experimental:
            return CNA_GRAPHICS_BACKEND_MATURITY_EXPERIMENTAL;
        case CNA::GraphicsRendererMaturity::Historical:
            return CNA_GRAPHICS_BACKEND_MATURITY_HISTORICAL;
        case CNA::GraphicsRendererMaturity::Deprecated:
            return CNA_GRAPHICS_BACKEND_MATURITY_DEPRECATED;
    }
    return CNA_GRAPHICS_BACKEND_MATURITY_EXPERIMENTAL;
}

[[nodiscard]] CNA_Result MapBackendMaturity(
    const CNA_GraphicsBackendMaturity maturity,
    CNA::GraphicsRendererMaturity* const outMaturity)
{
    switch (maturity) {
        case CNA_GRAPHICS_BACKEND_MATURITY_PRODUCTION:
            *outMaturity = CNA::GraphicsRendererMaturity::Production;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_MATURITY_SUPPORTED:
            *outMaturity = CNA::GraphicsRendererMaturity::Supported;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_MATURITY_EXPERIMENTAL:
            *outMaturity = CNA::GraphicsRendererMaturity::Experimental;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_MATURITY_HISTORICAL:
            *outMaturity = CNA::GraphicsRendererMaturity::Historical;
            return CNA_RESULT_SUCCESS;
        case CNA_GRAPHICS_BACKEND_MATURITY_DEPRECATED:
            *outMaturity = CNA::GraphicsRendererMaturity::Deprecated;
            return CNA_RESULT_SUCCESS;
        default:
            return InvalidArgument(
                "The requested backend maturity is not a canonical identity.");
    }
}

[[nodiscard]] CNA_Result LoggerLog(
    const CNA_LogLevel level,
    const CNA_StringView message,
    const CNA_LogCategory category,
    const CNA_Bool condition)
{
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
}

[[nodiscard]] CNA_Result PlatformGetCurrent(CNA_Platform* const outPlatform)
{
    switch (CNA::getCurrentPlatform()) {
        case CNA::TargetPlatform::Desktop: *outPlatform = CNA_PLATFORM_DESKTOP; break;
        case CNA::TargetPlatform::Android: *outPlatform = CNA_PLATFORM_ANDROID; break;
        case CNA::TargetPlatform::iOS: *outPlatform = CNA_PLATFORM_IOS; break;
        case CNA::TargetPlatform::Web: *outPlatform = CNA_PLATFORM_WEB; break;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result DesktopOsGetCurrent(CNA_DesktopOS* const outOs)
{
    switch (CNA::getCurrentDesktopOS()) {
        case CNA::DesktopOS::Windows: *outOs = CNA_DESKTOP_OS_WINDOWS; break;
        case CNA::DesktopOS::Linux: *outOs = CNA_DESKTOP_OS_LINUX; break;
        case CNA::DesktopOS::MacOSX: *outOs = CNA_DESKTOP_OS_MACOSX; break;
        case CNA::DesktopOS::Other: *outOs = CNA_DESKTOP_OS_OTHER; break;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsBackendCategoryGetNameSize(
    const CNA_GraphicsBackendCategory category,
    uint64_t* const outBytes)
{
    CNA::GraphicsRendererCategory nativeCategory = CNA::GraphicsRendererCategory::Native;
    if (const CNA_Result result = MapBackendCategory(category, &nativeCategory);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return ReportTextSize(CNA::toStringView(nativeCategory), outBytes);
}

[[nodiscard]] CNA_Result GraphicsBackendMaturityGetNameSize(
    const CNA_GraphicsBackendMaturity maturity,
    uint64_t* const outBytes)
{
    CNA::GraphicsRendererMaturity nativeMaturity = CNA::GraphicsRendererMaturity::Production;
    if (const CNA_Result result = MapBackendMaturity(maturity, &nativeMaturity);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return ReportTextSize(CNA::toStringView(nativeMaturity), outBytes);
}

// The fallback reasons, paired explicitly for the same reason the renderer identities are: neither
// side may depend on the other's declaration order.
constexpr std::array<std::pair<CNA_GraphicsRendererFallbackReason,
                               CNA::GraphicsRendererFallbackReason>, 4>
    FallbackReasons{{
        {CNA_GRAPHICS_RENDERER_FALLBACK_NOT_COMPILED_IN,
         CNA::GraphicsRendererFallbackReason::NotCompiledIn},
        {CNA_GRAPHICS_RENDERER_FALLBACK_PROBE_UNAVAILABLE,
         CNA::GraphicsRendererFallbackReason::ProbeUnavailable},
        {CNA_GRAPHICS_RENDERER_FALLBACK_INITIALIZATION_FAILED,
         CNA::GraphicsRendererFallbackReason::InitializationFailed},
        {CNA_GRAPHICS_RENDERER_FALLBACK_WINDOW_KIND_CONFLICT,
         CNA::GraphicsRendererFallbackReason::WindowKindConflict},
    }};

[[nodiscard]] CNA_Result MapFallbackReason(
    const CNA_GraphicsRendererFallbackReason reason,
    CNA::GraphicsRendererFallbackReason* const outReason)
{
    for (const auto& [identity, native] : FallbackReasons) {
        if (identity == reason) {
            *outReason = native;
            return CNA_RESULT_SUCCESS;
        }
    }
    return InvalidArgument("The fallback reason is not a defined identity.");
}

[[nodiscard]] CNA_GraphicsRendererFallbackReason MapFallbackReasonToC(
    const CNA::GraphicsRendererFallbackReason reason) noexcept
{
    for (const auto& [identity, native] : FallbackReasons) {
        if (native == reason) {
            return identity;
        }
    }
    return CNA_GRAPHICS_RENDERER_FALLBACK_NOT_COMPILED_IN;
}

// The history is a span the selection owns; a record is read by index so no C caller ever holds a
// pointer into it.
[[nodiscard]] CNA_Result BorrowFallbackRecord(
    const uint64_t index,
    const CNA::GraphicsRendererFallbackRecord** const outRecord)
{
    const std::span<const CNA::GraphicsRendererFallbackRecord> history =
        CNA::GraphicsRendererSelection::GetFallbackHistory();
    if (index >= history.size()) {
        return InvalidArgument("The fallback record position is outside the recorded history.");
    }
    *outRecord = &history[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

// The installed C sink, kept beside its context so both are replaced together. A raw pointer pair
// rather than a std::function member: the callback is a C function pointer and the context is the
// caller's, and neither is owned here.
struct LogSinkState final {
    std::mutex mutex;
    CNA_LogSinkCallback callback = nullptr;
    void* context = nullptr;
};

LogSinkState& LogSink()
{
    static LogSinkState state;
    return state;
}

[[nodiscard]] CNA_Result LoggerSetSinkExt(const CNA_LogSinkCallback callback, void* const context)
{
    LogSinkState& state = LogSink();
    {
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.callback = callback;
        state.context = context;
    }
    if (callback == nullptr) {
        CNA::Logger::ResetSink();
        return CNA_RESULT_SUCCESS;
    }
    // The lambda reads the state rather than capturing the callback, so replacing the sink
    // takes effect for a line already in flight on another thread rather than leaving the old
    // pointer live inside a std::function CNA still holds.
    CNA::Logger::SetSink([](const CNA::LogLevel level,
                            const CNA::LogCategory category,
                            const std::string_view formattedMessage) {
        LogSinkState& current = LogSink();
        CNA_LogSinkCallback installed = nullptr;
        void* installedContext = nullptr;
        {
            const std::lock_guard<std::mutex> lock(current.mutex);
            installed = current.callback;
            installedContext = current.context;
        }
        if (installed == nullptr) {
            return;
        }
        const CNA_StringView view{
            formattedMessage.empty() ? nullptr : formattedMessage.data(),
            static_cast<uint64_t>(formattedMessage.size())};
        installed(MapLogLevelToC(level), MapLogCategoryToC(category), view, installedContext);
    });
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result LoggerResetSinkExt(void)
{
    LogSinkState& state = LogSink();
    {
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.callback = nullptr;
        state.context = nullptr;
    }
    CNA::Logger::ResetSink();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsRendererCopyAvailableExt(
    CNA_GraphicsRendererType* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidArgument("The renderer identity output is invalid.");
    }
    const std::span<const CNA::GraphicsRendererType> available =
        CNA::GraphicsRendererSelection::GetAvailable();
    *outCount = static_cast<uint64_t>(available.size());
    if (capacity < available.size()) {
        return BufferTooSmall(
            "The destination capacity is smaller than the available renderer set.");
    }
    for (std::size_t index = 0U; index < available.size(); ++index) {
        destination[index] = MapRendererTypeToC(available[index]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsRendererSetFallbackChainExt(
    const CNA_GraphicsRendererType* const types,
    const uint64_t count)
{
    std::size_t ignoredByteCount = 0U;
    if (const CNA_Result result = CNA::C::Detail::CheckedElementByteCount(
            types, count, sizeof(CNA_GraphicsRendererType), &ignoredByteCount);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The renderer fallback chain array is invalid.");
    }
    // Every identity is validated before the chain is handed over, so a chain with one bad
    // entry changes nothing rather than being applied up to the mistake.
    std::vector<CNA::GraphicsRendererType> chain;
    chain.reserve(static_cast<std::size_t>(count));
    for (uint64_t index = 0U; index < count; ++index) {
        CNA::GraphicsRendererType nativeType = CNA::GraphicsRendererType::Headless;
        if (const CNA_Result result = MapRendererType(types[index], &nativeType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        chain.push_back(nativeType);
    }
    CNA::GraphicsRendererSelection::SetFallbackChain(chain);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsRendererGetFallbackAtExt(
    const uint64_t index,
    CNA_GraphicsRendererFallbackRecord* const outRecord)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(outRecord, "The fallback record structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const CNA::GraphicsRendererFallbackRecord* record = nullptr;
    if (const CNA_Result result = BorrowFallbackRecord(index, &record);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    outRecord->type = MapRendererTypeToC(record->type);
    outRecord->reason = MapFallbackReasonToC(record->reason);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsRendererTryParseNameExt(
    const CNA_StringView name,
    CNA_GraphicsRendererType* const outType,
    CNA_Bool* const outRecognized)
{
    *outType = CNA_GRAPHICS_RENDERER_UNKNOWN;
    *outRecognized = CNA_FALSE;
    std::string text;
    if (const CNA_Result result = CNA::C::Detail::ReadText(
            name, "The renderer name is not valid UTF-8 text.", &text,
            CNA::C::Detail::EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    CNA::GraphicsRendererType nativeType = CNA::GraphicsRendererType::Headless;
    // An unrecognized name is an answer, not a failure, exactly as the canonical route reports
    // it: the caller asked whether this is a renderer name and gets told.
    if (!CNA::tryParseGraphicsRendererName(std::string_view(text), nativeType)) {
        return CNA_RESULT_SUCCESS;
    }
    *outType = MapRendererTypeToC(nativeType);
    *outRecognized = CNA_TRUE;
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/CoreExt.toml.
#include "generated/CoreExt.inc"
