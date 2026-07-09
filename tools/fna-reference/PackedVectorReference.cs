// Task 473: reference values for FNA's PackedVector types, using the SAME input values already
// used by Task 197's own tests/PackedVectorGolden.md -- that table's own values were derived by
// hand (Python, reimplementing FNA's bit-packing formulas from reading the source), not by
// running FNA itself. This generator complements it with values from the real, running FNA
// implementation, for the same inputs, so the two can be diffed directly: agreement confirms
// Task 197's hand-derived formulas were correct; any disagreement would mean either Task 197's
// Python re-derivation or CNA's own C++ implementation has a real bug worth chasing.
//
// None of these 17 types need a GraphicsDevice to construct (all take plain float/Vector
// arguments) -- confirmed by reading each type's own constructor in
// src/Graphics/PackedVector/*.cs directly before calling it.
//
// Uses plain float[] arrays instead of value tuples: this project's own LangVersion is pinned
// to C# 4.0 (matching FNA's own FNA.csproj exactly), which predates tuple syntax.
using System;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics.PackedVector;

namespace CNA.FnaReference
{
    public static class PackedVectorReference
    {
        public static JsonWriter Generate()
        {
            return new JsonWriter()
                .Add("Alpha8", DumpAlpha8())
                .Add("Bgr565", DumpBgr565())
                .Add("Bgra4444", DumpBgra4444())
                .Add("Bgra5551", DumpBgra5551())
                .Add("Byte4", DumpByte4())
                .Add("HalfSingle", DumpHalfSingle())
                .Add("HalfVector2", DumpHalfVector2())
                .Add("HalfVector4", DumpHalfVector4())
                .Add("NormalizedByte2", DumpNormalizedByte2())
                .Add("NormalizedByte4", DumpNormalizedByte4())
                .Add("NormalizedShort2", DumpNormalizedShort2())
                .Add("NormalizedShort4", DumpNormalizedShort4())
                .Add("Rg32", DumpRg32())
                .Add("Rgba1010102", DumpRgba1010102())
                .Add("Rgba64", DumpRgba64())
                .Add("Short2", DumpShort2())
                .Add("Short4", DumpShort4());
        }

        private static string Label(float[] v)
        {
            string s = "(";
            for (int i = 0; i < v.Length; ++i)
            {
                if (i > 0) s += ",";
                s += v[i].ToString(System.Globalization.CultureInfo.InvariantCulture);
            }
            return s + ")";
        }

        private static JsonWriter DumpAlpha8()
        {
            var cases = new JsonWriter();
            float[] inputs = { 0.00f, 0.25f, 0.50f, 1.00f };
            foreach (float a in inputs)
            {
                var v = new Alpha8(a);
                cases.Add(a.ToString("0.00"), new JsonWriter().Add("packed", (int)v.PackedValue));
            }
            return cases;
        }

