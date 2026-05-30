#pragma once

#include <vector>

#include "TouchLocation.hpp"
#include "SharpRuntime/Prop.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    using SharpRuntime::intcs;

    /**
     * @brief Immutable snapshot-like collection of current touch locations.
     *
     * @note Status: PARTIAL
     */
    struct TouchCollection
    {
    private:
        std::vector<TouchLocation> touches;

    public:
        /**
         * @brief Initializes an empty touch collection.
         */
        TouchCollection();

        /**
         * @brief Initializes touch collection from an existing vector.
         */
        explicit TouchCollection(const std::vector<TouchLocation>& touches);

        /**
         * @brief Initializes touch collection by moving an existing vector.
         */
        explicit TouchCollection(std::vector<TouchLocation>&& touches);

        DEF_PROP(intcs, Count, getter1, setter0, member0, static0, constret0, ref0, constmet1)

        std::vector<TouchLocation>::iterator begin();
        std::vector<TouchLocation>::iterator end();

        [[nodiscard]] std::vector<TouchLocation>::const_iterator begin() const;
        [[nodiscard]] std::vector<TouchLocation>::const_iterator end() const;

        [[nodiscard]] const TouchLocation& operator[](std::size_t index) const;
        [[nodiscard]] bool empty() const;
    };
}
