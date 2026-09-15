// SPDX-License-Identifier: MS-PL

#include "EvdevControllers.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <filesystem>

#include <sys/inotify.h>
#include <unistd.h>

namespace CNA::Platform::Linux {

    namespace {

        // Bounded so an application that never drains events -- none of CNA's own consumers
        // reads controller events today; the XNA layer reads snapshots -- cannot grow the queue
        // without limit. The oldest events go first.
        constexpr std::size_t kMaximumPendingEvents = 4096;

        // Without an inotify watch (a kernel without it, a sandbox that forbids it), connections
        // are found by re-reading the directory at most this often.
        constexpr auto kRescanInterval = std::chrono::seconds(1);

        bool IsEventNode(const std::string& name)
        {
            return name.rfind("event", 0) == 0;
        }

        bool IsHatAxis(const std::uint16_t code)
        {
            return code >= ABS_HAT0X && code <= ABS_HAT3Y;
        }

        GamepadConnectionState ConnectionOf(const EvdevDescription& description)
        {
            switch (description.bus)
            {
                case BUS_USB:
                    return GamepadConnectionState::Wired;
                case BUS_BLUETOOTH:
                    return GamepadConnectionState::Wireless;
                default:
                    return GamepadConnectionState::Unknown;
            }
        }

    } // namespace

    // --- helpers ------------------------------------------------------------------------------

    std::string FormatEvdevGuid(const EvdevDescription& description)
    {
        std::array<std::uint8_t, 16> bytes{};
        const auto put = [&bytes](const std::size_t at, const std::uint16_t value) {
            bytes[at] = static_cast<std::uint8_t>(value & 0xFFu);
            bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
        };
        put(0, description.bus);
        put(4, description.vendor);
        put(8, description.product);
        put(12, description.version);
        std::string guid;
        guid.reserve(32);
        for (const std::uint8_t byte : bytes)
        {
            char digits[3] = {};
            std::snprintf(digits, sizeof(digits), "%02x", static_cast<unsigned>(byte));
            guid += digits;
        }
        return guid;
    }

    JoystickHat EvdevHatPosition(const int x, const int y)
    {
        if (y < 0)
        {
            return x < 0 ? JoystickHat::LeftUp : (x > 0 ? JoystickHat::RightUp : JoystickHat::Up);
        }
        if (y > 0)
        {
            return x < 0 ? JoystickHat::LeftDown
                         : (x > 0 ? JoystickHat::RightDown : JoystickHat::Down);
        }
        return x < 0 ? JoystickHat::Left : (x > 0 ? JoystickHat::Right : JoystickHat::Centered);
    }

    // --- EvdevControllerHub -------------------------------------------------------------------

    EvdevControllerHub::EvdevControllerHub(std::string directory, std::string sysfsRoot)
        : directory_(std::move(directory)), sysfsRoot_(std::move(sysfsRoot))
    {
    }

    EvdevControllerHub::~EvdevControllerHub()
    {
        Stop();
    }

    bool EvdevControllerHub::Start()
    {
        if (started_)
        {
            return true;
        }
        std::error_code error;
        if (!std::filesystem::is_directory(directory_, error))
        {
            return false;
        }
        watch_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (watch_ >= 0 &&
            inotify_add_watch(watch_, directory_.c_str(),
                              IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_TO | IN_MOVED_FROM) < 0)
        {
            ::close(watch_);
            watch_ = -1;
        }
        started_ = true;
        Scan();
        return true;
    }

    void EvdevControllerHub::Stop()
    {
        controllers_.clear();
        refused_.clear();
        pending_.clear();
        if (watch_ >= 0)
        {
            ::close(watch_);
            watch_ = -1;
        }
        started_ = false;
    }

    EvdevControllerHub::Controller* EvdevControllerHub::FindById(const DeviceId id) const
    {
        for (const auto& controller : controllers_)
        {
            if (controller->id == id)
            {
                return controller.get();
            }
        }
        return nullptr;
    }

