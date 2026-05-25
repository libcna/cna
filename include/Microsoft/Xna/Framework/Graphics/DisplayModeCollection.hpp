#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Read-only collection of supported display modes.
    class DisplayModeCollection : public System::Object
    {
    public:
        using iterator = std::vector<DisplayMode>::iterator;
        using const_iterator = std::vector<DisplayMode>::const_iterator;

        DisplayModeCollection();
        explicit DisplayModeCollection(std::vector<DisplayMode> modes);

        /// Gets the number of display modes.
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;

        /// Gets a display mode by index.
        [[nodiscard]] const DisplayMode& operator[](SharpRuntime::intcs index) const;

        [[nodiscard]] const_iterator begin() const;
        [[nodiscard]] const_iterator end() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<DisplayMode> modes_;
    };
}
