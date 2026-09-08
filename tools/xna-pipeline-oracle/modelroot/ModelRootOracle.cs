// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-162: what the genuine ModelProcessor leaves on a
// model's root bone. The importer's answer is measured elsewhere; this runs the processor over the
// same files and prints every bone's transform at full precision, which the model oracle's
// `0.######` formatting cannot show.
//
//   ModelRootOracle.exe <fixture directory> <out.txt>

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content.Pipeline;
using Microsoft.Xna.Framework.Content.Pipeline.Graphics;
using Microsoft.Xna.Framework.Content.Pipeline.Processors;
using Microsoft.Xna.Framework.Graphics;

internal static class ModelRootOracle
{
    private sealed class Logger : ContentBuildLogger
    {
        public readonly List<string> Lines = new List<string>();
        public override void LogMessage(string message, params object[] args) { }
        public override void LogImportantMessage(string message, params object[] args) { }
        public override void LogWarning(string helpLink, ContentIdentity identity, string message,
                                        params object[] args)
        { Lines.Add("warning: " + message); }
    }

    private sealed class Importing : ContentImporterContext
    {
        private readonly Logger logger = new Logger();
        public override string IntermediateDirectory { get { return "obj"; } }
        public override ContentBuildLogger Logger { get { return logger; } }
        public override string OutputDirectory { get { return "bin"; } }
        public override void AddDependency(string filename) { }
    }

    private sealed class Processing : ContentProcessorContext
    {
        private readonly Logger logger = new Logger();
        public override string BuildConfiguration { get { return "Debug"; } }
        public override string IntermediateDirectory { get { return "obj"; } }
        public override ContentBuildLogger Logger { get { return logger; } }
        public override string OutputDirectory { get { return "bin"; } }
        public override string OutputFilename { get { return "out.xnb"; } }
        public override OpaqueDataDictionary Parameters { get { return new OpaqueDataDictionary(); } }
        public override TargetPlatform TargetPlatform { get { return TargetPlatform.Windows; } }
        public override GraphicsProfile TargetProfile { get { return GraphicsProfile.HiDef; } }
        public override void AddDependency(string filename) { }
        public override void AddOutputFile(string filename) { }
        public override TOutput Convert<TInput, TOutput>(TInput input, string processorName,
                                                         OpaqueDataDictionary processorParameters)
        { throw new NotSupportedException(); }
        public override TOutput BuildAndLoadAsset<TInput, TOutput>(ExternalReference<TInput> source,
            string processorName, OpaqueDataDictionary processorParameters, string importerName)
        { return default(TOutput); }
        public override ExternalReference<TOutput> BuildAsset<TInput, TOutput>(
            ExternalReference<TInput> source, string processorName,
            OpaqueDataDictionary processorParameters, string importerName, string assetName)
        { return new ExternalReference<TOutput>(source.Filename); }
    }

    private static string F(float value)
    {
        return value.ToString("R", CultureInfo.InvariantCulture);
    }

    private static string Describe(Matrix m)
    {
        float[] v = { m.M11, m.M12, m.M13, m.M14, m.M21, m.M22, m.M23, m.M24,
                      m.M31, m.M32, m.M33, m.M34, m.M41, m.M42, m.M43, m.M44 };
        var parts = new List<string>();
        foreach (float one in v) parts.Add(F(one));
        return "[" + string.Join(" ", parts.ToArray()) + "]";
    }

    private static int Main(string[] args)
    {
        string directory = args[0];
        using (StreamWriter writer = new StreamWriter(args[1]))
        {
            string[] files = Directory.GetFiles(directory, "*.fbx");
            Array.Sort(files);
            foreach (string file in files)
            {
                string name = Path.GetFileName(file);
                try
                {
                    NodeContent root = new FbxImporter().Import(file, new Importing());
                    ModelContent model = new ModelProcessor().Process(root, new Processing());
                    var text = new StringBuilder();
                    foreach (ModelBoneContent bone in model.Bones)
                    {
                        text.Append(name + "|bone " + bone.Index + " " +
                                    (bone.Name == null ? "<null>" : bone.Name) + " " +
                                    Describe(bone.Transform) + "\n");
                    }
                    writer.Write(text.ToString());
                }
                catch (Exception error)
                {
                    writer.WriteLine(name + "|ERROR " + error.GetType().Name + ": " +
                                     error.Message.Replace('\n', ' '));
                }
            }
        }
        return 0;
    }
}
