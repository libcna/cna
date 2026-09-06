// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-216, XNAPP-220: black-box behaviour oracle for the two
// modelling importers, XImporter and FbxImporter, over the corpus in tests/assets/xna40/model.
//
// It runs the genuine assemblies and records what they DO. The NodeContent graph each answers is
// walked and described in full -- names, transforms, geometry, every vertex channel and its
// values, materials and their textures, bone weights, and every animation channel and keyframe --
// because a modelling importer that gets the shape right and the values wrong is the failure mode
// a summary would hide.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content.Pipeline;
using Microsoft.Xna.Framework.Content.Pipeline.Graphics;

namespace Cna.Xna40.ModelOracle
{
    internal static class Program
    {
        private static readonly List<string> Cases = new List<string>();
        private static string OutputDirectory = ".";
        private static string FixtureDirectory = ".";

        private static string Escape(string text)
        {
            return text.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\r", "\\r").Replace("\n", "\\n");
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
            Publish();
        }

        private static void Publish()
        {
            File.WriteAllText(Path.Combine(OutputDirectory, "model-import-oracle.json"), Document());
        }

        private static string Document()
        {
            return "{\n \"producer\": \"Microsoft XNA Game Studio 4.0 Content Pipeline (XImporter, FBXImporter), driven by tools/xna-pipeline-oracle/model/ModelImportOracle.cs\",\n \"runtime\": \"" +
                   Environment.Version + "\",\n \"cases\": [\n" + string.Join(",\n", Cases.ToArray()) + "\n ]\n}\n";
        }

        private static string F(float value)
        {
            return value.ToString("0.######", CultureInfo.InvariantCulture);
        }

        private static string Describe(Matrix m)
        {
            return "[" + F(m.M11) + " " + F(m.M12) + " " + F(m.M13) + " " + F(m.M14) + " " +
                   F(m.M21) + " " + F(m.M22) + " " + F(m.M23) + " " + F(m.M24) + " " +
                   F(m.M31) + " " + F(m.M32) + " " + F(m.M33) + " " + F(m.M34) + " " +
                   F(m.M41) + " " + F(m.M42) + " " + F(m.M43) + " " + F(m.M44) + "]";
        }

        private static string DescribeChannelValue(object value)
        {
            if (value is Vector2) { Vector2 v = (Vector2)value; return "(" + F(v.X) + "," + F(v.Y) + ")"; }
            if (value is Vector3) { Vector3 v = (Vector3)value; return "(" + F(v.X) + "," + F(v.Y) + "," + F(v.Z) + ")"; }
            if (value is Vector4) { Vector4 v = (Vector4)value; return "(" + F(v.X) + "," + F(v.Y) + "," + F(v.Z) + "," + F(v.W) + ")"; }
            if (value is Color) { Color c = (Color)value; return "#" + c.PackedValue.ToString("X8"); }
            if (value is BoneWeightCollection)
            {
                var weights = (BoneWeightCollection)value;
                var parts = new List<string>();
                foreach (BoneWeight weight in weights)
                {
                    parts.Add(weight.BoneName + ":" + F(weight.Weight));
                }
                return "{" + string.Join(",", parts.ToArray()) + "}";
            }
            return value == null ? "null" : value.ToString();
        }

