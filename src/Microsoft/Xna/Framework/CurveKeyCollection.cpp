// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/CurveKeyCollection.hpp"

#include <algorithm>
#include <stdexcept>

#include "Microsoft/Xna/Framework/MathHelper.hpp"

namespace Microsoft::Xna::Framework
{
    int CurveKeyCollection::getCountProperty() const
    {
        return static_cast<int>(innerlist.size());
    }

    bool CurveKeyCollection::getIsReadOnlyProperty() const
    {
        return isReadOnly;
    }

    CurveKey& CurveKeyCollection::getItemProperty(int index)
    {
        if (index < 0 || index >= getCountProperty())
        {
            throw std::out_of_range("CurveKeyCollection index is out of range");
        }
        return innerlist[static_cast<std::size_t>(index)];
    }

    const CurveKey& CurveKeyCollection::getItemProperty(int index) const
    {
        if (index < 0 || index >= getCountProperty())
        {
            throw std::out_of_range("CurveKeyCollection index is out of range");
        }
        return innerlist[static_cast<std::size_t>(index)];
    }

    void CurveKeyCollection::setItemProperty(int index, const CurveKey& value)
    {
        if (index < 0 || index >= getCountProperty())
        {
            throw std::out_of_range("CurveKeyCollection index is out of range");
        }

        auto position = innerlist[static_cast<std::size_t>(index)].getPositionProperty();
        if (MathHelper::WithinEpsilon(position, value.getPositionProperty()))
        {
            innerlist[static_cast<std::size_t>(index)] = value;
        }
        else
        {
            innerlist.erase(innerlist.begin() + index);
            Add(value);
        }
    }

    CurveKey& CurveKeyCollection::operator[](int index)
    {
        return getItemProperty(index);
    }

    const CurveKey& CurveKeyCollection::operator[](int index) const
    {
        return getItemProperty(index);
    }

    CurveKeyCollection::CurveKeyCollection()
        : isReadOnly(false), innerlist()
    {
    }

    void CurveKeyCollection::Add(const CurveKey& item)
    {
        if (innerlist.empty())
        {
            innerlist.push_back(item);
            return;
        }

        for (auto it = innerlist.begin(); it != innerlist.end(); ++it)
        {
            if (item.getPositionProperty() < it->getPositionProperty())
            {
                innerlist.insert(it, item);
                return;
            }
        }

        innerlist.push_back(item);
    }

    void CurveKeyCollection::Clear()
    {
        innerlist.clear();
    }

    CurveKeyCollection CurveKeyCollection::Clone() const
    {
        CurveKeyCollection ckc;
        for (const CurveKey& key : innerlist)
        {
            ckc.Add(key);
        }
        return ckc;
    }

    bool CurveKeyCollection::Contains(const CurveKey& item) const
    {
        return std::find(innerlist.begin(), innerlist.end(), item) != innerlist.end();
    }

    void CurveKeyCollection::CopyTo(std::vector<CurveKey>& array, int arrayIndex) const
    {
        if (arrayIndex < 0)
        {
            throw std::out_of_range("arrayIndex is out of range");
        }

        const auto start = static_cast<std::size_t>(arrayIndex);
        const auto requiredSize = start + innerlist.size();
        if (array.size() < requiredSize)
        {
            throw std::out_of_range("Destination array is too small");
        }

        std::copy(innerlist.begin(), innerlist.end(), array.begin() + arrayIndex);
    }

    int CurveKeyCollection::IndexOf(const CurveKey& item) const
    {
        const auto it = std::find(innerlist.begin(), innerlist.end(), item);
        if (it == innerlist.end())
        {
            return -1;
        }
        return static_cast<int>(std::distance(innerlist.begin(), it));
    }

    bool CurveKeyCollection::Remove(const CurveKey& item)
    {
        const auto it = std::find(innerlist.begin(), innerlist.end(), item);
        if (it == innerlist.end())
        {
            return false;
        }
        innerlist.erase(it);
        return true;
    }

    void CurveKeyCollection::RemoveAt(int index)
    {
        if (index < 0 || index >= getCountProperty())
        {
            throw std::out_of_range("CurveKeyCollection index is out of range");
        }
        innerlist.erase(innerlist.begin() + index);
    }

    CurveKeyCollection::iterator CurveKeyCollection::begin()
    {
        return innerlist.begin();
    }

    CurveKeyCollection::iterator CurveKeyCollection::end()
    {
        return innerlist.end();
    }

    CurveKeyCollection::const_iterator CurveKeyCollection::begin() const
    {
        return innerlist.begin();
    }

    CurveKeyCollection::const_iterator CurveKeyCollection::end() const
    {
        return innerlist.end();
    }

    CurveKeyCollection::const_iterator CurveKeyCollection::cbegin() const
    {
        return innerlist.cbegin();
    }

    CurveKeyCollection::const_iterator CurveKeyCollection::cend() const
    {
        return innerlist.cend();
    }
}
