// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0150: the Linux evdev controllers against devices the kernel really
// created, through uinput.
//
// A uinput device is not a simulation of an evdev node: it IS one. The kernel creates the node,
// udev grants its permissions, and every ioctl, every read, SYN_DROPPED, hot-unplug and the whole
// force-feedback upload handshake behave exactly as they do for a pad on a USB cable. What it
// cannot reproduce is a particular driver's quirks -- those are pinned in X11EvdevLayoutTests.
//
// Every test skips, rather than fails, where /dev/uinput cannot be opened or where the node the
// kernel creates is not readable by this user: both are properties of the machine. Each device is
// named uniquely and found by name, so a real pad plugged into the machine, or a parallel run of
// the same suite, occupies other slots without disturbing the assertions.
//
// None of these devices can type or point: they carry only controller buttons and axes, which
// udev classifies as a joystick and a desktop's input stack (libinput) ignores.

#include <gtest/gtest.h>

#ifdef CNA_PLATFORM_HAVE_EVDEV

#include "../../../src/Linux/EvdevControllers.hpp"
#include "../../../src/Linux/EvdevHaptics.hpp"
#include "../../../src/X11/X11Platform.hpp"

#include "System/Environment.hpp"

#include <linux/uinput.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Linux;

constexpr auto kTimeout = std::chrono::seconds(5);

std::string UniqueName(const char* role)
{
    static int counter = 0;
    return std::string("CNA evdev test ") + role + " " + std::to_string(::getpid()) + "-" +
           std::to_string(++counter);
}

/// One device created through /dev/uinput, destroyed with the object.
class VirtualDevice
{
public:
    struct Axis
    {
        std::uint16_t code;
        int minimum;
        int maximum;
        int resolution = 0;
    };

    struct Spec
    {
        std::string name;
        std::uint16_t bus = BUS_USB;
        // pid.codes' test vendor/product: an identity no real controller has.
        std::uint16_t vendor = 0x1209;
        std::uint16_t product = 0x0001;
        std::uint16_t version = 0x0100;
        std::vector<std::uint16_t> keys;
        std::vector<Axis> axes;
        bool rumble = false;
        /// Force-feedback codes beyond rumble (X11-0168): effect families, waveforms, controls.
        std::vector<std::uint16_t> forceFeedback;
        /// Where the device says it is attached; two nodes of one controller share it.
        std::string phys;
        /// A motion-sensor node (INPUT_PROP_ACCELEROMETER).
        bool accelerometer = false;
    };

    struct ForceFeedbackLog
    {
        std::vector<ff_effect> uploads;
        std::vector<std::pair<int, int>> plays;
        int erases = 0;
        std::vector<int> erased;
    };

    /// Creates the device, or explains why it cannot be created on this machine.
    static std::unique_ptr<VirtualDevice> Create(const Spec& spec, std::string& why)
    {
        const int descriptor = ::open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (descriptor < 0)
        {
            why = std::string("/dev/uinput cannot be opened: ") + std::strerror(errno);
            return nullptr;
        }
        auto device = std::unique_ptr<VirtualDevice>(new VirtualDevice(descriptor));
        if (!device->Setup(spec, why))
        {
            return nullptr;
        }
        return device;
    }

    ~VirtualDevice()
    {
        StopServicing();
        Destroy();
        ::close(descriptor_);
    }

    VirtualDevice(const VirtualDevice&) = delete;
    VirtualDevice& operator=(const VirtualDevice&) = delete;

    [[nodiscard]] const std::string& NodePath() const { return nodePath_; }

    void Emit(const std::uint16_t type, const std::uint16_t code, const std::int32_t value)
    {
        input_event event{};
        event.type = type;
        event.code = code;
        event.value = value;
        ASSERT_EQ(::write(descriptor_, &event, sizeof(event)), static_cast<ssize_t>(sizeof(event)));
    }

    void Report() { Emit(EV_SYN, SYN_REPORT, 0); }

    /// Unplugs the device.
    void Destroy()
    {
        if (created_)
        {
            ioctl(descriptor_, UI_DEV_DESTROY);
            created_ = false;
        }
    }

    /// Answers the kernel's force-feedback requests on a thread until StopServicing(). Without
    /// this a client's EVIOCSFF waits for the answer -- uinput's owner is the device's "driver".
    void StartServicing()
    {
        servicing_ = true;
        servicer_ = std::thread([this] { Service(); });
    }

    void StopServicing()
    {
        if (servicer_.joinable())
        {
            servicing_ = false;
            servicer_.join();
        }
    }

    [[nodiscard]] ForceFeedbackLog GetLog()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return log_;
    }

private:
    explicit VirtualDevice(const int descriptor) : descriptor_(descriptor) {}

    bool Setup(const Spec& spec, std::string& why)
    {
        const auto fail = [&why](const char* what) {
            why = std::string(what) + ": " + std::strerror(errno);
            return false;
        };
        if (ioctl(descriptor_, UI_SET_EVBIT, EV_SYN) < 0 ||
            ioctl(descriptor_, UI_SET_EVBIT, EV_KEY) < 0)
        {
            return fail("UI_SET_EVBIT");
        }
        for (const std::uint16_t key : spec.keys)
        {
            if (ioctl(descriptor_, UI_SET_KEYBIT, key) < 0)
            {
                return fail("UI_SET_KEYBIT");
            }
        }
        if (!spec.axes.empty() && ioctl(descriptor_, UI_SET_EVBIT, EV_ABS) < 0)
        {
            return fail("UI_SET_EVBIT(EV_ABS)");
        }
        for (const Axis& axis : spec.axes)
        {
            uinput_abs_setup setup{};
            setup.code = axis.code;
            setup.absinfo.minimum = axis.minimum;
            setup.absinfo.maximum = axis.maximum;
            setup.absinfo.resolution = axis.resolution;
            if (ioctl(descriptor_, UI_SET_ABSBIT, axis.code) < 0 ||
                ioctl(descriptor_, UI_ABS_SETUP, &setup) < 0)
            {
                return fail("UI_ABS_SETUP");
            }
        }
        if (spec.rumble &&
            (ioctl(descriptor_, UI_SET_EVBIT, EV_FF) < 0 ||
             ioctl(descriptor_, UI_SET_FFBIT, FF_RUMBLE) < 0))
        {
            return fail("UI_SET_FFBIT");
        }
        if (!spec.forceFeedback.empty() && ioctl(descriptor_, UI_SET_EVBIT, EV_FF) < 0)
        {
            return fail("UI_SET_EVBIT(EV_FF)");
        }
        for (const std::uint16_t code : spec.forceFeedback)
        {
            if (ioctl(descriptor_, UI_SET_FFBIT, code) < 0)
            {
                return fail("UI_SET_FFBIT");
            }
        }
        if (spec.accelerometer && ioctl(descriptor_, UI_SET_PROPBIT, INPUT_PROP_ACCELEROMETER) < 0)
        {
            return fail("UI_SET_PROPBIT");
        }
        if (!spec.phys.empty() && ioctl(descriptor_, UI_SET_PHYS, spec.phys.c_str()) < 0)
        {
            return fail("UI_SET_PHYS");
        }
        uinput_setup setup{};
        std::strncpy(setup.name, spec.name.c_str(), UINPUT_MAX_NAME_SIZE - 1);
        setup.id.bustype = spec.bus;
        setup.id.vendor = spec.vendor;
        setup.id.product = spec.product;
        setup.id.version = spec.version;
        setup.ff_effects_max = !spec.forceFeedback.empty() ? 16 : spec.rumble ? 4 : 0;
        if (ioctl(descriptor_, UI_DEV_SETUP, &setup) < 0)
        {
            return fail("UI_DEV_SETUP");
        }
        if (ioctl(descriptor_, UI_DEV_CREATE) < 0)
        {
            return fail("UI_DEV_CREATE");
        }
        created_ = true;

        char sysname[64] = {};
        if (ioctl(descriptor_, UI_GET_SYSNAME(sizeof(sysname) - 1), sysname) < 0)
        {
            return fail("UI_GET_SYSNAME");
        }
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(
                 std::filesystem::path("/sys/devices/virtual/input") / sysname, error))
        {
            const std::string name = entry.path().filename().string();
            if (name.rfind("event", 0) == 0)
            {
                nodePath_ = "/dev/input/" + name;
            }
        }
        if (nodePath_.empty())
        {
            why = "the kernel created no event node for the device";
            return false;
        }
        // udev grants permissions a moment after the node appears. A machine where it never
        // grants this user access is not one this suite can run on.
        const auto deadline = std::chrono::steady_clock::now() + kTimeout;
        while (::access(nodePath_.c_str(), R_OK) != 0)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                why = nodePath_ + " is not readable by this user (not in the input group, and no "
                                  "uaccess rule for the virtual device)";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

    void Service()
    {
        while (servicing_)
        {
            pollfd waiting{descriptor_, POLLIN, 0};
            if (::poll(&waiting, 1, 10) <= 0)
            {
                continue;
            }
            input_event event{};
            while (::read(descriptor_, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event)))
            {
                if (event.type == EV_UINPUT && event.code == UI_FF_UPLOAD)
                {
                    uinput_ff_upload upload{};
                    upload.request_id = static_cast<std::uint32_t>(event.value);
                    ioctl(descriptor_, UI_BEGIN_FF_UPLOAD, &upload);
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        log_.uploads.push_back(upload.effect);
                    }
                    upload.retval = 0;
                    ioctl(descriptor_, UI_END_FF_UPLOAD, &upload);
                }
                else if (event.type == EV_UINPUT && event.code == UI_FF_ERASE)
                {
                    uinput_ff_erase erase{};
                    erase.request_id = static_cast<std::uint32_t>(event.value);
                    ioctl(descriptor_, UI_BEGIN_FF_ERASE, &erase);
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        ++log_.erases;
                        log_.erased.push_back(static_cast<int>(erase.effect_id));
                    }
                    erase.retval = 0;
                    ioctl(descriptor_, UI_END_FF_ERASE, &erase);
                }
                else if (event.type == EV_FF)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    log_.plays.emplace_back(event.code, event.value);
                }
            }
        }
    }

    int descriptor_ = -1;
    bool created_ = false;
    std::string nodePath_;
    std::atomic<bool> servicing_{false};
    std::thread servicer_;
    std::mutex mutex_;
    ForceFeedbackLog log_;
};