        private static void Describe(StringBuilder builder, NodeContent node, string path)
        {
            string here = path + "/" + (node.Name ?? "<null>");
            builder.Append(here + " type=" + node.GetType().Name +
                           " transform=" + Describe(node.Transform) +
                           " absolute=" + Describe(node.AbsoluteTransform) +
                           " children=" + node.Children.Count +
                           " animations=" + node.Animations.Count +
                           " opaque=" + node.OpaqueData.Count + "\n");
            foreach (KeyValuePair<string, AnimationContent> animation in node.Animations)
            {
                builder.Append("  animation " + animation.Key + " duration=" +
                               animation.Value.Duration.Ticks.ToString(CultureInfo.InvariantCulture) +
                               " channels=" + animation.Value.Channels.Count + "\n");
                foreach (KeyValuePair<string, AnimationChannel> channel in animation.Value.Channels)
                {
                    builder.Append("   channel " + channel.Key + " keys=" + channel.Value.Count + "\n");
                    foreach (AnimationKeyframe key in channel.Value)
                    {
                        builder.Append("    key t=" + key.Time.Ticks.ToString(CultureInfo.InvariantCulture) +
                                       " " + Describe(key.Transform) + "\n");
                    }
                }
            }
            MeshContent mesh = node as MeshContent;
            if (mesh != null)
            {
                builder.Append("  mesh positions=" + mesh.Positions.Count + " geometry=" +
                               mesh.Geometry.Count + "\n");
                for (int i = 0; i < mesh.Positions.Count; i++)
                {
                    builder.Append("   position " + i + " " + DescribeChannelValue(mesh.Positions[i]) + "\n");
                }
                foreach (GeometryContent geometry in mesh.Geometry)
                {
                    builder.Append("   geometry name=" + (geometry.Name ?? "<null>") +
                                   " indices=" + geometry.Indices.Count +
                                   " vertices=" + geometry.Vertices.VertexCount +
                                   " channels=" + geometry.Vertices.Channels.Count +
                                   " material=" + (geometry.Material == null
                                                       ? "null"
                                                       : geometry.Material.GetType().Name + ":" +
                                                             (geometry.Material.Name ?? "<null>")) + "\n");
                    var indices = new List<string>();
                    foreach (int index in geometry.Indices) { indices.Add(index.ToString(CultureInfo.InvariantCulture)); }
                    builder.Append("    indices " + string.Join(",", indices.ToArray()) + "\n");
                    var positionIndices = new List<string>();
                    foreach (int index in geometry.Vertices.PositionIndices)
                    { positionIndices.Add(index.ToString(CultureInfo.InvariantCulture)); }
                    builder.Append("    positionIndices " + string.Join(",", positionIndices.ToArray()) + "\n");
                    foreach (VertexChannel channel in geometry.Vertices.Channels)
                    {
                        var values = new List<string>();
                        foreach (object value in channel) { values.Add(DescribeChannelValue(value)); }
                        builder.Append("    channel " + channel.Name + " type=" +
                                       channel.ElementType.Name + " " +
                                       string.Join(" ", values.ToArray()) + "\n");
                    }
                    if (geometry.Material != null)
                    {
                        foreach (KeyValuePair<string, object> entry in geometry.Material.OpaqueData)
                        {
                            builder.Append("    materialData " + entry.Key + "=" +
                                           DescribeChannelValue(entry.Value) + "\n");
                        }
                        foreach (KeyValuePair<string, ExternalReference<TextureContent>> texture
                                     in geometry.Material.Textures)
                        {
                            builder.Append("    materialTexture " + texture.Key + "=" +
                                           Path.GetFileName(texture.Value.Filename) + "\n");
                        }
                    }
                }
            }
            foreach (NodeContent child in node.Children)
            {
                Describe(builder, child, here);
            }
        }

        private sealed class ProbeLogger : ContentBuildLogger
        {
            public readonly List<string> Lines = new List<string>();
            public override void LogImportantMessage(string message, params object[] args) { Lines.Add(message); }
            public override void LogMessage(string message, params object[] args) { Lines.Add(message); }
            public override void LogWarning(string helpLink, ContentIdentity identity, string message, params object[] args)
            { Lines.Add("warning: " + message); }
        }

        private sealed class ProbeImporterContext : ContentImporterContext
        {
            private readonly ProbeLogger logger = new ProbeLogger();
            public readonly List<string> Dependencies = new List<string>();
            public override string IntermediateDirectory { get { return "obj"; } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return "bin"; } }
            public override void AddDependency(string filename) { Dependencies.Add(Path.GetFileName(filename)); }
            public string Report
            {
                get
                {
                    return "dependencies=[" + string.Join(",", Dependencies.ToArray()) + "] log=[" +
                           string.Join(" | ", logger.Lines.ToArray()) + "]";
                }
            }
        }

