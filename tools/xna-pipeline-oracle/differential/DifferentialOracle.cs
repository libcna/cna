// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-265 (§23): run Microsoft's own `BuildContent` task over a
// committed corpus and record, per case, what it produced.
//
// Every other oracle in this tree measures one object at a time -- what a processor answers, what a
// serializer writes. This one measures the *build*: the same task an XNA project's MSBuild run
// invokes, over the same sources CNA builds, with the same importer, processor, parameters,
// platform and profile. What it records is the `.xnb` itself, every diagnostic the build engine
// received, and the files the task says it wrote, read out of the task's own output properties
// rather than by looking at the directory.
//
// The corpus is a committed JSON manifest, so adding a case is an edit rather than a code change,
// and a case that fails is recorded as a failure with its diagnostics instead of stopping the run:
// what XNA refuses is as much a measurement as what it accepts.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;
using Microsoft.Xna.Framework.Content.Pipeline.Tasks;

/// Collects everything the task says, so a case's diagnostics travel with its result.
internal sealed class RecordingBuildEngine : IBuildEngine
{
    public readonly List<string> Messages = new List<string>();
    public readonly List<string> Warnings = new List<string>();
    public readonly List<string> Errors = new List<string>();

    public bool ContinueOnError { get { return false; } }
    public int LineNumberOfTaskNode { get { return 0; } }
    public int ColumnNumberOfTaskNode { get { return 0; } }
    public string ProjectFileOfTaskNode { get { return "cna-differential-oracle"; } }

    public bool BuildProjectFile(string projectFileName, string[] targetNames,
                                 System.Collections.IDictionary globalProperties,
                                 System.Collections.IDictionary targetOutputs)
    {
        return false;
    }

    public void LogCustomEvent(CustomBuildEventArgs e) { Messages.Add(e.Message); }
    public void LogErrorEvent(BuildErrorEventArgs e) { Errors.Add(Clean(e.Message)); }
    public void LogMessageEvent(BuildMessageEventArgs e) { Messages.Add(Clean(e.Message)); }
    public void LogWarningEvent(BuildWarningEventArgs e) { Warnings.Add(Clean(e.Message)); }

    private static string Clean(string text)
    {
        return text == null ? string.Empty : text.Replace("\r", " ").Replace("\n", " ");
    }
}

internal static class Program
{
    /// The smallest JSON reader that reads this manifest: it is written by this repository, so
    /// what it has to cope with is known rather than arbitrary.
    private sealed class Json
    {
        private readonly string text;
        private int at;
        private Json(string source) { text = source; }

        public static object Parse(string source)
        {
            var reader = new Json(source);
            reader.SkipSpace();
            object value = reader.ReadValue();
            return value;
        }

        private void SkipSpace() { while (at < text.Length && char.IsWhiteSpace(text[at])) at++; }

        private object ReadValue()
        {
            SkipSpace();
            char c = text[at];
            if (c == '{') return ReadObject();
            if (c == '[') return ReadArray();
            if (c == '"') return ReadString();
            if (c == 't') { at += 4; return true; }
            if (c == 'f') { at += 5; return false; }
            if (c == 'n') { at += 4; return null; }
            int start = at;
            while (at < text.Length && "-+.eE0123456789".IndexOf(text[at]) >= 0) at++;
            return double.Parse(text.Substring(start, at - start), CultureInfo.InvariantCulture);
        }

        private Dictionary<string, object> ReadObject()
        {
            var result = new Dictionary<string, object>();
            at++;  // '{'
            SkipSpace();
            if (text[at] == '}') { at++; return result; }
            while (true)
            {
                SkipSpace();
                string key = ReadString();
                SkipSpace();
                at++;  // ':'
                result[key] = ReadValue();
                SkipSpace();
                if (text[at] == ',') { at++; continue; }
                at++;  // '}'
                return result;
            }
        }

        private List<object> ReadArray()
        {
            var result = new List<object>();
            at++;  // '['
            SkipSpace();
            if (text[at] == ']') { at++; return result; }
            while (true)
            {
                result.Add(ReadValue());
                SkipSpace();
                if (text[at] == ',') { at++; continue; }
                at++;  // ']'
                return result;
            }
        }

