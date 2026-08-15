// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/JoystickCapabilities.hpp"
#include "CNA/Input/JoystickInfo.hpp"
#include "CNA/Input/JoystickState.hpp"
#include "System/MulticastAction.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — raw joystick access (flight sticks, wheels, throttles, arbitrary HID
     *        controllers), backed by the selected platform's raw-joystick service.
     *
     * XNA 4.0 only ever modeled Xbox-style mapped gamepads (`Microsoft::Xna::Framework::Input::
     * GamePad`). The raw joystick service exposes arbitrary axes/buttons/hats/trackballs with no
     * semantic mapping — essential for flight sims, racing wheels, and HOTAS setups that `GamePad`
     * cannot represent. A device the platform also maps as a gamepad is visible here too (as
     * `JoystickTypeEXT::Gamepad`); this is an independent, unmapped view of the same hardware.
     */
    CNAEXT class Joysticks
    {
    public:
        /** @brief Static-only utility; not instantiable. */
        Joysticks() = delete;

        /**
         * @brief Enumerates the connected raw joysticks.
         * @return A list of joystick id/name/type descriptors (empty if none connected).
         */
        CNAEXT [[nodiscard]] static std::vector<JoystickInfoEXT> GetJoysticksEXT();

        /**
         * @brief Returns the static hardware shape and identity of a joystick.
         * @param id The platform joystick id reported by enumeration or a hotplug event.
         * @return The device's capabilities, or a default (disconnected) value if `id` is not connected.
         */
        CNAEXT [[nodiscard]] static JoystickCapabilitiesEXT GetCapabilitiesEXT(std::uint32_t id);

        /**
         * @brief Returns the current axis/button/hat/trackball state of a joystick.
         * @param id The platform joystick id reported by enumeration or a hotplug event.
         * @return The device's current state, or all-empty if `id` is not connected.
         */
        CNAEXT [[nodiscard]] static JoystickStateEXT GetStateEXT(std::uint32_t id);

        /** @brief CNAEXT/EXT: fires with the platform device id when a joystick is connected. */
        CNAEXT static System::MulticastAction<std::uint32_t> ConnectedEXT;

        /** @brief CNAEXT/EXT: fires with the platform device id when a joystick is disconnected. */
        CNAEXT static System::MulticastAction<std::uint32_t> DisconnectedEXT;

        /**
         * @brief Test-only: clears the hot-plug event subscribers.
         * @note CNAEXT — a CNA test-support helper, not part of the XNA 4.0 API.
         */
        CNAEXT static void ResetForTests();
    };
}
