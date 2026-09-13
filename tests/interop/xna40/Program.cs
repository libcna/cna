// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-32/XNAP-33: the Microsoft XNA Game Studio 4.0 interoperability
// harness for CNA-generated .xnb files.
//
// WHAT THIS IS FOR
//
// CNA writes .xnb files. CNA can also read them back, and an independent Python parser validates
// them against the format specification. Neither of those is the same as a genuine Microsoft XNA
// 4.0 runtime accepting them, and no CNA test can be. This program closes that gap on a machine
// that has XNA installed: it loads every fixture in the committed corpus through the real
// ContentManager and asserts the values each fixture's expectation manifest declares.
//
// It has never been executed by the CNA repository's own automation, because no environment CNA
// builds in has an XNA 4.0 runtime. Until someone runs it and records the result, every claim
// about XNA compatibility in CNA's documentation must say "not verified against Microsoft XNA".
//
// See README.md in this directory for how to build and run it.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Audio;
using Microsoft.Xna.Framework.Content;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Media;

namespace Cna.Xna40.Interop
{
    /// <summary>A minimal JSON reader, so the harness needs no package references at all.</summary>
    internal static class Json
    {
        public static object Parse(string text)
        {
            int index = 0;
            object value = ParseValue(text, ref index);
            SkipWhitespace(text, ref index);
            if (index != text.Length)
            {
                throw new FormatException("trailing characters after the JSON document");
            }
            return value;
        }

        private static void SkipWhitespace(string text, ref int index)
        {
            while (index < text.Length && char.IsWhiteSpace(text[index])) { index++; }
        }

        private static object ParseValue(string text, ref int index)
        {
            SkipWhitespace(text, ref index);
            if (index >= text.Length) { throw new FormatException("unexpected end of JSON"); }
            char c = text[index];
            if (c == '{') { return ParseObject(text, ref index); }
            if (c == '[') { return ParseArray(text, ref index); }
            if (c == '"') { return ParseString(text, ref index); }
            if (text.Substring(index).StartsWith("true", StringComparison.Ordinal))
            {
                index += 4;
                return true;
            }
            if (text.Substring(index).StartsWith("false", StringComparison.Ordinal))
            {
                index += 5;
                return false;
            }
            if (text.Substring(index).StartsWith("null", StringComparison.Ordinal))
            {
                index += 4;
                return null;
            }
            return ParseNumber(text, ref index);
        }

        private static Dictionary<string, object> ParseObject(string text, ref int index)
        {
            var result = new Dictionary<string, object>();
            index++;
            SkipWhitespace(text, ref index);
            if (index < text.Length && text[index] == '}') { index++; return result; }
            while (true)
            {
                SkipWhitespace(text, ref index);
                string key = ParseString(text, ref index);
                SkipWhitespace(text, ref index);
                if (text[index] != ':') { throw new FormatException("expected ':'"); }
                index++;
                result[key] = ParseValue(text, ref index);
                SkipWhitespace(text, ref index);
                if (text[index] == ',') { index++; continue; }
                if (text[index] == '}') { index++; return result; }
                throw new FormatException("expected ',' or '}'");
            }
        }

        private static List<object> ParseArray(string text, ref int index)
        {
            var result = new List<object>();
            index++;
            SkipWhitespace(text, ref index);
            if (index < text.Length && text[index] == ']') { index++; return result; }
            while (true)
            {
                result.Add(ParseValue(text, ref index));
                SkipWhitespace(text, ref index);
                if (text[index] == ',') { index++; continue; }
                if (text[index] == ']') { index++; return result; }
                throw new FormatException("expected ',' or ']'");
            }
        }

