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
using System.Xml;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content.Pipeline;
using Microsoft.Xna.Framework.Content.Pipeline.Graphics;
using Microsoft.Xna.Framework.Content.Pipeline.Processors;
using Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate;
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

        /// The committed corpus of tests/assets/xna40/texture, so the genuine importer and CNA's
        /// read the same bytes rather than two encoders' idea of the same image.
        private static string fixtureDirectory = ".";

        private static void Main(string[] args)
        {
            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            outputDirectory = args.Length > 0 ? args[0] : ".";
            fixtureDirectory = args.Length > 1 ? args[1] : ".";
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
            // Does GetRow hand out the bitmap's own row, or a copy? Writing through the returned
            // array and reading the pixel back is the only way to tell from outside.
            Record("color/get_row_is_live", () =>
            {
                var bitmap = new PixelBitmapContent<Color>(2, 1);
                bitmap.SetPixel(0, 0, Color.Red);
                Color[] row = bitmap.GetRow(0);
                row[0] = Color.Lime;
                return "row=" + row.Length + " pixel=" + bitmap.GetPixel(0, 0);
            });
            // ...and the same question for the pixel data: is GetPixelData a snapshot?
            Record("color/get_pixel_data_is_snapshot", () =>
            {
                var bitmap = new PixelBitmapContent<Color>(1, 1);
                bitmap.SetPixel(0, 0, Color.Red);
                byte[] data = bitmap.GetPixelData();
                data[0] = 0x7F;
                return "pixel=" + bitmap.GetPixel(0, 0);
            });
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

            // ---- FontDescription -------------------------------------------------------------------
            Record("font/ctor3_defaults", () => DescribeFont(new FontDescription("Arial", 14.0f, 2.0f)));
            Record("font/ctor4_style", () => DescribeFont(new FontDescription("Arial", 14.0f, 2.0f, FontDescriptionStyle.Bold)));
            Record("font/ctor5_kerning", () => DescribeFont(new FontDescription("Arial", 14.0f, 2.0f, FontDescriptionStyle.Italic, false)));
            Record("font/ctor5_kerning_true", () => DescribeFont(new FontDescription("Arial", 14.0f, 2.0f, FontDescriptionStyle.Italic, true)));
            Record("font/set_use_kerning", () => { var f = new FontDescription("Arial", 14.0f, 2.0f); f.UseKerning = true; return DescribeFont(f); });
            Record("font/ctor_null_name", () => DescribeFont(new FontDescription(null, 14.0f, 2.0f)));
            Record("font/ctor_empty_name", () => DescribeFont(new FontDescription("", 14.0f, 2.0f)));
            Record("font/ctor_whitespace_name", () => DescribeFont(new FontDescription("   ", 14.0f, 2.0f)));
            Record("font/ctor_negative_size", () => DescribeFont(new FontDescription("Arial", -1.0f, 2.0f)));
            Record("font/ctor_zero_size", () => DescribeFont(new FontDescription("Arial", 0.0f, 2.0f)));
            Record("font/ctor_negative_spacing", () => DescribeFont(new FontDescription("Arial", 14.0f, -3.0f)));
            Record("font/ctor_nan_size", () => DescribeFont(new FontDescription("Arial", float.NaN, 2.0f)));
            Record("font/ctor_undefined_style", () => DescribeFont(new FontDescription("Arial", 14.0f, 2.0f, (FontDescriptionStyle)99)));
            Record("font/set_font_name_null", () => { var f = new FontDescription("Arial", 14.0f, 2.0f); f.FontName = null; return DescribeFont(f); });
            Record("font/set_font_name_empty", () => { var f = new FontDescription("Arial", 14.0f, 2.0f); f.FontName = ""; return DescribeFont(f); });
            Record("font/set_size_negative", () => { var f = new FontDescription("Arial", 14.0f, 2.0f); f.Size = -2.0f; return DescribeFont(f); });
            Record("font/set_spacing_negative", () => { var f = new FontDescription("Arial", 14.0f, 2.0f); f.Spacing = -2.0f; return DescribeFont(f); });
            Record("font/set_style_undefined", () => { var f = new FontDescription("Arial", 14.0f, 2.0f); f.Style = (FontDescriptionStyle)99; return DescribeFont(f); });
            Record("font/set_default_character", () => { var f = new FontDescription("Arial", 14.0f, 2.0f); f.DefaultCharacter = '?'; return DescribeFont(f); });
            Record("font/characters_type", () => new FontDescription("Arial", 14.0f, 2.0f).Characters.GetType().Name);
            Record("font/characters_add_duplicate", () =>
            {
                var f = new FontDescription("Arial", 14.0f, 2.0f);
                f.Characters.Add('a'); f.Characters.Add('b'); f.Characters.Add('a');
                return "count=" + f.Characters.Count + " contains_a=" + f.Characters.Contains('a') + " chars=" + Characters(f);
            });
            Record("font/characters_remove_and_clear", () =>
            {
                var f = new FontDescription("Arial", 14.0f, 2.0f);
                f.Characters.Add('a'); f.Characters.Add('b');
                bool removed = f.Characters.Remove('a');
                bool missing = f.Characters.Remove('z');
                f.Characters.Clear();
                return "removed=" + removed + " missing=" + missing + " count=" + f.Characters.Count + " readonly=" + f.Characters.IsReadOnly;
            });
            Record("font/contentitem_members", () =>
            {
                var f = new FontDescription("Arial", 14.0f, 2.0f);
                return "name=\"" + f.Name + "\" identity=" + (f.Identity == null ? "null" : "set") + " opaquedata=" + f.OpaqueData.Count;
            });
            Record("font/serialize", () =>
            {
                var f = new FontDescription("Segoe UI Mono", 14.0f, 1.5f, FontDescriptionStyle.Bold, false);
                f.DefaultCharacter = '?';
                f.Characters.Add('A'); f.Characters.Add('B');
                return SerializeIntermediate(f);
            });
            Record("font/serialize_minimal", () => SerializeIntermediate(new FontDescription("Arial", 12.0f, 0.0f)));
            Record("font/deserialize_spritefont", () => DescribeFont(DeserializeIntermediate<FontDescription>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                "    <FontName>Segoe UI Mono</FontName>\r\n" +
                "    <Size>14</Size>\r\n" +
                "    <Spacing>0</Spacing>\r\n" +
                "    <UseKerning>true</UseKerning>\r\n" +
                "    <Style>Regular</Style>\r\n" +
                "    <CharacterRegions>\r\n" +
                "      <CharacterRegion>\r\n" +
                "        <Start>&#32;</Start>\r\n" +
                "        <End>&#38;</End>\r\n" +
                "      </CharacterRegion>\r\n" +
                "    </CharacterRegions>\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n")));
            Record("font/deserialize_spritefont_defaultchar", () => DescribeFont(DeserializeIntermediate<FontDescription>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                "    <FontName>Arial</FontName>\r\n" +
                "    <Size>10</Size>\r\n" +
                "    <Style>Italic</Style>\r\n" +
                "    <DefaultCharacter>*</DefaultCharacter>\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n")));
            // Style is required (measured); is anything else? These drop one element at a time.
            Record("font/deserialize_spritefont_no_regions", () => DescribeFont(DeserializeIntermediate<FontDescription>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                "    <FontName>Arial</FontName>\r\n" +
                "    <Size>10</Size>\r\n" +
                "    <Style>Regular</Style>\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n")));
            Record("font/deserialize_spritefont_no_size", () => DescribeFont(DeserializeIntermediate<FontDescription>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                "    <FontName>Arial</FontName>\r\n" +
                "    <Style>Regular</Style>\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n")));
            Record("font/deserialize_spritefont_empty_regions", () => DescribeFont(DeserializeIntermediate<FontDescription>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                "    <FontName>Arial</FontName>\r\n" +
                "    <Size>10</Size>\r\n" +
                "    <Style>Regular</Style>\r\n" +
                "    <CharacterRegions />\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n")));
            Record("font/deserialize_spritefont_roundtrip", () => SerializeIntermediate(DeserializeIntermediate<FontDescription>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                "    <FontName>Arial</FontName>\r\n" +
                "    <Size>10</Size>\r\n" +
                "    <Spacing>2</Spacing>\r\n" +
                "    <UseKerning>true</UseKerning>\r\n" +
                "    <Style>Bold</Style>\r\n" +
                "    <CharacterRegions>\r\n" +
                "      <CharacterRegion><Start>a</Start><End>c</End></CharacterRegion>\r\n" +
                "      <CharacterRegion><Start>x</Start><End>x</End></CharacterRegion>\r\n" +
                "    </CharacterRegions>\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n")));
            Record("font/deserialize_spritefont_no_fontname", () => DescribeFont(DeserializeIntermediate<FontDescription>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                "    <Size>10</Size>\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n")));
            Record("font/deserialize_spritefont_two_regions", () => DescribeFont(DeserializeIntermediate<FontDescription>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                "    <FontName>Arial</FontName>\r\n" +
                "    <Size>10</Size>\r\n" +
                "    <Style>Regular</Style>\r\n" +
                "    <CharacterRegions>\r\n" +
                "      <CharacterRegion><Start>a</Start><End>c</End></CharacterRegion>\r\n" +
                "      <CharacterRegion><Start>c</Start><End>e</End></CharacterRegion>\r\n" +
                "    </CharacterRegions>\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n")));
            Record("font/deserialize_spritefont_reversed_region", () => DescribeFont(DeserializeIntermediate<FontDescription>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                "    <FontName>Arial</FontName>\r\n" +
                "    <Size>10</Size>\r\n" +
                "    <Style>Regular</Style>\r\n" +
                "    <CharacterRegions>\r\n" +
                "      <CharacterRegion><Start>e</Start><End>a</End></CharacterRegion>\r\n" +
                "    </CharacterRegions>\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n")));

            // ---- MaterialContent and the stock materials -------------------------------------------
            Record("material/base_defaults", () => DescribeMaterial(new MaterialContent()));
            Record("material/basic_defaults", () => DescribeMaterial(new BasicMaterialContent()));
            Record("material/basic_properties", () =>
            {
                var m = new BasicMaterialContent();
                m.Alpha = 0.5f;
                m.DiffuseColor = new Vector3(1, 0, 0);
                m.EmissiveColor = new Vector3(0, 1, 0);
                m.SpecularColor = new Vector3(0, 0, 1);
                m.SpecularPower = 16.0f;
                m.VertexColorEnabled = true;
                m.Texture = new ExternalReference<TextureContent>("cat.tga");
                return DescribeMaterial(m) + " read=" + m.Alpha + "," + m.DiffuseColor + "," + m.SpecularPower +
                       "," + m.VertexColorEnabled + "," + (m.Texture == null ? "null" : m.Texture.Filename);
            });
            Record("material/basic_property_cleared", () =>
            {
                var m = new BasicMaterialContent();
                m.Alpha = 0.5f;
                m.Alpha = null;
                return DescribeMaterial(m) + " read=" + (m.Alpha.HasValue ? m.Alpha.ToString() : "null");
            });
            Record("material/basic_texture_cleared", () =>
            {
                var m = new BasicMaterialContent();
                m.Texture = new ExternalReference<TextureContent>("cat.tga");
                m.Texture = null;
                return DescribeMaterial(m) + " read=" + (m.Texture == null ? "null" : m.Texture.Filename);
            });
            Record("material/opaque_data_is_the_store", () =>
            {
                var m = new BasicMaterialContent();
                m.OpaqueData.Add(BasicMaterialContent.AlphaKey, 0.25f);
                return "alpha=" + m.Alpha + " " + DescribeMaterial(m);
            });
            Record("material/value_property_wrong_type", () =>
            {
                var m = new BasicMaterialContent();
                m.OpaqueData.Add(BasicMaterialContent.AlphaKey, "not a float");
                return "alpha=" + m.Alpha;
            });
            Record("material/reference_property_wrong_type", () =>
            {
                var m = new BasicMaterialContent();
                m.OpaqueData.Add(BasicMaterialContent.TextureKey, 42);
                return "texture=" + (m.Texture == null ? "null" : m.Texture.Filename);
            });
            Record("material/value_property_missing", () => "alpha=" + new ProbeMaterial().ReadValue<float>("Nothing"));
            Record("material/reference_property_missing", () => "value=" + (new ProbeMaterial().ReadReference<string>("Nothing") ?? "null"));
            Record("material/get_texture_missing", () => { var t = new ProbeMaterial().ReadTexture("Nothing"); return "texture=" + (t == null ? "null" : t.Filename); });
            Record("material/set_property_null_name", () => { var m = new ProbeMaterial(); m.Write<float?>(null, 1.0f); return DescribeMaterial(m); });
            Record("material/set_property_null_value", () => { var m = new ProbeMaterial(); m.Write<float?>("Alpha", 1.0f); m.Write<float?>("Alpha", null); return DescribeMaterial(m); });
            Record("material/set_property_value_type", () => { var m = new ProbeMaterial(); m.Write("Count", 3); m.Write("Flag", true); return DescribeMaterial(m); });
            Record("material/set_texture_null_name", () => { var m = new ProbeMaterial(); m.WriteTexture(null, new ExternalReference<TextureContent>("cat.tga")); return DescribeMaterial(m); });
            Record("material/set_texture_null_value", () => { var m = new ProbeMaterial(); m.WriteTexture("Slot", null); return DescribeMaterial(m); });
            Record("material/set_texture_then_read", () => { var m = new ProbeMaterial(); m.WriteTexture("Slot", new ExternalReference<TextureContent>("cat.tga")); return DescribeMaterial(m) + " read=" + m.ReadTexture("Slot").Filename; });
            Record("material/read_value_wrong_type", () => { var m = new ProbeMaterial(); m.Write("Alpha", "text"); return "alpha=" + m.ReadValue<float>("Alpha"); });
            Record("material/read_reference_wrong_type", () => { var m = new ProbeMaterial(); m.Write("Ref", 42); return "value=" + (m.ReadReference<string>("Ref") ?? "null"); });
            Record("material/textures_direct", () =>
            {
                var m = new BasicMaterialContent();
                m.Textures.Add("Texture", new ExternalReference<TextureContent>("cat.tga"));
                return "texture=" + (m.Texture == null ? "null" : m.Texture.Filename) + " " + DescribeMaterial(m);
            });
            Record("material/alphatest_properties", () =>
            {
                var m = new AlphaTestMaterialContent();
                m.Alpha = 1.0f;
                m.AlphaFunction = CompareFunction.GreaterEqual;
                m.DiffuseColor = new Vector3(0.5f, 0.5f, 0.5f);
                m.ReferenceAlpha = 128;
                m.VertexColorEnabled = false;
                m.Texture = new ExternalReference<TextureContent>("cat.tga");
                return DescribeMaterial(m) + " function=" + m.AlphaFunction + " reference=" + m.ReferenceAlpha;
            });
            Record("material/dualtexture_properties", () =>
            {
                var m = new DualTextureMaterialContent();
                m.Alpha = 1.0f;
                m.DiffuseColor = Vector3.One;
                m.VertexColorEnabled = true;
                m.Texture = new ExternalReference<TextureContent>("one.tga");
                m.Texture2 = new ExternalReference<TextureContent>("two.tga");
                return DescribeMaterial(m);
            });
            Record("material/environmentmap_properties", () =>
            {
                var m = new EnvironmentMapMaterialContent();
                m.Alpha = 1.0f;
                m.DiffuseColor = Vector3.One;
                m.EmissiveColor = Vector3.Zero;
                m.EnvironmentMapAmount = 0.5f;
                m.EnvironmentMapSpecular = new Vector3(0.25f, 0.25f, 0.25f);
                m.FresnelFactor = 0.75f;
                m.Texture = new ExternalReference<TextureContent>("one.tga");
                m.EnvironmentMap = new ExternalReference<TextureContent>("cube.dds");
                return DescribeMaterial(m);
            });
            Record("material/skinned_properties", () =>
            {
                var m = new SkinnedMaterialContent();
                m.Alpha = 1.0f;
                m.DiffuseColor = Vector3.One;
                m.EmissiveColor = Vector3.Zero;
                m.SpecularColor = Vector3.One;
                m.SpecularPower = 8.0f;
                m.WeightsPerVertex = 2;
                m.Texture = new ExternalReference<TextureContent>("one.tga");
                return DescribeMaterial(m);
            });
            Record("material/effect_properties", () =>
            {
                var m = new EffectMaterialContent();
                m.Effect = new ExternalReference<EffectContent>("shader.fx");
                m.CompiledEffect = new ExternalReference<CompiledEffectContent>("shader.xnb");
                return DescribeMaterial(m) + " effect=" + m.Effect.Filename + " compiled=" + m.CompiledEffect.Filename;
            });
            Record("material/tostring", () => new BasicMaterialContent() + "|" + new MaterialContent() + "|" + new EffectMaterialContent());
            Record("material/serialize_basic", () =>
            {
                var m = new BasicMaterialContent();
                m.Alpha = 0.5f;
                m.DiffuseColor = new Vector3(1, 0, 0);
                m.VertexColorEnabled = true;
                m.Texture = new ExternalReference<TextureContent>("cat.tga");
                return SerializeIntermediate(m);
            });
            Record("material/serialize_empty_basic", () => SerializeIntermediate(new BasicMaterialContent()));
            Record("material/serialize_base", () => SerializeIntermediate(new MaterialContent()));

            // Is OpaqueData a serialized member of ContentItem itself, or something the materials
            // add? These two ask the question on types that are not materials.
            Record("effectcontent/serialize_with_opaquedata", () =>
            {
                var e = new EffectContent();
                e.EffectCode = "technique T { }";
                e.OpaqueData.Add("Note", "hello");
                e.Name = "TheName";
                return SerializeIntermediate(e);
            });
            Record("font/serialize_with_opaquedata", () =>
            {
                var f = new FontDescription("Arial", 12.0f, 0.0f);
                f.OpaqueData.Add("Note", 7);
                f.Name = "TheName";
                return SerializeIntermediate(f);
            });
            Record("material/serialize_with_name", () =>
            {
                var m = new BasicMaterialContent();
                m.Name = "TheName";
                m.Alpha = 1.0f;
                return SerializeIntermediate(m);
            });
            Record("material/deserialize_basic", () =>
            {
                var m = DeserializeIntermediate<BasicMaterialContent>(
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                    "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\" xmlns:Framework=\"Microsoft.Xna.Framework\">\r\n" +
                    "  <Asset Type=\"Graphics:BasicMaterialContent\">\r\n" +
                    "    <OpaqueData>\r\n" +
                    "      <Data Key=\"Alpha\" Type=\"float\">0.5</Data>\r\n" +
                    "      <Data Key=\"DiffuseColor\" Type=\"Framework:Vector3\">1 0 0</Data>\r\n" +
                    "    </OpaqueData>\r\n" +
                    "  </Asset>\r\n" +
                    "</XnaContent>\r\n");
                return DescribeMaterial(m) + " alpha=" + m.Alpha + " diffuse=" + m.DiffuseColor;
            });

            // ---- EffectContent -----------------------------------------------------------------
            Record("effectcontent/defaults", () => { var e = new EffectContent(); return "code=" + (e.EffectCode ?? "null") + " name=\"" + e.Name + "\" opaquedata=" + e.OpaqueData.Count; });
            Record("effectcontent/set_code", () => { var e = new EffectContent(); e.EffectCode = "technique T { }"; return "code=" + e.EffectCode; });
            Record("effectcontent/set_code_null", () => { var e = new EffectContent(); e.EffectCode = "x"; e.EffectCode = null; return "code=" + (e.EffectCode ?? "null"); });
            Record("effectcontent/serialize", () => { var e = new EffectContent(); e.EffectCode = "technique T { }"; return SerializeIntermediate(e); });

            // ---- CompiledEffectContent ---------------------------------------------------------
            Record("compiledeffect/roundtrip", () => { var c = new CompiledEffectContent(new byte[] { 1, 2, 3 }); return "code=" + Hex(c.GetEffectCode()) + " name=\"" + c.Name + "\""; });
            Record("compiledeffect/empty", () => { var c = new CompiledEffectContent(new byte[0]); return "code=" + Hex(c.GetEffectCode()) + " length=" + c.GetEffectCode().Length; });
            Record("compiledeffect/null", () => { var c = new CompiledEffectContent(null); return "code=" + Hex(c.GetEffectCode()); });
            Record("compiledeffect/serialize", () => SerializeIntermediate(new CompiledEffectContent(new byte[] { 1, 2, 3 })));
            Record("effectcontent/serialize_null_code", () => SerializeIntermediate(new EffectContent()));

            // ---- Animation content -----------------------------------------------------------------
            Record("animation/keyframe_members", () =>
            {
                var k = new AnimationKeyframe(TimeSpan.FromSeconds(1.5), Matrix.Identity);
                return "time=" + k.Time + " transform=" + k.Transform.M11 + "," + k.Transform.M44;
            });
            Record("animation/keyframe_set_transform", () =>
            {
                var k = new AnimationKeyframe(TimeSpan.Zero, Matrix.Identity);
                k.Transform = Matrix.CreateTranslation(1, 2, 3);
                return "transform=" + k.Transform.M41 + "," + k.Transform.M42 + "," + k.Transform.M43;
            });
            Record("animation/keyframe_compare", () =>
            {
                var early = new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.Identity);
                var late = new AnimationKeyframe(TimeSpan.FromSeconds(2), Matrix.Identity);
                var same = new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.CreateScale(2));
                return "early_vs_late=" + early.CompareTo(late) + " late_vs_early=" + late.CompareTo(early) +
                       " same=" + early.CompareTo(same) + " equals=" + early.Equals(same);
            });
            Record("animation/keyframe_compare_null", () => "result=" + new AnimationKeyframe(TimeSpan.Zero, Matrix.Identity).CompareTo(null));
            Record("animation/channel_sorted", () =>
            {
                var channel = new AnimationChannel();
                int third = channel.Add(new AnimationKeyframe(TimeSpan.FromSeconds(3), Matrix.Identity));
                int first = channel.Add(new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.Identity));
                int second = channel.Add(new AnimationKeyframe(TimeSpan.FromSeconds(2), Matrix.Identity));
                var times = new StringBuilder();
                foreach (AnimationKeyframe frame in channel) { if (times.Length > 0) times.Append(' '); times.Append(frame.Time.TotalSeconds); }
                return "indices=" + third + "," + first + "," + second + " count=" + channel.Count + " times=" + times;
            });
            Record("animation/channel_duplicate_time", () =>
            {
                var channel = new AnimationChannel();
                channel.Add(new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.Identity));
                int again = channel.Add(new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.CreateScale(2)));
                return "index=" + again + " count=" + channel.Count + " first_m11=" + channel[0].Transform.M11 + " second_m11=" + channel[1].Transform.M11;
            });
            Record("animation/channel_add_null", () => { var channel = new AnimationChannel(); channel.Add(null); return "count=" + channel.Count; });
            Record("animation/channel_indexer_out_of_range", () => new AnimationChannel()[0].Time.ToString());
            Record("animation/channel_contains_and_indexof", () =>
            {
                var channel = new AnimationChannel();
                var frame = new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.Identity);
                channel.Add(frame);
                var equal = new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.Identity);
                return "contains_same=" + channel.Contains(frame) + " contains_equal=" + channel.Contains(equal) +
                       " indexof_same=" + channel.IndexOf(frame) + " indexof_equal=" + channel.IndexOf(equal) +
                       " indexof_missing=" + channel.IndexOf(new AnimationKeyframe(TimeSpan.FromSeconds(9), Matrix.Identity));
            });
            Record("animation/channel_remove", () =>
            {
                var channel = new AnimationChannel();
                var frame = new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.Identity);
                channel.Add(frame);
                channel.Add(new AnimationKeyframe(TimeSpan.FromSeconds(2), Matrix.Identity));
                bool removed = channel.Remove(frame);
                bool missing = channel.Remove(new AnimationKeyframe(TimeSpan.FromSeconds(9), Matrix.Identity));
                channel.RemoveAt(0);
                return "removed=" + removed + " missing=" + missing + " count=" + channel.Count;
            });
            Record("animation/channel_remove_at_out_of_range", () => { var channel = new AnimationChannel(); channel.RemoveAt(0); return "accepted"; });
            Record("animation/channel_clear", () =>
            {
                var channel = new AnimationChannel();
                channel.Add(new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.Identity));
                channel.Clear();
                return "count=" + channel.Count;
            });
            Record("animation/content_defaults", () =>
            {
                var animation = new AnimationContent();
                return "duration=" + animation.Duration + " channels=" + animation.Channels.Count + " name=\"" + animation.Name + "\"";
            });
            Record("animation/content_members", () =>
            {
                var animation = new AnimationContent();
                animation.Duration = TimeSpan.FromSeconds(2.5);
                var channel = new AnimationChannel();
                channel.Add(new AnimationKeyframe(TimeSpan.Zero, Matrix.Identity));
                animation.Channels.Add("Bone1", channel);
                return "duration=" + animation.Duration + " channels=" + animation.Channels.Count +
                       " keys=" + animation.Channels["Bone1"].Count;
            });
            Record("animation/channel_dictionary_null", () => { var d = new AnimationChannelDictionary(); d.Add("A", null); return "count=" + d.Count; });
            Record("animation/content_dictionary_null", () => { var d = new AnimationContentDictionary(); d.Add("A", null); return "count=" + d.Count; });
            Record("animation/serialize_content", () =>
            {
                var animation = new AnimationContent();
                animation.Name = "Walk";
                animation.Duration = TimeSpan.FromSeconds(2);
                var channel = new AnimationChannel();
                channel.Add(new AnimationKeyframe(TimeSpan.Zero, Matrix.Identity));
                channel.Add(new AnimationKeyframe(TimeSpan.FromSeconds(1), Matrix.CreateTranslation(1, 0, 0)));
                animation.Channels.Add("Root", channel);
                return SerializeIntermediate(animation);
            });
            Record("animation/serialize_empty_content", () => SerializeIntermediate(new AnimationContent()));
            Record("animation/serialize_dictionary", () =>
            {
                var dictionary = new AnimationContentDictionary();
                var animation = new AnimationContent();
                animation.Duration = TimeSpan.FromSeconds(1);
                dictionary.Add("Walk", animation);
                return SerializeIntermediate(dictionary);
            });
            Record("animation/deserialize_content", () =>
            {
                var animation = DeserializeIntermediate<AnimationContent>(
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                    "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                    "  <Asset Type=\"Graphics:AnimationContent\">\r\n" +
                    "    <Name>Walk</Name>\r\n" +
                    "    <Duration>PT2S</Duration>\r\n" +
                    "    <Channels>\r\n" +
                    "      <Channel Key=\"Root\">\r\n" +
                    "        <Keyframe>\r\n" +
                    "          <Time>PT1S</Time>\r\n" +
                    "          <Transform>1 0 0 0 0 1 0 0 0 0 1 0 1 0 0 1</Transform>\r\n" +
                    "        </Keyframe>\r\n" +
                    "        <Keyframe>\r\n" +
                    "          <Time>PT0S</Time>\r\n" +
                    "          <Transform>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</Transform>\r\n" +
                    "        </Keyframe>\r\n" +
                    "      </Channel>\r\n" +
                    "    </Channels>\r\n" +
                    "  </Asset>\r\n" +
                    "</XnaContent>\r\n");
                var times = new StringBuilder();
                foreach (AnimationKeyframe frame in animation.Channels["Root"]) { if (times.Length > 0) times.Append(' '); times.Append(frame.Time.TotalSeconds); }
                return "name=\"" + animation.Name + "\" duration=" + animation.Duration + " channels=" + animation.Channels.Count + " times=" + times +
                       " m41=" + animation.Channels["Root"][1].Transform.M41;
            });
            Record("animation/deserialize_no_duration", () => DeserializeIntermediate<AnimationContent>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:AnimationContent\">\r\n" +
                "    <Channels />\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n").Duration.ToString());
            Record("animation/deserialize_keyframe_no_time", () => DeserializeIntermediate<AnimationContent>(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                "  <Asset Type=\"Graphics:AnimationContent\">\r\n" +
                "    <Duration>PT2S</Duration>\r\n" +
                "    <Channels>\r\n" +
                "      <Channel Key=\"Root\">\r\n" +
                "        <Keyframe><Transform>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</Transform></Keyframe>\r\n" +
                "      </Channel>\r\n" +
                "    </Channels>\r\n" +
                "  </Asset>\r\n" +
                "</XnaContent>\r\n").Channels["Root"].Count.ToString());
            Record("animation/tostring", () => new AnimationContent() + "|" + new AnimationChannel() + "|" + new AnimationChannelDictionary() + "|" + new AnimationKeyframe(TimeSpan.Zero, Matrix.Identity));

            // ---- VertexChannelNames ----------------------------------------------------------------
            Record("vertexnames/standard", () => VertexChannelNames.Normal() + "|" + VertexChannelNames.Normal(1) + "|" +
                   VertexChannelNames.Binormal(0) + "|" + VertexChannelNames.Color(2) + "|" + VertexChannelNames.Tangent(3) + "|" +
                   VertexChannelNames.TextureCoordinate(0) + "|" + VertexChannelNames.Weights() + "|" + VertexChannelNames.Weights(4));
            Record("vertexnames/encode_usage", () => VertexChannelNames.EncodeName(VertexElementUsage.Position, 0) + "|" +
                   VertexChannelNames.EncodeName(VertexElementUsage.TextureCoordinate, 7) + "|" +
                   VertexChannelNames.EncodeName(VertexElementUsage.BlendIndices, 1));
            Record("vertexnames/encode_string", () => VertexChannelNames.EncodeName("Custom", 0) + "|" + VertexChannelNames.EncodeName("Custom", 12));
            Record("vertexnames/encode_null", () => VertexChannelNames.EncodeName(null, 0));
            Record("vertexnames/encode_negative", () => VertexChannelNames.EncodeName("Custom", -1));
            Record("vertexnames/decode", () => VertexChannelNames.DecodeBaseName("TextureCoordinate0") + "|" +
                   VertexChannelNames.DecodeUsageIndex("TextureCoordinate0") + "|" +
                   VertexChannelNames.DecodeBaseName("Custom12") + "|" + VertexChannelNames.DecodeUsageIndex("Custom12") + "|" +
                   VertexChannelNames.DecodeBaseName("NoDigits") + "|" + VertexChannelNames.DecodeUsageIndex("NoDigits"));
            Record("vertexnames/decode_null", () => VertexChannelNames.DecodeBaseName(null) ?? "null");
            Record("vertexnames/decode_usage_index_null", () => VertexChannelNames.DecodeUsageIndex(null).ToString());
            Record("vertexnames/try_decode", () =>
            {
                VertexElementUsage usage;
                bool ok = VertexChannelNames.TryDecodeUsage("Normal0", out usage);
                VertexElementUsage other;
                bool no = VertexChannelNames.TryDecodeUsage("Custom0", out other);
                VertexElementUsage third;
                bool bare = VertexChannelNames.TryDecodeUsage("Normal", out third);
                return "normal=" + ok + "," + usage + " custom=" + no + "," + other + " bare=" + bare + "," + third;
            });
            Record("vertexnames/try_decode_null", () => { VertexElementUsage usage; bool ok = VertexChannelNames.TryDecodeUsage(null, out usage); return ok + "," + usage; });

            // ---- BoneWeight and BoneWeightCollection ----------------------------------------------
            Record("boneweight/members", () => { var w = new BoneWeight("Bone1", 0.25f); return "name=" + w.BoneName + " weight=" + w.Weight.ToString("R") + " tostring=" + w; });
            Record("boneweight/null_name", () => new BoneWeight(null, 1.0f).BoneName ?? "null");
            Record("boneweight/empty_name", () => "name=\"" + new BoneWeight("", 1.0f).BoneName + "\"");
            Record("boneweight/negative_weight", () => new BoneWeight("Bone1", -1.0f).Weight.ToString("R"));
            // The constructor refuses a weight outside its range, so these probe the range first and
            // then normalize with weights it accepts.
            Record("boneweight/weight_range", () =>
            {
                var results = new StringBuilder();
                float[] probes = new float[] { 0.0f, 0.0001f, 0.5f, 1.0f, 1.0001f, 2.0f, float.NaN };
                foreach (float probe in probes)
                {
                    if (results.Length > 0) results.Append(' ');
                    try { results.Append(probe.ToString("R") + "=" + new BoneWeight("A", probe).Weight.ToString("R")); }
                    catch (Exception error) { results.Append(probe.ToString("R") + "=" + error.GetType().Name); }
                }
                return results.ToString();
            });
            Record("boneweight/collection_normalize", () =>
            {
                var weights = new BoneWeightCollection();
                weights.Add(new BoneWeight("A", 0.25f));
                weights.Add(new BoneWeight("B", 0.75f));
                weights.NormalizeWeights();
                return Weights(weights);
            });
            Record("boneweight/collection_normalize_unnormalized", () =>
            {
                var weights = new BoneWeightCollection();
                weights.Add(new BoneWeight("A", 0.5f));
                weights.Add(new BoneWeight("B", 0.25f));
                weights.NormalizeWeights();
                return Weights(weights);
            });
            Record("boneweight/collection_normalize_max", () =>
            {
                var weights = new BoneWeightCollection();
                weights.Add(new BoneWeight("A", 0.2f));
                weights.Add(new BoneWeight("B", 0.5f));
                weights.Add(new BoneWeight("C", 0.3f));
                weights.NormalizeWeights(2);
                return Weights(weights);
            });
            Record("boneweight/collection_normalize_zero_total", () =>
            {
                var weights = new BoneWeightCollection();
                weights.NormalizeWeights();
                return Weights(weights);
            });
            Record("boneweight/collection_normalize_empty", () => { var weights = new BoneWeightCollection(); weights.NormalizeWeights(); return Weights(weights); });
            Record("boneweight/collection_normalize_negative_max", () => { var weights = new BoneWeightCollection(); weights.Add(new BoneWeight("A", 1.0f)); weights.NormalizeWeights(-1); return Weights(weights); });
            Record("boneweight/collection_normalize_zero_max", () => { var weights = new BoneWeightCollection(); weights.Add(new BoneWeight("A", 1.0f)); weights.NormalizeWeights(0); return Weights(weights); });
            Record("boneweight/collection_normalize_more_than_count", () => { var weights = new BoneWeightCollection(); weights.Add(new BoneWeight("A", 1.0f)); weights.NormalizeWeights(4); return Weights(weights); });
            Record("boneweight/collection_normalize_ties", () =>
            {
                var weights = new BoneWeightCollection();
                weights.Add(new BoneWeight("A", 0.25f));
                weights.Add(new BoneWeight("B", 0.25f));
                weights.Add(new BoneWeight("C", 0.5f));
                weights.NormalizeWeights(2);
                return Weights(weights);
            });
            Record("boneweight/default_value", () => { var w = default(BoneWeight); return "name=" + (w.BoneName ?? "null") + " weight=" + w.Weight.ToString("R"); });
            Record("boneweight/serialize", () =>
            {
                var weights = new BoneWeightCollection();
                weights.Add(new BoneWeight("A", 0.25f));
                weights.Add(new BoneWeight("B", 0.75f));
                return SerializeIntermediate(weights);
            });
            Record("boneweight/deserialize", () =>
            {
                var weights = DeserializeIntermediate<BoneWeightCollection>(
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                    "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                    "  <Asset Type=\"Graphics:BoneWeightCollection\">\r\n    <Item />\r\n  </Asset>\r\n" +
                    "</XnaContent>\r\n");
                return Weights(weights);
            });

            Record("indexcollection/addrange", () =>
            {
                var indices = new IndexCollection();
                indices.AddRange(new int[] { 3, 1, 2 });
                indices.Add(4);
                return "count=" + indices.Count + " items=" + string.Join(",", Array.ConvertAll(new List<int>(indices).ToArray(), i => i.ToString()));
            });
            Record("indexcollection/addrange_null", () => { var indices = new IndexCollection(); indices.AddRange(null); return "count=" + indices.Count; });
            Record("indexcollection/serialize", () => { var indices = new IndexCollection(); indices.AddRange(new int[] { 0, 1, 2 }); return SerializeIntermediate(indices); });
            Record("positioncollection/basics", () =>
            {
                var positions = new PositionCollection();
                positions.Add(new Vector3(1, 2, 3));
                positions.Add(new Vector3(4, 5, 6));
                return "count=" + positions.Count + " first=" + positions[0] + " contains=" + positions.Contains(new Vector3(4, 5, 6)) +
                       " indexof=" + positions.IndexOf(new Vector3(4, 5, 6));
            });
            Record("positioncollection/serialize", () => { var positions = new PositionCollection(); positions.Add(new Vector3(1, 2, 3)); return SerializeIntermediate(positions); });

            // ---- VertexContent, VertexChannel, VertexChannelCollection -----------------------------
            Record("vertexcontent/defaults", () =>
            {
                var geometry = new GeometryContent();
                return DescribeVertices(geometry.Vertices) + " geometry_indices=" + geometry.Indices.Count +
                       " material=" + (geometry.Material == null ? "null" : "set") +
                       " parent=" + (geometry.Parent == null ? "null" : "set");
            });
            Record("vertexcontent/add_positions", () =>
            {
                var mesh = new MeshContent();
                mesh.Positions.Add(new Vector3(0, 0, 0));
                mesh.Positions.Add(new Vector3(1, 0, 0));
                mesh.Positions.Add(new Vector3(0, 1, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1, 2 });
                return DescribeVertices(geometry.Vertices) + " positions=" + Positions(geometry.Vertices.Positions);
            });
            Record("vertexcontent/add_without_parent", () => { var geometry = new GeometryContent(); geometry.Vertices.Add(0); return DescribeVertices(geometry.Vertices); });
            Record("vertexcontent/insert_and_remove", () =>
            {
                var mesh = new MeshContent();
                for (int i = 0; i < 4; i++) mesh.Positions.Add(new Vector3(i, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1, 2 });
                geometry.Vertices.Insert(1, 3);
                geometry.Vertices.RemoveAt(0);
                geometry.Vertices.InsertRange(0, new int[] { 2, 2 });
                geometry.Vertices.RemoveRange(0, 1);
                return DescribeVertices(geometry.Vertices) + " positions=" + Positions(geometry.Vertices.Positions);
            });
            Record("vertexcontent/channel_add", () =>
            {
                var mesh = new MeshContent();
                mesh.Positions.Add(new Vector3(0, 0, 0));
                mesh.Positions.Add(new Vector3(1, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1 });
                VertexChannel<Vector2> channel = geometry.Vertices.Channels.Add<Vector2>(VertexChannelNames.TextureCoordinate(0), new Vector2[] { new Vector2(0, 0), new Vector2(1, 1) });
                return "name=" + channel.Name + " count=" + channel.Count + " element=" + channel.ElementType.Name +
                       " first=" + channel[0] + " channels=" + geometry.Vertices.Channels.Count;
            });
            Record("vertexcontent/channel_add_wrong_count", () =>
            {
                var mesh = new MeshContent();
                mesh.Positions.Add(new Vector3(0, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0 });
                geometry.Vertices.Channels.Add<Vector2>("Custom0", new Vector2[] { new Vector2(0, 0), new Vector2(1, 1) });
                return "accepted";
            });
            Record("vertexcontent/channel_add_null_data", () =>
            {
                var geometry = new GeometryContent();
                geometry.Vertices.Channels.Add<Vector2>("Custom0", null);
                return "count=" + geometry.Vertices.Channels.Count + " channel_count=" + geometry.Vertices.Channels[0].Count;
            });
            Record("vertexcontent/channel_add_duplicate", () =>
            {
                var geometry = new GeometryContent();
                geometry.Vertices.Channels.Add<Vector2>("Custom0", null);
                geometry.Vertices.Channels.Add<Vector2>("Custom0", null);
                return "count=" + geometry.Vertices.Channels.Count;
            });
            Record("vertexcontent/channel_lookup", () =>
            {
                var geometry = new GeometryContent();
                geometry.Vertices.Channels.Add<Vector2>("Custom0", null);
                VertexChannel byName = geometry.Vertices.Channels["Custom0"];
                VertexChannel byIndex = geometry.Vertices.Channels[0];
                return "same=" + object.ReferenceEquals(byName, byIndex) + " contains=" + geometry.Vertices.Channels.Contains("Custom0") +
                       " indexof=" + geometry.Vertices.Channels.IndexOf("Custom0") + " missing=" + geometry.Vertices.Channels.Contains("None") +
                       " indexof_missing=" + geometry.Vertices.Channels.IndexOf("None");
            });
            Record("vertexcontent/channel_lookup_missing", () => { var geometry = new GeometryContent(); return geometry.Vertices.Channels["None"].Name; });
            Record("vertexcontent/channel_get_typed", () =>
            {
                var geometry = new GeometryContent();
                geometry.Vertices.Channels.Add<Vector2>("Custom0", null);
                VertexChannel<Vector2> typed = geometry.Vertices.Channels.Get<Vector2>("Custom0");
                return "name=" + typed.Name + " element=" + typed.ElementType.Name;
            });
            Record("vertexcontent/channel_get_wrong_type", () =>
            {
                var geometry = new GeometryContent();
                geometry.Vertices.Channels.Add<Vector2>("Custom0", null);
                return geometry.Vertices.Channels.Get<Vector3>("Custom0").ElementType.Name;
            });
            Record("vertexcontent/channel_convert", () =>
            {
                var mesh = new MeshContent();
                mesh.Positions.Add(new Vector3(0, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0 });
                geometry.Vertices.Channels.Add<Vector2>("Custom0", new Vector2[] { new Vector2(0.25f, 0.5f) });
                VertexChannel<Vector4> converted = geometry.Vertices.Channels.ConvertChannelContent<Vector4>("Custom0");
                return "element=" + converted.ElementType.Name + " value=" + converted[0] + " channels=" + geometry.Vertices.Channels.Count +
                       " same_name=" + converted.Name;
            });
            Record("vertexcontent/channel_read_converted", () =>
            {
                var mesh = new MeshContent();
                mesh.Positions.Add(new Vector3(0, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0 });
                geometry.Vertices.Channels.Add<Vector2>("Custom0", new Vector2[] { new Vector2(0.25f, 0.5f) });
                var read = new StringBuilder();
                foreach (Vector4 value in geometry.Vertices.Channels[0].ReadConvertedContent<Vector4>()) read.Append(value);
                return "values=" + read;
            });
            Record("vertexcontent/channel_remove", () =>
            {
                var geometry = new GeometryContent();
                geometry.Vertices.Channels.Add<Vector2>("A0", null);
                geometry.Vertices.Channels.Add<Vector2>("B0", null);
                bool removed = geometry.Vertices.Channels.Remove("A0");
                bool missing = geometry.Vertices.Channels.Remove("None");
                geometry.Vertices.Channels.RemoveAt(0);
                return "removed=" + removed + " missing=" + missing + " count=" + geometry.Vertices.Channels.Count;
            });
            Record("vertexcontent/channel_insert", () =>
            {
                var geometry = new GeometryContent();
                geometry.Vertices.Channels.Add<Vector2>("A0", null);
                geometry.Vertices.Channels.Insert<Vector3>(0, "B0", null);
                var names = new StringBuilder();
                foreach (VertexChannel channel in geometry.Vertices.Channels) { if (names.Length > 0) names.Append(' '); names.Append(channel.Name); }
                return "names=" + names + " clear_then=" + ClearedChannelCount(geometry.Vertices.Channels);
            });
            Record("vertexcontent/indirect_positions", () =>
            {
                var mesh = new MeshContent();
                mesh.Positions.Add(new Vector3(7, 0, 0));
                mesh.Positions.Add(new Vector3(8, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 1, 0, 1 });
                IndirectPositionCollection positions = geometry.Vertices.Positions;
                return "count=" + positions.Count + " items=" + Positions(positions) + " contains=" + positions.Contains(new Vector3(7, 0, 0)) +
                       " indexof=" + positions.IndexOf(new Vector3(7, 0, 0)) + " missing=" + positions.IndexOf(new Vector3(9, 0, 0));
            });
            Record("vertexcontent/create_vertex_buffer", () =>
            {
                var mesh = new MeshContent();
                mesh.Positions.Add(new Vector3(0, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0 });
                VertexBufferContent buffer = geometry.Vertices.CreateVertexBuffer();
                return "bytes=" + buffer.VertexData.Length + " stride=" + buffer.VertexDeclaration.VertexStride +
                       " elements=" + buffer.VertexDeclaration.VertexElements.Count;
            });
            Record("vertexcontent/add_with_channel", () =>
            {
                var mesh = new MeshContent();
                for (int i = 0; i < 3; i++) mesh.Positions.Add(new Vector3(i, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1 });
                var channel = geometry.Vertices.Channels.Add<Vector2>("Custom0", new Vector2[] { new Vector2(1, 1), new Vector2(2, 2) });
                geometry.Vertices.Add(2);
                var values = new StringBuilder();
                foreach (Vector2 value in channel) { if (values.Length > 0) values.Append(' '); values.Append(value); }
                return "count=" + geometry.Vertices.VertexCount + " channel=" + channel.Count + " values=" + values;
            });
            Record("vertexcontent/remove_with_channel", () =>
            {
                var mesh = new MeshContent();
                for (int i = 0; i < 3; i++) mesh.Positions.Add(new Vector3(i, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1, 2 });
                var channel = geometry.Vertices.Channels.Add<Vector2>("Custom0", new Vector2[] { new Vector2(1, 1), new Vector2(2, 2), new Vector2(3, 3) });
                geometry.Vertices.RemoveAt(1);
                var values = new StringBuilder();
                foreach (Vector2 value in channel) { if (values.Length > 0) values.Append(' '); values.Append(value); }
                return "count=" + geometry.Vertices.VertexCount + " channel=" + channel.Count + " values=" + values;
            });
            Record("vertexcontent/insert_with_channel", () =>
            {
                var mesh = new MeshContent();
                for (int i = 0; i < 3; i++) mesh.Positions.Add(new Vector3(i, 0, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 2 });
                var channel = geometry.Vertices.Channels.Add<Vector2>("Custom0", new Vector2[] { new Vector2(1, 1), new Vector2(3, 3) });
                geometry.Vertices.Insert(1, 1);
                var values = new StringBuilder();
                foreach (Vector2 value in channel) { if (values.Length > 0) values.Append(' '); values.Append(value); }
                return "count=" + geometry.Vertices.VertexCount + " channel=" + channel.Count + " values=" + values;
            });
            Record("vertexcontent/tostring", () => new GeometryContent().Vertices + "|" + new GeometryContent().Vertices.Channels + "|" + new GeometryContent().Vertices.PositionIndices);

            // ---- NodeContent, MeshContent, GeometryContent -----------------------------------------
            Record("node/defaults", () =>
            {
                var node = new NodeContent();
                return "transform=" + Describe(node.Transform) + " absolute=" + Describe(node.AbsoluteTransform) +
                       " children=" + node.Children.Count + " animations=" + node.Animations.Count +
                       " parent=" + (node.Parent == null ? "null" : "set") + " name=\"" + node.Name + "\"";
            });
            Record("node/absolute_transform", () =>
            {
                var root = new NodeContent();
                root.Transform = Matrix.CreateTranslation(1, 0, 0);
                var child = new NodeContent();
                child.Transform = Matrix.CreateTranslation(0, 2, 0);
                root.Children.Add(child);
                var grandchild = new NodeContent();
                grandchild.Transform = Matrix.CreateTranslation(0, 0, 3);
                child.Children.Add(grandchild);
                return "child_parent=" + (child.Parent == root) + " child_absolute=" + Describe(child.AbsoluteTransform) +
                       " grandchild_absolute=" + Describe(grandchild.AbsoluteTransform);
            });
            Record("node/reparent", () =>
            {
                var first = new NodeContent();
                var second = new NodeContent();
                var child = new NodeContent();
                first.Children.Add(child);
                second.Children.Add(child);
                return "first=" + first.Children.Count + " second=" + second.Children.Count + " parent_is_second=" + (child.Parent == second);
            });
            Record("node/add_null_child", () => { var node = new NodeContent(); node.Children.Add(null); return "count=" + node.Children.Count; });
            Record("node/remove_child", () =>
            {
                var root = new NodeContent();
                var child = new NodeContent();
                root.Children.Add(child);
                root.Children.Remove(child);
                return "count=" + root.Children.Count + " parent=" + (child.Parent == null ? "null" : "set");
            });
            Record("node/clear_children", () =>
            {
                var root = new NodeContent();
                var child = new NodeContent();
                root.Children.Add(child);
                root.Children.Clear();
                return "count=" + root.Children.Count + " parent=" + (child.Parent == null ? "null" : "set");
            });
            Record("node/bone_is_a_node", () => { var bone = new BoneContent(); return "children=" + bone.Children.Count + " tostring=" + bone; });
            Record("mesh/defaults", () => { var mesh = new MeshContent(); return "positions=" + mesh.Positions.Count + " geometry=" + mesh.Geometry.Count + " children=" + mesh.Children.Count; });
            Record("mesh/geometry_parent", () =>
            {
                var mesh = new MeshContent();
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                bool parented = geometry.Parent == mesh;
                mesh.Geometry.Remove(geometry);
                return "parented=" + parented + " after_remove=" + (geometry.Parent == null ? "null" : "set") + " count=" + mesh.Geometry.Count;
            });
            Record("mesh/geometry_add_null", () => { var mesh = new MeshContent(); mesh.Geometry.Add(null); return "count=" + mesh.Geometry.Count; });
            Record("geometry/material", () =>
            {
                var geometry = new GeometryContent();
                geometry.Material = new BasicMaterialContent();
                return "material=" + geometry.Material.GetType().Name + " indices=" + geometry.Indices.Count;
            });
            Record("node/serialize", () =>
            {
                var root = new NodeContent();
                root.Name = "Root";
                root.Transform = Matrix.CreateTranslation(1, 2, 3);
                var child = new BoneContent();
                child.Name = "Bone";
                root.Children.Add(child);
                return SerializeIntermediate(root);
            });
            Record("mesh/serialize", () =>
            {
                var mesh = new MeshContent();
                mesh.Name = "Mesh";
                mesh.Positions.Add(new Vector3(0, 0, 0));
                mesh.Positions.Add(new Vector3(1, 0, 0));
                mesh.Positions.Add(new Vector3(0, 1, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1, 2 });
                geometry.Indices.AddRange(new int[] { 0, 1, 2 });
                geometry.Material = new BasicMaterialContent();
                geometry.Vertices.Channels.Add<Vector2>(VertexChannelNames.TextureCoordinate(0), new Vector2[] { new Vector2(0, 0), new Vector2(1, 0), new Vector2(0, 1) });
                return SerializeIntermediate(mesh);
            });
            Record("node/serialize_with_animation", () =>
            {
                var root = new NodeContent();
                root.Name = "Root";
                var animation = new AnimationContent();
                animation.Duration = TimeSpan.FromSeconds(1);
                root.Animations.Add("Walk", animation);
                var child = new NodeContent();
                root.Children.Add(child);
                return SerializeIntermediate(root);
            });
            Record("node/deserialize_minimal", () =>
            {
                var root = DeserializeIntermediate<NodeContent>(
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                    "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                    "  <Asset Type=\"Graphics:NodeContent\">\r\n" +
                    "    <Name>Root</Name>\r\n" +
                    "    <Transform>1 0 0 0 0 1 0 0 0 0 1 0 1 2 3 1</Transform>\r\n" +
                    "    <Children>\r\n" +
                    "      <Child Type=\"Graphics:BoneContent\">\r\n" +
                    "        <Name>Bone</Name>\r\n" +
                    "        <Transform>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</Transform>\r\n" +
                    "      </Child>\r\n" +
                    "    </Children>\r\n" +
                    "  </Asset>\r\n" +
                    "</XnaContent>\r\n");
                return "name=\"" + root.Name + "\" children=" + root.Children.Count + " child=" + root.Children[0].GetType().Name +
                       " child_parent=" + (root.Children[0].Parent == root) + " transform=" + Describe(root.Transform);
            });
            Record("mesh/deserialize", () =>
            {
                var mesh = DeserializeIntermediate<MeshContent>(
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                    "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\" xmlns:Framework=\"Microsoft.Xna.Framework\">\r\n" +
                    "  <Asset Type=\"Graphics:MeshContent\">\r\n" +
                    "    <Name>Mesh</Name>\r\n" +
                    "    <Transform>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</Transform>\r\n" +
                    "    <Positions>0 0 0 1 0 0 0 1 0</Positions>\r\n" +
                    "    <Geometry>\r\n" +
                    "      <Batch>\r\n" +
                    "        <Indices>0 1 2</Indices>\r\n" +
                    "        <Vertices>\r\n" +
                    "          <PositionIndices>0 1 2</PositionIndices>\r\n" +
                    "          <Channels>\r\n" +
                    "            <VertexChannel Name=\"TextureCoordinate0\" ElementType=\"Framework:Vector2\">0 0 1 0 0 1</VertexChannel>\r\n" +
                    "          </Channels>\r\n" +
                    "        </Vertices>\r\n" +
                    "      </Batch>\r\n" +
                    "    </Geometry>\r\n" +
                    "  </Asset>\r\n" +
                    "</XnaContent>\r\n");
                var geometry = mesh.Geometry[0];
                return "positions=" + mesh.Positions.Count + " geometry=" + mesh.Geometry.Count + " indices=" + geometry.Indices.Count +
                       " vertices=" + geometry.Vertices.VertexCount + " channels=" + geometry.Vertices.Channels.Count +
                       " channel=" + geometry.Vertices.Channels[0].Name + " element=" + geometry.Vertices.Channels[0].ElementType.Name +
                       " parent=" + (geometry.Parent == mesh);
            });
            Record("node/deserialize", () =>
            {
                var root = DeserializeIntermediate<NodeContent>(
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" +
                    "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                    "  <Asset Type=\"Graphics:NodeContent\">\r\n" +
                    "    <Name>Root</Name>\r\n" +
                    "    <Transform>1 0 0 0 0 1 0 0 0 0 1 0 1 2 3 1</Transform>\r\n" +
                    "    <Animations />\r\n" +
                    "    <Children>\r\n" +
                    "      <Child Type=\"Graphics:BoneContent\">\r\n" +
                    "        <Name>Bone</Name>\r\n" +
                    "        <Transform>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</Transform>\r\n" +
                    "        <Animations />\r\n" +
                    "        <Children />\r\n" +
                    "      </Child>\r\n" +
                    "    </Children>\r\n" +
                    "  </Asset>\r\n" +
                    "</XnaContent>\r\n");
                return "name=\"" + root.Name + "\" children=" + root.Children.Count + " child=" + root.Children[0].GetType().Name +
                       " child_parent=" + (root.Children[0].Parent == root) + " transform=" + Describe(root.Transform);
            });

            // ---- Processor defaults ----------------------------------------------------------------
            // Every processor's properties as constructed, read back through reflection so the
            // list cannot drift from the type. This is the measured form of the defaults table.
            Record("processor/TextureProcessor", () => Properties(new TextureProcessor()));
            Record("processor/SpriteTextureProcessor", () => Properties(new SpriteTextureProcessor()));
            Record("processor/ModelTextureProcessor", () => Properties(new ModelTextureProcessor()));
            Record("processor/FontDescriptionProcessor", () => Properties(new FontDescriptionProcessor()));
            Record("processor/FontTextureProcessor", () => Properties(new FontTextureProcessor()));
            Record("processor/MaterialProcessor", () => Properties(new MaterialProcessor()));
            Record("processor/ModelProcessor", () => Properties(new ModelProcessor()));
            Record("processor/EffectProcessor", () => Properties(new EffectProcessor()));
            Record("processor/PassThroughProcessor", () => Properties(new PassThroughProcessor()));
            Record("processor/SongProcessor", () => Properties(new SongProcessor()));
            Record("processor/SoundEffectProcessor", () => Properties(new SoundEffectProcessor()));
            Record("processor/VideoProcessor", () => Properties(new VideoProcessor()));
            Record("processor/types", () =>
            {
                var builder = new StringBuilder();
                object[] processors = new object[] { new TextureProcessor(), new SpriteTextureProcessor(), new ModelTextureProcessor(),
                                                     new FontDescriptionProcessor(), new FontTextureProcessor(), new MaterialProcessor(),
                                                     new ModelProcessor(), new EffectProcessor(), new PassThroughProcessor(),
                                                     new SongProcessor(), new SoundEffectProcessor(), new VideoProcessor() };
                foreach (object processor in processors)
                {
                    var typed = (IContentProcessor)processor;
                    if (builder.Length > 0) builder.Append(' ');
                    builder.Append(processor.GetType().Name + "=" + typed.InputType.Name + "->" + typed.OutputType.Name);
                }
                return builder.ToString();
            });
            Record("processor/effect_defines", () => { var p = new EffectProcessor(); p.Defines = "A=1;B"; return "defines=" + p.Defines + " debug=" + p.DebugMode; });
            Record("processor/font_texture_first_character", () => { var p = new FontTextureProcessor(); return "first=U+" + ((int)p.FirstCharacter).ToString("X4"); });

            // ---- TextureProcessor.Process ----------------------------------------------------------
            // The processor runs against a context of our own, because XNA's build context is
            // internal; everything the processor actually needs from it is answered here.
            Record("textureprocessor/defaults_4x4", () =>
            {
                var processor = new TextureProcessor();
                return DescribeTexture(Process(processor, ColorTexture(4, 4)));
            });
            Record("textureprocessor/color_key", () =>
            {
                var processor = new TextureProcessor();
                var texture = new Texture2DContent();
                var bitmap = new PixelBitmapContent<Color>(2, 1);
                bitmap.SetPixel(0, 0, new Color(255, 0, 255, 255));
                bitmap.SetPixel(1, 0, new Color(10, 20, 30, 255));
                texture.Mipmaps.Add(bitmap);
                return DescribeTexture(Process(processor, texture));
            });
            Record("textureprocessor/color_key_disabled", () =>
            {
                var processor = new TextureProcessor();
                processor.ColorKeyEnabled = false;
                var texture = new Texture2DContent();
                var bitmap = new PixelBitmapContent<Color>(2, 1);
                bitmap.SetPixel(0, 0, new Color(255, 0, 255, 255));
                bitmap.SetPixel(1, 0, new Color(10, 20, 30, 255));
                texture.Mipmaps.Add(bitmap);
                return DescribeTexture(Process(processor, texture));
            });
            Record("textureprocessor/premultiply", () =>
            {
                var processor = new TextureProcessor();
                processor.ColorKeyEnabled = false;
                var texture = new Texture2DContent();
                var bitmap = new PixelBitmapContent<Color>(1, 1);
                bitmap.SetPixel(0, 0, new Color(255, 128, 0, 128));
                texture.Mipmaps.Add(bitmap);
                return DescribeTexture(Process(processor, texture));
            });
            Record("textureprocessor/no_premultiply", () =>
            {
                var processor = new TextureProcessor();
                processor.ColorKeyEnabled = false;
                processor.PremultiplyAlpha = false;
                var texture = new Texture2DContent();
                var bitmap = new PixelBitmapContent<Color>(1, 1);
                bitmap.SetPixel(0, 0, new Color(255, 128, 0, 128));
                texture.Mipmaps.Add(bitmap);
                return DescribeTexture(Process(processor, texture));
            });
            Record("textureprocessor/mipmaps", () =>
            {
                var processor = new TextureProcessor();
                processor.GenerateMipmaps = true;
                return DescribeTexture(Process(processor, ColorTexture(4, 4)));
            });
            Record("textureprocessor/resize_to_power_of_two", () =>
            {
                var processor = new TextureProcessor();
                processor.ResizeToPowerOfTwo = true;
                return DescribeTexture(Process(processor, ColorTexture(3, 5)));
            });
            Record("textureprocessor/dxt", () =>
            {
                var processor = new TextureProcessor();
                processor.TextureFormat = TextureProcessorOutputFormat.DxtCompressed;
                return DescribeTexture(Process(processor, ColorTexture(4, 4)));
            });
            Record("textureprocessor/dxt_opaque", () =>
            {
                var processor = new TextureProcessor();
                processor.TextureFormat = TextureProcessorOutputFormat.DxtCompressed;
                processor.ColorKeyEnabled = false;
                var texture = new Texture2DContent();
                var bitmap = new PixelBitmapContent<Color>(4, 4);
                for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) bitmap.SetPixel(x, y, new Color(x * 60, y * 60, 30, 255));
                texture.Mipmaps.Add(bitmap);
                return DescribeTexture(Process(processor, texture));
            });
            Record("textureprocessor/dxt_colorkeyed_opaque", () =>
            {
                // The colour key turns pixels transparent, so an otherwise opaque texture still
                // needs an alpha-carrying format: this asks whether the choice is made after it.
                var processor = new TextureProcessor();
                processor.TextureFormat = TextureProcessorOutputFormat.DxtCompressed;
                var texture = new Texture2DContent();
                var bitmap = new PixelBitmapContent<Color>(4, 4);
                for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) bitmap.SetPixel(x, y, new Color(x * 60, y * 60, 30, 255));
                bitmap.SetPixel(0, 0, new Color(255, 0, 255, 255));
                texture.Mipmaps.Add(bitmap);
                return DescribeTexture(Process(processor, texture));
            });
            Record("textureprocessor/dxt_non_multiple_of_four", () =>
            {
                var processor = new TextureProcessor();
                processor.TextureFormat = TextureProcessorOutputFormat.DxtCompressed;
                return DescribeTexture(Process(processor, ColorTexture(5, 3)));
            });
            Record("textureprocessor/resize_already_power_of_two", () =>
            {
                var processor = new TextureProcessor();
                processor.ResizeToPowerOfTwo = true;
                return DescribeTexture(Process(processor, ColorTexture(4, 4)));
            });
            Record("textureprocessor/cube_defaults", () =>
            {
                var processor = new TextureProcessor();
                var texture = new TextureCubeContent();
                for (int face = 0; face < 6; face++) texture.Faces[face].Add(Gradient(4, 4));
                return DescribeTexture(Process(processor, texture));
            });
            Record("textureprocessor/null_input", () =>
            {
                var processor = new TextureProcessor();
                return DescribeTexture(Process(processor, null));
            });
            Record("textureprocessor/no_change", () =>
            {
                var processor = new TextureProcessor();
                processor.TextureFormat = TextureProcessorOutputFormat.NoChange;
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(new PixelBitmapContent<Bgr565>(4, 4));
                return DescribeTexture(Process(processor, texture));
            });
            Record("textureprocessor/sprite_defaults", () => DescribeTexture(Process(new SpriteTextureProcessor(), ColorTexture(4, 4))));
            Record("textureprocessor/model_defaults", () => DescribeTexture(Process(new ModelTextureProcessor(), ColorTexture(4, 4))));

            // ---- MaterialProcessor -----------------------------------------------------------------
            // The build context records what the processor asks it to build, which is the only way
            // to see the processor and parameters a material passes on to its textures.
            Record("materialprocessor/basic_with_texture", () =>
            {
                var processor = new MaterialProcessor();
                var material = new BasicMaterialContent();
                material.Texture = new ExternalReference<TextureContent>("cat.tga");
                var context = new RecordingProcessorContext();
                MaterialContent result = processor.Process(material, context);
                return DescribeMaterial(result) + " built=" + context.Built + " same=" + object.ReferenceEquals(result, material);
            });
            Record("materialprocessor/no_texture", () =>
            {
                var processor = new MaterialProcessor();
                var context = new RecordingProcessorContext();
                MaterialContent result = processor.Process(new BasicMaterialContent(), context);
                return DescribeMaterial(result) + " built=" + context.Built;
            });
            Record("materialprocessor/two_textures", () =>
            {
                var processor = new MaterialProcessor();
                var material = new DualTextureMaterialContent();
                material.Texture = new ExternalReference<TextureContent>("one.tga");
                material.Texture2 = new ExternalReference<TextureContent>("two.tga");
                var context = new RecordingProcessorContext();
                MaterialContent result = processor.Process(material, context);
                return DescribeMaterial(result) + " built=" + context.Built;
            });
            Record("materialprocessor/effect_material", () =>
            {
                var processor = new MaterialProcessor();
                var material = new EffectMaterialContent();
                material.Effect = new ExternalReference<EffectContent>("shader.fx");
                var context = new RecordingProcessorContext();
                MaterialContent result = processor.Process(material, context);
                return DescribeMaterial(result) + " built=" + context.Built;
            });
            Record("materialprocessor/properties_forwarded", () =>
            {
                var processor = new MaterialProcessor();
                processor.ColorKeyColor = new Color(1, 2, 3, 4);
                processor.ColorKeyEnabled = false;
                processor.GenerateMipmaps = false;
                processor.PremultiplyTextureAlpha = false;
                processor.ResizeTexturesToPowerOfTwo = true;
                processor.TextureFormat = TextureProcessorOutputFormat.NoChange;
                var material = new BasicMaterialContent();
                material.Texture = new ExternalReference<TextureContent>("cat.tga");
                var context = new RecordingProcessorContext();
                processor.Process(material, context);
                return context.Built;
            });
            Record("materialprocessor/null_input", () =>
            {
                var processor = new MaterialProcessor();
                return DescribeMaterial(processor.Process(null, new RecordingProcessorContext()));
            });
            Record("materialprocessor/base_material", () =>
            {
                var processor = new MaterialProcessor();
                var context = new RecordingProcessorContext();
                MaterialContent result = processor.Process(new MaterialContent(), context);
                return DescribeMaterial(result) + " built=" + context.Built;
            });

            // ---- EffectProcessor -------------------------------------------------------------------
            Record("effectprocessor/compile_simple", () =>
            {
                var processor = new EffectProcessor();
                var effect = new EffectContent();
                effect.EffectCode = "float4 PS() : COLOR0 { return float4(1,0,0,1); }\n" +
                                    "technique T { pass P { PixelShader = compile ps_2_0 PS(); } }\n";
                effect.Identity = new ContentIdentity("shader.fx");
                CompiledEffectContent compiled = processor.Process(effect, new RecordingProcessorContext());
                byte[] code = compiled.GetEffectCode();
                return "bytes=" + code.Length + " head=" + Hex(new byte[] { code[0], code[1], code[2], code[3] });
            });
            Record("effectprocessor/compile_error", () =>
            {
                var processor = new EffectProcessor();
                var effect = new EffectContent();
                effect.EffectCode = "this is not an effect";
                effect.Identity = new ContentIdentity("bad.fx");
                processor.Process(effect, new RecordingProcessorContext());
                return "accepted";
            });
            Record("effectprocessor/empty_code", () =>
            {
                var processor = new EffectProcessor();
                var effect = new EffectContent();
                effect.Identity = new ContentIdentity("empty.fx");
                processor.Process(effect, new RecordingProcessorContext());
                return "accepted";
            });
            Record("effectprocessor/defines_used", () =>
            {
                var processor = new EffectProcessor();
                processor.Defines = "TINT=float4(0,1,0,1)";
                var effect = new EffectContent();
                effect.EffectCode = "float4 PS() : COLOR0 { return TINT; }\n" +
                                    "technique T { pass P { PixelShader = compile ps_2_0 PS(); } }\n";
                effect.Identity = new ContentIdentity("defined.fx");
                CompiledEffectContent compiled = processor.Process(effect, new RecordingProcessorContext());
                return "bytes=" + compiled.GetEffectCode().Length;
            });
            Record("effectprocessor/defines_missing", () =>
            {
                var processor = new EffectProcessor();
                var effect = new EffectContent();
                effect.EffectCode = "float4 PS() : COLOR0 { return TINT; }\n" +
                                    "technique T { pass P { PixelShader = compile ps_2_0 PS(); } }\n";
                effect.Identity = new ContentIdentity("undefined.fx");
                processor.Process(effect, new RecordingProcessorContext());
                return "accepted";
            });
            Record("effectprocessor/null_input", () =>
            {
                var processor = new EffectProcessor();
                processor.Process(null, new RecordingProcessorContext());
                return "accepted";
            });

            // ---- Font processors -------------------------------------------------------------------
            Record("fontprocessor/description_arial", () =>
            {
                var processor = new FontDescriptionProcessor();
                var description = new FontDescription("Arial", 12.0f, 0.0f);
                description.Characters.Add('A');
                description.Characters.Add('B');
                description.Identity = new ContentIdentity("font.spritefont");
                SpriteFontContent font = processor.Process(description, new RecordingProcessorContext());
                return "type=" + font.GetType().Name + " properties=" + Properties(font);
            });
            Record("fontprocessor/description_missing_font", () =>
            {
                var processor = new FontDescriptionProcessor();
                var description = new FontDescription("No Such Font At All", 12.0f, 0.0f);
                description.Characters.Add('A');
                description.Identity = new ContentIdentity("font.spritefont");
                processor.Process(description, new RecordingProcessorContext());
                return "accepted";
            });
            Record("fontprocessor/description_no_characters", () =>
            {
                var processor = new FontDescriptionProcessor();
                var description = new FontDescription("Arial", 12.0f, 0.0f);
                description.Identity = new ContentIdentity("font.spritefont");
                processor.Process(description, new RecordingProcessorContext());
                return "accepted";
            });
            Record("fontprocessor/description_null", () =>
            {
                var processor = new FontDescriptionProcessor();
                processor.Process(null, new RecordingProcessorContext());
                return "accepted";
            });
            Record("fontprocessor/texture_null", () =>
            {
                var processor = new FontTextureProcessor();
                processor.Process(null, new RecordingProcessorContext());
                return "accepted";
            });
            Record("fontprocessor/texture_character_for_index", () =>
            {
                var processor = new FontTextureProcessor();
                var method = typeof(FontTextureProcessor).GetMethod("GetCharacterForIndex", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Public);
                var results = new StringBuilder();
                foreach (int index in new int[] { 0, 1, 5 })
                {
                    if (results.Length > 0) results.Append(' ');
                    object value = method.Invoke(processor, new object[] { index });
                    results.Append(index + "=U+" + ((int)(char)value).ToString("X4"));
                }
                return results.ToString();
            });
            Record("fontprocessor/texture_first_character_set", () =>
            {
                var processor = new FontTextureProcessor();
                processor.FirstCharacter = 'a';
                var method = typeof(FontTextureProcessor).GetMethod("GetCharacterForIndex", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Public);
                return "0=U+" + ((int)(char)method.Invoke(processor, new object[] { 0 })).ToString("X4") +
                       " 3=U+" + ((int)(char)method.Invoke(processor, new object[] { 3 })).ToString("X4");
            });
            Record("fontprocessor/texture_strip", () =>
            {
                // Two glyphs separated by the delimiter colour XNA looks for, whatever that is:
                // the message tells us when it is wrong.
                var processor = new FontTextureProcessor();
                var texture = new Texture2DContent();
                var bitmap = new PixelBitmapContent<Color>(8, 4);
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 8; x++)
                        bitmap.SetPixel(x, y, new Color(255, 0, 255, 255));
                for (int y = 1; y < 3; y++)
                {
                    bitmap.SetPixel(1, y, Color.White);
                    bitmap.SetPixel(2, y, Color.White);
                    bitmap.SetPixel(5, y, Color.White);
                }
                texture.Mipmaps.Add(bitmap);
                SpriteFontContent font = processor.Process(texture, new RecordingProcessorContext());
                return "type=" + font.GetType().Name;
            });
            Record("fontprocessor/texture_empty", () =>
            {
                var processor = new FontTextureProcessor();
                var texture = new Texture2DContent();
                texture.Mipmaps.Add(new PixelBitmapContent<Color>(4, 4));
                processor.Process(texture, new RecordingProcessorContext());
                return "accepted";
            });
            Record("fontprocessor/spritefont_content_members", () =>
            {
                var builder = new StringBuilder();
                foreach (System.Reflection.MemberInfo member in typeof(SpriteFontContent).GetMembers(System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.DeclaredOnly))
                {
                    if (builder.Length > 0) builder.Append(' ');
                    builder.Append(member.MemberType + ":" + member.Name);
                }
                return builder.Length == 0 ? "none" : builder.ToString();
            });

            // ---- ModelProcessor and the model graph -----------------------------------------------
            Record("modelprocessor/triangle", () =>
            {
                var processor = new ModelProcessor();
                var context = new RecordingProcessorContext();
                ModelContent model = processor.Process(TriangleScene(), context);
                return DescribeModel(model) + " built=" + context.Built;
            });
            Record("modelprocessor/bone_hierarchy", () =>
            {
                var processor = new ModelProcessor();
                var root = new NodeContent();
                root.Name = "Root";
                root.Transform = Matrix.CreateTranslation(1, 0, 0);
                var bone = new BoneContent();
                bone.Name = "Bone";
                bone.Transform = Matrix.CreateTranslation(0, 2, 0);
                root.Children.Add(bone);
                MeshContent mesh = TriangleMesh();
                bone.Children.Add(mesh);
                ModelContent model = processor.Process(root, new RecordingProcessorContext());
                return DescribeModel(model);
            });
            Record("modelprocessor/scale_and_rotation", () =>
            {
                var processor = new ModelProcessor();
                processor.Scale = 2.0f;
                processor.RotationY = 90.0f;
                ModelContent model = processor.Process(TriangleScene(), new RecordingProcessorContext());
                return DescribeModel(model);
            });
            Record("modelprocessor/swap_winding", () =>
            {
                var processor = new ModelProcessor();
                processor.SwapWindingOrder = true;
                ModelContent model = processor.Process(TriangleScene(), new RecordingProcessorContext());
                return DescribeModel(model);
            });
            Record("modelprocessor/default_effect_skinned", () =>
            {
                var processor = new ModelProcessor();
                processor.DefaultEffect = MaterialProcessorDefaultEffect.SkinnedEffect;
                ModelContent model = processor.Process(TriangleScene(), new RecordingProcessorContext());
                return DescribeModel(model);
            });
            Record("modelprocessor/generate_tangent_frames", () =>
            {
                var processor = new ModelProcessor();
                processor.GenerateTangentFrames = true;
                ModelContent model = processor.Process(TriangleScene(), new RecordingProcessorContext());
                return DescribeModel(model);
            });
            Record("modelprocessor/null_input", () =>
            {
                var processor = new ModelProcessor();
                return DescribeModel(processor.Process(null, new RecordingProcessorContext()));
            });
            Record("modelprocessor/empty_node", () =>
            {
                var processor = new ModelProcessor();
                ModelContent model = processor.Process(new NodeContent(), new RecordingProcessorContext());
                return DescribeModel(model);
            });
            Record("modelprocessor/vertex_buffer_content", () =>
            {
                var buffer = new VertexBufferContent(24);
                var declaration = new VertexDeclarationContent();
                declaration.VertexElements.Add(new VertexElement(0, VertexElementFormat.Vector3, VertexElementUsage.Position, 0));
                buffer.VertexDeclaration = declaration;
                buffer.Write(0, 12, new Vector3[] { new Vector3(1, 2, 3), new Vector3(4, 5, 6) });
                return "bytes=" + buffer.VertexData.Length + " stride=" + (declaration.VertexStride.HasValue ? declaration.VertexStride.Value.ToString() : "null") +
                       " elements=" + declaration.VertexElements.Count + " data=" + Hex(buffer.VertexData) +
                       " sizeof=" + VertexBufferContent.SizeOf(typeof(Vector3));
            });
            Record("modelprocessor/vertex_buffer_write_untyped", () =>
            {
                // The overload that names the element type rather than deducing it, and what it
                // does when a value is of another type.
                var buffer = new VertexBufferContent(24);
                buffer.Write(0, 12, typeof(Vector3), new Vector3[] { new Vector3(1, 2, 3), new Vector3(4, 5, 6) });
                string wrong;
                try
                {
                    var other = new VertexBufferContent(24);
                    other.Write(0, 12, typeof(Vector3), new object[] { new Vector2(1, 2), new Vector2(3, 4) });
                    wrong = "accepted";
                }
                catch (Exception error) { wrong = error.GetType().Name; }
                string unsupported;
                try
                {
                    var other = new VertexBufferContent(24);
                    other.Write(0, 4, typeof(string), new string[] { "a" });
                    unsupported = "accepted";
                }
                catch (Exception error) { unsupported = error.GetType().Name; }
                return "data=" + Hex(buffer.VertexData) + " wrongType=" + wrong + " unsupported=" + unsupported;
            });
            Record("modelprocessor/vertex_buffer_sizeof_refusals", () =>
            {
                var builder = new StringBuilder();
                foreach (Type probe in new Type[] { typeof(Vector2), typeof(Vector4), typeof(Color), typeof(float), typeof(string), typeof(int), typeof(byte), typeof(short), typeof(double), typeof(bool), typeof(char), typeof(Matrix), typeof(Quaternion), typeof(DateTime), typeof(SurfaceFormat), typeof(Vector3[]), null })
                {
                    if (builder.Length > 0) builder.Append(' ');
                    string name = probe == null ? "null" : probe.Name;
                    try { builder.Append(name + "=" + VertexBufferContent.SizeOf(probe)); }
                    catch (Exception error) { builder.Append(name + "=" + error.GetType().Name); }
                }
                return builder.ToString();
            });
            Record("modelprocessor/vertex_buffer_defaults", () =>
            {
                var buffer = new VertexBufferContent();
                return "bytes=" + buffer.VertexData.Length + " declaration=" + (buffer.VertexDeclaration == null ? "null" : "set") +
                       " name=\"" + buffer.Name + "\"";
            });
            Record("modelprocessor/vertex_declaration_defaults", () =>
            {
                var declaration = new VertexDeclarationContent();
                return "elements=" + declaration.VertexElements.Count + " stride=" + (declaration.VertexStride.HasValue ? declaration.VertexStride.Value.ToString() : "null");
            });

            Record("modelprocessor/scale_rotation_detail", () =>
            {
                // Where the processor's Scale and Rotation land -- on the root bone's transform, on
                // the geometry, or on both -- is what the plain scale_and_rotation case cannot say,
                // because its scene is at the origin and only a translation is printed there.
                var processor = new ModelProcessor();
                processor.Scale = 2.0f;
                processor.RotationY = 90.0f;
                var root = new NodeContent();
                root.Name = "Root";
                root.Transform = Matrix.CreateTranslation(1, 0, 0);
                var bone = new BoneContent();
                bone.Name = "Bone";
                bone.Transform = Matrix.CreateTranslation(0, 3, 0);
                root.Children.Add(bone);
                MeshContent mesh = TriangleMesh();
                bone.Children.Add(mesh);
                ModelContent model = processor.Process(root, new RecordingProcessorContext());
                return DescribeModelFull(model) + " source=" + Positions(mesh.Positions);
            });
            Record("modelprocessor/identity_detail", () =>
            {
                // The control leg: the same description with no scale and no rotation.
                var processor = new ModelProcessor();
                MeshContent mesh = TriangleMesh();
                var root = new NodeContent();
                root.Name = "Root";
                root.Children.Add(mesh);
                ModelContent model = processor.Process(root, new RecordingProcessorContext());
                return DescribeModelFull(model) + " source=" + Positions(mesh.Positions);
            });
            Record("modelprocessor/tangent_frames_detail", () =>
            {
                var processor = new ModelProcessor();
                processor.GenerateTangentFrames = true;
                ModelContent model = processor.Process(TriangleScene(), new RecordingProcessorContext());
                return DescribeModelFull(model);
            });
            Record("modelprocessor/vertex_colors", () =>
            {
                // A colour channel decides two things at once: the element format the vertex buffer
                // is given, and whether PremultiplyVertexColors touched the values.
                var processor = new ModelProcessor();
                MeshContent mesh = TriangleMesh();
                mesh.Geometry[0].Vertices.Channels.Add<Color>(VertexChannelNames.Color(0),
                    new Color[] { new Color(255, 128, 64, 128), Color.White, new Color(0, 0, 0, 0) });
                var root = new NodeContent();
                root.Name = "Root";
                root.Children.Add(mesh);
                ModelContent model = processor.Process(root, new RecordingProcessorContext());
                return DescribeModelFull(model);
            });
            Record("modelprocessor/vertex_colors_unpremultiplied", () =>
            {
                var processor = new ModelProcessor();
                processor.PremultiplyVertexColors = false;
                MeshContent mesh = TriangleMesh();
                mesh.Geometry[0].Vertices.Channels.Add<Color>(VertexChannelNames.Color(0),
                    new Color[] { new Color(255, 128, 64, 128), Color.White, new Color(0, 0, 0, 0) });
                var root = new NodeContent();
                root.Name = "Root";
                root.Children.Add(mesh);
                ModelContent model = processor.Process(root, new RecordingProcessorContext());
                return DescribeModelFull(model);
            });

            Record("modelprocessor/rotation_order", () =>
            {
                // Three rotations at once: the translation that comes out names the order they are
                // composed in, which one rotation alone cannot.
                var processor = new ModelProcessor();
                processor.RotationX = 30.0f;
                processor.RotationY = 45.0f;
                processor.RotationZ = 60.0f;
                var root = new NodeContent();
                root.Name = "Root";
                root.Transform = Matrix.CreateTranslation(1, 2, 3);
                root.Children.Add(TriangleMesh());
                ModelContent model = processor.Process(root, new RecordingProcessorContext());
                return DescribeModelFull(model);
            });
            Record("modelprocessor/vertex_colors_rounding", () =>
            {
                // Colours chosen so that truncation and rounding disagree on every channel.
                var processor = new ModelProcessor();
                MeshContent mesh = TriangleMesh();
                mesh.Geometry[0].Vertices.Channels.Add<Color>(VertexChannelNames.Color(0),
                    new Color[] { new Color(1, 3, 5, 128), new Color(255, 254, 253, 1), new Color(127, 129, 191, 3) });
                var root = new NodeContent();
                root.Name = "Root";
                root.Children.Add(mesh);
                ModelContent model = processor.Process(root, new RecordingProcessorContext());
                return DescribeModelFull(model);
            });
            Record("modelprocessor/tangent_frames_no_texcoords", () =>
            {
                // Tangent frames without the texture coordinates they are derived from.
                var processor = new ModelProcessor();
                processor.GenerateTangentFrames = true;
                var mesh = new MeshContent();
                mesh.Name = "Mesh";
                mesh.Positions.Add(new Vector3(0, 0, 0));
                mesh.Positions.Add(new Vector3(1, 0, 0));
                mesh.Positions.Add(new Vector3(0, 1, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1, 2 });
                geometry.Indices.AddRange(new int[] { 0, 1, 2 });
                geometry.Vertices.Channels.Add<Vector3>(VertexChannelNames.Normal(), new Vector3[] { Vector3.UnitZ, Vector3.UnitZ, Vector3.UnitZ });
                var root = new NodeContent();
                root.Name = "Root";
                root.Children.Add(mesh);
                ModelContent model = processor.Process(root, new RecordingProcessorContext());
                return DescribeModelFull(model);
            });

            // ---- MeshBuilder and MeshHelper ------------------------------------------------------
            Record("meshbuilder/defaults", () =>
            {
                MeshBuilder builder = MeshBuilder.StartMesh("Mesh");
                return "MergeDuplicatePositions=" + builder.MergeDuplicatePositions +
                       " MergePositionTolerance=" + builder.MergePositionTolerance.ToString("R", CultureInfo.InvariantCulture) +
                       " Name=" + (builder.Name == null ? "null" : "\"" + builder.Name + "\"") +
                       " SwapWindingOrder=" + builder.SwapWindingOrder;
            });
            Record("meshbuilder/quad", () => DescribeMeshFull(BuiltQuad(false, false)));
            Record("meshbuilder/quad_merged", () => DescribeMeshFull(BuiltQuad(true, false)));
            Record("meshbuilder/quad_swapped", () => DescribeMeshFull(BuiltQuad(false, true)));
            Record("meshbuilder/duplicate_positions", () =>
            {
                // The same position twice, once exactly and once within the tolerance.
                MeshBuilder builder = MeshBuilder.StartMesh("Mesh");
                builder.MergeDuplicatePositions = true;
                builder.MergePositionTolerance = 0.01f;
                int a = builder.CreatePosition(new Vector3(0, 0, 0));
                int b = builder.CreatePosition(new Vector3(0, 0, 0));
                int c = builder.CreatePosition(new Vector3(0.005f, 0, 0));
                int d = builder.CreatePosition(new Vector3(1, 0, 0));
                builder.AddTriangleVertex(a);
                builder.AddTriangleVertex(d);
                builder.AddTriangleVertex(c);
                return "a=" + a + " b=" + b + " c=" + c + " d=" + d + " " + DescribeMeshFull(builder.FinishMesh());
            });
            Record("meshbuilder/material_and_opaque_data", () =>
            {
                MeshBuilder builder = MeshBuilder.StartMesh("Mesh");
                var material = new BasicMaterialContent();
                material.Alpha = 0.5f;
                builder.SetMaterial(material);
                var data = new OpaqueDataDictionary();
                data.Add("Key", 7);
                builder.SetOpaqueData(data);
                builder.CreatePosition(0, 0, 0);
                builder.CreatePosition(1, 0, 0);
                builder.CreatePosition(0, 1, 0);
                builder.AddTriangleVertex(0);
                builder.AddTriangleVertex(1);
                builder.AddTriangleVertex(2);
                return DescribeMeshFull(builder.FinishMesh());
            });
            Record("meshbuilder/refusals", () =>
            {
                var builder = new StringBuilder();
                Action<string, Action> probe = delegate(string name, Action body)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try { body(); builder.Append(name + "=accepted"); }
                    catch (Exception error) { builder.Append(name + "=" + error.GetType().Name + ":" + error.Message); }
                };
                probe("nullName", delegate { MeshBuilder.StartMesh(null); });
                probe("channelAfterVertex", delegate
                {
                    MeshBuilder one = MeshBuilder.StartMesh("Mesh");
                    one.CreatePosition(0, 0, 0);
                    one.AddTriangleVertex(0);
                    one.CreateVertexChannel<Vector3>(VertexChannelNames.Normal());
                });
                probe("badVertexIndex", delegate
                {
                    MeshBuilder one = MeshBuilder.StartMesh("Mesh");
                    one.CreatePosition(0, 0, 0);
                    one.AddTriangleVertex(4);
                });
                probe("wrongChannelType", delegate
                {
                    MeshBuilder one = MeshBuilder.StartMesh("Mesh");
                    int channel = one.CreateVertexChannel<Vector3>(VertexChannelNames.Normal());
                    one.CreatePosition(0, 0, 0);
                    one.SetVertexChannelData(channel, new Vector2(1, 2));
                    one.AddTriangleVertex(0);
                    one.FinishMesh();
                });
                probe("badChannelIndex", delegate
                {
                    MeshBuilder one = MeshBuilder.StartMesh("Mesh");
                    one.SetVertexChannelData(3, Vector3.UnitZ);
                });
                probe("unfinishedTriangle", delegate
                {
                    MeshBuilder one = MeshBuilder.StartMesh("Mesh");
                    one.CreatePosition(0, 0, 0);
                    one.AddTriangleVertex(0);
                    one.FinishMesh();
                });
                probe("nullMaterial", delegate { MeshBuilder.StartMesh("Mesh").SetMaterial(null); });
                probe("nullOpaqueData", delegate { MeshBuilder.StartMesh("Mesh").SetOpaqueData(null); });
                return builder.ToString();
            });
            Record("meshhelper/calculate_normals", () =>
            {
                MeshContent mesh = BuiltQuad(true, false);
                mesh.Geometry[0].Vertices.Channels.Remove(VertexChannelNames.Normal());
                MeshHelper.CalculateNormals(mesh, false);
                return DescribeMeshFull(mesh);
            });
            Record("meshhelper/calculate_normals_overwrite", () =>
            {
                MeshContent mesh = BuiltQuad(true, false);
                for (int i = 0; i < mesh.Geometry[0].Vertices.VertexCount; i++)
                    mesh.Geometry[0].Vertices.Channels.Get<Vector3>(VertexChannelNames.Normal())[i] = new Vector3(1, 0, 0);
                MeshHelper.CalculateNormals(mesh, false);
                string kept = DescribeMeshFull(mesh);
                MeshHelper.CalculateNormals(mesh, true);
                return "kept=" + kept + " overwritten=" + DescribeMeshFull(mesh);
            });
            Record("meshhelper/calculate_tangent_frames", () =>
            {
                MeshContent mesh = BuiltQuad(true, false);
                MeshHelper.CalculateTangentFrames(mesh, VertexChannelNames.TextureCoordinate(0),
                                                  VertexChannelNames.Tangent(0), VertexChannelNames.Binormal(0));
                return DescribeMeshFull(mesh);
            });
            Record("meshhelper/calculate_tangent_frames_refusals", () =>
            {
                var builder = new StringBuilder();
                Action<string, Action> probe = delegate(string name, Action body)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try { body(); builder.Append(name + "=accepted"); }
                    catch (Exception error) { builder.Append(name + "=" + error.GetType().Name + ":" + error.Message); }
                };
                probe("noTexCoords", delegate
                {
                    MeshContent mesh = BuiltQuad(true, false);
                    mesh.Geometry[0].Vertices.Channels.Remove(VertexChannelNames.TextureCoordinate(0));
                    MeshHelper.CalculateTangentFrames(mesh, VertexChannelNames.TextureCoordinate(0),
                                                      VertexChannelNames.Tangent(0), VertexChannelNames.Binormal(0));
                });
                probe("nullTangentAndBinormal", delegate
                {
                    MeshContent mesh = BuiltQuad(true, false);
                    MeshHelper.CalculateTangentFrames(mesh, VertexChannelNames.TextureCoordinate(0), null, null);
                });
                probe("nullMesh", delegate
                {
                    MeshHelper.CalculateTangentFrames(null, VertexChannelNames.TextureCoordinate(0),
                                                      VertexChannelNames.Tangent(0), VertexChannelNames.Binormal(0));
                });
                return builder.ToString();
            });
            Record("meshhelper/skeleton", () =>
            {
                var root = new NodeContent();
                root.Name = "Root";
                var skeleton = new BoneContent();
                skeleton.Name = "Skeleton";
                var childA = new BoneContent();
                childA.Name = "A";
                var childB = new BoneContent();
                childB.Name = "B";
                var grandChild = new BoneContent();
                grandChild.Name = "A1";
                root.Children.Add(skeleton);
                skeleton.Children.Add(childA);
                skeleton.Children.Add(childB);
                childA.Children.Add(grandChild);
                BoneContent found = MeshHelper.FindSkeleton(root);
                var order = new StringBuilder();
                foreach (BoneContent bone in MeshHelper.FlattenSkeleton(skeleton))
                    order.Append((order.Length == 0 ? "" : ",") + bone.Name);
                string fromBone = MeshHelper.FindSkeleton(grandChild) == null ? "null" : MeshHelper.FindSkeleton(grandChild).Name;
                return "found=" + (found == null ? "null" : found.Name) + " flattened=[" + order + "]" +
                       " fromGrandChild=" + fromBone +
                       " fromEmpty=" + (MeshHelper.FindSkeleton(new NodeContent()) == null ? "null" : "found");
            });
            Record("meshhelper/merge_duplicate_positions", () =>
            {
                MeshContent mesh = BuiltQuad(false, false);
                MeshHelper.MergeDuplicatePositions(mesh, 0.0f);
                string exact = DescribeMeshFull(mesh);
                MeshContent loose = BuiltQuad(false, false);
                loose.Positions[1] = new Vector3(1.001f, 0, 0);
                MeshHelper.MergeDuplicatePositions(loose, 0.01f);
                return "exact=" + exact + " loose=" + DescribeMeshFull(loose);
            });
            Record("meshhelper/merge_duplicate_vertices", () =>
            {
                MeshContent mesh = BuiltQuad(true, false);
                MeshHelper.MergeDuplicateVertices(mesh.Geometry[0]);
                string one = DescribeMeshFull(mesh);
                MeshContent whole = BuiltQuad(true, false);
                MeshHelper.MergeDuplicateVertices(whole);
                return "geometry=" + one + " mesh=" + DescribeMeshFull(whole);
            });
            Record("meshhelper/optimize_for_cache", () =>
            {
                MeshContent mesh = BuiltQuad(true, false);
                MeshHelper.OptimizeForCache(mesh);
                return DescribeMeshFull(mesh);
            });
            Record("meshhelper/swap_winding_order", () =>
            {
                MeshContent mesh = BuiltQuad(true, false);
                MeshHelper.SwapWindingOrder(mesh);
                return DescribeMeshFull(mesh);
            });
            Record("meshhelper/transform_scene", () =>
            {
                var root = new NodeContent();
                root.Name = "Root";
                root.Transform = Matrix.CreateTranslation(1, 0, 0);
                var bone = new BoneContent();
                bone.Name = "Bone";
                bone.Transform = Matrix.CreateTranslation(0, 3, 0);
                root.Children.Add(bone);
                MeshContent mesh = BuiltQuad(true, false);
                bone.Children.Add(mesh);
                MeshHelper.TransformScene(root, Matrix.CreateRotationY(MathHelper.ToRadians(90)) * Matrix.CreateScale(2));
                return "root=" + DescribeMatrixFull(root.Transform) + " bone=" + DescribeMatrixFull(bone.Transform) +
                       " " + DescribeMeshFull(mesh);
            });

            Record("modelprocessor/swap_winding_detail", () =>
            {
                // The plain swap_winding case counts the indices; this one prints their order.
                var processor = new ModelProcessor();
                processor.SwapWindingOrder = true;
                var root = new NodeContent();
                root.Name = "Root";
                root.Children.Add(TriangleMesh());
                return DescribeModelFull(processor.Process(root, new RecordingProcessorContext()));
            });
            Record("meshhelper/calculate_normals_tent", () =>
            {
                MeshContent mesh = Tent();
                MeshHelper.CalculateNormals(mesh, true);
                return DescribeMeshFull(mesh);
            });
            Record("meshhelper/merge_duplicate_positions_real", () =>
            {
                var mesh = new MeshContent();
                mesh.Name = "Mesh";
                mesh.Positions.Add(new Vector3(0, 0, 0));
                mesh.Positions.Add(new Vector3(1, 0, 0));
                mesh.Positions.Add(new Vector3(0.0005f, 0, 0));
                mesh.Positions.Add(new Vector3(0, 1, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1, 2, 3 });
                geometry.Indices.AddRange(new int[] { 0, 1, 2, 1, 2, 3 });
                geometry.Vertices.Channels.Add<Vector2>(VertexChannelNames.TextureCoordinate(0),
                    new Vector2[] { new Vector2(0, 0), new Vector2(1, 0), new Vector2(0, 0), new Vector2(0, 1) });
                MeshContent tight = Tent();
                MeshHelper.MergeDuplicatePositions(mesh, 0.001f);
                string merged = DescribeMeshFull(mesh);
                MeshHelper.MergeDuplicatePositions(tight, 0.0f);
                return "merged=" + merged + " tent=" + DescribeMeshFull(tight);
            });
            Record("meshhelper/merge_duplicate_vertices_real", () =>
            {
                // Two vertices at the same position with the same channel data, and two with
                // different data: what tells which of the two a merge is keyed on.
                var mesh = new MeshContent();
                mesh.Name = "Mesh";
                mesh.Positions.Add(new Vector3(0, 0, 0));
                mesh.Positions.Add(new Vector3(1, 0, 0));
                mesh.Positions.Add(new Vector3(0, 1, 0));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1, 2, 0, 1, 2 });
                geometry.Indices.AddRange(new int[] { 0, 1, 2, 3, 4, 5 });
                geometry.Vertices.Channels.Add<Vector2>(VertexChannelNames.TextureCoordinate(0),
                    new Vector2[] { new Vector2(0, 0), new Vector2(1, 0), new Vector2(0, 1),
                                    new Vector2(0, 0), new Vector2(1, 0), new Vector2(9, 9) });
                MeshHelper.MergeDuplicateVertices(geometry);
                return DescribeMeshFull(mesh);
            });
            Record("meshhelper/optimize_for_cache_grid", () =>
            {
                MeshContent mesh = Grid(3);
                MeshHelper.OptimizeForCache(mesh);
                return DescribeMeshFull(mesh);
            });

            Record("modelprocessor/quad_ordering", () =>
            {
                // Two triangles through the processor: whether their order and their vertices come
                // out as they went in is what says whether the cache optimization runs here.
                var processor = new ModelProcessor();
                var root = new NodeContent();
                root.Name = "Root";
                root.Children.Add(BuiltQuad(true, false));
                return DescribeModelFull(processor.Process(root, new RecordingProcessorContext()));
            });
            Record("meshhelper/calculate_normals_shared_positions", () =>
            {
                // Two vertices at one position with different texture coordinates -- a seam. Do
                // they get one averaged normal, or one each?
                var mesh = new MeshContent();
                mesh.Name = "Seam";
                mesh.Positions.Add(new Vector3(0, 0, 0));
                mesh.Positions.Add(new Vector3(1, 0, 0));
                mesh.Positions.Add(new Vector3(0, 1, 0));
                mesh.Positions.Add(new Vector3(0, 0, 1));
                var geometry = new GeometryContent();
                mesh.Geometry.Add(geometry);
                geometry.Vertices.AddRange(new int[] { 0, 1, 2, 0, 3, 1 });
                geometry.Indices.AddRange(new int[] { 0, 1, 2, 3, 4, 5 });
                geometry.Vertices.Channels.Add<Vector2>(VertexChannelNames.TextureCoordinate(0),
                    new Vector2[] { new Vector2(0, 0), new Vector2(1, 0), new Vector2(0, 1),
                                    new Vector2(7, 7), new Vector2(8, 8), new Vector2(9, 9) });
                MeshHelper.CalculateNormals(mesh, true);
                return DescribeMeshFull(mesh);
            });
            Record("meshhelper/optimize_for_cache_shuffled", () =>
            {
                // The same grid with its triangles in a scrambled order: a plain reversal and a
                // real cache optimizer answer differently here.
                MeshContent mesh = Grid(3);
                GeometryContent geometry = mesh.Geometry[0];
                var order = new int[] { 5, 0, 11, 3, 8, 14, 1, 17, 6, 12, 2, 9, 16, 4, 10, 7, 15, 13 };
                var indices = new List<int>();
                foreach (int triangle in order)
                    for (int i = 0; i < 3; i++)
                        indices.Add(geometry.Indices[triangle * 3 + i]);
                geometry.Indices.Clear();
                geometry.Indices.AddRange(indices.ToArray());
                MeshHelper.OptimizeForCache(mesh);
                return DescribeMeshFull(mesh);
            });

            Record("meshhelper/null_and_range_refusals", () =>
            {
                var builder = new StringBuilder();
                Action<string, Action> probe = delegate(string name, Action body)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try { body(); builder.Append(name + "=accepted"); }
                    catch (Exception error) { builder.Append(name + "=" + error.GetType().Name + ":" + error.Message); }
                };
                probe("normalsNull", delegate { MeshHelper.CalculateNormals(null, true); });
                probe("mergePositionsNull", delegate { MeshHelper.MergeDuplicatePositions(null, 0.0f); });
                probe("mergePositionsNegative", delegate { MeshHelper.MergeDuplicatePositions(Tent(), -1.0f); });
                probe("mergeVerticesNullGeometry", delegate { MeshHelper.MergeDuplicateVertices((GeometryContent)null); });
                probe("mergeVerticesNullMesh", delegate { MeshHelper.MergeDuplicateVertices((MeshContent)null); });
                probe("optimizeNull", delegate { MeshHelper.OptimizeForCache(null); });
                probe("swapNull", delegate { MeshHelper.SwapWindingOrder(null); });
                probe("transformNull", delegate { MeshHelper.TransformScene(null, Matrix.Identity); });
                probe("findSkeletonNull", delegate { MeshHelper.FindSkeleton(null); });
                probe("flattenNull", delegate { MeshHelper.FlattenSkeleton(null); });
                return builder.ToString();
            });
            Record("meshbuilder/channel_refusals", () =>
            {
                var builder = new StringBuilder();
                Action<string, Action> probe = delegate(string name, Action body)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try { body(); builder.Append(name + "=accepted"); }
                    catch (Exception error) { builder.Append(name + "=" + error.GetType().Name + ":" + error.Message); }
                };
                probe("nullChannelName", delegate { MeshBuilder.StartMesh("Mesh").CreateVertexChannel<Vector3>(null); });
                probe("duplicateChannel", delegate
                {
                    MeshBuilder one = MeshBuilder.StartMesh("Mesh");
                    one.CreateVertexChannel<Vector3>(VertexChannelNames.Normal());
                    one.CreateVertexChannel<Vector3>(VertexChannelNames.Normal());
                });
                probe("intChannel", delegate { MeshBuilder.StartMesh("Mesh").CreateVertexChannel<int>("Custom0"); });
                probe("stringChannel", delegate { MeshBuilder.StartMesh("Mesh").CreateVertexChannel<string>("Custom0"); });
                probe("dataBeforePosition", delegate
                {
                    MeshBuilder one = MeshBuilder.StartMesh("Mesh");
                    int channel = one.CreateVertexChannel<Vector3>(VertexChannelNames.Normal());
                    one.SetVertexChannelData(channel, Vector3.UnitZ);
                });
                probe("finishTwice", delegate
                {
                    MeshBuilder one = MeshBuilder.StartMesh("Mesh");
                    one.CreatePosition(0, 0, 0);
                    one.CreatePosition(1, 0, 0);
                    one.CreatePosition(0, 1, 0);
                    one.AddTriangleVertex(0);
                    one.AddTriangleVertex(1);
                    one.AddTriangleVertex(2);
                    one.FinishMesh();
                    one.FinishMesh();
                });
                probe("nameAfterStart", delegate
                {
                    MeshBuilder one = MeshBuilder.StartMesh("Given");
                    one.Name = "Renamed";
                    one.CreatePosition(0, 0, 0);
                    one.CreatePosition(1, 0, 0);
                    one.CreatePosition(0, 1, 0);
                    one.AddTriangleVertex(0);
                    one.AddTriangleVertex(1);
                    one.AddTriangleVertex(2);
                    if (one.FinishMesh().Name != "Renamed") throw new Exception("name=" + one.FinishMesh().Name);
                });
                return builder.ToString();
            });

            Record("meshbuilder/channel_data_persistence", () =>
            {
                // Is the channel value set once carried into the vertices that follow, or does it
                // apply to one vertex only?
                MeshBuilder builder = MeshBuilder.StartMesh("Mesh");
                int normals = builder.CreateVertexChannel<Vector3>(VertexChannelNames.Normal());
                int coords = builder.CreateVertexChannel<Vector2>(VertexChannelNames.TextureCoordinate(0));
                builder.CreatePosition(0, 0, 0);
                builder.CreatePosition(1, 0, 0);
                builder.CreatePosition(0, 1, 0);
                builder.SetVertexChannelData(normals, new Vector3(0, 0, 1));
                builder.SetVertexChannelData(coords, new Vector2(5, 6));
                builder.AddTriangleVertex(0);
                builder.AddTriangleVertex(1);
                builder.SetVertexChannelData(coords, new Vector2(7, 8));
                builder.AddTriangleVertex(2);
                return DescribeMeshFull(builder.FinishMesh());
            });
            Record("meshbuilder/finish_twice", () =>
            {
                MeshBuilder builder = MeshBuilder.StartMesh("Mesh");
                builder.CreatePosition(0, 0, 0);
                builder.CreatePosition(1, 0, 0);
                builder.CreatePosition(0, 1, 0);
                builder.AddTriangleVertex(0);
                builder.AddTriangleVertex(1);
                builder.AddTriangleVertex(2);
                MeshContent first = builder.FinishMesh();
                MeshContent second = builder.FinishMesh();
                return "same=" + object.ReferenceEquals(first, second) + " first=" + DescribeMeshFull(first) +
                       " second=" + DescribeMeshFull(second);
            });
            Record("meshbuilder/no_triangles", () =>
            {
                MeshBuilder builder = MeshBuilder.StartMesh("Empty");
                builder.CreatePosition(0, 0, 0);
                return DescribeMeshFull(builder.FinishMesh());
            });

            Record("effectprocessor/debugmode", () =>
            {
                // DebugMode.Auto is documented as "optimise unless the build configuration is
                // Debug"; whether the answer actually differs is what this measures, against the
                // two explicit modes as controls.
                string source = "float4 Tint;\nfloat4 PS() : COLOR0 { float4 c = Tint * 2; return c + Tint; }\n" +
                                "technique T { pass P { PixelShader = compile ps_2_0 PS(); } }\n";
                var builder = new StringBuilder();
                Action<string, EffectProcessorDebugMode, string> probe =
                    delegate(string label, EffectProcessorDebugMode mode, string configuration)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try
                    {
                        var processor = new EffectProcessor();
                        processor.DebugMode = mode;
                        var effect = new EffectContent();
                        effect.EffectCode = source;
                        effect.Identity = new ContentIdentity("shader.fx");
                        CompiledEffectContent compiled =
                            processor.Process(effect, new RecordingProcessorContext(configuration));
                        byte[] code = compiled.GetEffectCode();
                        builder.Append(label + "=" + code.Length);
                    }
                    catch (Exception error) { builder.Append(label + "=" + error.GetType().Name); }
                };
                probe("auto_debug", EffectProcessorDebugMode.Auto, "Debug");
                probe("auto_release", EffectProcessorDebugMode.Auto, "Release");
                probe("debug_release", EffectProcessorDebugMode.Debug, "Release");
                probe("optimize_debug", EffectProcessorDebugMode.Optimize, "Debug");
                return builder.ToString();
            });

            // ---- TextureImporter: what each source format becomes --------------------------------
            Record("textureimporter/formats", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                var builder = new StringBuilder();
                Action<string, string> probe = delegate(string label, string path)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try
                    {
                        var importer = new TextureImporter();
                        var context = new ProbeImporterContext();
                        TextureContent texture = importer.Import(path, context);
                        builder.Append(label + "=[" + DescribeTexture(texture) + " dependencies=" +
                                       context.Dependencies.Count + " identity=" +
                                       (texture.Identity == null ? "null" : (texture.Identity.SourceTool ?? "null")) + "]");
                    }
                    catch (Exception error) { builder.Append(label + "=" + error.GetType().Name + ": " + error.Message); }
                };
                // A 2x2 image whose four pixels are red, green, blue and half-transparent white,
                // written in each format the importer names.
                probe("png", WriteImage(directory, "probe.png", System.Drawing.Imaging.ImageFormat.Png));
                probe("bmp", WriteImage(directory, "probe.bmp", System.Drawing.Imaging.ImageFormat.Bmp));
                probe("jpg", WriteImage(directory, "probe.jpg", System.Drawing.Imaging.ImageFormat.Jpeg));
                probe("tga", WriteTga(directory, "probe.tga"));
                probe("ppm", WritePpm(directory, "probe.ppm"));
                probe("pfm", WritePfm(directory, "probe.pfm"));
                probe("dib", WriteDib(directory, "probe.dib"));
                probe("dds", WriteDds(directory, "probe.dds"));
                probe("wrong_extension", WriteImage(directory, "probe.xyz", System.Drawing.Imaging.ImageFormat.Png));
                return builder.ToString();
            });
            Record("textureimporter/dds_variants", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                var builder = new StringBuilder();
                Action<string, string> probe = delegate(string label, string path)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try
                    {
                        TextureContent texture = new TextureImporter().Import(path, new ProbeImporterContext());
                        builder.Append(label + "=[" + DescribeTexture(texture) + "]");
                    }
                    catch (Exception error) { builder.Append(label + "=" + error.GetType().Name + ": " + error.Message); }
                };
                probe("uncompressed", WriteDds(directory, "u.dds"));
                probe("dxt1", WriteDdsCompressed(directory, "dxt1.dds", "DXT1", 4, 4, 1, false, false));
                probe("dxt3", WriteDdsCompressed(directory, "dxt3.dds", "DXT3", 4, 4, 1, false, false));
                probe("dxt5", WriteDdsCompressed(directory, "dxt5.dds", "DXT5", 4, 4, 1, false, false));
                probe("dxt1_mips", WriteDdsCompressed(directory, "dxt1_mips.dds", "DXT1", 8, 8, 4, false, false));
                probe("dxt1_cube", WriteDdsCompressed(directory, "dxt1_cube.dds", "DXT1", 4, 4, 1, true, false));
                probe("dxt1_volume", WriteDdsCompressed(directory, "dxt1_volume.dds", "DXT1", 4, 4, 1, false, true));
                probe("dx10", WriteDdsCompressed(directory, "dx10.dds", "DX10", 4, 4, 1, false, false));
                return builder.ToString();
            });
            // ---- TextureImporter: one committed fixture per declared extension -------------------
            // XNAPP-167. The nine extensions the attribute names, each read from the file CNA
            // committed, so a difference in the answer is a difference in the importer and never a
            // difference in what the two sides encoded.
            foreach (string fixture in new string[] {
                "probe.bmp", "probe.dds", "probe.dib", "probe.hdr", "probe.jpg",
                "probe.pfm", "probe.png", "probe.ppm", "probe.tga", "probe.xyz", "probe_3x2.png",
                "probe_flat.hdr",
                "empty.png", "truncated.png", "garbage.tga", "truncated.dds" })
            {
                string captured = fixture;
                Record("textureext/" + captured, () =>
                {
                    var context = new ProbeImporterContext();
                    TextureContent texture = new TextureImporter().Import(
                        Path.Combine(fixtureDirectory, captured), context);
                    return DescribeTexture(texture) + " dependencies=" + context.Dependencies.Count +
                           " identity=" + (texture.Identity == null ? "null" : (texture.Identity.SourceTool ?? "null"));
                });
            }
            // The same fixtures through the processor XNA names as the default for this importer,
            // and through the three the pipeline also offers, so the source-to-processor leg is
            // measured and not assumed.
            foreach (string fixture in new string[] { "probe.png", "probe.ppm", "probe.pfm", "probe.dds", "probe.jpg" })
            {
                string captured = fixture;
                Record("textureext/" + captured + "/spritetexture", () =>
                {
                    TextureContent texture = new TextureImporter().Import(
                        Path.Combine(fixtureDirectory, captured), new ProbeImporterContext());
                    TextureContent output = new SpriteTextureProcessor().Process(texture, new ProbeProcessorContext());
                    return DescribeTexture(output);
                });
                Record("textureext/" + captured + "/texture", () =>
                {
                    TextureContent texture = new TextureImporter().Import(
                        Path.Combine(fixtureDirectory, captured), new ProbeImporterContext());
                    TextureContent output = new TextureProcessor().Process(texture, new ProbeProcessorContext());
                    return DescribeTexture(output);
                });
                Record("textureext/" + captured + "/modeltexture", () =>
                {
                    TextureContent texture = new TextureImporter().Import(
                        Path.Combine(fixtureDirectory, captured), new ProbeImporterContext());
                    TextureContent output = new ModelTextureProcessor().Process(texture, new ProbeProcessorContext());
                    return DescribeTexture(output);
                });
            }

            // The two float sources' actual values, which DescribeTexture does not print.
            foreach (string fixture in new string[] { "probe.hdr", "probe_flat.hdr", "probe.pfm" })
            {
                string captured = fixture;
                Record("textureext/" + captured + "/floats", () =>
                {
                    TextureContent texture = new TextureImporter().Import(
                        Path.Combine(fixtureDirectory, captured), new ProbeImporterContext());
                    var bitmap = (PixelBitmapContent<Vector4>)texture.Faces[0][0];
                    var builder = new StringBuilder();
                    for (int y = 0; y < bitmap.Height; y++)
                        for (int x = 0; x < bitmap.Width; x++)
                        {
                            Vector4 pixel = bitmap.GetPixel(x, y);
                            if (builder.Length > 0) builder.Append(' ');
                            builder.Append("(" + pixel.X.ToString("R", CultureInfo.InvariantCulture) + "," +
                                           pixel.Y.ToString("R", CultureInfo.InvariantCulture) + "," +
                                           pixel.Z.ToString("R", CultureInfo.InvariantCulture) + "," +
                                           pixel.W.ToString("R", CultureInfo.InvariantCulture) + ")");
                        }
                    return builder.ToString();
                });
            }

            // ---- TextureProcessor: every property, over the committed corpus ----------------------
            // XNAPP-167 asks for colour key, premultiply, resize to a power of two, mipmap
            // generation, TextureFormat and the profile restrictions to be measured rather than
            // assumed. Each case names the one property it moves off its default.
            Action<string, string, Action<TextureProcessor>> textureCase =
                delegate(string label, string fixture, Action<TextureProcessor> configure)
            {
                Record("textureprop/" + label, () =>
                {
                    TextureContent texture = new TextureImporter().Import(
                        Path.Combine(fixtureDirectory, fixture), new ProbeImporterContext());
                    var processor = new TextureProcessor();
                    configure(processor);
                    return DescribeTexture(processor.Process(texture, new ProbeProcessorContext()));
                });
            };
            textureCase("defaults", "probe.png", delegate(TextureProcessor p) { });
            textureCase("no_premultiply", "probe.png", delegate(TextureProcessor p) { p.PremultiplyAlpha = false; });
            textureCase("colorkey_red_enabled", "probe.png", delegate(TextureProcessor p) {
                p.ColorKeyEnabled = true; p.ColorKeyColor = new Color(255, 0, 0, 255); });
            textureCase("colorkey_red_disabled", "probe.png", delegate(TextureProcessor p) {
                p.ColorKeyEnabled = false; p.ColorKeyColor = new Color(255, 0, 0, 255); });
            textureCase("colorkey_magenta_default", "probe.png", delegate(TextureProcessor p) {
                p.ColorKeyEnabled = true; });
            textureCase("generate_mipmaps", "probe.png", delegate(TextureProcessor p) { p.GenerateMipmaps = true; });
            textureCase("resize_to_power_of_two", "probe_3x2.png", delegate(TextureProcessor p) { p.ResizeToPowerOfTwo = true; });
            textureCase("no_resize_3x2", "probe_3x2.png", delegate(TextureProcessor p) { });
            textureCase("resize_and_mipmaps", "probe_3x2.png", delegate(TextureProcessor p) {
                p.ResizeToPowerOfTwo = true; p.GenerateMipmaps = true; });
            textureCase("mipmaps_without_resize_3x2", "probe_3x2.png", delegate(TextureProcessor p) { p.GenerateMipmaps = true; });
            textureCase("dxt_from_3x2", "probe_3x2.png", delegate(TextureProcessor p) { p.TextureFormat = TextureProcessorOutputFormat.DxtCompressed; });
            textureCase("format_color", "probe.png", delegate(TextureProcessor p) { p.TextureFormat = TextureProcessorOutputFormat.Color; });
            textureCase("format_dxt", "probe.png", delegate(TextureProcessor p) { p.TextureFormat = TextureProcessorOutputFormat.DxtCompressed; });
            textureCase("format_nochange", "probe.png", delegate(TextureProcessor p) { p.TextureFormat = TextureProcessorOutputFormat.NoChange; });
            textureCase("format_dxt_from_ppm", "probe.ppm", delegate(TextureProcessor p) { p.TextureFormat = TextureProcessorOutputFormat.DxtCompressed; });
            textureCase("format_nochange_from_pfm", "probe.pfm", delegate(TextureProcessor p) { p.TextureFormat = TextureProcessorOutputFormat.NoChange; });
            textureCase("format_nochange_from_dds", "probe.dds", delegate(TextureProcessor p) { p.TextureFormat = TextureProcessorOutputFormat.NoChange; });
            textureCase("mipmaps_and_dxt", "probe.png", delegate(TextureProcessor p) {
                p.GenerateMipmaps = true; p.TextureFormat = TextureProcessorOutputFormat.DxtCompressed; });

            // The same processor under each target and profile: what Reach refuses that HiDef allows.
            foreach (string leg in new string[] { "Windows/Reach", "Windows/HiDef", "Xbox360/Reach",
                                                 "Xbox360/HiDef", "WindowsPhone/Reach" })
            {
                string captured = leg;
                string[] parts = captured.Split('/');
                Record("textureprofile/" + captured.Replace('/', '_'), () =>
                {
                    TextureContent texture = new TextureImporter().Import(
                        Path.Combine(fixtureDirectory, "probe.png"), new ProbeImporterContext());
                    var context = new TargetedProcessorContext(
                        (TargetPlatform)Enum.Parse(typeof(TargetPlatform), parts[0]),
                        (GraphicsProfile)Enum.Parse(typeof(GraphicsProfile), parts[1]));
                    var processor = new TextureProcessor();
                    processor.TextureFormat = TextureProcessorOutputFormat.DxtCompressed;
                    return DescribeTexture(processor.Process(texture, context));
                });
            }

            Record("textureimporter/pfm_pixels", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                TextureContent texture = new TextureImporter().Import(WritePfm(directory, "pixels.pfm"),
                                                                      new ProbeImporterContext());
                var bitmap = (PixelBitmapContent<Vector4>)texture.Faces[0][0];
                var builder = new StringBuilder();
                for (int y = 0; y < bitmap.Height; y++)
                    for (int x = 0; x < bitmap.Width; x++)
                    {
                        Vector4 pixel = bitmap.GetPixel(x, y);
                        if (builder.Length > 0) builder.Append(' ');
                        builder.Append("(" + pixel.X.ToString("R", CultureInfo.InvariantCulture) + "," +
                                       pixel.Y.ToString("R", CultureInfo.InvariantCulture) + "," +
                                       pixel.Z.ToString("R", CultureInfo.InvariantCulture) + "," +
                                       pixel.W.ToString("R", CultureInfo.InvariantCulture) + ")");
                    }
                return builder.ToString();
            });
            Record("textureimporter/refusals", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                var builder = new StringBuilder();
                Action<string, string> probe = delegate(string label, string path)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try
                    {
                        TextureContent texture = new TextureImporter().Import(path, new ProbeImporterContext());
                        builder.Append(label + "=accepted");
                    }
                    catch (Exception error) { builder.Append(label + "=" + error.GetType().Name + ": " + error.Message); }
                };
                probe("missing", Path.Combine(directory, "no_such.png"));
                string garbage = Path.Combine(directory, "garbage.png");
                File.WriteAllBytes(garbage, new byte[] { 1, 2, 3, 4, 5 });
                probe("garbage", garbage);
                return builder.ToString();
            });

            // ---- FontDescriptionImporter: the .spritefont schema ---------------------------------
            Record("fontimporter/full", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                string path = Path.Combine(directory, "full.spritefont");
                File.WriteAllText(path,
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n" +
                    "  <Asset Type=\"Graphics:FontDescription\">\r\n" +
                    "    <FontName>Segoe UI Mono</FontName>\r\n    <Size>14</Size>\r\n    <Spacing>2</Spacing>\r\n" +
                    "    <UseKerning>true</UseKerning>\r\n    <Style>Bold</Style>\r\n    <DefaultCharacter>*</DefaultCharacter>\r\n" +
                    "    <CharacterRegions>\r\n      <CharacterRegion>\r\n        <Start>&#32;</Start>\r\n        <End>&#126;</End>\r\n      </CharacterRegion>\r\n" +
                    "      <CharacterRegion>\r\n        <Start>&#160;</Start>\r\n        <End>&#163;</End>\r\n      </CharacterRegion>\r\n" +
                    "    </CharacterRegions>\r\n  </Asset>\r\n</XnaContent>\r\n");
                var importer = new FontDescriptionImporter();
                var context = new ProbeImporterContext();
                FontDescription font = importer.Import(path, context);
                var characters = new List<char>(font.Characters);
                characters.Sort();
                return "type=" + font.GetType().Name + " dependencies=" + context.Dependencies.Count +
                       " identity=" + (font.Identity == null ? "null" : Path.GetFileName(font.Identity.SourceFilename ?? "") + "/" + (font.Identity.SourceTool ?? "null")) +
                       " " + DescribeFont(font) + " count=" + characters.Count +
                       " first=U+" + ((int)characters[0]).ToString("X4") +
                       " last=U+" + ((int)characters[characters.Count - 1]).ToString("X4");
            });
            Record("fontimporter/minimal", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                var builder = new StringBuilder();
                Action<string, string> probe = delegate(string label, string body)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    string path = Path.Combine(directory, label + ".spritefont");
                    File.WriteAllText(path,
                        "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">" +
                        "<Asset Type=\"Graphics:FontDescription\">" + body + "</Asset></XnaContent>");
                    try
                    {
                        FontDescription font = new FontDescriptionImporter().Import(path, new ProbeImporterContext());
                        builder.Append(label + "=[" + DescribeFont(font) + " count=" + new List<char>(font.Characters).Count + "]");
                    }
                    catch (Exception error) { builder.Append(label + "=" + error.GetType().Name + ": " + error.Message); }
                };
                probe("name_size_only", "<FontName>Arial</FontName><Size>10</Size>");
                probe("no_size", "<FontName>Arial</FontName>");
                probe("no_name", "<Size>10</Size>");
                probe("empty_regions", "<FontName>Arial</FontName><Size>10</Size><CharacterRegions></CharacterRegions>");
                probe("no_default_character", "<FontName>Arial</FontName><Size>10</Size><CharacterRegions><CharacterRegion><Start>&#65;</Start><End>&#67;</End></CharacterRegion></CharacterRegions>");
                probe("bad_style", "<FontName>Arial</FontName><Size>10</Size><Style>Sideways</Style>");
                probe("style_bold_italic", "<FontName>Arial</FontName><Size>10</Size><Style>Bold, Italic</Style>");
                probe("reversed_region", "<FontName>Arial</FontName><Size>10</Size><CharacterRegions><CharacterRegion><Start>&#67;</Start><End>&#65;</End></CharacterRegion></CharacterRegions>");
                probe("overlapping_regions", "<FontName>Arial</FontName><Size>10</Size><CharacterRegions><CharacterRegion><Start>&#65;</Start><End>&#70;</End></CharacterRegion><CharacterRegion><Start>&#68;</Start><End>&#74;</End></CharacterRegion></CharacterRegions>");
                probe("fractional_size", "<FontName>Arial</FontName><Size>12.5</Size>");
                probe("negative_spacing", "<FontName>Arial</FontName><Size>10</Size><Spacing>-3</Spacing>");
                probe("required_only", "<FontName>Arial</FontName><Size>10</Size><Style>Regular</Style><CharacterRegions><CharacterRegion><Start>&#65;</Start><End>&#67;</End></CharacterRegion></CharacterRegions>");
                probe("out_of_order", "<Size>10</Size><FontName>Arial</FontName><Style>Regular</Style><CharacterRegions><CharacterRegion><Start>&#65;</Start><End>&#67;</End></CharacterRegion></CharacterRegions>");
                probe("all_optional", "<FontName>Arial</FontName><Size>10</Size><Spacing>1.5</Spacing><UseKerning>false</UseKerning><Style>Italic</Style><DefaultCharacter>?</DefaultCharacter><CharacterRegions><CharacterRegion><Start>&#65;</Start><End>&#67;</End></CharacterRegion></CharacterRegions>");
                probe("region_missing_end", "<FontName>Arial</FontName><Size>10</Size><Style>Regular</Style><CharacterRegions><CharacterRegion><Start>&#65;</Start></CharacterRegion></CharacterRegions>");
                return builder.ToString();
            });
            Record("fontimporter/refusals", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                var builder = new StringBuilder();
                Action<string, string> probe = delegate(string label, string path)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try
                    {
                        FontDescription font = new FontDescriptionImporter().Import(path, new ProbeImporterContext());
                        builder.Append(label + "=accepted");
                    }
                    catch (Exception error) { builder.Append(label + "=" + error.GetType().Name + ": " + error.Message); }
                };
                probe("missing", Path.Combine(directory, "no_such.spritefont"));
                string garbage = Path.Combine(directory, "garbage.spritefont");
                File.WriteAllText(garbage, "not xml at all");
                probe("garbage", garbage);
                string wrongType = Path.Combine(directory, "wrong.spritefont");
                File.WriteAllText(wrongType, "<XnaContent><Asset Type=\"int\">3</Asset></XnaContent>");
                probe("wrong_type", wrongType);
                return builder.ToString();
            });

            // ---- EffectImporter ------------------------------------------------------------------
            Record("effectimporter/source", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                string path = Path.Combine(directory, "simple.fx");
                string source = "float4 Main() : COLOR { return float4(1,0,0,1); }\r\n" +
                                "technique T { pass P { PixelShader = compile ps_2_0 Main(); } }\r\n";
                File.WriteAllText(path, source);
                var importer = new EffectImporter();
                var context = new ProbeImporterContext();
                EffectContent content = importer.Import(path, context);
                return "type=" + content.GetType().Name + " dependencies=" + context.Dependencies.Count +
                       " identity=" + (content.Identity == null ? "null" : Path.GetFileName(content.Identity.SourceFilename ?? "") + "/" + (content.Identity.SourceTool ?? "null")) +
                       " name=" + (content.Name == null ? "null" : "\"" + content.Name + "\"") +
                       " codeIsSource=" + (content.EffectCode == source) +
                       " codeLength=" + (content.EffectCode == null ? -1 : content.EffectCode.Length);
            });
            Record("effectimporter/include", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                File.WriteAllText(Path.Combine(directory, "shared.fxh"), "float4 Tint;\r\n");
                string path = Path.Combine(directory, "uses_include.fx");
                File.WriteAllText(path, "#include \"shared.fxh\"\r\nfloat4 Main() : COLOR { return Tint; }\r\n" +
                                        "technique T { pass P { PixelShader = compile ps_2_0 Main(); } }\r\n");
                var importer = new EffectImporter();
                var context = new ProbeImporterContext();
                EffectContent content = importer.Import(path, context);
                var builder = new StringBuilder("dependencies=" + context.Dependencies.Count);
                foreach (string dependency in context.Dependencies)
                    builder.Append(" dependency=" + Path.GetFileName(dependency));
                builder.Append(" codeStartsWithInclude=" + (content.EffectCode != null && content.EffectCode.StartsWith("#include")));
                return builder.ToString();
            });
            Record("effectimporter/refusals", () =>
            {
                string directory = Path.Combine(outputDirectory, "work");
                Directory.CreateDirectory(directory);
                var builder = new StringBuilder();
                Action<string, string> probe = delegate(string label, string path)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try
                    {
                        var importer = new EffectImporter();
                        EffectContent content = importer.Import(path, new ProbeImporterContext());
                        builder.Append(label + "=" + (content == null ? "null" : "accepted:" + (content.EffectCode == null ? "null-code" : content.EffectCode.Length.ToString())));
                    }
                    catch (Exception error) { builder.Append(label + "=" + error.GetType().Name + ": " + error.Message); }
                };
                probe("missing", Path.Combine(directory, "no_such.fx"));
                string empty = Path.Combine(directory, "empty.fx");
                File.WriteAllText(empty, "");
                probe("empty", empty);
                string binary = Path.Combine(directory, "binary.fx");
                File.WriteAllBytes(binary, new byte[] { 0, 1, 2, 3, 255 });
                probe("binary", binary);
                return builder.ToString();
            });

            // ---- TextureReferenceDictionary ------------------------------------------------------
            Record("texturereferencedictionary/default", () => { var d = new TextureReferenceDictionary(); return "count=" + d.Count + " ToString=\"" + d + "\""; });

            File.WriteAllText(Path.Combine(outputDirectory, "graphics-content-oracle.json"),
                "{\n \"producer\": \"Microsoft XNA Game Studio 4.0 Content Pipeline (Microsoft.Xna.Framework.Content.Pipeline.Graphics), driven by tools/xna-pipeline-oracle/graphics/GraphicsContentOracle.cs\",\n \"runtime\": \"" +
                Environment.Version + "\",\n \"pipelineAssembly\": \"" + typeof(BitmapContent).Assembly.FullName + "\",\n \"cases\": [\n" +
                string.Join(",\n", Cases.ToArray()) + "\n ]\n}\n");
            Console.WriteLine("recorded " + Cases.Count + " measurements");
        }

        /// The five accessors MaterialContent declares protected are reachable only from a derived
        /// type, which is how a game's own material reaches them too.
        private sealed class ProbeMaterial : MaterialContent
        {
            public const string ProbeKey = "Alpha";
            public ExternalReference<TextureContent> ReadTexture(string key) { return GetTexture(key); }
            public void WriteTexture(string key, ExternalReference<TextureContent> value) { SetTexture(key, value); }
            public T ReadReference<T>(string key) where T : class { return GetReferenceTypeProperty<T>(key); }
            public T? ReadValue<T>(string key) where T : struct { return GetValueTypeProperty<T>(key); }
            public void Write<T>(string key, T value) { SetProperty(key, value); }
        }

        private static MeshContent TriangleMesh()
        {
            var mesh = new MeshContent();
            mesh.Name = "Mesh";
            mesh.Positions.Add(new Vector3(0, 0, 0));
            mesh.Positions.Add(new Vector3(1, 0, 0));
            mesh.Positions.Add(new Vector3(0, 1, 0));
            var geometry = new GeometryContent();
            mesh.Geometry.Add(geometry);
            geometry.Vertices.AddRange(new int[] { 0, 1, 2 });
            geometry.Indices.AddRange(new int[] { 0, 1, 2 });
            geometry.Vertices.Channels.Add<Vector3>(VertexChannelNames.Normal(), new Vector3[] { Vector3.UnitZ, Vector3.UnitZ, Vector3.UnitZ });
            geometry.Vertices.Channels.Add<Vector2>(VertexChannelNames.TextureCoordinate(0), new Vector2[] { new Vector2(0, 0), new Vector2(1, 0), new Vector2(0, 1) });
            return mesh;
        }

        private static NodeContent TriangleScene()
        {
            var root = new NodeContent();
            root.Name = "Root";
            root.Children.Add(TriangleMesh());
            return root;
        }

        /// DescribeModel, plus every bone's whole matrix and the vertex data itself: what tells a
        /// baked transform from one left on a bone.
        private static string DescribeModelFull(ModelContent model)
        {
            var builder = new StringBuilder(DescribeModel(model));
            foreach (ModelBoneContent bone in model.Bones)
                builder.Append(" matrix[" + bone.Index + "]=" + DescribeMatrixFull(bone.Transform));
            foreach (ModelMeshContent mesh in model.Meshes)
                foreach (ModelMeshPartContent part in mesh.MeshParts)
                {
                    if (part.VertexBuffer != null) builder.Append(" data=" + Hex(part.VertexBuffer.VertexData));
                    if (part.IndexBuffer != null)
                    {
                        builder.Append(" indices=");
                        for (int i = 0; i < part.IndexBuffer.Count; i++)
                            builder.Append((i == 0 ? "" : ",") + part.IndexBuffer[i]);
                    }
                }
            return builder.ToString();
        }

        private static string DescribeMatrixFull(Matrix m)
        {
            float[] values = new float[] { m.M11, m.M12, m.M13, m.M14, m.M21, m.M22, m.M23, m.M24,
                                           m.M31, m.M32, m.M33, m.M34, m.M41, m.M42, m.M43, m.M44 };
            var builder = new StringBuilder("[");
            for (int i = 0; i < values.Length; i++)
                builder.Append((i == 0 ? "" : ",") + values[i].ToString("R", CultureInfo.InvariantCulture));
            return builder.Append("]").ToString();
        }

        private static string DescribeModel(ModelContent model)
        {
            var builder = new StringBuilder("bones=" + model.Bones.Count + " meshes=" + model.Meshes.Count +
                                            " root=" + (model.Root == null ? "null" : model.Root.Name) +
                                            " tag=" + (model.Tag == null ? "null" : model.Tag.ToString()));
            foreach (ModelBoneContent bone in model.Bones)
            {
                builder.Append(" bone[" + bone.Index + "]=" + (bone.Name ?? "null") + ":" + Describe(bone.Transform) +
                               ":parent=" + (bone.Parent == null ? "null" : bone.Parent.Index.ToString()) +
                               ":children=" + bone.Children.Count);
            }
            foreach (ModelMeshContent mesh in model.Meshes)
            {
                builder.Append(" mesh=" + (mesh.Name ?? "null") + ":parts=" + mesh.MeshParts.Count +
                               ":bone=" + (mesh.ParentBone == null ? "null" : mesh.ParentBone.Index.ToString()) +
                               ":sphere=" + mesh.BoundingSphere.Radius.ToString("R", CultureInfo.InvariantCulture) +
                               ":source=" + (mesh.SourceMesh == null ? "null" : mesh.SourceMesh.Name));
                foreach (ModelMeshPartContent part in mesh.MeshParts)
                {
                    builder.Append(" part=" + part.NumVertices + "v/" + part.PrimitiveCount + "p/start=" + part.StartIndex +
                                   "/offset=" + part.VertexOffset + "/indices=" + (part.IndexBuffer == null ? "null" : part.IndexBuffer.Count.ToString()) +
                                   "/material=" + (part.Material == null ? "null" : part.Material.GetType().Name) +
                                   "/stride=" + (part.VertexBuffer == null || !part.VertexBuffer.VertexDeclaration.VertexStride.HasValue ? "null" : part.VertexBuffer.VertexDeclaration.VertexStride.Value.ToString()) +
                                   "/elements=" + (part.VertexBuffer == null ? "null" : part.VertexBuffer.VertexDeclaration.VertexElements.Count.ToString()) +
                                   "/bytes=" + (part.VertexBuffer == null ? "null" : part.VertexBuffer.VertexData.Length.ToString()));
                    if (part.VertexBuffer != null)
                    {
                        foreach (VertexElement element in part.VertexBuffer.VertexDeclaration.VertexElements)
                        {
                            builder.Append(" element=" + element.VertexElementUsage + element.UsageIndex + ":" + element.VertexElementFormat + "@" + element.Offset);
                        }
                    }
                }
            }
            return builder.ToString();
        }

        /// A context that records every asset a processor asks it to build, and answers a
        /// reference so the processor can carry on.
        private sealed class RecordingProcessorContext : ContentProcessorContext
        {
            private readonly OpaqueDataDictionary parameters = new OpaqueDataDictionary();
            private readonly ContentBuildLogger logger = new ProbeLogger();
            private readonly StringBuilder built = new StringBuilder();
            private readonly string configuration;
            public RecordingProcessorContext() { configuration = "Debug"; }
            public RecordingProcessorContext(string buildConfiguration) { configuration = buildConfiguration; }
            public string Built { get { return "[" + built + "]"; } }
            public override string BuildConfiguration { get { return configuration; } }
            public override string IntermediateDirectory { get { return "obj"; } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return "bin"; } }
            public override string OutputFilename { get { return "asset.xnb"; } }
            public override OpaqueDataDictionary Parameters { get { return parameters; } }
            public override TargetPlatform TargetPlatform { get { return TargetPlatform.Windows; } }
            public override GraphicsProfile TargetProfile { get { return GraphicsProfile.HiDef; } }
            public override void AddDependency(string filename) { }
            public override void AddOutputFile(string filename) { }
            public override TOutput BuildAndLoadAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName)
            { throw new NotSupportedException("BuildAndLoadAsset"); }
            public override ExternalReference<TOutput> BuildAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName, string assetName)
            {
                if (built.Length > 0) built.Append(' ');
                built.Append(System.IO.Path.GetFileName(sourceAsset.Filename) + "->" + (processorName ?? "null") + "(" + DescribeParameters(processorParameters) + ")" +
                             " importer=" + (importerName ?? "null") + " asset=" + (assetName ?? "null") + " out=" + typeof(TOutput).Name);
                return new ExternalReference<TOutput>(sourceAsset.Filename + ".xnb");
            }
            public override TOutput Convert<TInput, TOutput>(TInput input, string processorName, OpaqueDataDictionary processorParameters)
            {
                if (built.Length > 0) built.Append(' ');
                built.Append("convert:" + typeof(TInput).Name + "->" + (processorName ?? "null") + "(" + DescribeParameters(processorParameters) + ")->" + typeof(TOutput).Name);
                // The processor asked for a conversion; answering the input keeps the graph whole
                // so the rest of the processing can be measured.
                return (TOutput)(object)input;
            }

            private static string DescribeParameters(OpaqueDataDictionary values)
            {
                if (values == null) return "null";
                var keys = new List<string>();
                foreach (KeyValuePair<string, object> entry in values) keys.Add(entry.Key);
                keys.Sort(StringComparer.Ordinal);
                var builder = new StringBuilder();
                foreach (string key in keys)
                {
                    if (builder.Length > 0) builder.Append(',');
                    object value = values[key];
                    builder.Append(key + "=" + (value == null ? "null" : Convert.ToString(value, CultureInfo.InvariantCulture)));
                }
                return builder.ToString();
            }
        }

        /// A build context that answers what a processor asks of it and refuses the rest, so a
        /// processor's own work can be measured without XNA's internal build engine.
        private sealed class ProbeProcessorContext : ContentProcessorContext
        {
            private readonly OpaqueDataDictionary parameters = new OpaqueDataDictionary();
            private readonly ContentBuildLogger logger = new ProbeLogger();
            public override string BuildConfiguration { get { return "Debug"; } }
            public override string IntermediateDirectory { get { return "obj"; } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return "bin"; } }
            public override string OutputFilename { get { return "asset.xnb"; } }
            public override OpaqueDataDictionary Parameters { get { return parameters; } }
            public override TargetPlatform TargetPlatform { get { return TargetPlatform.Windows; } }
            public override GraphicsProfile TargetProfile { get { return GraphicsProfile.HiDef; } }
            public override void AddDependency(string filename) { }
            public override void AddOutputFile(string filename) { }
            public override TOutput BuildAndLoadAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName)
            { throw new NotSupportedException("BuildAndLoadAsset"); }
            public override ExternalReference<TOutput> BuildAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName, string assetName)
            { throw new NotSupportedException("BuildAsset"); }
            public override TOutput Convert<TInput, TOutput>(TInput input, string processorName, OpaqueDataDictionary processorParameters)
            { throw new NotSupportedException("Convert"); }
        }

        /// The same probe, with the target and profile chosen, for the Reach/HiDef rows.
        private sealed class TargetedProcessorContext : ContentProcessorContext
        {
            private readonly OpaqueDataDictionary parameters = new OpaqueDataDictionary();
            private readonly ContentBuildLogger logger = new ProbeLogger();
            private readonly TargetPlatform platform;
            private readonly GraphicsProfile profile;
            public TargetedProcessorContext(TargetPlatform targetPlatform, GraphicsProfile targetProfile)
            { platform = targetPlatform; profile = targetProfile; }
            public override string BuildConfiguration { get { return "Debug"; } }
            public override string IntermediateDirectory { get { return "obj"; } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return "bin"; } }
            public override string OutputFilename { get { return "asset.xnb"; } }
            public override OpaqueDataDictionary Parameters { get { return parameters; } }
            public override TargetPlatform TargetPlatform { get { return platform; } }
            public override GraphicsProfile TargetProfile { get { return profile; } }
            public override void AddDependency(string filename) { }
            public override void AddOutputFile(string filename) { }
            public override TOutput BuildAndLoadAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName)
            { throw new NotSupportedException("BuildAndLoadAsset"); }
            public override ExternalReference<TOutput> BuildAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName, string assetName)
            { throw new NotSupportedException("BuildAsset"); }
            public override TOutput Convert<TInput, TOutput>(TInput input, string processorName, OpaqueDataDictionary processorParameters)
            { throw new NotSupportedException("Convert"); }
        }

        /// The least an importer needs: a context that logs nothing and keeps what it is told.
        private sealed class ProbeImporterContext : ContentImporterContext
        {
            private readonly ContentBuildLogger logger = new ProbeLogger();
            public readonly List<string> Dependencies = new List<string>();
            public override string IntermediateDirectory { get { return "obj"; } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return "bin"; } }
            public override void AddDependency(string filename) { Dependencies.Add(filename); }
        }

        private sealed class ProbeLogger : ContentBuildLogger
        {
            public override void LogImportantMessage(string message, params object[] messageArgs) { }
            public override void LogMessage(string message, params object[] messageArgs) { }
            public override void LogWarning(string helpLink, ContentIdentity contentIdentity, string message, params object[] messageArgs) { }
        }

        private static TextureContent Process(TextureProcessor processor, TextureContent texture)
        {
            return processor.Process(texture, new ProbeProcessorContext());
        }

        private static Texture2DContent ColorTexture(int width, int height)
        {
            var texture = new Texture2DContent();
            texture.Mipmaps.Add(Gradient(width, height));
            return texture;
        }

        /// The four pixels every source-format probe carries: red, green, blue, half-transparent white.
        private static readonly byte[][] ProbePixels = new byte[][]
        {
            new byte[] { 255, 0, 0, 255 },
            new byte[] { 0, 255, 0, 255 },
            new byte[] { 0, 0, 255, 255 },
            new byte[] { 255, 255, 255, 128 },
        };

        private static string WriteImage(string directory, string name, System.Drawing.Imaging.ImageFormat format)
        {
            string path = Path.Combine(directory, name);
            using (var bitmap = new System.Drawing.Bitmap(2, 2, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
            {
                for (int i = 0; i < 4; i++)
                {
                    byte[] pixel = ProbePixels[i];
                    bitmap.SetPixel(i % 2, i / 2,
                                    System.Drawing.Color.FromArgb(pixel[3], pixel[0], pixel[1], pixel[2]));
                }
                bitmap.Save(path, format);
            }
            return path;
        }

        private static string WriteTga(string directory, string name)
        {
            string path = Path.Combine(directory, name);
            using (var stream = new FileStream(path, FileMode.Create, FileAccess.Write))
            using (var writer = new BinaryWriter(stream))
            {
                writer.Write(new byte[] { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
                writer.Write((short)2);
                writer.Write((short)2);
                writer.Write((byte)32);
                writer.Write((byte)0x28);   // top-left origin, eight alpha bits
                for (int i = 0; i < 4; i++)
                {
                    byte[] pixel = ProbePixels[i];
                    writer.Write(new byte[] { pixel[2], pixel[1], pixel[0], pixel[3] });
                }
            }
            return path;
        }

        private static string WritePpm(string directory, string name)
        {
            string path = Path.Combine(directory, name);
            var bytes = new List<byte>(Encoding.ASCII.GetBytes("P6\n2 2\n255\n"));
            foreach (byte[] pixel in ProbePixels) { bytes.Add(pixel[0]); bytes.Add(pixel[1]); bytes.Add(pixel[2]); }
            File.WriteAllBytes(path, bytes.ToArray());
            return path;
        }

        private static string WritePfm(string directory, string name)
        {
            string path = Path.Combine(directory, name);
            var bytes = new List<byte>(Encoding.ASCII.GetBytes("PF\n2 2\n-1.0\n"));
            // A PFM's rows run bottom to top, and each pixel is three little-endian floats.
            for (int row = 1; row >= 0; row--)
                for (int column = 0; column < 2; column++)
                {
                    byte[] pixel = ProbePixels[row * 2 + column];
                    for (int channel = 0; channel < 3; channel++)
                        bytes.AddRange(BitConverter.GetBytes(pixel[channel] / 255.0f));
                }
            File.WriteAllBytes(path, bytes.ToArray());
            return path;
        }

        private static string WriteDib(string directory, string name)
        {
            // A DIB is a BMP without its fourteen-byte file header.
            string bmp = WriteImage(directory, "probe_for_dib.bmp", System.Drawing.Imaging.ImageFormat.Bmp);
            byte[] all = File.ReadAllBytes(bmp);
            var body = new byte[all.Length - 14];
            Array.Copy(all, 14, body, 0, body.Length);
            string path = Path.Combine(directory, name);
            File.WriteAllBytes(path, body);
            return path;
        }

        private static string WriteDds(string directory, string name)
        {
            string path = Path.Combine(directory, name);
            var bytes = new List<byte>(Encoding.ASCII.GetBytes("DDS "));
            Action<int> Word = delegate(int value) { bytes.AddRange(BitConverter.GetBytes(value)); };
            Word(124);                     // header size
            Word(0x1 | 0x2 | 0x4 | 0x1000 | 0x8);  // caps, height, width, pixel format, pitch
            Word(2);                       // height
            Word(2);                       // width
            Word(2 * 4);                   // pitch
            Word(0);                       // depth
            Word(0);                       // mip count
            for (int i = 0; i < 11; i++) { Word(0); }  // reserved
            Word(32);                      // pixel format size
            Word(0x1 | 0x40);              // alpha pixels, RGB
            Word(0);                       // four cc
            Word(32);                      // bit count
            Word(0x00FF0000);              // red mask
            Word(0x0000FF00);              // green mask
            Word(0x000000FF);              // blue mask
            Word(unchecked((int)0xFF000000));      // alpha mask
            Word(0x1000);                  // caps: texture
            Word(0);
            Word(0);
            Word(0);
            Word(0);
            foreach (byte[] pixel in ProbePixels)
            {
                bytes.Add(pixel[2]);
                bytes.Add(pixel[1]);
                bytes.Add(pixel[0]);
                bytes.Add(pixel[3]);
            }
            File.WriteAllBytes(path, bytes.ToArray());
            return path;
        }

        /// Writes a DDS carrying compressed blocks (or a DX10 header), with the shape asked for.
        private static string WriteDdsCompressed(string directory, string name, string fourCc, int width,
                                                 int height, int mipCount, bool cube, bool volume)
        {
            string path = Path.Combine(directory, name);
            var bytes = new List<byte>(Encoding.ASCII.GetBytes("DDS "));
            Action<int> Word = delegate(int value) { bytes.AddRange(BitConverter.GetBytes(value)); };
            int blockBytes = fourCc == "DXT1" ? 8 : 16;
            int depth = volume ? 2 : 0;
            Word(124);
            int flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000;    // caps, height, width, pixel format, linear size
            if (mipCount > 1) flags |= 0x20000;
            if (volume) flags |= 0x800000;
            Word(flags);
            Word(height);
            Word(width);
            Word(Math.Max(1, width / 4) * Math.Max(1, height / 4) * blockBytes);
            Word(depth);
            Word(mipCount);
            for (int i = 0; i < 11; i++) { Word(0); }
            Word(32);
            Word(0x4);                                          // four cc
            bytes.AddRange(Encoding.ASCII.GetBytes(fourCc));
            Word(0);
            Word(0);
            Word(0);
            Word(0);
            Word(0);
            int caps = 0x1000 | (mipCount > 1 ? 0x400000 | 0x8 : 0) | (cube || volume ? 0x8 : 0);
            Word(caps);
            Word(cube ? 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000 : (volume ? 0x200000 : 0));
            Word(0);
            Word(0);
            Word(0);
            if (fourCc == "DX10")
            {
                Word(71);                                       // DXGI_FORMAT_BC1_UNORM
                Word(3);                                        // TEXTURE2D
                Word(0);
                Word(1);                                        // array size
                Word(0);
            }
            int faces = cube ? 6 : 1;
            int slices = volume ? 2 : 1;
            for (int face = 0; face < faces; face++)
                for (int level = 0; level < mipCount; level++)
                {
                    int levelWidth = Math.Max(1, width >> level);
                    int levelHeight = Math.Max(1, height >> level);
                    int blocks = Math.Max(1, levelWidth / 4) * Math.Max(1, levelHeight / 4) * slices;
                    for (int block = 0; block < blocks; block++)
                        for (int i = 0; i < blockBytes; i++)
                            bytes.Add((byte)((block * 31 + i * 7 + face * 13 + level * 3) & 0xFF));
                }
            File.WriteAllBytes(path, bytes.ToArray());
            return path;
        }

        private static string DescribeTexture(TextureContent texture)
        {
            var builder = new StringBuilder(texture.GetType().Name + " faces=" + texture.Faces.Count);
            for (int face = 0; face < texture.Faces.Count; face++)
            {
                builder.Append(" [");
                for (int level = 0; level < texture.Faces[face].Count; level++)
                {
                    BitmapContent bitmap = texture.Faces[face][level];
                    if (level > 0) builder.Append(' ');
                    SurfaceFormat format;
                    bool hasFormat = bitmap.TryGetFormat(out format);
                    builder.Append(bitmap.Width + "x" + bitmap.Height + ":" + (hasFormat ? format.ToString() : "none"));
                }
                builder.Append(']');
            }
            if (texture.Faces.Count == 1 && texture.Faces[0].Count > 0)
            {
                BitmapContent first = texture.Faces[0][0];
                SurfaceFormat format;
                if (first.TryGetFormat(out format) && format == SurfaceFormat.Color && first.Width * first.Height <= 16)
                {
                    builder.Append(" pixels=" + Hex(first.GetPixelData()));
                }
            }
            return builder.ToString();
        }

        /// Every public property of a processor, in name order, with its value: the defaults table,
        /// read from the object rather than from a document.
        private static string Properties(object instance)
        {
            var names = new List<string>();
            foreach (System.Reflection.PropertyInfo property in instance.GetType().GetProperties())
            {
                if (property.GetIndexParameters().Length == 0 && property.CanRead) names.Add(property.Name);
            }
            names.Sort(StringComparer.Ordinal);
            var builder = new StringBuilder();
            foreach (string name in names)
            {
                System.Reflection.PropertyInfo property = instance.GetType().GetProperty(name);
                object value;
                try { value = property.GetValue(instance, null); }
                catch (Exception error) { value = error.GetType().Name; }
                if (builder.Length > 0) builder.Append(' ');
                string text;
                if (value == null) text = "null";
                else if (value is float) text = ((float)value).ToString("R", CultureInfo.InvariantCulture);
                else text = Convert.ToString(value, CultureInfo.InvariantCulture);
                builder.Append(name + "=" + text);
            }
            return builder.ToString();
        }

        private static string Describe(Matrix matrix)
        {
            return "(" + matrix.M41.ToString("R", CultureInfo.InvariantCulture) + "," +
                   matrix.M42.ToString("R", CultureInfo.InvariantCulture) + "," +
                   matrix.M43.ToString("R", CultureInfo.InvariantCulture) + ")";
        }

        private static string Positions(System.Collections.Generic.IEnumerable<Vector3> positions)
        {
            var builder = new StringBuilder();
            foreach (Vector3 position in positions)
            {
                if (builder.Length > 0) builder.Append(' ');
                builder.Append(position.X.ToString("R", CultureInfo.InvariantCulture));
            }
            return "[" + builder + "]";
        }

        /// A mesh in full: its positions, and per geometry its vertex indices, triangle indices and
        /// every channel's values. What tells a merge from a copy, and a reorder from a rewrite.
        private static string DescribeMeshFull(MeshContent mesh)
        {
            if (mesh == null) return "null";
            var builder = new StringBuilder("name=" + (mesh.Name ?? "null") + " positions=" + mesh.Positions.Count + "[");
            for (int i = 0; i < mesh.Positions.Count; i++)
                builder.Append((i == 0 ? "" : " ") + VectorText(mesh.Positions[i]));
            builder.Append("] geometry=" + mesh.Geometry.Count);
            foreach (GeometryContent geometry in mesh.Geometry)
            {
                builder.Append(" {vertices=" + geometry.Vertices.VertexCount + " positionIndices=[");
                for (int i = 0; i < geometry.Vertices.PositionIndices.Count; i++)
                    builder.Append((i == 0 ? "" : ",") + geometry.Vertices.PositionIndices[i]);
                builder.Append("] indices=[");
                for (int i = 0; i < geometry.Indices.Count; i++)
                    builder.Append((i == 0 ? "" : ",") + geometry.Indices[i]);
                builder.Append("] material=" + (geometry.Material == null ? "null" : geometry.Material.GetType().Name));
                builder.Append(" opaque=" + geometry.OpaqueData.Count);
                foreach (VertexChannel channel in geometry.Vertices.Channels)
                {
                    builder.Append(" channel=" + channel.Name + ":" + channel.ElementType.Name + "[");
                    for (int i = 0; i < channel.Count; i++)
                        builder.Append((i == 0 ? "" : " ") + ValueText(channel[i]));
                    builder.Append("]");
                }
                builder.Append("}");
            }
            return builder.ToString();
        }

        private static string VectorText(Vector3 value)
        {
            return "(" + value.X.ToString("R", CultureInfo.InvariantCulture) + "," +
                   value.Y.ToString("R", CultureInfo.InvariantCulture) + "," +
                   value.Z.ToString("R", CultureInfo.InvariantCulture) + ")";
        }

        private static string ValueText(object value)
        {
            if (value == null) return "null";
            if (value is Vector3) return VectorText((Vector3)value);
            if (value is Vector2)
            {
                Vector2 v = (Vector2)value;
                return "(" + v.X.ToString("R", CultureInfo.InvariantCulture) + "," + v.Y.ToString("R", CultureInfo.InvariantCulture) + ")";
            }
            if (value is float) return ((float)value).ToString("R", CultureInfo.InvariantCulture);
            return Convert.ToString(value, CultureInfo.InvariantCulture);
        }

        /// The one mesh the MeshBuilder and MeshHelper cases start from: a unit quad of two
        /// triangles, built through the builder itself so the builder's own output is measured.
        private static MeshContent BuiltQuad(bool merge, bool swap)
        {
            MeshBuilder builder = MeshBuilder.StartMesh("Quad");
            builder.MergeDuplicatePositions = merge;
            builder.SwapWindingOrder = swap;
            int normals = builder.CreateVertexChannel<Vector3>(VertexChannelNames.Normal());
            int coords = builder.CreateVertexChannel<Vector2>(VertexChannelNames.TextureCoordinate(0));
            int a = builder.CreatePosition(0, 0, 0);
            int b = builder.CreatePosition(1, 0, 0);
            int c = builder.CreatePosition(1, 1, 0);
            int d = builder.CreatePosition(0, 1, 0);
            int[] corners = new int[] { a, b, c, a, c, d };
            Vector2[] uv = new Vector2[] { new Vector2(0, 0), new Vector2(1, 0), new Vector2(1, 1),
                                           new Vector2(0, 0), new Vector2(1, 1), new Vector2(0, 1) };
            for (int i = 0; i < corners.Length; i++)
            {
                builder.SetVertexChannelData(normals, Vector3.UnitZ);
                builder.SetVertexChannelData(coords, uv[i]);
                builder.AddTriangleVertex(corners[i]);
            }
            return builder.FinishMesh();
        }

        /// A tent: two triangles meeting along a shared edge at a right angle, which is what tells
        /// an averaged vertex normal from a face normal.
        private static MeshContent Tent()
        {
            var mesh = new MeshContent();
            mesh.Name = "Tent";
            mesh.Positions.Add(new Vector3(0, 0, 0));
            mesh.Positions.Add(new Vector3(1, 0, 0));
            mesh.Positions.Add(new Vector3(0, 1, 0));
            mesh.Positions.Add(new Vector3(0, 0, 1));
            var geometry = new GeometryContent();
            mesh.Geometry.Add(geometry);
            geometry.Vertices.AddRange(new int[] { 0, 1, 2, 3 });
            geometry.Indices.AddRange(new int[] { 0, 1, 2, 0, 3, 1 });
            return mesh;
        }

        /// A grid of quads, which is a mesh big enough for a cache optimizer to have a choice.
        private static MeshContent Grid(int side)
        {
            var mesh = new MeshContent();
            mesh.Name = "Grid";
            for (int y = 0; y <= side; y++)
                for (int x = 0; x <= side; x++)
                    mesh.Positions.Add(new Vector3(x, y, 0));
            var geometry = new GeometryContent();
            mesh.Geometry.Add(geometry);
            var vertices = new List<int>();
            var indices = new List<int>();
            for (int y = 0; y < side; y++)
                for (int x = 0; x < side; x++)
                {
                    int a = y * (side + 1) + x, b = a + 1, c = a + side + 1, d = c + 1;
                    foreach (int corner in new int[] { a, b, d, a, d, c })
                    {
                        indices.Add(vertices.Count);
                        vertices.Add(corner);
                    }
                }
            geometry.Vertices.AddRange(vertices.ToArray());
            geometry.Indices.AddRange(indices.ToArray());
            return mesh;
        }

        private static string DescribeVertices(VertexContent vertices)
        {
            var indices = new StringBuilder();
            foreach (int index in vertices.PositionIndices)
            {
                if (indices.Length > 0) indices.Append(' ');
                indices.Append(index);
            }
            return "count=" + vertices.VertexCount + " indices=[" + indices + "] channels=" + vertices.Channels.Count;
        }

        private static string ClearedChannelCount(VertexChannelCollection channels)
        {
            channels.Clear();
            return channels.Count.ToString();
        }

        private static string Weights(BoneWeightCollection weights)
        {
            var builder = new StringBuilder("count=" + weights.Count + " [");
            bool first = true;
            foreach (BoneWeight weight in weights)
            {
                if (!first) builder.Append(' ');
                first = false;
                builder.Append(weight.BoneName + "=" + weight.Weight.ToString("R", CultureInfo.InvariantCulture));
            }
            builder.Append(']');
            return builder.ToString();
        }

        private static string DescribeMaterial(MaterialContent material)
        {
            var builder = new StringBuilder(material.GetType().Name);
            builder.Append(" opaque={");
            bool first = true;
            foreach (KeyValuePair<string, object> entry in material.OpaqueData)
            {
                if (!first) builder.Append(' ');
                first = false;
                object value = entry.Value;
                string text;
                if (value == null) text = "null";
                else if (value is float) text = ((float)value).ToString("R", CultureInfo.InvariantCulture);
                else text = Convert.ToString(value, CultureInfo.InvariantCulture);
                builder.Append(entry.Key + "=" + text + ":" + (value == null ? "null" : value.GetType().Name));
            }
            builder.Append("} textures={");
            first = true;
            foreach (KeyValuePair<string, ExternalReference<TextureContent>> entry in material.Textures)
            {
                if (!first) builder.Append(' ');
                first = false;
                builder.Append(entry.Key + "=" + (entry.Value == null ? "null" : entry.Value.Filename));
            }
            builder.Append('}');
            return builder.ToString();
        }

        private static string Characters(FontDescription font)
        {
            var builder = new StringBuilder();
            foreach (char c in font.Characters)
            {
                if (builder.Length > 0) builder.Append(' ');
                builder.Append("U+" + ((int)c).ToString("X4"));
            }
            return builder.ToString();
        }

        private static string DescribeFont(FontDescription font)
        {
            return "name=\"" + font.FontName + "\" size=" + font.Size.ToString("R", CultureInfo.InvariantCulture) +
                   " spacing=" + font.Spacing.ToString("R", CultureInfo.InvariantCulture) +
                   " style=" + font.Style + " kerning=" + font.UseKerning +
                   " default=" + (font.DefaultCharacter.HasValue ? "U+" + ((int)font.DefaultCharacter.Value).ToString("X4") : "null") +
                   " chars=[" + Characters(font) + "]";
        }

        private static string SerializeIntermediate(object value)
        {
            var text = new StringWriter(CultureInfo.InvariantCulture);
            var settings = new XmlWriterSettings();
            settings.Indent = true;
            using (XmlWriter writer = XmlWriter.Create(text, settings))
            {
                IntermediateSerializer.Serialize(writer, value, null);
            }
            string xml = text.ToString();
            int cut = xml.IndexOf("?>");
            return cut < 0 ? xml : xml.Substring(cut + 2).TrimStart('\r', '\n');
        }

        private static T DeserializeIntermediate<T>(string xml)
        {
            using (XmlReader reader = XmlReader.Create(new StringReader(xml)))
            {
                return IntermediateSerializer.Deserialize<T>(reader, null);
            }
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
