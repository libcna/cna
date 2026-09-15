// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformHaptics.hpp"
#include "EvdevLayout.hpp"

#include <bitset>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <linux/input.h>

namespace CNA::Platform::Linux {

    /**
     * @brief Where a haptic device's id lands in CNA's device ids: above the Linux controllers'
     * and the X input devices' (`0x10000 +`), so none of them meet.
     */
    inline constexpr DeviceId kEvdevHapticIdBase = 0x20000;

    /**
     * @brief Gets whether a device is a haptic device: one that can play at least one effect.
     *
     * A device that only has a gain or an autocenter control plays nothing, and is not one.
     *
     * @param forceFeedback The device's force-feedback bitmap (`EV_FF`).
     * @return True when it can play an effect.
     */
    [[nodiscard]] bool IsEvdevHapticDevice(const std::bitset<FF_CNT>& forceFeedback);

    /**
     * @brief The contract's feature bits for a device's force-feedback bitmap.
     *
     * The values are `CNA::Input::HapticFeatureEXT`'s. Custom waveforms, effect status and pause
     * are never reported: this backend cannot upload the first, and the kernel offers the other
     * two to no client.
     *
     * @param forceFeedback The device's force-feedback bitmap.
     * @return The feature bits.
     */
    [[nodiscard]] std::uint32_t EvdevHapticFeatures(const std::bitset<FF_CNT>& forceFeedback);

    /**
     * @brief Gets whether a device can play an effect: its family's bit and, for a periodic
     * effect, its waveform's.
     *
     * @param forceFeedback The device's force-feedback bitmap.
     * @param effect The effect.
     * @return True when the device can play it; never for a Custom waveform.
     */
    [[nodiscard]] bool EvdevSupportsEffect(const std::bitset<FF_CNT>& forceFeedback, const HapticEffect& effect);

    /**
     * @brief The kernel's direction -- an angle in `[0, 0x10000)`, 0 from the north, clockwise --
     * for a contract direction.
     *
     * Polar and spherical hundredths of a degree are converted as SDL3's Linux backend converts
     * them (a spherical angle is measured from the east, so a quarter turn is added); a cartesian
     * vector by its angle; a steering axis is east.
     *
     * @param direction The direction.
     * @return The angle.
     */
    [[nodiscard]] std::uint16_t ToEvdevDirection(const HapticDirection& direction);

    /**
     * @brief Translates an effect into the kernel's form.
     *
     * The kernel's durations and envelope levels stop at 32767, and larger values are clamped to
     * it; an unlimited length is its 0, and a length of 0 is its shortest, 1 ms (the kernel's 0 is
     * "until stopped"). A condition's third axis has no place in the kernel's two. A LeftRight
     * effect's magnitudes are the kernel's rumble magnitudes as they are, the full 16-bit range.
     *
     * @param effect The effect.
     * @param out Receives the kernel's effect, its id -1 (a new one).
     * @return False for a Custom waveform, which this backend does not upload.
     */
    [[nodiscard]] bool ToEvdevEffect(const HapticEffect& effect, ff_effect& out);

    /**
     * @brief The effect simple rumble plays on a device, as SDL3 chooses it: a sine where the
     * device has one, else both rumble motors together.
     *
     * @param forceFeedback The device's force-feedback bitmap.
     * @param strength The strength in `[0, 1]` (clamped).
     * @param durationMilliseconds How long.
     * @return The effect, or nothing when the device can do neither.
     */
    [[nodiscard]] std::optional<HapticEffect> EvdevRumbleEffect(const std::bitset<FF_CNT>& forceFeedback,
                                                               float strength, std::uint32_t durationMilliseconds);

    /**
     * @brief One force-feedback device, opened for writing (plans/plan_x11.md X11-0168).
     *
     * Effects are uploaded to the kernel through this descriptor and belong to it: closing the
     * device erases every one, so a game that quits mid-effect leaves nothing playing.
     */
    class EvdevHapticDevice final : public IPlatformHapticDevice
    {
    public:
        /**
         * @brief Opens a node for force feedback.
         * @param path The event node.
         * @return The device, or null when the node cannot be opened for writing or plays nothing.
         */
        [[nodiscard]] static std::unique_ptr<EvdevHapticDevice> Open(const std::string& path);

        /** @brief Closes the node, which erases every effect uploaded through it. */
        ~EvdevHapticDevice() override;

        EvdevHapticDevice(const EvdevHapticDevice&) = delete;
        EvdevHapticDevice& operator=(const EvdevHapticDevice&) = delete;

        /** @brief Gets the node's path. @return The path. */
        [[nodiscard]] const std::string& GetPath() const { return path_; }

