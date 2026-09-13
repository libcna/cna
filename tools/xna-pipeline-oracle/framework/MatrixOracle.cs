// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-172: black-box behaviour oracle for XNA 4.0's
// Matrix.Multiply and Matrix.CreateRotation*, at the bit.
//
// A bone's transform in a built model is a product of matrices, and sixteen of the corpus's model
// references disagree with CNA's only in entries a rotation puts near zero -- 3.5e-15 where CNA
// writes 0, 1.2e-16 where CNA writes 0. Numbers that small are decided by where the arithmetic
// rounds, so the only way to settle it is to hand the genuine assembly matrix pairs whose product
// separates the candidates and record the exact bits it answers.
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

namespace Cna.Xna40.MatrixOracle
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

        private static Matrix ReadMatrix(string[] words, int at)
        {
            return new Matrix(Read(words[at + 0]), Read(words[at + 1]), Read(words[at + 2]), Read(words[at + 3]),
                              Read(words[at + 4]), Read(words[at + 5]), Read(words[at + 6]), Read(words[at + 7]),
                              Read(words[at + 8]), Read(words[at + 9]), Read(words[at + 10]), Read(words[at + 11]),
                              Read(words[at + 12]), Read(words[at + 13]), Read(words[at + 14]), Read(words[at + 15]));
        }

        private static string WriteMatrix(Matrix value)
        {
            var text = new StringBuilder();
            float[] entries = {
                value.M11, value.M12, value.M13, value.M14,
                value.M21, value.M22, value.M23, value.M24,
                value.M31, value.M32, value.M33, value.M34,
                value.M41, value.M42, value.M43, value.M44 };
            for (int i = 0; i < entries.Length; i++)
            {
                if (i != 0) { text.Append(' '); }
                text.Append(Write(entries[i]));
            }
            return text.ToString();
        }

        public static int Main(string[] args)
        {
            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            var output = new StringBuilder();
            string name = null;
            string op = null;
            var operands = new List<string>();
            foreach (string raw in File.ReadAllLines(args[0]))
            {
                string line = raw.Trim();
                if (line.Length == 0) { continue; }
                string[] words = line.Split(' ');
                if (words[0] == "case")
                {
                    Emit(output, name, op, operands);
                    name = words[1];
                    op = null;
                    operands.Clear();
                }
                else if (words[0] == "op")
                {
                    op = words[1];
                }
                else
                {
                    for (int i = 1; i < words.Length; i++) { operands.Add(words[i]); }
                }
            }
            Emit(output, name, op, operands);
            File.WriteAllText(args[1], output.ToString());
            return 0;
        }

        private static void Emit(StringBuilder output, string name, string op, List<string> operands)
        {
            if (name == null) { return; }
            output.Append("case ").Append(name).Append('\n');
            output.Append("op ").Append(op).Append('\n');
            string[] words = operands.ToArray();
            Matrix answer;
            switch (op)
            {
                case "multiply":
                    Matrix a = ReadMatrix(words, 0);
                    Matrix b = ReadMatrix(words, 16);
                    output.Append("a ").Append(WriteMatrix(a)).Append('\n');
                    output.Append("b ").Append(WriteMatrix(b)).Append('\n');
                    answer = Matrix.Multiply(a, b);
                    break;
                case "rotationx":
                case "rotationy":
                case "rotationz":
                    float radians = Read(words[0]);
                    output.Append("angle ").Append(Write(radians)).Append('\n');
                    answer = op == "rotationx" ? Matrix.CreateRotationX(radians)
                           : op == "rotationy" ? Matrix.CreateRotationY(radians)
                                               : Matrix.CreateRotationZ(radians);
                    break;
                case "invert":
                    Matrix source = ReadMatrix(words, 0);
                    output.Append("a ").Append(WriteMatrix(source)).Append('\n');
                    answer = Matrix.Invert(source);
                    break;
                case "transform3":
                case "transformnormal3":
                    Vector3 vector = new Vector3(Read(words[0]), Read(words[1]), Read(words[2]));
                    Matrix by = ReadMatrix(words, 16);
                    output.Append("v ").Append(Write(vector.X)).Append(' ').Append(Write(vector.Y))
                          .Append(' ').Append(Write(vector.Z)).Append('\n');
                    output.Append("b ").Append(WriteMatrix(by)).Append('\n');
                    Vector3 transformed = op == "transform3"
                        ? Vector3.Transform(vector, by)
                        : Vector3.TransformNormal(vector, by);
                    output.Append("result ").Append(Write(transformed.X)).Append(' ')
                          .Append(Write(transformed.Y)).Append(' ').Append(Write(transformed.Z))
                          .Append('\n');
                    return;
                case "toradians":
                    float degrees = Read(words[0]);
                    output.Append("degrees ").Append(Write(degrees)).Append('\n');
                    output.Append("result ").Append(Write(MathHelper.ToRadians(degrees))).Append('\n');
                    return;
                case "todegrees":
                    float radians2 = Read(words[0]);
                    output.Append("degrees ").Append(Write(radians2)).Append('\n');
                    output.Append("result ").Append(Write(MathHelper.ToDegrees(radians2))).Append('\n');
                    return;
                default:
                    throw new ArgumentException("unknown op " + op);
            }
            output.Append("result ").Append(WriteMatrix(answer)).Append('\n');
        }
    }
}
