// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Audio
{
    class SoundEffectInstance;

    /** @brief Abstract interface for types that can produce SoundEffectInstance objects. */
    class SoundEffectI
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~SoundEffectI() = default;

        /**
         * @brief Creates a new SoundEffectInstance from this sound effect.
         *
         * @return A new SoundEffectInstance bound to this effect.
         */
        [[nodiscard]] virtual SoundEffectInstance CreateInstance() const = 0;
    };
}
