// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Input::Touch
{
    TouchCollection::Enumerator::Enumerator(const TouchCollection& collection)
        : touches_(collection.touches_), position_(-1)
    {
    }

    const TouchLocation& TouchCollection::Enumerator::Current() const
    {
        if (position_ < 0 || position_ >= static_cast<int>(touches_.size()))
        {
            throw System::ArgumentOutOfRangeException("index");
        }
        return touches_[static_cast<std::size_t>(position_)];
    }

    bool TouchCollection::Enumerator::MoveNext()
    {
        ++position_;
        const int count = static_cast<int>(touches_.size());
        if (position_ >= count)
        {
            position_ = count;
            return false;
        }
        return true;
    }

    void TouchCollection::Enumerator::Reset()
    {
        position_ = -1;
    }

    void TouchCollection::Enumerator::Dispose()
    {
    }

    TouchCollection::Enumerator TouchCollection::GetEnumerator() const
    {
        return Enumerator(*this);
    }

    TouchCollection::TouchCollection() = default;

    TouchCollection::TouchCollection(const std::vector<TouchLocation>& touches)
        : isConnected_(true), touches_(touches)
    {
        // XNA TouchCollection..ctor IL_000e-001e rejects more than eight supplied locations.
        if (touches_.size() > 8)
            throw System::ArgumentOutOfRangeException("touches");
    }

    TouchCollection::TouchCollection(std::vector<TouchLocation>&& touches)
        : isConnected_(true)
    {
        // XNA TouchCollection..ctor IL_000e-001e; validate before consuming the C++ rvalue.
        if (touches.size() > 8)
            throw System::ArgumentOutOfRangeException("touches");
        touches_ = std::move(touches);
    }

    int  TouchCollection::getCountProperty()       const { return static_cast<int>(touches_.size()); }
    // XNA TouchCollection.get_IsConnected IL_0000-0006 reads a value field; ctor IL_001f-0021 sets true.
    bool TouchCollection::getIsConnectedProperty() const { return isConnected_; }
    // XNA TouchCollection.get_IsReadOnly IL_0000-0001 returns true.
    bool TouchCollection::getIsReadOnlyProperty()  const { return true; }

    void TouchCollection::setItemProperty(std::size_t, const TouchLocation&)
    {
        // XNA TouchCollection.set_Item IL_0000-0005 throws before validating the index.
        throw System::NotSupportedException();
    }

    const TouchLocation& TouchCollection::operator[](std::size_t index) const
    {
        if (index >= touches_.size())
            throw System::ArgumentOutOfRangeException("TouchCollection index out of range");
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
        // XNA FindById(int,out) IL_0032-003a uses initobj: default id 0, Invalid, Zero.
        touchLocation = TouchLocation();
        return false;
    }

    void TouchCollection::CopyTo(std::vector<TouchLocation>& array, int arrayIndex) const
    {
        // C++ deviation from .NET List<T>.CopyTo (which overwrites pre-allocated slots of a
        // fixed-size array): CNA's destination is a growable std::vector, so this INSERTS at
        // arrayIndex. Guard the index — `array.begin() + arrayIndex` with arrayIndex < 0 or
        // > size() forms an invalid iterator and std::vector::insert is undefined behavior.
        // FNA throws ArgumentOutOfRangeException for a bad index; we map that to std::out_of_range.
        if (arrayIndex < 0 || static_cast<std::size_t>(arrayIndex) > array.size())
            throw System::ArgumentOutOfRangeException("TouchCollection::CopyTo: arrayIndex is out of range");
        for (const auto& t : touches_)
            array.insert(array.begin() + arrayIndex++, t);
    }

    int TouchCollection::IndexOf(const TouchLocation& item) const
    {
        for (int i = 0; i < static_cast<int>(touches_.size()); ++i)
            if (touches_[i] == item) return i;
        return -1;
    }

    // XNA TouchCollection.Add/Clear/Remove/RemoveAt/Insert IL_0000-0005 each throw unconditionally.
    void TouchCollection::Add(const TouchLocation&) { throw System::NotSupportedException(); }
    void TouchCollection::Clear() { throw System::NotSupportedException(); }
    bool TouchCollection::Remove(const TouchLocation&) { throw System::NotSupportedException(); }
    void TouchCollection::RemoveAt(int) { throw System::NotSupportedException(); }
    void TouchCollection::Insert(int, const TouchLocation&) { throw System::NotSupportedException(); }

    std::vector<TouchLocation>::const_iterator TouchCollection::begin() { return touches_.begin(); }
    std::vector<TouchLocation>::const_iterator TouchCollection::end()   { return touches_.end(); }

    std::vector<TouchLocation>::const_iterator TouchCollection::begin() const { return touches_.begin(); }
    std::vector<TouchLocation>::const_iterator TouchCollection::end()   const { return touches_.end(); }
}
