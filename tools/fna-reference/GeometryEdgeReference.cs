// SPDX-License-Identifier: MS-PL
using System;
using Microsoft.Xna.Framework;

namespace CNA.FnaReference
{
    internal static partial class ValueEdgeReference
    {
        private static JsonWriter GeometryPlaneFields(Plane value)
        {
            var result = new JsonWriter().Add("normal", Vector(value.Normal));
            Scalar(result, "d", value.D);
            return result;
        }

        private static JsonWriter GeometryBoxFields(BoundingBox value)
        {
            return new JsonWriter().Add("min", Vector(value.Min)).Add("max", Vector(value.Max));
        }

        private static JsonWriter GeometrySphereFields(BoundingSphere value)
        {
            var result = new JsonWriter().Add("center", Vector(value.Center));
            Scalar(result, "radius", value.Radius);
            return result;
        }

        private static JsonWriter GeometryCorners(Vector3[] values)
        {
            var result = new JsonWriter();
            for (int i = 0; i < values.Length; ++i) result.Add("p" + i, Vector(values[i]));
            return result;
        }

        private static void GeometryDistance(JsonWriter output, string name, float? value)
        {
            if (value.HasValue) Scalar(output, name, value.Value);
            else output.Add(name, "none");
        }

        private static JsonWriter GeometryPlaneCase(Plane plane, Vector3 point, Matrix matrix, Quaternion quaternion)
        {
            var output = new JsonWriter().Add("input", GeometryPlaneFields(plane)).Add("point", Vector(point))
                .Add("matrix", MatrixFields(matrix)).Add("quaternion", Rotation(quaternion));
            var vector = new Vector4(point, 2);
            Scalar(output, "dot", plane.Dot(vector));
            plane.Dot(ref vector, out float dot);
            Scalar(output, "dotOut", dot);
            Scalar(output, "dotCoordinate", plane.DotCoordinate(point));
            plane.DotCoordinate(ref point, out dot);
            Scalar(output, "dotCoordinateOut", dot);
            Scalar(output, "dotNormal", plane.DotNormal(point));
            plane.DotNormal(ref point, out dot);
            Scalar(output, "dotNormalOut", dot);
            output.Add("normalize", GeometryPlaneFields(Plane.Normalize(plane)));
            Plane.Normalize(ref plane, out Plane result);
            output.Add("normalizeOut", GeometryPlaneFields(result));
            var copy = plane; copy.Normalize();
            output.Add("normalizeInstance", GeometryPlaneFields(copy));
            copy = plane; Plane.Normalize(ref copy, out copy);
            output.Add("normalizeAliased", GeometryPlaneFields(copy));
            output.Add("matrixTransform", GeometryPlaneFields(Plane.Transform(plane, matrix)));
            Plane.Transform(ref plane, ref matrix, out result);
            output.Add("matrixTransformOut", GeometryPlaneFields(result));
            copy = plane; Plane.Transform(ref copy, ref matrix, out copy);
            output.Add("matrixTransformAliased", GeometryPlaneFields(copy));
            output.Add("quaternionTransform", GeometryPlaneFields(Plane.Transform(plane, quaternion)));
            Plane.Transform(ref plane, ref quaternion, out result);
            output.Add("quaternionTransformOut", GeometryPlaneFields(result));
            copy = plane; Plane.Transform(ref copy, ref quaternion, out copy);
            output.Add("quaternionTransformAliased", GeometryPlaneFields(copy));
            return output;
        }