        private static string ParseString(string text, ref int index)
        {
            if (text[index] != '"') { throw new FormatException("expected a string"); }
            index++;
            var builder = new StringBuilder();
            while (text[index] != '"')
            {
                if (text[index] == '\\')
                {
                    index++;
                    switch (text[index])
                    {
                        case 'n': builder.Append('\n'); break;
                        case 't': builder.Append('\t'); break;
                        case 'r': builder.Append('\r'); break;
                        case 'b': builder.Append('\b'); break;
                        case 'f': builder.Append('\f'); break;
                        case '/': builder.Append('/'); break;
                        case '\\': builder.Append('\\'); break;
                        case '"': builder.Append('"'); break;
                        case 'u':
                            builder.Append((char)Convert.ToInt32(
                                text.Substring(index + 1, 4), 16));
                            index += 4;
                            break;
                        default: throw new FormatException("unknown escape");
                    }
                }
                else
                {
                    builder.Append(text[index]);
                }
                index++;
            }
            index++;
            return builder.ToString();
        }

        private static double ParseNumber(string text, ref int index)
        {
            int start = index;
            while (index < text.Length && "-+.eE0123456789".IndexOf(text[index]) >= 0) { index++; }
            return double.Parse(text.Substring(start, index - start),
                                CultureInfo.InvariantCulture);
        }
    }

    /// <summary>Collects one fixture's outcome so the summary can be exact.</summary>
    internal sealed class Outcome
    {
        public string Name;
        public string Status;
        public List<string> Problems = new List<string>();
        public List<string> Skipped = new List<string>();
    }

    internal static class Program
    {
        private static Dictionary<string, object> Object(object value, string where)
        {
            var result = value as Dictionary<string, object>;
            if (result == null) { throw new FormatException(where + " is not a JSON object"); }
            return result;
        }

        private static double Number(Dictionary<string, object> parent, string key)
        {
            return Convert.ToDouble(parent[key], CultureInfo.InvariantCulture);
        }

        private static void Expect(Outcome outcome, string what, object expected, object actual)
        {
            if (!Equals(Convert.ToString(expected, CultureInfo.InvariantCulture),
                        Convert.ToString(actual, CultureInfo.InvariantCulture)))
            {
                outcome.Problems.Add(what + ": expected " + expected + ", observed " + actual);
            }
        }

        private static void CheckTexture2D(Outcome outcome, ContentManager content,
                                            string name, Dictionary<string, object> root)
        {
            Texture2D texture = content.Load<Texture2D>(name);
            Expect(outcome, "Width", Number(root, "width"), texture.Width);
            Expect(outcome, "Height", Number(root, "height"), texture.Height);
            Expect(outcome, "LevelCount", Number(root, "mipCount"), texture.LevelCount);
            Expect(outcome, "Format", root["surfaceFormat"], texture.Format.ToString());

            // Level 0's exact bytes: the strongest thing a runtime can be asked to agree on.
            var pixels = new Color[texture.Width * texture.Height];
            texture.GetData(pixels);
            var bytes = new byte[pixels.Length * 4];
            for (int index = 0; index < pixels.Length; index++)
            {
                bytes[index * 4 + 0] = pixels[index].R;
                bytes[index * 4 + 1] = pixels[index].G;
                bytes[index * 4 + 2] = pixels[index].B;
                bytes[index * 4 + 3] = pixels[index].A;
            }
            var digests = root["levelDigests"] as List<object>;
            if (digests != null && digests.Count > 0)
            {
                Expect(outcome, "level 0 payload digest", digests[0], Fnv1a(bytes));
            }
        }

        private static void CheckTextureCube(Outcome outcome, ContentManager content,
                                              string name, Dictionary<string, object> root)
        {
            TextureCube texture = content.Load<TextureCube>(name);
            Expect(outcome, "Size", Number(root, "size"), texture.Size);
            Expect(outcome, "LevelCount", Number(root, "mipCount"), texture.LevelCount);
            Expect(outcome, "Format", root["surfaceFormat"], texture.Format.ToString());

            // Each face is a different flat colour, which is the one shape that makes a runtime
            // reading the faces in another order say so rather than merely look wrong.
            var expected = root["faceFirstTexels"] as List<object>;
            if (expected == null || expected.Count != 6)
            {
                outcome.Problems.Add("the expectation names no six face colours");
                return;
            }
            CubeMapFace[] faces =
            {
                CubeMapFace.PositiveX, CubeMapFace.NegativeX, CubeMapFace.PositiveY,
                CubeMapFace.NegativeY, CubeMapFace.PositiveZ, CubeMapFace.NegativeZ,
            };
            for (int index = 0; index < 6; index++)
            {
                var pixels = new Color[texture.Size * texture.Size];
                texture.GetData(faces[index], pixels);
                var want = expected[index] as List<object>;
                Expect(outcome, faces[index] + " R", want[0], (int)pixels[0].R);
                Expect(outcome, faces[index] + " G", want[1], (int)pixels[0].G);
                Expect(outcome, faces[index] + " B", want[2], (int)pixels[0].B);
                Expect(outcome, faces[index] + " A", want[3], (int)pixels[0].A);
            }
        }

