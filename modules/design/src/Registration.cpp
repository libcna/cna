// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Design/Registration.hpp"

#include <memory>
#include <mutex>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Design.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ComponentModel/TypeConverterAttribute.hpp"
#include "System/ComponentModel/TypeDescriptor.hpp"

namespace CNA::Internal::Design {
namespace {

template<class TValue, class TConverter>
void registerConverter() {
    using System::ComponentModel::TypeConverterAttribute;
    System::ComponentModel::TypeDescriptor::RegisterType<TValue>({
        std::make_shared<TypeConverterAttribute>(TypeConverterAttribute::Of<TConverter>())});
}

} // namespace

void EnsureFrameworkDesignConvertersRegistered() {
    static std::once_flag once;
    std::call_once(once, [] {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Design;
        registerConverter<Point, PointConverter>();
        registerConverter<Rectangle, RectangleConverter>();
        registerConverter<Vector2, Vector2Converter>();
        registerConverter<Vector3, Vector3Converter>();
        registerConverter<Vector4, Vector4Converter>();
        registerConverter<Quaternion, QuaternionConverter>();
        registerConverter<Matrix, MatrixConverter>();
        registerConverter<BoundingBox, BoundingBoxConverter>();
        registerConverter<BoundingSphere, BoundingSphereConverter>();
        registerConverter<Plane, PlaneConverter>();
        registerConverter<Ray, RayConverter>();
        registerConverter<Color, ColorConverter>();
    });
}

} // namespace CNA::Internal::Design
