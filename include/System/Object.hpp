//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include <string>
#include <cstddef>

#define GetTypeNameHPP() [[nodiscard]] virtual const std::string& GetTypeName() const override;
#define GetTypeNameCPP(CLASS, NAME) \
const std::string& CLASS ::GetTypeName() const\
        {\
            const static std::string type_name = #NAME;\
            return type_name;\
        }

namespace System
{
    class Object
    {
    public:
        Object();
        virtual ~Object();

        /// <summary>
        /// Returns a string that represents the current object.
        /// Default implementation returns the runtime type name.
        /// </summary>
        [[nodiscard]] virtual std::string ToString() const;

        /// <summary>
        /// Determines whether the specified object is equal to the current object.
        /// Default implementation uses reference identity.
        /// </summary>
        virtual bool Equals(const Object* obj) const;

        /// <summary>
        /// Static equality helper equivalent to System.Object.Equals(objA, objB).
        /// </summary>
        static bool Equals(const Object* objA, const Object* objB);

        /// <summary>
        /// Static reference equality helper equivalent to System.Object.ReferenceEquals(objA, objB).
        /// </summary>
        static bool ReferenceEquals(const Object* objA, const Object* objB);

        /// <summary>
        /// Serves as the default hash function.
        /// Default implementation hashes object identity (address).
        /// </summary>
        [[nodiscard]] virtual int GetHashCode() const;

        [[nodiscard]] virtual const std::string& GetTypeName() const = 0;
    };
}