// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-201, XNAPP-202, XNAPP-220, XNAPP-136: black-box behaviour oracle
// for the three importers XNA reads through Windows Media -- Mp3Importer, WmaImporter and
// WmvImporter -- for the VideoContent they and the VideoProcessor exchange, and for what
// SongProcessor and VideoProcessor do with them.
//
// The driver hands the synthetic corpus in tests/assets/xna40/media to the genuine pipeline
// assemblies and records what they answer. It runs the assemblies and records what they DO;
// nothing here inspects XNA's IL.
//
// Output: media-content-oracle.json, one entry per measurement, rewritten after every case so a
// call that never returns still leaves everything measured before it on disk. A case named on the
// command line after the two directories is skipped, which is how a hanging one is stepped over.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Microsoft.Xna.Framework.Content.Pipeline;
using Microsoft.Xna.Framework.Content.Pipeline.Audio;
using Microsoft.Xna.Framework.Content.Pipeline.Processors;
using Microsoft.Xna.Framework.Graphics;

namespace Cna.Xna40.MediaOracle
{
    internal static class Program
    {
        private static readonly List<string> Cases = new List<string>();
        private static string OutputDirectory = ".";
        private static string FixtureDirectory = ".";
        private static readonly HashSet<string> Skip = new HashSet<string>(StringComparer.Ordinal);

        private static string Escape(string text)
        {
            return text.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\r", "\\r").Replace("\n", "\\n");
        }

        private static void Record(string name, Func<string> measurement)
        {
            if (Skip.Contains(name))
            {
                Cases.Add("  {\"case\": \"" + name + "\", \"result\": \"SKIPPED (named on the command line: a previous run did not return)\"}");
                Publish();
                return;
            }
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
            File.WriteAllText(Path.Combine(OutputDirectory, "media-content-oracle.json"), Document());
        }

        private static string Document()
        {
            return "{\n \"producer\": \"Microsoft XNA Game Studio 4.0 Content Pipeline (AudioImporters, VideoImporters, Processors), driven by tools/xna-pipeline-oracle/media/MediaContentOracle.cs\",\n \"runtime\": \"" +
                   Environment.Version + "\",\n \"pipelineAssembly\": \"" + typeof(AudioContent).Assembly.FullName +
                   "\",\n \"cases\": [\n" + string.Join(",\n", Cases.ToArray()) + "\n ]\n}\n";
        }

        private static string Hex(IList<byte> bytes, int limit)
        {
            if (bytes == null) return "null";
            var builder = new StringBuilder();
            int count = Math.Min(limit, bytes.Count);
            for (int i = 0; i < count; i++) builder.Append(bytes[i].ToString("X2"));
            if (bytes.Count > count) builder.Append("...");
            return builder.ToString();
        }

        /// A stable digest of a byte sequence, so a decoded payload can be compared without
        /// publishing megabytes of samples. FNV-1a 64, written out here so the record is
        /// reproducible by any implementation.
        private static string Digest(IList<byte> bytes)
        {
            if (bytes == null) return "null";
            ulong hash = 14695981039346656037UL;
            for (int i = 0; i < bytes.Count; i++)
            {
                hash ^= bytes[i];
                hash *= 1099511628211UL;
            }
            return hash.ToString("X16");
        }

        private static string Describe(AudioFormat format)
        {
            if (format == null) return "null";
            return "format=" + format.Format + " channels=" + format.ChannelCount + " sampleRate=" +
                   format.SampleRate + " bits=" + format.BitsPerSample + " blockAlign=" + format.BlockAlign +
                   " bytesPerSecond=" + format.AverageBytesPerSecond + " native=" + Hex(format.NativeWaveFormat, 64);
        }

        private static string Describe(AudioContent audio)
        {
            return "fileType=" + audio.FileType + " durationTicks=" +
                   audio.Duration.Ticks.ToString(CultureInfo.InvariantCulture) + " durationMs=" +
                   ((long)audio.Duration.TotalMilliseconds).ToString(CultureInfo.InvariantCulture) +
                   " loopStart=" + audio.LoopStart + " loopLength=" + audio.LoopLength +
                   " dataLength=" + audio.Data.Count + " dataDigest=" + Digest(audio.Data) +
                   " dataHead=" + Hex(audio.Data, 16) + " " + Describe(audio.Format);
        }