        private static JsonWriter GeometryRelation(BoundingBox box, BoundingSphere sphere, Ray ray, Plane plane, Vector3 point)
        {
            var output = new JsonWriter().Add("box", GeometryBoxFields(box)).Add("sphere", GeometrySphereFields(sphere))
                .Add("origin", Vector(ray.Position)).Add("direction", Vector(ray.Direction))
                .Add("plane", GeometryPlaneFields(plane)).Add("point", Vector(point));
            var otherBox = new BoundingBox(new Vector3(-1), new Vector3(1));
            var otherSphere = new BoundingSphere(new Vector3(0, 0, 0.5f), 1);
            var frustum = new BoundingFrustum(Matrix.Identity);
            ContainmentType containment;
            PlaneIntersectionType side;
            bool hit;
            float? distance;
            output.Add("boxContainsBox", (int)box.Contains(otherBox));
            box.Contains(ref otherBox, out containment);
            output.Add("boxContainsBoxOut", (int)containment);
            output.Add("boxContainsSphere", (int)box.Contains(sphere));
            box.Contains(ref sphere, out containment);
            output.Add("boxContainsSphereOut", (int)containment);
            output.Add("boxContainsPoint", (int)box.Contains(point));
            box.Contains(ref point, out containment);
            output.Add("boxContainsPointOut", (int)containment);
            output.Add("boxContainsFrustum", (int)box.Contains(frustum));
            output.Add("sphereContainsBox", (int)sphere.Contains(box));
            sphere.Contains(ref box, out containment);
            output.Add("sphereContainsBoxOut", (int)containment);
            output.Add("sphereContainsSphere", (int)sphere.Contains(otherSphere));
            sphere.Contains(ref otherSphere, out containment);
            output.Add("sphereContainsSphereOut", (int)containment);
            output.Add("sphereContainsPoint", (int)sphere.Contains(point));
            sphere.Contains(ref point, out containment);
            output.Add("sphereContainsPointOut", (int)containment);
            output.Add("sphereContainsFrustum", (int)sphere.Contains(frustum));
            output.Add("frustumContainsBox", (int)frustum.Contains(box));
            frustum.Contains(ref box, out containment);
            output.Add("frustumContainsBoxOut", (int)containment);
            output.Add("frustumContainsSphere", (int)frustum.Contains(sphere));
            frustum.Contains(ref sphere, out containment);
            output.Add("frustumContainsSphereOut", (int)containment);
            output.Add("frustumContainsPoint", (int)frustum.Contains(point));
            frustum.Contains(ref point, out containment);
            output.Add("frustumContainsPointOut", (int)containment);
            output.Add("boxIntersectsBox", box.Intersects(otherBox));
            box.Intersects(ref otherBox, out hit);
            output.Add("boxIntersectsBoxOut", hit);
            output.Add("boxIntersectsSphere", box.Intersects(sphere));
            box.Intersects(ref sphere, out hit);
            output.Add("boxIntersectsSphereOut", hit);
            output.Add("boxIntersectsPlane", (int)box.Intersects(plane));
            box.Intersects(ref plane, out side);
            output.Add("boxIntersectsPlaneOut", (int)side);
            GeometryDistance(output, "boxIntersectsRay", box.Intersects(ray));
            box.Intersects(ref ray, out distance);
            GeometryDistance(output, "boxIntersectsRayOut", distance);
            output.Add("boxIntersectsFrustum", box.Intersects(frustum));
            output.Add("sphereIntersectsBox", sphere.Intersects(box));
            sphere.Intersects(ref box, out hit);
            output.Add("sphereIntersectsBoxOut", hit);
            output.Add("sphereIntersectsSphere", sphere.Intersects(otherSphere));
            sphere.Intersects(ref otherSphere, out hit);
            output.Add("sphereIntersectsSphereOut", hit);
            output.Add("sphereIntersectsPlane", (int)sphere.Intersects(plane));
            sphere.Intersects(ref plane, out side);
            output.Add("sphereIntersectsPlaneOut", (int)side);
            GeometryDistance(output, "sphereIntersectsRay", sphere.Intersects(ray));
            sphere.Intersects(ref ray, out distance);
            GeometryDistance(output, "sphereIntersectsRayOut", distance);
            output.Add("sphereIntersectsFrustum", sphere.Intersects(frustum));
            GeometryDistance(output, "rayIntersectsBox", ray.Intersects(box));
            ray.Intersects(ref box, out distance);
            GeometryDistance(output, "rayIntersectsBoxOut", distance);
            GeometryDistance(output, "rayIntersectsSphere", ray.Intersects(sphere));
            ray.Intersects(ref sphere, out distance);
            GeometryDistance(output, "rayIntersectsSphereOut", distance);
            GeometryDistance(output, "rayIntersectsPlane", ray.Intersects(plane));
            ray.Intersects(ref plane, out distance);
            GeometryDistance(output, "rayIntersectsPlaneOut", distance);
            output.Add("planeIntersectsBox", (int)plane.Intersects(box));
            plane.Intersects(ref box, out side);
            output.Add("planeIntersectsBoxOut", (int)side);
            output.Add("planeIntersectsSphere", (int)plane.Intersects(sphere));
            plane.Intersects(ref sphere, out side);
            output.Add("planeIntersectsSphereOut", (int)side);
            output.Add("planeIntersectsFrustum", (int)plane.Intersects(frustum));
            output.Add("frustumIntersectsBox", frustum.Intersects(box));
            frustum.Intersects(ref box, out hit);
            output.Add("frustumIntersectsBoxOut", hit);
            output.Add("frustumIntersectsSphere", frustum.Intersects(sphere));
            frustum.Intersects(ref sphere, out hit);
            output.Add("frustumIntersectsSphereOut", hit);
            output.Add("frustumIntersectsPlane", (int)frustum.Intersects(plane));
            frustum.Intersects(ref plane, out side);
            output.Add("frustumIntersectsPlaneOut", (int)side);
            output.Add("mergedBox", GeometryBoxFields(BoundingBox.CreateMerged(box, otherBox)));
            BoundingBox.CreateMerged(ref box, ref otherBox, out BoundingBox boxResult);
            output.Add("mergedBoxOut", GeometryBoxFields(boxResult));
            output.Add("boxFromSphere", GeometryBoxFields(BoundingBox.CreateFromSphere(sphere)));
            BoundingBox.CreateFromSphere(ref sphere, out boxResult);
            output.Add("boxFromSphereOut", GeometryBoxFields(boxResult));
            output.Add("sphereFromBox", GeometrySphereFields(BoundingSphere.CreateFromBoundingBox(box)));
            BoundingSphere.CreateFromBoundingBox(ref box, out BoundingSphere sphereResult);
            output.Add("sphereFromBoxOut", GeometrySphereFields(sphereResult));
            output.Add("mergedSphere", GeometrySphereFields(BoundingSphere.CreateMerged(sphere, otherSphere)));
            BoundingSphere.CreateMerged(ref sphere, ref otherSphere, out sphereResult);
            output.Add("mergedSphereOut", GeometrySphereFields(sphereResult));
            return output;
        }

