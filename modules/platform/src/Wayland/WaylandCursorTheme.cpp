// SPDX-License-Identifier: MS-PL

#include "WaylandCursorTheme.hpp"

#include <cstdlib>
#include <array>
#include <string>

#include <dlfcn.h>

#if defined(CNA_WAYLAND_HAVE_CURSOR_THEME)
#include <wayland-cursor.h>
#endif

namespace CNA::Platform::Wayland {

#if defined(CNA_WAYLAND_HAVE_CURSOR_THEME)

    namespace {

        struct CursorLibrary
        {
            bool loaded = false;
            wl_cursor_theme* (*load)(const char*, int, wl_shm*) = nullptr;
            void (*destroy)(wl_cursor_theme*) = nullptr;
            wl_cursor* (*getCursor)(wl_cursor_theme*, const char*) = nullptr;
            wl_buffer* (*getBuffer)(wl_cursor_image*) = nullptr;
        };

        const CursorLibrary& Library()
        {
            static const CursorLibrary library = [] {
                CursorLibrary result;
                // Never closed: buffers made through it outlive any one call.
                void* handle = dlopen("libwayland-cursor.so.0", RTLD_NOW | RTLD_LOCAL);
                if (handle == nullptr)
                {
                    return result;
                }
                result.load = reinterpret_cast<decltype(result.load)>(dlsym(handle, "wl_cursor_theme_load"));
                result.destroy = reinterpret_cast<decltype(result.destroy)>(dlsym(handle, "wl_cursor_theme_destroy"));
                result.getCursor =
                    reinterpret_cast<decltype(result.getCursor)>(dlsym(handle, "wl_cursor_theme_get_cursor"));
                result.getBuffer =
                    reinterpret_cast<decltype(result.getBuffer)>(dlsym(handle, "wl_cursor_image_get_buffer"));
                result.loaded = result.load != nullptr && result.destroy != nullptr && result.getCursor != nullptr &&
                                result.getBuffer != nullptr;
                return result;
            }();
            return library;
        }

        /// The names a shape goes by: the CSS name freedesktop cursor themes use today first, then
        /// the X core-font names older themes still ship. Unused slots are null.
        using CursorNames = std::array<const char*, 3>;

        CursorNames NamesOf(const SystemCursor cursor)
        {
            switch (cursor)
            {
                case SystemCursor::Arrow: return {"default", "left_ptr"};
                case SystemCursor::IBeam: return {"text", "xterm"};
                case SystemCursor::Wait: return {"wait", "watch"};
                case SystemCursor::Crosshair: return {"crosshair", "cross"};
                case SystemCursor::Move: return {"move", "fleur", "all-scroll"};
                case SystemCursor::NotAllowed: return {"not-allowed", "crossed_circle"};
                case SystemCursor::Pointer: return {"pointer", "hand2", "hand1"};
                case SystemCursor::Progress: return {"progress", "left_ptr_watch"};
                case SystemCursor::NwseResize: return {"nwse-resize", "size_fdiag", "bottom_right_corner"};
                case SystemCursor::NeswResize: return {"nesw-resize", "size_bdiag", "bottom_left_corner"};
                case SystemCursor::EwResize: return {"ew-resize", "sb_h_double_arrow", "size_hor"};
                case SystemCursor::NsResize: return {"ns-resize", "sb_v_double_arrow", "size_ver"};
            }
            return {"default"};
        }

    } // namespace

    std::unique_ptr<WaylandCursorTheme> WaylandCursorTheme::Load(wl_shm* shm)
    {
        const CursorLibrary& library = Library();
        if (!library.loaded || shm == nullptr)
        {
            return nullptr;
        }
        const char* name = std::getenv("XCURSOR_THEME");
        int size = 24;
        if (const char* configured = std::getenv("XCURSOR_SIZE"))
        {
            const int parsed = std::atoi(configured);
            if (parsed > 0 && parsed <= 512)
            {
                size = parsed;
            }
        }
        wl_cursor_theme* theme = library.load(name != nullptr && *name != '\0' ? name : nullptr, size, shm);
        if (theme == nullptr)
        {
            return nullptr;
        }
        std::unique_ptr<WaylandCursorTheme> result(new WaylandCursorTheme());
        result->theme_ = theme;
        // A theme with not even an arrow is no theme: libwayland-cursor "loads" an empty one when
        // no cursor directory exists at all.
        WaylandCursorImage probe;
        if (!result->Get(SystemCursor::Arrow, probe))
        {
            return nullptr;
        }
        return result;
    }

    WaylandCursorTheme::~WaylandCursorTheme()
    {
        if (theme_ != nullptr)
        {
            Library().destroy(static_cast<wl_cursor_theme*>(theme_));
        }
    }

    bool WaylandCursorTheme::Get(const SystemCursor cursor, WaylandCursorImage& image) const
    {
        const CursorLibrary& library = Library();
        for (const char* name : NamesOf(cursor))
        {
            if (name == nullptr)
            {
                break;
            }
            wl_cursor* found = library.getCursor(static_cast<wl_cursor_theme*>(theme_), name);
            if (found == nullptr || found->image_count == 0)
            {
                continue;
            }
            wl_cursor_image* first = found->images[0];
            wl_buffer* buffer = library.getBuffer(first);
            if (buffer == nullptr)
            {
                continue;
            }
            image.buffer = buffer;
            image.width = static_cast<int>(first->width);
            image.height = static_cast<int>(first->height);
            image.hotSpotX = static_cast<int>(first->hotspot_x);
            image.hotSpotY = static_cast<int>(first->hotspot_y);
            return true;
        }
        return false;
    }

#else

    std::unique_ptr<WaylandCursorTheme> WaylandCursorTheme::Load(wl_shm* shm)
    {
        (void) shm;
        return nullptr;
    }

    WaylandCursorTheme::~WaylandCursorTheme() = default;

    bool WaylandCursorTheme::Get(const SystemCursor cursor, WaylandCursorImage& image) const
    {
        (void) cursor;
        (void) image;
        return false;
    }

#endif

} // namespace CNA::Platform::Wayland