        private static string Describe(VideoContent video)
        {
            if (video == null) return "null";
            return "filename=" + Path.GetFileName(video.Filename) + " width=" + video.Width +
                   " height=" + video.Height + " framesPerSecond=" +
                   video.FramesPerSecond.ToString("R", CultureInfo.InvariantCulture) +
                   " bitsPerSecond=" + video.BitsPerSecond + " durationTicks=" +
                   video.Duration.Ticks.ToString(CultureInfo.InvariantCulture) + " durationMs=" +
                   ((long)video.Duration.TotalMilliseconds).ToString(CultureInfo.InvariantCulture) +
                   " soundtrack=" + video.VideoSoundtrackType +
                   " identity=" + (video.Identity == null ? "null" : "(" + Path.GetFileName(video.Identity.SourceFilename) + ", " + (video.Identity.SourceTool ?? "null") + ")") +
                   " name=" + (video.Name ?? "null") + " opaqueData=" + video.OpaqueData.Count;
        }

        private sealed class Logger : ContentBuildLogger
        {
            public readonly List<string> Lines = new List<string>();
            public override void LogMessage(string message, params object[] args) { Lines.Add("message: " + Format(message, args)); }
            public override void LogImportantMessage(string message, params object[] args) { Lines.Add("important: " + Format(message, args)); }
            public override void LogWarning(string helpLink, ContentIdentity identity, string message, params object[] args) { Lines.Add("warning: " + Format(message, args)); }
            private static string Format(string message, object[] args)
            {
                if (args == null || args.Length == 0) return message;
                try { return string.Format(CultureInfo.InvariantCulture, message, args); }
                catch (FormatException) { return message; }
            }
        }

        private sealed class ImporterContext : ContentImporterContext
        {
            private readonly Logger logger = new Logger();
            public readonly List<string> Dependencies = new List<string>();
            public override string IntermediateDirectory { get { return Path.Combine(Path.GetTempPath(), "cna-media-oracle-int"); } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return Path.Combine(Path.GetTempPath(), "cna-media-oracle-out"); } }
            public override void AddDependency(string filename) { Dependencies.Add(Path.GetFileName(filename)); }
            public string Report { get { return "dependencies=[" + string.Join(",", Dependencies.ToArray()) + "] log=[" + string.Join(" | ", logger.Lines.ToArray()) + "]"; } }
        }

