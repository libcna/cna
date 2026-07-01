// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

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
    bool TouchCollection::getIsConnectedProperty() const { return TouchPanel::getTouchDeviceExistsProperty(); }
    bool TouchCollection::getIsReadOnlyProperty()  const { return true; }

    TouchLocation& TouchCollection::operator[](std::size_t index)
    {
        if (index >= touches_.size())
            throw std::out_of_range("TouchCollection index out of range");
        return touches_[index];
    }

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

    void TouchCollection::Add(const TouchLocation& item)
    {
        touches_.push_back(item);
    }

    void TouchCollection::Clear()
    {
        touches_.clear();
    }

    bool TouchCollection::Remove(const TouchLocation& item)
    {
        for (auto it = touches_.begin(); it != touches_.end(); ++it)
        {
            if (*it == item)
            {
                touches_.erase(it);
                return true;
            }
        }
        return false;
    }

    void TouchCollection::RemoveAt(int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= touches_.size())
            throw std::out_of_range("TouchCollection index out of range");
        touches_.erase(touches_.begin() + index);
    }

    void TouchCollection::Insert(int index, const TouchLocation& item)
    {
        if (index < 0 || static_cast<std::size_t>(index) > touches_.size())
            throw std::out_of_range("TouchCollection index out of range");
        touches_.insert(touches_.begin() + index, item);
    }

    std::vector<TouchLocation>::iterator TouchCollection::begin() { return touches_.begin(); }
    std::vector<TouchLocation>::iterator TouchCollection::end()   { return touches_.end(); }

    std::vector<TouchLocation>::const_iterator TouchCollection::begin() const { return touches_.begin(); }
    std::vector<TouchLocation>::const_iterator TouchCollection::end()   const { return touches_.end(); }
}
