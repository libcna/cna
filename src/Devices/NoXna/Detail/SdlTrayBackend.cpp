// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Detail/SdlTrayBackend.hpp"

#ifdef CNA_DEVICES

#include <SDL3/SDL_tray.h>

namespace
{
    void EntryClickTrampoline(void* userdata, SDL_TrayEntry* /*entry*/)
    {
        auto* callback = static_cast<CNA::Devices::Detail::TrayEntryClickCallback*>(userdata);
        if (callback != nullptr && *callback)
        {
            (*callback)();
        }
    }
} // namespace

namespace CNA::Devices::Detail
{
    SdlTrayBackend::SdlTrayBackend() = default;

    SdlTrayBackend::~SdlTrayBackend()
    {
        Destroy();
    }

    void SdlTrayBackend::Create(const std::string& tooltip)
    {
        if (tray_ != nullptr)
        {
            return;
        }

        tray_ = SDL_CreateTray(nullptr, tooltip.empty() ? nullptr : tooltip.c_str());
        if (tray_ != nullptr)
        {
            menu_ = SDL_CreateTrayMenu(tray_);
        }
    }

    void SdlTrayBackend::Destroy()
    {
        if (tray_ != nullptr)
        {
            SDL_DestroyTray(tray_);
            tray_ = nullptr;
            menu_ = nullptr;
        }
        entries_.clear();
        entryCallbacks_.clear();
    }

    void SdlTrayBackend::SetTooltip(const std::string& tooltip)
    {
        if (tray_ != nullptr)
        {
            SDL_SetTrayTooltip(tray_, tooltip.empty() ? nullptr : tooltip.c_str());
        }
    }

    std::size_t SdlTrayBackend::AddEntry(
        const std::string& label,
        bool checkable,
        bool initiallyChecked,
        bool initiallyEnabled,
        TrayEntryClickCallback onClick)
    {
        auto callback = std::make_unique<TrayEntryClickCallback>(std::move(onClick));
        SDL_TrayEntry* entry = nullptr;

        if (menu_ != nullptr)
        {
            SDL_TrayEntryFlags flags = checkable ? SDL_TRAYENTRY_CHECKBOX : SDL_TRAYENTRY_BUTTON;
            if (!initiallyEnabled)
            {
                flags |= SDL_TRAYENTRY_DISABLED;
            }
            if (checkable && initiallyChecked)
            {
                flags |= SDL_TRAYENTRY_CHECKED;
            }

            entry = SDL_InsertTrayEntryAt(menu_, -1, label.c_str(), flags);
            if (entry != nullptr)
            {
                SDL_SetTrayEntryCallback(entry, &EntryClickTrampoline, callback.get());
            }
        }

        entries_.push_back(entry);
        entryCallbacks_.push_back(std::move(callback));
        return entries_.size() - 1;
    }

    void SdlTrayBackend::SetEntryLabel(std::size_t index, const std::string& label)
    {
        if (index < entries_.size() && entries_[index] != nullptr)
        {
            SDL_SetTrayEntryLabel(entries_[index], label.c_str());
        }
    }

    void SdlTrayBackend::SetEntryChecked(std::size_t index, bool checked)
    {
        if (index < entries_.size() && entries_[index] != nullptr)
        {
            SDL_SetTrayEntryChecked(entries_[index], checked);
        }
    }

    bool SdlTrayBackend::GetEntryChecked(std::size_t index) const
    {
        if (index < entries_.size() && entries_[index] != nullptr)
        {
            return SDL_GetTrayEntryChecked(entries_[index]);
        }
        return false;
    }

    void SdlTrayBackend::SetEntryEnabled(std::size_t index, bool enabled)
    {
        if (index < entries_.size() && entries_[index] != nullptr)
        {
            SDL_SetTrayEntryEnabled(entries_[index], enabled);
        }
    }

    bool SdlTrayBackend::GetEntryEnabled(std::size_t index) const
    {
        if (index < entries_.size() && entries_[index] != nullptr)
        {
            return SDL_GetTrayEntryEnabled(entries_[index]);
        }
        return false;
    }
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
