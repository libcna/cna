// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Audio
{
    class SoundEffectInstance;

    class SoundEffectI
    {
    public:
        virtual ~SoundEffectI() = default;
        [[nodiscard]] virtual SoundEffectInstance CreateInstance() const = 0;
    };
}
