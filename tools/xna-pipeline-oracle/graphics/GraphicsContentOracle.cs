// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-098: black-box behaviour oracle for the XNA 4.0 graphics
// content object model (Microsoft.Xna.Framework.Content.Pipeline.Graphics). It runs the genuine
// assemblies and records what they DO -- pixel layouts, conversion rounding, resize filtering,
// DXT block sizes, mipmap chains, validation messages, VectorConverter tables -- so CNA's
// BitmapContent family can be tested against measurements rather than a reading of any other
// implementation. Nothing here inspects XNA's IL; the assemblies are executed, not read.
//
// Output: one JSON document (graphics-content-oracle.json) whose "cases" list has one entry per
// measurement, plus .bin dumps for byte arrays that are too large to inline.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content.Pipeline.Graphics;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Graphics.PackedVector;

namespace Cna.Xna40.GraphicsOracle
{
    internal static class Program
    {
        private static readonly List<string> Cases = new List<string>();
        private static string outputDirectory;

        private static string Escape(string text)
        {
            return text.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\r", "\\r").Replace("\n", "\\n");
        }

        private static string Hex(byte[] bytes)
        {
            if (bytes == null) return "null";
            var builder = new StringBuilder(bytes.Length * 2);
            foreach (byte b in bytes) builder.Append(b.ToString("X2"));
            return builder.ToString();
        }

        private static void Record(string name, string result)
        {
            Cases.Add("  {\"case\": \"" + name + "\", \"result\": \"" + Escape(result) + "\"}");
        }

        private static void Record(string name, Func<string> measurement)
        {
            try
            {
                Record(name, measurement());
            }
            catch (TargetInvocationExceptionShim error)
            {
                Record(name, "throws " + error.Inner.GetType().Name + ": " + error.Inner.Message);
            }
            catch (Exception error)
            {
                Record(name, "throws " + error.GetType().Name + ": " + error.Message);
            }
        }

        private sealed class TargetInvocationExceptionShim : Exception
        {
            public readonly Exception Inner;
            public TargetInvocationExceptionShim(Exception inner) { Inner = inner; }
        }

        private static string Describe(BitmapContent bitmap)
        {
            SurfaceFormat format;
            bool hasFormat = bitmap.TryGetFormat(out format);
            return bitmap.GetType().Name + " " + bitmap.Width + "x" + bitmap.Height + " format=" +
                   (hasFormat ? format.ToString() : "none") + " bytes=" + bitmap.GetPixelData().Length + " ToString=\"" + bitmap + "\"";
        }

        private static string Pixels(PixelBitmapContent<Color> bitmap)
        {
            var builder = new StringBuilder();
            for (int y = 0; y < bitmap.Height; y++)
            {
                for (int x = 0; x < bitmap.Width; x++)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    builder.Append(bitmap.GetPixel(x, y).PackedValue.ToString("X8"));
                }
            }
            return builder.ToString();
        }