// A pad shaped like an Xbox pad on the gamepad API.
VirtualDevice::Spec PadSpec(const char* role)
{
    VirtualDevice::Spec spec;
    spec.name = UniqueName(role);
    spec.keys = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
                 BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    spec.axes = {{ABS_X, -32768, 32767}, {ABS_Y, -32768, 32767}, {ABS_RX, -32768, 32767},
                 {ABS_RY, -32768, 32767}, {ABS_Z, 0, 255},       {ABS_RZ, 0, 255},
                 {ABS_HAT0X, -1, 1},      {ABS_HAT0Y, -1, 1}};
    return spec;
}

#define CREATE_OR_SKIP(variable, spec)                                                             \
    std::string variable##Why;                                                                     \
    std::unique_ptr<VirtualDevice> variable = VirtualDevice::Create((spec), variable##Why);        \
    if (variable == nullptr)                                                                       \
    {                                                                                              \
        GTEST_SKIP() << variable##Why;                                                             \
    }

// For a test the machine was asked to run: a device it cannot create is a failure, not a skip.
#define CREATE_OR_FAIL(variable, spec)                                                             \
    std::string variable##Why;                                                                     \
    std::unique_ptr<VirtualDevice> variable = VirtualDevice::Create((spec), variable##Why);        \
    ASSERT_NE(variable, nullptr) << variable##Why

bool PumpUntil(EvdevControllerHub& hub, const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (true)
    {
        hub.Pump();
        if (done())
        {
            return true;
        }
        if (std::chrono::steady_clock::now() > deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

EvdevControllerHub::Controller* FindByName(const EvdevControllerHub& hub, const std::string& name)
{
    for (const auto& controller : hub.GetControllers())
    {
        if (controller->device->GetDescription().name == name)
        {
            return controller.get();
        }
    }
    return nullptr;
}

int FindSlot(const IPlatformGamepad& gamepad, const std::string& name)
{
    for (int slot = 0; slot < gamepad.GetCount(); ++slot)
    {
        if (gamepad.GetInfo(slot).name == name)
        {
            return slot;
        }
    }
    return -1;
}

template <typename Event>
std::vector<Event> EventsOf(const std::vector<PlatformEvent>& events, const DeviceId device)
{
    std::vector<Event> matching;
    for (const PlatformEvent& event : events)
    {
        if (const Event* typed = std::get_if<Event>(&event); typed != nullptr && typed->device == device)
        {
            matching.push_back(*typed);
        }
    }
    return matching;
}

float Axis(const GamepadSnapshot& snapshot, const GamepadAxis axis)
{
    return snapshot.axes[static_cast<std::size_t>(axis)];
}

// --- connection ----------------------------------------------------------------------------------

TEST(X11EvdevVirtualDevice, APadPluggedInConnectsAsAGamepadAndAJoystick)
{
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    std::vector<PlatformEvent> events;
    hub.TakeEvents(events);  // Whatever was already plugged in is not this test's business.

    const VirtualDevice::Spec spec = PadSpec("pad");
    CREATE_OR_SKIP(pad, spec);

    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }))
        << "the hub never picked up " << pad->NodePath();
    const EvdevControllerHub::Controller* controller = FindByName(hub, spec.name);
    EXPECT_EQ(controller->kind, EvdevDeviceClass::Gamepad);
    EXPECT_EQ(controller->device->GetPath(), pad->NodePath());
    EXPECT_NE(controller->id, 0u);

    // Joystick first, then gamepad: both name the same id.
    events.clear();
    hub.TakeEvents(events);
    const std::vector<DeviceEvent> connections = EventsOf<DeviceEvent>(events, controller->id);
    ASSERT_EQ(connections.size(), 2u);
    EXPECT_EQ(connections[0].kind, InputDeviceKind::Joystick);
    EXPECT_TRUE(connections[0].connected);
    EXPECT_EQ(connections[1].kind, InputDeviceKind::Gamepad);
    EXPECT_TRUE(connections[1].connected);

    gamepad.Update();
    const int slot = FindSlot(gamepad, spec.name);
    ASSERT_GE(slot, 0);
    EXPECT_TRUE(gamepad.GetSnapshot(slot).connected);
    EXPECT_EQ(gamepad.GetSnapshot(slot).packetNumber, 1u);
    EXPECT_EQ(gamepad.GetName(slot), spec.name);

    const GamepadCapabilities& capabilities = gamepad.GetCapabilities(slot);
    EXPECT_TRUE(capabilities.connected);
    EXPECT_EQ(capabilities.kind, GamepadKind::Gamepad);
    EXPECT_EQ(capabilities.axes, 0x3F);
    EXPECT_NE(capabilities.buttons & static_cast<std::uint32_t>(GamepadButton::A), 0u);
    EXPECT_NE(capabilities.buttons & static_cast<std::uint32_t>(GamepadButton::DPadDown), 0u);
    EXPECT_EQ(capabilities.buttons & static_cast<std::uint32_t>(GamepadButton::Paddle1), 0u);
    EXPECT_FALSE(capabilities.rumble);  // This one declared no force feedback.

    const GamepadInfo& info = gamepad.GetInfo(slot);
    EXPECT_EQ(info.path, pad->NodePath());
    EXPECT_EQ(info.vendor, spec.vendor);
    EXPECT_EQ(info.product, spec.product);
    EXPECT_EQ(info.firmwareVersion, spec.version);
    EXPECT_EQ(info.connectionState, GamepadConnectionState::Wired);
    EXPECT_EQ(info.model, GamepadModel::Standard);
    EXPECT_EQ(gamepad.GetPowerInfo(slot).state, GamepadPowerState::Unknown);
    EXPECT_EQ(gamepad.GetButtonLabel(slot, GamepadButton::A), GamepadButtonLabel::A);
}

TEST(X11EvdevVirtualDevice, UnpluggingEmptiesTheSlotAndSaysSo)
{
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    EvdevJoystick joystick(hub);

    const VirtualDevice::Spec spec = PadSpec("unplugged");
    CREATE_OR_SKIP(pad, spec);
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }));
    const DeviceId id = FindByName(hub, spec.name)->id;
    gamepad.Update();
    joystick.Update();
    const int slot = FindSlot(gamepad, spec.name);
    ASSERT_GE(slot, 0);
    EXPECT_TRUE(joystick.IsConnected(id));
    std::vector<PlatformEvent> events;
    hub.TakeEvents(events);

    pad->Destroy();
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) == nullptr; }));

    events.clear();
    hub.TakeEvents(events);
    const std::vector<DeviceEvent> disconnections = EventsOf<DeviceEvent>(events, id);
    ASSERT_EQ(disconnections.size(), 2u);
    EXPECT_EQ(disconnections[0].kind, InputDeviceKind::Gamepad);
    EXPECT_FALSE(disconnections[0].connected);
    EXPECT_EQ(disconnections[1].kind, InputDeviceKind::Joystick);
    EXPECT_FALSE(disconnections[1].connected);

    gamepad.Update();
    joystick.Update();
    EXPECT_NE(gamepad.GetInfo(slot).name, spec.name);
    if (gamepad.GetInfo(slot).name.empty())
    {
        // Nobody else moved in: the slot is empty, and its packet count starts over.
        EXPECT_FALSE(gamepad.GetSnapshot(slot).connected);
        EXPECT_EQ(gamepad.GetSnapshot(slot).packetNumber, 0u);
        EXPECT_FALSE(gamepad.GetCapabilities(slot).connected);
        EXPECT_EQ(gamepad.GetPowerInfo(slot).state, GamepadPowerState::Error);
        EXPECT_FALSE(gamepad.SetRumble(slot, 1.0f, 1.0f, 10));
    }
    EXPECT_FALSE(joystick.IsConnected(id));
    EXPECT_FALSE(joystick.GetCapabilities(id).connected);
    EXPECT_TRUE(joystick.GetSnapshot(id).buttons.empty());
}