        private string ReadString()
        {
            var builder = new StringBuilder();
            at++;  // '"'
            while (text[at] != '"')
            {
                if (text[at] == '\\')
                {
                    at++;
                    char escape = text[at++];
                    if (escape == 'n') builder.Append('\n');
                    else if (escape == 't') builder.Append('\t');
                    else if (escape == 'r') builder.Append('\r');
                    else if (escape == 'u')
                    {
                        builder.Append((char)Convert.ToInt32(text.Substring(at, 4), 16));
                        at += 4;
                    }
                    else builder.Append(escape);
                    continue;
                }
                builder.Append(text[at++]);
            }
            at++;  // '"'
            return builder.ToString();
        }
    }

    private static string Text(Dictionary<string, object> row, string key, string fallback)
    {
        object value;
        return row.TryGetValue(key, out value) && value is string ? (string)value : fallback;
    }

    private static string Quote(string text)
    {
        var builder = new StringBuilder("\"");
        foreach (char c in text ?? string.Empty)
        {
            if (c == '"' || c == '\\') builder.Append('\\').Append(c);
            else if (c == '\n') builder.Append("\\n");
            else if (c == '\r') builder.Append("\\r");
            else if (c == '\t') builder.Append("\\t");
            else if (c < 0x20) builder.Append("\\u").Append(((int)c).ToString("X4"));
            else builder.Append(c);
        }
        return builder.Append('"').ToString();
    }

    private static string Hex(byte[] bytes)
    {
        var builder = new StringBuilder(bytes.Length * 2);
        foreach (byte b in bytes) builder.Append(b.ToString("X2"));
        return builder.ToString();
    }

    private static string Names(ITaskItem[] items, string root)
    {
        if (items == null) return "[]";
        var parts = new List<string>();
        foreach (ITaskItem item in items)
        {
            string spec = item.ItemSpec ?? string.Empty;
            if (root.Length > 0 && spec.StartsWith(root, StringComparison.OrdinalIgnoreCase))
            {
                spec = spec.Substring(root.Length).TrimStart('\\', '/');
            }
            parts.Add(Quote(spec.Replace('\\', '/')));
        }
        parts.Sort(StringComparer.Ordinal);
        return "[" + string.Join(",", parts.ToArray()) + "]";
    }

    private static string List(List<string> lines)
    {
        var parts = new List<string>();
        foreach (string line in lines) parts.Add(Quote(line));
        return "[" + string.Join(",", parts.ToArray()) + "]";
    }

