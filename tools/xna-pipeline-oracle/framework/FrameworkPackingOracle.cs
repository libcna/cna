// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-099: black-box behaviour oracle for the XNA 4.0
// framework's float-to-integer packing -- the Color constructors, Color.FromNonPremultiplied and
// the Graphics.PackedVector types. The content pipeline's VectorConverter rounds where CNA (which
// follows FNA here) truncates, and a converter measurement alone cannot say whether the rounding
// lives in the converter or in the framework type it packs into. This driver packs the framework
// types directly, with inputs chosen so that truncation, round-half-away-from-zero and
// round-half-to-even each produce a different answer.
//
// It runs the genuine assemblies and records what they DO; nothing here inspects XNA's IL.
// No Direct3D device is created, so no display is needed.
//
// Output: one JSON document (framework-packing-oracle.json) whose "cases" list has one entry per
// measurement.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics.PackedVector;

namespace Cna.Xna40.FrameworkOracle
{
    internal static class Program
    {
        private static readonly List<string> Cases = new List<string>();

        private static string Escape(string text)
        {
            return text.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\r", "\\r").Replace("\n", "\\n");
        }

        /** A sphere printed so a tie-break shows: centre to nine digits, then the radius. */
        private static string Describe(BoundingSphere sphere)
        {
            return "center=(" + sphere.Center.X.ToString("R", CultureInfo.InvariantCulture) + "," +
                   sphere.Center.Y.ToString("R", CultureInfo.InvariantCulture) + "," +
                   sphere.Center.Z.ToString("R", CultureInfo.InvariantCulture) + ") radius=" +
                   sphere.Radius.ToString("R", CultureInfo.InvariantCulture);
        }

        private static void Record(string name, Func<string> measurement)
        {
            try
            {
                Cases.Add("  {\"case\": \"" + name + "\", \"result\": \"" + Escape(measurement()) + "\"}");
            }
            catch (Exception error)
            {
                Cases.Add("  {\"case\": \"" + name + "\", \"result\": \"throws " + error.GetType().Name +
                          ": " + Escape(error.Message) + "\"}");
            }
        }

        private static string Describe(Color color)
        {
            return color.R + "," + color.G + "," + color.B + "," + color.A + " packed=" +
                   color.PackedValue.ToString("X8");
        }

        private static string R(float value)
        {
            return value.ToString("R", CultureInfo.InvariantCulture);
        }

