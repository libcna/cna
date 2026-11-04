// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"

#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

    void RegisterMathXnbReaders()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Vector2Reader", [] { return std::make_unique<Vector2Reader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Vector3Reader", [] { return std::make_unique<Vector3Reader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Vector4Reader", [] { return std::make_unique<Vector4Reader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.MatrixReader", [] { return std::make_unique<MatrixReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.QuaternionReader", [] { return std::make_unique<QuaternionReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ColorReader", [] { return std::make_unique<ColorReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.PlaneReader", [] { return std::make_unique<PlaneReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.PointReader", [] { return std::make_unique<PointReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.RectangleReader", [] { return std::make_unique<RectangleReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.BoundingBoxReader", [] { return std::make_unique<BoundingBoxReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.BoundingSphereReader",
            [] { return std::make_unique<BoundingSphereReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.BoundingFrustumReader",
            [] { return std::make_unique<BoundingFrustumReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.RayReader", [] { return std::make_unique<RayReader>(); });

        // A `Vector3[]` in an .xnb, which the stock pipeline writes whenever a processor tags a
        // model with one. `ArrayReader<T>` already existed as a template but no instantiation of
        // it was registered for any type, so the reader table of such a file could not resolve --
        // and an .xnb's table must resolve IN FULL before a single object is read, so the whole
        // asset failed rather than just the array. Found by cna-samples SAMPLE-048
        // (TrianglePickingSample), whose own ContentProcessor attaches every world-space triangle
        // vertex to `Model.Tag` as exactly this type.
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ArrayReader`1[[Microsoft.Xna.Framework.Vector3]]",
            [] {
                return std::make_unique<ArrayReader<Vector3>>(
                    "Microsoft.Xna.Framework.Vector3[]",
                    "Microsoft.Xna.Framework.Content.Vector3Reader");
            });

        // A `List<Matrix>`, which is how a skeleton's bind pose and inverse bind pose reach a
        // game -- XNA's own CustomModelAnimation and SkinnedModel samples both write exactly this
        // on `Model.Tag`. Same gap as the `Vector3[]` above: the template existed, no
        // instantiation of it was registered, and a table that names it cannot resolve at all.
        // Found by cna-samples SAMPLE-051.
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Matrix]]",
            [] {
                return std::make_unique<ListReader<Matrix>>(
                    "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Matrix]]",
                    "Microsoft.Xna.Framework.Content.MatrixReader");
            });

        // XNA instantiates NullableReader`1<T> for any value type on demand (TQ-033); the closed
        // readers of the framework's value types (BoundingFrustum is a class, so it has none).
        RegisterNullableXnbReader<Vector2>("Microsoft.Xna.Framework.Vector2", "Microsoft.Xna.Framework.Content.Vector2Reader");
        RegisterNullableXnbReader<Vector3>("Microsoft.Xna.Framework.Vector3", "Microsoft.Xna.Framework.Content.Vector3Reader");
        RegisterNullableXnbReader<Vector4>("Microsoft.Xna.Framework.Vector4", "Microsoft.Xna.Framework.Content.Vector4Reader");
        RegisterNullableXnbReader<Matrix>("Microsoft.Xna.Framework.Matrix", "Microsoft.Xna.Framework.Content.MatrixReader");
        RegisterNullableXnbReader<Quaternion>("Microsoft.Xna.Framework.Quaternion", "Microsoft.Xna.Framework.Content.QuaternionReader");
        RegisterNullableXnbReader<Color>("Microsoft.Xna.Framework.Color", "Microsoft.Xna.Framework.Content.ColorReader");
        RegisterNullableXnbReader<Point>("Microsoft.Xna.Framework.Point", "Microsoft.Xna.Framework.Content.PointReader");
        RegisterNullableXnbReader<Rectangle>("Microsoft.Xna.Framework.Rectangle", "Microsoft.Xna.Framework.Content.RectangleReader");
        RegisterNullableXnbReader<BoundingBox>("Microsoft.Xna.Framework.BoundingBox", "Microsoft.Xna.Framework.Content.BoundingBoxReader");
        RegisterNullableXnbReader<BoundingSphere>("Microsoft.Xna.Framework.BoundingSphere", "Microsoft.Xna.Framework.Content.BoundingSphereReader");
        RegisterNullableXnbReader<Ray>("Microsoft.Xna.Framework.Ray", "Microsoft.Xna.Framework.Content.RayReader");
        RegisterNullableXnbReader<Plane>("Microsoft.Xna.Framework.Plane", "Microsoft.Xna.Framework.Content.PlaneReader");
    }
}
