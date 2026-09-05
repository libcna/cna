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
            Record("vertexcontent/tostring", () => new GeometryContent().Vertices + "|" + new GeometryContent().Vertices.Channels + "|" + new GeometryContent().Vertices.PositionIndices);

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
