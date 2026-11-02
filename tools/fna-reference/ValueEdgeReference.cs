// SPDX-License-Identifier: MS-PL
using System;
using System.Globalization;
using Microsoft.Xna.Framework;

namespace CNA.FnaReference
{
    internal static class ValueEdgeReference
    {
        private const uint Seed = 0xC0FFEE03;

        private static float Next(ref uint state)
        {
            state = unchecked(state * 1664525u + 1013904223u);
            return ((int)((state >> 16) % 257) - 128) / 8.0f;
        }

        private static void Scalar(JsonWriter output, string key, float value)
        {
            if (float.IsNaN(value)) output.Add(key, "nan");
            else if (float.IsPositiveInfinity(value)) output.Add(key, "+inf");
            else if (float.IsNegativeInfinity(value)) output.Add(key, "-inf");
            else if (value == 0) output.Add(key, BitConverter.SingleToInt32Bits(value) < 0 ? "-zero" : "+zero");
            else output.Add(key, (double)value);
        }

        private static JsonWriter Vector(Vector2 value)
        {
            var output = new JsonWriter();
            Scalar(output, "X", value.X);
            Scalar(output, "Y", value.Y);
            return output;
        }

        private static JsonWriter VectorCase(Vector2 value, Vector2 divisor)
        {
            var min = new Vector2(-2, -2);
            var max = new Vector2(2, 2);
            var output = new JsonWriter().Add("input", Vector(value)).Add("divisor", Vector(divisor));
            output.Add("divide", Vector(Vector2.Divide(value, divisor)));
            Vector2.Divide(ref value, ref divisor, out Vector2 result);
            output.Add("divideOut", Vector(result));
            output.Add("divideOperator", Vector(value / divisor));
            output.Add("scalarDivide", Vector(Vector2.Divide(value, divisor.X)));
            Vector2.Divide(ref value, divisor.X, out result);
            output.Add("scalarDivideOut", Vector(result));
            output.Add("scalarDivideOperator", Vector(value / divisor.X));
            output.Add("clamp", Vector(Vector2.Clamp(value, min, max)));
            Vector2.Clamp(ref value, ref min, ref max, out result);
            output.Add("clampOut", Vector(result));
            output.Add("reversedClamp", Vector(Vector2.Clamp(value, max, min)));
            Vector2.Clamp(ref value, ref max, ref min, out result);
            output.Add("reversedClampOut", Vector(result));
            var copy = new Vector2(value.X, value.Y);
            output.Add("equalsCopy", value.Equals(copy)).Add("operatorEqualCopy", value == copy)
                .Add("operatorUnequalCopy", value != copy)
                .Add("copyHashEqual", value.GetHashCode() == copy.GetHashCode());
            return output;
        }

        public static JsonWriter Vectors()
        {
            var cases = new JsonWriter()
                .Add("zero", VectorCase(Vector2.Zero, new Vector2(0, -0.0f)))
                .Add("signedZero", VectorCase(new Vector2(-0.0f, 0), new Vector2(2, -2)))
                .Add("boundaries", VectorCase(new Vector2(-2, 2), new Vector2(-1, 1)))
                .Add("outside", VectorCase(new Vector2(-2.125f, 2.125f), new Vector2(0, -0.0f)))
                .Add("infinity", VectorCase(new Vector2(float.PositiveInfinity, float.NegativeInfinity), new Vector2(2, -2)))
                .Add("nan", VectorCase(new Vector2(float.NaN, 1), new Vector2(1, float.NaN)))
                .Add("max", VectorCase(new Vector2(float.MaxValue, -float.MaxValue), new Vector2(0.5f, 2)))
                .Add("subnormal", VectorCase(new Vector2(float.Epsilon, -float.Epsilon), new Vector2(2, 2)));
            uint state = Seed;
            for (int i = 0; i < 8; ++i)
            {
                var value = new Vector2(Next(ref state), Next(ref state));
                var divisor = new Vector2(Next(ref state), Next(ref state));
                cases.Add("seeded" + i, VectorCase(value, divisor));
            }
            var magnitudes = new JsonWriter();
            var inputs = new[] { Vector2.Zero, new Vector2(3, 4), new Vector2(1e20f, 0),
                new Vector2(1e-30f, 0), new Vector2(float.PositiveInfinity, 0), new Vector2(float.NaN, 0) };
            var names = new[] { "zero", "threeFour", "large", "tiny", "infinity", "nan" };
            for (int i = 0; i < inputs.Length; ++i)
            {
                var value = inputs[i];
                var item = new JsonWriter().Add("input", Vector(value));
                Scalar(item, "length", value.Length());
                Scalar(item, "lengthSquared", value.LengthSquared());
                item.Add("normalize", Vector(Vector2.Normalize(value)));
                Vector2.Normalize(ref value, out Vector2 result);
                item.Add("normalizeOut", Vector(result));
                value.Normalize();
                item.Add("normalizeInstance", Vector(value));
                magnitudes.Add(names[i], item);
            }
            var a = new Vector2(1.5f, -2.25f);
            var b = new Vector2(1.5f, 2.25f);
            // Only these short, exactly representable decimals are claimed for formatting.
            CultureInfo previous = CultureInfo.CurrentCulture;
            string format;
            try { CultureInfo.CurrentCulture = CultureInfo.InvariantCulture; format = a.ToString(); }
            finally { CultureInfo.CurrentCulture = previous; }
            return new JsonWriter().Add("seed", (double)Seed).Add("arithmetic", cases)
                .Add("magnitude", magnitudes).Add("format", format)
                .Add("unequalEquals", a.Equals(b)).Add("unequalOperator", a == b).Add("unequalNotOperator", a != b)
                .Add("signedZeroEquals", new Vector2(-0.0f, 0).Equals(Vector2.Zero))
                .Add("signedZeroHashEqual", new Vector2(-0.0f, 0).GetHashCode() == Vector2.Zero.GetHashCode());
        }