        private static JsonWriter GeometryPointSet(Vector3[] points)
        {
            var output = new JsonWriter().Add("points", GeometryCorners(points)).Add("count", points.Length);
            try { output.Add("box", GeometryBoxFields(BoundingBox.CreateFromPoints(points))).Add("boxException", "none"); }
            catch (ArgumentException) { output.Add("boxException", "ArgumentException"); }
            try { output.Add("sphere", GeometrySphereFields(BoundingSphere.CreateFromPoints(points))).Add("sphereException", "none"); }
            catch (ArgumentException) { output.Add("sphereException", "ArgumentException"); }
            return output;
        }

        private static JsonWriter GeometryFrustumCase(Matrix matrix)
        {
            var frustum = new BoundingFrustum(matrix);
            var other = new BoundingFrustum(Matrix.Identity);
            var output = new JsonWriter().Add("matrix", MatrixFields(matrix))
                .Add("contains", (int)frustum.Contains(other)).Add("reverseContains", (int)other.Contains(frustum))
                .Add("intersects", frustum.Intersects(other)).Add("reverseIntersects", other.Intersects(frustum))
                .Add("corners", GeometryCorners(frustum.GetCorners()))
                .Add("sphere", GeometrySphereFields(BoundingSphere.CreateFromFrustum(frustum)));
            var corners = new Vector3[8]; frustum.GetCorners(corners);
            output.Add("cornersOut", GeometryCorners(corners));
            return output;
        }

        public static JsonWriter Geometry()
        {
            var planes = new JsonWriter();
            Plane[] inputs = { new Plane(Vector3.Up, -2), new Plane(new Vector3(0, 2, 0), -4),
                new Plane(Vector3.Zero, 1), new Plane(new Vector3(0.8414709568023682f, 0.5403022766113281f, 0), -2),
                new Plane(new Vector3(1e20f, 0, 0), -1), new Plane(new Vector3(1e-30f, 0, 0), -1),
                new Plane(new Vector3(float.NaN, 0, 0), 1), new Plane(new Vector3(float.PositiveInfinity, 0, 0), 1) };
            string[] names = { "unit", "scaled", "zero", "nearUnit", "large", "tiny", "nan", "infinity" };
            var transform = new Matrix(2, 0, 0, 0, 0, 4, 0, 0, 0, 0, -1, 0, 7, 8, 9, 1);
            var rotation = new Quaternion(0.5f, -0.5f, 0.5f, -0.5f);
            for (int i = 0; i < inputs.Length; ++i)
                planes.Add(names[i], GeometryPlaneCase(inputs[i], new Vector3(1, -2, 3), transform, rotation));
            uint state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                var normal = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                float d = Next(ref state);
                planes.Add("seeded" + i, GeometryPlaneCase(new Plane(normal, d), new Vector3(1, -2, 3), transform, rotation));
            }
            planes.Add("singular", GeometryPlaneCase(inputs[0], Vector3.One, new Matrix(), rotation));

