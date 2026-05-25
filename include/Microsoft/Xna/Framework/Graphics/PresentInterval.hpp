#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines how presentation is synchronized with display refresh.
    enum class PresentInterval
    {
        Default,
        One,
        Two,
        Immediate
    };
}
