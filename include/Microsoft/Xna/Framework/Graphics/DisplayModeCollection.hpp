// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Read-only collection of DisplayMode objects representing supported display resolutions. */
    class DisplayModeCollection : public System::Object
    {
    public:
        using iterator = std::vector<DisplayMode>::iterator;
        using const_iterator = std::vector<DisplayMode>::const_iterator;

        /** @brief Constructs an empty DisplayModeCollection. */
        DisplayModeCollection();
        /**
         * @brief Constructs a DisplayModeCollection from a vector of display modes.
         * @param modes The display modes to store in this collection.
         */
        explicit DisplayModeCollection(std::vector<DisplayMode> modes);

        /** @brief Returns the number of display modes in the collection. */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;

        /**
         * @brief Returns the display mode at the given index.
         * @param index Zero-based index into the collection.
         * @return Const reference to the DisplayMode at @p index.
         */
        [[nodiscard]] const DisplayMode& operator[](SharpRuntime::intcs index) const;

        /** @brief Returns an iterator to the first display mode. */
        NOXNA [[nodiscard]] const_iterator begin() const;
        /** @brief Returns an iterator past the last display mode. */
        NOXNA [[nodiscard]] const_iterator end() const;

        /** @brief Returns the fully qualified .NET type name of this class. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<DisplayMode> modes_;
    };
}
