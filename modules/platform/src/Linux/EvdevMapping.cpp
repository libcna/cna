// SPDX-License-Identifier: MS-PL

#include "EvdevMapping.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <utility>

namespace CNA::Platform::Linux {

    namespace {

        constexpr int kAxisMinimum = -32768;
        constexpr int kAxisMaximum = 32767;

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
            {
                text.remove_suffix(1);
            }
            return text;
        }

        std::optional<int> HexDigit(const char character)
        {
            if (character >= '0' && character <= '9') { return character - '0'; }
            if (character >= 'a' && character <= 'f') { return character - 'a' + 10; }
            if (character >= 'A' && character <= 'F') { return character - 'A' + 10; }
            return std::nullopt;
        }

        std::optional<int> Number(std::string_view text)
        {
            if (text.empty() || text.size() > 6)
            {
                return std::nullopt;
            }
            int value = 0;
            for (const char character : text)
            {
                if (character < '0' || character > '9')
                {
                    return std::nullopt;
                }
                value = value * 10 + (character - '0');
            }
            return value;
        }

        std::optional<GamepadButton> ButtonNamed(const std::string_view name)
        {
            // Positions, as databases mean them: "a" is the bottom face button whatever is printed
            // on it -- which is also what CNA's GamepadButton::A is.
            static const std::pair<std::string_view, GamepadButton> buttons[] = {
                {"a", GamepadButton::A},
                {"b", GamepadButton::B},
                {"x", GamepadButton::X},
                {"y", GamepadButton::Y},
                {"back", GamepadButton::Back},
                {"guide", GamepadButton::BigButton},
                {"start", GamepadButton::Start},
                {"leftstick", GamepadButton::LeftStick},
                {"rightstick", GamepadButton::RightStick},
                {"leftshoulder", GamepadButton::LeftShoulder},
                {"rightshoulder", GamepadButton::RightShoulder},
                {"dpup", GamepadButton::DPadUp},
                {"dpdown", GamepadButton::DPadDown},
                {"dpleft", GamepadButton::DPadLeft},
                {"dpright", GamepadButton::DPadRight},
                {"misc1", GamepadButton::Misc1},
                {"paddle1", GamepadButton::Paddle1},
                {"paddle2", GamepadButton::Paddle2},
                {"paddle3", GamepadButton::Paddle3},
                {"paddle4", GamepadButton::Paddle4},
                {"touchpad", GamepadButton::TouchPad},
            };
            for (const auto& [candidate, button] : buttons)
            {
                if (candidate == name) { return button; }
            }
            return std::nullopt;
        }

        std::optional<GamepadAxis> AxisNamed(const std::string_view name)
        {
            static const std::pair<std::string_view, GamepadAxis> axes[] = {
                {"leftx", GamepadAxis::LeftThumbstickX},
                {"lefty", GamepadAxis::LeftThumbstickY},
                {"rightx", GamepadAxis::RightThumbstickX},
                {"righty", GamepadAxis::RightThumbstickY},
                {"lefttrigger", GamepadAxis::LeftTrigger},
                {"righttrigger", GamepadAxis::RightTrigger},
            };
            for (const auto& [candidate, axis] : axes)
            {
                if (candidate == name) { return axis; }
            }
            return std::nullopt;
        }

        std::optional<ControllerMappingOutput> ParseOutput(std::string_view key)
        {
            char half = 0;
            if (!key.empty() && (key.front() == '+' || key.front() == '-'))
            {
                half = key.front();
                key.remove_prefix(1);
            }
            ControllerMappingOutput output;
            if (const std::optional<GamepadAxis> axis = AxisNamed(key))
            {
                output.isButton = false;
                output.axis = *axis;
                if (*axis == GamepadAxis::LeftTrigger || *axis == GamepadAxis::RightTrigger)
                {
                    output.minimum = 0;
                    output.maximum = kAxisMaximum;
                }
                else if (half == '+')
                {
                    output.minimum = 0;
                    output.maximum = kAxisMaximum;
                }
                else if (half == '-')
                {
                    output.minimum = 0;
                    output.maximum = kAxisMinimum;
                }
                else
                {
                    output.minimum = kAxisMinimum;
                    output.maximum = kAxisMaximum;
                }
                return output;
            }
            if (half != 0)
            {
                return std::nullopt;
            }
            if (const std::optional<GamepadButton> button = ButtonNamed(key))
            {
                output.button = *button;
                return output;
            }
            return std::nullopt;
        }

