// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Internal/Input/SdlJoystickBackend.hpp"

// Test-only fake implementation of the internal SDL raw-joystick seam. Lets joystick runtime
// behavior (hot-plug, capabilities, raw axis/button/hat/ball state) be exercised with NO real
// hardware. The opaque SDL_Joystick* handle is just a reinterpret-cast FakeDevice pointer that is
// only ever handed back to this fake. NOT compiled into production.
namespace CNA::Internal::Input::test_support
{
    // Per-device fake description; register one per synthetic joystick instance id before
    // delivering an SDL_EVENT_JOYSTICK_ADDED for that id.
    struct FakeJoystickConfig
    {
        std::string name;                              // SDL_GetJoystickName
        SDL_JoystickType type = SDL_JOYSTICK_TYPE_UNKNOWN;
        std::string guid;                              // SDL_GetJoystickGUID (pre-formatted string)
        std::vector<Sint16> axes;                       // one raw value per axis
        std::vector<bool> buttons;                      // one down/up state per button
        std::vector<Uint8> hats;                        // one SDL_HAT_* value per hat
        std::vector<std::pair<int, int>> balls;         // one (dx, dy) per trackball
        SDL_PowerState powerState = SDL_POWERSTATE_UNKNOWN;
        int powerPercent = -1;
        bool openFails = false;                         // simulate SDL_OpenJoystick returning nullptr
    };

    class FakeSdlJoystickBackend final : public ISdlJoystickBackend
    {
    public:
        // --- test setup / introspection ---
        void Register(SDL_JoystickID id, FakeJoystickConfig cfg) { registered_[id] = std::move(cfg); }

        int openCount = 0;             // total OpenJoystick calls
        int closeCount = 0;            // total CloseJoystick calls
        SDL_JoystickID lastClosedId = 0;

        // --- ISdlJoystickBackend ---
        SDL_Joystick* OpenJoystick(SDL_JoystickID id) override
        {
            const auto it = registered_.find(id);
            if (it == registered_.end() || it->second.openFails)
                return nullptr;
            auto device = std::make_unique<FakeDevice>();
            device->id = id;
            device->cfg = it->second;
            ++openCount;
            FakeDevice* raw = device.get();
            devices_.push_back(std::move(device));
            return reinterpret_cast<SDL_Joystick*>(raw);
        }

        void CloseJoystick(SDL_Joystick* joystick) override
        {
            if (FakeDevice* d = dev(joystick))
            {
                d->closed = true;
                ++closeCount;
                lastClosedId = d->id;
            }
        }

        std::string GetJoystickName(SDL_Joystick* joystick) override
        {
            FakeDevice* d = dev(joystick);
            return d ? d->cfg.name : "";
        }

        SDL_JoystickType GetJoystickType(SDL_Joystick* joystick) override
        {
            FakeDevice* d = dev(joystick);
            return d ? d->cfg.type : SDL_JOYSTICK_TYPE_UNKNOWN;
        }

        std::string GetJoystickGUID(SDL_Joystick* joystick) override
        {
            FakeDevice* d = dev(joystick);
            return d ? d->cfg.guid : "";
        }

        int GetNumJoystickAxes(SDL_Joystick* joystick) override
        {
            FakeDevice* d = dev(joystick);
            return d ? static_cast<int>(d->cfg.axes.size()) : 0;
        }

        int GetNumJoystickButtons(SDL_Joystick* joystick) override
        {
            FakeDevice* d = dev(joystick);
            return d ? static_cast<int>(d->cfg.buttons.size()) : 0;
        }

        int GetNumJoystickHats(SDL_Joystick* joystick) override
        {
            FakeDevice* d = dev(joystick);
            return d ? static_cast<int>(d->cfg.hats.size()) : 0;
        }

        int GetNumJoystickBalls(SDL_Joystick* joystick) override
        {
            FakeDevice* d = dev(joystick);
            return d ? static_cast<int>(d->cfg.balls.size()) : 0;
        }

        Sint16 GetJoystickAxis(SDL_Joystick* joystick, int axis) override
        {
            FakeDevice* d = dev(joystick);
            if (d == nullptr || axis < 0 || axis >= static_cast<int>(d->cfg.axes.size()))
                return 0;
            return d->cfg.axes[static_cast<std::size_t>(axis)];
        }

        bool GetJoystickButton(SDL_Joystick* joystick, int button) override
        {
            FakeDevice* d = dev(joystick);
            if (d == nullptr || button < 0 || button >= static_cast<int>(d->cfg.buttons.size()))
                return false;
            return d->cfg.buttons[static_cast<std::size_t>(button)];
        }

        Uint8 GetJoystickHat(SDL_Joystick* joystick, int hat) override
        {
            FakeDevice* d = dev(joystick);
            if (d == nullptr || hat < 0 || hat >= static_cast<int>(d->cfg.hats.size()))
                return SDL_HAT_CENTERED;
            return d->cfg.hats[static_cast<std::size_t>(hat)];
        }

        bool GetJoystickBall(SDL_Joystick* joystick, int ball, int* dx, int* dy) override
        {
            FakeDevice* d = dev(joystick);
            if (d == nullptr || ball < 0 || ball >= static_cast<int>(d->cfg.balls.size()))
                return false;
            const auto& motion = d->cfg.balls[static_cast<std::size_t>(ball)];
            if (dx != nullptr) *dx = motion.first;
            if (dy != nullptr) *dy = motion.second;
            return true;
        }

        SDL_PowerState GetJoystickPowerInfo(SDL_Joystick* joystick, int* percent) override
        {
            FakeDevice* d = dev(joystick);
            if (d == nullptr)
            {
                if (percent != nullptr)
                    *percent = -1;
                return SDL_POWERSTATE_ERROR;
            }
            if (percent != nullptr)
                *percent = d->cfg.powerPercent;
            return d->cfg.powerState;
        }

    private:
        struct FakeDevice
        {
            SDL_JoystickID id = 0;
            FakeJoystickConfig cfg;
            bool closed = false;
        };

        FakeDevice* dev(SDL_Joystick* j) { return reinterpret_cast<FakeDevice*>(j); }

        std::map<SDL_JoystickID, FakeJoystickConfig> registered_;
        std::deque<std::unique_ptr<FakeDevice>> devices_;
    };
}
