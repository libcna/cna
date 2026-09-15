// SPDX-License-Identifier: MS-PL

#include "X11CoreFont.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Clipboard.hpp"
#include "X11Error.hpp"
#include "X11MessageBox.hpp"

#include <string>

namespace CNA::Platform::X11 {

    namespace {

        /// Unicode core fonts, best first: the classic 6x13 `fixed`, whose Unicode edition covers
        /// most scripts, then whatever Unicode font the server has.
        constexpr const char* kUnicodeFonts[] = {
            "-misc-fixed-medium-r-normal--13-*-*-*-c-70-iso10646-1",
            "-misc-fixed-medium-r-normal--13-*-*-*-*-*-iso10646-1",
            "-*-*-medium-r-normal--*-120-*-*-*-*-iso10646-1",
            "-*-*-*-*-*--*-*-*-*-*-*-iso10646-1",
        };

    } // namespace

    X11CoreFont::X11CoreFont(Display* display) : display_(display)
    {
        X11ErrorTrap trap(display_);
        for (const char* pattern : kUnicodeFonts)
        {
            font_ = XLoadQueryFont(display_, pattern);
            if (font_ != nullptr)
            {
                unicode_ = true;
                break;
            }
        }
        if (font_ == nullptr)
        {
            font_ = XLoadQueryFont(display_, "fixed");
        }
        trap.Sync();
        if (font_ == nullptr)
        {
            throw PlatformException("X11CoreFont", "the X server has no font to draw text with");
        }
    }

    X11CoreFont::~X11CoreFont()
    {
        XFreeFont(display_, font_);
    }

    std::vector<XChar2b> X11CoreFont::Characters(const std::string_view text)
    {
        std::vector<XChar2b> characters;
        for (const char16_t unit : DecodeMessageBoxText(text))
        {
            characters.push_back(XChar2b{static_cast<unsigned char>(unit >> 8), static_cast<unsigned char>(unit & 0xFFu)});
        }
        return characters;
    }

    int X11CoreFont::Measure(const std::string_view text) const
    {
        if (unicode_)
        {
            const std::vector<XChar2b> characters = Characters(text);
            return XTextWidth16(font_, characters.data(), static_cast<int>(characters.size()));
        }
        const std::string latin1 = Utf8ToLatin1(std::string(text));
        return XTextWidth(font_, latin1.data(), static_cast<int>(latin1.size()));
    }

    void X11CoreFont::Draw(const Drawable drawable, GC gc, const int x, const int y, const std::string_view text) const
    {
        XSetFont(display_, gc, font_->fid);
        if (unicode_)
        {
            const std::vector<XChar2b> characters = Characters(text);
            XDrawString16(display_, drawable, gc, x, y, characters.data(), static_cast<int>(characters.size()));
            return;
        }
        const std::string latin1 = Utf8ToLatin1(std::string(text));
        XDrawString(display_, drawable, gc, x, y, latin1.data(), static_cast<int>(latin1.size()));
    }

} // namespace CNA::Platform::X11
