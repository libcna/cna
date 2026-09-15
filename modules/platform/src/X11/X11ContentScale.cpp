// SPDX-License-Identifier: MS-PL

#include "X11ContentScale.hpp"

#include "X11Display.hpp"
#include "X11Error.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace CNA::Platform::X11 {

    namespace {

        std::size_t Pad4(const std::size_t length)
        {
            return (4 - (length % 4)) % 4;
        }

    } // namespace

    std::optional<float> UsableScale(const double scale) noexcept
    {
        if (!std::isfinite(scale) || scale < 0.5 || scale > 8.0)
        {
            return std::nullopt;
        }
        return static_cast<float>(scale);
    }

    std::optional<float> ScaleFromXftDpi(const std::string_view resources)
    {
        if (resources.empty())
        {
            return std::nullopt;
        }
        XrmInitialize();
        const std::string text(resources);
        XrmDatabase database = XrmGetStringDatabase(text.c_str());
        if (database == nullptr)
        {
            return std::nullopt;
        }
        std::optional<float> scale;
        char* type = nullptr;
        XrmValue value{};
        if (XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &type, &value) == kXTrue &&
            value.addr != nullptr)
        {
            scale = UsableScale(std::atof(value.addr) / 96.0);
        }
        XrmDestroyDatabase(database);
        return scale;
    }

    std::optional<float> ScaleFromXsettings(const std::vector<unsigned char>& property)
    {
        const std::size_t size = property.size();
        if (size < 12)
        {
            return std::nullopt;
        }
        const bool mostSignificantFirst = property[0] == 1;
        const auto read16 = [&](const std::size_t at) -> std::uint32_t {
            return mostSignificantFirst
                       ? (static_cast<std::uint32_t>(property[at]) << 8) | property[at + 1]
                       : (static_cast<std::uint32_t>(property[at + 1]) << 8) | property[at];
        };
        const auto read32 = [&](const std::size_t at) -> std::uint32_t {
            std::uint32_t value = 0;
            for (int index = 0; index < 4; ++index)
            {
                const std::uint32_t byte = property[at + static_cast<std::size_t>(index)];
                value |= mostSignificantFirst ? byte << (8 * (3 - index)) : byte << (8 * index);
            }
            return value;
        };

        std::optional<std::int32_t> windowScalingFactor;
        std::optional<std::int32_t> xftDpi;
        const std::uint32_t count = read32(8);
        std::size_t offset = 12;
        for (std::uint32_t setting = 0; setting < count; ++setting)
        {
            if (offset + 4 > size) { break; }
            const unsigned char type = property[offset];
            const std::size_t nameLength = read16(offset + 2);
            offset += 4;
            if (offset + nameLength > size) { break; }
            const std::string name(reinterpret_cast<const char*>(property.data() + offset),
                                   nameLength);
            offset += nameLength + Pad4(nameLength);
            // The setting's last-change serial.
            if (offset + 4 > size) { break; }
            offset += 4;
            if (type == 0)
            {
                if (offset + 4 > size) { break; }
                const auto value = static_cast<std::int32_t>(read32(offset));
                offset += 4;
                if (name == "Gdk/WindowScalingFactor") { windowScalingFactor = value; }
                else if (name == "Xft/DPI") { xftDpi = value; }
            }
            else if (type == 1)
            {
                if (offset + 4 > size) { break; }
                const std::size_t length = read32(offset);
                offset += 4;
                if (length > size - offset) { break; }
                offset += length + Pad4(length);
            }
            else if (type == 2)
            {
                offset += 8;
            }
            else
            {
                // A type this reader does not know has a length it cannot know either.
                break;
            }
        }

        if (windowScalingFactor && *windowScalingFactor > 0)
        {
            if (const std::optional<float> scale = UsableScale(*windowScalingFactor))
            {
                return scale;
            }
        }
        if (xftDpi && *xftDpi > 0)
        {
            return UsableScale(static_cast<double>(*xftDpi) / 1024.0 / 96.0);
        }
        return std::nullopt;
    }

    X11ScreenScaleFactors ParseScreenScaleFactors(const std::string_view value)
    {
        X11ScreenScaleFactors factors;
        std::size_t start = 0;
        while (start <= value.size())
        {
            std::size_t end = value.find(';', start);
            if (end == std::string_view::npos)
            {
                end = value.size();
            }
            const std::string entry(value.substr(start, end - start));
            start = end + 1;
            if (entry.empty())
            {
                continue;
            }
            const std::size_t equals = entry.find('=');
            const std::string number = equals == std::string::npos ? entry : entry.substr(equals + 1);
            char* parsedEnd = nullptr;
            const double parsed = std::strtod(number.c_str(), &parsedEnd);
            if (parsedEnd == number.c_str() || *parsedEnd != '\0')
            {
                continue;
            }
            const std::optional<float> scale = UsableScale(parsed);
            if (!scale)
            {
                continue;
            }
            if (equals == std::string::npos)
            {
                factors.positional.push_back(*scale);
            }
            else if (equals > 0)
            {
                factors.named[entry.substr(0, equals)] = *scale;
            }
        }
        return factors;
    }

    X11ContentScale::X11ContentScale(X11Connection& connection) : connection_(connection)
    {
        Display* display = connection_.GetDisplay();
        resourceManager_ = XInternAtom(display, "RESOURCE_MANAGER", kXFalse);
        const std::string selection = "_XSETTINGS_S" + std::to_string(connection_.GetScreen());
        settingsSelection_ = XInternAtom(display, selection.c_str(), kXFalse);
        settingsProperty_ = XInternAtom(display, "_XSETTINGS_SETTINGS", kXFalse);
        manager_ = XInternAtom(display, "MANAGER", kXFalse);
        if (const char* screens = std::getenv("QT_SCREEN_SCALE_FACTORS"))
        {
            screens_ = ParseScreenScaleFactors(screens);
        }
        FindSettingsManager();
        global_ = Read();
    }

    float X11ContentScale::ForMonitor(const std::string& outputName, const std::size_t index) const
    {
        if (const auto named = screens_.named.find(outputName); named != screens_.named.end())
        {
            return named->second;
        }
        if (screens_.named.empty() && index < screens_.positional.size())
        {
            return screens_.positional[index];
        }
        return global_;
    }

    void X11ContentScale::FindSettingsManager()
    {
        Display* display = connection_.GetDisplay();
        X11ErrorTrap trap(display);
        settingsOwner_ = XGetSelectionOwner(display, settingsSelection_);
        if (settingsOwner_ != kNone)
        {
            // Another client's window; the mask is this client's own and changes nothing for
            // anyone else. Its property is the settings; its destruction is the manager leaving.
            XSelectInput(display, settingsOwner_, PropertyChangeMask | StructureNotifyMask);
        }
        trap.Sync();
        if (trap.HasError())
        {
            // It went away between the two requests.
            settingsOwner_ = kNone;
        }
    }

    float X11ContentScale::Read() const
    {
        Display* display = connection_.GetDisplay();

        // Xft.dpi from the live property, not XResourceManagerString(): that is the string as it
        // was when the connection opened, and a session that changes its scale changes this.
        {
            std::vector<unsigned char> data;
            int format = 0;
            if (connection_.ReadProperty(connection_.GetRoot(), resourceManager_, XA_STRING, format,
                                         data) &&
                format == 8)
            {
                if (const std::optional<float> scale = ScaleFromXftDpi(
                        std::string_view(reinterpret_cast<const char*>(data.data()), data.size())))
                {
                    return *scale;
                }
            }
        }

        if (settingsOwner_ != kNone)
        {
            X11ErrorTrap trap(display);
            std::vector<unsigned char> data;
            int format = 0;
            const bool read = connection_.ReadProperty(settingsOwner_, settingsProperty_,
                                                       settingsProperty_, format, data);
            trap.Sync();
            if (read && format == 8)
            {
                if (const std::optional<float> scale = ScaleFromXsettings(data))
                {
                    return *scale;
                }
            }
        }

        if (const char* gdk = std::getenv("GDK_SCALE"))
        {
            if (const std::optional<float> scale = UsableScale(std::atoi(gdk)))
            {
                return *scale;
            }
        }
        return 1.0f;
    }

    bool X11ContentScale::HandleEvent(const XEvent& event)
    {
        bool reread = false;
        if (event.type == PropertyNotify)
        {
            reread = (event.xproperty.window == connection_.GetRoot() &&
                      event.xproperty.atom == resourceManager_) ||
                     (settingsOwner_ != kNone && event.xproperty.window == settingsOwner_ &&
                      event.xproperty.atom == settingsProperty_);
        }
        else if (event.type == DestroyNotify && settingsOwner_ != kNone &&
                 event.xdestroywindow.window == settingsOwner_)
        {
            FindSettingsManager();
            reread = true;
        }
        else if (event.type == ClientMessage && event.xclient.window == connection_.GetRoot() &&
                 event.xclient.message_type == manager_ &&
                 static_cast<Atom>(event.xclient.data.l[1]) == settingsSelection_)
        {
            // A settings manager announcing itself (ICCCM's MANAGER message).
            FindSettingsManager();
            reread = true;
        }
        if (!reread)
        {
            return false;
        }
        const float before = global_;
        global_ = Read();
        return global_ != before;
    }

} // namespace CNA::Platform::X11
