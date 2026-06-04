#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Input::Touch
{
    TouchCollection::TouchCollection() = default;

    TouchCollection::TouchCollection(const std::vector<TouchLocation>& touches)
        : touches_(touches)
    {
    }

    TouchCollection::TouchCollection(std::vector<TouchLocation>&& touches)
        : touches_(std::move(touches))
    {
    }

    int  TouchCollection::getCountProperty()       const { return static_cast<int>(touches_.size()); }
    bool TouchCollection::getIsConnectedProperty() const { return true; }
    bool TouchCollection::getIsReadOnlyProperty()  const { return true; }

    const TouchLocation& TouchCollection::operator[](std::size_t index) const
    {
        if (index >= touches_.size())
            throw std::out_of_range("TouchCollection index out of range");
        return touches_[index];
    }

    bool TouchCollection::empty() const
    {
        return touches_.empty();
    }

    bool TouchCollection::Contains(const TouchLocation& item) const
    {
        for (const auto& t : touches_)
            if (t == item) return true;
        return false;
    }

    bool TouchCollection::FindById(int id, TouchLocation& touchLocation) const
    {
        for (const auto& t : touches_)
        {
            if (t.getIdProperty() == id)
            {
                touchLocation = t;
                return true;
            }
        }
        return false;
    }

    void TouchCollection::CopyTo(std::vector<TouchLocation>& array, int arrayIndex) const
    {
        for (const auto& t : touches_)
            array.insert(array.begin() + arrayIndex++, t);
    }

    int TouchCollection::IndexOf(const TouchLocation& item) const
    {
        for (int i = 0; i < static_cast<int>(touches_.size()); ++i)
            if (touches_[i] == item) return i;
        return -1;
    }

    std::vector<TouchLocation>::iterator TouchCollection::begin() { return touches_.begin(); }
    std::vector<TouchLocation>::iterator TouchCollection::end()   { return touches_.end(); }

    std::vector<TouchLocation>::const_iterator TouchCollection::begin() const { return touches_.begin(); }
    std::vector<TouchLocation>::const_iterator TouchCollection::end()   const { return touches_.end(); }
}