        private static void CheckTexture3D(Outcome outcome, ContentManager content,
                                            string name, Dictionary<string, object> root)
        {
            Texture3D texture = content.Load<Texture3D>(name);
            Expect(outcome, "Width", Number(root, "width"), texture.Width);
            Expect(outcome, "Height", Number(root, "height"), texture.Height);
            Expect(outcome, "Depth", Number(root, "depth"), texture.Depth);
            Expect(outcome, "LevelCount", Number(root, "mipCount"), texture.LevelCount);
            Expect(outcome, "Format", root["surfaceFormat"], texture.Format.ToString());

            // The two slices differ, so a runtime that collapsed the depth says so here.
            var pixels = new Color[texture.Width * texture.Height * texture.Depth];
            texture.GetData(pixels);
            var first = root["firstTexel"] as List<object>;
            var second = root["secondSliceFirstTexel"] as List<object>;
            int sliceStride = texture.Width * texture.Height;
            Expect(outcome, "slice 0 texel 0 R", first[0], (int)pixels[0].R);
            Expect(outcome, "slice 0 texel 0 G", first[1], (int)pixels[0].G);
            Expect(outcome, "slice 1 texel 0 R", second[0], (int)pixels[sliceStride].R);
            Expect(outcome, "slice 1 texel 0 G", second[1], (int)pixels[sliceStride].G);
        }

        private static void CheckEffect(Outcome outcome, ContentManager content,
                                         string name, Dictionary<string, object> root)
        {
            // Loading an Effect is the assertion: the runtime hands the bytes straight to
            // Direct3D, which either accepts them as a compiled effect or does not. Everything
            // after this is what the container says *about* them.
            Effect effect = content.Load<Effect>(name);
            Expect(outcome, "Techniques.Count", Number(root, "techniqueCount"),
                   effect.Techniques.Count);
            Expect(outcome, "Parameters.Count", Number(root, "parameterCount"),
                   effect.Parameters.Count);
            if (root.ContainsKey("techniqueName"))
            {
                Expect(outcome, "Techniques[0].Name", root["techniqueName"],
                       effect.Techniques[0].Name);
            }
            if (root.ContainsKey("passName"))
            {
                Expect(outcome, "Techniques[0].Passes[0].Name", root["passName"],
                       effect.Techniques[0].Passes[0].Name);
            }
            if (root.ContainsKey("parameterName"))
            {
                Expect(outcome, "Parameters[0].Name", root["parameterName"],
                       effect.Parameters[0].Name);
            }
        }

        private static void CheckSong(Outcome outcome, ContentManager content,
                                       string name, Dictionary<string, object> root)
        {
            Song song = content.Load<Song>(name);
            Expect(outcome, "Duration (ms)", Number(root, "durationMs"),
                   (int)Math.Round(song.Duration.TotalMilliseconds));
            outcome.Skipped.Add(
                "Song exposes no sample data in XNA 4.0, and playing one needs the media stack, " +
                "so only the load and the duration are asserted here.");
        }

        private static void CheckVideo(Outcome outcome, ContentManager content,
                                        string name, Dictionary<string, object> root)
        {
            Video video = content.Load<Video>(name);
            Expect(outcome, "Width", Number(root, "width"), video.Width);
            Expect(outcome, "Height", Number(root, "height"), video.Height);
            Expect(outcome, "FramesPerSecond", Number(root, "framesPerSecond"),
                   (int)Math.Round(video.FramesPerSecond));
        }

