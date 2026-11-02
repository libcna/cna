// SPDX-License-Identifier: MS-PL
using System;
using System.Globalization;
using Microsoft.Xna.Framework;

namespace CNA.FnaReference
{
    internal static partial class ValueEdgeReference
    {
        private static JsonWriter Vector(Vector3 value)
        {
            var output = new JsonWriter();
            Scalar(output, "X", value.X);
            Scalar(output, "Y", value.Y);
            Scalar(output, "Z", value.Z);
            return output;
        }

        private static JsonWriter VectorCase(Vector3 value, Vector3 divisor)
        {
            var min = new Vector3(-2, -2, -2);
            var max = new Vector3(2, 2, 2);
            var output = new JsonWriter().Add("input", Vector(value)).Add("divisor", Vector(divisor));
            output.Add("divide", Vector(Vector3.Divide(value, divisor)));
            Vector3.Divide(ref value, ref divisor, out Vector3 result);
            output.Add("divideOut", Vector(result));
            output.Add("divideOperator", Vector(value / divisor));
            output.Add("scalarDivide", Vector(Vector3.Divide(value, divisor.X)));
            Vector3.Divide(ref value, divisor.X, out result);
            output.Add("scalarDivideOut", Vector(result));
            output.Add("scalarDivideOperator", Vector(value / divisor.X));
            output.Add("clamp", Vector(Vector3.Clamp(value, min, max)));
            Vector3.Clamp(ref value, ref min, ref max, out result);
            output.Add("clampOut", Vector(result));
            output.Add("reversedClamp", Vector(Vector3.Clamp(value, max, min)));
            Vector3.Clamp(ref value, ref max, ref min, out result);
            output.Add("reversedClampOut", Vector(result));
            var copy = new Vector3(value.X, value.Y, value.Z);
            output.Add("equalsCopy", value.Equals(copy)).Add("operatorEqualCopy", value == copy)
                .Add("operatorUnequalCopy", value != copy)
                .Add("copyHashEqual", value.GetHashCode() == copy.GetHashCode());
            return output;
        }

        private static JsonWriter SpatialCase(Vector3 a, Vector3 b, Vector3 c, Vector3 d, float amount)
        {
            var output = new JsonWriter().Add("a", Vector(a)).Add("b", Vector(b))
                .Add("c", Vector(c)).Add("d", Vector(d));
            Scalar(output, "amount", amount);
            Vector3 result;
            output.Add("lerp", Vector(Vector3.Lerp(a, b, amount)));
            Vector3.Lerp(ref a, ref b, amount, out result);
            output.Add("lerpOut", Vector(result));
            output.Add("smoothStep", Vector(Vector3.SmoothStep(a, b, amount)));
            Vector3.SmoothStep(ref a, ref b, amount, out result);
            output.Add("smoothStepOut", Vector(result));
            output.Add("hermite", Vector(Vector3.Hermite(a, b, c, d, amount)));
            Vector3.Hermite(ref a, ref b, ref c, ref d, amount, out result);
            output.Add("hermiteOut", Vector(result));
            output.Add("catmullRom", Vector(Vector3.CatmullRom(a, b, c, d, amount)));
            Vector3.CatmullRom(ref a, ref b, ref c, ref d, amount, out result);
            output.Add("catmullRomOut", Vector(result));
            output.Add("barycentric", Vector(Vector3.Barycentric(a, b, c, amount, 0.25f)));
            Vector3.Barycentric(ref a, ref b, ref c, amount, 0.25f, out result);
            output.Add("barycentricOut", Vector(result));
            output.Add("cross", Vector(Vector3.Cross(a, b)));
            Vector3.Cross(ref a, ref b, out result);
            output.Add("crossOut", Vector(result));
            output.Add("reflect", Vector(Vector3.Reflect(a, b)));
            Vector3.Reflect(ref a, ref b, out result);
            output.Add("reflectOut", Vector(result));
            var matrix = new Matrix(2, 0, 1, 0.5f, 0, -3, 0, 0, 1, 0, 4, 0, 7, 8, 9, 1);
            var rotation = new Quaternion(0.25f, -0.5f, 0.75f, 1);
            output.Add("transform", Vector(Vector3.Transform(a, matrix)));
            Vector3.Transform(ref a, ref matrix, out result);
            output.Add("transformOut", Vector(result));
            output.Add("rotate", Vector(Vector3.Transform(a, rotation)));
            Vector3.Transform(ref a, ref rotation, out result);
            output.Add("rotateOut", Vector(result));
            output.Add("normal", Vector(Vector3.TransformNormal(a, matrix)));
            Vector3.TransformNormal(ref a, ref matrix, out result);
            output.Add("normalOut", Vector(result));
            return output;
        }