    /// <param name="args">0: manifest, 1: source root, 2: output root, 3: pipeline assembly dir.</param>
    private static int Main(string[] args)
    {
        if (args.Length < 4)
        {
            Console.Error.WriteLine(
                "usage: DifferentialOracle <corpus.json> <sourceRoot> <outputRoot> <assemblyDir>");
            return 2;
        }
        string manifestPath = args[0];
        string sourceRoot = args[1].TrimEnd('\\');
        string outputRoot = args[2].TrimEnd('\\');
        string assemblies = args[3].TrimEnd('\\');

        var manifest = (Dictionary<string, object>)Json.Parse(File.ReadAllText(manifestPath));
        var cases = (List<object>)manifest["cases"];

        Directory.CreateDirectory(outputRoot);
        var results = new StringBuilder();
        results.Append("{\n  \"format\": \"CNA.XnaDifferential.Results\",\n  \"version\": 1,\n");
        results.Append("  \"producer\": \"tools/xna-pipeline-oracle/differential/DifferentialOracle.cs\",\n");
        results.Append("  \"cases\": [\n");

        var pipelineAssemblies = new List<ITaskItem>();
        foreach (string dll in Directory.GetFiles(assemblies,
                                                  "Microsoft.Xna.Framework.Content.Pipeline.*.dll"))
        {
            pipelineAssemblies.Add(new TaskItem(dll));
        }

        int index = 0;
        int failures = 0;
        foreach (object entry in cases)
        {
            var row = (Dictionary<string, object>)entry;
            string name = Text(row, "case", "case" + index);
            string source = Text(row, "source", string.Empty);
            string platform = Text(row, "platform", "Windows");
            string profile = Text(row, "profile", "Reach");

            string caseDirectory = Path.Combine(outputRoot, name.Replace('/', '_'));
            string intermediate = Path.Combine(caseDirectory, "obj");
            string output = Path.Combine(caseDirectory, "bin");
            Directory.CreateDirectory(intermediate);
            Directory.CreateDirectory(output);

            var item = new TaskItem(Path.Combine(sourceRoot, source.Replace('/', '\\')));
            item.SetMetadata("Name", Path.GetFileNameWithoutExtension(source));
            string importer = Text(row, "importer", string.Empty);
            string processor = Text(row, "processor", string.Empty);
            if (importer.Length > 0) item.SetMetadata("Importer", importer);
            if (processor.Length > 0) item.SetMetadata("Processor", processor);
            object parameters;
            if (row.TryGetValue("parameters", out parameters) && parameters is Dictionary<string, object>)
            {
                foreach (var pair in (Dictionary<string, object>)parameters)
                {
                    item.SetMetadata("ProcessorParameters_" + pair.Key, Convert.ToString(
                        pair.Value, CultureInfo.InvariantCulture));
                }
            }

            var engine = new RecordingBuildEngine();
            var task = new BuildContent
            {
                BuildEngine = engine,
                ContentProjectGUID = "{7C1D0F4E-5B2A-4E97-9E3C-1B7A2D6F4E80}",
                BuildConfiguration = "Release",
                IntermediateDirectory = intermediate,
                OutputDirectory = output,
                PipelineAssemblies = pipelineAssemblies.ToArray(),
                RebuildAll = true,
                RootDirectory = sourceRoot,
                LoggerRootDirectory = sourceRoot,
                SourceAssets = new ITaskItem[] { item },
                TargetPlatform = platform,
                TargetProfile = profile,
                CompressContent = false
            };

            bool built;
            string threw = string.Empty;
            try
            {
                built = task.Execute();
            }
            catch (Exception error)
            {
                built = false;
                threw = error.GetType().Name + ": " +
                        (error.Message ?? string.Empty).Replace("\r", " ").Replace("\n", " ");
            }
            if (!built) failures++;

            // The compiled bytes are published beside the results so the comparison has something
            // to compare; a case XNA refused simply has none.
            string digest = string.Empty;
            long length = -1;
            if (built && task.OutputContentFiles != null && task.OutputContentFiles.Length > 0)
            {
                string produced = task.OutputContentFiles[0].ItemSpec;
                if (File.Exists(produced))
                {
                    byte[] bytes = File.ReadAllBytes(produced);
                    length = bytes.LongLength;
                    File.WriteAllBytes(Path.Combine(outputRoot, name.Replace('/', '_') + ".xnb"), bytes);
                    using (var sha = System.Security.Cryptography.SHA256.Create())
                    {
                        digest = Hex(sha.ComputeHash(bytes));
                    }
                }
            }

            if (index > 0) results.Append(",\n");
            results.Append("    {");
            results.Append("\"case\": ").Append(Quote(name));
            results.Append(", \"built\": ").Append(built ? "true" : "false");
            results.Append(", \"bytes\": ").Append(length.ToString(CultureInfo.InvariantCulture));
            results.Append(", \"sha256\": ").Append(Quote(digest));
            results.Append(", \"outputs\": ").Append(Names(task.OutputContentFiles, output));
            results.Append(", \"intermediates\": ").Append(Names(task.IntermediateFiles, intermediate));
            results.Append(", \"errors\": ").Append(List(engine.Errors));
            results.Append(", \"warnings\": ").Append(List(engine.Warnings));
            if (threw.Length > 0) results.Append(", \"threw\": ").Append(Quote(threw));
            results.Append("}");

            Console.WriteLine((built ? "built  " : "refused") + " " + name);
            index++;
        }

        results.Append("\n  ]\n}\n");
        File.WriteAllText(Path.Combine(outputRoot, "differential-oracle.json"), results.ToString());
        Console.WriteLine("recorded " + index + " case(s), " + failures + " refused");
        return 0;
    }
}