        /** @brief Gets the device's force-feedback bitmap. @return The bitmap. */
        [[nodiscard]] const std::bitset<FF_CNT>& GetForceFeedback() const { return forceFeedback_; }

        /**
         * @brief Gets what the device can do.
         * @return Its name, feature bits, the effects it holds (`EVIOCGEFFECTS`) and whether simple
         * rumble works; the axis count and the effects that play at once are not something the
         * kernel reports, and are 0 and -1.
         */
        [[nodiscard]] HapticDeviceCapabilities GetCapabilities() const override;
        /** @brief Gets whether an effect can be played. @param effect The effect. @return The answer. */
        [[nodiscard]] bool IsEffectSupported(const HapticEffect& effect) const override;
        /** @brief Uploads the simple-rumble effect. @return True when it is ready. */
        bool InitializeRumble() override;
        /**
         * @brief Plays simple rumble.
         * @param strength In `[0, 1]`.
         * @param durationMilliseconds How long.
         * @return True when the device took it.
         */
        bool PlayRumble(float strength, std::uint32_t durationMilliseconds) override;
        /** @brief Stops simple rumble. @return True when the device took it. */
        bool StopRumble() override;
        /** @brief Uploads an effect. @param effect The effect. @return Its id, or -1. */
        [[nodiscard]] int CreateEffect(const HapticEffect& effect) override;
        /**
         * @brief Replaces an uploaded effect's parameters.
         * @param effectId The effect.
         * @param effect The new parameters.
         * @return True when the device took them.
         */
        bool UpdateEffect(int effectId, const HapticEffect& effect) override;
        /**
         * @brief Plays an uploaded effect.
         * @param effectId The effect.
         * @param iterations How many times; UINT32_MAX is as many as the kernel counts.
         * @return True when the device took it.
         */
        bool RunEffect(int effectId, std::uint32_t iterations) override;
        /** @brief Stops an uploaded effect. @param effectId The effect. @return True when taken. */
        bool StopEffect(int effectId) override;
        /** @brief Erases an uploaded effect. @param effectId The effect. */
        void DestroyEffect(int effectId) override;
        /** @brief The kernel tells no client whether an effect is playing. @return False. */
        [[nodiscard]] bool GetEffectStatus(int effectId) const override;
        /** @brief Stops every effect uploaded through this device. @return True when all were. */
        bool StopAllEffects() override;
        /** @brief Sets the gain (`FF_GAIN`). @param gain 0 to 100. @return False without the control. */
        bool SetGain(int gain) override;
        /**
         * @brief Sets the autocenter strength (`FF_AUTOCENTER`).
         * @param autocenter 0 to 100.
         * @return False without the control.
         */
        bool SetAutocenter(int autocenter) override;
        /** @brief The kernel cannot pause effects. @return False. */
        bool Pause() override;
        /** @brief The kernel cannot pause effects. @return False. */
        bool Resume() override;

        /**
         * @brief Plays both rumble motors, the one effect reused.
         * @param large The strong motor, `[0, 1]`.
         * @param small The weak motor, `[0, 1]`.
         * @param durationMilliseconds How long.
         * @return False without `FF_RUMBLE` or when the device refused.
         */
        bool PlayLeftRight(float large, float small, std::uint32_t durationMilliseconds);

        /** @brief Gets whether a write failed because the device is gone. @return The answer. */
        [[nodiscard]] bool IsGone() const { return gone_; }

    private:
        EvdevHapticDevice(int descriptor, std::string path, std::string name, std::bitset<FF_CNT> forceFeedback,
                          int maxEffects);

        bool Write(std::uint16_t code, std::int32_t value);
        int Upload(ff_effect& effect);
        bool Play(std::optional<int>& slot, const HapticEffect& effect);

        mutable std::mutex mutex_;
        int descriptor_ = -1;
        std::string path_;
        std::string name_;
        std::bitset<FF_CNT> forceFeedback_;
        int maxEffects_ = -1;
        std::set<int> effects_;
        std::optional<int> rumble_;
        std::optional<int> leftRight_;
        bool gone_ = false;
    };

    /**
     * @brief `IPlatformHaptics` over the kernel's force-feedback interface (plans/plan_x11.md
     * X11-0168).
     *
     * Every event node that can play an effect is a haptic device -- a wheel, a stick, a pad, a
     * phone's vibrator -- listed from sysfs without opening anything, as long as this user may
     * write to it. Its id is kept for as long as the kernel's input device lasts (a replug is a
     * new device). The service opens a device when it is first asked to play on it, and a release
     * of the Haptic subsystem closes it, which stops whatever it played. `Open` hands the caller
     * an independent device of its own.
     */
    class EvdevHaptics final : public IPlatformHaptics
    {
    public:
        /** @brief Finds a joystick's event node from its id; empty when there is none. */
        using JoystickNodes = std::function<std::string(DeviceId)>;

