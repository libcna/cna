// SPDX-License-Identifier: MS-PL
using System;
using System.Globalization;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics.PackedVector;

namespace CNA.FnaReference
{
    internal static partial class ValueEdgeReference
    {
        private static string PackingHex(ulong value) { return value.ToString("x16", CultureInfo.InvariantCulture); }

        private static JsonWriter PackingCase<T, TStorage>(Vector4 input, T value, T vectorValue, ulong raw)
            where T : struct, IPackedVector<TStorage>
        {
            var output = new JsonWriter().Add("input", Vector(input))
                .Add("constructor", PackingHex(Convert.ToUInt64(value.PackedValue)))
                .Add("vectorConstructor", PackingHex(Convert.ToUInt64(vectorValue.PackedValue)))
                .Add("unpack", Vector(value.ToVector4())).Add("vectorUnpack", Vector(vectorValue.ToVector4()));
            var packed = new T(); packed.PackFromVector4(input);
            output.Add("pack", PackingHex(Convert.ToUInt64(packed.PackedValue))).Add("packUnpack", Vector(packed.ToVector4()));
            packed.PackedValue = (TStorage)Convert.ChangeType(raw, typeof(TStorage));
            output.Add("raw", PackingHex(Convert.ToUInt64(packed.PackedValue))).Add("rawUnpack", Vector(packed.ToVector4()));
            Vector4 expanded = packed.ToVector4(); packed.PackFromVector4(expanded);
            return output.Add("repackedRaw", PackingHex(Convert.ToUInt64(packed.PackedValue)));
        }

