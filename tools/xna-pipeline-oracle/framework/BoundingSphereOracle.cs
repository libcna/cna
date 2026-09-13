// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-134: black-box behaviour oracle for XNA 4.0's
// BoundingSphere.CreateFromPoints, at the bit.
//
// The corpus's model references disagreed with CNA's on a mesh's bounding-sphere centre by a few
// parts in ten million, on 123 of them, and a difference that small can only be settled by
// comparing the exact bits over point sets that separate the candidate rules. This runs the
// genuine assembly over the sets tests/assets/xna40/framework/bounding-sphere-points.txt names and
// records what it answers, in hexadecimal so nothing is lost to formatting.
//
// It runs the genuine assembly and records what it DOES; nothing here inspects XNA's IL. No
// Direct3D device is created, so no display is needed.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using Microsoft.Xna.Framework;

namespace Cna.Xna40.BoundingSphereOracle
{
    internal static class Program
    {
        private static float Read(string word)
        {
            uint bits = uint.Parse(word, NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            return BitConverter.ToSingle(BitConverter.GetBytes(bits), 0);
        }

        private static string Write(float value)
        {
            return BitConverter.ToUInt32(BitConverter.GetBytes(value), 0)
                               .ToString("X8", CultureInfo.InvariantCulture);
        }

        private static void Emit(StringBuilder output, string name, List<Vector3> points)
        {
            if (name == null)
            {
                return;
            }
            output.Append("case ").Append(name).Append('\n');
            foreach (Vector3 point in points)
            {
                output.Append("point ").Append(Write(point.X)).Append(' ')
                      .Append(Write(point.Y)).Append(' ').Append(Write(point.Z)).Append('\n');
            }
            BoundingSphere sphere = BoundingSphere.CreateFromPoints(points);
            output.Append("result ").Append(Write(sphere.Center.X)).Append(' ')
                  .Append(Write(sphere.Center.Y)).Append(' ').Append(Write(sphere.Center.Z))
                  .Append(' ').Append(Write(sphere.Radius)).Append('\n');
        }

        public static int Main(string[] args)
        {
            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            string input = args[0];
            string output = args[1];
            var document = new StringBuilder();
            document.Append("# XNA 4.0 BoundingSphere.CreateFromPoints, measured\n");
            document.Append("# producer: tools/xna-pipeline-oracle/framework/BoundingSphereOracle.cs\n");
            document.Append("# points:   tests/assets/xna40/framework/bounding-sphere-points.txt\n");
            document.Append("# every value is the IEEE-754 single-precision bit pattern, big-endian hex\n");
            string name = null;
            var points = new List<Vector3>();
            foreach (string raw in File.ReadAllLines(input))
            {
                string line = raw.Trim();
                if (line.Length == 0 || line.StartsWith("#", StringComparison.Ordinal))
                {
                    continue;
                }
                if (line.StartsWith("case ", StringComparison.Ordinal))
                {
                    Emit(document, name, points);
                    name = line.Substring(5).Trim();
                    points = new List<Vector3>();
                    continue;
                }
                if (line.StartsWith("point ", StringComparison.Ordinal))
                {
                    string[] parts = line.Substring(6).Trim().Split(' ');
                    points.Add(new Vector3(Read(parts[0]), Read(parts[1]), Read(parts[2])));
                }
            }
            Emit(document, name, points);
            File.WriteAllText(output, document.ToString());
            return 0;
        }
    }
}
