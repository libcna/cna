#include "Microsoft/Xna/Framework/CurveKeyCollection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Microsoft::Xna::Framework
{
    namespace
    {
        float MachineEpsilonFloat()
        {
            static const float epsilon = []()
            {
                float machineEpsilon = 1.0f;
                float comparison = 0.0f;
                do
                {
                    machineEpsilon *= 0.5f;
                    comparison = 1.0f + machineEpsilon;
                }
                while (comparison > 1.0f);
                return machineEpsilon;
            }();
            return epsilon;
        }

        bool WithinEpsilon(float a, float b)
        {
            return std::fabs(a - b) < MachineEpsilonFloat();
        }
    }

    CurveKeyCollection::CurveKeyCollection()
        : isReadOnly(false), innerlist()
    {
    }

    int CurveKeyCollection::Count() const
    {
        return static_cast<int>(innerlist.size());
    }

    int CurveKeyCollection::getCountProperty() const
    {
        return Count();
    }

    bool CurveKeyCollection::IsReadOnly() const
    {
        return isReadOnly;
    }

    bool CurveKeyCollection::getIsReadOnlyProperty() const
    {
        return IsReadOnly();
    }

    CurveKey& CurveKeyCollection::operator[](int index)
    {
        if (index < 0 || index >= Count())
        {
            throw std::out_of_range("CurveKeyCollection index is out of range");
        }
        return innerlist[static_cast<std::size_t>(index)];
    }

    const CurveKey& CurveKeyCollection::operator[](int index) const
    {
        if (index < 0 || index >= Count())
        {
            throw std::out_of_range("CurveKeyCollection index is out of range");
        }
        return innerlist[static_cast<std::size_t>(index)];
    }

    void CurveKeyCollection::Set(int index, const CurveKey& value)
    {
        if (index < 0 || index >= Count())
        {
            throw std::out_of_range("CurveKeyCollection index is out of range");
        }

        const std::size_t sizeIndex = static_cast<std::size_t>(index);
        if (WithinEpsilon(innerlist[sizeIndex].Position, value.Position))
        {
            innerlist[sizeIndex] = value;
        }
        else
        {
            innerlist.erase(innerlist.begin() + index);
            Add(value);
        }
    }

    void CurveKeyCollection::Add(const CurveKey& item)
    {
        auto insertPosition = std::find_if(
            innerlist.begin(),
            innerlist.end(),
            [&item](const CurveKey& key)
            {
                return item.Position < key.Position;
            }
        );
        innerlist.insert(insertPosition, item);
    }

    void CurveKeyCollection::Clear()
    {
        innerlist.clear();
    }

    CurveKeyCollection CurveKeyCollection::Clone() const
    {
        CurveKeyCollection result;
        for (const CurveKey& key : innerlist)
        {
            result.Add(key);
        }
        return result;
    }

    bool CurveKeyCollection::Contains(const CurveKey& item) const
    {
        return std::find(innerlist.begin(), innerlist.end(), item) != innerlist.end();
    }

    void CurveKeyCollection::CopyTo(std::vector<CurveKey>& array, int arrayIndex) const
    {
        if (arrayIndex < 0)
        {
            throw std::out_of_range("arrayIndex cannot be negative");
        }

        const std::size_t start = static_cast<std::size_t>(arrayIndex);
        if (array.size() < start + innerlist.size())
        {
            throw std::out_of_range("Destination vector is too small");
        }

        std::copy(innerlist.begin(), innerlist.end(), array.begin() + arrayIndex);
    }

    int CurveKeyCollection::IndexOf(const CurveKey& item) const
    {
        auto it = std::find(innerlist.begin(), innerlist.end(), item);
        if (it == innerlist.end())
        {
            return -1;
        }
        return static_cast<int>(std::distance(innerlist.begin(), it));
    }

    bool CurveKeyCollection::Remove(const CurveKey& item)
    {
        auto it = std::find(innerlist.begin(), innerlist.end(), item);
        if (it == innerlist.end())
        {
            return false;
        }
        innerlist.erase(it);
        return true;
    }

    void CurveKeyCollection::RemoveAt(int index)
    {
        if (index < 0 || index >= Count())
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
