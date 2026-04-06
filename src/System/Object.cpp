//
// Created by robertvokac on 5/25/25.
//

#include "System/Object.hpp"

#include <typeinfo>
#include <functional>
#include <cstdint>

namespace System
{
    Object::Object() = default;

    Object::~Object() = default;

    // std::string Object::ToString() const
    // {
    //     // Closest practical equivalent to .NET default Object.ToString()
    //     // without a custom System::Type implementation.
    //     return typeid(*this).name();
    // }

    std::string Object::ToString() const
    {
        return GetTypeName();
    }

    bool Object::Equals(const Object* obj) const
    {
        return this == obj;
    }

    bool Object::Equals(const Object* objA, const Object* objB)
    {
        if (objA == objB)
        {
            return true;
        }

        if (objA == nullptr || objB == nullptr)
        {
            return false;
        }

        return objA->Equals(objB);
    }

    bool Object::ReferenceEquals(const Object* objA, const Object* objB)
    {
        return objA == objB;
    }

    int Object::GetHashCode() const
    {
        const std::size_t hash = std::hash<const void*>{}(this);

        // Narrow to int to resemble .NET Int32 return type.
        return static_cast<int>(hash & 0x7fffffff);
    }
}