// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-234: how `MeshHelper.TransformScene` normalizes.
//
// The `.x` importer's own normalization is measured and wide -- the sum of squares and the square
// root in `double`, narrowed once, and the components *divided* by it (`XNASWEEP-170`). The
// processor normalizes again, through `MeshHelper.TransformScene`, and whether that one is the
// same shape is what separates a model whose normals are exact from one whose normals are a ulp
// out. This asks it directly: a mesh whose normals are known non-unit vectors, through
// `TransformScene` with the identity, printed as raw IEEE-754 bits.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content.Pipeline;
using Microsoft.Xna.Framework.Content.Pipeline.Graphics;

namespace Cna.Xna40.MeshNormalizeOracle
{
    internal static class Program
    {
        private static readonly List<string> Cases = new List<string>();

        private static string Bits(float value)
        {
            return "0x" + BitConverter.ToUInt32(BitConverter.GetBytes(value), 0)
                                      .ToString("X8", CultureInfo.InvariantCulture);
        }

        private static void Record(string name, string result)
        {
            Cases.Add("  {\"case\": \"" + name + "\", \"result\": \"" + result + "\"}");
        }

        /** A one-triangle mesh whose three normals are the vectors given. */
        private static MeshContent Mesh(Vector3[] normals)
        {
            var builder = MeshBuilder.StartMesh("probe");
            builder.CreateVertexChannel<Vector3>(VertexChannelNames.Normal());
            // Oblique on purpose: a triangle in a coordinate plane has an axis-aligned face
            // normal, which every normalization answers the same and which measures nothing.
            int a = builder.CreatePosition(0.0f, 0.0f, 0.0f);
            int b = builder.CreatePosition(0.855686f, 0.331f, 0.517496f);
            int c = builder.CreatePosition(-0.217f, 0.913f, 0.4013f);
            int[] corners = { a, b, c };
            for (int i = 0; i < 3; i++)
            {
                builder.SetVertexChannelData(0, normals[i]);
                builder.AddTriangleVertex(corners[i]);
            }
            return builder.FinishMesh();
        }

        private static int Main(string[] args)
        {
            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            string outputDirectory = args.Length > 0 ? args[0] : ".";
            Directory.CreateDirectory(outputDirectory);

            // Vectors whose length is not one, so the normalization is observable, and one that is
            // already unit to a ulp, which is the case a model's own normals are in.
            var probes = new List<Vector3[]>
            {
                new[] { new Vector3(0.855686f, 0.0f, 0.517496f),
                        new Vector3(0.801784f, 0.267261f, 0.534522f),
                        new Vector3(3.0f, 4.0f, 12.0f) },
                new[] { new Vector3(0.40100646f, 0.61088794f, -0.68286306f),
                        new Vector3(0.19023408f, 0.64794993f, -0.73754448f),
                        new Vector3(0.5460802f, -0.75476962f, 0.36269963f) },
                new[] { new Vector3(1e-4f, 2e-4f, 3e-4f),
                        new Vector3(1234.5f, -2345.6f, 3456.7f),
                        new Vector3(1.0f, 0.0f, 0.0f) },
            };
            for (int p = 0; p < probes.Count; p++)
            {
                Vector3[] normals = probes[p];
                var builder = new StringBuilder();
                for (int i = 0; i < normals.Length; i++)
                {
                    if (i > 0) { builder.Append(' '); }
                    builder.Append("in=(" + Bits(normals[i].X) + "," + Bits(normals[i].Y) + "," +
                                   Bits(normals[i].Z) + ")");
                }
                MeshContent mesh = Mesh(normals);
                var root = new NodeContent();
                root.Children.Add(mesh);
                MeshHelper.TransformScene(root, Matrix.Identity);
                foreach (GeometryContent geometry in mesh.Geometry)
                {
                    VertexChannel<Vector3> channel =
                        geometry.Vertices.Channels.Get<Vector3>(VertexChannelNames.Normal());
                    for (int i = 0; i < channel.Count; i++)
                    {
                        builder.Append(" out=(" + Bits(channel[i].X) + "," + Bits(channel[i].Y) +
                                       "," + Bits(channel[i].Z) + ")");
                    }
                }
                Record("transformscene/identity/" + p, builder.ToString());

                // ...and the same normals through `CalculateNormals`, which is the other place the
                // pipeline normalizes: a mesh with no normal channel at all, so the sums are the
                // face normals themselves.
                MeshContent bare = Mesh(normals);
                bare.Geometry[0].Vertices.Channels.Remove(VertexChannelNames.Normal());
                var bareRoot = new NodeContent();
                bareRoot.Children.Add(bare);
                MeshHelper.CalculateNormals(bare, true);
                var calculated = new StringBuilder();
                foreach (GeometryContent geometry in bare.Geometry)
                {
                    VertexChannel<Vector3> channel =
                        geometry.Vertices.Channels.Get<Vector3>(VertexChannelNames.Normal());
                    for (int i = 0; i < channel.Count; i++)
                    {
                        calculated.Append((calculated.Length > 0 ? " " : "") + "out=(" +
                                          Bits(channel[i].X) + "," + Bits(channel[i].Y) + "," +
                                          Bits(channel[i].Z) + ")");
                    }
                }
                Record("calculatenormals/" + p, calculated.ToString());
            }
            File.WriteAllText(Path.Combine(outputDirectory, "mesh-normalize-oracle.json"),
                              "{\n \"producer\": \"Microsoft XNA Game Studio 4.0 MeshHelper, driven by tools/xna-pipeline-oracle/model/MeshNormalizeOracle.cs\",\n \"cases\": [\n" +
                              string.Join(",\n", Cases.ToArray()) + "\n ]\n}\n");
            Console.WriteLine("recorded " + Cases.Count + " measurements");
            return 0;
        }
    }
}
