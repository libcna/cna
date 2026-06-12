// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Audio
{
    class SoundEffectInstance;

    /// Abstract interface for types that can create sound effect instances.
    class SoundEffectI
    {
    public:
        /// Virtual destructor.
        virtual ~SoundEffectI() = default;
        /// Creates a new SoundEffectInstance from this sound effect.
        [[nodiscard]] virtual SoundEffectInstance CreateInstance() const = 0;
    };
}