        private static JsonWriter DumpBgr565()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0, 0}, new float[] {0, 1, 0},
                new float[] {0, 0, 1}, new float[] {0.5f, 0.5f, 0.5f}
            };
            foreach (float[] c in inputs)
            {
                var v = new Bgr565(c[0], c[1], c[2]);
                Vector3 u = v.ToVector3();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (int)v.PackedValue)
                    .Add("ToVector3_X", (double)u.X)
                    .Add("ToVector3_Y", (double)u.Y)
                    .Add("ToVector3_Z", (double)u.Z));
            }
            return cases;
        }

        private static JsonWriter DumpBgra4444()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0, 0, 1}, new float[] {0, 1, 0, 0.5f},
                new float[] {0.5f, 0.5f, 0.5f, 0.5f}
            };
            foreach (float[] c in inputs)
            {
                var v = new Bgra4444(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (int)v.PackedValue)
                    .Add("ToVector4_X", (double)u.X).Add("ToVector4_Y", (double)u.Y)
                    .Add("ToVector4_Z", (double)u.Z).Add("ToVector4_W", (double)u.W));
            }
            return cases;
        }

        private static JsonWriter DumpBgra5551()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0, 0, 1}, new float[] {0, 0, 1, 0},
                new float[] {0.5f, 0.5f, 0.5f, 1}
            };
            foreach (float[] c in inputs)
            {
                var v = new Bgra5551(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (int)v.PackedValue)
                    .Add("ToVector4_X", (double)u.X).Add("ToVector4_Y", (double)u.Y)
                    .Add("ToVector4_Z", (double)u.Z).Add("ToVector4_W", (double)u.W));
            }
            return cases;
        }

        private static JsonWriter DumpByte4()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {255, 0, 0, 255}, new float[] {0, 255, 0, 255},
                new float[] {100, 150, 200, 128}
            };
            foreach (float[] c in inputs)
            {
                var v = new Byte4(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (double)v.PackedValue)
                    .Add("ToVector4_X", (double)u.X).Add("ToVector4_Y", (double)u.Y)
                    .Add("ToVector4_Z", (double)u.Z).Add("ToVector4_W", (double)u.W));
            }
            return cases;
        }

        private static JsonWriter DumpHalfSingle()
        {
            var cases = new JsonWriter();
            float[] inputs = { 0.0f, 1.0f, -1.0f, 0.5f, 0.25f };
            foreach (float f in inputs)
            {
                var v = new HalfSingle(f);
                cases.Add(f.ToString("0.00"), new JsonWriter()
                    .Add("packed", (int)v.PackedValue)
                    .Add("ToSingle", (double)v.ToSingle()));
            }
            return cases;
        }

        private static JsonWriter DumpHalfVector2()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0}, new float[] {0, 1},
                new float[] {0.5f, 0.5f}, new float[] {-1, -1}
            };
            foreach (float[] c in inputs)
            {
                var v = new HalfVector2(c[0], c[1]);
                Vector2 u = v.ToVector2();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (double)v.PackedValue)
                    .Add("ToVector2_X", (double)u.X).Add("ToVector2_Y", (double)u.Y));
            }
            return cases;
        }

        private static JsonWriter DumpHalfVector4()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0, 0, 1}, new float[] {0.5f, 0.5f, 0.5f, 0.5f},
                new float[] {-1, -1, -1, -1}
            };
            foreach (float[] c in inputs)
            {
                var v = new HalfVector4(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", v.PackedValue.ToString("x16"))
                    .Add("ToVector4_X", (double)u.X).Add("ToVector4_Y", (double)u.Y)
                    .Add("ToVector4_Z", (double)u.Z).Add("ToVector4_W", (double)u.W));
            }
            return cases;
        }

        private static JsonWriter DumpNormalizedByte2()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0}, new float[] {0, 1},
                new float[] {-1, -1}, new float[] {0.5f, 0.5f}
            };
            foreach (float[] c in inputs)
            {
                var v = new NormalizedByte2(c[0], c[1]);
                Vector2 u = v.ToVector2();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (int)v.PackedValue)
                    .Add("ToVector2_X", (double)u.X).Add("ToVector2_Y", (double)u.Y));
            }
            return cases;
        }

        private static JsonWriter DumpNormalizedByte4()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0, 0, 1}, new float[] {0.5f, 0.5f, 0.5f, 0.5f},
                new float[] {-1, -1, -1, -1}
            };
            foreach (float[] c in inputs)
            {
                var v = new NormalizedByte4(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (double)v.PackedValue)
                    .Add("ToVector4_X", (double)u.X).Add("ToVector4_Y", (double)u.Y)
                    .Add("ToVector4_Z", (double)u.Z).Add("ToVector4_W", (double)u.W));
            }
            return cases;
        }

        private static JsonWriter DumpNormalizedShort2()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0}, new float[] {0, 1},
                new float[] {-1, -1}, new float[] {0.5f, 0.5f}
            };
            foreach (float[] c in inputs)
            {
                var v = new NormalizedShort2(c[0], c[1]);
                Vector2 u = v.ToVector2();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (double)v.PackedValue)
                    .Add("ToVector2_X", (double)u.X).Add("ToVector2_Y", (double)u.Y));
            }
            return cases;
        }

        private static JsonWriter DumpNormalizedShort4()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0, 0, 1}, new float[] {0.5f, 0.5f, 0.5f, 0.5f},
                new float[] {-1, -1, -1, -1}
            };
            foreach (float[] c in inputs)
            {
                var v = new NormalizedShort4(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", v.PackedValue.ToString("x16"))
                    .Add("ToVector4_X", (double)u.X).Add("ToVector4_Y", (double)u.Y)
                    .Add("ToVector4_Z", (double)u.Z).Add("ToVector4_W", (double)u.W));
            }
            return cases;
        }

        private static JsonWriter DumpRg32()
        {
            var cases = new JsonWriter();
            float[][] inputs = { new float[] {1, 0}, new float[] {0, 1}, new float[] {0.5f, 0.5f} };
            foreach (float[] c in inputs)
            {
                var v = new Rg32(c[0], c[1]);
                Vector2 u = v.ToVector2();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (double)v.PackedValue)
                    .Add("ToVector2_X", (double)u.X).Add("ToVector2_Y", (double)u.Y));
            }
            return cases;
        }

        private static JsonWriter DumpRgba1010102()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0, 0, 1}, new float[] {0, 1, 0, 0.5f},
                new float[] {0.5f, 0.5f, 0.5f, 0.5f}
            };
            foreach (float[] c in inputs)
            {
                var v = new Rgba1010102(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (double)v.PackedValue)
                    .Add("ToVector4_X", (double)u.X).Add("ToVector4_Y", (double)u.Y)
                    .Add("ToVector4_Z", (double)u.Z).Add("ToVector4_W", (double)u.W));
            }
            return cases;
        }

        private static JsonWriter DumpRgba64()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {1, 0, 0, 1}, new float[] {0, 1, 0, 0.5f},
                new float[] {0.5f, 0.5f, 0.5f, 0.5f}
            };
            foreach (float[] c in inputs)
            {
                var v = new Rgba64(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", v.PackedValue.ToString("x16"))
                    .Add("ToVector4_X", (double)u.X).Add("ToVector4_Y", (double)u.Y)
                    .Add("ToVector4_Z", (double)u.Z).Add("ToVector4_W", (double)u.W));
            }
            return cases;
        }

        private static JsonWriter DumpShort2()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {32767, 0}, new float[] {0, 32767},
                new float[] {-32768, -32768}, new float[] {100, 200}
            };
            foreach (float[] c in inputs)
            {
                var v = new Short2(c[0], c[1]);
                Vector2 u = v.ToVector2();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", (double)v.PackedValue)
                    .Add("ToVector2_X", (double)u.X).Add("ToVector2_Y", (double)u.Y));
            }
            return cases;
        }

        private static JsonWriter DumpShort4()
        {
            var cases = new JsonWriter();
            float[][] inputs = {
                new float[] {32767, 0, 0, 32767}, new float[] {0, 32767, -32768, 0},
                new float[] {100, 200, 300, 400}
            };
            foreach (float[] c in inputs)
            {
                var v = new Short4(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label(c), new JsonWriter()
                    .Add("packed", v.PackedValue.ToString("x16"))
                    .Add("ToVector4_X", (double)u.X).Add("ToVector4_Y", (double)u.Y)
                    .Add("ToVector4_Z", (double)u.Z).Add("ToVector4_W", (double)u.W));
            }
            return cases;
        }
    }
}