TEST(X11EvdevVirtualDevice, AStateHeldBeforeOpeningIsSeenWithoutAnEvent)
{
    // A trigger already squeezed when the game starts is the pad's state, not a change: the
    // snapshot reports it and no press event is invented for it.
    const VirtualDevice::Spec spec = PadSpec("held");
    CREATE_OR_SKIP(pad, spec);
    pad->Emit(EV_KEY, BTN_SOUTH, 1);
    pad->Emit(EV_ABS, ABS_RZ, 255);
    pad->Report();

    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }));
    const DeviceId id = FindByName(hub, spec.name)->id;
    gamepad.Update();
    const int slot = FindSlot(gamepad, spec.name);
    ASSERT_GE(slot, 0);
    EXPECT_NE(gamepad.GetSnapshot(slot).buttons & static_cast<std::uint32_t>(GamepadButton::A), 0u);
    EXPECT_FLOAT_EQ(Axis(gamepad.GetSnapshot(slot), GamepadAxis::RightTrigger), 1.0f);

    std::vector<PlatformEvent> events;
    hub.TakeEvents(events);
    EXPECT_TRUE(EventsOf<ControllerButtonEvent>(events, id).empty());
    EXPECT_TRUE(EventsOf<ControllerAxisEvent>(events, id).empty());
}

TEST(X11EvdevVirtualDevice, SysfsAndTheNodeItselfDescribeEveryDeviceAlike)
{
    // The hub decides what to open from sysfs alone, so sysfs must say exactly what the node would.
    // Checked against every node this user can open -- keyboards with high key codes, touchpads
    // with multitouch axes, whatever the machine has -- and against a pad of our own, so there is
    // at least one controller in the comparison.
    VirtualDevice::Spec spec = PadSpec("sysfs");
    spec.rumble = true;
    CREATE_OR_SKIP(pad, spec);

    int compared = 0;
    bool sawPad = false;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/input", error))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("event", 0) != 0)
        {
            continue;
        }
        const std::unique_ptr<EvdevDevice> device = EvdevDevice::Open(entry.path().string());
        if (device == nullptr)
        {
            continue;
        }
        const EvdevDescription& fromNode = device->GetDescription();
        EvdevDescription fromSysfs;
        ASSERT_TRUE(ReadEvdevSysfsDescription(name, fromSysfs)) << name;
        EXPECT_EQ(fromSysfs.name, fromNode.name) << name;
        EXPECT_EQ(fromSysfs.uniq, fromNode.uniq) << name;
        EXPECT_EQ(fromSysfs.bus, fromNode.bus) << name;
        EXPECT_EQ(fromSysfs.vendor, fromNode.vendor) << name;
        EXPECT_EQ(fromSysfs.product, fromNode.product) << name;
        EXPECT_EQ(fromSysfs.version, fromNode.version) << name;
        EXPECT_EQ(fromSysfs.keys, fromNode.keys) << name << " (" << fromNode.name << ")";
        EXPECT_EQ(fromSysfs.axes, fromNode.axes) << name << " (" << fromNode.name << ")";
        EXPECT_EQ(fromSysfs.forceFeedback, fromNode.forceFeedback) << name;
        EXPECT_EQ(fromSysfs.properties, fromNode.properties) << name;
        EXPECT_EQ(fromSysfs.driver, fromNode.driver) << name;
        EXPECT_EQ(ClassifyEvdevDevice(fromSysfs), ClassifyEvdevDevice(fromNode)) << name;
        sawPad = sawPad || fromNode.name == spec.name;
        ++compared;
    }
    EXPECT_TRUE(sawPad) << "the virtual pad " << pad->NodePath() << " was not among the nodes";
    RecordProperty("NodesCompared", compared);
}

// --- input ---------------------------------------------------------------------------------------

TEST(X11EvdevVirtualDevice, ButtonsSticksAndTheHatReachSnapshotsAndEvents)
{
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    const VirtualDevice::Spec spec = PadSpec("input");
    CREATE_OR_SKIP(pad, spec);
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }));
    const DeviceId id = FindByName(hub, spec.name)->id;
    gamepad.Update();
    const int slot = FindSlot(gamepad, spec.name);
    ASSERT_GE(slot, 0);
    std::vector<PlatformEvent> events;
    hub.TakeEvents(events);
    const std::uint32_t packetBefore = gamepad.GetSnapshot(slot).packetNumber;

    pad->Emit(EV_KEY, BTN_SOUTH, 1);
    pad->Emit(EV_KEY, BTN_WEST, 1);
    pad->Emit(EV_ABS, ABS_X, 32767);
    pad->Emit(EV_ABS, ABS_Y, -32768);  // Pushed up.
    pad->Emit(EV_ABS, ABS_Z, 255);
    pad->Emit(EV_ABS, ABS_HAT0Y, 1);   // D-pad down.
    pad->Report();

    ASSERT_TRUE(PumpUntil(hub, [&] {
        gamepad.Update();
        return (gamepad.GetSnapshot(slot).buttons &
                static_cast<std::uint32_t>(GamepadButton::DPadDown)) != 0;
    }));
    const GamepadSnapshot& snapshot = gamepad.GetSnapshot(slot);
    EXPECT_EQ(snapshot.buttons, static_cast<std::uint32_t>(GamepadButton::A) |
                                    static_cast<std::uint32_t>(GamepadButton::X) |
                                    static_cast<std::uint32_t>(GamepadButton::DPadDown));
    EXPECT_FLOAT_EQ(Axis(snapshot, GamepadAxis::LeftThumbstickX), 1.0f);
    EXPECT_FLOAT_EQ(Axis(snapshot, GamepadAxis::LeftThumbstickY), 1.0f);
    EXPECT_FLOAT_EQ(Axis(snapshot, GamepadAxis::LeftTrigger), 1.0f);
    EXPECT_FLOAT_EQ(Axis(snapshot, GamepadAxis::RightTrigger), 0.0f);
    EXPECT_GT(snapshot.packetNumber, packetBefore);

    // Nothing changed since: the packet number stays put.
    const std::uint32_t packet = snapshot.packetNumber;
    gamepad.Update();
    gamepad.Update();
    EXPECT_EQ(gamepad.GetSnapshot(slot).packetNumber, packet);

    events.clear();
    hub.TakeEvents(events);
    const std::vector<ControllerButtonEvent> buttons = EventsOf<ControllerButtonEvent>(events, id);
    ASSERT_EQ(buttons.size(), 3u);
    EXPECT_EQ(buttons[0].button, GamepadButton::A);
    EXPECT_TRUE(buttons[0].pressed);
    EXPECT_EQ(buttons[1].button, GamepadButton::X);
    EXPECT_EQ(buttons[2].button, GamepadButton::DPadDown);
    const std::vector<ControllerAxisEvent> axes = EventsOf<ControllerAxisEvent>(events, id);
    ASSERT_EQ(axes.size(), 3u);
    EXPECT_EQ(axes[0].axis, GamepadAxis::LeftThumbstickX);
    EXPECT_FLOAT_EQ(axes[0].value, 1.0f);
    EXPECT_EQ(axes[1].axis, GamepadAxis::LeftThumbstickY);
    EXPECT_FLOAT_EQ(axes[1].value, 1.0f);
    EXPECT_EQ(axes[2].axis, GamepadAxis::LeftTrigger);

    // Releases come through the same way.
    pad->Emit(EV_KEY, BTN_SOUTH, 0);
    pad->Emit(EV_ABS, ABS_HAT0Y, 0);
    pad->Report();
    ASSERT_TRUE(PumpUntil(hub, [&] {
        gamepad.Update();
        return gamepad.GetSnapshot(slot).buttons == static_cast<std::uint32_t>(GamepadButton::X);
    }));
}