    EvdevControllerHub::Controller* EvdevControllerHub::FindBySlot(const int slot) const
    {
        if (slot < 0)
        {
            return nullptr;
        }
        for (const auto& controller : controllers_)
        {
            if (controller->slot == slot)
            {
                return controller.get();
            }
        }
        return nullptr;
    }

    void EvdevControllerHub::Push(PlatformEvent event)
    {
        pending_.push_back(std::move(event));
        while (pending_.size() > kMaximumPendingEvents)
        {
            pending_.pop_front();
        }
    }

    void EvdevControllerHub::TakeEvents(std::vector<PlatformEvent>& destination)
    {
        for (PlatformEvent& event : pending_)
        {
            destination.push_back(std::move(event));
        }
        pending_.clear();
    }

    void EvdevControllerHub::Scan()
    {
        std::error_code error;
        std::filesystem::directory_iterator entries(directory_, error);
        if (error)
        {
            return;
        }
        std::vector<std::string> names;
        for (const auto& entry : entries)
        {
            const std::string name = entry.path().filename().string();
            if (IsEventNode(name))
            {
                names.push_back(name);
            }
        }
        // Numeric order, so two runs with the same devices give them the same ids and slots.
        std::sort(names.begin(), names.end(), [](const std::string& left, const std::string& right) {
            return left.size() != right.size() ? left.size() < right.size() : left < right;
        });
        for (const std::string& name : names)
        {
            TryOpen(name);
        }
    }

    void EvdevControllerHub::ReadWatch()
    {
        alignas(inotify_event) std::array<char, 4096> buffer{};
        while (true)
        {
            const ssize_t bytes = ::read(watch_, buffer.data(), buffer.size());
            if (bytes <= 0)
            {
                return;
            }
            for (ssize_t offset = 0; offset < bytes;)
            {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
                offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
                if ((event->mask & IN_Q_OVERFLOW) != 0)
                {
                    // Changes were lost; the directory itself is the truth.
                    refused_.clear();
                    Scan();
                    continue;
                }
                if (event->len == 0)
                {
                    continue;
                }
                const std::string name(event->name);
                if (!IsEventNode(name))
                {
                    continue;
                }
                if ((event->mask & (IN_DELETE | IN_MOVED_FROM)) != 0)
                {
                    refused_.erase(name);
                    Close(name);
                    continue;
                }
                if ((event->mask & IN_ATTRIB) != 0)
                {
                    // udev grants the session its controllers a moment after the node appears; a
                    // node refused at IN_CREATE deserves another try once its permissions change.
                    refused_.erase(name);
                }
                TryOpen(name);
            }
        }
    }

