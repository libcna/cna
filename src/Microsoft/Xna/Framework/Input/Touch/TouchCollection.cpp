#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include <stdexcept>

namespace Microsoft::Xna::Framework::Input::Touch {

    TouchCollection::TouchCollection() = default;

    TouchCollection::TouchCollection(const std::vector<TouchLocation>& touches)
        : touches(touches)
    {
    }

    TouchCollection::TouchCollection(std::vector<TouchLocation>&& touches)
        : touches(std::move(touches))
    {
    }

    intcs TouchCollection::getCountProperty() const
    {
        return static_cast<intcs>(this->touches.size());
    }

    std::vector<TouchLocation>::iterator TouchCollection::begin()
    {
        return touches.begin();
    }

    std::vector<TouchLocation>::iterator TouchCollection::end()
    {
        return touches.end();
    }

    std::vector<TouchLocation>::const_iterator TouchCollection::begin() const
    {
        return touches.begin();
    }

    std::vector<TouchLocation>::const_iterator TouchCollection::end() const
    {
        return touches.end();
    }

    const TouchLocation& TouchCollection::operator[](std::size_t index) const
    {
        if (index >= touches.size()) {
            throw std::out_of_range("TouchCollection index out of range");
        }
        return touches[index];
    }

    bool TouchCollection::empty() const
    {
        return touches.empty();
    }
}