        public static JsonWriter Vectors3()
        {
            var cases = new JsonWriter();
            cases.Add("zero", VectorCase(new Vector3(0, 0, 0), new Vector3(0, -0.0f, 2)));
            cases.Add("signedZero", VectorCase(new Vector3(-0.0f, 0, -0.0f), new Vector3(2, -2, -2)));
            cases.Add("boundaries", VectorCase(new Vector3(-2, 2, -2), new Vector3(-1, 1, 2)));
            cases.Add("outside", VectorCase(new Vector3(-2.125f, 2.125f, -2.125f), new Vector3(0, -0.0f, 0)));
            cases.Add("infinity", VectorCase(new Vector3(float.PositiveInfinity, float.NegativeInfinity, float.PositiveInfinity), new Vector3(2, -2, 2)));
            cases.Add("nan", VectorCase(new Vector3(float.NaN, 1, float.NaN), new Vector3(1, float.NaN, 1)));
            cases.Add("max", VectorCase(new Vector3(float.MaxValue, -float.MaxValue, float.MaxValue), new Vector3(0.5f, 2, 0.5f)));
            cases.Add("subnormal", VectorCase(new Vector3(float.Epsilon, -float.Epsilon, float.Epsilon), new Vector3(2, 2, 2)));
            uint state = Seed;
            for (int i = 0; i < 8; ++i)
            {
                var value = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                var divisor = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                cases.Add("seeded" + i, VectorCase(value, divisor));
            }
            var magnitudes = new JsonWriter();
            var inputs = new[] { Vector3.Zero, new Vector3(3, 4, 0), new Vector3(1e20f, 0, 0),
                new Vector3(1e-30f, 0, 0), new Vector3(float.PositiveInfinity, 0, 0), new Vector3(float.NaN, 0, 0) };
            var names = new[] { "zero", "threeFour", "large", "tiny", "infinity", "nan" };
            for (int i = 0; i < inputs.Length; ++i)
            {
                var value = inputs[i];
                var item = new JsonWriter().Add("input", Vector(value));
                Scalar(item, "length", value.Length());
                Scalar(item, "lengthSquared", value.LengthSquared());
                item.Add("normalize", Vector(Vector3.Normalize(value)));
                Vector3.Normalize(ref value, out Vector3 result);
                item.Add("normalizeOut", Vector(result));
                value.Normalize();
                item.Add("normalizeInstance", Vector(value));
                magnitudes.Add(names[i], item);
            }
            var spatial = new JsonWriter();
            float[] amounts = { -1, 0, 0.25f, 0.5f, 1, 2, float.NaN, float.PositiveInfinity };
            for (int i = 0; i < amounts.Length; ++i)
                spatial.Add("fixed" + i, SpatialCase(new Vector3(1, 2, 3), new Vector3(4, 3, 2),
                    new Vector3(-2, 1, 0), new Vector3(2, -1, 4), amounts[i]));
            state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                var a = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                var b = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                var c = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                var d = new Vector3(Next(ref state), Next(ref state), Next(ref state));
                spatial.Add("seeded" + i, SpatialCase(a, b, c, d, amounts[i]));
            }
            var products = new JsonWriter();
            var crossInputs = new[] { Vector3.Zero, new Vector3(1e20f, 1e20f, 0) };
            for (int i = 0; i < crossInputs.Length; ++i)
            {
                var left = crossInputs[i];
                var right = left;
                var item = new JsonWriter().Add("a", Vector(left)).Add("b", Vector(right))
                    .Add("cross", Vector(Vector3.Cross(left, right)));
                Vector3.Cross(ref left, ref right, out Vector3 result);
                item.Add("crossOut", Vector(result));
                products.Add(i == 0 ? "zero" : "wideCancellation", item);
            }
            var first = new Vector3(1.5f, -2.25f, 3.5f);
            var other = new Vector3(1.5f, 2.25f, 3.5f);
            CultureInfo previous = CultureInfo.CurrentCulture;
            string format;
            try { CultureInfo.CurrentCulture = CultureInfo.InvariantCulture; format = first.ToString(); }
            finally { CultureInfo.CurrentCulture = previous; }
            return new JsonWriter().Add("seed", (double)Seed).Add("arithmetic", cases)
                .Add("magnitude", magnitudes).Add("spatial", spatial).Add("products", products).Add("format", format)
                .Add("unequalEquals", first.Equals(other)).Add("unequalOperator", first == other)
                .Add("unequalNotOperator", first != other)
                .Add("signedZeroEquals", new Vector3(-0.0f).Equals(Vector3.Zero))
                .Add("signedZeroHashEqual", new Vector3(-0.0f).GetHashCode() == Vector3.Zero.GetHashCode());
        }

