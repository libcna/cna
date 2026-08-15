// SPDX-License-Identifier: MS-PL

#include "CNA/C/devices.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Devices/Detail/IVibrateBackend.hpp"
#include "Microsoft/Devices/VibrateController.hpp"
#include "System/TimeSpan.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef CNA_DEVICES
#include "CNA/Devices/Clipboard.hpp"
#include "CNA/Devices/Detail/IFileDialogBackend.hpp"
#include "CNA/Devices/Detail/IMessageBoxBackend.hpp"
#include "CNA/Devices/Detail/ITrayBackend.hpp"
#include "CNA/Devices/DisplayInfo.hpp"
#include "CNA/Devices/FileDialog.hpp"
#include "CNA/Devices/FileDialogFilter.hpp"
#include "CNA/Devices/Locale.hpp"
#include "CNA/Devices/LocaleInfo.hpp"
#include "CNA/Devices/MessageBox.hpp"
#include "CNA/Devices/MessageBoxType.hpp"
#include "CNA/Devices/PowerInfo.hpp"
#include "CNA/Devices/PowerState.hpp"
#include "CNA/Devices/SystemInfo.hpp"
#include "CNA/Devices/SystemTray.hpp"
#include "CNA/Devices/UrlLauncher.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <functional>
#endif

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using Microsoft::Devices::VibrateController;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

#ifndef CNA_DEVICES
[[nodiscard]] CNA_Result ExtensionUnavailable()
{
    return Fail(
        CNA_RESULT_NOT_SUPPORTED,
        CNA_ERROR_CATEGORY_NOT_SUPPORTED,
        "This CNA build does not contain the extended device layer.");
}
#endif

[[nodiscard]] CNA_Result NoTestBackend()
{
    return Fail(
        CNA_RESULT_INVALID_STATE,
        CNA_ERROR_CATEGORY_STATE,
        "No test backend is installed for this device.");
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The device text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the device text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowText(
    const CNA_StringView view,
    const char* const message,
    std::string* const outText)
{
    if (const CNA_Result result = CNA::C::Detail::CopyStringView(view, false, outText);
        result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), message);
    }
    return CNA_RESULT_SUCCESS;
}

// The canonical vibration hook takes a caller-implemented backend object; C cannot write one, so
// this ABI supplies it and records what it was asked to do. The state lives beside the backend so a
// query still works after the canonical controller has replaced or destroyed the backend itself.
struct VibrationTestState final {
    std::mutex mutex;
    bool supported = false;
    std::string deviceName;
    uint32_t startCalls = 0U;
    uint32_t stopCalls = 0U;
    uint32_t leftRightCalls = 0U;
    int64_t lastDurationTicks = 0;
    float lastIntensity = 0.0F;
    float lastLargeMotor = 0.0F;
    float lastSmallMotor = 0.0F;
};

class TestVibrateBackend final : public Microsoft::Devices::Detail::IVibrateBackend {
public:
    explicit TestVibrateBackend(std::shared_ptr<VibrationTestState> state)
        : state_(std::move(state))
    {
    }

    void Start(const System::TimeSpan& duration, const float intensity) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->startCalls;
        state_->lastDurationTicks = static_cast<int64_t>(duration.getTicksProperty());
        state_->lastIntensity = intensity;
    }

    void Stop() override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->stopCalls;
    }

    [[nodiscard]] bool IsSupported() override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->supported;
    }

    [[nodiscard]] std::string GetDeviceName() override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->deviceName;
    }

    void StartLeftRight(
        const float largeMotor,
        const float smallMotor,
        const System::TimeSpan& duration) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->leftRightCalls;
        state_->lastLargeMotor = largeMotor;
        state_->lastSmallMotor = smallMotor;
        state_->lastDurationTicks = static_cast<int64_t>(duration.getTicksProperty());
    }

private:
    std::shared_ptr<VibrationTestState> state_;
};

std::mutex& VibrationTestMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<VibrationTestState>& VibrationTestStorage()
{
    static std::shared_ptr<VibrationTestState> state;
    return state;
}

[[nodiscard]] std::shared_ptr<VibrationTestState> InstalledVibrationTestState()
{
    const std::lock_guard<std::mutex> lock(VibrationTestMutex());
    return VibrationTestStorage();
}

[[nodiscard]] System::TimeSpan ToDuration(const int64_t ticks)
{
    return System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks));
}

#ifdef CNA_DEVICES

using CNA::Devices::Clipboard;
using CNA::Devices::DisplayInfo;
using CNA::Devices::FileDialog;
using CNA::Devices::FileDialogFilter;
using CNA::Devices::Locale;
using CNA::Devices::LocaleInfo;
using CNA::Devices::MessageBox;
using CNA::Devices::MessageBoxType;
using CNA::Devices::PowerInfo;
using CNA::Devices::SystemInfo;
using CNA::Devices::SystemTray;
using CNA::Devices::UrlLauncher;

// A message box waits for a person, so no automated caller can complete a real one: the test backend
// answers immediately and records the request instead.
struct MessageBoxTestState final {
    std::mutex mutex;
    int chosenButton = 0;
    uint32_t simpleCalls = 0U;
    uint32_t choiceCalls = 0U;
    MessageBoxType lastType = MessageBoxType::Error;
    uint32_t lastButtonCount = 0U;
};

