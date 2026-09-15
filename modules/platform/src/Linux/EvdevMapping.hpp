// SPDX-License-Identifier: MS-PL
#pragma once

#include "EvdevLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Controller mappings in the community database's format (the one gamecontrollerdb.txt and every
// SDL-based game use): "GUID,name,a:b0,b:b1,leftx:a0,dpup:h0.1,...". They are what turns a pad
// whose driver does not follow the kernel's gamepad API -- most generic USB and Bluetooth HID pads
// -- into a gamepad at all (plans/plan_x11.md X11-0160). Nothing here does I/O except reading a
// mapping file; the numbering and the matching are pure functions of a device's description.

namespace CNA::Platform::Linux {

    /**
     * @brief The CRC-16 databases use to tell apart devices that share a vendor and product id.
     *
     * CRC-16/ARC: reflected polynomial 0xA001, initial value 0, over the device's name.
     *
     * @param name The device name.
     * @return The checksum.
     */
    [[nodiscard]] std::uint16_t ControllerNameCrc16(std::string_view name);

    /** @brief One input a mapping reads from the device. */
    struct ControllerMappingInput
    {
        /** @brief What kind of input. */
        enum class Kind : std::uint8_t
        {
            /** @brief The device's Nth button. */
            Button,
            /** @brief The device's Nth axis, whole or half. */
            Axis,
            /** @brief One direction of the device's Nth hat. */
            Hat
        };

        /** @brief What kind of input. */
        Kind kind = Kind::Button;
        /** @brief The button, axis or hat index, as databases number them. */
        int index = 0;
        /**
         * @brief For an axis, the range read, on the [-32768, 32767] scale: the whole axis, one
         * half, or either inverted (minimum above maximum).
         */
        int minimum = 0;
        /** @brief The other end of @ref minimum's range. */
        int maximum = 0;
        /** @brief For a hat, the direction: 1 up, 2 right, 4 down, 8 left. */
        std::uint8_t hatMask = 0;
    };

    /** @brief One gamepad control a mapping drives. */
    struct ControllerMappingOutput
    {
        /** @brief True for a button, false for an axis. */
        bool isButton = true;
        /** @brief The button, when @ref isButton. */
        GamepadButton button = GamepadButton::A;
        /** @brief The axis, when not @ref isButton. */
        GamepadAxis axis = GamepadAxis::LeftThumbstickX;
        /**
         * @brief For an axis, the range driven on the [-32768, 32767] scale, positive down for the
         * vertical sticks as databases write them: the whole stick, one half, or a trigger's
         * [0, 32767].
         */
        int minimum = 0;
        /** @brief The other end of @ref minimum's range. */
        int maximum = 0;
    };

    /** @brief One element of a mapping: an input and the control it drives. */
    struct ControllerMappingBinding
    {
        /** @brief The input. */
        ControllerMappingInput input;
        /** @brief The control. */
        ControllerMappingOutput output;
    };

    /** @brief One parsed database entry. */
    struct ControllerMapping
    {
        /** @brief The GUID it applies to, without a name checksum in it. */
        std::array<std::uint8_t, 16> guid{};
        /** @brief The controller's name, as the entry gives it. */
        std::string name;
        /** @brief The name checksum the device must also have, when the entry states one. */
        std::optional<std::uint16_t> crc;
        /** @brief The elements, in the entry's order. */
        std::vector<ControllerMappingBinding> bindings;
    };

    /**
     * @brief Parses one database line.
     *
     * An unknown element is skipped, as databases expect of a reader older than the entry. Of the
     * conditions an entry can carry (`hint:`), the two that say whether "a", "b", "x", "y" name
     * positions or a Nintendo-style pad's printed labels turn a labelled entry into a positional
     * one; any other takes the default the entry states.
     *
     * @param line The line.
     * @return The mapping; nothing for a comment, a blank or malformed line, an entry for another
     * platform, or one whose condition does not hold.
     */
    [[nodiscard]] std::optional<ControllerMapping> ParseControllerMapping(std::string_view line);

    /**
     * @brief The GUID databases key a device by: bus, vendor, product and version as little-endian
     * 16-bit fields at bytes 0, 4, 8 and 12 -- or, for a device with neither vendor nor product,
     * the start of its name from byte 4 -- and no name checksum.
     *
     * @param description The device.
     * @return The GUID.
     */
    [[nodiscard]] std::array<std::uint8_t, 16> ControllerGuidOf(const EvdevDescription& description);

    /** @brief The kernel codes a mapping's indices name. */
    struct ControllerInputNumbering
    {
        /** @brief Button index to key code. */
        std::vector<std::uint16_t> buttons;
        /** @brief Axis index to `ABS_*` code. */
        std::vector<std::uint16_t> axes;
        /** @brief Hat index to its `ABS_HATnX` code (its Y axis is the next code). */
        std::vector<std::uint16_t> hats;
    };

    /**
     * @brief Numbers a device's inputs as databases do.
     *
     * Buttons: the joystick range and everything above it in code order, then everything below.
     * Hats: each `ABS_HATn` pair that looks digital -- a range of -1..1, or no fuzz, flat or
     * resolution -- in order. Axes: every other absolute axis in code order, a hat that looks
     * analogue included.
     *
     * @param description The device.
     * @return The numbering.
     */
    [[nodiscard]] ControllerInputNumbering NumberControllerInputs(const EvdevDescription& description);

    /** @brief Mappings to look devices up in; a later entry for a device replaces an earlier one. */
    class ControllerMappingDatabase
    {
    public:
        /**
         * @brief Adds every usable entry of a text, one per line.
         *
         * @param text The text.
         * @return How many entries were added or replaced.
         */
        std::size_t AddMappings(std::string_view text);

        /**
         * @brief Adds every usable entry of a file.
         *
         * @param path The file.
         * @return How many entries were added or replaced; 0 when it cannot be read.
         */
        std::size_t AddMappingsFromFile(const std::string& path);

        /**
         * @brief Builds the database the environment asks for: the file named by
         * `CNA_GAMECONTROLLERCONFIG_FILE`, then the entries in `CNA_GAMECONTROLLERCONFIG`.
         *
         * @return The database; empty when neither is set.
         */
        [[nodiscard]] static ControllerMappingDatabase FromEnvironment();

        /**
         * @brief Finds the mapping for a device.
         *
         * An entry for its exact identity first, then one for any version of it; an entry that
         * states a name checksum applies only to a device whose name has it.
         *
         * @param description The device.
         * @return The mapping, or null when there is none.
         */
        [[nodiscard]] const ControllerMapping* Find(const EvdevDescription& description) const;

        /** @brief Gets how many entries the database holds. @return The count. */
        [[nodiscard]] std::size_t GetCount() const { return mappings_.size(); }

    private:
        std::vector<ControllerMapping> mappings_;
    };

    /**
     * @brief Builds a gamepad layout from a mapping, resolved to the device's kernel codes.
     *
     * An element naming an input the device does not have is left out.
     *
     * @param mapping The mapping.
     * @param description The device.
     * @return The layout; its `mapped` bindings drive the state instead of the gamepad API's.
     */
    [[nodiscard]] EvdevGamepadLayout BuildMappedGamepadLayout(const ControllerMapping& mapping,
                                                              const EvdevDescription& description);

} // namespace CNA::Platform::Linux
