// SPDX-License-Identifier: MS-PL
using System;
using Microsoft.Xna.Framework;

namespace CNA.FnaReference
{
    internal static partial class ValueEdgeReference
    {
        private static JsonWriter MatrixCamera(Vector3 position, Vector3 target, Vector3 up,
            Vector3? cameraForward, Vector3? objectForward)
        {
            var output = new JsonWriter().Add("position", Vector(position)).Add("target", Vector(target))
                .Add("up", Vector(up)).Add("hasCameraForward", cameraForward.HasValue)
                .Add("hasObjectForward", objectForward.HasValue)
                .Add("cameraForward", Vector(cameraForward.GetValueOrDefault()))
                .Add("objectForward", Vector(objectForward.GetValueOrDefault()));
            output.Add("lookAt", MatrixFields(Matrix.CreateLookAt(position, target, up)));
            Matrix.CreateLookAt(ref position, ref target, ref up, out Matrix result);
            output.Add("lookAtOut", MatrixFields(result));
            output.Add("world", MatrixFields(Matrix.CreateWorld(position, target, up)));
            Matrix.CreateWorld(ref position, ref target, ref up, out result);
            output.Add("worldOut", MatrixFields(result));
            output.Add("billboard", MatrixFields(Matrix.CreateBillboard(position, target, up, cameraForward)));
            Matrix.CreateBillboard(ref position, ref target, ref up, cameraForward, out result);
            output.Add("billboardOut", MatrixFields(result));
            output.Add("constrained", MatrixFields(Matrix.CreateConstrainedBillboard(position, target, up, cameraForward, objectForward)));
            Matrix.CreateConstrainedBillboard(ref position, ref target, ref up, cameraForward, objectForward, out result);
            output.Add("constrainedOut", MatrixFields(result));
            return output;
        }

        private static JsonWriter MatrixRotation(Vector3 axis, float angle, float pitch, float roll, Quaternion quaternion)
        {
            var output = new JsonWriter().Add("axis", Vector(axis)).Add("quaternion", Rotation(quaternion));
            Scalar(output, "angle", angle); Scalar(output, "pitch", pitch); Scalar(output, "roll", roll);
            output.Add("axisAngle", MatrixFields(Matrix.CreateFromAxisAngle(axis, angle)));
            Matrix.CreateFromAxisAngle(ref axis, angle, out Matrix result);
            output.Add("axisAngleOut", MatrixFields(result));
            output.Add("quaternionValue", MatrixFields(Matrix.CreateFromQuaternion(quaternion)));
            Matrix.CreateFromQuaternion(ref quaternion, out result);
            output.Add("quaternionOut", MatrixFields(result));
            output.Add("yawPitchRoll", MatrixFields(Matrix.CreateFromYawPitchRoll(angle, pitch, roll)));
            Matrix.CreateFromYawPitchRoll(angle, pitch, roll, out result);
            output.Add("yawPitchRollOut", MatrixFields(result));
            return output;
        }

        private static JsonWriter MatrixPlane(Plane plane, Vector3 light)
        {
            var output = new JsonWriter().Add("normal", Vector(plane.Normal)).Add("light", Vector(light));
            Scalar(output, "d", plane.D);
            output.Add("reflection", MatrixFields(Matrix.CreateReflection(plane)));
            Matrix.CreateReflection(ref plane, out Matrix result);
            output.Add("reflectionOut", MatrixFields(result));
            output.Add("shadow", MatrixFields(Matrix.CreateShadow(light, plane)));
            Matrix.CreateShadow(ref light, ref plane, out result);
            output.Add("shadowOut", MatrixFields(result));
            return output;
        }

        private static JsonWriter MatrixAlgebra(Matrix value, Matrix other, float amount)
        {
            var output = new JsonWriter().Add("input", MatrixFields(value)).Add("other", MatrixFields(other));
            Scalar(output, "amount", amount); Scalar(output, "determinant", value.Determinant());
            output.Add("inverse", MatrixFields(Matrix.Invert(value)));
            Matrix.Invert(ref value, out Matrix result);
            output.Add("inverseOut", MatrixFields(result));
            var copy = value;
            Matrix.Invert(ref copy, out copy);
            output.Add("inverseAliased", MatrixFields(copy));
            output.Add("transpose", MatrixFields(Matrix.Transpose(value)));
            Matrix.Transpose(ref value, out result);
            output.Add("transposeOut", MatrixFields(result));
            copy = value;
            Matrix.Transpose(ref copy, out copy);
            output.Add("transposeAliased", MatrixFields(copy));
            output.Add("lerp", MatrixFields(Matrix.Lerp(value, other, amount)));
            Matrix.Lerp(ref value, ref other, amount, out result);
            output.Add("lerpOut", MatrixFields(result));
            copy = value;
            Matrix.Lerp(ref copy, ref other, amount, out copy);
            output.Add("lerpAliasedFirst", MatrixFields(copy));
            copy = other;
            Matrix.Lerp(ref value, ref copy, amount, out copy);
            output.Add("lerpAliasedSecond", MatrixFields(copy));
            return output;
        }