    void EvdevControllerHub::TryOpen(const std::string& name)
    {
        const std::string path = (std::filesystem::path(directory_) / name).string();
        if (refused_.count(name) != 0)
        {
            return;
        }
        for (const auto& controller : controllers_)
        {
            if (controller->device->GetPath() == path)
            {
                return;
            }
        }
        // Asked of sysfs first, so a keyboard or a mouse is never opened at all (see
        // ReadEvdevSysfsDescription for what opening them cost). Only a node sysfs cannot describe
        // -- no sysfs in a container, a test's own directory -- is opened to be asked.
        EvdevDescription preview;
        if (ReadEvdevSysfsDescription(name, preview, sysfsRoot_) &&
            ClassifyEvdevDevice(preview) == EvdevDeviceClass::None)
        {
            refused_.insert(name);
            return;
        }
        std::unique_ptr<EvdevDevice> device = EvdevDevice::Open(path);
        if (device == nullptr)
        {
            refused_.insert(name);
            return;
        }
        const EvdevDescription& description = device->GetDescription();
        const EvdevDeviceClass kind = ClassifyEvdevDevice(description);
        if (kind == EvdevDeviceClass::None)
        {
            // Not a controller. Closed at once: holding someone's keyboard open is not ours to do.
            refused_.insert(name);
            return;
        }

        auto controller = std::make_unique<Controller>();
        controller->id = nextId_++;
        controller->kind = kind;
        if (kind == EvdevDeviceClass::Gamepad)
        {
            controller->gamepad =
                std::make_unique<EvdevGamepadState>(BuildEvdevGamepadLayout(description));
            for (int slot = 0; slot < GamepadSlotCount; ++slot)
            {
                if (FindBySlot(slot) == nullptr)
                {
                    controller->slot = slot;
                    break;
                }
            }
        }
        for (std::uint16_t code = 0; code < ABS_CNT; ++code)
        {
            if (description.axes.test(code) && !IsHatAxis(code))
            {
                controller->axisCodes.push_back(code);
            }
        }
        controller->axes.assign(controller->axisCodes.size(), 0);
        // Buttons in the order controller databases number them: the joystick and gamepad
        // ranges and everything above them first, then the miscellaneous buttons below them.
        for (std::uint32_t code = BTN_JOYSTICK; code < KEY_CNT; ++code)
        {
            if (description.keys.test(code))
            {
                controller->buttonCodes.push_back(static_cast<std::uint16_t>(code));
            }
        }
        for (std::uint32_t code = BTN_MISC; code < BTN_JOYSTICK; ++code)
        {
            if (description.keys.test(code))
            {
                controller->buttonCodes.push_back(static_cast<std::uint16_t>(code));
            }
        }
        controller->buttons.assign(controller->buttonCodes.size(), false);
        for (int hat = 0; hat < 4; ++hat)
        {
            if (description.axes.test(static_cast<std::size_t>(ABS_HAT0X + 2 * hat)) ||
                description.axes.test(static_cast<std::size_t>(ABS_HAT0Y + 2 * hat)))
            {
                controller->hatCount = hat + 1;
            }
        }

        // The pad's state at the moment it is opened -- a trigger already held, a stick resting
        // off centre -- applied silently: nothing changed, it was simply first seen like this.
        controller->device = std::move(device);
        scratch_.clear();
        controller->device->AppendCurrentState(scratch_);
        for (const input_event& event : scratch_)
        {
            if (controller->gamepad != nullptr)
            {
                changes_.clear();
                controller->gamepad->Apply(event.type, event.code, event.value, changes_);
            }
            ApplyRaw(*controller, event);
        }

        const DeviceId id = controller->id;
        const bool gamepad = controller->kind == EvdevDeviceClass::Gamepad;
        controllers_.push_back(std::move(controller));
        // After the controller is in the list: a consumer that reacts to the event by asking the
        // joystick service about the id must find it connected.
        Push(DeviceEvent{id, InputDeviceKind::Joystick, true});
        if (gamepad)
        {
            Push(DeviceEvent{id, InputDeviceKind::Gamepad, true});
        }
    }

    void EvdevControllerHub::Close(const std::string& name)
    {
        const std::string path = (std::filesystem::path(directory_) / name).string();
        for (std::size_t index = 0; index < controllers_.size(); ++index)
        {
            if (controllers_[index]->device->GetPath() == path)
            {
                Remove(index);
                return;
            }
        }
    }

    void EvdevControllerHub::Remove(const std::size_t index)
    {
        const DeviceId id = controllers_[index]->id;
        const bool gamepad = controllers_[index]->kind == EvdevDeviceClass::Gamepad;
        const int slot = controllers_[index]->slot;
        controllers_.erase(controllers_.begin() + static_cast<std::ptrdiff_t>(index));
        if (slot >= 0)
        {
            // A fifth pad waits without a slot; the first one to connect takes the freed slot,
            // as it would have had it connected into an empty one.
            for (const auto& waiting : controllers_)
            {
                if (waiting->kind == EvdevDeviceClass::Gamepad && waiting->slot < 0)
                {
                    waiting->slot = slot;
                    break;
                }
            }
        }
        if (gamepad)
        {
            Push(DeviceEvent{id, InputDeviceKind::Gamepad, false});
        }
        Push(DeviceEvent{id, InputDeviceKind::Joystick, false});
    }