        private static JsonWriter Vector(Vector4 value)
        {
            var output = new JsonWriter();
            Scalar(output, "X", value.X);
            Scalar(output, "Y", value.Y);
            Scalar(output, "Z", value.Z);
            Scalar(output, "W", value.W);
            return output;
        }

        private static JsonWriter VectorCase(Vector4 value, Vector4 divisor)
        {
            var min = new Vector4(-2, -2, -2, -2);
            var max = new Vector4(2, 2, 2, 2);
            var output = new JsonWriter().Add("input", Vector(value)).Add("divisor", Vector(divisor));
            output.Add("divide", Vector(Vector4.Divide(value, divisor)));
            Vector4.Divide(ref value, ref divisor, out Vector4 result);
            output.Add("divideOut", Vector(result));
            output.Add("divideOperator", Vector(value / divisor));
            output.Add("scalarDivide", Vector(Vector4.Divide(value, divisor.X)));
            Vector4.Divide(ref value, divisor.X, out result);
            output.Add("scalarDivideOut", Vector(result));
            output.Add("scalarDivideOperator", Vector(value / divisor.X));
            output.Add("clamp", Vector(Vector4.Clamp(value, min, max)));
            Vector4.Clamp(ref value, ref min, ref max, out result);
            output.Add("clampOut", Vector(result));
            output.Add("reversedClamp", Vector(Vector4.Clamp(value, max, min)));
            Vector4.Clamp(ref value, ref max, ref min, out result);
            output.Add("reversedClampOut", Vector(result));
            var copy = new Vector4(value.X, value.Y, value.Z, value.W);
            output.Add("equalsCopy", value.Equals(copy)).Add("operatorEqualCopy", value == copy)
                .Add("operatorUnequalCopy", value != copy)
                .Add("copyHashEqual", value.GetHashCode() == copy.GetHashCode());
            return output;
        }

        private static JsonWriter SpatialCase(Vector4 a, Vector4 b, Vector4 c, Vector4 d, float amount)
        {
            var output = new JsonWriter().Add("a", Vector(a)).Add("b", Vector(b))
                .Add("c", Vector(c)).Add("d", Vector(d));
            Scalar(output, "amount", amount);
            Vector4 result;
            output.Add("lerp", Vector(Vector4.Lerp(a, b, amount)));
            Vector4.Lerp(ref a, ref b, amount, out result);
            output.Add("lerpOut", Vector(result));
            output.Add("smoothStep", Vector(Vector4.SmoothStep(a, b, amount)));
            Vector4.SmoothStep(ref a, ref b, amount, out result);
            output.Add("smoothStepOut", Vector(result));
            output.Add("hermite", Vector(Vector4.Hermite(a, b, c, d, amount)));
            Vector4.Hermite(ref a, ref b, ref c, ref d, amount, out result);
            output.Add("hermiteOut", Vector(result));
            output.Add("catmullRom", Vector(Vector4.CatmullRom(a, b, c, d, amount)));
            Vector4.CatmullRom(ref a, ref b, ref c, ref d, amount, out result);
            output.Add("catmullRomOut", Vector(result));
            output.Add("barycentric", Vector(Vector4.Barycentric(a, b, c, amount, 0.25f)));
            Vector4.Barycentric(ref a, ref b, ref c, amount, 0.25f, out result);
            output.Add("barycentricOut", Vector(result));
            var matrix = new Matrix(2, 0, 1, 0.5f, 0, -3, 0, 0, 1, 0, 4, 0, 7, 8, 9, 1);
            var rotation = new Quaternion(0.25f, -0.5f, 0.75f, 1);
            output.Add("transform", Vector(Vector4.Transform(a, matrix)));
            Vector4.Transform(ref a, ref matrix, out result);
            output.Add("transformOut", Vector(result));
            output.Add("rotate", Vector(Vector4.Transform(a, rotation)));
            Vector4.Transform(ref a, ref rotation, out result);
            output.Add("rotateOut", Vector(result));
            var a2 = new Vector2(a.X, a.Y);
            var a3 = new Vector3(a.X, a.Y, a.Z);
            output.Add("transform2", Vector(Vector4.Transform(a2, matrix)));
            Vector4.Transform(ref a2, ref matrix, out result);
            output.Add("transform2Out", Vector(result));
            output.Add("rotate2", Vector(Vector4.Transform(a2, rotation)));
            Vector4.Transform(ref a2, ref rotation, out result);
            output.Add("rotate2Out", Vector(result));
            output.Add("transform3", Vector(Vector4.Transform(a3, matrix)));
            Vector4.Transform(ref a3, ref matrix, out result);
            output.Add("transform3Out", Vector(result));
            output.Add("rotate3", Vector(Vector4.Transform(a3, rotation)));
            Vector4.Transform(ref a3, ref rotation, out result);
            output.Add("rotate3Out", Vector(result));
            return output;
        }

