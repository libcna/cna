// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

// plans/plan_xnb.md XNB-19: math .xnb readers -- see PrimitiveContentTypeReaders.hpp's own note on why
// these live in CNA::Internal::Xnb (FNA's own equivalents are all internal/default-visibility
// classes, never subclassed by game code).

namespace CNA::Internal::Xnb
{
    using namespace Microsoft::Xna::Framework;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;

    class Vector2Reader : public ContentTypeReader<Vector2>
    {
    public:
        Vector2Reader() : ContentTypeReader<Vector2>("Microsoft.Xna.Framework.Vector2") {}
    protected:
        Vector2 Read(ContentReader& input, std::optional<Vector2>) override { return input.ReadVector2(); }
    };

    class Vector3Reader : public ContentTypeReader<Vector3>
    {
    public:
        Vector3Reader() : ContentTypeReader<Vector3>("Microsoft.Xna.Framework.Vector3") {}
    protected:
        Vector3 Read(ContentReader& input, std::optional<Vector3>) override { return input.ReadVector3(); }
    };

    class Vector4Reader : public ContentTypeReader<Vector4>
    {
    public:
        Vector4Reader() : ContentTypeReader<Vector4>("Microsoft.Xna.Framework.Vector4") {}
    protected:
        Vector4 Read(ContentReader& input, std::optional<Vector4>) override { return input.ReadVector4(); }
    };

    class MatrixReader : public ContentTypeReader<Matrix>
    {
    public:
        MatrixReader() : ContentTypeReader<Matrix>("Microsoft.Xna.Framework.Matrix") {}
    protected:
        Matrix Read(ContentReader& input, std::optional<Matrix>) override { return input.ReadMatrix(); }
    };

    class QuaternionReader : public ContentTypeReader<Quaternion>
    {
    public:
        QuaternionReader() : ContentTypeReader<Quaternion>("Microsoft.Xna.Framework.Quaternion") {}
    protected:
        Quaternion Read(ContentReader& input, std::optional<Quaternion>) override { return input.ReadQuaternion(); }
    };

    class ColorReader : public ContentTypeReader<Color>
    {
    public:
        ColorReader() : ContentTypeReader<Color>("Microsoft.Xna.Framework.Color") {}
    protected:
        Color Read(ContentReader& input, std::optional<Color>) override { return input.ReadColor(); }
    };

    class PlaneReader : public ContentTypeReader<Plane>
    {
    public:
        PlaneReader() : ContentTypeReader<Plane>("Microsoft.Xna.Framework.Plane") {}
    protected:
        Plane Read(ContentReader& input, std::optional<Plane>) override
        {
            const Vector3 normal = input.ReadVector3();
            const float d = input.ReadSingle();
            return Plane(normal, d);
        }
    };

    class PointReader : public ContentTypeReader<Point>
    {
    public:
        PointReader() : ContentTypeReader<Point>("Microsoft.Xna.Framework.Point") {}
    protected:
        Point Read(ContentReader& input, std::optional<Point>) override
        {
            const int32_t x = input.ReadInt32();
            const int32_t y = input.ReadInt32();
            return Point(x, y);
        }
    };

    class RectangleReader : public ContentTypeReader<Rectangle>
    {
    public:
        RectangleReader() : ContentTypeReader<Rectangle>("Microsoft.Xna.Framework.Rectangle") {}
    protected:
        Rectangle Read(ContentReader& input, std::optional<Rectangle>) override
        {
            const int32_t left = input.ReadInt32();
            const int32_t top = input.ReadInt32();
            const int32_t width = input.ReadInt32();
            const int32_t height = input.ReadInt32();
            return Rectangle(left, top, width, height);
        }
    };

    class BoundingBoxReader : public ContentTypeReader<BoundingBox>
    {
    public:
        BoundingBoxReader() : ContentTypeReader<BoundingBox>("Microsoft.Xna.Framework.BoundingBox") {}
    protected:
        BoundingBox Read(ContentReader& input, std::optional<BoundingBox>) override
        {
            const Vector3 min = input.ReadVector3();
            const Vector3 max = input.ReadVector3();
            return BoundingBox(min, max);
        }
    };

    class BoundingSphereReader : public ContentTypeReader<BoundingSphere>
    {
    public:
        BoundingSphereReader() : ContentTypeReader<BoundingSphere>("Microsoft.Xna.Framework.BoundingSphere") {}
    protected:
        BoundingSphere Read(ContentReader& input, std::optional<BoundingSphere>) override
        {
            return input.ReadBoundingSphere();
        }
    };

    class BoundingFrustumReader : public ContentTypeReader<BoundingFrustum>
    {
    public:
        BoundingFrustumReader() : ContentTypeReader<BoundingFrustum>("Microsoft.Xna.Framework.BoundingFrustum") {}
    protected:
        BoundingFrustum Read(ContentReader& input, std::optional<BoundingFrustum>) override
        {
            return BoundingFrustum(input.ReadMatrix());
        }
    };

    class RayReader : public ContentTypeReader<Ray>
    {
    public:
        RayReader() : ContentTypeReader<Ray>("Microsoft.Xna.Framework.Ray") {}
    protected:
        Ray Read(ContentReader& input, std::optional<Ray>) override
        {
            const Vector3 position = input.ReadVector3();
            const Vector3 direction = input.ReadVector3();
            return Ray(position, direction);
        }
    };

    /** @brief Registers all math readers above under their real FNA canonical names. Idempotent. */
    void RegisterMathXnbReaders();
}
