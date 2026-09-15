// SPDX-License-Identifier: MS-PL

#include "EvdevDevice.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace CNA::Platform::Linux {

    namespace {

        constexpr std::size_t kBitsPerLong = sizeof(unsigned long) * CHAR_BIT;

        template <std::size_t Count>
        std::bitset<Count> BitsFromWords(const unsigned long* words, const std::size_t wordCount)
        {
            std::bitset<Count> bits;
            for (std::size_t index = 0; index < Count && index / kBitsPerLong < wordCount; ++index)
            {
                if (((words[index / kBitsPerLong] >> (index % kBitsPerLong)) & 1u) != 0)
                {
                    bits.set(index);
                }
            }
            return bits;
        }

        template <std::size_t Count>
        std::bitset<Count> ReadBits(const int descriptor, const unsigned long request)
        {
            std::array<unsigned long, (Count + kBitsPerLong - 1) / kBitsPerLong> words{};
            if (ioctl(descriptor, request, words.data()) < 0)
            {
                return {};
            }
            return BitsFromWords<Count>(words.data(), words.size());
        }

        /// The first line of a small sysfs attribute, without its newline.
        bool ReadAttribute(const std::filesystem::path& path, std::string& value)
        {
            std::ifstream file(path);
            if (!file)
            {
                return false;
            }
            std::getline(file, value);
            return !file.bad();
        }

        template <std::size_t Count>
        bool ReadSysfsBits(const std::filesystem::path& path, std::bitset<Count>& bits)
        {
            std::string text;
            if (!ReadAttribute(path, text))
            {
                return false;
            }
            const std::vector<unsigned long> words = ParseEvdevSysfsBitmap(text);
            if (words.empty())
            {
                return false;
            }
            bits = BitsFromWords<Count>(words.data(), words.size());
            return true;
        }

        bool ReadSysfsHex(const std::filesystem::path& path, std::uint16_t& value)
        {
            std::string text;
            if (!ReadAttribute(path, text) || text.empty())
            {
                return false;
            }
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(text.c_str(), &end, 16);
            if (end == text.c_str() || parsed > 0xFFFFu)
            {
                return false;
            }
            value = static_cast<std::uint16_t>(parsed);
            return true;
        }

        input_event MakeEvent(const std::uint16_t type, const std::uint16_t code,
                              const std::int32_t value)
        {
            input_event event{};
            event.type = type;
            event.code = code;
            event.value = value;
            return event;
        }

    } // namespace

    std::string ReadEvdevDriverName(const std::string& nodeName, const std::string& sysfsRoot)
    {
        // /sys/class/input/eventN/device is the input device; its own `device` is what the driver
        // is bound to -- the USB interface for xpad, the HID device for hid-playstation.
        std::error_code error;
        const std::filesystem::path driver =
            std::filesystem::path(sysfsRoot) / nodeName / "device" / "device" / "driver";
        const std::filesystem::path target = std::filesystem::read_symlink(driver, error);
        if (error)
        {
            return {};
        }
        return target.filename().string();
    }

    std::vector<unsigned long> ParseEvdevSysfsBitmap(const std::string& text)
    {
        std::vector<unsigned long> words;
        std::size_t position = 0;
        while (position < text.size())
        {
            while (position < text.size() && (text[position] == ' ' || text[position] == '\n'))
            {
                ++position;
            }
            if (position == text.size())
            {
                break;
            }
            const std::size_t start = position;
            while (position < text.size() && std::isxdigit(static_cast<unsigned char>(text[position])))
            {
                ++position;
            }
            const std::size_t digits = position - start;
            if (digits == 0 || digits > kBitsPerLong / 4 ||
                (position < text.size() && text[position] != ' ' && text[position] != '\n'))
            {
                return {};
            }
            words.push_back(std::stoul(text.substr(start, digits), nullptr, 16));
        }
        std::reverse(words.begin(), words.end());
        return words;
    }

    bool ReadEvdevSysfsDescription(const std::string& nodeName, EvdevDescription& description,
                                   const std::string& sysfsRoot)
    {
        const std::filesystem::path device = std::filesystem::path(sysfsRoot) / nodeName / "device";
        EvdevDescription read;
        if (!ReadAttribute(device / "name", read.name) ||
            !ReadSysfsBits(device / "capabilities" / "key", read.keys) ||
            !ReadSysfsBits(device / "capabilities" / "abs", read.axes) ||
            !ReadSysfsHex(device / "id" / "bustype", read.bus) ||
            !ReadSysfsHex(device / "id" / "vendor", read.vendor) ||
            !ReadSysfsHex(device / "id" / "product", read.product) ||
            !ReadSysfsHex(device / "id" / "version", read.version))
        {
            return false;
        }
        // Optional: older kernels have no `properties`, and a device without force feedback may
        // print an empty `ff`.
        (void) ReadSysfsBits(device / "capabilities" / "ff", read.forceFeedback);
        (void) ReadSysfsBits(device / "properties", read.properties);
        (void) ReadAttribute(device / "uniq", read.uniq);
        read.driver = ReadEvdevDriverName(nodeName, sysfsRoot);
        description = std::move(read);
        return true;
    }

    std::unique_ptr<EvdevDevice> EvdevDevice::Open(const std::string& path)
    {
        bool writable = true;
        int descriptor = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (descriptor < 0 && (errno == EACCES || errno == EPERM || errno == EROFS))
        {
            writable = false;
            descriptor = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        }
        if (descriptor < 0)
        {
            return nullptr;
        }
        int version = 0;
        if (ioctl(descriptor, EVIOCGVERSION, &version) < 0)
        {
            ::close(descriptor);
            return nullptr;
        }

        EvdevDescription description;
        std::array<char, 256> name{};
        if (ioctl(descriptor, EVIOCGNAME(name.size() - 1), name.data()) >= 0)
        {
            description.name = name.data();
        }
        std::array<char, 256> uniq{};
        if (ioctl(descriptor, EVIOCGUNIQ(uniq.size() - 1), uniq.data()) >= 0)
        {
            description.uniq = uniq.data();
        }
        input_id identity{};
        if (ioctl(descriptor, EVIOCGID, &identity) >= 0)
        {
            description.bus = identity.bustype;
            description.vendor = identity.vendor;
            description.product = identity.product;
            description.version = identity.version;
        }
        description.keys = ReadBits<KEY_CNT>(descriptor, EVIOCGBIT(EV_KEY, (KEY_CNT + 7) / 8));
        description.axes = ReadBits<ABS_CNT>(descriptor, EVIOCGBIT(EV_ABS, (ABS_CNT + 7) / 8));
        description.forceFeedback = ReadBits<FF_CNT>(descriptor, EVIOCGBIT(EV_FF, (FF_CNT + 7) / 8));
        description.properties =
            ReadBits<INPUT_PROP_CNT>(descriptor, EVIOCGPROP((INPUT_PROP_CNT + 7) / 8));
        for (std::size_t code = 0; code < ABS_CNT; ++code)
        {
            if (!description.axes.test(code))
            {
                continue;
            }
            input_absinfo info{};
            if (ioctl(descriptor, EVIOCGABS(code), &info) >= 0)
            {
                description.ranges[code] = {info.minimum, info.maximum, info.flat, info.fuzz,
                                            info.resolution};
            }
        }
        description.driver =
            ReadEvdevDriverName(std::filesystem::path(path).filename().string());
        return std::unique_ptr<EvdevDevice>(
            new EvdevDevice(descriptor, path, std::move(description), writable));
    }

    EvdevDevice::EvdevDevice(const int descriptor, std::string path, EvdevDescription description,
                             const bool writable)
        : descriptor_(descriptor), path_(std::move(path)), description_(std::move(description)),
          writable_(writable)
    {
    }

    EvdevDevice::~EvdevDevice()
    {
        if (descriptor_ < 0)
        {
            return;
        }
        if (effect_ >= 0)
        {
            // Removing the effect also stops it: a game that exits mid-rumble must not leave the
            // pad buzzing.
            ioctl(descriptor_, EVIOCRMFF, effect_);
        }
        ::close(descriptor_);
    }

    bool EvdevDevice::Drain(std::vector<input_event>& events)
    {
        std::array<input_event, 64> buffer{};
        while (true)
        {
            const ssize_t bytes = ::read(descriptor_, buffer.data(), sizeof(buffer));
            if (bytes < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                // EAGAIN: everything queued has been read. ENODEV: the device was unplugged.
                return errno == EAGAIN || errno == EWOULDBLOCK;
            }
            if (bytes == 0)
            {
                return false;
            }
            const std::size_t count = static_cast<std::size_t>(bytes) / sizeof(input_event);
            for (std::size_t index = 0; index < count; ++index)
            {
                const input_event& event = buffer[index];
                if (event.type == EV_SYN && event.code == SYN_DROPPED)
                {
                    dropping_ = true;
                    continue;
                }
                if (dropping_)
                {
                    if (event.type == EV_SYN && event.code == SYN_REPORT)
                    {
                        dropping_ = false;
                        AppendCurrentState(events);
                        // The rest of this read is OLDER than the state just read, and must not
                        // be applied on top of it: reading the key state makes the kernel discard
                        // the key events still queued (EVIOCGKEY flushes them), so a stale press
                        // left in this buffer would have nothing newer behind it to correct it.
                        // Absolute axes are not flushed, but what is still queued for them ends
                        // on the same value the state read reported.
                        break;
                    }
                    continue;
                }
                if (event.type == EV_KEY || event.type == EV_ABS)
                {
                    events.push_back(event);
                }
            }
        }
    }

    void EvdevDevice::AppendCurrentState(std::vector<input_event>& events) const
    {
        const std::bitset<KEY_CNT> held =
            ReadBits<KEY_CNT>(descriptor_, EVIOCGKEY((KEY_CNT + 7) / 8));
        for (std::size_t code = 0; code < KEY_CNT; ++code)
        {
            if (description_.keys.test(code))
            {
                events.push_back(MakeEvent(EV_KEY, static_cast<std::uint16_t>(code),
                                           held.test(code) ? 1 : 0));
            }
        }
        for (std::size_t code = 0; code < ABS_CNT; ++code)
        {
            if (!description_.axes.test(code))
            {
                continue;
            }
            input_absinfo info{};
            if (ioctl(descriptor_, EVIOCGABS(code), &info) >= 0)
            {
                events.push_back(MakeEvent(EV_ABS, static_cast<std::uint16_t>(code), info.value));
            }
        }
    }

    bool EvdevDevice::CanRumble() const
    {
        return writable_ && description_.forceFeedback.test(FF_RUMBLE);
    }

    bool EvdevDevice::Rumble(const float low, const float high,
                             const std::uint32_t durationMilliseconds)
    {
        if (!CanRumble())
        {
            return false;
        }
        const auto magnitude = [](const float level) {
            const float clamped = std::isnan(level) ? 0.0f : std::clamp(level, 0.0f, 1.0f);
            return static_cast<std::uint16_t>(std::lround(clamped * 65535.0f));
        };
        const std::uint16_t strong = magnitude(low);
        const std::uint16_t weak = magnitude(high);
        if (strong == 0 && weak == 0)
        {
            if (effect_ >= 0)
            {
                const input_event stop = MakeEvent(EV_FF, static_cast<std::uint16_t>(effect_), 0);
                return ::write(descriptor_, &stop, sizeof(stop)) == static_cast<ssize_t>(sizeof(stop));
            }
            return true;
        }

        ff_effect effect{};
        effect.type = FF_RUMBLE;
        // -1 asks the kernel for a new slot; re-using ours updates the effect in place instead of
        // filling the device's effect memory with one upload per call.
        effect.id = static_cast<std::int16_t>(effect_);
        effect.u.rumble.strong_magnitude = strong;
        effect.u.rumble.weak_magnitude = weak;
        // 0 is "until changed" for the memoryless drivers every rumbling pad uses, and what XNA's
        // SetVibration means; anything longer than the kernel's 16-bit field is capped.
        effect.replay.length =
            static_cast<std::uint16_t>(std::min<std::uint32_t>(durationMilliseconds, 0xFFFFu));
        effect.replay.delay = 0;
        if (ioctl(descriptor_, EVIOCSFF, &effect) < 0)
        {
            return false;
        }
        effect_ = effect.id;
        const input_event play = MakeEvent(EV_FF, static_cast<std::uint16_t>(effect_), 1);
        return ::write(descriptor_, &play, sizeof(play)) == static_cast<ssize_t>(sizeof(play));
    }

} // namespace CNA::Platform::Linux