        public static JsonWriter Vectors4()
        {
            var cases = new JsonWriter();
            cases.Add("zero", VectorCase(new Vector4(0, 0, 0, 0), new Vector4(0, -0.0f, 2, -2)));
            cases.Add("signedZero", VectorCase(new Vector4(-0.0f, 0, -0.0f, 0), new Vector4(2, -2, -2, 2)));
            cases.Add("boundaries", VectorCase(new Vector4(-2, 2, -2, 2), new Vector4(-1, 1, 2, -2)));
            cases.Add("outside", VectorCase(new Vector4(-2.125f, 2.125f, -2.125f, 2.125f), new Vector4(0, -0.0f, 0, -0.0f)));
            cases.Add("infinity", VectorCase(new Vector4(float.PositiveInfinity, float.NegativeInfinity, float.PositiveInfinity, float.NegativeInfinity), new Vector4(2, -2, 2, -2)));
            cases.Add("nan", VectorCase(new Vector4(float.NaN, 1, float.NaN, 1), new Vector4(1, float.NaN, 1, float.NaN)));
            cases.Add("max", VectorCase(new Vector4(float.MaxValue, -float.MaxValue, float.MaxValue, -float.MaxValue), new Vector4(0.5f, 2, 0.5f, 2)));
            cases.Add("subnormal", VectorCase(new Vector4(float.Epsilon, -float.Epsilon, float.Epsilon, -float.Epsilon), new Vector4(2, 2, 2, 2)));
            uint state = Seed;
            for (int i = 0; i < 8; ++i)
            {
                var value = new Vector4(Next(ref state), Next(ref state), Next(ref state), Next(ref state));
                var divisor = new Vector4(Next(ref state), Next(ref state), Next(ref state), Next(ref state));
                cases.Add("seeded" + i, VectorCase(value, divisor));
            }
            var magnitudes = new JsonWriter();
            var inputs = new[] { Vector4.Zero, new Vector4(3, 4, 0, 0), new Vector4(1e20f, 0, 0, 0),
                new Vector4(1e-30f, 0, 0, 0), new Vector4(float.PositiveInfinity, 0, 0, 0), new Vector4(float.NaN, 0, 0, 0) };
            var names = new[] { "zero", "threeFour", "large", "tiny", "infinity", "nan" };
            for (int i = 0; i < inputs.Length; ++i)
            {
                var value = inputs[i];
                var item = new JsonWriter().Add("input", Vector(value));
                Scalar(item, "length", value.Length());
                Scalar(item, "lengthSquared", value.LengthSquared());
                item.Add("normalize", Vector(Vector4.Normalize(value)));
                Vector4.Normalize(ref value, out Vector4 result);
                item.Add("normalizeOut", Vector(result));
                value.Normalize();
                item.Add("normalizeInstance", Vector(value));
                magnitudes.Add(names[i], item);
            }
            var spatial = new JsonWriter();
            float[] amounts = { -1, 0, 0.25f, 0.5f, 1, 2, float.NaN, float.PositiveInfinity };
            for (int i = 0; i < amounts.Length; ++i)
                spatial.Add("fixed" + i, SpatialCase(new Vector4(1, 2, 3, 4), new Vector4(4, 3, 2, 1),
                    new Vector4(-2, 1, 0, 3), new Vector4(2, -1, 4, -3), amounts[i]));
            state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                var a = new Vector4(Next(ref state), Next(ref state), Next(ref state), Next(ref state));
                var b = new Vector4(Next(ref state), Next(ref state), Next(ref state), Next(ref state));
                var c = new Vector4(Next(ref state), Next(ref state), Next(ref state), Next(ref state));
                var d = new Vector4(Next(ref state), Next(ref state), Next(ref state), Next(ref state));
                spatial.Add("seeded" + i, SpatialCase(a, b, c, d, amounts[i]));
            }
            var first = new Vector4(1.5f, -2.25f, 3.5f, -4.75f);
            var other = new Vector4(1.5f, 2.25f, 3.5f, -4.75f);
            CultureInfo previous = CultureInfo.CurrentCulture;
            string format;
            try { CultureInfo.CurrentCulture = CultureInfo.InvariantCulture; format = first.ToString(); }
            finally { CultureInfo.CurrentCulture = previous; }
            return new JsonWriter().Add("seed", (double)Seed).Add("arithmetic", cases)
                .Add("magnitude", magnitudes).Add("spatial", spatial).Add("format", format)
                .Add("unequalEquals", first.Equals(other)).Add("unequalOperator", first == other)
                .Add("unequalNotOperator", first != other)
                .Add("signedZeroEquals", new Vector4(-0.0f).Equals(Vector4.Zero))
                .Add("signedZeroHashEqual", new Vector4(-0.0f).GetHashCode() == Vector4.Zero.GetHashCode());
        }

    }
}
