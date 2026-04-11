#pragma once

#include <vector>

#include "TouchLocation.hpp"
#include "CNA/Prop.hpp"
#include "CNA/CnaHelper.hpp"

namespace Microsoft::Xna::Framework::Input::Touch {
    using CNA::intcs;

    struct TouchCollection {
    private:
        std::vector<TouchLocation> touches;

    public:
        TouchCollection();
        explicit TouchCollection(const std::vector<TouchLocation>& touches);
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