        private static Vector4 PackingInput(int index, Vector4 scales, ref uint state)
        {
            float nan = BitConverter.Int32BitsToSingle(0x7fc00000);
            if (index == 0) return Vector4.Zero;
            if (index == 1) return new Vector4(-0.0f, 0, -0.0f, 0);
            if (index == 2) return Vector4.One;
            if (index == 3) return new Vector4(-1);
            if (index == 4) return new Vector4(0.25f, 0.5f, 0.75f, 1);
            var ties = new Vector4(0.5f / scales.X, 1.5f / scales.Y, 2.5f / scales.Z, 3.5f / scales.W);
            if (index == 5) return ties;
            if (index == 6 || index == 7)
            {
                int step = index == 6 ? -1 : 1;
                return new Vector4(BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(ties.X) + step),
                    BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(ties.Y) + step),
                    BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(ties.Z) + step),
                    BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(ties.W) + step));
            }
            if (index == 8) return new Vector4(-1.125f, -1, -0.125f, 0);
            if (index == 9) return new Vector4(0, 0.875f, 1, 1.125f);
            if (index == 10) return new Vector4(-0.5f, 0.5f, 254.5f, 255.5f);
            if (index == 11) return new Vector4(-32769, -32768, 32767, 32768);
            if (index == 12) return new Vector4(1.00048828125f, 1.00146484375f, 65504, 65520);
            if (index == 13) return new Vector4(0.0000000298023223876953125f, 0.0000000894069671630859375f,
                0.000000059604644775390625f, 0.000060975551605224609375f);
            if (index == 14) return new Vector4(nan, float.PositiveInfinity, float.NegativeInfinity, -0.0f);
            return new Vector4(Next(ref state) / 8, Next(ref state) / 8, Next(ref state) / 8, Next(ref state) / 8);
        }

        private static JsonWriter PackingColor(Color value)
        {
            return new JsonWriter().Add("packed", PackingHex(value.PackedValue))
                .Add("r", (int)value.R).Add("g", (int)value.G).Add("b", (int)value.B).Add("a", (int)value.A)
                .Add("vector3", Vector(value.ToVector3())).Add("vector4", Vector(value.ToVector4()));
        }

        public static JsonWriter Packing()
        {
            string[] names = { "zero", "signedZero", "one", "negative", "quarters", "ties", "belowTies", "aboveTies",
                "clampLow", "clampHigh", "byteBoundary", "shortBoundary", "halfBoundary", "halfTiny", "nonfinite",
                "seeded0", "seeded1", "seeded2", "seeded3" };
            ulong[] rawValues = { 0UL, 1UL, ulong.MaxValue, 0x8000800080008000UL, 0x7fff7fff7fff7fffUL, 0x7e007c00fc008000UL };
            var packedTypes = new JsonWriter();
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(255, 255, 255, 255), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffUL;
                    cases.Add(names[i], PackingCase<Alpha8, byte>(input, new Alpha8(input.W), new Alpha8(input.W), raw));
                }
                packedTypes.Add("Alpha8", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(31, 63, 31, 1), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffUL;
                    cases.Add(names[i], PackingCase<Bgr565, ushort>(input, new Bgr565(input.X, input.Y, input.Z), new Bgr565(new Vector3(input.X, input.Y, input.Z)), raw));
                }
                packedTypes.Add("Bgr565", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(15, 15, 15, 15), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffUL;
                    cases.Add(names[i], PackingCase<Bgra4444, ushort>(input, new Bgra4444(input.X, input.Y, input.Z, input.W), new Bgra4444(input), raw));
                }
                packedTypes.Add("Bgra4444", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(31, 31, 31, 1), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffUL;
                    cases.Add(names[i], PackingCase<Bgra5551, ushort>(input, new Bgra5551(input.X, input.Y, input.Z, input.W), new Bgra5551(input), raw));
                }
                packedTypes.Add("Bgra5551", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(1, 1, 1, 1), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffffffUL;
                    cases.Add(names[i], PackingCase<Byte4, uint>(input, new Byte4(input.X, input.Y, input.Z, input.W), new Byte4(input), raw));
                }
                packedTypes.Add("Byte4", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(1, 1, 1, 1), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffUL;
                    cases.Add(names[i], PackingCase<HalfSingle, ushort>(input, new HalfSingle(input.X), new HalfSingle(input.X), raw));
                }
                packedTypes.Add("HalfSingle", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(1, 1, 1, 1), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffffffUL;
                    cases.Add(names[i], PackingCase<HalfVector2, uint>(input, new HalfVector2(input.X, input.Y), new HalfVector2(new Vector2(input.X, input.Y)), raw));
                }
                packedTypes.Add("HalfVector2", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(1, 1, 1, 1), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & ulong.MaxValue;
                    cases.Add(names[i], PackingCase<HalfVector4, ulong>(input, new HalfVector4(input.X, input.Y, input.Z, input.W), new HalfVector4(input), raw));
                }
                packedTypes.Add("HalfVector4", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(127, 127, 127, 127), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffUL;
                    cases.Add(names[i], PackingCase<NormalizedByte2, ushort>(input, new NormalizedByte2(input.X, input.Y), new NormalizedByte2(new Vector2(input.X, input.Y)), raw));
                }
                packedTypes.Add("NormalizedByte2", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(127, 127, 127, 127), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffffffUL;
                    cases.Add(names[i], PackingCase<NormalizedByte4, uint>(input, new NormalizedByte4(input.X, input.Y, input.Z, input.W), new NormalizedByte4(input), raw));
                }
                packedTypes.Add("NormalizedByte4", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(32767, 32767, 32767, 32767), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffffffUL;
                    cases.Add(names[i], PackingCase<NormalizedShort2, uint>(input, new NormalizedShort2(input.X, input.Y), new NormalizedShort2(new Vector2(input.X, input.Y)), raw));
                }
                packedTypes.Add("NormalizedShort2", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(32767, 32767, 32767, 32767), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & ulong.MaxValue;
                    cases.Add(names[i], PackingCase<NormalizedShort4, ulong>(input, new NormalizedShort4(input.X, input.Y, input.Z, input.W), new NormalizedShort4(input), raw));
                }
                packedTypes.Add("NormalizedShort4", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(65535, 65535, 65535, 65535), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffffffUL;
                    cases.Add(names[i], PackingCase<Rg32, uint>(input, new Rg32(input.X, input.Y), new Rg32(new Vector2(input.X, input.Y)), raw));
                }
                packedTypes.Add("Rg32", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(1023, 1023, 1023, 3), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffffffUL;
                    cases.Add(names[i], PackingCase<Rgba1010102, uint>(input, new Rgba1010102(input.X, input.Y, input.Z, input.W), new Rgba1010102(input), raw));
                }
                packedTypes.Add("Rgba1010102", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(65535, 65535, 65535, 65535), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & ulong.MaxValue;
                    cases.Add(names[i], PackingCase<Rgba64, ulong>(input, new Rgba64(input.X, input.Y, input.Z, input.W), new Rgba64(input), raw));
                }
                packedTypes.Add("Rgba64", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(1, 1, 1, 1), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & 0xffffffffUL;
                    cases.Add(names[i], PackingCase<Short2, uint>(input, new Short2(input.X, input.Y), new Short2(new Vector2(input.X, input.Y)), raw));
                }
                packedTypes.Add("Short2", cases);
            }
            {
                var cases = new JsonWriter(); uint state = Seed;
                for (int i = 0; i < names.Length; ++i)
                {
                    Vector4 input = PackingInput(i, new Vector4(1, 1, 1, 1), ref state);
                    ulong raw = rawValues[i % rawValues.Length] & ulong.MaxValue;
                    cases.Add(names[i], PackingCase<Short4, ulong>(input, new Short4(input.X, input.Y, input.Z, input.W), new Short4(input), raw));
                }
                packedTypes.Add("Short4", cases);
            }
            var colors = new JsonWriter(); uint colorState = Seed;
            for (int i = 0; i < names.Length; ++i)
            {
                Vector4 input = PackingInput(i, new Vector4(255), ref colorState);
                IPackedVector packed = new Color(); packed.PackFromVector4(input);
                var raw = new Color(); raw.PackedValue = (uint)rawValues[i % rawValues.Length];
                colors.Add(names[i], new JsonWriter().Add("input", Vector(input))
                    .Add("vector4", PackingColor(new Color(input)))
                    .Add("floats4", PackingColor(new Color(input.X, input.Y, input.Z, input.W)))
                    .Add("vector3", PackingColor(new Color(new Vector3(input.X, input.Y, input.Z))))
                    .Add("floats3", PackingColor(new Color(input.X, input.Y, input.Z)))
                    .Add("pack", PackingColor((Color)packed)).Add("raw", PackingColor(raw))
                    .Add("nonpremultiplied", PackingColor(Color.FromNonPremultiplied(input))));
            }
            var integers = new JsonWriter();
            int[,] integerInputs = { {0, 1, 254, 255}, {-1, 0, 255, 256}, {1, 2, 3, 128},
                {32767, -32768, 256, 255}, {int.MinValue, 0, int.MaxValue, 0}, {255, 254, 1, -1} };
            for (int i = 0; i < integerInputs.GetLength(0); ++i)
            {
                int r = integerInputs[i, 0], g = integerInputs[i, 1], b = integerInputs[i, 2], a = integerInputs[i, 3];
                integers.Add("case" + i, new JsonWriter().Add("r", r).Add("g", g).Add("b", b).Add("a", a)
                    .Add("rgb", PackingColor(new Color(r, g, b))).Add("rgba", PackingColor(new Color(r, g, b, a)))
                    .Add("nonpremultiplied", PackingColor(Color.FromNonPremultiplied(r, g, b, a))));
            }
            var interpolation = new JsonWriter();
            float[] factors = { -1, -0.0f, 0, 0.1f, 0.5f, 0.99999f, 1, 2, float.NaN, float.PositiveInfinity,
                float.NegativeInfinity, 1.0f / 131072, 3.0f / 131072, 0.99999994f };
            var first = new Color(255, 1, 0, 254); var second = new Color(0, 254, 1, 0);
            for (int i = 0; i < factors.Length + 4; ++i)
            {
                float amount = i < factors.Length ? factors[i] : Next(ref colorState) / 8;
                var item = new JsonWriter(); Scalar(item, "amount", amount);
                item.Add("lerp", PackingColor(Color.Lerp(first, second, amount)))
                    .Add("multiply", PackingColor(Color.Multiply(first, amount)))
                    .Add("operator", PackingColor(first * amount));
                interpolation.Add("case" + i, item);
            }
            return new JsonWriter().Add("seed", (double)Seed).Add("packedTypes", packedTypes).Add("colors", colors)
                .Add("integers", integers).Add("interpolation", interpolation);
        }
    }
}
