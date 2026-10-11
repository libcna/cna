// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_MARSHAL_HPP
#define CNA_C_API_MARSHAL_HPP

// The conversions between the C ABI's value structs and the canonical C++ values, in one place.
// `ToNative` reads a C value; `ToC` writes one. Every conversion is field by field: no C struct is
// ever reinterpreted as a C++ type, so the C layout and the C++ layout are free to differ.

#include "CNA/C/core.h"
#include "CNA/C/curve.h"
#include "CNA/C/graphics3d.h"
#include "CNA/C/graphics_device.h"
#include "CNA/C/math_values.h"
#include "CNA/C/vertex_values.h"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cstdint>

namespace CNA::C::Detail {

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

// ---- Framework math values ----------------------------------------------------------------

[[nodiscard]] inline Xna::Vector2 ToNative(const CNA_Vector2 value) noexcept
{
    return Xna::Vector2(value.x, value.y);
}

[[nodiscard]] inline CNA_Vector2 ToC(const Xna::Vector2& value) noexcept
{
    return CNA_Vector2{value.X, value.Y};
}

[[nodiscard]] inline Xna::Vector3 ToNative(const CNA_Vector3 value) noexcept
{
    return Xna::Vector3(value.x, value.y, value.z);
}

[[nodiscard]] inline CNA_Vector3 ToC(const Xna::Vector3& value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] inline Xna::Vector4 ToNative(const CNA_Vector4 value) noexcept
{
    return Xna::Vector4(value.x, value.y, value.z, value.w);
}

[[nodiscard]] inline CNA_Vector4 ToC(const Xna::Vector4& value) noexcept
{
    return CNA_Vector4{value.X, value.Y, value.Z, value.W};
}

[[nodiscard]] inline Xna::Quaternion ToNative(const CNA_Quaternion value) noexcept
{
    return Xna::Quaternion(value.x, value.y, value.z, value.w);
}

[[nodiscard]] inline CNA_Quaternion ToC(const Xna::Quaternion& value) noexcept
{
    return CNA_Quaternion{value.X, value.Y, value.Z, value.W};
}

[[nodiscard]] inline Xna::Matrix ToNative(const CNA_Matrix& value) noexcept
{
    return Xna::Matrix(
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44);
}

[[nodiscard]] inline CNA_Matrix ToC(const Xna::Matrix& value) noexcept
{
    return CNA_Matrix{
        value.M11, value.M12, value.M13, value.M14,
        value.M21, value.M22, value.M23, value.M24,
        value.M31, value.M32, value.M33, value.M34,
        value.M41, value.M42, value.M43, value.M44};
}

[[nodiscard]] inline Xna::Point ToNative(const CNA_Point value) noexcept
{
    return Xna::Point(value.x, value.y);
}

[[nodiscard]] inline CNA_Point ToC(const Xna::Point& value) noexcept
{
    return CNA_Point{value.X, value.Y};
}

[[nodiscard]] inline Xna::Rectangle ToNative(const CNA_Rectangle value) noexcept
{
    return Xna::Rectangle(value.x, value.y, value.width, value.height);
}

[[nodiscard]] inline CNA_Rectangle ToC(const Xna::Rectangle& value) noexcept
{
    return CNA_Rectangle{value.X, value.Y, value.Width, value.Height};
}

[[nodiscard]] inline Xna::Plane ToNative(const CNA_Plane value) noexcept
{
    return Xna::Plane(ToNative(value.normal), value.d);
}

[[nodiscard]] inline CNA_Plane ToC(const Xna::Plane& value) noexcept
{
    return CNA_Plane{ToC(value.Normal), value.D};
}

[[nodiscard]] inline Xna::Ray ToNative(const CNA_Ray value) noexcept
{
    return Xna::Ray(ToNative(value.position), ToNative(value.direction));
}

[[nodiscard]] inline CNA_Ray ToC(const Xna::Ray& value) noexcept
{
    return CNA_Ray{ToC(value.Position), ToC(value.Direction)};
}

[[nodiscard]] inline Xna::BoundingBox ToNative(const CNA_BoundingBox value) noexcept
{
    return Xna::BoundingBox(ToNative(value.min), ToNative(value.max));
}

[[nodiscard]] inline CNA_BoundingBox ToC(const Xna::BoundingBox& value) noexcept
{
    return CNA_BoundingBox{ToC(value.Min), ToC(value.Max)};
}

[[nodiscard]] inline Xna::BoundingSphere ToNative(const CNA_BoundingSphere value) noexcept
{
    return Xna::BoundingSphere(ToNative(value.center), value.radius);
}

[[nodiscard]] inline CNA_BoundingSphere ToC(const Xna::BoundingSphere& value) noexcept
{
    return CNA_BoundingSphere{ToC(value.Center), value.Radius};
}

[[nodiscard]] inline Xna::BoundingFrustum ToNative(const CNA_BoundingFrustum& value)
{
    return Xna::BoundingFrustum(ToNative(value.matrix));
}

[[nodiscard]] inline CNA_BoundingFrustum ToC(const Xna::BoundingFrustum& value) noexcept
{
    return CNA_BoundingFrustum{ToC(value.getMatrixProperty())};
}

[[nodiscard]] inline Xna::Color ToNative(const CNA_Color value) noexcept
{
    return Xna::Color(value.r, value.g, value.b, value.a);
}

[[nodiscard]] inline CNA_Color ToC(const Xna::Color& value) noexcept
{
    return CNA_Color{
        value.getRProperty(),
        value.getGProperty(),
        value.getBProperty(),
        value.getAProperty()};
}

[[nodiscard]] inline Xna::CurveKey ToNative(const CNA_CurveKey value)
{
    return Xna::CurveKey(
        value.position,
        value.value,
        value.tangent_in,
        value.tangent_out,
        static_cast<Xna::CurveContinuity>(value.continuity));
}

[[nodiscard]] inline CNA_CurveKey ToC(const Xna::CurveKey& value) noexcept
{
    return CNA_CurveKey{
        value.getPositionProperty(),
        value.getValueProperty(),
        value.getTangentInProperty(),
        value.getTangentOutProperty(),
        static_cast<CNA_CurveContinuity>(value.getContinuityProperty())};
}

// ---- Graphics values -----------------------------------------------------------------------

[[nodiscard]] inline XnaGraphics::Viewport ToNative(const CNA_Viewport value)
{
    XnaGraphics::Viewport viewport(value.x, value.y, value.width, value.height);
    viewport.setMinDepthProperty(value.min_depth);
    viewport.setMaxDepthProperty(value.max_depth);
    return viewport;
}

[[nodiscard]] inline CNA_Viewport ToC(const XnaGraphics::Viewport& value)
{
    return CNA_Viewport{
        value.getXProperty(),
        value.getYProperty(),
        value.getWidthProperty(),
        value.getHeightProperty(),
        value.getMinDepthProperty(),
        value.getMaxDepthProperty()};
}

[[nodiscard]] inline CNA_VertexElement ToC(const XnaGraphics::VertexElement& value) noexcept
{
    return CNA_VertexElement{
        value.getOffsetProperty(),
        static_cast<CNA_VertexElementFormat>(value.getVertexElementFormatProperty()),
        static_cast<CNA_VertexElementUsage>(value.getVertexElementUsageProperty()),
        value.getUsageIndexProperty()};
}

[[nodiscard]] inline XnaGraphics::VertexPositionColor ToNative(
    const CNA_VertexPositionColor& value)
{
    return XnaGraphics::VertexPositionColor(ToNative(value.position), ToNative(value.color));
}

[[nodiscard]] inline CNA_VertexPositionColor ToC(
    const XnaGraphics::VertexPositionColor& value) noexcept
{
    return CNA_VertexPositionColor{ToC(value.Position), ToC(value.Color)};
}

[[nodiscard]] inline XnaGraphics::VertexPositionColorTexture ToNative(
    const CNA_VertexPositionColorTexture& value)
{
    return XnaGraphics::VertexPositionColorTexture(
        ToNative(value.position), ToNative(value.color), ToNative(value.texture_coordinate));
}

[[nodiscard]] inline CNA_VertexPositionColorTexture ToC(
    const XnaGraphics::VertexPositionColorTexture& value) noexcept
{
    return CNA_VertexPositionColorTexture{
        ToC(value.Position), ToC(value.Color), ToC(value.TextureCoordinate)};
}

[[nodiscard]] inline XnaGraphics::VertexPositionNormalTangentTexture ToNative(
    const CNA_VertexPositionNormalTangentTexture& value)
{
    return XnaGraphics::VertexPositionNormalTangentTexture(
        ToNative(value.position), ToNative(value.normal), ToNative(value.tangent),
        ToNative(value.texture_coordinate));
}

[[nodiscard]] inline CNA_VertexPositionNormalTangentTexture ToC(
    const XnaGraphics::VertexPositionNormalTangentTexture& value) noexcept
{
    return CNA_VertexPositionNormalTangentTexture{
        ToC(value.Position), ToC(value.Normal), ToC(value.Tangent),
        ToC(value.TextureCoordinate)};
}

[[nodiscard]] inline XnaGraphics::VertexPositionNormalTangentTextureSkinned ToNative(
    const CNA_VertexPositionNormalTangentTextureSkinned& value)
{
    return XnaGraphics::VertexPositionNormalTangentTextureSkinned(
        ToNative(value.position), ToNative(value.normal), ToNative(value.tangent),
        ToNative(value.texture_coordinate), ToNative(value.blend_weight),
        std::array<uint8_t, 4>{
            value.blend_indices[0], value.blend_indices[1],
            value.blend_indices[2], value.blend_indices[3]});
}

[[nodiscard]] inline CNA_VertexPositionNormalTangentTextureSkinned ToC(
    const XnaGraphics::VertexPositionNormalTangentTextureSkinned& value) noexcept
{
    return CNA_VertexPositionNormalTangentTextureSkinned{
        ToC(value.Position), ToC(value.Normal), ToC(value.Tangent),
        ToC(value.TextureCoordinate), ToC(value.BlendWeight),
        {value.BlendIndices[0], value.BlendIndices[1],
         value.BlendIndices[2], value.BlendIndices[3]}};
}

[[nodiscard]] inline XnaGraphics::VertexPositionNormalTexture ToNative(
    const CNA_VertexPositionNormalTexture& value)
{
    return XnaGraphics::VertexPositionNormalTexture(
        ToNative(value.position), ToNative(value.normal), ToNative(value.texture_coordinate));
}

[[nodiscard]] inline CNA_VertexPositionNormalTexture ToC(
    const XnaGraphics::VertexPositionNormalTexture& value) noexcept
{
    return CNA_VertexPositionNormalTexture{
        ToC(value.Position), ToC(value.Normal), ToC(value.TextureCoordinate)};
}

[[nodiscard]] inline XnaGraphics::VertexPositionNormalTextureSkinned ToNative(
    const CNA_VertexPositionNormalTextureSkinned& value)
{
    return XnaGraphics::VertexPositionNormalTextureSkinned(
        ToNative(value.position), ToNative(value.normal), ToNative(value.texture_coordinate),
        ToNative(value.blend_weight),
        std::array<uint8_t, 4>{
            value.blend_indices[0], value.blend_indices[1],
            value.blend_indices[2], value.blend_indices[3]});
}

[[nodiscard]] inline CNA_VertexPositionNormalTextureSkinned ToC(
    const XnaGraphics::VertexPositionNormalTextureSkinned& value) noexcept
{
    return CNA_VertexPositionNormalTextureSkinned{
        ToC(value.Position), ToC(value.Normal), ToC(value.TextureCoordinate),
        ToC(value.BlendWeight),
        {value.BlendIndices[0], value.BlendIndices[1],
         value.BlendIndices[2], value.BlendIndices[3]}};
}

[[nodiscard]] inline XnaGraphics::VertexPositionTexture ToNative(
    const CNA_VertexPositionTexture& value)
{
    return XnaGraphics::VertexPositionTexture(
        ToNative(value.position), ToNative(value.texture_coordinate));
}

[[nodiscard]] inline CNA_VertexPositionTexture ToC(
    const XnaGraphics::VertexPositionTexture& value) noexcept
{
    return CNA_VertexPositionTexture{ToC(value.Position), ToC(value.TextureCoordinate)};
}

} // namespace CNA::C::Detail

#endif
