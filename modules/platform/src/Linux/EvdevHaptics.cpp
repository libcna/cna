// SPDX-License-Identifier: MS-PL

#include "EvdevHaptics.hpp"

#include "EvdevDevice.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <utility>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace CNA::Platform::Linux {

    namespace {

        // CNA::Input::HapticFeatureEXT's values; the platform layer does not see that header.
        constexpr std::uint32_t kConstant = 1u << 0;
        constexpr std::uint32_t kSine = 1u << 1;
        constexpr std::uint32_t kSquare = 1u << 2;
        constexpr std::uint32_t kTriangle = 1u << 3;
        constexpr std::uint32_t kSawtoothUp = 1u << 4;
        constexpr std::uint32_t kSawtoothDown = 1u << 5;
        constexpr std::uint32_t kRamp = 1u << 6;
        constexpr std::uint32_t kSpring = 1u << 7;
        constexpr std::uint32_t kDamper = 1u << 8;
        constexpr std::uint32_t kInertia = 1u << 9;
        constexpr std::uint32_t kFriction = 1u << 10;
        constexpr std::uint32_t kLeftRight = 1u << 11;
        constexpr std::uint32_t kGain = 1u << 16;
        constexpr std::uint32_t kAutocenter = 1u << 17;

        /// The kernel's durations and envelope levels: "values above 32767 have unspecified results".
        constexpr std::uint32_t kKernelMaximum = 0x7FFF;

        std::uint16_t Clamp(const std::uint32_t value)
        {
            return static_cast<std::uint16_t>(std::min(value, kKernelMaximum));
        }

        std::uint16_t Length(const std::uint32_t length)
        {
            if (length == UINT32_MAX)
            {
                return 0;  // The kernel's "until stopped".
            }
            return length == 0 ? std::uint16_t{1} : Clamp(length);
        }

        std::uint16_t Button(const std::uint16_t button)
        {
            return button == 0 ? std::uint16_t{0} : static_cast<std::uint16_t>(BTN_GAMEPAD + button - 1);
        }

        ff_envelope Envelope(const HapticEffect& effect)
        {
            ff_envelope envelope{};
            envelope.attack_length = Clamp(effect.attackLength);
            envelope.attack_level = Clamp(effect.attackLevel);
            envelope.fade_length = Clamp(effect.fadeLength);
            envelope.fade_level = Clamp(effect.fadeLevel);
            return envelope;
        }

        std::optional<std::uint16_t> Waveform(const HapticEffectType type)
        {
            switch (type)
            {
                case HapticEffectType::Sine: return FF_SINE;
                case HapticEffectType::Square: return FF_SQUARE;
                case HapticEffectType::Triangle: return FF_TRIANGLE;
                case HapticEffectType::SawtoothUp: return FF_SAW_UP;
                case HapticEffectType::SawtoothDown: return FF_SAW_DOWN;
                default: return std::nullopt;
            }
        }

        std::optional<std::uint16_t> ConditionType(const HapticEffectType type)
        {
            switch (type)
            {
                case HapticEffectType::Spring: return FF_SPRING;
                case HapticEffectType::Damper: return FF_DAMPER;
                case HapticEffectType::Inertia: return FF_INERTIA;
                case HapticEffectType::Friction: return FF_FRICTION;
                default: return std::nullopt;
            }
        }

        int Wrap(const std::int64_t hundredths)
        {
            return static_cast<int>(((hundredths % 36000) + 36000) % 36000);
        }

        std::uint16_t FromHundredths(const int hundredths)
        {
            return static_cast<std::uint16_t>((static_cast<std::uint32_t>(hundredths) * 0x8000u) / 18000u);
        }

        std::bitset<FF_CNT> ReadForceFeedback(const int descriptor)
        {
            std::array<unsigned long, (FF_CNT + sizeof(unsigned long) * 8 - 1) / (sizeof(unsigned long) * 8)> words{};
            std::bitset<FF_CNT> bits;
            if (ioctl(descriptor, EVIOCGBIT(EV_FF, sizeof(words)), words.data()) < 0)
            {
                return bits;
            }
            for (std::size_t bit = 0; bit < FF_CNT; ++bit)
            {
                const std::size_t word = bit / (sizeof(unsigned long) * 8);
                if ((words[word] >> (bit % (sizeof(unsigned long) * 8))) & 1ul)
                {
                    bits.set(bit);
                }
            }
            return bits;
        }

        float Unit(const float value)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
        }

    } // namespace

    bool IsEvdevHapticDevice(const std::bitset<FF_CNT>& forceFeedback)
    {
        const bool periodic = forceFeedback.test(FF_PERIODIC) &&
                              (forceFeedback.test(FF_SINE) || forceFeedback.test(FF_SQUARE) ||
                               forceFeedback.test(FF_TRIANGLE) || forceFeedback.test(FF_SAW_UP) ||
                               forceFeedback.test(FF_SAW_DOWN));
        return periodic || forceFeedback.test(FF_RUMBLE) || forceFeedback.test(FF_CONSTANT) ||
               forceFeedback.test(FF_RAMP) || forceFeedback.test(FF_SPRING) || forceFeedback.test(FF_FRICTION) ||
               forceFeedback.test(FF_DAMPER) || forceFeedback.test(FF_INERTIA);
    }

    std::uint32_t EvdevHapticFeatures(const std::bitset<FF_CNT>& forceFeedback)
    {
        std::uint32_t features = 0;
        const bool periodic = forceFeedback.test(FF_PERIODIC);
        const auto add = [&](const bool present, const std::uint32_t feature) {
            features |= present ? feature : 0u;
        };
        add(forceFeedback.test(FF_CONSTANT), kConstant);
        add(periodic && forceFeedback.test(FF_SINE), kSine);
        add(periodic && forceFeedback.test(FF_SQUARE), kSquare);
        add(periodic && forceFeedback.test(FF_TRIANGLE), kTriangle);
        add(periodic && forceFeedback.test(FF_SAW_UP), kSawtoothUp);
        add(periodic && forceFeedback.test(FF_SAW_DOWN), kSawtoothDown);
        add(forceFeedback.test(FF_RAMP), kRamp);
        add(forceFeedback.test(FF_SPRING), kSpring);
        add(forceFeedback.test(FF_DAMPER), kDamper);
        add(forceFeedback.test(FF_INERTIA), kInertia);
        add(forceFeedback.test(FF_FRICTION), kFriction);
        add(forceFeedback.test(FF_RUMBLE), kLeftRight);
        add(forceFeedback.test(FF_GAIN), kGain);
        add(forceFeedback.test(FF_AUTOCENTER), kAutocenter);
        return features;
    }

    bool EvdevSupportsEffect(const std::bitset<FF_CNT>& forceFeedback, const HapticEffect& effect)
    {
        if (const std::optional<std::uint16_t> waveform = Waveform(effect.type))
        {
            return forceFeedback.test(FF_PERIODIC) && forceFeedback.test(*waveform);
        }
        if (const std::optional<std::uint16_t> condition = ConditionType(effect.type))
        {
            return forceFeedback.test(*condition);
        }
        switch (effect.type)
        {
            case HapticEffectType::Constant: return forceFeedback.test(FF_CONSTANT);
            case HapticEffectType::Ramp: return forceFeedback.test(FF_RAMP);
            case HapticEffectType::LeftRight: return forceFeedback.test(FF_RUMBLE);
            default: return false;
        }
    }

    std::uint16_t ToEvdevDirection(const HapticDirection& direction)
    {
        switch (direction.type)
        {
            case HapticDirectionType::Polar:
                return FromHundredths(Wrap(direction.values[0]));
            case HapticDirectionType::Spherical:
                // Measured from the east towards the south: a quarter turn from the kernel's north.
                return FromHundredths(Wrap(static_cast<std::int64_t>(direction.values[0]) + 9000));
            case HapticDirectionType::Cartesian:
            {
                const std::int32_t x = direction.values[0];
                const std::int32_t y = direction.values[1];
                if (y == 0)
                {
                    return x >= 0 ? std::uint16_t{0x4000} : std::uint16_t{0xC000};
                }
                if (x == 0)
                {
                    return y >= 0 ? std::uint16_t{0x8000} : std::uint16_t{0};
                }
                const double angle = std::atan2(static_cast<double>(y), static_cast<double>(x));
                return FromHundredths(Wrap(static_cast<std::int64_t>(angle * 18000.0 / std::numbers::pi) + 45000));
            }
            case HapticDirectionType::SteeringAxis:
                return 0x4000;
        }
        return 0x4000;
    }

    bool ToEvdevEffect(const HapticEffect& effect, ff_effect& out)
    {
        out = ff_effect{};
        out.id = -1;
        out.direction = ToEvdevDirection(effect.direction);
        out.replay.length = Length(effect.length);
        out.replay.delay = Clamp(effect.delay);
        out.trigger.button = Button(effect.button);
        out.trigger.interval = Clamp(effect.interval);

        if (const std::optional<std::uint16_t> waveform = Waveform(effect.type))
        {
            out.type = FF_PERIODIC;
            out.u.periodic.waveform = *waveform;
            out.u.periodic.period = Clamp(effect.period);
            out.u.periodic.magnitude = effect.magnitude;
            out.u.periodic.offset = effect.offset;
            // The kernel's phase is a fraction of the period, [0, 0x10000) for [0, 360) degrees.
            out.u.periodic.phase =
                static_cast<std::uint16_t>((static_cast<std::uint32_t>(effect.phase % 36000) * 0x10000u) / 36000u);
            out.u.periodic.envelope = Envelope(effect);
            return true;
        }
        if (const std::optional<std::uint16_t> condition = ConditionType(effect.type))
        {
            out.type = *condition;
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                out.u.condition[axis].right_saturation = effect.rightSaturation[axis];
                out.u.condition[axis].left_saturation = effect.leftSaturation[axis];
                out.u.condition[axis].right_coeff = effect.rightCoefficient[axis];
                out.u.condition[axis].left_coeff = effect.leftCoefficient[axis];
                out.u.condition[axis].deadband = effect.deadband[axis];
                out.u.condition[axis].center = effect.center[axis];
            }
            return true;
        }
        switch (effect.type)
        {
            case HapticEffectType::Constant:
                out.type = FF_CONSTANT;
                out.u.constant.level = effect.level;
                out.u.constant.envelope = Envelope(effect);
                return true;
            case HapticEffectType::Ramp:
                out.type = FF_RAMP;
                out.u.ramp.start_level = effect.rampStart;
                out.u.ramp.end_level = effect.rampEnd;
                out.u.ramp.envelope = Envelope(effect);
                return true;
            case HapticEffectType::LeftRight:
                // The motors have no direction, no delay and no trigger in the kernel's rumble.
                out.type = FF_RUMBLE;
                out.direction = 0x4000;
                out.replay.delay = 0;
                out.trigger = ff_trigger{};
                out.u.rumble.strong_magnitude = effect.largeMagnitude;
                out.u.rumble.weak_magnitude = effect.smallMagnitude;
                return true;
            default:
                return false;
        }
    }

    std::optional<HapticEffect> EvdevRumbleEffect(const std::bitset<FF_CNT>& forceFeedback, const float strength,
                                                  const std::uint32_t durationMilliseconds)
    {
        const float unit = Unit(strength);
        HapticEffect effect;
        effect.length = durationMilliseconds;
        if (forceFeedback.test(FF_PERIODIC) && forceFeedback.test(FF_SINE))
        {
            effect.type = HapticEffectType::Sine;
            effect.direction.type = HapticDirectionType::Cartesian;
            effect.period = 1000;
            effect.magnitude = static_cast<std::int16_t>(32767.0f * unit);
            return effect;
        }
        if (forceFeedback.test(FF_RUMBLE))
        {
            effect.type = HapticEffectType::LeftRight;
            effect.largeMagnitude = static_cast<std::uint16_t>(65535.0f * unit);
            effect.smallMagnitude = effect.largeMagnitude;
            return effect;
        }
        return std::nullopt;
    }

    // --- one device ------------------------------------------------------------------------------

    EvdevHapticDevice::EvdevHapticDevice(const int descriptor, std::string path, std::string name,
                                         std::bitset<FF_CNT> forceFeedback, const int maxEffects)
        : descriptor_(descriptor), path_(std::move(path)), name_(std::move(name)),
          forceFeedback_(forceFeedback), maxEffects_(maxEffects)
    {
    }

    std::unique_ptr<EvdevHapticDevice> EvdevHapticDevice::Open(const std::string& path)
    {
        const int descriptor = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (descriptor < 0)
        {
            return nullptr;
        }
        const std::bitset<FF_CNT> forceFeedback = ReadForceFeedback(descriptor);
        if (!IsEvdevHapticDevice(forceFeedback))
        {
            ::close(descriptor);
            return nullptr;
        }
        std::array<char, 256> name{};
        if (ioctl(descriptor, EVIOCGNAME(name.size() - 1), name.data()) < 0)
        {
            name[0] = '\0';
        }
        int effects = -1;
        if (ioctl(descriptor, EVIOCGEFFECTS, &effects) < 0)
        {
            effects = -1;
        }
        return std::unique_ptr<EvdevHapticDevice>(
            new EvdevHapticDevice(descriptor, path, name.data(), forceFeedback, effects));
    }

    EvdevHapticDevice::~EvdevHapticDevice()
    {
        if (descriptor_ >= 0)
        {
            ::close(descriptor_);
        }
    }

    HapticDeviceCapabilities EvdevHapticDevice::GetCapabilities() const
    {
        HapticDeviceCapabilities capabilities;
        capabilities.name = name_;
        capabilities.features = EvdevHapticFeatures(forceFeedback_);
        capabilities.axisCount = 0;
        capabilities.maxEffects = maxEffects_;
        capabilities.maxEffectsPlaying = -1;
        capabilities.rumbleSupported = EvdevRumbleEffect(forceFeedback_, 0.0f, 0).has_value();
        return capabilities;
    }

    bool EvdevHapticDevice::IsEffectSupported(const HapticEffect& effect) const
    {
        return EvdevSupportsEffect(forceFeedback_, effect);
    }

    bool EvdevHapticDevice::Write(const std::uint16_t code, const std::int32_t value)
    {
        input_event event{};
        event.type = EV_FF;
        event.code = code;
        event.value = value;
        if (::write(descriptor_, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event)))
        {
            return true;
        }
        gone_ = gone_ || errno == ENODEV;
        return false;
    }

    int EvdevHapticDevice::Upload(ff_effect& effect)
    {
        if (ioctl(descriptor_, EVIOCSFF, &effect) < 0)
        {
            gone_ = gone_ || errno == ENODEV;
            return -1;
        }
        return effect.id;
    }

    int EvdevHapticDevice::CreateEffect(const HapticEffect& effect)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ff_effect native{};
        if (!EvdevSupportsEffect(forceFeedback_, effect) || !ToEvdevEffect(effect, native))
        {
            return -1;
        }
        const int id = Upload(native);
        if (id >= 0)
        {
            effects_.insert(id);
        }
        return id;
    }

    bool EvdevHapticDevice::UpdateEffect(const int effectId, const HapticEffect& effect)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ff_effect native{};
        if (effects_.count(effectId) == 0 || !EvdevSupportsEffect(forceFeedback_, effect) ||
            !ToEvdevEffect(effect, native))
        {
            return false;
        }
        // The kernel keeps the id, and refuses a change of family or waveform.
        native.id = static_cast<std::int16_t>(effectId);
        return Upload(native) >= 0;
    }

    bool EvdevHapticDevice::RunEffect(const int effectId, const std::uint32_t iterations)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (effects_.count(effectId) == 0)
        {
            return false;
        }
        return Write(static_cast<std::uint16_t>(effectId),
                     static_cast<std::int32_t>(std::min<std::uint32_t>(iterations, INT_MAX)));
    }

    bool EvdevHapticDevice::StopEffect(const int effectId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return effects_.count(effectId) != 0 && Write(static_cast<std::uint16_t>(effectId), 0);
    }

    void EvdevHapticDevice::DestroyEffect(const int effectId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (effects_.erase(effectId) == 0)
        {
            return;
        }
        (void) ioctl(descriptor_, EVIOCRMFF, effectId);
        if (rumble_ == effectId) { rumble_.reset(); }
        if (leftRight_ == effectId) { leftRight_.reset(); }
    }

    bool EvdevHapticDevice::GetEffectStatus(int) const
    {
        return false;
    }

    bool EvdevHapticDevice::StopAllEffects()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool stopped = true;
        for (const int id : effects_)
        {
            stopped = Write(static_cast<std::uint16_t>(id), 0) && stopped;
        }
        return stopped;
    }

    bool EvdevHapticDevice::SetGain(const int gain)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return forceFeedback_.test(FF_GAIN) && Write(FF_GAIN, 0xFFFF * std::clamp(gain, 0, 100) / 100);
    }

    bool EvdevHapticDevice::SetAutocenter(const int autocenter)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return forceFeedback_.test(FF_AUTOCENTER) &&
               Write(FF_AUTOCENTER, 0xFFFF * std::clamp(autocenter, 0, 100) / 100);
    }

    bool EvdevHapticDevice::Pause()
    {
        return false;
    }

    bool EvdevHapticDevice::Resume()
    {
        return false;
    }

    bool EvdevHapticDevice::InitializeRumble()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (rumble_)
        {
            return true;
        }
        // Uploaded, not played: half strength for five seconds, as SDL3 readies it.
        const std::optional<HapticEffect> effect = EvdevRumbleEffect(forceFeedback_, 0.5f, 5000);
        ff_effect native{};
        if (!effect || !ToEvdevEffect(*effect, native))
        {
            return false;
        }
        const int id = Upload(native);
        if (id < 0)
        {
            return false;
        }
        effects_.insert(id);
        rumble_ = id;
        return true;
    }

    bool EvdevHapticDevice::Play(std::optional<int>& slot, const HapticEffect& effect)
    {
        ff_effect native{};
        if (!ToEvdevEffect(effect, native))
        {
            return false;
        }
        if (slot)
        {
            native.id = static_cast<std::int16_t>(*slot);
        }
        const int id = Upload(native);
        if (id < 0)
        {
            return false;
        }
        effects_.insert(id);
        slot = id;
        return Write(static_cast<std::uint16_t>(id), 1);
    }

    bool EvdevHapticDevice::PlayRumble(const float strength, const std::uint32_t durationMilliseconds)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!rumble_)
        {
            return false;
        }
        const std::optional<HapticEffect> effect = EvdevRumbleEffect(forceFeedback_, strength, durationMilliseconds);
        if (!effect)
        {
            return false;
        }
        if (leftRight_ && *leftRight_ != *rumble_)
        {
            (void) Write(static_cast<std::uint16_t>(*leftRight_), 0);
        }
        return Play(rumble_, *effect);
    }

    bool EvdevHapticDevice::StopRumble()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool stopped = rumble_.has_value() || leftRight_.has_value();
        if (rumble_)
        {
            stopped = Write(static_cast<std::uint16_t>(*rumble_), 0) && stopped;
        }
        if (leftRight_)
        {
            stopped = Write(static_cast<std::uint16_t>(*leftRight_), 0) && stopped;
        }
        return stopped;
    }

    bool EvdevHapticDevice::PlayLeftRight(const float large, const float small,
                                          const std::uint32_t durationMilliseconds)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!forceFeedback_.test(FF_RUMBLE))
        {
            return false;
        }
        HapticEffect effect;
        effect.type = HapticEffectType::LeftRight;
        effect.length = durationMilliseconds;
        effect.largeMagnitude = static_cast<std::uint16_t>(65535.0f * Unit(large));
        effect.smallMagnitude = static_cast<std::uint16_t>(65535.0f * Unit(small));
        if (rumble_)
        {
            (void) Write(static_cast<std::uint16_t>(*rumble_), 0);
        }
        return Play(leftRight_, effect);
    }

    // --- the service -----------------------------------------------------------------------------

    EvdevHaptics::EvdevHaptics(JoystickNodes joystickNodes, std::string directory, std::string sysfsRoot)
        : joystickNodes_(std::move(joystickNodes)), directory_(std::move(directory)), sysfsRoot_(std::move(sysfsRoot))
    {
    }

    std::vector<EvdevHaptics::Node> EvdevHaptics::Scan() const
    {
        std::vector<Node> nodes;
        std::map<std::string, DeviceId> seen;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(directory_, error))
        {
            const std::string name = entry.path().filename().string();
            if (name.rfind("event", 0) != 0)
            {
                continue;
            }
            Node node;
            node.path = entry.path().string();
            if (!ReadEvdevSysfsDescription(name, node.description, sysfsRoot_) ||
                !IsEvdevHapticDevice(node.description.forceFeedback) || ::access(node.path.c_str(), W_OK) != 0)
            {
                continue;
            }
            // The kernel's input device (`inputN`) outlives no replug, which is what an id promises.
            std::error_code linkError;
            const std::filesystem::path device =
                std::filesystem::read_symlink(std::filesystem::path(sysfsRoot_) / name / "device", linkError);
            const std::string identity = linkError ? name : device.filename().string();
            auto known = ids_.find(identity);
            if (known == ids_.end())
            {
                known = ids_.emplace(identity, nextId_++).first;
            }
            node.id = known->second;
            seen.emplace(identity, node.id);
            nodes.push_back(std::move(node));
        }
        ids_ = std::move(seen);
        std::sort(nodes.begin(), nodes.end(), [](const Node& left, const Node& right) { return left.id < right.id; });
        return nodes;
    }

    std::optional<EvdevHaptics::Node> EvdevHaptics::Find(const DeviceId id) const
    {
        for (Node& node : Scan())
        {
            if (node.id == id)
            {
                return std::move(node);
            }
        }
        return std::nullopt;
    }

    HapticInfo EvdevHaptics::InfoOf(const Node& node)
    {
        return HapticInfo{node.id, node.description.name,
                          EvdevRumbleEffect(node.description.forceFeedback, 0.0f, 0).has_value()};
    }

    EvdevHapticDevice* EvdevHaptics::Acquire(const DeviceId id)
    {
        const auto open = open_.find(id);
        if (open != open_.end())
        {
            if (!open->second->IsGone())
            {
                return open->second.get();
            }
            open_.erase(open);
        }
        const std::optional<Node> node = Find(id);
        if (!node)
        {
            return nullptr;
        }
        std::unique_ptr<EvdevHapticDevice> device = EvdevHapticDevice::Open(node->path);
        if (device == nullptr)
        {
            return nullptr;
        }
        return open_.emplace(id, std::move(device)).first->second.get();
    }

    std::vector<HapticInfo> EvdevHaptics::GetHaptics() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<HapticInfo> haptics;
        for (const Node& node : Scan())
        {
            haptics.push_back(InfoOf(node));
        }
        return haptics;
    }

    std::optional<HapticInfo> EvdevHaptics::GetDefaultVibrationDevice() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Node& node : Scan())
        {
            const HapticInfo info = InfoOf(node);
            if (info.rumbleSupported && ClassifyEvdevDevice(node.description) != EvdevDeviceClass::Gamepad)
            {
                return info;
            }
        }
        return std::nullopt;
    }

    bool EvdevHaptics::IsConnected(const DeviceId id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return Find(id).has_value();
    }

    bool EvdevHaptics::SupportsRumble(const DeviceId id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::optional<Node> node = Find(id);
        return node && InfoOf(*node).rumbleSupported;
    }

    bool EvdevHaptics::InitializeRumble(const DeviceId id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        EvdevHapticDevice* device = Acquire(id);
        return device != nullptr && device->InitializeRumble();
    }

    bool EvdevHaptics::PlayRumble(const DeviceId id, const float strength, const std::uint32_t durationMilliseconds)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        EvdevHapticDevice* device = Acquire(id);
        // A device unplugged mid-effect answers false, as the contract asks: that is ordinary.
        return device != nullptr && device->InitializeRumble() && device->PlayRumble(strength, durationMilliseconds);
    }

    bool EvdevHaptics::PlayLeftRight(const DeviceId id, const float largeMotor, const float smallMotor,
                                     const std::uint32_t durationMilliseconds)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        EvdevHapticDevice* device = Acquire(id);
        return device != nullptr && device->PlayLeftRight(largeMotor, smallMotor, durationMilliseconds);
    }

    bool EvdevHaptics::StopRumble(const DeviceId id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto open = open_.find(id);
        return open != open_.end() && open->second->StopRumble();
    }

    bool EvdevHaptics::StopAll(const DeviceId id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto open = open_.find(id);
        return open != open_.end() && open->second->StopAllEffects();
    }

    std::unique_ptr<IPlatformHapticDevice> EvdevHaptics::Open(const DeviceId id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::optional<Node> node = Find(id);
        return node ? EvdevHapticDevice::Open(node->path) : nullptr;
    }

    std::unique_ptr<IPlatformHapticDevice> EvdevHaptics::OpenFromJoystick(const DeviceId id)
    {
        const std::string path = joystickNodes_ ? joystickNodes_(id) : std::string();
        return path.empty() ? nullptr : EvdevHapticDevice::Open(path);
    }

    std::unique_ptr<IPlatformHapticDevice> EvdevHaptics::OpenFromMouse()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Node& node : Scan())
        {
            if (node.description.keys.test(BTN_MOUSE))
            {
                return EvdevHapticDevice::Open(node.path);
            }
        }
        return nullptr;
    }

    bool EvdevHaptics::IsJoystickHaptic(const DeviceId id) const
    {
        const std::string path = joystickNodes_ ? joystickNodes_(id) : std::string();
        if (path.empty())
        {
            return false;
        }
        EvdevDescription description;
        return ReadEvdevSysfsDescription(std::filesystem::path(path).filename().string(), description, sysfsRoot_) &&
               IsEvdevHapticDevice(description.forceFeedback) && ::access(path.c_str(), W_OK) == 0;
    }

    bool EvdevHaptics::IsMouseHaptic() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::vector<Node> nodes = Scan();
        return std::any_of(nodes.begin(), nodes.end(),
                           [](const Node& node) { return node.description.keys.test(BTN_MOUSE); });
    }

    void EvdevHaptics::CloseAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_.clear();
    }

} // namespace CNA::Platform::Linux
