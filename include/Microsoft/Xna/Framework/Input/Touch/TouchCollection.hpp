#pragma once

#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"

#include <cstddef>
#include <vector>

namespace Microsoft::Xna::Framework::Input::Touch
{
    /// Provides an immutable snapshot of current touch locations.
    struct TouchCollection
    {
        /// Gets the number of touch locations in this collection.
        [[nodiscard]] int getCountProperty() const;

        /// Gets whether a touch device is connected.
        [[nodiscard]] bool getIsConnectedProperty() const;

        /// Always returns true — this collection does not support mutation.
        [[nodiscard]] bool getIsReadOnlyProperty() const;

        /// Constructs an empty touch collection.
        TouchCollection();

        /// Constructs from an array of touch locations.
        explicit TouchCollection(const std::vector<TouchLocation>& touches);

        /// Constructs by moving a vector of touch locations.
        explicit TouchCollection(std::vector<TouchLocation>&& touches);

        /// Returns the touch location at the given index.
        [[nodiscard]] const TouchLocation& operator[](std::size_t index) const;

        /// Returns true if the collection has no touch locations.
        [[nodiscard]] bool empty() const;

        [[nodiscard]] bool Contains(const TouchLocation& item) const;

        /// Tries to find a touch by finger id. Returns false if not found.
        bool FindById(int id, TouchLocation& touchLocation) const;

        void CopyTo(std::vector<TouchLocation>& array, int arrayIndex) const;
        [[nodiscard]] int IndexOf(const TouchLocation& item) const;

        std::vector<TouchLocation>::iterator begin();
        std::vector<TouchLocation>::iterator end();
        [[nodiscard]] std::vector<TouchLocation>::const_iterator begin() const;
        [[nodiscard]] std::vector<TouchLocation>::const_iterator end() const;

    private:
        std::vector<TouchLocation> touches_;
    };
}