        /**
         * @brief Serves the haptic devices in a directory.
         * @param joystickNodes The Linux controllers' nodes, for `OpenFromJoystick`.
         * @param directory The event nodes; overridable for tests.
         * @param sysfsRoot Where sysfs describes them; overridable for tests.
         */
        explicit EvdevHaptics(JoystickNodes joystickNodes, std::string directory = "/dev/input",
                              std::string sysfsRoot = "/sys/class/input");

        /** @brief Gets the devices, in id order. @return The devices. */
        [[nodiscard]] std::vector<HapticInfo> GetHaptics() const override;
        /**
         * @brief Gets the vibration device a game without a gamepad would use: the first haptic
         * device that is not a gamepad and can rumble.
         * @return The device, or nothing.
         */
        [[nodiscard]] std::optional<HapticInfo> GetDefaultVibrationDevice() const override;
        /** @brief Gets whether an id is connected. @param id The id. @return The answer. */
        [[nodiscard]] bool IsConnected(DeviceId id) const override;
        /** @brief Gets whether a device can rumble. @param id The id. @return The answer. */
        [[nodiscard]] bool SupportsRumble(DeviceId id) const override;
        /** @brief Readies simple rumble. @param id The id. @return True when ready. */
        bool InitializeRumble(DeviceId id) override;
        /**
         * @brief Plays simple rumble.
         * @param id The id.
         * @param strength In `[0, 1]`.
         * @param durationMilliseconds How long.
         * @return False when the device is gone or cannot rumble.
         */
        bool PlayRumble(DeviceId id, float strength, std::uint32_t durationMilliseconds) override;
        /**
         * @brief Plays both rumble motors.
         * @param id The id.
         * @param largeMotor The strong motor, `[0, 1]`.
         * @param smallMotor The weak motor, `[0, 1]`.
         * @param durationMilliseconds How long.
         * @return False when the device is gone or has no rumble motors.
         */
        bool PlayLeftRight(DeviceId id, float largeMotor, float smallMotor,
                           std::uint32_t durationMilliseconds) override;
        /** @brief Stops simple rumble. @param id The id. @return True when taken. */
        bool StopRumble(DeviceId id) override;
        /** @brief Stops every effect this service plays on a device. @param id The id. @return True when taken. */
        bool StopAll(DeviceId id) override;
        /** @brief Opens a device of the caller's own. @param id The id. @return The device, or null. */
        [[nodiscard]] std::unique_ptr<IPlatformHapticDevice> Open(DeviceId id) override;
        /**
         * @brief Opens a joystick's own force feedback.
         * @param id The joystick's id.
         * @return The device, or null when the joystick has none.
         */
        [[nodiscard]] std::unique_ptr<IPlatformHapticDevice> OpenFromJoystick(DeviceId id) override;
        /** @brief Opens the first mouse with force feedback. @return The device, or null. */
        [[nodiscard]] std::unique_ptr<IPlatformHapticDevice> OpenFromMouse() override;
        /** @brief Gets whether a joystick has force feedback. @param id The joystick. @return The answer. */
        [[nodiscard]] bool IsJoystickHaptic(DeviceId id) const override;
        /** @brief Gets whether a mouse has force feedback. @return The answer. */
        [[nodiscard]] bool IsMouseHaptic() const override;

        /** @brief Closes every device the service opened: the Haptic subsystem's release. */
        void CloseAll();

    private:
        struct Node
        {
            DeviceId id = 0;
            std::string path;
            EvdevDescription description;
        };

        [[nodiscard]] std::vector<Node> Scan() const;
        [[nodiscard]] std::optional<Node> Find(DeviceId id) const;
        [[nodiscard]] EvdevHapticDevice* Acquire(DeviceId id);
        [[nodiscard]] static HapticInfo InfoOf(const Node& node);

        JoystickNodes joystickNodes_;
        std::string directory_;
        std::string sysfsRoot_;
        mutable std::mutex mutex_;
        // Ids by the kernel's input device (`inputN`), which a replug does not reuse.
        mutable std::map<std::string, DeviceId> ids_;
        mutable DeviceId nextId_ = kEvdevHapticIdBase + 1;
        std::map<DeviceId, std::unique_ptr<EvdevHapticDevice>> open_;
    };

} // namespace CNA::Platform::Linux
