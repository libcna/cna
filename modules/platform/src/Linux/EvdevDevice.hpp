// SPDX-License-Identifier: MS-PL
#pragma once

#include "EvdevLayout.hpp"

#include <linux/input.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform::Linux {

    /**
     * @brief One opened `/dev/input/event*` node.
     *
     * Opened read-write where the system allows it (force feedback is written to the node) and
     * read-only otherwise. Never grabbed: other applications keep seeing the device.
     */
    class EvdevDevice
    {
    public:
        /**
         * @brief Opens and describes a device.
         *
         * @param path The node, e.g. `/dev/input/event7`.
         * @return The device, or null when it cannot be opened or does not answer the evdev
         * ioctls. On a desktop most nodes cannot be opened by an ordinary user -- udev grants the
         * logged-in user its controllers, not its keyboards and mice -- and that is the expected
         * answer for them, not an error.
         */
        [[nodiscard]] static std::unique_ptr<EvdevDevice> Open(const std::string& path);

        /** @brief Stops any rumble effect this device uploaded and closes the node. */
        ~EvdevDevice();

        EvdevDevice(const EvdevDevice&) = delete;
        EvdevDevice& operator=(const EvdevDevice&) = delete;

        /** @brief Gets the node's path. @return The path. */
        [[nodiscard]] const std::string& GetPath() const { return path_; }

        /** @brief Gets what the kernel reported about the device. @return The description. */
        [[nodiscard]] const EvdevDescription& GetDescription() const { return description_; }

        /**
         * @brief Reads every event the kernel has queued.
         *
         * After a queue overflow (`SYN_DROPPED`) the events up to the next `SYN_REPORT` are
         * worthless; they are discarded and the device's whole current state is appended in
         * their place, so a consumer applying the events in order still ends up right.
         *
         * @param events Receives the events.
         * @return False when the device is gone (unplugged) or unreadable.
         */
        bool Drain(std::vector<input_event>& events);

        /**
         * @brief Appends the device's whole current state as events.
         *
         * @param events Receives one `EV_KEY` event per key the device has and one `EV_ABS`
         * event per absolute axis.
         */
        void AppendCurrentState(std::vector<input_event>& events) const;

        /** @brief Gets whether the device can rumble. @return True with FF_RUMBLE and write access. */
        [[nodiscard]] bool CanRumble() const;

        /**
         * @brief Starts, changes or stops rumble.
         *
         * @param low Strong (low-frequency) motor strength in [0, 1].
         * @param high Weak (high-frequency) motor strength in [0, 1].
         * @param durationMilliseconds How long; 0 until changed. Capped at 65535, the kernel's limit.
         * @return True when the kernel accepted the effect.
         */
        bool Rumble(float low, float high, std::uint32_t durationMilliseconds);

    private:
        EvdevDevice(int descriptor, std::string path, EvdevDescription description, bool writable);

        int descriptor_ = -1;
        std::string path_;
        EvdevDescription description_;
        bool writable_ = false;
        int effect_ = -1;
        // Between a SYN_DROPPED and the SYN_REPORT that ends the damaged packet. Kept across
        // Drain() calls: the two can arrive in different reads.
        bool dropping_ = false;
    };

    /**
     * @brief Reads the name of the kernel driver bound to an input node, from sysfs.
     *
     * @param nodeName The node's file name, e.g. `event7`.
     * @param sysfsRoot The sysfs input class directory; overridable for tests.
     * @return The driver, e.g. `xpad`, or empty when it cannot be determined.
     */
    [[nodiscard]] std::string ReadEvdevDriverName(const std::string& nodeName,
                                                  const std::string& sysfsRoot = "/sys/class/input");

    /**
     * @brief Reads what sysfs says about an input node, without opening the node.
     *
     * This is how the hub tells a controller from everything else. Opening every node to ask it
     * would open every keyboard and mouse on the system, which is not a controller service's
     * business -- and it is slow: closing an evdev node waits for an RCU grace period in the
     * kernel, measured at 23-72 ms per node on a laptop, so classifying its sixteen nodes by
     * opening them stalled the first controller query for most of a second.
     *
     * Everything but the axes' ranges, which only the node itself reports.
     *
     * @param nodeName The node's file name, e.g. `event7`.
     * @param description Receives the name, identity, key/axis/force-feedback bitmaps, properties
     * and driver.
     * @param sysfsRoot The sysfs input class directory; overridable for tests.
     * @return False when sysfs does not describe the node.
     */
    [[nodiscard]] bool ReadEvdevSysfsDescription(const std::string& nodeName,
                                                 EvdevDescription& description,
                                                 const std::string& sysfsRoot = "/sys/class/input");

    /**
     * @brief Parses one sysfs capability bitmap, as `capabilities/key` or `properties` print it.
     *
     * @param text Hexadecimal words separated by spaces, most significant first; the kernel omits
     * leading zero words but prints zero words in the middle. Each word is an `unsigned long` of
     * the reading process (the kernel prints 32-bit words to a 32-bit reader).
     * @return The words, least significant first; empty when @p text is not such a bitmap.
     */
    [[nodiscard]] std::vector<unsigned long> ParseEvdevSysfsBitmap(const std::string& text);

} // namespace CNA::Platform::Linux