TEST(X11EvdevVirtualDevice, AnOverflowedQueueEndsOnTheDevicesRealState)
{
    // Thousands of events with nobody reading overflow the kernel's per-client queue, which then
    // reports SYN_DROPPED. What was lost cannot be replayed; what matters is that the state the
    // pad ends in is the one it is really in -- not the last event that happened to survive.
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    const VirtualDevice::Spec spec = PadSpec("flood");
    CREATE_OR_SKIP(pad, spec);
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }));
    const DeviceId id = FindByName(hub, spec.name)->id;
    gamepad.Update();
    const int slot = FindSlot(gamepad, spec.name);
    ASSERT_GE(slot, 0);
    std::vector<PlatformEvent> events;
    hub.TakeEvents(events);

    // Where the damage lands depends on where the kernel's ring buffer last wrapped: a queue left
    // holding more after the SYN_DROPPED than one read takes is the case that goes wrong. So the
    // flood's length is varied across more than a whole ring's worth of packets, and each round
    // ends in the opposite state from the one before, so every round's answer is a real change.
    constexpr int kRounds = 48;
    constexpr int kBaseToggles = 400;
    int toggles = 0;
    std::vector<std::string> wrong;
    for (int round = 0; round < kRounds; ++round)
    {
        for (int index = 0; index < kBaseToggles + round; ++index)
        {
            pad->Emit(EV_KEY, BTN_SOUTH, index % 2);
            pad->Emit(EV_KEY, BTN_EAST, (index + 1) % 2);
            pad->Emit(EV_ABS, ABS_RX, (index % 2) != 0 ? 32767 : -32768);
            pad->Report();
            ++toggles;
        }
        // The real final state: A held and the stick right, or B held and the stick left.
        const bool holdA = (round % 2) == 0;
        pad->Emit(EV_KEY, BTN_SOUTH, holdA ? 1 : 0);
        pad->Emit(EV_KEY, BTN_EAST, holdA ? 0 : 1);
        pad->Emit(EV_ABS, ABS_RX, holdA ? 32767 : -32768);
        pad->Report();

        hub.Pump();
        gamepad.Update();
        const GamepadSnapshot& snapshot = gamepad.GetSnapshot(slot);
        const auto expected = static_cast<std::uint32_t>(holdA ? GamepadButton::A : GamepadButton::B);
        if (snapshot.buttons != expected ||
            Axis(snapshot, GamepadAxis::RightThumbstickX) != (holdA ? 1.0f : -1.0f))
        {
            wrong.push_back("round " + std::to_string(round) + ": buttons " +
                            std::to_string(snapshot.buttons) + ", right stick " +
                            std::to_string(Axis(snapshot, GamepadAxis::RightThumbstickX)));
        }
    }
    EXPECT_TRUE(wrong.empty()) << wrong.size() << " of " << kRounds
                               << " rounds ended on a stale state, e.g. " << wrong.front();

    // And the overflow really happened -- otherwise this test proved nothing about it.
    events.clear();
    hub.TakeEvents(events);
    EXPECT_LT(EventsOf<ControllerButtonEvent>(events, id).size(), static_cast<std::size_t>(toggles))
        << "every event arrived; the kernel queue did not overflow";
}

TEST(X11EvdevVirtualDevice, AnXboxIdentityGetsXpadFaceButtons)
{
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    VirtualDevice::Spec spec = PadSpec("xbox");
    spec.vendor = 0x045E;
    spec.product = 0x028E;
    CREATE_OR_SKIP(pad, spec);
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }));
    gamepad.Update();
    const int slot = FindSlot(gamepad, spec.name);
    ASSERT_GE(slot, 0);
    EXPECT_EQ(gamepad.GetInfo(slot).model, GamepadModel::Xbox360);

    pad->Emit(EV_KEY, BTN_X, 1);  // The left face button, as xpad reports it.
    pad->Report();
    ASSERT_TRUE(PumpUntil(hub, [&] {
        gamepad.Update();
        return gamepad.GetSnapshot(slot).buttons != 0;
    }));
    EXPECT_EQ(gamepad.GetSnapshot(slot).buttons, static_cast<std::uint32_t>(GamepadButton::X));
}

// --- rumble --------------------------------------------------------------------------------------

TEST(X11EvdevVirtualDevice, RumbleIsUploadedPlayedStoppedAndErased)
{
    VirtualDevice::Spec spec = PadSpec("rumble");
    spec.rumble = true;
    CREATE_OR_SKIP(pad, spec);
    pad->StartServicing();

    auto hub = std::make_unique<EvdevControllerHub>();
    ASSERT_TRUE(hub->Start());
    EvdevGamepad gamepad(*hub);
    ASSERT_TRUE(PumpUntil(*hub, [&] { return FindByName(*hub, spec.name) != nullptr; }));
    gamepad.Update();
    const int slot = FindSlot(gamepad, spec.name);
    ASSERT_GE(slot, 0);
    ASSERT_TRUE(gamepad.GetCapabilities(slot).rumble);

    ASSERT_TRUE(gamepad.SetRumble(slot, 1.0f, 0.5f, 250));
    // Changing the strength updates the same effect rather than uploading another one.
    ASSERT_TRUE(gamepad.SetRumble(slot, 0.25f, 0.0f, 0));
    ASSERT_TRUE(gamepad.SetRumble(slot, 0.0f, 0.0f, 0));

    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (pad->GetLog().plays.size() < 3 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    VirtualDevice::ForceFeedbackLog log = pad->GetLog();
    ASSERT_EQ(log.uploads.size(), 2u);
    EXPECT_EQ(log.uploads[0].type, FF_RUMBLE);
    EXPECT_EQ(log.uploads[0].u.rumble.strong_magnitude, 0xFFFF);
    EXPECT_EQ(log.uploads[0].u.rumble.weak_magnitude, 0x8000);
    EXPECT_EQ(log.uploads[0].replay.length, 250);
    EXPECT_EQ(log.uploads[1].id, log.uploads[0].id);
    EXPECT_EQ(log.uploads[1].u.rumble.strong_magnitude, 0x4000);
    EXPECT_EQ(log.uploads[1].replay.length, 0);  // "Until changed".
    ASSERT_EQ(log.plays.size(), 3u);
    EXPECT_EQ(log.plays[0], std::make_pair(static_cast<int>(log.uploads[0].id), 1));
    EXPECT_EQ(log.plays[1].second, 1);
    EXPECT_EQ(log.plays[2].second, 0);  // Both motors at zero stops it.

    // Closing the pad -- releasing the controller subsystem, or exiting -- removes the effect, so
    // a game that quits mid-rumble does not leave the pad buzzing.
    hub->Stop();
    const auto eraseDeadline = std::chrono::steady_clock::now() + kTimeout;
    while (pad->GetLog().erases == 0 && std::chrono::steady_clock::now() < eraseDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_GE(pad->GetLog().erases, 1);
    pad->StopServicing();
}

TEST(X11EvdevVirtualDevice, APadWithoutForceFeedbackRefusesRumbleQuietly)
{
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    const VirtualDevice::Spec spec = PadSpec("still");
    CREATE_OR_SKIP(pad, spec);
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }));
    gamepad.Update();
    const int slot = FindSlot(gamepad, spec.name);
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(gamepad.GetCapabilities(slot).rumble);
    EXPECT_FALSE(gamepad.SetRumble(slot, 1.0f, 1.0f, 100));
    EXPECT_FALSE(gamepad.SetTriggerRumble(slot, 1.0f, 1.0f, 100));
    EXPECT_FALSE(gamepad.SetLightBar(slot, 255, 0, 0));
}

// --- joysticks -----------------------------------------------------------------------------------

