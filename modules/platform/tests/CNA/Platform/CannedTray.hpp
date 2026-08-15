// SPDX-License-Identifier: MS-PL
#pragma once

// Shared test scaffolding: a platform whose tray service records instead of creating desktop UI.

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Platform::Testing {

    /** @brief Observable state retained after a canned tray icon is destroyed. */
    struct CannedTrayState
    {
        /** @brief One recorded flat-menu entry. */
        struct Entry
        {
            /** @brief The current label. */
            std::string label;
            /** @brief Whether the entry exposes a checked state. */
            bool checkable = false;
            /** @brief The current checked state. */
            bool checked = false;
            /** @brief Whether the entry accepts activation. */
            bool enabled = true;
            /** @brief The callback to invoke when scripted as clicked. */
            TrayEntryClickCallback onClick;
        };

        /** @brief The icon's current tooltip. */
        std::string tooltip;
        /** @brief The entries in insertion order. */
        std::vector<Entry> entries;
        /** @brief Whether the owning icon interface has been destroyed. */
        bool destroyed = false;
    };

    /** @brief One in-memory tray icon backed by externally observable state. */
    class CannedTrayIcon final : public IPlatformTrayIcon
    {
    public:
        /** @brief Wraps retained state. @param state The state to mutate. */
        explicit CannedTrayIcon(std::shared_ptr<CannedTrayState> state)
            : state_(std::move(state))
        {
        }

        /** @brief Marks the retained state as destroyed. */
        ~CannedTrayIcon() override { state_->destroyed = true; }

        /** @brief Records a tooltip change. @param tooltip The new text. */
        void SetTooltip(const std::string& tooltip) override { state_->tooltip = tooltip; }

        /**
         * @brief Records a new entry.
         * @param label Its label.
         * @param checkable Whether it is checkable.
         * @param initiallyChecked Its initial checked state.
         * @param initiallyEnabled Its initial enabled state.
         * @param onClick Its activation callback.
         * @return Its insertion index.
         */
        [[nodiscard]] std::size_t AddEntry(
            const std::string& label, const bool checkable, const bool initiallyChecked,
            const bool initiallyEnabled, TrayEntryClickCallback onClick) override
        {
            state_->entries.push_back(CannedTrayState::Entry{
                label, checkable, initiallyChecked, initiallyEnabled, std::move(onClick)});
            return state_->entries.size() - 1;
        }

        /** @brief Records a label change. @param index The entry index. @param label The text. */
        void SetEntryLabel(const std::size_t index, const std::string& label) override
        {
            if (index < state_->entries.size())
            {
                state_->entries[index].label = label;
            }
        }

        /** @brief Records a checked-state change. @param index The entry. @param checked The state. */
        void SetEntryChecked(const std::size_t index, const bool checked) override
        {
            if (index < state_->entries.size())
            {
                state_->entries[index].checked = checked;
            }
        }

        /** @brief Reads the checked state. @param index The entry. @return False when unknown. */
        [[nodiscard]] bool GetEntryChecked(const std::size_t index) const override
        {
            return index < state_->entries.size() && state_->entries[index].checked;
        }

        /** @brief Records an enabled-state change. @param index The entry. @param enabled The state. */
        void SetEntryEnabled(const std::size_t index, const bool enabled) override
        {
            if (index < state_->entries.size())
            {
                state_->entries[index].enabled = enabled;
            }
        }

        /** @brief Reads the enabled state. @param index The entry. @return False when unknown. */
        [[nodiscard]] bool GetEntryEnabled(const std::size_t index) const override
        {
            return index < state_->entries.size() && state_->entries[index].enabled;
        }

    private:
        std::shared_ptr<CannedTrayState> state_;
    };

    /** @brief A tray service that creates in-memory icons and retains their observable states. */
    class CannedTray final : public IPlatformTray
    {
    public:
        /** @brief Every icon created, retained in creation order. */
        std::vector<std::shared_ptr<CannedTrayState>> icons;

        /**
         * @brief Creates an in-memory icon.
         * @param tooltip The initial tooltip.
         * @return The icon interface.
         */
        [[nodiscard]] std::unique_ptr<IPlatformTrayIcon> CreateTray(
            const std::string& tooltip) override
        {
            auto state = std::make_shared<CannedTrayState>();
            state->tooltip = tooltip;
            icons.push_back(state);
            return std::make_unique<CannedTrayIcon>(std::move(state));
        }
    };

    /** @brief A platform that is real in every respect except its tray icons. */
    class CannedTrayPlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Reports tray support in addition to the inner platform's capabilities. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override
        {
            PlatformCapabilities capabilities = PlatformTestDecorator::GetCapabilities();
            capabilities.tray = true;
            return capabilities;
        }

        /** @brief Gets the recording tray service. @return The service; never null. */
        [[nodiscard]] IPlatformTray* GetTray() override { return &tray_; }

        /** @brief Gets the recording service for inspection. @return The recorded icons. */
        [[nodiscard]] CannedTray& Canned() { return tray_; }

    private:
        CannedTray tray_;
    };

} // namespace CNA::Platform::Testing
