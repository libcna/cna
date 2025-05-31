// #ifndef READONLY_PROPERTY_H
// #define READONLY_PROPERTY_H
//
// #include <iostream>
// #include <functional>
// #include "CNA/Property.h"
//
// namespace CNA {
//     // Derived class: ReadOnlyProperty
//     template <typename T>
//     class ReadOnlyProperty : public Property<T> {
//     public:
//         ReadOnlyProperty(std::function<T()> customGetter)
//             : Property<T>(customGetter, nullptr) {}
//
//         // Delete setter to enforce read-only
//         T& operator=(const T& value) = delete;
//     };
//
//
// }
//
//
//
//
// ///// Example usage
// //class Owner {
// //public:
// //    Owner()
// //        : Width([this]() { return rightX - leftX; }),
// //          Height([this]() { return bottomY - topY; }) {}
// //
// //    // Properties
// //    ReadOnlyProperty<int> Width;
// //    ReadOnlyProperty<int> Height;
// //
// //private:
// //    int leftX = 10;
// //    int rightX = 50;
// //    int topY = 20;
// //    int bottomY = 80;
// //};
// //
// //int main() {
// //    Owner rect;
// //
// //    // Access the read-only properties
// //    std::cout << "Width: " << rect.Width << "\n";  // Output: 40
// //    std::cout << "Height: " << rect.Height << "\n"; // Output: 60
// //
// //    // Uncommenting the following lines will result in a compilation error due to deleted setter
// //    // rect.Width = 100;
// //    // rect.Height = 200;
// //
// //    return 0;
// //}
//
// #endif