        private static void CheckSoundEffect(Outcome outcome, ContentManager content,
                                              string name, Dictionary<string, object> root)
        {
            SoundEffect effect = content.Load<SoundEffect>(name);
            Expect(outcome, "Duration (ms)", Number(root, "durationMs"),
                   (int)Math.Round(effect.Duration.TotalMilliseconds));
            outcome.Skipped.Add(
                "SoundEffect exposes no sample-rate, channel-count or PCM accessor in XNA 4.0, " +
                "so only Duration is asserted here.");
        }

        private static void CheckSpriteFont(Outcome outcome, ContentManager content,
                                             string name, Dictionary<string, object> root)
        {
            SpriteFont font = content.Load<SpriteFont>(name);
            Expect(outcome, "LineSpacing", Number(root, "lineSpacing"), font.LineSpacing);
            Expect(outcome, "Spacing", Number(root, "spacing"), font.Spacing);
            if (root.ContainsKey("defaultCharacter"))
            {
                string expected = Convert.ToString(root["defaultCharacter"]);
                string actual = font.DefaultCharacter.HasValue
                    ? font.DefaultCharacter.Value.ToString()
                    : "(none)";
                Expect(outcome, "DefaultCharacter", expected, actual);
            }
            var characters = root["characters"] as List<object>;
            if (characters != null)
            {
                Expect(outcome, "Characters.Count", characters.Count, font.Characters.Count);
                for (int index = 0; index < characters.Count &&
                                    index < font.Characters.Count; index++)
                {
                    Expect(outcome, "Characters[" + index + "]", characters[index],
                           font.Characters[index].ToString());
                }
            }
        }

        private static void CheckCurve(Outcome outcome, ContentManager content,
                                        string name, Dictionary<string, object> root)
        {
            Curve curve = content.Load<Curve>(name);
            Expect(outcome, "PreLoop", (int)Number(root, "preLoop"), (int)curve.PreLoop);
            Expect(outcome, "PostLoop", (int)Number(root, "postLoop"), (int)curve.PostLoop);
            var keys = root["keys"] as List<object>;
            if (keys != null)
            {
                Expect(outcome, "Keys.Count", keys.Count, curve.Keys.Count);
                for (int index = 0; index < keys.Count && index < curve.Keys.Count; index++)
                {
                    var key = Object(keys[index], "key " + index);
                    Expect(outcome, "Keys[" + index + "].Position", Number(key, "position"),
                           curve.Keys[index].Position);
                    Expect(outcome, "Keys[" + index + "].Value", Number(key, "value"),
                           curve.Keys[index].Value);
                }
            }
        }

        private static void CheckStringList(Outcome outcome, ContentManager content,
                                             string name, List<object> expected)
        {
            List<string> loaded = content.Load<List<string>>(name);
            Expect(outcome, "Count", expected.Count, loaded.Count);
            for (int index = 0; index < expected.Count && index < loaded.Count; index++)
            {
                Expect(outcome, "[" + index + "]", expected[index], loaded[index]);
            }
        }