        private sealed class ProcessorContext : ContentProcessorContext
        {
            private readonly Logger logger = new Logger();
            private readonly string platform;
            private readonly GraphicsProfile profile;
            public readonly List<string> Dependencies = new List<string>();
            public readonly List<string> OutputFiles = new List<string>();
            public ProcessorContext() : this("Windows", GraphicsProfile.HiDef) { }
            public ProcessorContext(string targetPlatform, GraphicsProfile targetProfile)
            {
                platform = targetPlatform;
                profile = targetProfile;
                Directory.CreateDirectory(IntermediateDirectory);
                Directory.CreateDirectory(OutputDirectory);
            }
            public override string BuildConfiguration { get { return "Release"; } }
            public override string IntermediateDirectory { get { return Path.Combine(Path.GetTempPath(), "cna-media-oracle-int"); } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return Path.Combine(Path.GetTempPath(), "cna-media-oracle-out"); } }
            public override string OutputFilename { get { return Path.Combine(OutputDirectory, "asset.xnb"); } }
            public override OpaqueDataDictionary Parameters { get { return new OpaqueDataDictionary(); } }
            public override TargetPlatform TargetPlatform { get { return (TargetPlatform)Enum.Parse(typeof(TargetPlatform), platform); } }
            public override GraphicsProfile TargetProfile { get { return profile; } }
            public override void AddDependency(string filename) { Dependencies.Add(Path.GetFileName(filename)); }
            public override void AddOutputFile(string filename) { OutputFiles.Add(Path.GetFileName(filename)); }
            public override TOutput Convert<TInput, TOutput>(TInput input, string processorName, OpaqueDataDictionary processorParameters) { throw new NotSupportedException("Convert"); }
            public override TOutput BuildAndLoadAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName) { throw new NotSupportedException("BuildAndLoadAsset"); }
            public override ExternalReference<TOutput> BuildAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName, string assetName) { throw new NotSupportedException("BuildAsset"); }
            public string Report { get { return "dependencies=[" + string.Join(",", Dependencies.ToArray()) + "] outputFiles=[" + string.Join(",", OutputFiles.ToArray()) + "] log=[" + string.Join(" | ", logger.Lines.ToArray()) + "]"; } }
        }

        private static string Fixture(string name) { return Path.Combine(FixtureDirectory, name); }

        // --- importer measurements -----------------------------------------------------------

        /// Reads one property in isolation, so a source XNA opens but cannot decode is told apart
        /// from one it refuses outright: the message and the member that carries it both matter.
        private static string Probe(string label, Func<string> read)
        {
            try { return label + "=" + read(); }
            catch (Exception error) { return label + "=throws " + error.GetType().Name + ": " + error.Message.Replace("\r", " ").Replace("\n", " "); }
        }

        private static void MeasureAudioImporter(string label, ContentImporter<AudioContent> importer, string fixture)
        {
            Record(label, delegate
            {
                var context = new ImporterContext();
                AudioContent audio = importer.Import(Fixture(fixture), context);
                if (audio == null) return "IMPORT RETURNED null";
                return "IMPORT RETURNED " + audio.GetType().Name + " " +
                       Probe("fileName", delegate { return Path.GetFileName(audio.FileName); }) + " " +
                       Probe("fileType", delegate { return audio.FileType.ToString(); }) + " " +
                       Probe("format", delegate { return Describe(audio.Format); }) + " " +
                       Probe("durationTicks", delegate { return audio.Duration.Ticks.ToString(CultureInfo.InvariantCulture); }) + " " +
                       Probe("loopStart", delegate { return audio.LoopStart.ToString(CultureInfo.InvariantCulture); }) + " " +
                       Probe("loopLength", delegate { return audio.LoopLength.ToString(CultureInfo.InvariantCulture); }) + " " +
                       Probe("dataLength", delegate { return audio.Data.Count.ToString(CultureInfo.InvariantCulture); }) + " " +
                       Probe("dataDigest", delegate { return Digest(audio.Data); }) + " " +
                       Probe("identity", delegate { return audio.Identity == null ? "null" : Path.GetFileName(audio.Identity.SourceFilename); }) +
                       " " + context.Report;
            });
        }

        private static void MeasureVideoImporter(string label, string fixture)
        {
            Record(label, delegate
            {
                var context = new ImporterContext();
                VideoContent video = new WmvImporter().Import(Fixture(fixture), context);
                if (video == null) return "IMPORT RETURNED null";
                return "IMPORT RETURNED " + video.GetType().Name + " " +
                       Probe("filename", delegate { return Path.GetFileName(video.Filename); }) + " " +
                       Probe("width", delegate { return video.Width.ToString(CultureInfo.InvariantCulture); }) + " " +
                       Probe("height", delegate { return video.Height.ToString(CultureInfo.InvariantCulture); }) + " " +
                       Probe("framesPerSecond", delegate { return video.FramesPerSecond.ToString("R", CultureInfo.InvariantCulture); }) + " " +
                       Probe("bitsPerSecond", delegate { return video.BitsPerSecond.ToString(CultureInfo.InvariantCulture); }) + " " +
                       Probe("durationTicks", delegate { return video.Duration.Ticks.ToString(CultureInfo.InvariantCulture); }) + " " +
                       Probe("soundtrack", delegate { return video.VideoSoundtrackType.ToString(); }) + " " +
                       context.Report;
            });
        }

        private static void Main2()
        {
            // 1. What the three importers declare -- the attribute the host reads to route a file.
            Record("attribute/mp3", delegate { return DescribeImporterAttribute(typeof(Mp3Importer)); });
            Record("attribute/wma", delegate { return DescribeImporterAttribute(typeof(WmaImporter)); });
            Record("attribute/wmv", delegate { return DescribeImporterAttribute(typeof(WmvImporter)); });

            // 2. Mp3Importer over the corpus.
            foreach (string name in new string[] {
                "mp3_mono_44100_128k.mp3", "mp3_stereo_44100_192k.mp3", "mp3_mono_22050_64k.mp3",
                "mp3_mono_44100_tagged.mp3", "mp3_mono_44100_vbr.mp3",
                "mp3_mono_48000_128k.mp3", "mp3_mono_32000_128k.mp3", "mp3_mono_24000_64k.mp3",
                "mp3_mono_16000_64k.mp3", "mp3_mono_8000_32k.mp3", "mp3_stereo_22050_96k.mp3",
                "mp3_mono_44100_2s.mp3" })
            {
                MeasureAudioImporter("mp3/" + name, new Mp3Importer(), name);
            }
            // Refusals, and the extension-versus-content question.
            foreach (string name in new string[] { "empty.mp3", "truncated.mp3", "garbage.mp3", "actually_wav.mp3", "missing.mp3" })
            {
                MeasureAudioImporter("mp3/" + name, new Mp3Importer(), name);
            }
            MeasureAudioImporter("mp3/tone_mono_44100.wav", new Mp3Importer(), "tone_mono_44100.wav");
            MeasureAudioImporter("wav/actually_mp3.wav", new WavImporter(), "actually_mp3.wav");
            MeasureAudioImporter("wav/mp3_mono_44100_128k.mp3", new WavImporter(), "mp3_mono_44100_128k.mp3");

            // 3. WmaImporter over the corpus.
            foreach (string name in new string[] {
                "wma_mono_44100.wma", "wma_stereo_44100.wma", "wma_mono_22050.wma", "wma_v1_mono_44100.wma",
                "empty.wma", "truncated.wma", "missing.wma" })
            {
                MeasureAudioImporter("wma/" + name, new WmaImporter(), name);
            }
            MeasureAudioImporter("wma/mp3_mono_44100_128k.mp3", new WmaImporter(), "mp3_mono_44100_128k.mp3");

            // 4. WmvImporter over the corpus.
            foreach (string name in new string[] {
                "wmv_64x48_15fps_silent.wmv", "wmv_320x240_30fps_stereo.wmv", "wmv_v1_64x48.wmv",
                "empty.wmv", "truncated.wmv", "missing.wmv" })
            {
                MeasureVideoImporter("wmv/" + name, name);
            }
            MeasureVideoImporter("wmv/mp3_mono_44100_128k.mp3", "mp3_mono_44100_128k.mp3");

            // 5. VideoContent constructed directly, which is what a custom importer does.
            Record("videocontent/construct", delegate
            {
                using (var video = new VideoContent(Fixture("wmv_64x48_15fps_silent.wmv")))
                {
                    return Describe(video);
                }
            });
            Record("videocontent/construct_missing", delegate
            {
                using (var video = new VideoContent(Fixture("missing.wmv"))) { return Describe(video); }
            });
            Record("videocontent/construct_null", delegate
            {
                using (var video = new VideoContent(null)) { return Describe(video); }
            });
            Record("videocontent/construct_empty", delegate
            {
                using (var video = new VideoContent("")) { return Describe(video); }
            });
            Record("videocontent/construct_not_video", delegate
            {
                using (var video = new VideoContent(Fixture("tone_mono_44100.wav"))) { return Describe(video); }
            });
            Record("videocontent/after_dispose", delegate
            {
                var video = new VideoContent(Fixture("wmv_64x48_15fps_silent.wmv"));
                video.Dispose();
                video.Dispose();
                return Describe(video);
            });
            Record("videocontent/filename_relative", delegate
            {
                // Does the property answer what was passed, or a rooted path?
                string previous = Directory.GetCurrentDirectory();
                Directory.SetCurrentDirectory(FixtureDirectory);
                try
                {
                    using (var video = new VideoContent("wmv_64x48_15fps_silent.wmv"))
                    {
                        return "passed=wmv_64x48_15fps_silent.wmv answered=" + video.Filename;
                    }
                }
                finally { Directory.SetCurrentDirectory(previous); }
            });

            // 6. VideoProcessor: its default, and what Process answers.
            Record("videoprocessor/defaults", delegate
            {
                var processor = new VideoProcessor();
                return "VideoSoundtrackType=" + processor.VideoSoundtrackType;
            });
            Record("videoprocessor/process", delegate
            {
                var context = new ProcessorContext();
                using (var video = new VideoContent(Fixture("wmv_64x48_15fps_silent.wmv")))
                {
                    VideoContent output = new VideoProcessor().Process(video, context);
                    return "sameInstance=" + object.ReferenceEquals(output, video) + " " + Describe(output) + " " + context.Report;
                }
            });
            foreach (string soundtrack in new string[] { "Music", "Dialog", "MusicAndDialog" })
            {
                string captured = soundtrack;
                Record("videoprocessor/process_" + captured.ToLowerInvariant(), delegate
                {
                    var context = new ProcessorContext();
                    using (var video = new VideoContent(Fixture("wmv_320x240_30fps_stereo.wmv")))
                    {
                        var processor = new VideoProcessor();
                        processor.VideoSoundtrackType = (Microsoft.Xna.Framework.Media.VideoSoundtrackType)
                            Enum.Parse(typeof(Microsoft.Xna.Framework.Media.VideoSoundtrackType), captured);
                        VideoContent output = processor.Process(video, context);
                        return "sameInstance=" + object.ReferenceEquals(output, video) + " " + Describe(output) + " " + context.Report;
                    }
                });
            }
            Record("videoprocessor/process_null", delegate
            {
                return Describe(new VideoProcessor().Process(null, new ProcessorContext()));
            });
            foreach (string target in new string[] { "Windows", "Xbox360", "WindowsPhone" })
            {
                string captured = target;
                Record("videoprocessor/target_" + captured.ToLowerInvariant(), delegate
                {
                    var context = new ProcessorContext(captured, GraphicsProfile.Reach);
                    using (var video = new VideoContent(Fixture("wmv_64x48_15fps_silent.wmv")))
                    {
                        VideoContent output = new VideoProcessor().Process(video, context);
                        return Describe(output) + " " + context.Report;
                    }
                });
            }

            // 7. SongProcessor over an imported MP3 and WMA. The previous session measured that
            //    XNA's Windows Media *encoder* never returns under this prefix; these two cases
            //    are the ones a run steps over by name when that happens again.
            Record("songprocessor/mp3", delegate
            {
                var context = new ProcessorContext();
                AudioContent audio = new Mp3Importer().Import(Fixture("mp3_mono_44100_128k.mp3"), new ImporterContext());
                SongContent song = new SongProcessor().Process(audio, context);
                return "song=" + (song == null ? "null" : song.GetType().Name) + " inputAfter=" + Describe(audio) + " " + context.Report;
            });
            Record("songprocessor/wma", delegate
            {
                var context = new ProcessorContext();
                AudioContent audio = new WmaImporter().Import(Fixture("wma_mono_44100.wma"), new ImporterContext());
                SongContent song = new SongProcessor().Process(audio, context);
                return "song=" + (song == null ? "null" : song.GetType().Name) + " inputAfter=" + Describe(audio) + " " + context.Report;
            });
        }

        private static string DescribeImporterAttribute(Type importer)
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

        private static int Main(string[] args)
        {
            OutputDirectory = args.Length > 0 ? args[0] : ".";
            FixtureDirectory = args.Length > 1 ? args[1] : ".";
            for (int i = 2; i < args.Length; i++) Skip.Add(args[i]);
            Directory.CreateDirectory(OutputDirectory);
            Main2();
            Publish();
            Console.WriteLine("recorded " + Cases.Count + " measurements");
            return 0;
        }
    }
}
