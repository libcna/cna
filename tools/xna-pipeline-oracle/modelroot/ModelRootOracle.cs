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
        // `ModelProcessor` converts every material through this, so throwing here fails the whole
        // process with `NotSupportedException` before a single bone is reached. What the root
        // transform is does not depend on what a material converts to, so the input is handed
        // straight back where the types allow and a default otherwise.
        public override TOutput Convert<TInput, TOutput>(TInput input, string processorName,
                                                         OpaqueDataDictionary processorParameters)
        {
            if (input is TOutput) { return (TOutput)(object)input; }
            return default(TOutput);
        }
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

    private static void DescribePositions(StringBuilder text, string file, NodeContent node,
                                         string path)
    {
        string here = path + "/" + (node.Name == null ? "<null>" : node.Name);
        MeshContent mesh = node as MeshContent;
        if (mesh != null)
        {
            var parts = new List<string>();
            foreach (Vector3 p in mesh.Positions)
            {
                parts.Add("(" + F(p.X) + "," + F(p.Y) + "," + F(p.Z) + ")");
            }
            text.Append(file + "|positions " + here + " " + mesh.Positions.Count + " " +
                        string.Join(" ", parts.ToArray()) + "\n");
            foreach (GeometryContent geometry in mesh.Geometry)
            {
                var indices = new List<string>();
                foreach (int i in geometry.Vertices.PositionIndices) { indices.Add(i.ToString(CultureInfo.InvariantCulture)); }
                text.Append(file + "|positionIndices " + here + " " +
                            string.Join(",", indices.ToArray()) + "\n");
            }
        }
        foreach (NodeContent child in node.Children) { DescribePositions(text, file, child, here); }
    }

    private static void DescribeNormals(StringBuilder text, string file, NodeContent node,
                                       string path)
    {
        string here = path + "/" + (node.Name == null ? "<null>" : node.Name);
        MeshContent mesh = node as MeshContent;
        if (mesh != null)
        {
            foreach (GeometryContent geometry in mesh.Geometry)
            {
                foreach (VertexChannel channel in geometry.Vertices.Channels)
                {
                    VertexChannel<Vector3> typed = channel as VertexChannel<Vector3>;
                    if (typed == null) { continue; }
                    var parts = new List<string>();
                    foreach (Vector3 one in typed)
                    {
                        parts.Add("(" + F(one.X) + "," + F(one.Y) + "," + F(one.Z) + ")");
                    }
                    text.Append(file + "|" + here + " " + channel.Name + " " +
                                string.Join(" ", parts.ToArray()) + "\n");
                }
            }
        }
        foreach (NodeContent child in node.Children) { DescribeNormals(text, file, child, here); }
    }

    private static int Main(string[] args)
    {
        string directory = args[0];
        using (StreamWriter writer = new StreamWriter(args[1]))
        {
            var found = new List<string>(Directory.GetFiles(directory, "*.fbx"));
            found.AddRange(Directory.GetFiles(directory, "*.x"));
            string[] files = found.ToArray();
            Array.Sort(files);
            foreach (string file in files)
            {
                string name = Path.GetFileName(file);
                try
                {
                    NodeContent root = file.EndsWith(".x", StringComparison.OrdinalIgnoreCase)
                        ? (NodeContent)new XImporter().Import(file, new Importing())
                        : new FbxImporter().Import(file, new Importing());
                    var text = new StringBuilder();
                    // `CNA_MODELROOT_TRANSFORM_SCENE=<sx>,<sy>,<sz>` runs `MeshHelper.TransformScene`
                    // over the imported graph with that scale and prints every normal channel
                    // afterwards, at round-trip precision. That is the one question a built `.xnb`
                    // cannot answer on its own: whether the method normalizes what it transforms
                    // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-167`).
                    string scene = Environment.GetEnvironmentVariable("CNA_MODELROOT_TRANSFORM_SCENE");
                    if (!string.IsNullOrEmpty(scene))
                    {
                        string[] parts = scene.Split(',');
                        Matrix transform = Matrix.CreateScale(
                            float.Parse(parts[0], CultureInfo.InvariantCulture),
                            float.Parse(parts[1], CultureInfo.InvariantCulture),
                            float.Parse(parts[2], CultureInfo.InvariantCulture));
                        MeshHelper.TransformScene(root, transform);
                        DescribeNormals(text, name, root, "");
                        writer.Write(text.ToString());
                        continue;
                    }
                    ModelProcessor processor = new ModelProcessor();
                    // `CNA_MODELROOT_PROCESSOR=RotationX=-90;Scale=0.1;DefaultEffect=...` sets the
                    // processor's own properties before it runs, because several of the corpus's
                    // models are built with them and what they do to a bone's transform is exactly
                    // what is being measured (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-172`).
                    string settings = Environment.GetEnvironmentVariable("CNA_MODELROOT_PROCESSOR");
                    if (!string.IsNullOrEmpty(settings))
                    {
                        foreach (string pair in settings.Split(';'))
                        {
                            if (pair.Length == 0) { continue; }
                            int split = pair.IndexOf('=');
                            string key = pair.Substring(0, split);
                            string value = pair.Substring(split + 1);
                            switch (key)
                            {
                                case "RotationX": processor.RotationX = float.Parse(value, CultureInfo.InvariantCulture); break;
                                case "RotationY": processor.RotationY = float.Parse(value, CultureInfo.InvariantCulture); break;
                                case "RotationZ": processor.RotationZ = float.Parse(value, CultureInfo.InvariantCulture); break;
                                case "Scale": processor.Scale = float.Parse(value, CultureInfo.InvariantCulture); break;
                                case "SwapWindingOrder": processor.SwapWindingOrder = bool.Parse(value); break;
                                case "GenerateTangentFrames": processor.GenerateTangentFrames = bool.Parse(value); break;
                                case "DefaultEffect":
                                    processor.DefaultEffect = (MaterialProcessorDefaultEffect)Enum.Parse(
                                        typeof(MaterialProcessorDefaultEffect), value);
                                    break;
                                default: throw new ArgumentException("unknown processor property " + key);
                            }
                        }
                    }
                    ModelContent model = processor.Process(root, new Processing());
                    // `CNA_MODELROOT_POSITIONS=1` prints every mesh's positions *after* the
                    // processor has run, because XNA's `ModelProcessor` mutates the graph it is
                    // given and the bounding sphere is computed over whatever the positions are by
                    // then (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-168`).
                    if (Environment.GetEnvironmentVariable("CNA_MODELROOT_POSITIONS") == "1")
                    {
                        DescribePositions(text, name, root, "");
                    }
                    // Every mesh's bounding sphere, at round-trip precision. What XNA computes it
                    // *over* is the open question, so the answer has to be read rather than
                    // assumed (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-168`).
                    for (int mi = 0; mi < model.Meshes.Count; mi++)
                    {
                        ModelMeshContent mesh = model.Meshes[mi];
                        text.Append(name + "|sphere " + mi + " " +
                                    (mesh.Name == null ? "<null>" : mesh.Name) + " centre=(" +
                                    F(mesh.BoundingSphere.Center.X) + "," +
                                    F(mesh.BoundingSphere.Center.Y) + "," +
                                    F(mesh.BoundingSphere.Center.Z) + ") radius=" +
                                    F(mesh.BoundingSphere.Radius) + " parts=" +
                                    mesh.MeshParts.Count + "\n");
                    }
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
