// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/HapticCapabilities.hpp"
#include "CNA/Input/HapticEffect.hpp"
#include "System/IDisposable.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace CNA::Platform
{
    class IPlatform;
    class IPlatformHapticDevice;
}

namespace CNA::Input
{
    /**
     * @brief CNAEXT — an opened force-feedback (haptic) device.
     *
     * Obtained from `Haptics::OpenEXT`/`OpenFromJoystickEXT`/`OpenFromMouseEXT`. Move-only RAII,
     * mirroring `Microsoft::Xna::Framework::Input::MouseCursor`: the device is closed on
     * destruction/`Dispose()`. A device that failed to open (or was moved-from) reports
     * `IsOpenEXT() == false`, and every other member is then a safe no-op / default-returning call.
     *
     * @note CNAEXT — XNA 4.0 has no force-feedback API beyond `GamePad::SetVibration`'s dual-motor
     *       rumble; this whole class is a CNA extension.
     */
    CNAEXT class HapticDevice : public System::IDisposable
    {
    public:
        /** @brief Constructs a closed haptic device. */
        CNAEXT HapticDevice() = default;

        HapticDevice(const HapticDevice&)            = delete;
        HapticDevice& operator=(const HapticDevice&) = delete;
        /** @brief Move-constructs a HapticDevice, transferring handle ownership. */
        HapticDevice(HapticDevice&& other) noexcept;
        /** @brief Move-assigns a HapticDevice, transferring handle ownership. */
        HapticDevice& operator=(HapticDevice&& other) noexcept;

        /** @brief Destructor; closes the device if open. */
        ~HapticDevice() override;

        /** @brief Closes the device if open. Safe to call more than once. */
        CNAEXT void Dispose() override;

        /** @brief Returns true if this device holds an open handle. */
        CNAEXT [[nodiscard]] bool IsOpenEXT() const;

        /** @brief Returns the device's human-readable name, or "" if closed/unknown. */
        CNAEXT [[nodiscard]] std::string GetNameEXT() const;

        /** @brief Returns the device's static hardware capabilities (default value if closed). */
        CNAEXT [[nodiscard]] HapticCapabilitiesEXT GetCapabilitiesEXT() const;

        /** @brief Returns true if the device could play the given effect template. */
        CNAEXT [[nodiscard]] bool IsEffectSupportedEXT(const HapticEffectEXT& effect) const;

        /**
         * @brief Initializes the device's simple rumble feature. Must succeed before
         *        `PlayRumbleEXT`/`StopRumbleEXT` can work.
         * @return True on success.
         */
        CNAEXT bool InitRumbleEXT();

        /**
         * @brief Plays the simple rumble.
         * @param strength Rumble strength, 0.0 to 1.0.
         * @param lengthMs Duration in milliseconds.
         * @return True on success.
         */
        CNAEXT bool PlayRumbleEXT(float strength, std::uint32_t lengthMs);

        /** @brief Stops the simple rumble. @return True on success. */
        CNAEXT bool StopRumbleEXT();

        /**
         * @brief Uploads a new effect to the device.
         * @param effect The effect template to upload.
         * @return The new effect's id, or -1 on failure.
         */
        CNAEXT [[nodiscard]] int CreateEffectEXT(const HapticEffectEXT& effect);

        /**
         * @brief Updates the parameters of a previously created effect.
         * @param effectId The id returned by `CreateEffectEXT`.
         * @param effect The new effect parameters.
         * @return True on success.
         */
        CNAEXT bool UpdateEffectEXT(int effectId, const HapticEffectEXT& effect);

        /**
         * @brief Plays a previously created effect.
         * @param effectId The id returned by `CreateEffectEXT`.
         * @param iterations Number of times to repeat the effect (1 = play once).
         * @return True on success.
         */
        CNAEXT bool RunEffectEXT(int effectId, std::uint32_t iterations = 1);

        /** @brief Stops a playing effect. @return True on success. */
        CNAEXT bool StopEffectEXT(int effectId);

        /** @brief Frees a previously created effect. */
        CNAEXT void DestroyEffectEXT(int effectId);

        /** @brief Returns true if the given effect is currently playing. */
        CNAEXT [[nodiscard]] bool GetEffectStatusEXT(int effectId) const;

        /** @brief Stops every effect currently playing on the device. @return True on success. */
        CNAEXT bool StopAllEffectsEXT();

        /**
         * @brief Sets the overall effect gain.
         * @param gain Gain from 0 (silent) to 100 (maximum).
         * @return True on success.
         */
        CNAEXT bool SetGainEXT(int gain);

        /**
         * @brief Sets the autocenter strength (e.g. a wheel centering itself).
         * @param autocenter Strength from 0 (disabled) to 100 (maximum).
         * @return True on success.
         */
        CNAEXT bool SetAutocenterEXT(int autocenter);

        /** @brief Pauses all effect playback on the device. @return True on success. */
        CNAEXT bool PauseEXT();

        /** @brief Resumes effect playback after a `PauseEXT`. @return True on success. */
        CNAEXT bool ResumeEXT();

    private:
        friend class Haptics;

        HapticDevice(std::unique_ptr<CNA::Platform::IPlatformHapticDevice> effectDevice,
                     CNA::Platform::IPlatform* platform,
                     std::optional<std::uint64_t> standaloneId, std::string standaloneName);

        std::unique_ptr<CNA::Platform::IPlatformHapticDevice> effectDevice_;
        CNA::Platform::IPlatform* platform_ = nullptr;
        std::optional<std::uint64_t> standaloneId_;
        std::string standaloneName_;
        bool isDisposed_ = false;
    };
}