        private static void CheckModel(Outcome outcome, ContentManager content,
                                        string name, Dictionary<string, object> root)
        {
            Model model = content.Load<Model>(name);
            var bones = root["bones"] as List<object>;
            Expect(outcome, "Bones.Count", bones.Count, model.Bones.Count);
            for (int index = 0; index < bones.Count && index < model.Bones.Count; index++)
            {
                var bone = Object(bones[index], "bone " + index);
                Expect(outcome, "Bones[" + index + "].Name", bone["name"],
                       model.Bones[index].Name);
            }
            Expect(outcome, "Root.Index", (int)Number(root, "rootBone"), model.Root.Index);

            var meshes = root["meshes"] as List<object>;
            Expect(outcome, "Meshes.Count", meshes.Count, model.Meshes.Count);
            for (int index = 0; index < meshes.Count && index < model.Meshes.Count; index++)
            {
                var mesh = Object(meshes[index], "mesh " + index);
                ModelMesh loaded = model.Meshes[index];
                Expect(outcome, "Meshes[" + index + "].Name", mesh["name"], loaded.Name);
                Expect(outcome, "Meshes[" + index + "].ParentBone.Index",
                       (int)Number(mesh, "parentBone"), loaded.ParentBone.Index);
                var parts = mesh["parts"] as List<object>;
                Expect(outcome, "Meshes[" + index + "].MeshParts.Count", parts.Count,
                       loaded.MeshParts.Count);
                for (int part = 0; part < parts.Count && part < loaded.MeshParts.Count; part++)
                {
                    var expected = Object(parts[part], "part " + part);
                    ModelMeshPart actual = loaded.MeshParts[part];
                    Expect(outcome, "part " + part + ".NumVertices",
                           (int)Number(expected, "numVertices"), actual.NumVertices);
                    Expect(outcome, "part " + part + ".PrimitiveCount",
                           (int)Number(expected, "primitiveCount"), actual.PrimitiveCount);
                    Expect(outcome, "part " + part + ".StartIndex",
                           (int)Number(expected, "startIndex"), actual.StartIndex);
                    if (actual.Effect == null)
                    {
                        outcome.Problems.Add("part " + part + " has no Effect");
                    }
                    else if (!(actual.Effect is BasicEffect))
                    {
                        outcome.Problems.Add(
                            "part " + part + " Effect is " + actual.Effect.GetType().Name +
                            ", expected BasicEffect");
                    }
                }
            }
        }

        private static string Fnv1a(byte[] data)
        {
            ulong value = 0xCBF29CE484222325UL;
            foreach (byte b in data) { value = (value ^ b) * 0x100000001B3UL; }
            return value.ToString("x16", CultureInfo.InvariantCulture);
        }