TEST(X11EvdevVirtualDevice, AFlightStickIsARawJoystickAndNotAGamepad)
{
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    EvdevJoystick joystick(hub);

    VirtualDevice::Spec spec;
    spec.name = UniqueName("stick");
    spec.product = 0x0002;
    spec.keys = {BTN_TRIGGER, BTN_THUMB, BTN_THUMB2};
    spec.axes = {{ABS_X, 0, 1023}, {ABS_Y, 0, 1023}, {ABS_THROTTLE, 0, 255},
                 {ABS_HAT0X, -1, 1}, {ABS_HAT0Y, -1, 1}};
    CREATE_OR_SKIP(stick, spec);

    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }));
    const EvdevControllerHub::Controller* controller = FindByName(hub, spec.name);
    EXPECT_EQ(controller->kind, EvdevDeviceClass::Joystick);
    EXPECT_EQ(controller->slot, -1);
    const DeviceId id = controller->id;

    std::vector<PlatformEvent> events;
    hub.TakeEvents(events);
    const std::vector<DeviceEvent> connections = EventsOf<DeviceEvent>(events, id);
    ASSERT_EQ(connections.size(), 1u);
    EXPECT_EQ(connections[0].kind, InputDeviceKind::Joystick);

    gamepad.Update();
    EXPECT_EQ(FindSlot(gamepad, spec.name), -1);

    joystick.Update();
    bool listed = false;
    for (const JoystickInfo& info : joystick.GetJoysticks())
    {
        if (info.id == id)
        {
            listed = true;
            EXPECT_EQ(info.name, spec.name);
            EXPECT_EQ(info.kind, JoystickKind::Unknown);
        }
    }
    EXPECT_TRUE(listed);
    EXPECT_TRUE(joystick.IsConnected(id));
    const JoystickCapabilities capabilities = joystick.GetCapabilities(id);
    EXPECT_TRUE(capabilities.connected);
    EXPECT_EQ(capabilities.axisCount, 3);  // X, Y, throttle; the hat is a hat.
    EXPECT_EQ(capabilities.buttonCount, 3);
    EXPECT_EQ(capabilities.hatCount, 1);
    EXPECT_EQ(capabilities.ballCount, 0);
    EXPECT_EQ(capabilities.name, spec.name);
    EXPECT_EQ(capabilities.guid, "03000000091200000200000000010000");

    stick->Emit(EV_KEY, BTN_THUMB, 1);
    stick->Emit(EV_ABS, ABS_X, 1023);
    stick->Emit(EV_ABS, ABS_THROTTLE, 0);
    stick->Emit(EV_ABS, ABS_HAT0X, 1);
    stick->Emit(EV_ABS, ABS_HAT0Y, -1);
    stick->Report();
    ASSERT_TRUE(PumpUntil(hub, [&] {
        joystick.Update();
        const JoystickSnapshot snapshot = joystick.GetSnapshot(id);
        return !snapshot.hats.empty() && snapshot.hats[0] == JoystickHat::RightUp;
    }));
    const JoystickSnapshot snapshot = joystick.GetSnapshot(id);
    ASSERT_EQ(snapshot.axes.size(), 3u);
    EXPECT_EQ(snapshot.axes[0], 32767);
    EXPECT_EQ(snapshot.axes[2], -32768);
    ASSERT_EQ(snapshot.buttons.size(), 3u);
    EXPECT_FALSE(snapshot.buttons[0]);  // Trigger.
    EXPECT_TRUE(snapshot.buttons[1]);   // Thumb.
    EXPECT_FALSE(snapshot.buttons[2]);

    // And no gamepad event for a device that is not a gamepad.
    events.clear();
    hub.TakeEvents(events);
    EXPECT_TRUE(EventsOf<ControllerButtonEvent>(events, id).empty());
}

// --- motion sensors (X11-0166) -----------------------------------------------------------------------

TEST(X11EvdevVirtualDevice, AMotionSensorNodeIsPairedWithItsPad)
{
    // Opt-in, and set only on CI's throwaway VM: an input accelerometer on a desktop is the
    // desktop's business too -- iio-sensor-proxy takes one for screen rotation -- so this suite
    // never creates one on a machine someone uses. Where it is opted in it must run: a device it
    // cannot create fails the test rather than skipping it.
    const char* optIn = std::getenv("CNA_X11_TEST_MOTION_SENSOR");
    if (optIn == nullptr || std::string(optIn) != "1")
    {
        GTEST_SKIP() << "opt-in (CNA_X11_TEST_MOTION_SENSOR=1): creates a virtual accelerometer";
    }
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    const std::string phys = "cna-motion-test-" + std::to_string(::getpid()) + "/input0";

    VirtualDevice::Spec padSpec = PadSpec("motion pad");
    padSpec.product = 0x0005;
    padSpec.phys = phys;
    CREATE_OR_FAIL(pad, padSpec);
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, padSpec.name) != nullptr; }));

    // hid-playstation's second node: the same physical path, an accelerometer's and a gyroscope's
    // axes in units per g and per degree per second.
    VirtualDevice::Spec sensorSpec;
    sensorSpec.name = UniqueName("motion sensors");
    sensorSpec.product = 0x0005;
    sensorSpec.phys = phys;
    sensorSpec.accelerometer = true;
    sensorSpec.axes = {{ABS_X, -32768, 32767, 8192},       {ABS_Y, -32768, 32767, 8192},
                       {ABS_Z, -32768, 32767, 8192},       {ABS_RX, -2097152, 2097151, 1024},
                       {ABS_RY, -2097152, 2097151, 1024}, {ABS_RZ, -2097152, 2097151, 1024}};
    CREATE_OR_FAIL(sensor, sensorSpec);
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, padSpec.name)->sensor != nullptr; }))
        << "the sensor node was never given to its pad";
    EXPECT_EQ(FindByName(hub, sensorSpec.name), nullptr) << "a sensor is not a controller of its own";

    gamepad.Update();
    const int slot = FindSlot(gamepad, padSpec.name);
    ASSERT_GE(slot, 0);
    EXPECT_TRUE(gamepad.GetCapabilities(slot).accelerometer);
    EXPECT_TRUE(gamepad.GetCapabilities(slot).gyroscope);

    sensor->Emit(EV_ABS, ABS_Y, 8192);
    sensor->Emit(EV_ABS, ABS_RZ, 1024 * 90);
    sensor->Report();
    GamepadSensorReading acceleration{};
    ASSERT_TRUE(PumpUntil(hub, [&] {
        return gamepad.TryGetSensor(slot, GamepadSensor::Accelerometer, acceleration) &&
               std::fabs(acceleration.y - 9.80665f) < 1e-3f;
    })) << "one g along Y never arrived";
    GamepadSensorReading rotation{};
    ASSERT_TRUE(gamepad.TryGetSensor(slot, GamepadSensor::Gyroscope, rotation));
    EXPECT_NEAR(rotation.z, 3.14159265f / 2.0f, 1e-3f);

    // The sensor node unplugged alone: the pad stays, its sensors go.
    sensor->Destroy();
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, padSpec.name)->sensor == nullptr; }));
    gamepad.Update();
    EXPECT_FALSE(gamepad.GetCapabilities(slot).accelerometer);
    EXPECT_FALSE(gamepad.TryGetSensor(slot, GamepadSensor::Gyroscope, rotation));
    EXPECT_NE(FindByName(hub, padSpec.name), nullptr);
}

// --- controller-database mappings (X11-0160) ---------------------------------------------------------

/// Sets one environment variable for a scope.
class ScopedVariable
{
public:
    ScopedVariable(std::string name, const std::string& value)
        : name_(std::move(name)), saved_(System::Environment::GetEnvironmentVariable(name_))
    {
        System::Environment::SetEnvironmentVariable(name_, value);
    }
    ~ScopedVariable() { System::Environment::SetEnvironmentVariable(name_, saved_); }
    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;

private:
    std::string name_;
    std::optional<std::string> saved_;
};

