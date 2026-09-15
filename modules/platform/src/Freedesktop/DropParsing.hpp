// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Platform::Freedesktop {

    /** @brief How the data of the type a drop is read as turns into text. */
    enum class DropEncoding
    {
        /** @brief `text/uri-list`: one URI per line, files among them. */
        UriList,
        /** @brief UTF-8 text. */
        Utf8,
        /** @brief ISO 8859-1 text (X's `STRING`, by the ICCCM). */
        Latin1,
    };

    /** @brief The type chosen to read a drop as. */
    struct DropTarget
    {
        /** @brief Its position in the list the source offered. */
        std::size_t index = 0;
        /** @brief How its data decodes. */
        DropEncoding encoding = DropEncoding::Utf8;
    };

    /**
     * @brief Chooses the type to read a drop as, from the MIME types the source offers.
     *
     * `text/uri-list` first, so a file manager's drag is files rather than their names as text;
     * then UTF-8 text (`text/plain;charset=utf-8`, `UTF8_STRING`); then `text/plain`, `TEXT` and
     * `STRING`. SDL3's preference, extended by `STRING`. Shared by the X11 (XDND) and Wayland
     * (`wl_data_device`) drop targets (plans/plan_wayland.md WAYLAND-0017): the types are MIME
     * names on both.
     *
     * @param offered The source's types, by name; an empty name is a type the backend does not
     * know and never chooses.
     * @return The choice, or nothing when the window can take none of them.
     */
    [[nodiscard]] std::optional<DropTarget> ChooseDropTarget(const std::vector<std::string>& offered);

    /**
     * @brief Turns a `file:` URI into a local path.
     *
     * Accepts `file:///path`, `file://localhost/path`, `file://<this host>/path` and the older
     * `file:/path`, percent-decoded. A file on another host, a malformed escape or an embedded NUL
     * is not a local path.
     *
     * @param uri The URI.
     * @param hostname This machine's host name.
     * @return The path, or nothing when the URI does not name a local file.
     */
    [[nodiscard]] std::optional<std::string> FileUriToPath(std::string_view uri, std::string_view hostname);

    /** @brief One item of a drop. */
    struct DroppedItem
    {
        /** @brief True for a local file, false for text. */
        bool file = false;
        /** @brief The path, or the text. */
        std::string value;
    };

    /**
     * @brief Splits a `text/uri-list` into what it names.
     *
     * One URI per line (CRLF by RFC 2483, a bare LF accepted), comment lines skipped. A local file
     * becomes a path; any other URI -- a link dragged out of a browser -- is text, the URI itself,
     * rather than nothing (SDL3 drops those silently).
     *
     * @param list The list.
     * @param hostname This machine's host name.
     * @return The items, in order.
     */
    [[nodiscard]] std::vector<DroppedItem> ParseUriList(std::string_view list, std::string_view hostname);

    /**
     * @brief A drop's bytes as UTF-8 text. Sources commonly append a NUL; a claimed-UTF-8 source
     * that sends something else is read as Latin-1 rather than passed on invalid.
     *
     * @param data The bytes.
     * @param encoding How they were offered.
     * @return The text.
     */
    [[nodiscard]] std::string DecodeDropText(const std::vector<unsigned char>& data, DropEncoding encoding);

    /** @brief Gets this machine's host name, for FileUriToPath. @return The name, or empty. */
    [[nodiscard]] std::string LocalHostName();

} // namespace CNA::Platform::Freedesktop