class TestMessageBoxBackend final : public CNA::Devices::Detail::IMessageBoxBackend {
public:
    explicit TestMessageBoxBackend(std::shared_ptr<MessageBoxTestState> state)
        : state_(std::move(state))
    {
    }

    void ShowSimple(const MessageBoxType type, const std::string&, const std::string&) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->simpleCalls;
        state_->lastType = type;
    }

    int Show(
        const MessageBoxType type,
        const std::string&,
        const std::string&,
        const std::vector<std::string>& buttonLabels) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->choiceCalls;
        state_->lastType = type;
        state_->lastButtonCount = static_cast<uint32_t>(buttonLabels.size());
        return state_->chosenButton;
    }

private:
    std::shared_ptr<MessageBoxTestState> state_;
};

std::mutex& MessageBoxTestMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<MessageBoxTestState>& MessageBoxTestStorage()
{
    static std::shared_ptr<MessageBoxTestState> state;
    return state;
}

// A real file dialog is asynchronous and driven by a person; the test backend answers the callback
// before returning, with whatever result the switch was given.
struct FileDialogTestState final {
    std::mutex mutex;
    std::vector<std::string> results;
};

class TestFileDialogBackend final : public CNA::Devices::Detail::IFileDialogBackend {
public:
    explicit TestFileDialogBackend(std::shared_ptr<FileDialogTestState> state)
        : state_(std::move(state))
    {
    }

    void ShowOpenFile(
        CNA::Devices::Detail::FileDialogResultCallback onResult,
        const std::vector<FileDialogFilter>&,
        const std::string&,
        bool) override
    {
        Answer(std::move(onResult));
    }

    void ShowSaveFile(
        CNA::Devices::Detail::FileDialogResultCallback onResult,
        const std::vector<FileDialogFilter>&,
        const std::string&) override
    {
        Answer(std::move(onResult));
    }

    void ShowOpenFolder(
        CNA::Devices::Detail::FileDialogResultCallback onResult,
        const std::string&,
        bool) override
    {
        Answer(std::move(onResult));
    }

private:
    void Answer(CNA::Devices::Detail::FileDialogResultCallback onResult)
    {
        std::vector<std::string> results;
        {
            const std::lock_guard<std::mutex> lock(state_->mutex);
            results = state_->results;
        }
        if (onResult) {
            onResult(results);
        }
    }

    std::shared_ptr<FileDialogTestState> state_;
};

std::mutex& FileDialogTestMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<FileDialogTestState>& FileDialogTestStorage()
{
    static std::shared_ptr<FileDialogTestState> state;
    return state;
}

// The canonical tray takes its backend as a constructor argument rather than through a switch, so
// this state belongs to one tray handle instead of the process.
struct TrayTestEntry final {
    std::string label;
    bool checkable = false;
    bool checked = false;
    bool enabled = true;
    CNA::Devices::Detail::TrayEntryClickCallback onClick;
};

struct TrayTestState final {
    std::mutex mutex;
    std::string tooltip;
    bool created = false;
    std::vector<TrayTestEntry> entries;
};

class TestTrayBackend final : public CNA::Devices::Detail::ITrayBackend {
public:
    explicit TestTrayBackend(std::shared_ptr<TrayTestState> state)
        : state_(std::move(state))
    {
    }

    void Create(const std::string& tooltip) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        state_->created = true;
        state_->tooltip = tooltip;
    }

    void Destroy() override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        state_->created = false;
        state_->entries.clear();
    }

    void SetTooltip(const std::string& tooltip) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        state_->tooltip = tooltip;
    }

    std::size_t AddEntry(
        const std::string& label,
        const bool checkable,
        const bool initiallyChecked,
        const bool initiallyEnabled,
        CNA::Devices::Detail::TrayEntryClickCallback onClick) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        TrayTestEntry entry;
        entry.label = label;
        entry.checkable = checkable;
        entry.checked = initiallyChecked;
        entry.enabled = initiallyEnabled;
        entry.onClick = std::move(onClick);
        state_->entries.push_back(std::move(entry));
        return state_->entries.size() - 1U;
    }

    // Every entry mutator ignores an index past the last entry and every reader answers false for
    // one, which is the platform backend's own behavior rather than a refusal invented here.
    void SetEntryLabel(const std::size_t index, const std::string& label) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        if (index < state_->entries.size()) {
            state_->entries[index].label = label;
        }
    }

    void SetEntryChecked(const std::size_t index, const bool checked) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        if (index < state_->entries.size()) {
            state_->entries[index].checked = checked;
        }
    }

    [[nodiscard]] bool GetEntryChecked(const std::size_t index) const override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return index < state_->entries.size() && state_->entries[index].checked;
    }

    void SetEntryEnabled(const std::size_t index, const bool enabled) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        if (index < state_->entries.size()) {
            state_->entries[index].enabled = enabled;
        }
    }

    [[nodiscard]] bool GetEntryEnabled(const std::size_t index) const override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return index < state_->entries.size() && state_->entries[index].enabled;
    }

private:
    std::shared_ptr<TrayTestState> state_;
};

struct SystemTrayResource final {
    std::unique_ptr<SystemTray> value;
    std::shared_ptr<TrayTestState> testState;
};