        std::optional<ControllerMappingInput> ParseInput(std::string_view value)
        {
            char half = 0;
            if (!value.empty() && (value.front() == '+' || value.front() == '-'))
            {
                half = value.front();
                value.remove_prefix(1);
            }
            bool invert = false;
            if (!value.empty() && value.back() == '~')
            {
                invert = true;
                value.remove_suffix(1);
            }
            if (value.size() < 2)
            {
                return std::nullopt;
            }
            ControllerMappingInput input;
            const char kind = value.front();
            value.remove_prefix(1);
            if (kind == 'a')
            {
                const std::optional<int> index = Number(value);
                if (!index) { return std::nullopt; }
                input.kind = ControllerMappingInput::Kind::Axis;
                input.index = *index;
                if (half == '+')
                {
                    input.minimum = 0;
                    input.maximum = kAxisMaximum;
                }
                else if (half == '-')
                {
                    input.minimum = 0;
                    input.maximum = kAxisMinimum;
                }
                else
                {
                    input.minimum = kAxisMinimum;
                    input.maximum = kAxisMaximum;
                }
                if (invert)
                {
                    std::swap(input.minimum, input.maximum);
                }
                return input;
            }
            if (half != 0 || invert)
            {
                return std::nullopt;
            }
            if (kind == 'b')
            {
                const std::optional<int> index = Number(value);
                if (!index) { return std::nullopt; }
                input.kind = ControllerMappingInput::Kind::Button;
                input.index = *index;
                return input;
            }
            if (kind == 'h')
            {
                const std::size_t dot = value.find('.');
                if (dot == std::string_view::npos) { return std::nullopt; }
                const std::optional<int> index = Number(value.substr(0, dot));
                const std::optional<int> mask = Number(value.substr(dot + 1));
                if (!index || !mask || *mask <= 0 || *mask > 15) { return std::nullopt; }
                input.kind = ControllerMappingInput::Kind::Hat;
                input.index = *index;
                input.hatMask = static_cast<std::uint8_t>(*mask);
                return input;
            }
            return std::nullopt;
        }

        bool LooksDigital(const EvdevDescription& description, const int xCode)
        {
            const bool hasX = description.axes.test(static_cast<std::size_t>(xCode));
            const bool hasY = description.axes.test(static_cast<std::size_t>(xCode + 1));
            if (!hasX && !hasY)
            {
                return false;
            }
            const EvdevAxisRange& x = description.ranges[static_cast<std::size_t>(xCode)];
            const EvdevAxisRange& y = description.ranges[static_cast<std::size_t>(xCode + 1)];
            const auto unitRange = [](const EvdevAxisRange& range) {
                return range.minimum == -1 && range.maximum == 1;
            };
            const auto plain = [](const EvdevAxisRange& range) {
                return range.fuzz == 0 && range.flat == 0 && range.resolution == 0;
            };
            if ((!hasX || unitRange(x)) && (!hasY || unitRange(y)))
            {
                return true;
            }
            return (!hasX || plain(x)) && (!hasY || plain(y));
        }

        bool EndsWith(const std::string_view text, const std::string_view suffix)
        {
            return text.size() >= suffix.size() &&
                   text.substr(text.size() - suffix.size()) == suffix;
        }

        void SwapFaceButtons(ControllerMapping& mapping, const GamepadButton first,
                             const GamepadButton second)
        {
            for (ControllerMappingBinding& binding : mapping.bindings)
            {
                if (!binding.output.isButton) { continue; }
                if (binding.output.button == first) { binding.output.button = second; }
                else if (binding.output.button == second) { binding.output.button = first; }
            }
        }

