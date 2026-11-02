// SPDX-License-Identifier: MS-PL
// Run with dotnet FnaReference.dll <output.json>, or an effect mode plus input/output paths.
// ReferenceMetadata validates source and binary pins before entering FNA-dependent code.
using System;
using System.IO;
using Microsoft.Xna.Framework;

namespace CNA.FnaReference
{
    public static class Program
    {
        public static int Main(string[] args)
        {
            try
            {
                bool effects = args.Length > 0 && (args[0] == "--effects" ||
                    args[0] == "--effect-states" || args[0] == "--effect-pixels");
                if ((effects && args.Length != 3) || (!effects &&
                    (args.Length != 1 || args[0].StartsWith("--"))))
                {
                    Console.Error.WriteLine("usage: FnaReference <output.json> or --effects/--effect-states/--effect-pixels <fxb-directory> <output.json>");
                    return 2;
                }
                ReferenceMetadata.Initialize(effects);
                return Run(args);
            }
            catch (Exception error)
            {
                Console.Error.WriteLine("Reference probe refused: " + error.Message);
                return 1;
            }
        }

        private static int Run(string[] args)
        {
            if (args[0] == "--effects")
                return EffectReflectionReference.Run(args[1], args[2]);
            if (args[0] == "--effect-states")
                return EffectStateReference.Run(args[1], args[2]);
            if (args[0] == "--effect-pixels")
                return EffectPixelReference.Run(args[1], args[2]);

            string outputPath = args[0];

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

            // Task 476: Viewport.Project/Unproject reference values, complementing Phase 40.
            JsonWriter viewport = ViewportReference.Generate();

            var root = new JsonWriter()
                .Add("metadata", ReferenceMetadata.Capture())
                .Add("fnaAssemblyVersion", typeof(Vector3).Assembly.GetName().Version.ToString())
                .Add("MathHelper", mathHelper)
                .Add("Color", colorConstants)
                .Add("NonRenderingApis", nonRenderingApis)
                .Add("PackedVector", packedVectors)
                .Add("Viewport", viewport)
                .Add("Vector2Edges", ValueEdgeReference.Vectors())
                .Add("Vector3Edges", ValueEdgeReference.Vectors3())
                .Add("Vector4Edges", ValueEdgeReference.Vectors4())
                .Add("QuaternionEdges", ValueEdgeReference.Quaternions())
                .Add("MatrixPerspectiveEdges", ValueEdgeReference.Projections());

            string json = root.ToString();
            File.WriteAllText(outputPath, json);

            Console.WriteLine("Wrote " + outputPath);
            Console.WriteLine(json);
            return 0;
        }
    }
}