        private static JsonWriter MatrixDecomposition(Matrix value)
        {
            var output = new JsonWriter().Add("input", MatrixFields(value));
            var scale = new Vector3(11, 12, 13);
            var translation = new Vector3(21, 22, 23);
            var rotation = new Quaternion(31, 32, 33, 34);
            bool accepted = false;
            string exception = "none";
            try { accepted = value.Decompose(out scale, out rotation, out translation); }
            catch (Exception error) { exception = error.GetType().Name; }
            return output.Add("accepted", accepted).Add("exception", exception).Add("scale", Vector(scale))
                .Add("rotation", Rotation(rotation)).Add("translation", Vector(translation));
        }

        public static JsonWriter Matrices()
        {
            var cameras = new JsonWriter();
            cameras.Add("ordinary", MatrixCamera(new Vector3(0, 0, 5), Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("coincident", MatrixCamera(Vector3.Zero, Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("explicitForward", MatrixCamera(Vector3.Zero, Vector3.Zero, Vector3.Up, Vector3.Forward, Vector3.Right));
            cameras.Add("nonunitForward", MatrixCamera(Vector3.Zero, Vector3.Zero, Vector3.Up, new Vector3(0, 0, -2), Vector3.Up));
            cameras.Add("zeroUp", MatrixCamera(Vector3.One, Vector3.Forward, Vector3.Zero, null, null));
            cameras.Add("parallelUp", MatrixCamera(Vector3.Up, Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("parallelForwards", MatrixCamera(Vector3.Up, Vector3.Zero, Vector3.Up, Vector3.Up, Vector3.Up));
            cameras.Add("forwardAxis", MatrixCamera(Vector3.Forward, Vector3.Zero, Vector3.Forward, null, null));
            cameras.Add("belowDistance", MatrixCamera(new Vector3(0, 0, 0.009999f), Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("aboveDistance", MatrixCamera(new Vector3(0, 0, 0.010001f), Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("belowParallel", MatrixCamera(new Vector3(0.06f, 0.998f, 0), Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("aboveParallel", MatrixCamera(new Vector3(0.05f, 0.999f, 0), Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("large", MatrixCamera(new Vector3(1e20f, 0, 0), Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("tiny", MatrixCamera(new Vector3(1e-30f, 0, 0), Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("nan", MatrixCamera(new Vector3(float.NaN, 0, 0), Vector3.Zero, Vector3.Up, null, null));
            cameras.Add("infinity", MatrixCamera(new Vector3(float.PositiveInfinity, 0, 0), Vector3.Zero, Vector3.Up, null, null));
            uint state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                var position = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                var target = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                var up = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                cameras.Add("seeded" + i, MatrixCamera(position, target, up, Vector3.Forward, Vector3.Right));
            }

            var rotations = new JsonWriter();
            float[] angles = { 0, MathHelper.PiOver2, MathHelper.Pi, -MathHelper.PiOver2, float.NaN, float.PositiveInfinity };
            for (int i = 0; i < angles.Length; ++i)
                rotations.Add("angle" + i, MatrixRotation(Vector3.UnitY, angles[i], 0, 0, Quaternion.Identity));
            rotations.Add("zero", MatrixRotation(Vector3.Zero, 0.5f, 0, 0, new Quaternion()));
            rotations.Add("nonunit", MatrixRotation(new Vector3(2, -1, 0.5f), 0.390625f, 0.5f, -0.25f, new Quaternion(1, 2, 3, 4)));
            rotations.Add("large", MatrixRotation(new Vector3(1e20f, 0, 0), 0.5f, 0, 0, new Quaternion(1e20f, 0, 0, 0)));
            rotations.Add("tiny", MatrixRotation(new Vector3(1e-30f, 0, 0), 0.5f, 0, 0, new Quaternion(1e-30f, 0, 0, 0)));
            state = Seed;
            for (int i = 0; i < 8; ++i)
            {
                float angle = Next(ref state) / 8, pitch = Next(ref state) / 8, roll = Next(ref state) / 8;
                rotations.Add("seeded" + i, MatrixRotation(Vector3.UnitX, angle, pitch, roll, new Quaternion(0.5f, -0.5f, 0.5f, -0.5f)));
            }

            var planes = new JsonWriter();
            Plane[] planeInputs = { new Plane(Vector3.Up, -2), new Plane(new Vector3(0, 2, 0), -4),
                new Plane(Vector3.Zero, 1), new Plane(new Vector3(0, 1.0000001f, 0), -2),
                new Plane(new Vector3(1e20f, 0, 0), -1), new Plane(new Vector3(1e-30f, 0, 0), -1),
                new Plane(new Vector3(float.NaN, 0, 0), 1), new Plane(new Vector3(float.PositiveInfinity, 0, 0), 1) };
            string[] planeNames = { "unit", "nonunit", "zero", "nearUnit", "large", "tiny", "nan", "infinity" };
            for (int i = 0; i < planeInputs.Length; ++i)
                planes.Add(planeNames[i], MatrixPlane(planeInputs[i], new Vector3(1, -2, 3)));
            planes.Add("parallelLight", MatrixPlane(new Plane(Vector3.Up, -2), Vector3.Right));
            planes.Add("zeroLight", MatrixPlane(new Plane(Vector3.Up, -2), Vector3.Zero));
            state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                var normal = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                float d = Next(ref state);
                planes.Add("seeded" + i, MatrixPlane(new Plane(normal, d), new Vector3(1, -2, 3)));
            }

            var algebra = new JsonWriter();
            var identity = Matrix.Identity;
            Matrix[] matrices = { identity, new Matrix(), Matrix.CreateScale(1, 0, 1),
                Matrix.CreateScale(1e-30f, 1, 1), Matrix.CreateScale(1e20f, 1e20f, 1e20f),
                new Matrix(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16),
                new Matrix(1, 1, 0, 0, 1, 1.0000001f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1),
                new Matrix(-0.0f, 2, 3, 4, 5, float.NaN, 7, 8, 9, 10, float.PositiveInfinity, 12, 13, 14, 15, 16) };
            string[] names = { "identity", "zero", "singular", "tiny", "large", "sequential", "nearSingular", "ieee" };
            for (int i = 0; i < matrices.Length; ++i)
                algebra.Add(names[i], MatrixAlgebra(matrices[i], identity, 0.5f));
            float[] amounts = { -1, 0, 1, 2, float.NaN, float.PositiveInfinity };
            for (int i = 0; i < amounts.Length; ++i)
                algebra.Add("amount" + i, MatrixAlgebra(matrices[5], identity, amounts[i]));
            state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                var matrix = new Matrix(Next(ref state), Next(ref state), Next(ref state), Next(ref state),
                    Next(ref state), Next(ref state), Next(ref state), Next(ref state),
                    Next(ref state), Next(ref state), Next(ref state), Next(ref state),
                    Next(ref state), Next(ref state), Next(ref state), Next(ref state));
                algebra.Add("seeded" + i, MatrixAlgebra(matrix, identity, 0.25f));
            }

            var decomposition = new JsonWriter();
            Vector3[] scales = { Vector3.One, new Vector3(-2, 3, 4), new Vector3(-1, 1, 1),
                new Vector3(-2, -3, 4), new Vector3(1, 0, 1), Vector3.Zero, new Vector3(1e-5f, 1, 1),
                new Vector3(1e-4f, 1, 1), new Vector3(1e20f, 1, 1), new Vector3(float.NaN, 1, 1),
                new Vector3(float.PositiveInfinity, 1, 1) };
            string[] scaleNames = { "unit", "negative", "negativeTie", "twoNegative", "zeroAxis", "allZero", "belowEpsilon", "atEpsilon", "large", "nan", "infinity" };
            for (int i = 0; i < scales.Length; ++i)
            {
                var matrix = Matrix.CreateScale(scales[i]);
                matrix.M41 = 7; matrix.M42 = 8; matrix.M43 = 9;
                decomposition.Add(scaleNames[i], MatrixDecomposition(matrix));
            }
            var shear = identity; shear.M12 = 0.5f;
            decomposition.Add("shear", MatrixDecomposition(shear));
            var fourth = identity; fourth.M14 = 3; fourth.M24 = -4; fourth.M34 = 5; fourth.M44 = 7;
            decomposition.Add("fourthColumn", MatrixDecomposition(fourth));
            state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                float x = Next(ref state), y = Next(ref state), z = Next(ref state);
                var matrix = Matrix.CreateScale(x, y, z);
                matrix.M41 = 7; matrix.M42 = 8; matrix.M43 = 9;
                decomposition.Add("seeded" + i, MatrixDecomposition(matrix));
            }
            return new JsonWriter().Add("seed", (double)Seed).Add("cameras", cameras).Add("rotations", rotations)
                .Add("planes", planes).Add("algebra", algebra).Add("decomposition", decomposition);
        }
    }
}
