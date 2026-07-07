// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <memory>
#include <vector>

#include "CNA/Devices/Detail/ITrayBackend.hpp"

struct SDL_Tray;
struct SDL_TrayMenu;
struct SDL_TrayEntry;

namespace CNA::Devices::Detail
{
    /**
     * @brief Real `ITrayBackend` implementation calling SDL3's
     * `SDL_CreateTray()`/`SDL_CreateTrayMenu()`/`SDL_InsertTrayEntryAt()`/etc.
     * directly. The default backend `SystemTray` uses outside of tests.
     */
    class SdlTrayBackend final : public ITrayBackend
    {
    public:
        SdlTrayBackend();
        ~SdlTrayBackend() override;

        SdlTrayBackend(const SdlTrayBackend&) = delete;
        SdlTrayBackend& operator=(const SdlTrayBackend&) = delete;

        void Create(const std::string& tooltip) override;
        void Destroy() override;
        void SetTooltip(const std::string& tooltip) override;

        std::size_t AddEntry(
            const std::string& label,
            bool checkable,
            bool initiallyChecked,
            bool initiallyEnabled,
            TrayEntryClickCallback onClick) override;

        void SetEntryLabel(std::size_t index, const std::string& label) override;
        void SetEntryChecked(std::size_t index, bool checked) override;
        [[nodiscard]] bool GetEntryChecked(std::size_t index) const override;
        void SetEntryEnabled(std::size_t index, bool enabled) override;
        [[nodiscard]] bool GetEntryEnabled(std::size_t index) const override;

    private:
        SDL_Tray* tray_ = nullptr;
        SDL_TrayMenu* menu_ = nullptr;
        std::vector<SDL_TrayEntry*> entries_;

        // Each entry's click callback must stay alive for as long as the entry
        // itself does (SDL_SetTrayEntryCallback() can fire any number of times, not
        // just once, unlike FileDialog's one-shot result callback) -- owned here,
        // one heap allocation per entry, freed in Destroy().
        std::vector<std::unique_ptr<TrayEntryClickCallback>> entryCallbacks_;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