        private static JsonWriter MatrixFields(Matrix matrix)
        {
            var output = new JsonWriter();
            float[] values = { matrix.M11, matrix.M12, matrix.M13, matrix.M14,
                matrix.M21, matrix.M22, matrix.M23, matrix.M24, matrix.M31, matrix.M32, matrix.M33, matrix.M34,
                matrix.M41, matrix.M42, matrix.M43, matrix.M44 };
            for (int i = 0; i < values.Length; ++i) Scalar(output, "M" + (i / 4 + 1) + (i % 4 + 1), values[i]);
            return output;
        }

        private static void Project(int kind, float a, float b, float near, float far, out Matrix result)
        {
            if (kind == 0) Matrix.CreatePerspective(a, b, near, far, out result);
            else if (kind == 1) Matrix.CreatePerspectiveFieldOfView(a, b, near, far, out result);
            else Matrix.CreatePerspectiveOffCenter(-a, a, -b, b, near, far, out result);
        }

        private static Matrix Project(int kind, float a, float b, float near, float far)
        {
            if (kind == 0) return Matrix.CreatePerspective(a, b, near, far);
            if (kind == 1) return Matrix.CreatePerspectiveFieldOfView(a, b, near, far);
            return Matrix.CreatePerspectiveOffCenter(-a, a, -b, b, near, far);
        }

        private static JsonWriter ProjectionCase(int kind, float a, float b, float near, float far)
        {
            var output = new JsonWriter();
            Scalar(output, "a", a); Scalar(output, "b", b); Scalar(output, "near", near); Scalar(output, "far", far);
            try { output.Add("value", MatrixFields(Project(kind, a, b, near, far))).Add("valueException", "none"); }
            catch (ArgumentException error) { output.Add("valueException", error.GetType().Name); }
            Matrix result = Matrix.CreateTranslation(7, 8, 9);
            try { Project(kind, a, b, near, far, out result); output.Add("outException", "none"); }
            catch (ArgumentException error) { output.Add("outException", error.GetType().Name); }
            output.Add("out", MatrixFields(result));
            return output;
        }

        public static JsonWriter Projections()
        {
            var output = new JsonWriter().Add("seed", (double)Seed);
            string[] kinds = { "size", "fov", "offCenter" };
            for (int kind = 0; kind < kinds.Length; ++kind)
            {
                var cases = new JsonWriter()
                    .Add("ordinary", ProjectionCase(kind, 1, 2, 1, 2))
                    .Add("zeroDimension", ProjectionCase(kind, 0, 0, 1, 2))
                    .Add("negativeDimension", ProjectionCase(kind, -1, -2, 1, 2))
                    .Add("nearZero", ProjectionCase(kind, 1, 2, 0, 2))
                    .Add("farZero", ProjectionCase(kind, 1, 2, 1, 0))
                    .Add("equalDepth", ProjectionCase(kind, 1, 2, 2, 2))
                    .Add("reversedDepth", ProjectionCase(kind, 1, 2, 3, 2))
                    .Add("bothZero", ProjectionCase(kind, 1, 2, 0, 0))
                    .Add("nanNear", ProjectionCase(kind, 1, 2, float.NaN, 2))
                    .Add("nanFar", ProjectionCase(kind, 1, 2, 1, float.NaN))
                    .Add("infiniteFar", ProjectionCase(kind, 1, 2, 1, float.PositiveInfinity))
                    .Add("nanDimension", ProjectionCase(kind, float.NaN, 2, 1, 2));
                uint state = Seed;
                for (int i = 0; i < 4; ++i)
                {
                    float near = Math.Abs(Next(ref state)) + 1;
                    float far = near + Math.Abs(Next(ref state)) + 1;
                    cases.Add("seeded" + i, ProjectionCase(kind, 1, 2, near, far));
                }
                if (kind == 1)
                {
                    cases.Add("pi", ProjectionCase(kind, MathHelper.Pi, 1, 1, 2));
                    cases.Add("belowPi", ProjectionCase(kind, MathF.BitDecrement(MathHelper.Pi), 1, 1, 2));
                    cases.Add("fnaPi", ProjectionCase(kind, 3.141593f, 1, 1, 2));
                    cases.Add("infiniteAngle", ProjectionCase(kind, float.PositiveInfinity, 1, 1, 2));
                }
                output.Add(kinds[kind], cases);
            }
            return output;
        }
    }
}