TEST(X11EvdevVirtualDevice, AGenericPadBecomesAGamepadThroughAMapping)
{
    // A generic HID pad: joystick buttons from BTN_TRIGGER, byte-wide axes, a hat -- no
    // gamepad-API code, so without a mapping it is only a joystick.
    VirtualDevice::Spec spec;
    spec.name = UniqueName("generic");
    spec.product = 0x0003;
    for (std::uint16_t code = BTN_TRIGGER; code <= BTN_BASE6; ++code)
    {
        spec.keys.push_back(code);
    }
    spec.axes = {{ABS_X, 0, 255},    {ABS_Y, 0, 255},    {ABS_Z, 0, 255},
                 {ABS_RZ, 0, 255},   {ABS_HAT0X, -1, 1}, {ABS_HAT0Y, -1, 1}};
    // Bus 3, vendor 0x1209, product 0x0003, version 0x0100, as databases write it.
    const ScopedVariable mapping(
        "CNA_GAMECONTROLLERCONFIG",
        "03000000091200000300000000010000,CNA Generic Test Pad,a:b2,b:b1,x:b3,y:b0,back:b8,"
        "start:b9,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,leftx:a0,"
        "lefty:a1,rightx:a2,righty:a3,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
        "platform:Linux,");
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    CREATE_OR_SKIP(pad, spec);

    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }));
    const EvdevControllerHub::Controller* controller = FindByName(hub, spec.name);
    EXPECT_EQ(controller->kind, EvdevDeviceClass::Gamepad);
    EXPECT_TRUE(controller->mapped);
    ASSERT_GE(controller->slot, 0);
    const DeviceId id = controller->id;

    std::vector<PlatformEvent> events;
    hub.TakeEvents(events);
    const std::vector<DeviceEvent> connections = EventsOf<DeviceEvent>(events, id);
    ASSERT_EQ(connections.size(), 2u);
    EXPECT_EQ(connections[1].kind, InputDeviceKind::Gamepad);

    gamepad.Update();
    const int slot = FindSlot(gamepad, spec.name);
    ASSERT_GE(slot, 0);
    EXPECT_EQ(gamepad.GetCapabilities(slot).axes, 0x3F);

    pad->Emit(EV_KEY, BTN_THUMB2, 1);  // b2: A
    pad->Emit(EV_ABS, ABS_Y, 0);        // a1 up
    pad->Emit(EV_ABS, ABS_HAT0Y, -1);   // hat up
    pad->Emit(EV_KEY, BTN_BASE, 1);     // b6: the left trigger, fully
    pad->Report();
    ASSERT_TRUE(PumpUntil(hub, [&] {
        gamepad.Update();
        return (gamepad.GetSnapshot(slot).buttons & static_cast<std::uint32_t>(GamepadButton::A)) != 0;
    }));
    const GamepadSnapshot& snapshot = gamepad.GetSnapshot(slot);
    EXPECT_NE(snapshot.buttons & static_cast<std::uint32_t>(GamepadButton::DPadUp), 0u);
    EXPECT_EQ(snapshot.buttons & static_cast<std::uint32_t>(GamepadButton::Y), 0u);
    EXPECT_EQ(Axis(snapshot, GamepadAxis::LeftThumbstickY), 1.0f);
    EXPECT_EQ(Axis(snapshot, GamepadAxis::LeftTrigger), 1.0f);

    events.clear();
    hub.TakeEvents(events);
    bool pressedA = false;
    for (const ControllerButtonEvent& button : EventsOf<ControllerButtonEvent>(events, id))
    {
        pressedA = pressedA || (button.button == GamepadButton::A && button.pressed);
    }
    EXPECT_TRUE(pressedA) << "the mapped press is an event too";
}

TEST(X11EvdevVirtualDevice, WithoutAMappingTheSamePadIsOnlyAJoystick)
{
    VirtualDevice::Spec spec;
    spec.name = UniqueName("unmapped");
    spec.product = 0x0003;
    for (std::uint16_t code = BTN_TRIGGER; code <= BTN_BASE6; ++code)
    {
        spec.keys.push_back(code);
    }
    spec.axes = {{ABS_X, 0, 255}, {ABS_Y, 0, 255}, {ABS_HAT0X, -1, 1}, {ABS_HAT0Y, -1, 1}};
    const ScopedVariable noMapping("CNA_GAMECONTROLLERCONFIG", "");
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    CREATE_OR_SKIP(pad, spec);
    ASSERT_TRUE(PumpUntil(hub, [&] { return FindByName(hub, spec.name) != nullptr; }));
    const EvdevControllerHub::Controller* controller = FindByName(hub, spec.name);
    EXPECT_EQ(controller->kind, EvdevDeviceClass::Joystick);
    EXPECT_FALSE(controller->mapped);
    EXPECT_EQ(controller->slot, -1);
}

// --- the platform ------------------------------------------------------------------------------

/// Takes DISPLAY away for one scope, so the platform is constructed with no X server at all.
class NoDisplay
{
public:
    NoDisplay() : saved_(System::Environment::GetEnvironmentVariable("DISPLAY"))
    {
        System::Environment::SetEnvironmentVariable("DISPLAY", std::nullopt);
    }
    ~NoDisplay()
    {
        System::Environment::SetEnvironmentVariable("DISPLAY", saved_);
    }
    NoDisplay(const NoDisplay&) = delete;
    NoDisplay& operator=(const NoDisplay&) = delete;

private:
    std::optional<std::string> saved_;
};