        public static void Main(string[] args)
        {
            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            string outputDirectory = args.Length > 0 ? args[0] : ".";

            // ---- Color: unit floats to bytes -----------------------------------------------------
            // 0.25 * 255 = 63.75 and 0.75 * 255 = 191.25 separate truncation (63, 191) from
            // rounding (64, 191); 0.5 * 255 = 127.5 is the tie (127 truncated, 128 rounded).

            // XNAPP-266: BoundingSphere.CreateFromPoints, on point sets whose extents tie.
            //
            // The seed of the sphere is the widest axis, and a tie between two axes has to be
            // broken somehow. FNA -- and CNA, which followed it -- keeps the earlier axis; the
            // model differential says XNA keeps the later one, because the mesh bounding sphere of
            // a right triangle came out mirrored. That is a claim about a public XNA math API, so
            // it is measured here directly rather than inferred from a model.
            Record("boundingsphere/tie_x_and_y", () =>
                Describe(BoundingSphere.CreateFromPoints(new[]
                {
                    new Vector3(0, 0, 0), new Vector3(2, 0, 0), new Vector3(0, 2, 0),
                })));
            Record("boundingsphere/tie_x_and_y_reversed", () =>
                Describe(BoundingSphere.CreateFromPoints(new[]
                {
                    new Vector3(0, 2, 0), new Vector3(2, 0, 0), new Vector3(0, 0, 0),
                })));
            Record("boundingsphere/tie_all_three_axes", () =>
                Describe(BoundingSphere.CreateFromPoints(new[]
                {
                    new Vector3(0, 0, 0), new Vector3(2, 0, 0),
                    new Vector3(0, 2, 0), new Vector3(0, 0, 2),
                })));
            Record("boundingsphere/x_widest", () =>
                Describe(BoundingSphere.CreateFromPoints(new[]
                {
                    new Vector3(0, 0, 0), new Vector3(4, 0, 0), new Vector3(0, 2, 0),
                })));
            Record("boundingsphere/y_widest", () =>
                Describe(BoundingSphere.CreateFromPoints(new[]
                {
                    new Vector3(0, 0, 0), new Vector3(2, 0, 0), new Vector3(0, 4, 0),
                })));
            Record("boundingsphere/single_point", () =>
                Describe(BoundingSphere.CreateFromPoints(new[] { new Vector3(1, 2, 3) })));

            Record("color/vector4_quarters", () => Describe(new Color(new Vector4(0.25f, 0.5f, 0.75f, 1.0f))));
            Record("color/floats_quarters", () => Describe(new Color(0.25f, 0.5f, 0.75f, 1.0f)));
            Record("color/vector3_quarters", () => Describe(new Color(new Vector3(0.25f, 0.5f, 0.75f))));
            Record("color/floats_rgb_quarters", () => Describe(new Color(0.25f, 0.5f, 0.75f)));
            // 0.002 * 255 = 0.51: one below the tie, so truncation gives 0 and rounding gives 1.
            Record("color/vector4_barely_above_zero", () => Describe(new Color(new Vector4(0.002f, 0.001f, 0.0019f, 0.0021f))));
            // Ties at even and odd byte results: 126.5 rounds to 126 half-to-even but 127
            // half-away-from-zero; 127.5 rounds to 128 either way.
            Record("color/vector4_tie_even", () => Describe(new Color(new Vector4(126.5f / 255.0f, 127.5f / 255.0f, 128.5f / 255.0f, 129.5f / 255.0f))));
            Record("color/vector4_out_of_range", () => Describe(new Color(new Vector4(2.0f, -1.0f, 0.5f, 1.0f))));
            Record("color/floats_out_of_range", () => Describe(new Color(2.0f, -1.0f, 0.5f, 1.0f)));
            Record("color/vector4_nan", () => Describe(new Color(new Vector4(float.NaN, 0.5f, 0.5f, 0.5f))));
            Record("color/ints_out_of_range", () => Describe(new Color(300, -5, 128, 255)));

            // ---- Color.FromNonPremultiplied -------------------------------------------------------
            Record("color/from_non_premultiplied_vector4", () => Describe(Color.FromNonPremultiplied(new Vector4(1.0f, 0.0f, 0.25f, 0.5f))));
            Record("color/from_non_premultiplied_ints", () => Describe(Color.FromNonPremultiplied(255, 0, 64, 128)));

            // ---- Color.Lerp / Color.Multiply ------------------------------------------------------
            // Both interpolate in byte units, so a .5 result is an exact tie: 127.5 separates
            // truncation (127) from rounding (128), and 0.5/1.5/2.5/3.5 separate the two roundings.
            Record("color/lerp_half", () => Describe(Color.Lerp(new Color(0, 0, 0, 0), new Color(255, 255, 255, 255), 0.5f)));
            Record("color/lerp_odd_ties", () => Describe(Color.Lerp(new Color(0, 1, 2, 3), new Color(1, 2, 3, 4), 0.5f)));
            Record("color/lerp_amount_above_one", () => Describe(Color.Lerp(new Color(100, 100, 100, 100), new Color(200, 200, 200, 200), 2.0f)));
            Record("color/lerp_amount_below_zero", () => Describe(Color.Lerp(new Color(100, 100, 100, 100), new Color(200, 200, 200, 200), -1.0f)));
            Record("color/multiply_half", () => Describe(Color.Multiply(new Color(255, 255, 255, 255), 0.5f)));
            Record("color/multiply_odd_ties", () => Describe(Color.Multiply(new Color(1, 3, 5, 7), 0.5f)));
            Record("color/multiply_above_range", () => Describe(Color.Multiply(new Color(200, 200, 200, 200), 2.0f)));
            Record("color/operator_multiply_half", () => Describe(new Color(255, 255, 255, 255) * 0.5f));

            // ---- Color as IPackedVector -----------------------------------------------------------
            // Whether the explicit interface implementation clamps and rounds like the constructor.
            Record("color/packfromvector4_quarters", () =>
            {
                IPackedVector packed = new Color();
                packed.PackFromVector4(new Vector4(0.25f, 0.5f, 0.75f, 1.0f));
                return Describe((Color)packed);
            });
            Record("color/packfromvector4_out_of_range", () =>
            {
                IPackedVector packed = new Color();
                packed.PackFromVector4(new Vector4(2.0f, -1.0f, 0.5f, 1.0f));
                return Describe((Color)packed);
            });
            Record("color/packfromvector4_nan", () =>
            {
                IPackedVector packed = new Color();
                packed.PackFromVector4(new Vector4(float.NaN, 0.5f, 0.5f, 0.5f));
                return Describe((Color)packed);
            });
            Record("color/packfromvector4_infinities", () =>
            {
                IPackedVector packed = new Color();
                packed.PackFromVector4(new Vector4(float.PositiveInfinity, float.NegativeInfinity, 0.5f, 1.0f));
                return Describe((Color)packed);
            });
            Record("color/packfromvector4_extreme_finite", () =>
            {
                IPackedVector packed = new Color();
                packed.PackFromVector4(new Vector4(1e30f, -1e30f, 0.0f, 0.0f));
                return Describe((Color)packed);
            });
            Record("color/vector4_infinities", () => Describe(new Color(new Vector4(float.PositiveInfinity, float.NegativeInfinity, 0.5f, 1.0f))));
            Record("color/vector4_extreme_finite", () => Describe(new Color(new Vector4(1e30f, -1e30f, 0.0f, 0.0f))));
            Record("color/tovector4_roundtrip", () =>
            {
                Vector4 v = new Color(10, 20, 30, 40).ToVector4();
                return R(v.X) + "," + R(v.Y) + "," + R(v.Z) + "," + R(v.W);
            });

            // ---- PackedVector types: unnormalized integer channels ---------------------------------
            // Byte4/Short2/Short4 take raw channel values, so .5 inputs are exact ties in binary
            // floating point: half-to-even gives 0,2,2,4 where half-away-from-zero gives 1,2,3,4.
            Record("packed/Byte4/ties", () => new Byte4(0.5f, 1.5f, 2.5f, 3.5f).PackedValue.ToString("X8"));
            Record("packed/Byte4/fractions", () => new Byte4(0.4f, 0.6f, 1.4f, 1.6f).PackedValue.ToString("X8"));
            Record("packed/Byte4/vector4_quarters", () => new Byte4(new Vector4(0.25f, 0.5f, 0.75f, 1.0f)).PackedValue.ToString("X8"));
            Record("packed/Byte4/out_of_range", () => new Byte4(300.0f, -5.0f, 255.0f, 0.0f).PackedValue.ToString("X8"));
            Record("packed/Byte4/tovector4", () => { Vector4 v = new Byte4(10.0f, 20.0f, 30.0f, 40.0f).ToVector4(); return R(v.X) + "," + R(v.Y) + "," + R(v.Z) + "," + R(v.W); });
            Record("packed/Byte4/nan_and_infinities", () => new Byte4(float.NaN, float.PositiveInfinity, float.NegativeInfinity, 1e30f).PackedValue.ToString("X8"));
            Record("packed/Short2/ties", () => new Short2(0.5f, 1.5f).PackedValue.ToString("X8"));
            Record("packed/Short2/negative_ties", () => new Short2(-0.5f, -1.5f).PackedValue.ToString("X8"));
            Record("packed/Short2/out_of_range", () => new Short2(40000.0f, -40000.0f).PackedValue.ToString("X8"));
            Record("packed/Short4/ties", () => new Short4(0.5f, 1.5f, 2.5f, 3.5f).PackedValue.ToString("X16"));
            Record("packed/Short4/negative_ties", () => new Short4(-0.5f, -1.5f, -2.5f, -3.5f).PackedValue.ToString("X16"));
            Record("packed/Short4/out_of_range", () => new Short4(40000.0f, -40000.0f, 32767.0f, -32768.0f).PackedValue.ToString("X16"));

            // ---- PackedVector types: normalized channels ------------------------------------------
            Record("packed/Short2/nan_and_infinity", () => new Short2(float.NaN, float.PositiveInfinity).PackedValue.ToString("X8"));
            Record("packed/Short4/nan_and_infinities", () => new Short4(float.NaN, float.PositiveInfinity, float.NegativeInfinity, 1e30f).PackedValue.ToString("X16"));
            // Normalized channels scale by 127/32767 before rounding, so a tie has to be built from
            // the scale itself; these separate half-to-even from half-away-from-zero there too.
            Record("packed/NormalizedByte4/ties", () => new NormalizedByte4(0.5f / 127.0f, 1.5f / 127.0f, 2.5f / 127.0f, 3.5f / 127.0f).PackedValue.ToString("X8"));
            Record("packed/NormalizedByte4/negative_ties", () => new NormalizedByte4(-0.5f / 127.0f, -1.5f / 127.0f, -2.5f / 127.0f, -3.5f / 127.0f).PackedValue.ToString("X8"));
            Record("packed/NormalizedShort4/ties", () => new NormalizedShort4(0.5f / 32767.0f, 1.5f / 32767.0f, 2.5f / 32767.0f, 3.5f / 32767.0f).PackedValue.ToString("X16"));
            Record("packed/NormalizedByte4/nan_and_infinities", () => new NormalizedByte4(float.NaN, float.PositiveInfinity, float.NegativeInfinity, 2.0f).PackedValue.ToString("X8"));
            Record("packed/NormalizedShort4/nan_and_infinities", () => new NormalizedShort4(float.NaN, float.PositiveInfinity, float.NegativeInfinity, 2.0f).PackedValue.ToString("X16"));
            Record("packed/Alpha8/nan", () => new Alpha8(float.NaN).PackedValue.ToString("X2"));
            Record("packed/Alpha8/ties", () => new Alpha8(0.5f / 255.0f).PackedValue.ToString("X2") + "," + new Alpha8(1.5f / 255.0f).PackedValue.ToString("X2") + "," + new Alpha8(2.5f / 255.0f).PackedValue.ToString("X2"));
            Record("packed/Bgr565/ties", () => new Bgr565(0.5f / 31.0f, 0.5f / 63.0f, 1.5f / 31.0f).PackedValue.ToString("X4"));
            Record("packed/Rg32/ties", () => new Rg32(0.5f / 65535.0f, 1.5f / 65535.0f).PackedValue.ToString("X8"));
            Record("packed/Rgba1010102/ties", () => new Rgba1010102(0.5f / 1023.0f, 1.5f / 1023.0f, 2.5f / 1023.0f, 0.5f / 3.0f).PackedValue.ToString("X8"));
            Record("packed/NormalizedByte2/quarters", () => new NormalizedByte2(0.25f, -0.25f).PackedValue.ToString("X4"));
            Record("packed/NormalizedByte4/quarters", () => new NormalizedByte4(0.25f, -0.25f, 0.5f, -0.5f).PackedValue.ToString("X8"));
            Record("packed/NormalizedShort2/quarters", () => new NormalizedShort2(0.25f, -0.25f).PackedValue.ToString("X8"));
            Record("packed/NormalizedShort4/quarters", () => new NormalizedShort4(0.25f, -0.25f, 0.5f, -0.5f).PackedValue.ToString("X16"));
            Record("packed/Alpha8/quarters", () => new Alpha8(0.25f).PackedValue.ToString("X2"));
            Record("packed/Bgr565/quarters", () => new Bgr565(0.25f, 0.5f, 0.75f).PackedValue.ToString("X4"));
            Record("packed/Bgra4444/quarters", () => new Bgra4444(0.25f, 0.5f, 0.75f, 1.0f).PackedValue.ToString("X4"));
            Record("packed/Bgra5551/quarters", () => new Bgra5551(0.25f, 0.5f, 0.75f, 1.0f).PackedValue.ToString("X4"));
            Record("packed/Rg32/quarters", () => new Rg32(0.25f, 0.5f).PackedValue.ToString("X8"));
            Record("packed/Rgba64/quarters", () => new Rgba64(0.25f, 0.5f, 0.75f, 1.0f).PackedValue.ToString("X16"));
            Record("packed/Rgba1010102/quarters", () => new Rgba1010102(0.25f, 0.5f, 0.75f, 1.0f).PackedValue.ToString("X8"));
            Record("packed/Byte4/tostring", () => new Byte4(1.0f, 2.0f, 3.0f, 4.0f).ToString());
            Record("packed/Short2/tostring", () => new Short2(1.0f, 2.0f).ToString());
            Record("packed/HalfVector2/quarters", () => new HalfVector2(0.25f, 0.5f).PackedValue.ToString("X8"));
            Record("packed/HalfVector4/quarters", () => new HalfVector4(0.25f, 0.5f, 0.75f, 1.0f).PackedValue.ToString("X16"));
            Record("packed/HalfSingle/quarter", () => new HalfSingle(0.25f).PackedValue.ToString("X4"));

            Directory.CreateDirectory(outputDirectory);
            File.WriteAllText(Path.Combine(outputDirectory, "framework-packing-oracle.json"),
                "{\n \"producer\": \"Microsoft XNA Game Studio 4.0 framework (Microsoft.Xna.Framework, Microsoft.Xna.Framework.Graphics.PackedVector), driven by tools/xna-pipeline-oracle/framework/FrameworkPackingOracle.cs\",\n \"runtime\": \"" +
                Environment.Version + "\",\n \"frameworkAssembly\": \"" + typeof(Color).Assembly.FullName + "\",\n \"cases\": [\n" +
                string.Join(",\n", Cases.ToArray()) + "\n ]\n}\n");
            Console.WriteLine("recorded " + Cases.Count + " measurements");
        }
    }
}