        /// A `hint:[!]NAME[:=value]` condition. Two of them are not conditions at all but say
        /// whether "a", "b", "x", "y" name positions or the labels printed on a Nintendo-style
        /// pad; a labelled entry is turned into a positional one, as every reader of the
        /// database does. Any other condition names a setting of the library that wrote it, so
        /// it takes the default the entry states.
        bool ApplyCondition(std::string_view hint, ControllerMapping& mapping)
        {
            bool negate = false;
            if (!hint.empty() && hint.front() == '!')
            {
                negate = true;
                hint.remove_prefix(1);
            }
            bool fallback = false;
            if (const std::size_t assign = hint.find(":="); assign != std::string_view::npos)
            {
                fallback = std::atoi(std::string(hint.substr(assign + 2)).c_str()) != 0;
                hint = hint.substr(0, assign);
            }
            if (EndsWith(hint, "_USE_BUTTON_LABELS"))
            {
                if (!negate)
                {
                    // Labels B A Y X where positions are A B X Y.
                    SwapFaceButtons(mapping, GamepadButton::A, GamepadButton::B);
                    SwapFaceButtons(mapping, GamepadButton::X, GamepadButton::Y);
                }
                return true;
            }
            if (EndsWith(hint, "_USE_GAMECUBE_LABELS"))
            {
                if (!negate)
                {
                    // A GameCube pad's B is on the left and its X on the right.
                    SwapFaceButtons(mapping, GamepadButton::B, GamepadButton::X);
                }
                return true;
            }
            return negate ? !fallback : fallback;
        }