    void EvdevControllerHub::ApplyRaw(Controller& controller, const input_event& event)
    {
        if (event.type == EV_KEY)
        {
            for (std::size_t index = 0; index < controller.buttonCodes.size(); ++index)
            {
                if (controller.buttonCodes[index] == event.code)
                {
                    controller.buttons[index] = event.value != 0;
                    return;
                }
            }
            return;
        }
        if (event.type != EV_ABS)
        {
            return;
        }
        if (IsHatAxis(event.code))
        {
            controller.hatValues[event.code - ABS_HAT0X] = (event.value > 0) - (event.value < 0);
            return;
        }
        for (std::size_t index = 0; index < controller.axisCodes.size(); ++index)
        {
            if (controller.axisCodes[index] == event.code)
            {
                controller.axes[index] = static_cast<std::int16_t>(ScaleEvdevAxis(
                    event.value, controller.device->GetDescription().ranges[event.code]));
                return;
            }
        }
    }

    void EvdevControllerHub::Pump()
    {
        if (!started_)
        {
            return;
        }
        if (watch_ >= 0)
        {
            ReadWatch();
        }
        else if (std::chrono::steady_clock::now() >= nextScan_)
        {
            nextScan_ = std::chrono::steady_clock::now() + kRescanInterval;
            Scan();
        }

        std::vector<DeviceId> gone;
        for (const auto& controller : controllers_)
        {
            scratch_.clear();
            const bool alive = controller->device->Drain(scratch_);
            for (const input_event& event : scratch_)
            {
                if (controller->gamepad != nullptr)
                {
                    changes_.clear();
                    controller->gamepad->Apply(event.type, event.code, event.value, changes_);
                    for (const EvdevGamepadChange& change : changes_)
                    {
                        if (change.isButton)
                        {
                            Push(ControllerButtonEvent{controller->id, change.button, change.pressed});
                        }
                        else
                        {
                            Push(ControllerAxisEvent{controller->id, change.axis, change.value});
                        }
                    }
                }
                ApplyRaw(*controller, event);
            }
            if (!alive)
            {
                gone.push_back(controller->id);
            }
        }
        for (const DeviceId id : gone)
        {
            for (std::size_t index = 0; index < controllers_.size(); ++index)
            {
                if (controllers_[index]->id == id)
                {
                    Remove(index);
                    break;
                }
            }
        }
    }

    // --- EvdevGamepad -------------------------------------------------------------------------

    EvdevGamepad::EvdevGamepad(EvdevControllerHub& hub) : hub_(hub) {}

    bool EvdevGamepad::IsValid(const int index) const
    {
        return index >= 0 && index < GamepadSlotCount;
    }

    void EvdevGamepad::Update()
    {
        hub_.Pump();
        for (int slot = 0; slot < GamepadSlotCount; ++slot)
        {
            const auto at = static_cast<std::size_t>(slot);
            const EvdevControllerHub::Controller* controller = hub_.FindBySlot(slot);
            if (controller == nullptr)
            {
                if (occupant_[at] != 0)
                {
                    // A disconnect resets the slot, packet number included, so a pad that
                    // connects into it later starts from a clean count.
                    snapshots_[at] = GamepadSnapshot{};
                    capabilities_[at] = GamepadCapabilities{};
                    info_[at] = GamepadInfo{};
                    occupant_[at] = 0;
                }
                continue;
            }
            if (occupant_[at] != controller->id)
            {
                const EvdevDescription& description = controller->device->GetDescription();
                const EvdevGamepadLayout& layout = controller->gamepad->GetLayout();
                GamepadCapabilities capabilities;
                capabilities.connected = true;
                capabilities.buttons = layout.buttonMask;
                capabilities.axes = layout.axisMask;
                capabilities.kind = GamepadKind::Gamepad;
                capabilities.rumble = controller->device->CanRumble();
                capabilities_[at] = capabilities;
                GamepadInfo info;
                info.name = description.name;
                info.path = controller->device->GetPath();
                info.serial = description.uniq;
                info.vendor = description.vendor;
                info.product = description.product;
                info.firmwareVersion = description.version;
                info.connectionState = ConnectionOf(description);
                info.model = layout.model;
                info_[at] = info;
                snapshots_[at] = GamepadSnapshot{};
                occupant_[at] = controller->id;
            }
            GamepadSnapshot& snapshot = snapshots_[at];
            const bool changed = !snapshot.connected ||
                                 snapshot.buttons != controller->gamepad->GetButtons() ||
                                 snapshot.axes != controller->gamepad->GetAxes();
            if (changed)
            {
                snapshot.connected = true;
                snapshot.buttons = controller->gamepad->GetButtons();
                snapshot.axes = controller->gamepad->GetAxes();
                ++snapshot.packetNumber;
            }
        }
    }

