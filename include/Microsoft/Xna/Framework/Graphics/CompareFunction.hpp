#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines comparison functions for depth/stencil tests.
    enum class CompareFunction
    {
        Always,
        Never,
        Less,
        LessEqual,
        Equal,
        GreaterEqual,
        Greater,
        NotEqual,
    };
}