        std::array<std::uint8_t, 16> WithoutVersion(std::array<std::uint8_t, 16> guid)
        {
            guid[12] = 0;
            guid[13] = 0;
            return guid;
        }

    } // namespace

    std::uint16_t ControllerNameCrc16(const std::string_view name)
    {
        std::uint16_t crc = 0;
        for (const char character : name)
        {
            crc ^= static_cast<std::uint8_t>(character);
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = (crc & 1u) != 0 ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001u)
                                      : static_cast<std::uint16_t>(crc >> 1);
            }
        }
        return crc;
    }

    std::optional<ControllerMapping> ParseControllerMapping(std::string_view line)
    {
        line = Trim(line);
        if (line.empty() || line.front() == '#')
        {
            return std::nullopt;
        }
        std::vector<std::string_view> fields;
        std::size_t start = 0;
        while (start <= line.size())
        {
            std::size_t end = line.find(',', start);
            if (end == std::string_view::npos) { end = line.size(); }
            fields.push_back(Trim(line.substr(start, end - start)));
            start = end + 1;
        }
        if (fields.size() < 3 || fields[0].size() != 32)
        {
            return std::nullopt;
        }

        ControllerMapping mapping;
        for (std::size_t index = 0; index < 16; ++index)
        {
            const std::optional<int> high = HexDigit(fields[0][2 * index]);
            const std::optional<int> low = HexDigit(fields[0][2 * index + 1]);
            if (!high || !low) { return std::nullopt; }
            mapping.guid[index] = static_cast<std::uint8_t>(*high * 16 + *low);
        }
        // A checksum written into the GUID itself is moved out of it: GUIDs are matched without
        // one, and the checksum is compared on its own.
        const auto embedded =
            static_cast<std::uint16_t>(mapping.guid[2] | (mapping.guid[3] << 8));
        if (embedded != 0)
        {
            mapping.crc = embedded;
            mapping.guid[2] = 0;
            mapping.guid[3] = 0;
        }
        mapping.name = std::string(fields[1]);

        std::string_view hint;
        for (std::size_t index = 2; index < fields.size(); ++index)
        {
            const std::string_view field = fields[index];
            if (field.empty()) { continue; }
            const std::size_t colon = field.find(':');
            if (colon == std::string_view::npos) { continue; }
            const std::string_view key = field.substr(0, colon);
            const std::string_view value = field.substr(colon + 1);
            if (key == "platform")
            {
                if (value != "Linux") { return std::nullopt; }
                continue;
            }
            if (key == "hint")
            {
                hint = value;
                continue;
            }
            if (key == "crc")
            {
                char* end = nullptr;
                const std::string text(value);
                const unsigned long crc = std::strtoul(text.c_str(), &end, 16);
                if (end == text.c_str() || *end != '\0' || crc > 0xFFFF) { return std::nullopt; }
                mapping.crc = static_cast<std::uint16_t>(crc);
                continue;
            }
            const std::optional<ControllerMappingOutput> output = ParseOutput(key);
            const std::optional<ControllerMappingInput> input = ParseInput(value);
            if (!output || !input)
            {
                // An element this reader does not know, or cannot read: the rest still applies.
                continue;
            }
            mapping.bindings.push_back({*input, *output});
        }
        if (!hint.empty() && !ApplyCondition(hint, mapping))
        {
            return std::nullopt;
        }
        if (mapping.bindings.empty())
        {
            return std::nullopt;
        }
        return mapping;
    }

    std::array<std::uint8_t, 16> ControllerGuidOf(const EvdevDescription& description)
    {
        std::array<std::uint8_t, 16> guid{};
        const auto put = [&guid](const std::size_t at, const std::uint16_t value) {
            guid[at] = static_cast<std::uint8_t>(value & 0xFFu);
            guid[at + 1] = static_cast<std::uint8_t>(value >> 8);
        };
        put(0, description.bus);
        if (description.vendor != 0 || description.product != 0)
        {
            put(4, description.vendor);
            put(8, description.product);
            put(12, description.version);
        }
        else
        {
            // No identity to key by: the name's first eleven bytes stand in, as databases write
            // such an entry.
            for (std::size_t index = 0; index < 11 && index < description.name.size(); ++index)
            {
                guid[4 + index] = static_cast<std::uint8_t>(description.name[index]);
            }
        }
        return guid;
    }

    ControllerInputNumbering NumberControllerInputs(const EvdevDescription& description)
    {
        ControllerInputNumbering numbering;
        for (std::uint32_t code = BTN_JOYSTICK; code < KEY_MAX; ++code)
        {
            if (description.keys.test(code)) { numbering.buttons.push_back(static_cast<std::uint16_t>(code)); }
        }
        for (std::uint32_t code = 0; code < BTN_JOYSTICK; ++code)
        {
            if (description.keys.test(code)) { numbering.buttons.push_back(static_cast<std::uint16_t>(code)); }
        }
        std::array<bool, 4> digitalHat{};
        for (int hat = 0; hat < 4; ++hat)
        {
            const int xCode = ABS_HAT0X + 2 * hat;
            if (LooksDigital(description, xCode))
            {
                digitalHat[static_cast<std::size_t>(hat)] = true;
                numbering.hats.push_back(static_cast<std::uint16_t>(xCode));
            }
        }
        for (std::uint32_t code = 0; code < ABS_MAX; ++code)
        {
            if (code >= ABS_HAT0X && code <= ABS_HAT3Y &&
                digitalHat[static_cast<std::size_t>(code - ABS_HAT0X) / 2])
            {
                continue;
            }
            if (description.axes.test(code)) { numbering.axes.push_back(static_cast<std::uint16_t>(code)); }
        }
        return numbering;
    }

    std::size_t ControllerMappingDatabase::AddMappings(const std::string_view text)
    {
        std::size_t added = 0;
        std::size_t start = 0;
        while (start < text.size())
        {
            std::size_t end = text.find('\n', start);
            if (end == std::string_view::npos) { end = text.size(); }
            if (std::optional<ControllerMapping> mapping = ParseControllerMapping(text.substr(start, end - start)))
            {
                const auto same = std::find_if(mappings_.begin(), mappings_.end(),
                                               [&mapping](const ControllerMapping& existing) {
                                                   return existing.guid == mapping->guid &&
                                                          existing.crc == mapping->crc;
                                               });
                if (same != mappings_.end())
                {
                    *same = std::move(*mapping);
                }
                else
                {
                    mappings_.push_back(std::move(*mapping));
                }
                ++added;
            }
            start = end + 1;
        }
        return added;
    }

    std::size_t ControllerMappingDatabase::AddMappingsFromFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return 0;
        }
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return AddMappings(text);
    }

    ControllerMappingDatabase ControllerMappingDatabase::FromEnvironment()
    {
        ControllerMappingDatabase database;
        if (const char* path = std::getenv("CNA_GAMECONTROLLERCONFIG_FILE"); path != nullptr && *path != '\0')
        {
            database.AddMappingsFromFile(path);
        }
        if (const char* text = std::getenv("CNA_GAMECONTROLLERCONFIG"); text != nullptr)
        {
            database.AddMappings(text);
        }
        return database;
    }

    const ControllerMapping* ControllerMappingDatabase::Find(const EvdevDescription& description) const
    {
        const std::array<std::uint8_t, 16> guid = ControllerGuidOf(description);
        const std::uint16_t crc = ControllerNameCrc16(description.name);
        // The exact identity first, then any version of it. Within each, an entry stating the
        // name checksum the device has wins at once; one stating another checksum is not for this
        // device; one stating none is kept in case nothing more specific follows.
        for (const bool anyVersion : {false, true})
        {
            const ControllerMapping* candidate = nullptr;
            const std::array<std::uint8_t, 16> wanted = anyVersion ? WithoutVersion(guid) : guid;
            for (const ControllerMapping& mapping : mappings_)
            {
                const std::array<std::uint8_t, 16> offered =
                    anyVersion ? WithoutVersion(mapping.guid) : mapping.guid;
                if (offered != wanted)
                {
                    continue;
                }
                if (mapping.crc)
                {
                    if (*mapping.crc == crc) { return &mapping; }
                    continue;
                }
                if (candidate == nullptr) { candidate = &mapping; }
            }
            if (candidate != nullptr)
            {
                return candidate;
            }
        }
        return nullptr;
    }

    EvdevGamepadLayout BuildMappedGamepadLayout(const ControllerMapping& mapping,
                                                const EvdevDescription& description)
    {
        EvdevGamepadLayout layout;
        const ControllerInputNumbering numbering = NumberControllerInputs(description);
        for (const ControllerMappingBinding& element : mapping.bindings)
        {
            EvdevMappedBinding binding;
            const ControllerMappingInput& input = element.input;
            const auto index = static_cast<std::size_t>(input.index);
            switch (input.kind)
            {
                case ControllerMappingInput::Kind::Button:
                    if (index >= numbering.buttons.size()) { continue; }
                    binding.source = EvdevMappedBinding::Source::Key;
                    binding.code = numbering.buttons[index];
                    break;
                case ControllerMappingInput::Kind::Axis:
                    if (index >= numbering.axes.size()) { continue; }
                    binding.source = EvdevMappedBinding::Source::Axis;
                    binding.code = numbering.axes[index];
                    binding.range = description.ranges[binding.code];
                    binding.inputMinimum = input.minimum;
                    binding.inputMaximum = input.maximum;
                    break;
                case ControllerMappingInput::Kind::Hat:
                    if (index >= numbering.hats.size()) { continue; }
                    binding.source = EvdevMappedBinding::Source::Hat;
                    binding.code = numbering.hats[index];
                    binding.range = description.ranges[binding.code];
                    binding.yRange = description.ranges[binding.code + 1u];
                    binding.hatMask = input.hatMask;
                    break;
            }
            const ControllerMappingOutput& output = element.output;
            binding.toButton = output.isButton;
            binding.button = output.button;
            binding.axis = output.axis;
            binding.outputMinimum = output.minimum;
            binding.outputMaximum = output.maximum;
            if (output.isButton)
            {
                layout.buttonMask |= static_cast<std::uint32_t>(output.button);
            }
            else
            {
                layout.axisMask |= GamepadAxisBit(output.axis);
            }
            layout.mapped.push_back(binding);
        }
        return layout;
    }

} // namespace CNA::Platform::Linux