        private static string Pixels(PixelBitmapContent<Vector4> bitmap)
        {
            var builder = new StringBuilder();
            for (int y = 0; y < bitmap.Height; y++)
            {
                for (int x = 0; x < bitmap.Width; x++)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    Vector4 v = bitmap.GetPixel(x, y);
                    builder.Append(v.X.ToString("R") + "," + v.Y.ToString("R") + "," + v.Z.ToString("R") + "," + v.W.ToString("R"));
                }
            }
            return builder.ToString();
        }

        private static PixelBitmapContent<Color> Gradient(int width, int height)
        {
            var bitmap = new PixelBitmapContent<Color>(width, height);
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    bitmap.SetPixel(x, y, new Color((byte)(x * 255 / Math.Max(1, width - 1)), (byte)(y * 255 / Math.Max(1, height - 1)), (byte)(37 + 11 * x + 7 * y), (byte)(255 - 40 * y)));
            return bitmap;
        }

        private static PixelBitmapContent<Color> FourPixels()
        {
            var bitmap = new PixelBitmapContent<Color>(2, 2);
            bitmap.SetPixel(0, 0, new Color(0, 0, 0, 0));
            bitmap.SetPixel(1, 0, new Color(255, 0, 0, 255));
            bitmap.SetPixel(0, 1, new Color(0, 255, 0, 128));
            bitmap.SetPixel(1, 1, new Color(10, 20, 30, 40));
            return bitmap;
        }

        private static void ProbePixelType<T>(string typeName) where T : struct, IEquatable<T>
        {
            Record("pixel_type/" + typeName + "/describe", () => Describe(new PixelBitmapContent<T>(3, 2)));
            Record("pixel_type/" + typeName + "/convert_from_color", () =>
            {
                var source = FourPixels();
                var target = new PixelBitmapContent<T>(2, 2);
                BitmapContent.Copy(source, target);
                var back = new PixelBitmapContent<Color>(2, 2);
                BitmapContent.Copy(target, back);
                return "data=" + Hex(target.GetPixelData()) + " back=" + Pixels(back);
            });
            Record("pixel_type/" + typeName + "/surface_format", () =>
            {
                SurfaceFormat format;
                bool ok = VectorConverter.TryGetSurfaceFormat(typeof(T), out format);
                VertexElementFormat vertexFormat;
                bool okVertex = VectorConverter.TryGetVertexElementFormat(typeof(T), out vertexFormat);
                return "surface=" + (ok ? format.ToString() : "none") + " vertex=" + (okVertex ? vertexFormat.ToString() : "none");
            });
            Record("pixel_type/" + typeName + "/converter_vector4", () =>
            {
                Converter<Vector4, T> toT = VectorConverter.GetConverter<Vector4, T>();
                Converter<T, Vector4> fromT = VectorConverter.GetConverter<T, Vector4>();
                Vector4 input = new Vector4(0.25f, 0.5f, 0.75f, 1.0f);
                T packed = toT(input);
                Vector4 back = fromT(packed);
                return "packed=" + packed + " back=" + back.X.ToString("R") + "," + back.Y.ToString("R") + "," + back.Z.ToString("R") + "," + back.W.ToString("R");
            });
        }

        private static void ProbeDxt(string name, Func<int, int, DxtBitmapContent> factory)
        {
            Record("dxt/" + name + "/describe_8x4", () => Describe(factory(8, 4)));
            Record("dxt/" + name + "/describe_5x3", () => Describe(factory(5, 3)));
            Record("dxt/" + name + "/describe_1x1", () => Describe(factory(1, 1)));
            Record("dxt/" + name + "/set_wrong_length", () =>
            {
                var bitmap = factory(8, 4);
                bitmap.SetPixelData(new byte[7]);
                return "accepted";
            });
            Record("dxt/" + name + "/encode_gradient_8x4", () =>
            {
                var bitmap = factory(8, 4);
                BitmapContent.Copy(Gradient(8, 4), bitmap);
                byte[] data = bitmap.GetPixelData();
                var back = new PixelBitmapContent<Color>(8, 4);
                BitmapContent.Copy(bitmap, back);
                return "data=" + Hex(data) + " back=" + Pixels(back);
            });
            Record("dxt/" + name + "/encode_solid_8x4", () =>
            {
                var solid = new PixelBitmapContent<Color>(8, 4);
                for (int y = 0; y < 4; y++) for (int x = 0; x < 8; x++) solid.SetPixel(x, y, new Color(200, 100, 50, 255));
                var bitmap = factory(8, 4);
                BitmapContent.Copy(solid, bitmap);
                var back = new PixelBitmapContent<Color>(8, 4);
                BitmapContent.Copy(bitmap, back);
                return "data=" + Hex(bitmap.GetPixelData()) + " back=" + Pixels(back);
            });
            Record("dxt/" + name + "/encode_transparent_8x4", () =>
            {
                var source = new PixelBitmapContent<Color>(8, 4);
                for (int y = 0; y < 4; y++) for (int x = 0; x < 8; x++) source.SetPixel(x, y, new Color(200, 100, 50, (byte)(x * 36)));
                var bitmap = factory(8, 4);
                BitmapContent.Copy(source, bitmap);
                var back = new PixelBitmapContent<Color>(8, 4);
                BitmapContent.Copy(bitmap, back);
                return "data=" + Hex(bitmap.GetPixelData()) + " back=" + Pixels(back);
            });
            Record("dxt/" + name + "/copy_from_5x3", () =>
            {
                var bitmap = factory(5, 3);
                BitmapContent.Copy(Gradient(5, 3), bitmap);
                return "data=" + Hex(bitmap.GetPixelData());
            });
            Record("dxt/" + name + "/copy_region_into_dxt", () =>
            {
                var bitmap = factory(8, 8);
                BitmapContent.Copy(Gradient(4, 4), new Rectangle(0, 0, 4, 4), bitmap, new Rectangle(4, 4, 4, 4));
                return "data=" + Hex(bitmap.GetPixelData());
            });
        }

        private static void Main(string[] args)
        {
            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            outputDirectory = args.Length > 0 ? args[0] : ".";
            Directory.CreateDirectory(outputDirectory);

            // ---- pixel layout and accessors ------------------------------------------------------
            Record("color/pixel_data_layout", () =>
            {
                var bitmap = new PixelBitmapContent<Color>(2, 1);
                bitmap.SetPixel(0, 0, new Color(1, 2, 3, 4));
                bitmap.SetPixel(1, 0, new Color(250, 251, 252, 253));
                return Hex(bitmap.GetPixelData());
            });
            Record("color/set_pixel_data_roundtrip", () =>
            {
                var bitmap = new PixelBitmapContent<Color>(2, 1);
                bitmap.SetPixelData(new byte[] { 1, 2, 3, 4, 250, 251, 252, 253 });
                return Pixels(bitmap) + " row0=" + bitmap.GetRow(0).Length;
            });
            Record("color/set_pixel_data_wrong_length", () =>
            {
                var bitmap = new PixelBitmapContent<Color>(2, 1);
                bitmap.SetPixelData(new byte[7]);
                return "accepted";
            });
            Record("color/get_pixel_out_of_range", () => new PixelBitmapContent<Color>(2, 1).GetPixel(2, 0).ToString());
            Record("color/set_pixel_out_of_range", () => { new PixelBitmapContent<Color>(2, 1).SetPixel(0, 1, Color.Red); return "accepted"; });
            Record("color/get_row_out_of_range", () => new PixelBitmapContent<Color>(2, 1).GetRow(1).Length.ToString());
            Record("color/replace_color", () =>
            {
                var bitmap = FourPixels();
                bitmap.ReplaceColor(new Color(255, 0, 0, 255), new Color(1, 2, 3, 4));
                return Pixels(bitmap);
            });
            Record("color/ctor_zero_size", () => Describe(new PixelBitmapContent<Color>(0, 0)));
            Record("color/ctor_negative", () => Describe(new PixelBitmapContent<Color>(-1, 4)));
            Record("color/default_pixels", () => Pixels(new PixelBitmapContent<Color>(2, 1)));
            Record("vector4/default_pixels", () => Pixels(new PixelBitmapContent<Vector4>(2, 1)));
            Record("vector4/pixel_data_layout", () =>
            {
                var bitmap = new PixelBitmapContent<Vector4>(1, 1);
                bitmap.SetPixel(0, 0, new Vector4(1, 0.5f, -2, 1e-3f));
                return Hex(bitmap.GetPixelData());
            });

            // ---- every pixel type ----------------------------------------------------------------
            ProbePixelType<Alpha8>("Alpha8");
            ProbePixelType<Bgr565>("Bgr565");
            ProbePixelType<Bgra4444>("Bgra4444");
            ProbePixelType<Bgra5551>("Bgra5551");
            ProbePixelType<Byte4>("Byte4");
            ProbePixelType<Color>("Color");
            ProbePixelType<HalfSingle>("HalfSingle");
            ProbePixelType<HalfVector2>("HalfVector2");
            ProbePixelType<HalfVector4>("HalfVector4");
            ProbePixelType<NormalizedByte2>("NormalizedByte2");
            ProbePixelType<NormalizedByte4>("NormalizedByte4");
            ProbePixelType<NormalizedShort2>("NormalizedShort2");
            ProbePixelType<NormalizedShort4>("NormalizedShort4");
            ProbePixelType<Rg32>("Rg32");
            ProbePixelType<Rgba1010102>("Rgba1010102");
            ProbePixelType<Rgba64>("Rgba64");
            ProbePixelType<Short2>("Short2");
            ProbePixelType<Short4>("Short4");
            ProbePixelType<float>("Single");
            ProbePixelType<Vector2>("Vector2");
            ProbePixelType<Vector3>("Vector3");
            ProbePixelType<Vector4>("Vector4");
            ProbePixelType<byte>("Byte");
            ProbePixelType<int>("Int32");
            ProbePixelType<double>("Double");

            // ---- resizing and regions ------------------------------------------------------------
            Record("resize/4x4_to_2x2", () =>
            {
                var target = new PixelBitmapContent<Color>(2, 2);
                BitmapContent.Copy(Gradient(4, 4), target);
                return Pixels(target);
            });
            Record("resize/2x2_to_4x4", () =>
            {
                var target = new PixelBitmapContent<Color>(4, 4);
                BitmapContent.Copy(FourPixels(), target);
                return Pixels(target);
            });
            Record("resize/3x3_to_2x2", () =>
            {
                var target = new PixelBitmapContent<Color>(2, 2);
                BitmapContent.Copy(Gradient(3, 3), target);
                return Pixels(target);
            });
            Record("resize/4x4_to_2x2_vector4", () =>
            {
                var target = new PixelBitmapContent<Vector4>(2, 2);
                BitmapContent.Copy(Gradient(4, 4), target);
                return Pixels(target);
            });
            Record("region/copy_subrect", () =>
            {
                var target = new PixelBitmapContent<Color>(4, 4);
                BitmapContent.Copy(Gradient(4, 4), new Rectangle(1, 1, 2, 2), target, new Rectangle(0, 0, 2, 2));
                return Pixels(target);
            });
            Record("region/copy_subrect_resized", () =>
            {
                var target = new PixelBitmapContent<Color>(4, 4);
                BitmapContent.Copy(Gradient(4, 4), new Rectangle(0, 0, 2, 2), target, new Rectangle(0, 0, 4, 4));
                return Pixels(target);
            });
            Record("region/source_rect_outside", () =>
            {
                BitmapContent.Copy(Gradient(4, 4), new Rectangle(2, 2, 4, 4), new PixelBitmapContent<Color>(4, 4), new Rectangle(0, 0, 4, 4));
                return "accepted";
            });
            Record("region/dest_rect_outside", () =>
            {
                BitmapContent.Copy(Gradient(4, 4), new Rectangle(0, 0, 4, 4), new PixelBitmapContent<Color>(4, 4), new Rectangle(1, 0, 4, 4));
                return "accepted";
            });
            Record("region/negative_size", () =>
            {
                BitmapContent.Copy(Gradient(4, 4), new Rectangle(0, 0, -1, 4), new PixelBitmapContent<Color>(4, 4), new Rectangle(0, 0, 4, 4));
                return "accepted";
            });
            Record("region/zero_size", () =>
            {
                BitmapContent.Copy(Gradient(4, 4), new Rectangle(0, 0, 0, 0), new PixelBitmapContent<Color>(4, 4), new Rectangle(0, 0, 0, 0));
                return "accepted";
            });
            Record("copy/null_source", () => { BitmapContent.Copy(null, new PixelBitmapContent<Color>(1, 1)); return "accepted"; });
            Record("copy/null_destination", () => { BitmapContent.Copy(new PixelBitmapContent<Color>(1, 1), null); return "accepted"; });
            Record("copy/same_instance", () =>
            {
                var bitmap = FourPixels();
                BitmapContent.Copy(bitmap, bitmap);
                return Pixels(bitmap);
            });
            Record("copy/same_instance_overlapping_regions", () =>
            {
                var bitmap = Gradient(4, 4);
                BitmapContent.Copy(bitmap, new Rectangle(0, 0, 2, 2), bitmap, new Rectangle(1, 1, 2, 2));
                return Pixels(bitmap);
            });

            // ---- DXT ---------------------------------------------------------------------------------
            ProbeDxt("Dxt1", (w, h) => new Dxt1BitmapContent(w, h));
            ProbeDxt("Dxt3", (w, h) => new Dxt3BitmapContent(w, h));
            ProbeDxt("Dxt5", (w, h) => new Dxt5BitmapContent(w, h));
            Record("dxt/Dxt1_to_Dxt5", () =>
            {
                var dxt1 = new Dxt1BitmapContent(8, 4);
                BitmapContent.Copy(Gradient(8, 4), dxt1);
                var dxt5 = new Dxt5BitmapContent(8, 4);
                BitmapContent.Copy(dxt1, dxt5);
                return Hex(dxt5.GetPixelData());
            });
            Record("dxt/Dxt1_to_Vector4", () =>
            {
                var dxt1 = new Dxt1BitmapContent(4, 4);
                BitmapContent.Copy(Gradient(4, 4), dxt1);
                var target = new PixelBitmapContent<Vector4>(4, 4);
                BitmapContent.Copy(dxt1, target);
                return Pixels(target);
            });
            Record("dxt/Dxt1_resize_to_4x4_color", () =>
            {
                var dxt1 = new Dxt1BitmapContent(8, 8);
                BitmapContent.Copy(Gradient(8, 8), dxt1);
                var target = new PixelBitmapContent<Color>(4, 4);
                BitmapContent.Copy(dxt1, target);
                return Pixels(target);
            });

            // ---- MipmapChain, MipmapChainCollection -----------------------------------------------
            Record("mipmapchain/implicit", () => { MipmapChain chain = Gradient(2, 2); return "count=" + chain.Count + " ToString=\"" + chain + "\""; });
            Record("mipmapchain/empty", () => "count=" + new MipmapChain().Count);
            Record("mipmapchain/add_null", () => { new MipmapChain().Add(null); return "accepted"; });
            Record("mipmapchain/set_null", () => { var c = new MipmapChain(Gradient(2, 2)); c[0] = null; return "accepted"; });
            Record("mipmapchain/implicit_null", () => { MipmapChain chain = (BitmapContent)null; return chain == null ? "null chain" : "count=" + chain.Count; });
            Record("mipmapchaincollection/texture2d_default", () =>
            {
                var texture = new Texture2DContent();
                return "faces=" + texture.Faces.Count + " mipmaps=" + texture.Mipmaps.Count + " same=" + object.ReferenceEquals(texture.Faces[0], texture.Mipmaps);
            });
            Record("mipmapchaincollection/texture2d_add_face", () => { new Texture2DContent().Faces.Add(new MipmapChain()); return "accepted"; });
            Record("mipmapchaincollection/texture2d_remove_face", () => { new Texture2DContent().Faces.RemoveAt(0); return "accepted"; });
            Record("mipmapchaincollection/texture2d_clear", () => { new Texture2DContent().Faces.Clear(); return "accepted"; });
            Record("mipmapchaincollection/texture2d_set_face_null", () => { new Texture2DContent().Faces[0] = null; return "accepted"; });
            Record("mipmapchaincollection/texture2d_set_face", () => { var t = new Texture2DContent(); t.Faces[0] = new MipmapChain(Gradient(2, 2)); return "mipmaps=" + t.Mipmaps.Count; });
            Record("mipmapchaincollection/texturecube_default", () => "faces=" + new TextureCubeContent().Faces.Count);
            Record("mipmapchaincollection/texturecube_add_face", () => { new TextureCubeContent().Faces.Add(new MipmapChain()); return "accepted"; });
            Record("mipmapchaincollection/texture3d_default", () => "faces=" + new Texture3DContent().Faces.Count);
            Record("mipmapchaincollection/texture3d_add_face", () => { var t = new Texture3DContent(); t.Faces.Add(new MipmapChain(Gradient(2, 2))); return "faces=" + t.Faces.Count; });

            // ---- TextureContent ------------------------------------------------------------------
            Record("texture/tostring", () => new Texture2DContent().ToString());
            Record("texture/generate_mipmaps_5x3", () =>
            {
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(Gradient(5, 3));
                texture.GenerateMipmaps(false);
                return DescribeChain(texture.Mipmaps);
            });
            Record("texture/generate_mipmaps_8x2", () =>
            {
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(Gradient(8, 2));
                texture.GenerateMipmaps(false);
                return DescribeChain(texture.Mipmaps) + " level1=" + Pixels((PixelBitmapContent<Color>)texture.Mipmaps[1]) + " level2=" + Pixels((PixelBitmapContent<Color>)texture.Mipmaps[2]) + " level3=" + Pixels((PixelBitmapContent<Color>)texture.Mipmaps[3]);
            });
            Record("texture/generate_mipmaps_keeps_existing", () =>
            {
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(Gradient(4, 4));
                texture.Mipmaps.Add(Gradient(2, 2));
                texture.GenerateMipmaps(false);
                return DescribeChain(texture.Mipmaps);
            });
            Record("texture/generate_mipmaps_overwrite", () =>
            {
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(Gradient(4, 4));
                texture.Mipmaps.Add(FourPixels());
                texture.GenerateMipmaps(true);
                return DescribeChain(texture.Mipmaps) + " level1=" + Pixels((PixelBitmapContent<Color>)texture.Mipmaps[1]);
            });
            Record("texture/generate_mipmaps_1x1", () =>
            {
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(Gradient(1, 1));
                texture.GenerateMipmaps(false);
                return DescribeChain(texture.Mipmaps);
            });
            Record("texture/generate_mipmaps_empty", () => { var t = new Texture2DContent(); t.GenerateMipmaps(false); return DescribeChain(t.Mipmaps); });
            Record("texture/generate_mipmaps_vector4", () =>
            {
                var texture = new Texture2DContent();
                var v = new PixelBitmapContent<Vector4>(4, 2);
                BitmapContent.Copy(Gradient(4, 2), v);
                texture.Mipmaps.Add(v);
                texture.GenerateMipmaps(false);
                return DescribeChain(texture.Mipmaps);
            });
            Record("texture/generate_mipmaps_dxt", () =>
            {
                var texture = new Texture2DContent();
                var dxt = new Dxt1BitmapContent(8, 8);
                BitmapContent.Copy(Gradient(8, 8), dxt);
                texture.Mipmaps.Add(dxt);
                texture.GenerateMipmaps(false);
                return DescribeChain(texture.Mipmaps);
            });
            Record("texture/convert_to_vector4", () =>
            {
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(Gradient(4, 4));
                texture.GenerateMipmaps(false);
                texture.ConvertBitmapType(typeof(PixelBitmapContent<Vector4>));
                return DescribeChain(texture.Mipmaps);
            });
            Record("texture/convert_to_dxt1_5x3", () =>
            {
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(Gradient(5, 3));
                texture.ConvertBitmapType(typeof(Dxt1BitmapContent));
                return DescribeChain(texture.Mipmaps);
            });
            Record("texture/convert_to_dxt5_8x8_mips", () =>
            {
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(Gradient(8, 8));
                texture.GenerateMipmaps(false);
                texture.ConvertBitmapType(typeof(Dxt5BitmapContent));
                return DescribeChain(texture.Mipmaps);
            });
            Record("texture/convert_to_non_bitmap", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(2, 2)); t.ConvertBitmapType(typeof(string)); return "accepted"; });
            Record("texture/convert_to_abstract", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(2, 2)); t.ConvertBitmapType(typeof(BitmapContent)); return "accepted"; });
            Record("texture/convert_to_generic_definition", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(2, 2)); t.ConvertBitmapType(typeof(PixelBitmapContent<>)); return "accepted"; });
            Record("texture/convert_null", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(2, 2)); t.ConvertBitmapType(null); return "accepted"; });
            Record("texture/convert_same_type", () => { var t = new Texture2DContent(); var b = Gradient(2, 2); t.Mipmaps.Add(b); t.ConvertBitmapType(typeof(PixelBitmapContent<Color>)); return "same=" + object.ReferenceEquals(b, t.Mipmaps[0]); });

            // ---- Validate ------------------------------------------------------------------------
            Record("validate/2d_empty_null_profile", () => { new Texture2DContent().Validate(null); return "accepted"; });
            Record("validate/2d_empty_reach", () => { new Texture2DContent().Validate(GraphicsProfile.Reach); return "accepted"; });
            Record("validate/2d_5x3_reach", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(5, 3)); t.Validate(GraphicsProfile.Reach); return "accepted"; });
            Record("validate/2d_5x3_hidef", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(5, 3)); t.Validate(GraphicsProfile.HiDef); return "accepted"; });
            Record("validate/2d_5x3_null", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(5, 3)); t.Validate(null); return "accepted"; });
            Record("validate/2d_4x4_reach", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(4, 4)); t.Validate(GraphicsProfile.Reach); return "accepted"; });
            Record("validate/2d_4096x4_reach", () => { var t = new Texture2DContent(); t.Mipmaps.Add(new PixelBitmapContent<Color>(4096, 4)); t.Validate(GraphicsProfile.Reach); return "accepted"; });
            Record("validate/2d_4096x4_hidef", () => { var t = new Texture2DContent(); t.Mipmaps.Add(new PixelBitmapContent<Color>(4096, 4)); t.Validate(GraphicsProfile.HiDef); return "accepted"; });
            Record("validate/2d_8192x4_hidef", () => { var t = new Texture2DContent(); t.Mipmaps.Add(new PixelBitmapContent<Color>(8192, 4)); t.Validate(GraphicsProfile.HiDef); return "accepted"; });
            Record("validate/2d_two_faces", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(4, 4)); t.Faces.Add(new MipmapChain(Gradient(4, 4))); t.Validate(null); return "accepted"; });
            Record("validate/2d_bad_mip_chain", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(4, 4)); t.Mipmaps.Add(Gradient(3, 3)); t.Validate(null); return "accepted"; });
            Record("validate/2d_incomplete_mip_chain", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(4, 4)); t.Mipmaps.Add(Gradient(2, 2)); t.Validate(null); return "accepted"; });
            Record("validate/2d_mixed_types_in_chain", () => { var t = new Texture2DContent(); t.Mipmaps.Add(Gradient(4, 4)); var v = new PixelBitmapContent<Vector4>(2, 2); t.Mipmaps.Add(v); var c = new PixelBitmapContent<Color>(1, 1); t.Mipmaps.Add(c); t.Validate(null); return "accepted"; });
            Record("validate/2d_dxt_5x3_reach", () => { var t = new Texture2DContent(); t.Mipmaps.Add(new Dxt1BitmapContent(5, 3)); t.Validate(GraphicsProfile.Reach); return "accepted"; });
            Record("validate/2d_dxt_5x3_hidef", () => { var t = new Texture2DContent(); t.Mipmaps.Add(new Dxt1BitmapContent(5, 3)); t.Validate(GraphicsProfile.HiDef); return "accepted"; });
            Record("validate/2d_vector4_reach", () => { var t = new Texture2DContent(); t.Mipmaps.Add(new PixelBitmapContent<Vector4>(4, 4)); t.Validate(GraphicsProfile.Reach); return "accepted"; });
            Record("validate/2d_vector4_hidef", () => { var t = new Texture2DContent(); t.Mipmaps.Add(new PixelBitmapContent<Vector4>(4, 4)); t.Validate(GraphicsProfile.HiDef); return "accepted"; });
            Record("validate/2d_short4_null", () => { var t = new Texture2DContent(); t.Mipmaps.Add(new PixelBitmapContent<Short4>(4, 4)); t.Validate(null); return "accepted"; });
            Record("validate/2d_vector3_null", () => { var t = new Texture2DContent(); t.Mipmaps.Add(new PixelBitmapContent<Vector3>(4, 4)); t.Validate(null); return "accepted"; });
            Record("validate/cube_empty", () => { new TextureCubeContent().Validate(null); return "accepted"; });
            Record("validate/cube_six_4x4", () => { var t = new TextureCubeContent(); for (int i = 0; i < 6; i++) t.Faces[i].Add(Gradient(4, 4)); t.Validate(null); return "accepted"; });
            Record("validate/cube_six_4x4_reach", () => { var t = new TextureCubeContent(); for (int i = 0; i < 6; i++) t.Faces[i].Add(Gradient(4, 4)); t.Validate(GraphicsProfile.Reach); return "accepted"; });
            Record("validate/cube_one_face_missing", () => { var t = new TextureCubeContent(); for (int i = 0; i < 5; i++) t.Faces[i].Add(Gradient(4, 4)); t.Validate(null); return "accepted"; });
            Record("validate/cube_non_square", () => { var t = new TextureCubeContent(); for (int i = 0; i < 6; i++) t.Faces[i].Add(Gradient(4, 2)); t.Validate(null); return "accepted"; });
            Record("validate/cube_different_sizes", () => { var t = new TextureCubeContent(); for (int i = 0; i < 6; i++) t.Faces[i].Add(Gradient(i == 3 ? 2 : 4, i == 3 ? 2 : 4)); t.Validate(null); return "accepted"; });
            Record("validate/cube_generate_mipmaps", () => { var t = new TextureCubeContent(); for (int i = 0; i < 6; i++) t.Faces[i].Add(Gradient(4, 4)); t.GenerateMipmaps(false); return "face0=" + DescribeChain(t.Faces[0]) + " face5=" + DescribeChain(t.Faces[5]); });
            Record("validate/3d_empty", () => { new Texture3DContent().Validate(null); return "accepted"; });
            Record("validate/3d_depth2_4x4", () => { var t = new Texture3DContent(); t.Faces.Add(new MipmapChain(Gradient(4, 4))); t.Faces.Add(new MipmapChain(Gradient(4, 4))); t.Validate(null); return "accepted"; });
            Record("validate/3d_depth3_generate_mipmaps", () => { var t = new Texture3DContent(); for (int i = 0; i < 4; i++) t.Faces.Add(new MipmapChain(Gradient(4, 4))); t.GenerateMipmaps(false); var sb = new StringBuilder(); for (int i = 0; i < t.Faces.Count; i++) sb.Append(" face" + i + "=" + DescribeChain(t.Faces[i])); return "faces=" + t.Faces.Count + sb; });
            Record("validate/3d_depth2_different_sizes", () => { var t = new Texture3DContent(); t.Faces.Add(new MipmapChain(Gradient(4, 4))); t.Faces.Add(new MipmapChain(Gradient(2, 2))); t.Validate(null); return "accepted"; });
            Record("validate/3d_reach", () => { var t = new Texture3DContent(); t.Faces.Add(new MipmapChain(Gradient(4, 4))); t.Validate(GraphicsProfile.Reach); return "accepted"; });
            Record("validate/3d_hidef", () => { var t = new Texture3DContent(); t.Faces.Add(new MipmapChain(Gradient(4, 4))); t.Validate(GraphicsProfile.HiDef); return "accepted"; });

            // ---- VectorConverter tables ------------------------------------------------------------
            foreach (SurfaceFormat format in Enum.GetValues(typeof(SurfaceFormat)))
            {
                SurfaceFormat captured = format;
                Record("vectorconverter/surface/" + format, () => { Type type; return VectorConverter.TryGetVectorType(captured, out type) ? type.FullName : "none"; });
            }
            foreach (VertexElementFormat format in Enum.GetValues(typeof(VertexElementFormat)))
            {
                VertexElementFormat captured = format;
                Record("vectorconverter/vertex/" + format, () => { Type type; return VectorConverter.TryGetVectorType(captured, out type) ? type.FullName : "none"; });
            }
            Record("vectorconverter/converter_unsupported", () => { var c = VectorConverter.GetConverter<int, string>(); return c == null ? "null" : "converter"; });
            Record("vectorconverter/converter_identity", () => { var c = VectorConverter.GetConverter<Color, Color>(); return c(new Color(1, 2, 3, 4)).ToString(); });
            Record("vectorconverter/converter_color_to_bgr565", () => { var c = VectorConverter.GetConverter<Color, Bgr565>(); return c(new Color(10, 20, 30, 40)).PackedValue.ToString("X4"); });
            Record("vectorconverter/converter_color_to_single", () => { var c = VectorConverter.GetConverter<Color, float>(); return c(new Color(10, 20, 30, 40)).ToString("R"); });
            Record("vectorconverter/converter_single_to_color", () => { var c = VectorConverter.GetConverter<float, Color>(); return c(0.5f).ToString(); });
            Record("vectorconverter/converter_alpha8_to_color", () => { var c = VectorConverter.GetConverter<Alpha8, Color>(); return c(new Alpha8(0.5f)).ToString(); });
            Record("vectorconverter/converter_color_to_alpha8", () => { var c = VectorConverter.GetConverter<Color, Alpha8>(); return c(new Color(10, 20, 30, 40)).PackedValue.ToString("X2"); });
            Record("vectorconverter/converter_vector2_to_color", () => { var c = VectorConverter.GetConverter<Vector2, Color>(); return c(new Vector2(0.25f, 0.5f)).ToString(); });
            Record("vectorconverter/converter_color_to_vector2", () => { var c = VectorConverter.GetConverter<Color, Vector2>(); Vector2 v = c(new Color(10, 20, 30, 40)); return v.X.ToString("R") + "," + v.Y.ToString("R"); });
            Record("vectorconverter/converter_byte4_to_color", () => { var c = VectorConverter.GetConverter<Byte4, Color>(); return c(new Byte4(10, 20, 30, 40)).ToString(); });
            Record("vectorconverter/converter_vector4_clamped", () => { var c = VectorConverter.GetConverter<Vector4, Color>(); return c(new Vector4(2, -1, 0.5f, 1)).ToString(); });

            // ---- TextureReferenceDictionary ------------------------------------------------------
            Record("texturereferencedictionary/default", () => { var d = new TextureReferenceDictionary(); return "count=" + d.Count + " ToString=\"" + d + "\""; });

            File.WriteAllText(Path.Combine(outputDirectory, "graphics-content-oracle.json"),
                "{\n \"producer\": \"Microsoft XNA Game Studio 4.0 Content Pipeline (Microsoft.Xna.Framework.Content.Pipeline.Graphics), driven by tools/xna-pipeline-oracle/graphics/GraphicsContentOracle.cs\",\n \"runtime\": \"" +
                Environment.Version + "\",\n \"pipelineAssembly\": \"" + typeof(BitmapContent).Assembly.FullName + "\",\n \"cases\": [\n" +
                string.Join(",\n", Cases.ToArray()) + "\n ]\n}\n");
            Console.WriteLine("recorded " + Cases.Count + " measurements");
        }

        private static string DescribeChain(MipmapChain chain)
        {
            var builder = new StringBuilder("count=" + chain.Count);
            foreach (BitmapContent level in chain)
            {
                SurfaceFormat format;
                builder.Append(" [" + level.GetType().Name + " " + level.Width + "x" + level.Height + (level.TryGetFormat(out format) ? " " + format : "") + "]");
            }
            return builder.ToString();
        }
    }
}