TEST(X11EvdevVirtualDevice, ThePlatformServesControllersEvenWithoutADisplay)
{
    // Controllers come from the kernel, not the X server: a process with no display -- a
    // dedicated server reading an admin's pad, a test -- still has them.
    std::unique_ptr<CNA::Platform::X11::X11Platform> platform;
    {
        NoDisplay noDisplay;
        platform = std::make_unique<CNA::Platform::X11::X11Platform>();
    }
    if (!std::filesystem::is_directory("/dev/input"))
    {
        EXPECT_FALSE(platform->GetCapabilities().gamepad);
        EXPECT_EQ(platform->GetGamepad(), nullptr);
        GTEST_SKIP() << "no /dev/input on this machine";
    }
    const PlatformCapabilities capabilities = platform->GetCapabilities();
    ASSERT_TRUE(capabilities.gamepad);
    ASSERT_TRUE(capabilities.joystick);
    EXPECT_TRUE(capabilities.gamepadRumble);
    EXPECT_TRUE(capabilities.gamepadSensors);    // Each pad says whether it has them (X11-0166).
    EXPECT_TRUE(capabilities.haptics);           // Force feedback is the kernel's too (X11-0168).
    EXPECT_NE(platform->GetHaptics(), nullptr);
    EXPECT_FALSE(capabilities.multipleWindows);  // Everything the display backs is still off.
    // So is battery state, which is the kernel's too (plans/plan_x11.md X11-0163).
    EXPECT_TRUE(capabilities.powerInfo);
    EXPECT_GT(platform->GetSystemInfo()->GetSystemMemoryMegabytes(), 0);
    EXPECT_EQ(platform->GetKeyboard(), nullptr);

    // Lazy: nothing is started until someone asks.
    EXPECT_FALSE(platform->IsSubsystemInitialized(PlatformSubsystem::Gamepad));
    IPlatformGamepad* gamepad = platform->GetGamepad();
    ASSERT_NE(gamepad, nullptr);
    EXPECT_TRUE(platform->IsSubsystemInitialized(PlatformSubsystem::Gamepad));
    EXPECT_EQ(platform->GetJoystick() != nullptr, capabilities.joystick);

    std::vector<PlatformEvent> events;
    platform->PollEvents(events);

    const VirtualDevice::Spec spec = PadSpec("platform");
    CREATE_OR_SKIP(pad, spec);

    // The platform's own once-per-frame calls, and nothing else, bring the pad in.
    IPlatformJoystick* joystick = platform->GetJoystick();
    DeviceId id = 0;
    std::vector<PlatformEvent> seen;
    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (id == 0 && std::chrono::steady_clock::now() < deadline)
    {
        platform->PollEvents(events);
        seen.insert(seen.end(), events.begin(), events.end());
        joystick->Update();
        for (const JoystickInfo& info : joystick->GetJoysticks())
        {
            if (info.name == spec.name)
            {
                id = info.id;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_NE(id, 0u) << "the platform never picked up " << spec.name;
    // The connection arrived through PollEvents, under the id the joystick service uses.
    platform->PollEvents(events);
    seen.insert(seen.end(), events.begin(), events.end());
    const std::vector<DeviceEvent> connections = EventsOf<DeviceEvent>(seen, id);
    ASSERT_EQ(connections.size(), 2u);
    EXPECT_EQ(connections[1].kind, InputDeviceKind::Gamepad);
    EXPECT_TRUE(connections[1].connected);

    pad->Emit(EV_KEY, BTN_START, 1);
    pad->Report();
    bool pressed = false;
    const auto pressDeadline = std::chrono::steady_clock::now() + kTimeout;
    while (!pressed && std::chrono::steady_clock::now() < pressDeadline)
    {
        platform->PollEvents(events);
        pressed = !EventsOf<ControllerButtonEvent>(events, id).empty();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(pressed);
    gamepad->Update();
    const int slot = FindSlot(*gamepad, spec.name);
    ASSERT_GE(slot, 0);
    EXPECT_EQ(gamepad->GetSnapshot(slot).buttons, static_cast<std::uint32_t>(GamepadButton::Start));

    // Releasing the subsystem closes the devices; the services stay, reporting nothing.
    platform->ReleaseSubsystem(PlatformSubsystem::Gamepad);
    EXPECT_FALSE(platform->IsSubsystemInitialized(PlatformSubsystem::Gamepad));
    EXPECT_EQ(platform->GetGamepad(), gamepad);
    EXPECT_FALSE(platform->GetJoystick()->IsConnected(id));
    EXPECT_EQ(FindSlot(*gamepad, spec.name), -1);

    // And acquiring it again finds the pad again, under a new id.
    platform->AcquireSubsystem(PlatformSubsystem::Gamepad);
    const auto againDeadline = std::chrono::steady_clock::now() + kTimeout;
    while (FindSlot(*gamepad, spec.name) < 0 && std::chrono::steady_clock::now() < againDeadline)
    {
        gamepad->Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_GE(FindSlot(*gamepad, spec.name), 0);
}

// --- force feedback: the haptics service (plans/plan_x11.md X11-0168) ----------------------------

/// A force-feedback wheel: a joystick, not a gamepad, with the effects a wheel driver offers.
VirtualDevice::Spec WheelSpec(const char* role)
{
    VirtualDevice::Spec spec;
    spec.name = UniqueName(role);
    spec.product = 0x0006;
    spec.keys = {BTN_TRIGGER, BTN_THUMB, BTN_THUMB2, BTN_TOP};
    spec.axes = {{ABS_X, -32768, 32767}, {ABS_Z, 0, 255}, {ABS_RZ, 0, 255}};
    spec.forceFeedback = {FF_CONSTANT, FF_PERIODIC, FF_SINE, FF_SQUARE, FF_SPRING, FF_DAMPER, FF_GAIN, FF_AUTOCENTER};
    return spec;
}

std::optional<HapticInfo> WaitForHaptic(const IPlatformHaptics& haptics, const std::string& name)
{
    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        for (const HapticInfo& info : haptics.GetHaptics())
        {
            if (info.name == name)
            {
                return info;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

/// Waits until a device's force-feedback log satisfies a condition, and returns it.
VirtualDevice::ForceFeedbackLog WaitForLog(VirtualDevice& device,
                                           const std::function<bool(const VirtualDevice::ForceFeedbackLog&)>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    VirtualDevice::ForceFeedbackLog log = device.GetLog();
    while (!done(log) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        log = device.GetLog();
    }
    return log;
}

TEST(X11EvdevVirtualDevice, AWheelsEffectsAreUploadedPlayedUpdatedAndErasedThroughTheKernel)
{
    const VirtualDevice::Spec spec = WheelSpec("wheel");
    CREATE_OR_SKIP(wheel, spec);
    wheel->StartServicing();
    EvdevHaptics haptics({});
    const std::optional<HapticInfo> info = WaitForHaptic(haptics, spec.name);
    ASSERT_TRUE(info.has_value()) << "the wheel was never listed";
    EXPECT_GT(info->id, kEvdevHapticIdBase);
    EXPECT_TRUE(info->rumbleSupported) << "it has a sine";
    EXPECT_TRUE(haptics.IsConnected(info->id));
    EXPECT_EQ(WaitForHaptic(haptics, spec.name)->id, info->id) << "the id is kept while it is connected";

    std::unique_ptr<IPlatformHapticDevice> device = haptics.Open(info->id);
    ASSERT_NE(device, nullptr);
    const HapticDeviceCapabilities capabilities = device->GetCapabilities();
    EXPECT_EQ(capabilities.name, spec.name);
    // Constant, sine, square, spring, damper, gain, autocenter -- and left/right, which the kernel
    // gives every device that has periodic effects.
    EXPECT_EQ(capabilities.features, (1u << 0) | (1u << 1) | (1u << 2) | (1u << 7) | (1u << 8) | (1u << 11) |
                                         (1u << 16) | (1u << 17));
    EXPECT_EQ(capabilities.maxEffects, 16);
    EXPECT_EQ(capabilities.maxEffectsPlaying, -1) << "the kernel does not say";
    EXPECT_TRUE(capabilities.rumbleSupported);

    HapticEffect push;
    push.type = HapticEffectType::Constant;
    push.direction.type = HapticDirectionType::Polar;
    push.direction.values = {9000, 0, 0};
    push.length = 800;
    push.level = 12000;
    const int id = device->CreateEffect(push);
    ASSERT_GE(id, 0);
    push.level = -6000;
    EXPECT_TRUE(device->UpdateEffect(id, push));
    EXPECT_TRUE(device->RunEffect(id, 3));
    EXPECT_TRUE(device->StopEffect(id));
    EXPECT_TRUE(device->SetGain(50));
    EXPECT_TRUE(device->SetAutocenter(100));

    HapticEffect triangle;
    triangle.type = HapticEffectType::Triangle;
    EXPECT_FALSE(device->IsEffectSupported(triangle));
    EXPECT_EQ(device->CreateEffect(triangle), -1) << "a waveform the wheel does not have";
    HapticEffect sine;
    sine.type = HapticEffectType::Sine;
    EXPECT_FALSE(device->UpdateEffect(id, sine)) << "the kernel keeps an effect's family";
    EXPECT_FALSE(device->RunEffect(id + 5, 1)) << "not an effect of this device";
    EXPECT_FALSE(device->GetEffectStatus(id));
    EXPECT_FALSE(device->Pause());

    VirtualDevice::ForceFeedbackLog log =
        WaitForLog(*wheel, [](const auto& seen) { return seen.plays.size() >= 4; });
    ASSERT_EQ(log.uploads.size(), 2u) << "the triangle and the change of family never reached the driver";
    EXPECT_EQ(log.uploads[0].type, FF_CONSTANT);
    EXPECT_EQ(log.uploads[0].direction, 0x4000);
    EXPECT_EQ(log.uploads[0].replay.length, 800);
    EXPECT_EQ(log.uploads[0].u.constant.level, 12000);
    EXPECT_EQ(log.uploads[1].id, id);
    EXPECT_EQ(log.uploads[1].u.constant.level, -6000);
    ASSERT_EQ(log.plays.size(), 4u);
    EXPECT_EQ(log.plays[0], std::make_pair(id, 3));
    EXPECT_EQ(log.plays[1], std::make_pair(id, 0));
    EXPECT_EQ(log.plays[2], std::make_pair(static_cast<int>(FF_GAIN), 0x7FFF));
    EXPECT_EQ(log.plays[3], std::make_pair(static_cast<int>(FF_AUTOCENTER), 0xFFFF));

    device->DestroyEffect(id);
    log = WaitForLog(*wheel, [](const auto& seen) { return seen.erases >= 1; });
    ASSERT_EQ(log.erased.size(), 1u);
    EXPECT_EQ(log.erased[0], id);

    // What a device leaves uploaded is erased when it is closed: nothing keeps pushing.
    const int left = device->CreateEffect(push);
    ASSERT_GE(left, 0);
    ASSERT_TRUE(device->RunEffect(left, 1));
    device.reset();
    log = WaitForLog(*wheel, [](const auto& seen) { return seen.erases >= 2; });
    ASSERT_EQ(log.erased.size(), 2u);
    EXPECT_EQ(log.erased[1], left);
    wheel->StopServicing();
}

TEST(X11EvdevVirtualDevice, SimpleRumbleIsASineOnAWheelAndBothMotorsOnAPad)
{
    const VirtualDevice::Spec wheelSpec = WheelSpec("rumbling wheel");
    VirtualDevice::Spec padSpec = PadSpec("rumbling pad");
    padSpec.rumble = true;
    CREATE_OR_SKIP(wheel, wheelSpec);
    CREATE_OR_SKIP(pad, padSpec);
    wheel->StartServicing();
    pad->StartServicing();
    EvdevHaptics haptics({});
    const std::optional<HapticInfo> wheelInfo = WaitForHaptic(haptics, wheelSpec.name);
    const std::optional<HapticInfo> padInfo = WaitForHaptic(haptics, padSpec.name);
    ASSERT_TRUE(wheelInfo && padInfo);
    EXPECT_TRUE(haptics.SupportsRumble(padInfo->id));

    ASSERT_TRUE(haptics.PlayRumble(wheelInfo->id, 0.5f, 300));
    VirtualDevice::ForceFeedbackLog log =
        WaitForLog(*wheel, [](const auto& seen) { return !seen.plays.empty(); });
    // Readied at half strength for five seconds, then played as asked.
    ASSERT_EQ(log.uploads.size(), 2u);
    EXPECT_EQ(log.uploads[0].type, FF_PERIODIC);
    EXPECT_EQ(log.uploads[0].u.periodic.waveform, FF_SINE);
    EXPECT_EQ(log.uploads[1].u.periodic.magnitude, 16383);
    EXPECT_EQ(log.uploads[1].u.periodic.period, 1000);
    EXPECT_EQ(log.uploads[1].replay.length, 300);
    EXPECT_EQ(log.plays.at(0), std::make_pair(static_cast<int>(log.uploads[1].id), 1));

    ASSERT_TRUE(haptics.PlayRumble(padInfo->id, 1.0f, 250));
    log = WaitForLog(*pad, [](const auto& seen) { return !seen.plays.empty(); });
    ASSERT_EQ(log.uploads.size(), 2u);
    EXPECT_EQ(log.uploads[1].type, FF_RUMBLE);
    EXPECT_EQ(log.uploads[1].u.rumble.strong_magnitude, 0xFFFF);
    EXPECT_EQ(log.uploads[1].u.rumble.weak_magnitude, 0xFFFF);
    EXPECT_EQ(log.uploads[1].replay.length, 250);
    const int rumble = log.uploads[1].id;

    // The two motors apart: the simple rumble is stopped, the pair played as an effect of its own.
    ASSERT_TRUE(haptics.PlayLeftRight(padInfo->id, 0.25f, 1.0f, 100));
    log = WaitForLog(*pad, [](const auto& seen) { return seen.plays.size() >= 3; });
    ASSERT_EQ(log.uploads.size(), 3u);
    EXPECT_EQ(log.uploads[2].u.rumble.strong_magnitude, 16383);
    EXPECT_EQ(log.uploads[2].u.rumble.weak_magnitude, 0xFFFF);
    const int pair = log.uploads[2].id;
    EXPECT_NE(pair, rumble);
    ASSERT_EQ(log.plays.size(), 3u);
    EXPECT_EQ(log.plays[1], std::make_pair(rumble, 0));
    EXPECT_EQ(log.plays[2], std::make_pair(pair, 1));

    EXPECT_TRUE(haptics.StopRumble(padInfo->id));
    log = WaitForLog(*pad, [](const auto& seen) { return seen.plays.size() >= 5; });
    ASSERT_EQ(log.plays.size(), 5u) << "both the simple rumble and the pair are stopped";

    // A release of the Haptic subsystem closes what the service opened.
    haptics.CloseAll();
    log = WaitForLog(*pad, [](const auto& seen) { return seen.erases >= 2; });
    EXPECT_EQ(log.erases, 2);
    EXPECT_GE(WaitForLog(*wheel, [](const auto& seen) { return seen.erases >= 1; }).erases, 1);
    wheel->StopServicing();
    pad->StopServicing();
}

TEST(X11EvdevVirtualDevice, AJoysticksForceFeedbackIsFoundFromItsControllerId)
{
    VirtualDevice::Spec rumbling = PadSpec("haptic joystick");
    rumbling.rumble = true;
    const VirtualDevice::Spec still = PadSpec("still joystick");
    CREATE_OR_SKIP(pad, rumbling);
    CREATE_OR_SKIP(quiet, still);
    pad->StartServicing();
    EvdevControllerHub hub;
    ASSERT_TRUE(hub.Start());
    ASSERT_TRUE(PumpUntil(hub, [&] {
        return FindByName(hub, rumbling.name) != nullptr && FindByName(hub, still.name) != nullptr;
    }));
    EvdevHaptics haptics([&hub](const DeviceId id) {
        const EvdevControllerHub::Controller* controller = hub.FindById(id);
        return controller != nullptr ? controller->device->GetPath() : std::string();
    });
    const DeviceId padId = FindByName(hub, rumbling.name)->id;
    const DeviceId quietId = FindByName(hub, still.name)->id;
    EXPECT_TRUE(haptics.IsJoystickHaptic(padId));
    EXPECT_FALSE(haptics.IsJoystickHaptic(quietId));
    EXPECT_FALSE(haptics.IsJoystickHaptic(999999)) << "no such joystick";
    EXPECT_EQ(haptics.OpenFromJoystick(quietId), nullptr);
    std::unique_ptr<IPlatformHapticDevice> device = haptics.OpenFromJoystick(padId);
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->GetCapabilities().name, rumbling.name);
    ASSERT_TRUE(device->InitializeRumble());
    EXPECT_TRUE(device->PlayRumble(0.75f, 100));
    const VirtualDevice::ForceFeedbackLog log = WaitForLog(*pad, [](const auto& seen) { return !seen.plays.empty(); });
    ASSERT_EQ(log.uploads.size(), 2u);
    EXPECT_EQ(log.uploads[1].u.rumble.strong_magnitude, static_cast<std::uint16_t>(65535.0f * 0.75f));
    device.reset();
    pad->StopServicing();
}

TEST(X11EvdevVirtualDevice, TheDefaultVibrationDeviceIsNotAGamepad)
{
    VirtualDevice::Spec padSpec = PadSpec("vibrating pad");
    padSpec.rumble = true;
    VirtualDevice::Spec motorSpec;
    motorSpec.name = UniqueName("vibration motor");
    motorSpec.product = 0x0007;
    motorSpec.rumble = true;  // Nothing else: a phone's vibrator, as the kernel presents one.
    CREATE_OR_SKIP(pad, padSpec);
    CREATE_OR_SKIP(motor, motorSpec);
    pad->StartServicing();
    motor->StartServicing();
    EvdevHaptics haptics({});
    ASSERT_TRUE(WaitForHaptic(haptics, padSpec.name).has_value());
    const std::optional<HapticInfo> motorInfo = WaitForHaptic(haptics, motorSpec.name);
    ASSERT_TRUE(motorInfo.has_value());

    const std::optional<HapticInfo> chosen = haptics.GetDefaultVibrationDevice();
    ASSERT_TRUE(chosen.has_value());
    EXPECT_NE(chosen->name, padSpec.name) << "a gamepad's rumble is the gamepad's";
    EXPECT_TRUE(chosen->rumbleSupported);
    std::size_t others = 0;
    for (const HapticInfo& info : haptics.GetHaptics())
    {
        others += info.name != padSpec.name && info.name != motorSpec.name ? 1 : 0;
    }
    if (others == 0)
    {
        EXPECT_EQ(chosen->id, motorInfo->id) << "the only vibration device that is not a gamepad";
    }
    pad->StopServicing();
    motor->StopServicing();
}

TEST(X11EvdevVirtualDevice, ReleasingTheHapticSubsystemStopsWhatThePlatformPlayed)
{
    std::unique_ptr<CNA::Platform::X11::X11Platform> platform;
    {
        NoDisplay noDisplay;
        platform = std::make_unique<CNA::Platform::X11::X11Platform>();
    }
    VirtualDevice::Spec spec = PadSpec("platform haptic");
    spec.rumble = true;
    CREATE_OR_SKIP(pad, spec);
    pad->StartServicing();
    platform->AcquireSubsystem(PlatformSubsystem::Haptic);
    IPlatformHaptics* haptics = platform->GetHaptics();
    ASSERT_NE(haptics, nullptr);
    const std::optional<HapticInfo> info = WaitForHaptic(*haptics, spec.name);
    ASSERT_TRUE(info.has_value());
    ASSERT_TRUE(haptics->PlayRumble(info->id, 0.5f, UINT32_MAX));
    VirtualDevice::ForceFeedbackLog log = WaitForLog(*pad, [](const auto& seen) { return !seen.plays.empty(); });
    ASSERT_FALSE(log.uploads.empty());
    EXPECT_EQ(log.uploads.back().replay.length, 0) << "until stopped";
    EXPECT_EQ(log.erases, 0);

    platform->ReleaseSubsystem(PlatformSubsystem::Haptic);
    log = WaitForLog(*pad, [](const auto& seen) { return seen.erases >= 1; });
    EXPECT_EQ(log.erases, 1) << "the device was closed, and the kernel erased its effect";
    pad->StopServicing();
}

TEST(X11EvdevVirtualDevice, AnUnpluggedHapticDeviceAnswersFalse)
{
    VirtualDevice::Spec spec = PadSpec("unplugged haptic");
    spec.rumble = true;
    CREATE_OR_SKIP(pad, spec);
    pad->StartServicing();
    EvdevHaptics haptics({});
    const std::optional<HapticInfo> info = WaitForHaptic(haptics, spec.name);
    ASSERT_TRUE(info.has_value());
    ASSERT_TRUE(haptics.PlayRumble(info->id, 1.0f, 100));
    pad->StopServicing();
    pad->Destroy();
    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (haptics.IsConnected(info->id) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_FALSE(haptics.IsConnected(info->id));
    EXPECT_FALSE(haptics.PlayRumble(info->id, 1.0f, 100)) << "unplugged mid-effect is ordinary, not an exception";
    EXPECT_FALSE(haptics.SupportsRumble(info->id));
    EXPECT_EQ(haptics.Open(info->id), nullptr);
}

} // namespace

#endif // CNA_PLATFORM_HAVE_EVDEV
