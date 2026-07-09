// Task 471: small FNA reference-app generator (Phase 53's own opening task).
//
// Proves the whole harness genuinely works end to end: real FNA.dll reference resolves and
// loads under mono, a real non-GraphicsDevice-dependent FNA API call executes correctly, and the
// result is written out as JSON -- the exact shape Tasks 472+ (non-rendering API defaults,
// PackedVector, BasicEffect defaults/lighting constants, SpriteFont.MeasureString,
// Viewport.Project/Unproject) will extend with real per-class reference-value coverage.
//
// Usage: mono bin/Debug/FnaReference.exe [output.json]
// Defaults to writing reference-values.json next to the built executable if no path is given.
using System;
using System.IO;
using Microsoft.Xna.Framework;

namespace CNA.FnaReference
{
    public static class Program
    {
        public static int Main(string[] args)
        {
            string outputPath = args.Length > 0
                ? args[0]
                : Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "reference-values.json");

            Console.WriteLine("CNA FNA reference-app generator (Task 471)");
            Console.WriteLine("FNA assembly: " + typeof(Vector3).Assembly.FullName);

            // A genuine call into FNA's own real MathHelper -- not a hand-transcribed constant.
            var mathHelper = new JsonWriter()
                .Add("Pi", (double)MathHelper.Pi)
                .Add("PiOver2", (double)MathHelper.PiOver2)
                .Add("PiOver4", (double)MathHelper.PiOver4)
                .Add("TwoPi", (double)MathHelper.TwoPi);

            // A genuine call into FNA's own real Color -- confirms the packed layout Tasks 472+
            // will rely on for the rest of the PackedVector/Color reference-value work.
            Color cornflowerBlue = Color.CornflowerBlue;
            var colorConstants = new JsonWriter()
                .Add("CornflowerBlue_R", (int)cornflowerBlue.R)
                .Add("CornflowerBlue_G", (int)cornflowerBlue.G)
                .Add("CornflowerBlue_B", (int)cornflowerBlue.B)
                .Add("CornflowerBlue_A", (int)cornflowerBlue.A)
                .Add("CornflowerBlue_PackedValue", (double)cornflowerBlue.PackedValue);

            // Task 472: non-rendering-API reference values (enum numeric values, state presets).
            JsonWriter nonRenderingApis = NonRenderingApiReference.Generate();

            // Task 473: PackedVector reference values, complementing Task 197's own
            // hand-derived tests/PackedVectorGolden.md.
            JsonWriter packedVectors = PackedVectorReference.Generate();

            var root = new JsonWriter()
                .Add("fnaAssemblyVersion", typeof(Vector3).Assembly.GetName().Version.ToString())
                .Add("MathHelper", mathHelper)
                .Add("Color", colorConstants)
                .Add("NonRenderingApis", nonRenderingApis)
                .Add("PackedVector", packedVectors);

            string json = root.ToString();
            File.WriteAllText(outputPath, json);

            Console.WriteLine("Wrote " + outputPath);
            Console.WriteLine(json);
            return 0;
        }
    }
}
