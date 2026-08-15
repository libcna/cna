// SPDX-License-Identifier: MS-PL
#pragma once

// Shared test scaffolding: a platform whose dialog service records instead of showing.
//
// A real message box blocks the calling thread until a human closes it, and a real file dialog
// waits for one too. Either would hang an automated run indefinitely -- an incident this project
// has already had once, during FileDialog's own development. So no test may ever reach the real
// service, and this is what they reach instead.
//
// It replaces the per-surface `SetBackendForTesting` hooks that used to live on the public
// `MessageBox` and `FileDialog` classes. Those existed only so tests could avoid the real dialog;
// with the platform contract there is a seam for that already, and a test-only hook on a public
// API is a worse place for it.

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <string>
#include <utility>
#include <vector>

namespace CNA::Platform::Testing {

    /** @brief A dialog service that records what it was asked to show and never shows anything. */
    class CannedDialogs final : public IPlatformDialogs
    {
    public:
        /** @brief One recorded message box. */
        struct MessageBoxCall
        {
            /** @brief How prominently it would have been presented. */
            MessageBoxSeverity severity = MessageBoxSeverity::Information;
            /** @brief The title it was given. */
            std::string title;
            /** @brief The body it was given. */
            std::string message;
            /** @brief The button labels, empty for the single-button form. */
            std::vector<std::string> buttons;
        };

        /** @brief One recorded file dialog. */
        struct FileDialogCall
        {
            /** @brief Which dialog: `"open"`, `"save"` or `"folder"`. */
            std::string kind;
            /** @brief The filters it was given. */
            std::vector<FileDialogFilter> filters;
            /** @brief The starting location it was given. */
            std::string defaultLocation;
            /** @brief Whether multiple selection was requested. */
            bool allowMultiple = false;
        };

        /** @brief Every message box shown, in order. */
        std::vector<MessageBoxCall> messageBoxes;
        /** @brief Every file dialog shown, in order. */
        std::vector<FileDialogCall> fileDialogs;

        /** @brief What `ShowMessageBoxWithButtons` reports the user chose. -1 means dismissed. */
        int chosenButton = 0;
        /** @brief What every file dialog's callback receives. Empty means the user cancelled. */
        std::vector<std::string> chosenPaths;

        /**
         * @brief Records a message box.
         * @param severity How prominently it would be presented.
         * @param title The title.
         * @param message The body.
         * @param parent Ignored; there is no window to parent to.
         */
        void ShowMessageBox(const MessageBoxSeverity severity, const std::string& title,
                            const std::string& message, IPlatformWindow* /*parent*/) override
        {
            messageBoxes.push_back(MessageBoxCall{severity, title, message, {}});
        }

        /**
         * @brief Records a message box and reports the scripted choice.
         * @param severity How prominently it would be presented.
         * @param title The title.
         * @param message The body.
         * @param buttons The button labels.
         * @param parent Ignored.
         * @return The scripted button index.
         */
        [[nodiscard]] int ShowMessageBoxWithButtons(const MessageBoxSeverity severity,
                                                    const std::string& title,
                                                    const std::string& message,
                                                    const std::vector<std::string>& buttons,
                                                    IPlatformWindow* /*parent*/) override
        {
            messageBoxes.push_back(MessageBoxCall{severity, title, message, buttons});
            return chosenButton;
        }

        /**
         * @brief Records an open dialog and delivers the scripted result.
         *
         * The callback fires before this returns, which a real platform's would not. That is a
         * deliberate simplification: a test that had to pump an event loop to see its own
         * callback would be testing the loop.
         *
         * @param onResult Receives the scripted paths.
         * @param filters The filters.
         * @param defaultLocation The starting location.
         * @param allowMultiple Whether multiple selection was requested.
         * @param parent Ignored.
         */
        void ShowOpenFileDialog(FileDialogCallback onResult,
                                const std::vector<FileDialogFilter>& filters,
                                const std::string& defaultLocation, const bool allowMultiple,
                                IPlatformWindow* /*parent*/) override
        {
            Record("open", filters, defaultLocation, allowMultiple, std::move(onResult));
        }

        /**
         * @brief Records a save dialog and delivers the scripted result.
         * @param onResult Receives the scripted paths.
         * @param filters The filters.
         * @param defaultLocation The starting location.
         * @param parent Ignored.
         */
        void ShowSaveFileDialog(FileDialogCallback onResult,
                                const std::vector<FileDialogFilter>& filters,
                                const std::string& defaultLocation,
                                IPlatformWindow* /*parent*/) override
        {
            Record("save", filters, defaultLocation, false, std::move(onResult));
        }

        /**
         * @brief Records a folder dialog and delivers the scripted result.
         * @param onResult Receives the scripted paths.
         * @param defaultLocation The starting location.
         * @param allowMultiple Whether multiple selection was requested.
         * @param parent Ignored.
         */
        void ShowOpenFolderDialog(FileDialogCallback onResult, const std::string& defaultLocation,
                                  const bool allowMultiple, IPlatformWindow* /*parent*/) override
        {
            Record("folder", {}, defaultLocation, allowMultiple, std::move(onResult));
        }

    private:
        void Record(const char* kind, const std::vector<FileDialogFilter>& filters,
                    const std::string& defaultLocation, const bool allowMultiple,
                    FileDialogCallback onResult)
        {
            fileDialogs.push_back(FileDialogCall{kind, filters, defaultLocation, allowMultiple});

            // Rejected here for the same reason the SDL3 service rejects it: a dialog whose
            // answer has nowhere to go would interrupt the user for nothing.
            if (!onResult)
            {
                throw PlatformException(kind, "no result callback was supplied");
            }
            onResult(chosenPaths);
        }
    };

    /** @brief A platform that is real in every respect except its dialogs. */
    class CannedDialogPlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Gets the recording dialog service. @return The service; never null. */
        [[nodiscard]] IPlatformDialogs* GetDialogs() override { return &dialogs_; }

        /** @brief Gets the recording service for inspection. @return The recorded calls. */
        [[nodiscard]] CannedDialogs& Canned() { return dialogs_; }

    private:
        CannedDialogs dialogs_;
    };

} // namespace CNA::Platform::Testing
