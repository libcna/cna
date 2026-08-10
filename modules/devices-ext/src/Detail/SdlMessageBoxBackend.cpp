// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Detail/SdlMessageBoxBackend.hpp"

#ifdef CNA_DEVICES

#include <vector>

#include <SDL3/SDL_messagebox.h>

namespace
{
    Uint32 ToSdlFlags(CNA::Devices::MessageBoxType type)
    {
        switch (type)
        {
        case CNA::Devices::MessageBoxType::Error:
            return SDL_MESSAGEBOX_ERROR;
        case CNA::Devices::MessageBoxType::Warning:
            return SDL_MESSAGEBOX_WARNING;
        case CNA::Devices::MessageBoxType::Information:
        default:
            return SDL_MESSAGEBOX_INFORMATION;
        }
    }
} // namespace

namespace CNA::Devices::Detail
{
    void SdlMessageBoxBackend::ShowSimple(MessageBoxType type, const std::string& title, const std::string& message)
    {
        SDL_ShowSimpleMessageBox(ToSdlFlags(type), title.c_str(), message.c_str(), nullptr);
    }

    int SdlMessageBoxBackend::Show(
        MessageBoxType type,
        const std::string& title,
        const std::string& message,
        const std::vector<std::string>& buttonLabels)
    {
        std::vector<SDL_MessageBoxButtonData> sdlButtons;
        sdlButtons.reserve(buttonLabels.size());
        for (std::size_t i = 0; i < buttonLabels.size(); ++i)
        {
            SDL_MessageBoxButtonData button{};
            button.flags = 0;
            button.buttonID = static_cast<int>(i);
            button.text = buttonLabels[i].c_str();
            sdlButtons.push_back(button);
        }

        SDL_MessageBoxData data{};
        data.flags = ToSdlFlags(type);
        data.window = nullptr;
        data.title = title.c_str();
        data.message = message.c_str();
        data.numbuttons = static_cast<int>(sdlButtons.size());
        data.buttons = sdlButtons.empty() ? nullptr : sdlButtons.data();
        data.colorScheme = nullptr;

        int buttonId = -1;
        SDL_ShowMessageBox(&data, &buttonId);
        return buttonId;
    }
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