    const GamepadSnapshot& EvdevGamepad::GetSnapshot(const int index) const
    {
        static const GamepadSnapshot empty{};
        return IsValid(index) ? snapshots_[static_cast<std::size_t>(index)] : empty;
    }

    std::string EvdevGamepad::GetName(const int index) const
    {
        return IsValid(index) ? info_[static_cast<std::size_t>(index)].name : std::string();
    }

    const GamepadCapabilities& EvdevGamepad::GetCapabilities(const int index) const
    {
        static const GamepadCapabilities empty{};
        return IsValid(index) ? capabilities_[static_cast<std::size_t>(index)] : empty;
    }

    const GamepadInfo& EvdevGamepad::GetInfo(const int index) const
    {
        static const GamepadInfo empty{};
        return IsValid(index) ? info_[static_cast<std::size_t>(index)] : empty;
    }

    bool EvdevGamepad::SetRumble(const int index, const float lowFrequency, const float highFrequency,
                                 const std::uint32_t durationMilliseconds)
    {
        if (!IsValid(index))
        {
            return false;
        }
        EvdevControllerHub::Controller* controller = hub_.FindBySlot(index);
        return controller != nullptr &&
               controller->device->Rumble(lowFrequency, highFrequency, durationMilliseconds);
    }

    bool EvdevGamepad::SetTriggerRumble(int, float, float, std::uint32_t)
    {
        return false;
    }

    bool EvdevGamepad::SetLightBar(int, std::uint8_t, std::uint8_t, std::uint8_t)
    {
        return false;
    }

    bool EvdevGamepad::TryGetSensor(int, GamepadSensor, GamepadSensorReading& reading)
    {
        reading = GamepadSensorReading{};
        return false;
    }

    int EvdevGamepad::GetPlayerIndex(int) const
    {
        return -1;
    }

    bool EvdevGamepad::SetPlayerIndex(int, int)
    {
        return false;
    }

    GamepadPowerInfo EvdevGamepad::GetPowerInfo(const int index) const
    {
        GamepadPowerInfo power;
        if (!IsValid(index) || occupant_[static_cast<std::size_t>(index)] == 0)
        {
            power.state = GamepadPowerState::Error;
        }
        return power;
    }

