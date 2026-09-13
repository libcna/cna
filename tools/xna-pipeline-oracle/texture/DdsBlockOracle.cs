// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-224: what the genuine `TextureImporter` answers for a
// block-compressed `.dds` whose dimensions are not a whole number of blocks.
//
// The DDS files are written here, correctly: a level of w by h pixels holds ceil(w/4) * ceil(h/4)
// blocks, which is what a real encoder writes and what `dwPitchOrLinearSize` describes. Each block
// carries a distinguishable payload so the *placement* of the data in the answered bitmap can be
// read as well as its size.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using Microsoft.Xna.Framework.Content.Pipeline;
using Microsoft.Xna.Framework.Content.Pipeline.Graphics;

namespace Cna.Xna40.DdsBlockOracle
{
    internal static class Program
    {
        private static readonly List<string> Cases = new List<string>();
        private static string OutputDirectory = ".";

        private static string Escape(string text)
        {
            return text.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\r", "\\r").Replace("\n", "\\n");
        }

        private static void Record(string name, Func<string> measurement)
        {
            try { Cases.Add("  {\"case\": \"" + name + "\", \"result\": \"" + Escape(measurement()) + "\"}"); }
            catch (Exception error)
            {
                Cases.Add("  {\"case\": \"" + name + "\", \"result\": \"throws " +
                          error.GetType().Name + ": " + Escape(error.Message) + "\"}");
            }
        }

        private sealed class ProbeLogger : ContentBuildLogger
        {
            public override void LogImportantMessage(string message, params object[] args) { }
            public override void LogMessage(string message, params object[] args) { }
            public override void LogWarning(string helpLink, ContentIdentity identity, string message, params object[] args) { }
        }

        private sealed class ProbeImporterContext : ContentImporterContext
        {
            private readonly ProbeLogger logger = new ProbeLogger();
            public override string IntermediateDirectory { get { return "obj"; } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return "bin"; } }
            public override void AddDependency(string filename) { }
        }

        /** The committed corpus of tests/assets/xna40/texture. */
        private static string fixtureDirectory = ".";

        private static string Describe(TextureContent texture)
        {
            var builder = new StringBuilder(texture.GetType().Name + " faces=" + texture.Faces.Count);
            for (int face = 0; face < texture.Faces.Count; face++)
            {
                builder.Append(" [");
                for (int level = 0; level < texture.Faces[face].Count; level++)
                {
                    BitmapContent bitmap = texture.Faces[face][level];
                    if (level > 0) { builder.Append(' '); }
                    Microsoft.Xna.Framework.Graphics.SurfaceFormat format;
                    bool known = bitmap.TryGetFormat(out format);
                    builder.Append(bitmap.Width + "x" + bitmap.Height + ":" +
                                   (known ? format.ToString() : "none") + ":" +
                                   bitmap.GetPixelData().Length);
                }
                builder.Append(']');
            }
            return builder.ToString();
        }

        private static string Hex(byte[] data, int limit)
        {
            var builder = new StringBuilder();
            for (int i = 0; i < Math.Min(limit, data.Length); i++)
            {
                builder.Append(data[i].ToString("X2", CultureInfo.InvariantCulture));
            }
            if (data.Length > limit) { builder.Append("..."); }
            return builder.ToString();
        }

        private static int Main(string[] args)
        {
            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            OutputDirectory = args.Length > 0 ? args[0] : ".";
            fixtureDirectory = args.Length > 1 ? args[1] : ".";
            Directory.CreateDirectory(OutputDirectory);
            foreach (string fixture in new string[] {
                "dds_blocks_dxt1_5x5.dds", "dds_blocks_dxt1_9x9.dds", "dds_blocks_dxt1_17x3.dds",
                "dds_blocks_dxt3_5x5.dds", "dds_blocks_dxt3_9x9.dds",
                "dds_blocks_dxt5_5x5.dds", "dds_blocks_dxt5_17x3.dds",
                "dds_blocks_dxt1_1x1.dds", "dds_blocks_dxt1_22x22.dds",
                "dds_blocks_dxt1_6x10.dds", "dds_blocks_dxt1_9x9_mips.dds" })
            {
                string captured = fixture;
                Record("dds/" + captured, delegate
                {
                    TextureContent texture = new TextureImporter().Import(
                        Path.Combine(fixtureDirectory, captured), new ProbeImporterContext());
                    var builder = new StringBuilder(Describe(texture));
                    for (int level = 0; level < texture.Faces[0].Count; level++)
                    {
                        builder.Append(" level" + level + "=" +
                                       Hex(texture.Faces[0][level].GetPixelData(), 4096));
                    }
                    return builder.ToString();
                });
            }
            File.WriteAllText(Path.Combine(OutputDirectory, "dds-block-oracle.json"),
                              "{\n \"producer\": \"Microsoft XNA Game Studio 4.0 TextureImporter, driven by tools/xna-pipeline-oracle/texture/DdsBlockOracle.cs\",\n \"cases\": [\n" +
                              string.Join(",\n", Cases.ToArray()) + "\n ]\n}\n");
            Console.WriteLine("recorded " + Cases.Count + " measurements");
            return 0;
        }
    }
}
