// SPDX-License-Identifier: MS-PL
using System;
using Microsoft.Xna.Framework;

namespace CNA.FnaReference
{
    internal static partial class ValueEdgeReference
    {
        private static JsonWriter Rotation(Quaternion value)
        {
            var output = new JsonWriter();
            Scalar(output, "X", value.X); Scalar(output, "Y", value.Y);
            Scalar(output, "Z", value.Z); Scalar(output, "W", value.W);
            return output;
        }

        private static JsonWriter QuaternionCase(Quaternion value)
        {
            var output = new JsonWriter().Add("input", Rotation(value));
            Scalar(output, "length", value.Length());
            Scalar(output, "lengthSquared", value.LengthSquared());
            output.Add("normalize", Rotation(Quaternion.Normalize(value)));
            Quaternion.Normalize(ref value, out Quaternion result);
            output.Add("normalizeOut", Rotation(result));
            var copy = value;
            copy.Normalize();
            output.Add("normalizeInstance", Rotation(copy));
            output.Add("inverse", Rotation(Quaternion.Inverse(value)));
            Quaternion.Inverse(ref value, out result);
            output.Add("inverseOut", Rotation(result));
            output.Add("conjugate", Rotation(Quaternion.Conjugate(value)));
            Quaternion.Conjugate(ref value, out result);
            output.Add("conjugateOut", Rotation(result));
            copy = value;
            copy.Conjugate();
            output.Add("conjugateInstance", Rotation(copy));
            return output;
        }

        private static JsonWriter QuaternionPair(Quaternion a, Quaternion b, float amount)
        {
            var output = new JsonWriter().Add("a", Rotation(a)).Add("b", Rotation(b));
            Scalar(output, "amount", amount);
            output.Add("lerp", Rotation(Quaternion.Lerp(a, b, amount)));
            Quaternion.Lerp(ref a, ref b, amount, out Quaternion result);
            output.Add("lerpOut", Rotation(result));
            output.Add("slerp", Rotation(Quaternion.Slerp(a, b, amount)));
            Quaternion.Slerp(ref a, ref b, amount, out result);
            output.Add("slerpOut", Rotation(result));
            output.Add("concatenate", Rotation(Quaternion.Concatenate(a, b)));
            Quaternion.Concatenate(ref a, ref b, out result);
            output.Add("concatenateOut", Rotation(result));
            return output;
        }

        private static JsonWriter QuaternionFactory(Vector3 axis, float angle, float pitch, float roll)
        {
            var output = new JsonWriter().Add("axis", Vector(axis));
            Scalar(output, "angle", angle); Scalar(output, "pitch", pitch); Scalar(output, "roll", roll);
            output.Add("axisAngle", Rotation(Quaternion.CreateFromAxisAngle(axis, angle)));
            Quaternion.CreateFromAxisAngle(ref axis, angle, out Quaternion result);
            output.Add("axisAngleOut", Rotation(result));
            output.Add("yawPitchRoll", Rotation(Quaternion.CreateFromYawPitchRoll(angle, pitch, roll)));
            Quaternion.CreateFromYawPitchRoll(angle, pitch, roll, out result);
            output.Add("yawPitchRollOut", Rotation(result));
            return output;
        }

        public static JsonWriter Quaternions()
        {
            var unary = new JsonWriter();
            var inputs = new[] { new Quaternion(), Quaternion.Identity, new Quaternion(1, 2, 3, 4),
                new Quaternion(1e20f, 0, 0, 0), new Quaternion(1e-30f, 0, 0, 0),
                new Quaternion(float.NaN, 0, 0, 0), new Quaternion(float.PositiveInfinity, 0, 0, 0) };
            string[] names = { "zero", "identity", "nonunit", "large", "tiny", "nan", "infinity" };
            for (int i = 0; i < inputs.Length; ++i) unary.Add(names[i], QuaternionCase(inputs[i]));
            uint state = Seed;
            for (int i = 0; i < 4; ++i)
                unary.Add("seeded" + i, QuaternionCase(new Quaternion(
                    Next(ref state), Next(ref state), Next(ref state), Next(ref state))));

            var pairs = new JsonWriter();
            var identity = Quaternion.Identity;
            var halfTurn = new Quaternion(0, 0, 1, 0);
            pairs.Add("identical", QuaternionPair(identity, identity, 0.5f));
            pairs.Add("opposite", QuaternionPair(identity, new Quaternion(0, 0, 0, -1), 0.5f));
            pairs.Add("zero", QuaternionPair(new Quaternion(), new Quaternion(), 0.5f));
            pairs.Add("nearThreshold", QuaternionPair(identity, new Quaternion(0, 0, 0, 0.999999f), 0.5f));
            float[] amounts = { -1, 0, 0.5f, 1, 2, float.NaN, float.PositiveInfinity };
            for (int i = 0; i < amounts.Length; ++i)
                pairs.Add("orthogonal" + i, QuaternionPair(identity, halfTurn, amounts[i]));
            state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                float amount = Next(ref state) / 8;
                pairs.Add("seeded" + i, QuaternionPair(new Quaternion(0.5f, 0.5f, 0.5f, 0.5f),
                    new Quaternion(0.5f, -0.5f, 0.5f, -0.5f), amount));
            }

            var factories = new JsonWriter();
            float[] angles = { 0, MathHelper.PiOver2, MathHelper.Pi, -MathHelper.PiOver2, float.NaN, float.PositiveInfinity };
            for (int i = 0; i < angles.Length; ++i)
                factories.Add("angle" + i, QuaternionFactory(Vector3.UnitY, angles[i], 0, 0));
            factories.Add("zeroAxis", QuaternionFactory(Vector3.Zero, MathHelper.PiOver2, 0, 0));
            factories.Add("nonunitAxis", QuaternionFactory(new Vector3(2, -1, 0.5f), MathHelper.PiOver2, 0.5f, -0.25f));
            state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                float yaw = Next(ref state) / 8, pitch = Next(ref state) / 8, roll = Next(ref state) / 8;
                factories.Add("seeded" + i, QuaternionFactory(Vector3.UnitX, yaw, pitch, roll));
            }

            var matrices = new JsonWriter();
            var rotations = new[] { Matrix.Identity,
                new Matrix(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1),
                new Matrix(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1),
                new Matrix(-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1), new Matrix() };
            string[] matrixNames = { "identity", "xHalfTurn", "yHalfTurn", "zHalfTurn", "zero" };
            for (int i = 0; i < rotations.Length; ++i)
            {
                var matrix = rotations[i];
                var item = new JsonWriter().Add("input", MatrixFields(matrix))
                    .Add("value", Rotation(Quaternion.CreateFromRotationMatrix(matrix)));
                Quaternion.CreateFromRotationMatrix(ref matrix, out Quaternion result);
                item.Add("out", Rotation(result));
                matrices.Add(matrixNames[i], item);
            }
            return new JsonWriter().Add("seed", (double)Seed).Add("unary", unary).Add("pairs", pairs)
                .Add("factories", factories).Add("matrices", matrices);
        }
    }
}
