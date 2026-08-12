// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <string>
#include <vector>

#include "CNA/Devices/Detail/FileDialogResultCallback.hpp"
#include "CNA/Devices/FileDialogFilter.hpp"

namespace CNA::Devices
{
    /**
     * @brief Shows native open-file/save-file/open-folder dialogs.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Calls straight through to the platform's
     * dialog service. Every dialog call is **asynchronous**: it returns immediately, and the
     * result arrives later via `onResult`, possibly on a different thread than the one the call
     * was made from.
     *
     * A test must never reach a real dialog — it launches an interactive native window (a real
     * `zenity` process on Linux) that waits for a human forever, which happened once during this
     * class's own development and left orphaned processes on a real desktop session. Tests
     * therefore install a platform whose dialog service records instead of showing
     * (`CNA::Platform::Testing::CannedDialogPlatform`); there is deliberately no test-only
     * injection hook on this class.
     *
     * @note Real native backends exist for Windows, Linux (via XDG portal/zenity),
     * macOS, and **Android** (confirmed by reading
     * `third_party/SDL/src/dialog/{windows,unix,cocoa,android}/` directly, not
     * assumed) — this is *not* a desktop-only capability, contrary to an earlier,
     * uncorroborated assumption in `noxna_devices.md`'s original analysis. No backend
     * exists for iOS or Web/Emscripten (confirmed by their absence from
     * `third_party/SDL/src/dialog/` and from `third_party/SDL/CMakeLists.txt`'s own
     * per-platform dialog-source selection) — `getIsSupportedProperty()` reports this
     * honestly per platform.
     *
     * @note Every dialog shown by this class is currently non-modal (SDL3's own
     * `window` parameter, for making a dialog modal to a specific game window, is
     * always passed as `nullptr` here) — a future task could add an optional
     * `GameWindow` parameter if modal dialogs become a real need.
     *
     * @note The result callback receives an empty file list both when the user
     * canceled the dialog and when an error occurred — SDL3 itself distinguishes
     * these (a null vs. a non-null-but-empty list), but this simplified wrapper does
     * not currently surface that distinction.
     */
    class FileDialog
    {
    public:
        /** @brief See `Detail::FileDialogResultCallback` for the full contract. */
        using ResultCallback = Detail::FileDialogResultCallback;

        /**
         * @brief Gets whether native file dialogs are supported on the current
         * platform.
         *
         * @return true on Desktop (Windows/Linux/macOS) and Android; false on iOS and
         * Web/Emscripten, where no native dialog backend exists.
         */
        [[nodiscard]] static bool getIsSupportedProperty();

        /**
         * @brief Shows a dialog that lets the user select one or more existing files.
         *
         * @param onResult Callback invoked when the dialog closes.
         * @param filters File-type filters to offer; empty means no filtering.
         * @param defaultLocation The folder or file to start the dialog at; empty
         * means platform-default.
         * @param allowMultiple true to let the user select more than one file.
         */
        static void ShowOpenFile(
            ResultCallback onResult,
            const std::vector<FileDialogFilter>& filters = {},
            const std::string& defaultLocation = "",
            bool allowMultiple = false);

        /**
         * @brief Shows a dialog that lets the user choose a new or existing file to
         * save to.
         *
         * @param onResult Callback invoked when the dialog closes.
         * @param filters File-type filters to offer; empty means no filtering.
         * @param defaultLocation The folder or file to start the dialog at; empty
         * means platform-default.
         */
        static void ShowSaveFile(
            ResultCallback onResult,
            const std::vector<FileDialogFilter>& filters = {},
            const std::string& defaultLocation = "");

        /**
         * @brief Shows a dialog that lets the user select one or more folders.
         *
         * @param onResult Callback invoked when the dialog closes.
         * @param defaultLocation The folder to start the dialog at; empty means
         * platform-default.
         * @param allowMultiple true to let the user select more than one folder.
         */
        static void ShowOpenFolder(
            ResultCallback onResult,
            const std::string& defaultLocation = "",
            bool allowMultiple = false);


    private:
        /** @brief Not instantiable — every member is static. */
        FileDialog() = delete;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