[[nodiscard]] CNA_Result BorrowTray(
    const CNA_Handle handle,
    std::shared_ptr<SystemTrayResource>* const outTray)
{
    const CNA_Result result =
        CNA::C::Detail::GetRuntimeHandles().Get(handle, ObjectKind::SystemTray, outTray);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The system tray handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToMessageBoxType(const CNA_MessageBoxType value, MessageBoxType* const out)
{
    if (value > CNA_MESSAGE_BOX_TYPE_MAXIMUM) {
        return InvalidInput("The message box severity is not a defined identity.");
    }
    *out = static_cast<MessageBoxType>(value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CollectFilters(
    const CNA_FileDialogFilter* const filters,
    const uint64_t filterCount,
    std::vector<FileDialogFilter>* const outFilters)
{
    if (filterCount != UINT64_C(0) && filters == nullptr) {
        return InvalidInput("The file dialog filter array is null.");
    }
    for (uint64_t index = UINT64_C(0); index < filterCount; ++index) {
        const CNA_FileDialogFilter& filter = filters[index];
        if (filter.struct_size < sizeof(CNA_FileDialogFilter) ||
            filter.struct_version != StructureVersion) {
            return InvalidInput("A file dialog filter is not a valid structure.");
        }
        FileDialogFilter mapped;
        if (const CNA_Result result =
                BorrowText(filter.name, "A file dialog filter name is not valid UTF-8.", &mapped.Name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowText(
                filter.pattern,
                "A file dialog filter pattern is not valid UTF-8.",
                &mapped.Pattern);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        outFilters->push_back(std::move(mapped));
    }
    return CNA_RESULT_SUCCESS;
}

// The canonical result is a vector of owned strings and the C handler takes borrowed views, so the
// views are built over the canonical strings and stay valid exactly for the duration of the call.
void DeliverFileDialogResult(
    const std::vector<std::string>& files,
    const CNA_FileDialogResultCallback callback,
    void* const context)
{
    std::vector<CNA_StringView> views;
    views.reserve(files.size());
    for (const std::string& file : files) {
        CNA_StringView view = {};
        view.data = file.data();
        view.byte_length = file.size();
        views.push_back(view);
    }
    callback(views.empty() ? nullptr : views.data(), static_cast<uint64_t>(views.size()), context);
}

[[nodiscard]] CNA_Result CollectLocales(std::vector<LocaleInfo>* const outLocales)
{
    *outLocales = Locale::getPreferredLocalesProperty();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowLocale(
    const CNA_Handle gameHandle,
    const uint64_t index,
    LocaleInfo* const outLocale)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::vector<LocaleInfo> locales;
    if (const CNA_Result result = CollectLocales(&locales); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (index >= static_cast<uint64_t>(locales.size())) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The locale index is at or past the reported count.");
    }
    *outLocale = locales[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowWindow(
    const CNA_Handle gameHandle,
    Microsoft::Xna::Framework::GameWindow** const outWindow)
{
    return CNA::C::Detail::GetGameWindow(gameHandle, outWindow);
}

#endif // CNA_DEVICES

} // namespace

CNA_Result cna_devices_ext_is_available(CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAvailable == nullptr) {
            return InvalidInput("The device extension availability output is null.");
        }
#ifdef CNA_DEVICES
        *outAvailable = CNA_TRUE;
#else
        *outAvailable = CNA_FALSE;
#endif
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vibrate_controller_start(const CNA_Handle gameHandle, const int64_t durationTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        VibrateController::getDefaultProperty()->Start(ToDuration(durationTicks));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vibrate_controller_start_with_intensity_ext(
    const CNA_Handle gameHandle,
    const int64_t durationTicks,
    const float intensity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        VibrateController::getDefaultProperty()->Start(ToDuration(durationTicks), intensity);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vibrate_controller_start_left_right_ext(
    const CNA_Handle gameHandle,
    const float largeMotor,
    const float smallMotor,
    const int64_t durationTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        VibrateController::getDefaultProperty()->StartLeftRight(
            largeMotor,
            smallMotor,
            ToDuration(durationTicks));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vibrate_controller_stop(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        VibrateController::getDefaultProperty()->Stop();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vibrate_controller_get_is_supported_ext(
    const CNA_Handle gameHandle,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidInput("The vibration support output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported =
            VibrateController::getDefaultProperty()->getIsSupportedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vibrate_controller_get_device_name_size_ext(
    const CNA_Handle gameHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The device name size output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = VibrateController::getDefaultProperty()->getDeviceNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vibrate_controller_copy_device_name_ext(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            VibrateController::getDefaultProperty()->getDeviceNameProperty(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_vibrate_controller_set_test_backend_ext(
    const CNA_Handle gameHandle,
    const CNA_Bool installed,
    const CNA_Bool supported,
    const CNA_StringView deviceName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (installed == CNA_FALSE) {
            VibrateController::getDefaultProperty()->SetBackendForTesting(nullptr);
            const std::lock_guard<std::mutex> lock(VibrationTestMutex());
            VibrationTestStorage().reset();
            return CNA_RESULT_SUCCESS;
        }
        auto state = std::make_shared<VibrationTestState>();
        state->supported = (supported != CNA_FALSE);
        if (const CNA_Result result = BorrowText(
                deviceName,
                "The vibration device name is not valid UTF-8.",
                &state->deviceName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        VibrateController::getDefaultProperty()->SetBackendForTesting(
            std::make_unique<TestVibrateBackend>(state));
        const std::lock_guard<std::mutex> lock(VibrationTestMutex());
        VibrationTestStorage() = std::move(state);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vibrate_controller_get_test_log_ext(
    const CNA_Handle gameHandle,
    CNA_VibrationTestLog* const outLog)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLog == nullptr) {
            return InvalidInput("The vibration test log output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::shared_ptr<VibrationTestState> state = InstalledVibrationTestState();
        if (!state) {
            return NoTestBackend();
        }
        CNA_VibrationTestLog log = {};
        log.struct_size = sizeof(CNA_VibrationTestLog);
        log.struct_version = StructureVersion;
        {
            const std::lock_guard<std::mutex> lock(state->mutex);
            log.start_calls = state->startCalls;
            log.stop_calls = state->stopCalls;
            log.left_right_calls = state->leftRightCalls;
            log.last_duration_ticks = state->lastDurationTicks;
            log.last_intensity = state->lastIntensity;
            log.last_large_motor = state->lastLargeMotor;
            log.last_small_motor = state->lastSmallMotor;
        }
        *outLog = log;
        return CNA_RESULT_SUCCESS;
    });
}

#ifdef CNA_DEVICES

CNA_Result cna_power_get_state_ext(const CNA_Handle gameHandle, CNA_PowerState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The power state output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = static_cast<CNA_PowerState>(PowerInfo::getStateProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_power_get_battery_percent_ext(
    const CNA_Handle gameHandle,
    int32_t* const outPercent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPercent == nullptr) {
            return InvalidInput("The battery percentage output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPercent = static_cast<int32_t>(PowerInfo::getBatteryPercentProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_power_get_seconds_remaining_ext(
    const CNA_Handle gameHandle,
    int32_t* const outSeconds)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSeconds == nullptr) {
            return InvalidInput("The battery seconds output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSeconds = static_cast<int32_t>(PowerInfo::getSecondsRemainingProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_info_get_logical_cpu_core_count_ext(
    const CNA_Handle gameHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The core count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(SystemInfo::getLogicalCpuCoreCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_info_get_system_ram_megabytes_ext(
    const CNA_Handle gameHandle,
    int32_t* const outMegabytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMegabytes == nullptr) {
            return InvalidInput("The system memory output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outMegabytes = static_cast<int32_t>(SystemInfo::getSystemRamMegabytesProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_locale_get_preferred_count_ext(
    const CNA_Handle gameHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The locale count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<LocaleInfo> locales;
        if (const CNA_Result result = CollectLocales(&locales); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(locales.size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_locale_get_language_size_at_ext(
    const CNA_Handle gameHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The locale language size output is null.");
        }
        LocaleInfo locale;
        if (const CNA_Result result = BorrowLocale(gameHandle, index, &locale);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = locale.Language.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_locale_copy_language_at_ext(
    const CNA_Handle gameHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        LocaleInfo locale;
        if (const CNA_Result result = BorrowLocale(gameHandle, index, &locale);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(locale.Language, destination, capacity, outBytes);
    });
}

CNA_Result cna_locale_get_country_size_at_ext(
    const CNA_Handle gameHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The locale country size output is null.");
        }
        LocaleInfo locale;
        if (const CNA_Result result = BorrowLocale(gameHandle, index, &locale);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = locale.Country.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_locale_copy_country_at_ext(
    const CNA_Handle gameHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        LocaleInfo locale;
        if (const CNA_Result result = BorrowLocale(gameHandle, index, &locale);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(locale.Country, destination, capacity, outBytes);
    });
}

CNA_Result cna_display_info_get_content_scale_ext(
    const CNA_Handle gameHandle,
    float* const outScale)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outScale == nullptr) {
            return InvalidInput("The content scale output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outScale = DisplayInfo::getContentScaleProperty(*window);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_display_info_get_safe_area_ext(
    const CNA_Handle gameHandle,
    CNA_Rectangle* const outArea)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outArea == nullptr) {
            return InvalidInput("The safe area output is null.");
        }
        Microsoft::Xna::Framework::GameWindow* window = nullptr;
        if (const CNA_Result result = BorrowWindow(gameHandle, &window);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const Microsoft::Xna::Framework::Rectangle area = DisplayInfo::getSafeAreaProperty(*window);
        CNA_Rectangle mapped = {};
        mapped.x = area.X;
        mapped.y = area.Y;
        mapped.width = area.Width;
        mapped.height = area.Height;
        *outArea = mapped;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_devices_clipboard_set_text_ext(
    const CNA_Handle gameHandle,
    const CNA_StringView text,
    CNA_Bool* const outAccepted)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAccepted == nullptr) {
            return InvalidInput("The clipboard acceptance output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string value;
        if (const CNA_Result result =
                BorrowText(text, "The clipboard text is not valid UTF-8.", &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outAccepted = Clipboard::setTextProperty(value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_url_launcher_open_ext(
    const CNA_Handle gameHandle,
    const CNA_StringView url,
    CNA_Bool* const outOpened)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOpened == nullptr) {
            return InvalidInput("The URL acceptance output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string value;
        if (const CNA_Result result = BorrowText(url, "The URL is not valid UTF-8.", &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (value.empty()) {
            return InvalidInput("The URL is empty.");
        }
        *outOpened = UrlLauncher::Open(value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_message_box_get_is_supported_ext(
    const CNA_Handle gameHandle,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidInput("The message box support output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = MessageBox::getIsSupportedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_message_box_show_simple_ext(
    const CNA_Handle gameHandle,
    const CNA_MessageBoxType type,
    const CNA_StringView title,
    const CNA_StringView message)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        MessageBoxType severity = MessageBoxType::Error;
        if (const CNA_Result result = ToMessageBoxType(type, &severity);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string titleText;
        std::string messageText;
        if (const CNA_Result result =
                BorrowText(title, "The message box title is not valid UTF-8.", &titleText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                BorrowText(message, "The message box body is not valid UTF-8.", &messageText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        MessageBox::ShowSimple(severity, titleText, messageText);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_message_box_show_ext(
    const CNA_Handle gameHandle,
    const CNA_MessageBoxType type,
    const CNA_StringView title,
    const CNA_StringView message,
    const CNA_StringView* const buttonLabels,
    const uint64_t buttonCount,
    int32_t* const outChosen)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outChosen == nullptr) {
            return InvalidInput("The chosen button output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        MessageBoxType severity = MessageBoxType::Error;
        if (const CNA_Result result = ToMessageBoxType(type, &severity);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (buttonCount == UINT64_C(0) || buttonLabels == nullptr) {
            return InvalidInput("A message box with buttons needs at least one label.");
        }
        std::string titleText;
        std::string messageText;
        if (const CNA_Result result =
                BorrowText(title, "The message box title is not valid UTF-8.", &titleText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                BorrowText(message, "The message box body is not valid UTF-8.", &messageText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<std::string> labels;
        labels.reserve(static_cast<std::size_t>(buttonCount));
        for (uint64_t index = UINT64_C(0); index < buttonCount; ++index) {
            std::string label;
            if (const CNA_Result result = BorrowText(
                    buttonLabels[index],
                    "A message box button label is not valid UTF-8.",
                    &label);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            labels.push_back(std::move(label));
        }
        *outChosen = static_cast<int32_t>(MessageBox::Show(severity, titleText, messageText, labels));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_message_box_set_test_backend_ext(
    const CNA_Handle gameHandle,
    const CNA_Bool installed,
    const int32_t chosenButton)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (installed == CNA_FALSE) {
            MessageBox::SetBackendForTesting(nullptr);
            const std::lock_guard<std::mutex> lock(MessageBoxTestMutex());
            MessageBoxTestStorage().reset();
            return CNA_RESULT_SUCCESS;
        }
        auto state = std::make_shared<MessageBoxTestState>();
        state->chosenButton = static_cast<int>(chosenButton);
        MessageBox::SetBackendForTesting(std::make_unique<TestMessageBoxBackend>(state));
        const std::lock_guard<std::mutex> lock(MessageBoxTestMutex());
        MessageBoxTestStorage() = std::move(state);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_message_box_get_test_log_ext(
    const CNA_Handle gameHandle,
    CNA_MessageBoxTestLog* const outLog)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLog == nullptr) {
            return InvalidInput("The message box test log output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<MessageBoxTestState> state;
        {
            const std::lock_guard<std::mutex> lock(MessageBoxTestMutex());
            state = MessageBoxTestStorage();
        }
        if (!state) {
            return NoTestBackend();
        }
        CNA_MessageBoxTestLog log = {};
        log.struct_size = sizeof(CNA_MessageBoxTestLog);
        log.struct_version = StructureVersion;
        {
            const std::lock_guard<std::mutex> lock(state->mutex);
            log.simple_calls = state->simpleCalls;
            log.choice_calls = state->choiceCalls;
            log.last_type = static_cast<CNA_MessageBoxType>(state->lastType);
            log.last_button_count = state->lastButtonCount;
        }
        *outLog = log;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_file_dialog_get_is_supported_ext(
    const CNA_Handle gameHandle,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidInput("The file dialog support output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = FileDialog::getIsSupportedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_file_dialog_show_open_file_ext(
    const CNA_Handle gameHandle,
    const CNA_FileDialogResultCallback onResult,
    void* const context,
    const CNA_FileDialogFilter* const filters,
    const uint64_t filterCount,
    const CNA_StringView defaultLocation,
    const CNA_Bool allowMultiple)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (onResult == nullptr) {
            return InvalidInput("The file dialog result handler is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<FileDialogFilter> mappedFilters;
        if (const CNA_Result result = CollectFilters(filters, filterCount, &mappedFilters);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string location;
        if (const CNA_Result result = BorrowText(
                defaultLocation,
                "The file dialog default location is not valid UTF-8.",
                &location);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        FileDialog::ShowOpenFile(
            [onResult, context](const std::vector<std::string>& files) {
                DeliverFileDialogResult(files, onResult, context);
            },
            mappedFilters,
            location,
            allowMultiple != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_file_dialog_show_save_file_ext(
    const CNA_Handle gameHandle,
    const CNA_FileDialogResultCallback onResult,
    void* const context,
    const CNA_FileDialogFilter* const filters,
    const uint64_t filterCount,
    const CNA_StringView defaultLocation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (onResult == nullptr) {
            return InvalidInput("The file dialog result handler is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<FileDialogFilter> mappedFilters;
        if (const CNA_Result result = CollectFilters(filters, filterCount, &mappedFilters);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string location;
        if (const CNA_Result result = BorrowText(
                defaultLocation,
                "The file dialog default location is not valid UTF-8.",
                &location);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        FileDialog::ShowSaveFile(
            [onResult, context](const std::vector<std::string>& files) {
                DeliverFileDialogResult(files, onResult, context);
            },
            mappedFilters,
            location);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_file_dialog_show_open_folder_ext(
    const CNA_Handle gameHandle,
    const CNA_FileDialogResultCallback onResult,
    void* const context,
    const CNA_StringView defaultLocation,
    const CNA_Bool allowMultiple)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (onResult == nullptr) {
            return InvalidInput("The file dialog result handler is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string location;
        if (const CNA_Result result = BorrowText(
                defaultLocation,
                "The file dialog default location is not valid UTF-8.",
                &location);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        FileDialog::ShowOpenFolder(
            [onResult, context](const std::vector<std::string>& files) {
                DeliverFileDialogResult(files, onResult, context);
            },
            location,
            allowMultiple != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_file_dialog_set_test_backend_ext(
    const CNA_Handle gameHandle,
    const CNA_Bool installed,
    const CNA_StringView* const results,
    const uint64_t resultCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (installed == CNA_FALSE) {
            FileDialog::SetBackendForTesting(nullptr);
            const std::lock_guard<std::mutex> lock(FileDialogTestMutex());
            FileDialogTestStorage().reset();
            return CNA_RESULT_SUCCESS;
        }
        if (resultCount != UINT64_C(0) && results == nullptr) {
            return InvalidInput("The file dialog result array is null.");
        }
        auto state = std::make_shared<FileDialogTestState>();
        for (uint64_t index = UINT64_C(0); index < resultCount; ++index) {
            std::string path;
            if (const CNA_Result result =
                    BorrowText(results[index], "A file dialog result is not valid UTF-8.", &path);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            state->results.push_back(std::move(path));
        }
        FileDialog::SetBackendForTesting(std::make_unique<TestFileDialogBackend>(state));
        const std::lock_guard<std::mutex> lock(FileDialogTestMutex());
        FileDialogTestStorage() = std::move(state);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_get_is_supported_ext(
    const CNA_Handle gameHandle,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidInput("The system tray support output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = SystemTray::getIsSupportedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_create(
    const CNA_Handle gameHandle,
    const CNA_StringView tooltip,
    CNA_SystemTrayHandle* const outTray)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTray == nullptr) {
            return InvalidInput("The system tray output is null.");
        }
        *outTray = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string tooltipText;
        if (const CNA_Result result =
                BorrowText(tooltip, "The system tray tooltip is not valid UTF-8.", &tooltipText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<SystemTrayResource>();
        resource->value = std::make_unique<SystemTray>(tooltipText);
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::SystemTray,
            std::move(resource),
            outTray);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The system tray handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_create_with_test_backend_ext(
    const CNA_Handle gameHandle,
    const CNA_StringView tooltip,
    CNA_SystemTrayHandle* const outTray)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTray == nullptr) {
            return InvalidInput("The system tray output is null.");
        }
        *outTray = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string tooltipText;
        if (const CNA_Result result =
                BorrowText(tooltip, "The system tray tooltip is not valid UTF-8.", &tooltipText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<SystemTrayResource>();
        resource->testState = std::make_shared<TrayTestState>();
        resource->value = std::make_unique<SystemTray>(
            tooltipText,
            std::make_unique<TestTrayBackend>(resource->testState));
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::SystemTray,
            std::move(resource),
            outTray);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The system tray handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_set_tooltip(
    const CNA_SystemTrayHandle tray,
    const CNA_StringView tooltip)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SystemTrayResource> resource;
        if (const CNA_Result result = BorrowTray(tray, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string tooltipText;
        if (const CNA_Result result =
                BorrowText(tooltip, "The system tray tooltip is not valid UTF-8.", &tooltipText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setTooltipProperty(tooltipText);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_add_entry(
    const CNA_SystemTrayHandle tray,
    const CNA_StringView label,
    const CNA_Bool checkable,
    const CNA_Bool initiallyChecked,
    const CNA_Bool initiallyEnabled,
    const CNA_TrayEntryClickCallback onClick,
    void* const context,
    uint64_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidInput("The tray entry index output is null.");
        }
        std::shared_ptr<SystemTrayResource> resource;
        if (const CNA_Result result = BorrowTray(tray, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string labelText;
        if (const CNA_Result result =
                BorrowText(label, "The tray entry label is not valid UTF-8.", &labelText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        CNA::Devices::Detail::TrayEntryClickCallback handler;
        if (onClick != nullptr) {
            handler = [onClick, context]() { onClick(context); };
        }
        *outIndex = static_cast<uint64_t>(resource->value->AddEntry(
            labelText,
            checkable != CNA_FALSE,
            initiallyChecked != CNA_FALSE,
            initiallyEnabled != CNA_FALSE,
            std::move(handler)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_set_entry_label(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    const CNA_StringView label)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SystemTrayResource> resource;
        if (const CNA_Result result = BorrowTray(tray, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string labelText;
        if (const CNA_Result result =
                BorrowText(label, "The tray entry label is not valid UTF-8.", &labelText);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetEntryLabel(static_cast<std::size_t>(index), labelText);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_set_entry_checked(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    const CNA_Bool checked)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SystemTrayResource> resource;
        if (const CNA_Result result = BorrowTray(tray, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetEntryChecked(static_cast<std::size_t>(index), checked != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_get_entry_checked(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    CNA_Bool* const outChecked)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outChecked == nullptr) {
            return InvalidInput("The tray entry check output is null.");
        }
        std::shared_ptr<SystemTrayResource> resource;
        if (const CNA_Result result = BorrowTray(tray, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outChecked = resource->value->GetEntryChecked(static_cast<std::size_t>(index))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_set_entry_enabled(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    const CNA_Bool enabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SystemTrayResource> resource;
        if (const CNA_Result result = BorrowTray(tray, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetEntryEnabled(static_cast<std::size_t>(index), enabled != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_get_entry_enabled(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    CNA_Bool* const outEnabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEnabled == nullptr) {
            return InvalidInput("The tray entry enabled output is null.");
        }
        std::shared_ptr<SystemTrayResource> resource;
        if (const CNA_Result result = BorrowTray(tray, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEnabled = resource->value->GetEntryEnabled(static_cast<std::size_t>(index))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_click_entry_for_tests_ext(
    const CNA_SystemTrayHandle tray,
    const uint64_t index)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SystemTrayResource> resource;
        if (const CNA_Result result = BorrowTray(tray, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!resource->testState) {
            return NoTestBackend();
        }
        CNA::Devices::Detail::TrayEntryClickCallback handler;
        {
            const std::lock_guard<std::mutex> lock(resource->testState->mutex);
            if (index >= static_cast<uint64_t>(resource->testState->entries.size())) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The tray entry index is at or past the entry count.");
            }
            handler = resource->testState->entries[static_cast<std::size_t>(index)].onClick;
        }
        if (handler) {
            handler();
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_system_tray_destroy(const CNA_SystemTrayHandle tray)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SystemTrayResource> resource;
        if (const CNA_Result result = BorrowTray(tray, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(tray);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The system tray handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

#else // CNA_DEVICES

CNA_Result cna_power_get_state_ext(const CNA_Handle game, CNA_PowerState* const outState)
{
    (void)game;
    (void)outState;
    return ExtensionUnavailable();
}

CNA_Result cna_power_get_battery_percent_ext(const CNA_Handle game, int32_t* const outPercent)
{
    (void)game;
    (void)outPercent;
    return ExtensionUnavailable();
}

CNA_Result cna_power_get_seconds_remaining_ext(const CNA_Handle game, int32_t* const outSeconds)
{
    (void)game;
    (void)outSeconds;
    return ExtensionUnavailable();
}

CNA_Result cna_system_info_get_logical_cpu_core_count_ext(
    const CNA_Handle game,
    int32_t* const outCount)
{
    (void)game;
    (void)outCount;
    return ExtensionUnavailable();
}

CNA_Result cna_system_info_get_system_ram_megabytes_ext(
    const CNA_Handle game,
    int32_t* const outMegabytes)
{
    (void)game;
    (void)outMegabytes;
    return ExtensionUnavailable();
}

CNA_Result cna_locale_get_preferred_count_ext(const CNA_Handle game, uint64_t* const outCount)
{
    (void)game;
    (void)outCount;
    return ExtensionUnavailable();
}

CNA_Result cna_locale_get_language_size_at_ext(
    const CNA_Handle game,
    const uint64_t index,
    uint64_t* const outBytes)
{
    (void)game;
    (void)index;
    (void)outBytes;
    return ExtensionUnavailable();
}

CNA_Result cna_locale_copy_language_at_ext(
    const CNA_Handle game,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    (void)game;
    (void)index;
    (void)destination;
    (void)capacity;
    (void)outBytes;
    return ExtensionUnavailable();
}

CNA_Result cna_locale_get_country_size_at_ext(
    const CNA_Handle game,
    const uint64_t index,
    uint64_t* const outBytes)
{
    (void)game;
    (void)index;
    (void)outBytes;
    return ExtensionUnavailable();
}

CNA_Result cna_locale_copy_country_at_ext(
    const CNA_Handle game,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    (void)game;
    (void)index;
    (void)destination;
    (void)capacity;
    (void)outBytes;
    return ExtensionUnavailable();
}

CNA_Result cna_display_info_get_content_scale_ext(const CNA_Handle game, float* const outScale)
{
    (void)game;
    (void)outScale;
    return ExtensionUnavailable();
}

CNA_Result cna_display_info_get_safe_area_ext(const CNA_Handle game, CNA_Rectangle* const outArea)
{
    (void)game;
    (void)outArea;
    return ExtensionUnavailable();
}

CNA_Result cna_devices_clipboard_set_text_ext(
    const CNA_Handle game,
    const CNA_StringView text,
    CNA_Bool* const outAccepted)
{
    (void)game;
    (void)text;
    (void)outAccepted;
    return ExtensionUnavailable();
}

CNA_Result cna_url_launcher_open_ext(
    const CNA_Handle game,
    const CNA_StringView url,
    CNA_Bool* const outOpened)
{
    (void)game;
    (void)url;
    (void)outOpened;
    return ExtensionUnavailable();
}

CNA_Result cna_message_box_get_is_supported_ext(
    const CNA_Handle game,
    CNA_Bool* const outSupported)
{
    (void)game;
    (void)outSupported;
    return ExtensionUnavailable();
}

CNA_Result cna_message_box_show_simple_ext(
    const CNA_Handle game,
    const CNA_MessageBoxType type,
    const CNA_StringView title,
    const CNA_StringView message)
{
    (void)game;
    (void)type;
    (void)title;
    (void)message;
    return ExtensionUnavailable();
}

CNA_Result cna_message_box_show_ext(
    const CNA_Handle game,
    const CNA_MessageBoxType type,
    const CNA_StringView title,
    const CNA_StringView message,
    const CNA_StringView* const buttonLabels,
    const uint64_t buttonCount,
    int32_t* const outChosen)
{
    (void)game;
    (void)type;
    (void)title;
    (void)message;
    (void)buttonLabels;
    (void)buttonCount;
    (void)outChosen;
    return ExtensionUnavailable();
}

CNA_Result cna_message_box_set_test_backend_ext(
    const CNA_Handle game,
    const CNA_Bool installed,
    const int32_t chosenButton)
{
    (void)game;
    (void)installed;
    (void)chosenButton;
    return ExtensionUnavailable();
}

CNA_Result cna_message_box_get_test_log_ext(
    const CNA_Handle game,
    CNA_MessageBoxTestLog* const outLog)
{
    (void)game;
    (void)outLog;
    return ExtensionUnavailable();
}

CNA_Result cna_file_dialog_get_is_supported_ext(
    const CNA_Handle game,
    CNA_Bool* const outSupported)
{
    (void)game;
    (void)outSupported;
    return ExtensionUnavailable();
}

CNA_Result cna_file_dialog_show_open_file_ext(
    const CNA_Handle game,
    const CNA_FileDialogResultCallback onResult,
    void* const context,
    const CNA_FileDialogFilter* const filters,
    const uint64_t filterCount,
    const CNA_StringView defaultLocation,
    const CNA_Bool allowMultiple)
{
    (void)game;
    (void)onResult;
    (void)context;
    (void)filters;
    (void)filterCount;
    (void)defaultLocation;
    (void)allowMultiple;
    return ExtensionUnavailable();
}

CNA_Result cna_file_dialog_show_save_file_ext(
    const CNA_Handle game,
    const CNA_FileDialogResultCallback onResult,
    void* const context,
    const CNA_FileDialogFilter* const filters,
    const uint64_t filterCount,
    const CNA_StringView defaultLocation)
{
    (void)game;
    (void)onResult;
    (void)context;
    (void)filters;
    (void)filterCount;
    (void)defaultLocation;
    return ExtensionUnavailable();
}

CNA_Result cna_file_dialog_show_open_folder_ext(
    const CNA_Handle game,
    const CNA_FileDialogResultCallback onResult,
    void* const context,
    const CNA_StringView defaultLocation,
    const CNA_Bool allowMultiple)
{
    (void)game;
    (void)onResult;
    (void)context;
    (void)defaultLocation;
    (void)allowMultiple;
    return ExtensionUnavailable();
}

CNA_Result cna_file_dialog_set_test_backend_ext(
    const CNA_Handle game,
    const CNA_Bool installed,
    const CNA_StringView* const results,
    const uint64_t resultCount)
{
    (void)game;
    (void)installed;
    (void)results;
    (void)resultCount;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_get_is_supported_ext(
    const CNA_Handle game,
    CNA_Bool* const outSupported)
{
    (void)game;
    (void)outSupported;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_create(
    const CNA_Handle game,
    const CNA_StringView tooltip,
    CNA_SystemTrayHandle* const outTray)
{
    (void)game;
    (void)tooltip;
    if (outTray != nullptr) {
        *outTray = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_create_with_test_backend_ext(
    const CNA_Handle game,
    const CNA_StringView tooltip,
    CNA_SystemTrayHandle* const outTray)
{
    (void)game;
    (void)tooltip;
    if (outTray != nullptr) {
        *outTray = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_set_tooltip(
    const CNA_SystemTrayHandle tray,
    const CNA_StringView tooltip)
{
    (void)tray;
    (void)tooltip;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_add_entry(
    const CNA_SystemTrayHandle tray,
    const CNA_StringView label,
    const CNA_Bool checkable,
    const CNA_Bool initiallyChecked,
    const CNA_Bool initiallyEnabled,
    const CNA_TrayEntryClickCallback onClick,
    void* const context,
    uint64_t* const outIndex)
{
    (void)tray;
    (void)label;
    (void)checkable;
    (void)initiallyChecked;
    (void)initiallyEnabled;
    (void)onClick;
    (void)context;
    (void)outIndex;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_set_entry_label(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    const CNA_StringView label)
{
    (void)tray;
    (void)index;
    (void)label;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_set_entry_checked(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    const CNA_Bool checked)
{
    (void)tray;
    (void)index;
    (void)checked;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_get_entry_checked(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    CNA_Bool* const outChecked)
{
    (void)tray;
    (void)index;
    (void)outChecked;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_set_entry_enabled(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    const CNA_Bool enabled)
{
    (void)tray;
    (void)index;
    (void)enabled;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_get_entry_enabled(
    const CNA_SystemTrayHandle tray,
    const uint64_t index,
    CNA_Bool* const outEnabled)
{
    (void)tray;
    (void)index;
    (void)outEnabled;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_click_entry_for_tests_ext(
    const CNA_SystemTrayHandle tray,
    const uint64_t index)
{
    (void)tray;
    (void)index;
    return ExtensionUnavailable();
}

CNA_Result cna_system_tray_destroy(const CNA_SystemTrayHandle tray)
{
    (void)tray;
    return ExtensionUnavailable();
}

#endif // CNA_DEVICES