    GamepadButtonLabel EvdevGamepad::GetButtonLabel(const int index, const GamepadButton button) const
    {
        if (!IsValid(index) || occupant_[static_cast<std::size_t>(index)] == 0)
        {
            return GamepadButtonLabel::Unknown;
        }
        const GamepadModel model = info_[static_cast<std::size_t>(index)].model;
        const bool playStation = model == GamepadModel::PlayStation3 ||
                                 model == GamepadModel::PlayStation4 ||
                                 model == GamepadModel::PlayStation5;
        const bool nintendo = model == GamepadModel::NintendoSwitchPro;
        switch (button)
        {
            case GamepadButton::A:
                return playStation ? GamepadButtonLabel::Cross
                                   : (nintendo ? GamepadButtonLabel::B : GamepadButtonLabel::A);
            case GamepadButton::B:
                return playStation ? GamepadButtonLabel::Circle
                                   : (nintendo ? GamepadButtonLabel::A : GamepadButtonLabel::B);
            case GamepadButton::X:
                return playStation ? GamepadButtonLabel::Square
                                   : (nintendo ? GamepadButtonLabel::Y : GamepadButtonLabel::X);
            case GamepadButton::Y:
                return playStation ? GamepadButtonLabel::Triangle
                                   : (nintendo ? GamepadButtonLabel::X : GamepadButtonLabel::Y);
            default:
                return GamepadButtonLabel::Unknown;
        }
    }

    int EvdevGamepad::GetTouchpadCount(int) const
    {
        return 0;
    }

    int EvdevGamepad::GetTouchpadFingerCount(int, int) const
    {
        return 0;
    }

    bool EvdevGamepad::TryGetTouchpadFinger(int, int, int, GamepadTouchpadFinger& finger) const
    {
        finger = GamepadTouchpadFinger{};
        return false;
    }

    // --- EvdevJoystick ------------------------------------------------------------------------

    EvdevJoystick::EvdevJoystick(EvdevControllerHub& hub) : hub_(hub) {}

    void EvdevJoystick::Update()
    {
        hub_.Pump();
        published_.clear();
        for (const auto& controller : hub_.GetControllers())
        {
            JoystickSnapshot snapshot;
            snapshot.axes = controller->axes;
            snapshot.buttons = controller->buttons;
            for (int hat = 0; hat < controller->hatCount; ++hat)
            {
                snapshot.hats.push_back(EvdevHatPosition(controller->hatValues[2 * hat],
                                                         controller->hatValues[2 * hat + 1]));
            }
            published_[controller->id] = std::move(snapshot);
        }
    }

    std::vector<JoystickInfo> EvdevJoystick::GetJoysticks() const
    {
        std::vector<JoystickInfo> joysticks;
        for (const auto& controller : hub_.GetControllers())
        {
            JoystickInfo info;
            info.id = controller->id;
            info.name = controller->device->GetDescription().name;
            info.kind = controller->kind == EvdevDeviceClass::Gamepad ? JoystickKind::Gamepad
                                                                      : JoystickKind::Unknown;
            joysticks.push_back(std::move(info));
        }
        std::sort(joysticks.begin(), joysticks.end(),
                  [](const JoystickInfo& left, const JoystickInfo& right) { return left.id < right.id; });
        return joysticks;
    }

    bool EvdevJoystick::IsConnected(const DeviceId id) const
    {
        return id != 0 && hub_.FindById(id) != nullptr;
    }

    JoystickCapabilities EvdevJoystick::GetCapabilities(const DeviceId id) const
    {
        JoystickCapabilities capabilities;
        const EvdevControllerHub::Controller* controller = id != 0 ? hub_.FindById(id) : nullptr;
        if (controller == nullptr)
        {
            return capabilities;
        }
        const EvdevDescription& description = controller->device->GetDescription();
        capabilities.connected = true;
        capabilities.axisCount = static_cast<int>(controller->axisCodes.size());
        capabilities.buttonCount = static_cast<int>(controller->buttonCodes.size());
        capabilities.hatCount = controller->hatCount;
        capabilities.ballCount = 0;
        capabilities.kind = controller->kind == EvdevDeviceClass::Gamepad ? JoystickKind::Gamepad
                                                                          : JoystickKind::Unknown;
        capabilities.name = description.name;
        capabilities.guid = FormatEvdevGuid(description);
        return capabilities;
    }

    JoystickSnapshot EvdevJoystick::GetSnapshot(const DeviceId id) const
    {
        const auto found = published_.find(id);
        return found != published_.end() ? found->second : JoystickSnapshot{};
    }

} // namespace CNA::Platform::Linux
