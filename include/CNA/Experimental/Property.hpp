#pragma once

/**
 * @brief Experimental property wrapper.
 * @note Status: Stub
 * @note This type is experimental and is not used by the current CNA design.
 * Prop.hpp is preferred because it expands to ordinary members and methods
 * without per-instance std::function-based indirection.
 */
#include <iostream>
#include <functional>

#define DEF_PROP_AUTO(type, name, init) \
    private: type name##VVVV = init; \
    public: CNA::Property<type> name;

#define IMPL_PROP_AUTO(type, name)\
name( [this]() { return name##VVVV; } , [this](type v) {name##VVVV = v; })

#define IMPL_PROP_AUTO_READONLY(type, name)\
name( [this]() { return name##VVVV; })

#define DEF_PROP_CUSTOM(type, name) \
CNA::Property<type> name;

#define IMPL_PROP_CUSTOM(type, name, customGetter, customSetter)\
name( [this]() customGetter, [this](type v) customSetter)

#define IMPL_PROP_CUSTOM_READONLY(type, name, customGetter)\
name( [this]() customGetter )

namespace CNA::Experimental {
    // Template for Property
    template <typename T>
    class Property {
    public:
        Property(std::function<T()> customGetter, std::function<void(const T&)> customSetter = nullptr)
            : getter(customGetter), setter(customSetter) {}

        // Setter (only if a custom setter is provided)
        T& operator=(const T& value) {
            if (setter) {
                setter(value);
            } else {
                throw std::logic_error("Setter not implemented.");
            }
            return cachedValue; // Return cached value for chaining
        }

        // Getter
        operator T() const {
            return getter();
        }
        T get() const {
            return getter();
        }
        void set(T value) {
            if (setter) {
                setter(value);
            } else {
                throw std::logic_error("Setter not implemented.");
            }
        }

    private:
        std::function<T()> getter;
        std::function<void(const T&)> setter;
        T cachedValue; // For cases when setter stores its own value
    };

}


//// Example usage
//class MyClass {
//public:
//    MyClass()
//        : ReadWriteValue(
//            // Getter
//            [this]() { return value; },
//            // Setter
//            [this](const int& v) { value = v; }),
//          ReadOnlyValue(
//            // Getter
//            [this]() { return readOnlyValue; }
//          )
//    {}
//
//    // Property: Read-Write
//    Property<int> ReadWriteValue;
//
//    // Property: Read-Only
//    Property<int> ReadOnlyValue;
//
//private:
//    int value = 0;
//    int readOnlyValue = 42; // Example of a read-only value
//};
//
//int main() {
//    MyClass obj;
//
//    // Read-Write Property
//    obj.ReadWriteValue = 100; // Sets the value using the custom setter
//    std::cout << "Read-Write Value: " << obj.ReadWriteValue << "\n";
//
//    // Read-Only Property
//    std::cout << "Read-Only Value: " << obj.ReadOnlyValue << "\n";
//
//    // Uncommenting the following line will throw an exception because it's read-only
//    // obj.ReadOnlyValue = 200;
//
//    return 0;
//}