            var relations = new JsonWriter();
            var unitBox = new BoundingBox(new Vector3(-1), new Vector3(1));
            var unitSphere = new BoundingSphere(new Vector3(0, 0, 0.5f), 1);
            var unitPlane = new Plane(Vector3.Up, 0);
            relations.Add("inside", GeometryRelation(unitBox, unitSphere, new Ray(Vector3.Zero, Vector3.Right), unitPlane, Vector3.Zero));
            relations.Add("boundary", GeometryRelation(unitBox, new BoundingSphere(new Vector3(2, 0, 0.5f), 1), new Ray(Vector3.Right, Vector3.Right), unitPlane, Vector3.Right));
            relations.Add("disjoint", GeometryRelation(new BoundingBox(new Vector3(3), new Vector3(4)), new BoundingSphere(new Vector3(5), 1), new Ray(new Vector3(5), Vector3.Right), unitPlane, new Vector3(5)));
            relations.Add("zero", GeometryRelation(new BoundingBox(Vector3.Zero, Vector3.Zero), new BoundingSphere(Vector3.Zero, 0), new Ray(Vector3.Zero, Vector3.Zero), new Plane(Vector3.Zero, 0), Vector3.Zero));
            relations.Add("insideOut", GeometryRelation(new BoundingBox(Vector3.One, -Vector3.One), unitSphere, new Ray(Vector3.Zero, Vector3.Right), unitPlane, Vector3.Zero));
            relations.Add("negativeRadius", GeometryRelation(unitBox, NegativeGeometrySphere(), new Ray(Vector3.Zero, Vector3.Right), unitPlane, Vector3.Right));
            relations.Add("stationary", GeometryRelation(unitBox, unitSphere, new Ray(Vector3.Zero, Vector3.Zero), unitPlane, Vector3.Zero));
            relations.Add("parallelOutside", GeometryRelation(unitBox, unitSphere, new Ray(new Vector3(2, 0, 0), Vector3.Up), unitPlane, new Vector3(0, 0, -0.000005f)));
            float[] thresholds = { 0.0000005f, 0.000001f, 0.000002f, 0.000009f, 0.00001f, 0.000011f };
            for (int i = 0; i < thresholds.Length; ++i)
                relations.Add("threshold" + i, GeometryRelation(unitBox, unitSphere,
                    new Ray(new Vector3(-2, 0, 0), new Vector3(thresholds[i], 0, 0)), new Plane(Vector3.Right, 1), Vector3.Right));
            state = Seed;
            for (int i = 0; i < 6; ++i)
            {
                var center = new Vector3(Next(ref state) / 8, Next(ref state) / 8, Next(ref state) / 8);
                float radius = Math.Abs(Next(ref state)) / 8;
                var extent = new Vector3(0.25f, 0.5f, 0.75f);
                relations.Add("seeded" + i, GeometryRelation(new BoundingBox(center - extent, center + extent),
                    new BoundingSphere(center, radius), new Ray(center, Vector3.Right), unitPlane, center));
            }

            var sets = new JsonWriter();
            sets.Add("empty", GeometryPointSet(new Vector3[] {}));
            sets.Add("single", GeometryPointSet(new[] { new Vector3(1, 2, 3) }));
            sets.Add("repeated", GeometryPointSet(new[] { Vector3.One, Vector3.One, Vector3.One }));
            sets.Add("pair", GeometryPointSet(new[] { new Vector3(-2, 0, 0), new Vector3(2, 0, 0) }));
            sets.Add("triangleTie", GeometryPointSet(new[] { Vector3.Zero, Vector3.Right, Vector3.Up }));
            sets.Add("largePair", GeometryPointSet(new[] { new Vector3(-1e20f, 0, 0), new Vector3(1e20f, 0, 0) }));
            sets.Add("tinyPair", GeometryPointSet(new[] { new Vector3(-1e-30f, 0, 0), new Vector3(1e-30f, 0, 0) }));
            state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                var points = new Vector3[5];
                for (int j = 0; j < points.Length; ++j)
                    points[j] = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                sets.Add("seeded" + i, GeometryPointSet(points));
            }
            var frustums = new JsonWriter();
            frustums.Add("identity", GeometryFrustumCase(Matrix.Identity));
            frustums.Add("touching", GeometryFrustumCase(new Matrix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, 0, 0, 1)));
            frustums.Add("separated", GeometryFrustumCase(new Matrix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -3, 0, 0, 1)));
            frustums.Add("enclosing", GeometryFrustumCase(new Matrix(0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1)));
            frustums.Add("enclosed", GeometryFrustumCase(new Matrix(2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1)));
            return new JsonWriter().Add("seed", (double)Seed).Add("planes", planes).Add("relations", relations)
                .Add("pointSets", sets).Add("frustums", frustums);
        }

        private static BoundingSphere NegativeGeometrySphere()
        {
            var result = new BoundingSphere(Vector3.Zero, 1);
            result.Radius = -1;
            return result;
        }
    }
}
