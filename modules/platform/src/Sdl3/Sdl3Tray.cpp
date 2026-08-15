// SPDX-License-Identifier: MS-PL

#include "Sdl3Tray.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <utility>
#include <vector>

namespace CNA::Platform::Sdl3 {

    namespace {

        class Sdl3TrayIcon final : public IPlatformTrayIcon
        {
        public:
            explicit Sdl3TrayIcon(const std::string& tooltip)
            {
                tray_ = SDL_CreateTray(nullptr, tooltip.empty() ? nullptr : tooltip.c_str());
                if (tray_ == nullptr)
                {
                    throw PlatformException("CreateTray", SDL_GetError());
                }

                menu_ = SDL_CreateTrayMenu(tray_);
                if (menu_ == nullptr)
                {
                    const std::string error = SDL_GetError();
                    SDL_DestroyTray(tray_);
                    tray_ = nullptr;
                    throw PlatformException("CreateTrayMenu", error);
                }
            }

            ~Sdl3TrayIcon() override
            {
                // SDL removes every entry with the tray. Callback storage stays alive until that
                // operation has completed, then the member vectors are destroyed normally.
                SDL_DestroyTray(tray_);
            }

            void SetTooltip(const std::string& tooltip) override
            {
                SDL_SetTrayTooltip(tray_, tooltip.empty() ? nullptr : tooltip.c_str());
            }

            [[nodiscard]] std::size_t AddEntry(
                const std::string& label, const bool checkable, const bool initiallyChecked,
                const bool initiallyEnabled, TrayEntryClickCallback onClick) override
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

                // Complete both potentially-throwing allocations before SDL owns an entry whose
                // callback points into our storage. The following pointer/unique_ptr pushes are
                // then noexcept and cannot leave a native ghost entry after a C++ allocation
                // failure.
                entries_.reserve(entries_.size() + 1);
                callbacks_.reserve(callbacks_.size() + 1);
                auto callback = std::make_unique<TrayEntryClickCallback>(std::move(onClick));

                SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(menu_, -1, label.c_str(), flags);
                if (entry == nullptr)
                {
                    throw PlatformException("AddTrayEntry", SDL_GetError());
                }

                SDL_SetTrayEntryCallback(entry, &EntryClickTrampoline, callback.get());
                entries_.push_back(entry);
                callbacks_.push_back(std::move(callback));
                return entries_.size() - 1;
            }

            void SetEntryLabel(const std::size_t index, const std::string& label) override
            {
                if (index < entries_.size())
                {
                    SDL_SetTrayEntryLabel(entries_[index], label.c_str());
                }
            }

            void SetEntryChecked(const std::size_t index, const bool checked) override
            {
                if (index < entries_.size())
                {
                    SDL_SetTrayEntryChecked(entries_[index], checked);
                }
            }

            [[nodiscard]] bool GetEntryChecked(const std::size_t index) const override
            {
                return index < entries_.size() && SDL_GetTrayEntryChecked(entries_[index]);
            }

            void SetEntryEnabled(const std::size_t index, const bool enabled) override
            {
                if (index < entries_.size())
                {
                    SDL_SetTrayEntryEnabled(entries_[index], enabled);
                }
            }

            [[nodiscard]] bool GetEntryEnabled(const std::size_t index) const override
            {
                return index < entries_.size() && SDL_GetTrayEntryEnabled(entries_[index]);
            }

        private:
            static void EntryClickTrampoline(void* userdata, SDL_TrayEntry*)
            {
                auto* callback = static_cast<TrayEntryClickCallback*>(userdata);
                if (callback != nullptr && *callback)
                {
                    (*callback)();
                }
            }

            SDL_Tray* tray_ = nullptr;
            SDL_TrayMenu* menu_ = nullptr;
            std::vector<SDL_TrayEntry*> entries_;
            std::vector<std::unique_ptr<TrayEntryClickCallback>> callbacks_;
        };

    } // namespace

    bool Sdl3Tray::IsSupported()
    {
#if defined(SDL_PLATFORM_WINDOWS) || defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_MACOS)
        return true;
#else
        return false;
#endif
    }

    std::unique_ptr<IPlatformTrayIcon> Sdl3Tray::CreateTray(const std::string& tooltip)
    {
        if (!IsSupported())
        {
            throw PlatformNotSupportedException(PlatformCapability::Tray, "SDL3");
        }
        return std::make_unique<Sdl3TrayIcon>(tooltip);
    }

} // namespace CNA::Platform::Sdl3