        private static int Main(string[] arguments)
        {
            if (arguments.Length != 1)
            {
                Console.Error.WriteLine(
                    "usage: CnaXnbInterop.exe <directory containing the CNA fixture corpus>");
                return 2;
            }

            string root = arguments[0];
            string indexPath = Path.Combine(root, "fixtures.json");
            if (!File.Exists(indexPath))
            {
                Console.Error.WriteLine("no fixtures.json in " + root);
                return 2;
            }

            var index = Object(Json.Parse(File.ReadAllText(indexPath)), "fixtures.json");
            var fixtures = index["fixtures"] as List<object>;

            // A GraphicsDevice is required for Texture2D, SpriteFont and Model. A headless
            // console process gets one through a hidden Game; that is the least surprising way to
            // obtain a real device from XNA 4.0.
            using (var game = new Game())
            {
                var manager = new GraphicsDeviceManager(game);
                // HiDef rather than the default Reach, because Reach is a *smaller* profile and
                // one of the output families needs the larger one: XNA refuses a Texture3D under
                // Reach outright -- `XNA Framework Reach profile does not support Texture3D` --
                // and a corpus that cannot ask about volume textures is not covering that family
                // (plans/plan_xnapipeline_parity.md XNAPP-281). Every Reach asset loads on a HiDef
                // device; the reverse is what does not hold.
                manager.GraphicsProfile = GraphicsProfile.HiDef;
                // RunOneFrame() is what a game does, and on some hosts it never reaches device
                // creation: under Wine it returns with GraphicsDeviceManager.GraphicsDevice still
                // null, and every Texture2D, SpriteFont and Model then fails with "GraphicsDevice
                // component not found" for a reason that has nothing to do with the .xnb. The
                // documented interface method creates the device directly, so it is tried first
                // and RunOneFrame is the fallback.
                try
                {
                    ((IGraphicsDeviceManager)manager).CreateDevice();
                }
                catch (Exception error)
                {
                    Console.Error.WriteLine("CreateDevice failed (" + error.GetType().Name + ": " +
                                            error.Message + "); falling back to RunOneFrame");
                }
                if (manager.GraphicsDevice == null)
                {
                    game.RunOneFrame();
                }
                if (manager.GraphicsDevice == null)
                {
                    Console.Error.WriteLine(
                        "no GraphicsDevice could be created on this host; Texture2D, SpriteFont " +
                        "and Model will fail for that reason and not because of their .xnb");
                }
                else
                {
                    Console.WriteLine("graphics device: " + manager.GraphicsDevice.Adapter.Description +
                                      " (" + manager.GraphicsDevice.GraphicsProfile + ")");
                }

                var content = new ContentManager(game.Services, root);
                var outcomes = new List<Outcome>();

                foreach (object entry in fixtures)
                {
                    var fixture = Object(entry, "fixture");
                    string name = Convert.ToString(fixture["name"]);
                    var outcome = new Outcome { Name = name, Status = "passed" };
                    outcomes.Add(outcome);
                    try
                    {
                        var expectation = Object(
                            Json.Parse(File.ReadAllText(
                                Path.Combine(root, Convert.ToString(fixture["expectation"])))),
                            name);
                        string reader = Convert.ToString(expectation["rootReader"]);
                        object rootValue = expectation["root"];

                        if (reader.EndsWith("Texture2DReader", StringComparison.Ordinal))
                        {
                            CheckTexture2D(outcome, content, name, Object(rootValue, name));
                        }
                        else if (reader.EndsWith("TextureCubeReader", StringComparison.Ordinal))
                        {
                            CheckTextureCube(outcome, content, name, Object(rootValue, name));
                        }
                        else if (reader.EndsWith("Texture3DReader", StringComparison.Ordinal))
                        {
                            CheckTexture3D(outcome, content, name, Object(rootValue, name));
                        }
                        // The dot matters: `SoundEffectReader` also ends with `EffectReader`.
                        else if (reader.EndsWith("SongReader", StringComparison.Ordinal))
                        {
                            CheckSong(outcome, content, name, Object(rootValue, name));
                        }
                        else if (reader.EndsWith("VideoReader", StringComparison.Ordinal))
                        {
                            CheckVideo(outcome, content, name, Object(rootValue, name));
                        }
                        // The dot matters: `SoundEffectReader` also ends with `EffectReader`.
                        else if (reader.EndsWith(".EffectReader", StringComparison.Ordinal))
                        {
                            CheckEffect(outcome, content, name, Object(rootValue, name));
                        }
                        else if (reader.EndsWith("SoundEffectReader", StringComparison.Ordinal))
                        {
                            CheckSoundEffect(outcome, content, name, Object(rootValue, name));
                        }
                        else if (reader.EndsWith("SpriteFontReader", StringComparison.Ordinal))
                        {
                            CheckSpriteFont(outcome, content, name, Object(rootValue, name));
                        }
                        else if (reader.EndsWith("CurveReader", StringComparison.Ordinal))
                        {
                            CheckCurve(outcome, content, name, Object(rootValue, name));
                        }
                        else if (reader.EndsWith("ModelReader", StringComparison.Ordinal))
                        {
                            CheckModel(outcome, content, name, Object(rootValue, name));
                        }
                        else if (reader.Contains("ListReader"))
                        {
                            CheckStringList(outcome, content, name, (List<object>)rootValue);
                        }
                        else
                        {
                            outcome.Status = "skipped";
                            outcome.Skipped.Add("no harness check for root reader " + reader);
                        }
                    }
                    catch (Exception error)
                    {
                        outcome.Status = "error";
                        outcome.Problems.Add(error.GetType().Name + ": " + error.Message);
                    }
                    if (outcome.Status == "passed" && outcome.Problems.Count > 0)
                    {
                        outcome.Status = "failed";
                    }
                }

                int failed = 0;
                foreach (Outcome outcome in outcomes)
                {
                    Console.WriteLine(outcome.Status.ToUpperInvariant().PadRight(8) +
                                      outcome.Name);
                    foreach (string problem in outcome.Problems)
                    {
                        Console.WriteLine("         " + problem);
                    }
                    foreach (string skipped in outcome.Skipped)
                    {
                        Console.WriteLine("         (not asserted) " + skipped);
                    }
                    if (outcome.Status == "failed" || outcome.Status == "error") { failed++; }
                }
                Console.WriteLine();
                Console.WriteLine("fixtures: " + outcomes.Count + "   failed: " + failed);
                Console.WriteLine("XNA runtime: " +
                                  typeof(Game).Assembly.GetName().Version);
                return failed == 0 ? 0 : 1;
            }
        }
    }
}