        private static int Main(string[] args)
        {
            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            OutputDirectory = args.Length > 0 ? args[0] : ".";
            FixtureDirectory = args.Length > 1 ? args[1] : ".";
            Directory.CreateDirectory(OutputDirectory);

            Record("attribute/x", delegate { return DescribeAttribute(typeof(XImporter)); });
            Record("attribute/fbx", delegate { return DescribeAttribute(typeof(FbxImporter)); });

            string[] xFiles = Directory.GetFiles(FixtureDirectory, "*.x");
            Array.Sort(xFiles, StringComparer.Ordinal);
            foreach (string fixture in xFiles)
            {
                string captured = fixture;
                Record("x/" + Path.GetFileName(captured), delegate
                {
                    var context = new ProbeImporterContext();
                    using (var importer = new XImporter())
                    {
                        NodeContent root = importer.Import(captured, context);
                        if (root == null) { return "IMPORT RETURNED null " + context.Report; }
                        var builder = new StringBuilder();
                        Describe(builder, root, "");
                        return builder.ToString() + context.Report;
                    }
                });
            }
            Record("x/missing.x", delegate
            {
                using (var importer = new XImporter())
                {
                    NodeContent root = importer.Import(Path.Combine(FixtureDirectory, "no_such_model.x"),
                                                       new ProbeImporterContext());
                    return root == null ? "null" : "returned";
                }
            });
            Record("x/dispose_twice", delegate
            {
                var importer = new XImporter();
                importer.Dispose();
                importer.Dispose();
                return "accepted";
            });
            Record("x/import_after_dispose", delegate
            {
                var importer = new XImporter();
                importer.Dispose();
                NodeContent root = importer.Import(Path.Combine(FixtureDirectory, "bare_mesh.x"),
                                                   new ProbeImporterContext());
                return root == null ? "null" : "returned " + (root.Name ?? "<null>");
            });
            Record("fbx/missing.fbx", delegate
            {
                NodeContent root = new FbxImporter().Import(Path.Combine(FixtureDirectory, "no_such_model.fbx"),
                                                            new ProbeImporterContext());
                return root == null ? "null" : "returned";
            });
            Record("fbx/an_x_file", delegate
            {
                NodeContent root = new FbxImporter().Import(Path.Combine(FixtureDirectory, "bare_mesh.x"),
                                                            new ProbeImporterContext());
                return root == null ? "null" : "returned";
            });
            string[] fbxFiles = Directory.GetFiles(FixtureDirectory, "*.fbx");
            Array.Sort(fbxFiles, StringComparer.Ordinal);
            foreach (string fixture in fbxFiles)
            {
                string captured = fixture;
                Record("fbx/" + Path.GetFileName(captured), delegate
                {
                    var context = new ProbeImporterContext();
                    NodeContent root = new FbxImporter().Import(captured, context);
                    if (root == null) { return "IMPORT RETURNED null " + context.Report; }
                    var builder = new StringBuilder();
                    Describe(builder, root, "");
                    return builder.ToString() + context.Report;
                });
            }

            Publish();
            Console.WriteLine("recorded " + Cases.Count + " measurements");
            return 0;
        }

        private static string DescribeAttribute(Type importer)
        {
            var parts = new List<string>();
            foreach (object attribute in importer.GetCustomAttributes(typeof(ContentImporterAttribute), true))
            {
                var a = (ContentImporterAttribute)attribute;
                parts.Add("extensions=[" + string.Join(",", new List<string>(a.FileExtensions).ToArray()) +
                          "] displayName=" + a.DisplayName + " defaultProcessor=" + a.DefaultProcessor +
                          " cacheImportedData=" + a.CacheImportedData);
            }
            return string.Join(" ; ", parts.ToArray());
        }
    }
}
