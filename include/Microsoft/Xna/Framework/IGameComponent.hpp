#pragma once

namespace Microsoft::Xna::Framework
{
    /// Provides initialization behavior for a game component.
    class IGameComponent
    {
    public:
        virtual ~IGameComponent() = default;

        /// Initializes the component.
        virtual void Initialize() = 0;
    };
}
