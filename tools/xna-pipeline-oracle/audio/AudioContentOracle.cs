// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-160, XNAPP-161: black-box behaviour oracle for the XNA 4.0
// content pipeline's audio intermediate types -- AudioContent, AudioFormat and the three
// enumerations -- and for what ConvertFormat does at each quality.
//
// The driver writes its own small WAV files, hands them to the genuine pipeline assembly and
// records what it answers. It runs the assemblies and records what they DO; nothing here inspects
// XNA's IL. No Direct3D device is created, so no display is needed.
//
// Output: one JSON document (audio-content-oracle.json) whose "cases" list has one entry per
// measurement.
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Text;
using Microsoft.Xna.Framework.Content.Pipeline;
using Microsoft.Xna.Framework.Content.Pipeline.Audio;
using Microsoft.Xna.Framework.Content.Pipeline.Processors;
using Microsoft.Xna.Framework.Graphics;

namespace Cna.Xna40.AudioOracle
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
            try
            {
                Cases.Add("  {\"case\": \"" + name + "\", \"result\": \"" + Escape(measurement()) + "\"}");
            }
            catch (Exception error)
            {
                Cases.Add("  {\"case\": \"" + name + "\", \"result\": \"throws " + error.GetType().Name +
                          ": " + Escape(error.Message) + "\"}");
            }
            // Written after every case: a conversion that never returns still leaves everything
            // measured before it on disk, which is how the blocked ones were found.
            Publish();
        }

        private static void Publish()
        {
            File.WriteAllText(Path.Combine(OutputDirectory, "audio-content-oracle.json"), Document());
        }


        private static string Hex(IList<byte> bytes)
        {
            if (bytes == null) return "null";
            var builder = new StringBuilder(bytes.Count * 2);
            foreach (byte b in bytes) builder.Append(b.ToString("X2"));
            return builder.ToString();
        }

        /// Writes a PCM WAV of the requested shape, filled with a simple ramp.
        private static string WriteWav(string directory, string name, int sampleRate, int channels,
                                       int bitsPerSample, int frames)
        {
            string path = Path.Combine(directory, name);
            int blockAlign = channels * bitsPerSample / 8;
            int dataBytes = frames * blockAlign;
            using (var stream = new FileStream(path, FileMode.Create, FileAccess.Write))
            using (var writer = new BinaryWriter(stream))
            {
                writer.Write(new char[] { 'R', 'I', 'F', 'F' });
                writer.Write(36 + dataBytes);
                writer.Write(new char[] { 'W', 'A', 'V', 'E' });
                writer.Write(new char[] { 'f', 'm', 't', ' ' });
                writer.Write(16);
                writer.Write((short)1);
                writer.Write((short)channels);
                writer.Write(sampleRate);
                writer.Write(sampleRate * blockAlign);
                writer.Write((short)blockAlign);
                writer.Write((short)bitsPerSample);
                writer.Write(new char[] { 'd', 'a', 't', 'a' });
                writer.Write(dataBytes);
                for (int i = 0; i < dataBytes; i++) writer.Write((byte)((i * 7 + 13) & 0xFF));
            }
            return path;
        }

        /// Writes a WAV with the exact fmt fields given, plus an optional loop region.
        private static string WriteWavRaw(string directory, string name, ushort formatTag, ushort channels,
                                          int sampleRate, ushort bitsPerSample, ushort blockAlign,
                                          int averageBytesPerSecond, byte[] extension, byte[] payload,
                                          int loopStart, int loopLength, int factFrames)
        {
            string path = Path.Combine(directory, name);
            using (var stream = new FileStream(path, FileMode.Create, FileAccess.Write))
            using (var writer = new BinaryWriter(stream))
            {
                int fmtSize = extension == null ? 16 : 18 + extension.Length;
                int factSize = factFrames > 0 ? 12 : 0;
                int smplSize = loopLength > 0 ? 8 + 36 + 24 : 0;
                writer.Write(new char[] { 'R', 'I', 'F', 'F' });
                writer.Write(4 + 8 + fmtSize + factSize + smplSize + 8 + payload.Length);
                writer.Write(new char[] { 'W', 'A', 'V', 'E' });
                writer.Write(new char[] { 'f', 'm', 't', ' ' });
                writer.Write(fmtSize);
                writer.Write(formatTag);
                writer.Write(channels);
                writer.Write(sampleRate);
                writer.Write(averageBytesPerSecond);
                writer.Write(blockAlign);
                writer.Write(bitsPerSample);
                if (extension != null)
                {
                    writer.Write((ushort)extension.Length);
                    writer.Write(extension);
                }
                if (factFrames > 0)
                {
                    writer.Write(new char[] { 'f', 'a', 'c', 't' });
                    writer.Write(4);
                    writer.Write(factFrames);
                }
                if (loopLength > 0)
                {
                    writer.Write(new char[] { 's', 'm', 'p', 'l' });
                    writer.Write(36 + 24);
                    for (int i = 0; i < 7; i++) writer.Write(0);          // manufacturer .. midi
                    writer.Write(1);                                       // one loop
                    writer.Write(0);                                       // sampler data
                    writer.Write(0);                                       // identifier
                    writer.Write(0);                                       // type: forward
                    writer.Write(loopStart);
                    writer.Write(loopStart + loopLength - 1);
                    writer.Write(0);                                       // fraction
                    writer.Write(0);                                       // play count
                }
                writer.Write(new char[] { 'd', 'a', 't', 'a' });
                writer.Write(payload.Length);
                writer.Write(payload);
            }
            return path;
        }

        private static byte[] Ramp(int count)
        {
            var bytes = new byte[count];
            for (int i = 0; i < count; i++) bytes[i] = (byte)((i * 7 + 13) & 0xFF);
            return bytes;
        }

        private static string Describe(AudioFormat format)
        {
            if (format == null) return "null";
            return "format=" + format.Format + " channels=" + format.ChannelCount + " sampleRate=" +
                   format.SampleRate + " bits=" + format.BitsPerSample + " blockAlign=" + format.BlockAlign +
                   " bytesPerSecond=" + format.AverageBytesPerSecond + " native=" + Hex(format.NativeWaveFormat);
        }

        private static string Describe(AudioContent audio)
        {
            return "fileType=" + audio.FileType + " duration=" +
                   audio.Duration.Ticks.ToString(CultureInfo.InvariantCulture) + " loopStart=" +
                   audio.LoopStart + " loopLength=" + audio.LoopLength + " dataLength=" + audio.Data.Count +
                   " " + Describe(audio.Format);
        }

        private static string Document()
        {
            return "{\n \"producer\": \"Microsoft XNA Game Studio 4.0 Content Pipeline (Microsoft.Xna.Framework.Content.Pipeline.Audio), driven by tools/xna-pipeline-oracle/audio/AudioContentOracle.cs\",\n \"runtime\": \"" +
                   Environment.Version + "\",\n \"pipelineAssembly\": \"" + typeof(AudioContent).Assembly.FullName +
                   "\",\n \"cases\": [\n" + string.Join(",\n", Cases.ToArray()) + "\n ]\n}\n";
        }

        /// The MS-ADPCM fmt extension: samples per block, the seven standard coefficient pairs.
        private static byte[] MsAdpcmExtension(ushort samplesPerBlock)
        {
            short[] coefficients = new short[] { 256, 0, 512, -256, 0, 0, 192, 64, 240, 0, 460, -208, 392, -232 };
            var bytes = new List<byte>();
            bytes.AddRange(BitConverter.GetBytes(samplesPerBlock));
            bytes.AddRange(BitConverter.GetBytes((ushort)7));
            foreach (short value in coefficients) bytes.AddRange(BitConverter.GetBytes(value));
            return bytes.ToArray();
        }

        /// The WAVE_FORMAT_EXTENSIBLE extension: valid bits, channel mask, and the PCM sub-format.
        private static byte[] ExtensibleExtension(ushort validBits, uint channelMask)
        {
            var bytes = new List<byte>();
            bytes.AddRange(BitConverter.GetBytes(validBits));
            bytes.AddRange(BitConverter.GetBytes(channelMask));
            bytes.AddRange(new Guid("00000001-0000-0010-8000-00aa00389b71").ToByteArray());
            return bytes.ToArray();
        }

        /// The least an importer needs: a context that logs nothing and keeps what it is told.
        private sealed class ProbeImporterContext : ContentImporterContext
        {
            private readonly ContentBuildLogger logger = new ProbeLogger();
            public readonly List<string> Dependencies = new List<string>();
            public override string IntermediateDirectory { get { return "obj"; } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return "bin"; } }
            public override void AddDependency(string filename) { Dependencies.Add(filename); }
        }

        /// The least a processor needs: a context that answers, records nothing and builds nothing.
        private sealed class ProbeContext : ContentProcessorContext
        {
            private readonly OpaqueDataDictionary parameters = new OpaqueDataDictionary();
            private readonly ContentBuildLogger logger = new ProbeLogger();
            public override string BuildConfiguration { get { return "Debug"; } }
            public override string IntermediateDirectory { get { return "obj"; } }
            public override ContentBuildLogger Logger { get { return logger; } }
            public override string OutputDirectory { get { return "bin"; } }
            public override string OutputFilename { get { return "asset.xnb"; } }
            public override OpaqueDataDictionary Parameters { get { return parameters; } }
            private readonly TargetPlatform platform = TargetPlatform.Windows;
            private readonly GraphicsProfile profile = GraphicsProfile.HiDef;
            public ProbeContext() { }
            // XNAPP-021: the same probe context aimed at one of XNA's three targets, so a
            // processor that answers differently per platform or profile can be measured doing it.
            public ProbeContext(TargetPlatform targetPlatform, GraphicsProfile targetProfile)
            { platform = targetPlatform; profile = targetProfile; }
            public override TargetPlatform TargetPlatform { get { return platform; } }
            public override GraphicsProfile TargetProfile { get { return profile; } }
            public override void AddDependency(string filename) { }
            public override void AddOutputFile(string filename) { }
            public override TOutput BuildAndLoadAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName)
            { throw new NotSupportedException("BuildAndLoadAsset"); }
            public override ExternalReference<TOutput> BuildAsset<TInput, TOutput>(ExternalReference<TInput> sourceAsset, string processorName, OpaqueDataDictionary processorParameters, string importerName, string assetName)
            { throw new NotSupportedException("BuildAsset"); }
            public override TOutput Convert<TInput, TOutput>(TInput input, string processorName, OpaqueDataDictionary processorParameters)
            { throw new NotSupportedException("Convert"); }
        }

        private sealed class ProbeLogger : ContentBuildLogger
        {
            public override void LogImportantMessage(string message, params object[] messageArgs) { }
            public override void LogMessage(string message, params object[] messageArgs) { }
            public override void LogWarning(string helpLink, ContentIdentity contentIdentity, string message, params object[] messageArgs) { }
        }

        private static int Main(string[] args)
        {
            string outputDirectory = args.Length > 0 ? args[0] : Environment.CurrentDirectory;
            Directory.CreateDirectory(outputDirectory);
            OutputDirectory = outputDirectory;
            string work = Path.Combine(outputDirectory, "work");
            Directory.CreateDirectory(work);

            // ---- the three enumerations ---------------------------------------------------------
            Record("enums/AudioFileType", () =>
            {
                var builder = new StringBuilder();
                foreach (AudioFileType value in Enum.GetValues(typeof(AudioFileType)))
                    builder.Append((builder.Length == 0 ? "" : " ") + value + "=" + (int)value);
                return builder.ToString();
            });
            Record("enums/ConversionFormat", () =>
            {
                var builder = new StringBuilder();
                foreach (ConversionFormat value in Enum.GetValues(typeof(ConversionFormat)))
                    builder.Append((builder.Length == 0 ? "" : " ") + value + "=" + (int)value);
                return builder.ToString();
            });
            Record("enums/ConversionQuality", () =>
            {
                var builder = new StringBuilder();
                foreach (ConversionQuality value in Enum.GetValues(typeof(ConversionQuality)))
                    builder.Append((builder.Length == 0 ? "" : " ") + value + "=" + (int)value);
                return builder.ToString();
            });

            // ---- AudioContent over a WAV of known shape -----------------------------------------
            string mono = WriteWav(work, "mono8k.wav", 8000, 1, 16, 800);
            string stereo = WriteWav(work, "stereo44k.wav", 44100, 2, 16, 4410);
            string eightBit = WriteWav(work, "mono8bit.wav", 22050, 1, 8, 2205);

            Record("audiocontent/mono_pcm16", () =>
            {
                using (var audio = new AudioContent(mono, AudioFileType.Wav))
                {
                    return "fileName=" + Path.GetFileName(audio.FileName) + " " + Describe(audio) +
                           " first16=" + Hex(new List<byte>(new List<byte>(audio.Data).GetRange(0, 16)));
                }
            });
            Record("audiocontent/stereo_pcm16", () =>
            {
                using (var audio = new AudioContent(stereo, AudioFileType.Wav)) { return Describe(audio); }
            });
            Record("audiocontent/mono_pcm8", () =>
            {
                using (var audio = new AudioContent(eightBit, AudioFileType.Wav)) { return Describe(audio); }
            });
            Record("audiocontent/identity_and_name", () =>
            {
                using (var audio = new AudioContent(mono, AudioFileType.Wav))
                {
                    return "name=" + (audio.Name == null ? "null" : "\"" + audio.Name + "\"") +
                           " identity=" + (audio.Identity == null ? "null" : "set") +
                           " opaque=" + audio.OpaqueData.Count +
                           " fileNameIsFullPath=" + Path.IsPathRooted(audio.FileName);
                }
            });

            Record("convert/pcm_from_8bit", () =>
            {
                using (var audio = new AudioContent(eightBit, AudioFileType.Wav))
                {
                    audio.ConvertFormat(ConversionFormat.Pcm, ConversionQuality.Best, null);
                    return Describe(audio);
                }
            });
            Record("convert/pcm_best_first_bytes", () =>
            {
                using (var audio = new AudioContent(mono, AudioFileType.Wav))
                {
                    audio.ConvertFormat(ConversionFormat.Pcm, ConversionQuality.Best, null);
                    var bytes = new List<byte>(audio.Data);
                    return "unchanged=" + Hex(bytes.GetRange(0, 16));
                }
            });

            // ConversionFormat.WindowsMedia and .Xma are deliberately NOT measured here. The first
            // hands the file to the Windows Media encoder, which spins forever under this Wine
            // prefix -- eleven minutes of full-core CPU and a zero-byte output before the run was
            // killed -- and the second needs the Xbox 360 XMA encoder the Windows install does not
            // carry. Both stay EXTERNAL_BLOCKED; see plans/plan_xnapipeline_parity.md XNAPP-161.
            // ---- refusals ------------------------------------------------------------------------
            Record("refusals/missing_file", () =>
            {
                using (var audio = new AudioContent(Path.Combine(work, "absent.wav"), AudioFileType.Wav))
                {
                    return Describe(audio);
                }
            });
            Record("refusals/wrong_file_type", () =>
            {
                using (var audio = new AudioContent(mono, AudioFileType.Mp3)) { return Describe(audio); }
            });
            Record("refusals/null_file_name", () =>
            {
                using (var audio = new AudioContent(null, AudioFileType.Wav)) { return Describe(audio); }
            });
            Record("refusals/not_a_wav", () =>
            {
                string path = Path.Combine(work, "garbage.wav");
                File.WriteAllBytes(path, new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 });
                using (var audio = new AudioContent(path, AudioFileType.Wav)) { return Describe(audio); }
            });
            Record("refusals/after_dispose", () =>
            {
                var audio = new AudioContent(mono, AudioFileType.Wav);
                audio.Dispose();
                var builder = new StringBuilder();
                try { builder.Append("data=" + audio.Data.Count); }
                catch (Exception error) { builder.Append("data=" + error.GetType().Name); }
                try { builder.Append(" duration=" + audio.Duration.Ticks); }
                catch (Exception error) { builder.Append(" duration=" + error.GetType().Name); }
                try { builder.Append(" format=" + (audio.Format == null ? "null" : "set")); }
                catch (Exception error) { builder.Append(" format=" + error.GetType().Name); }
                try { builder.Append(" fileName=" + (audio.FileName == null ? "null" : "set")); }
                catch (Exception error) { builder.Append(" fileName=" + error.GetType().Name); }
                try { audio.ConvertFormat(ConversionFormat.Pcm, ConversionQuality.Best, null); builder.Append(" convert=accepted"); }
                catch (Exception error) { builder.Append(" convert=" + error.GetType().Name); }
                try { audio.Dispose(); builder.Append(" disposeTwice=accepted"); }
                catch (Exception error) { builder.Append(" disposeTwice=" + error.GetType().Name); }
                return builder.ToString();
            });
            Record("refusals/data_is_read_only", () =>
            {
                using (var audio = new AudioContent(mono, AudioFileType.Wav))
                {
                    ReadOnlyCollection<byte> data = audio.Data;
                    return "type=" + data.GetType().Name + " count=" + data.Count;
                }
            });

            // ---- which WAV variants the importer accepts -------------------------------------------
            Record("wav/variants", () =>
            {
                var builder = new StringBuilder();
                Action<string, string> probe = delegate(string label, string path)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try
                    {
                        using (var audio = new AudioContent(path, AudioFileType.Wav))
                        {
                            builder.Append(label + "=[" + Describe(audio) + "]");
                        }
                    }
                    catch (Exception error) { builder.Append(label + "=" + error.GetType().Name); }
                };
                probe("pcm8", WriteWavRaw(work, "v_pcm8.wav", 1, 1, 8000, 8, 1, 8000, null, Ramp(800), 0, 0, 0));
                probe("pcm16", WriteWavRaw(work, "v_pcm16.wav", 1, 1, 8000, 16, 2, 16000, null, Ramp(1600), 0, 0, 0));
                probe("pcm24", WriteWavRaw(work, "v_pcm24.wav", 1, 1, 8000, 24, 3, 24000, null, Ramp(2400), 0, 0, 0));
                probe("pcm32", WriteWavRaw(work, "v_pcm32.wav", 1, 1, 8000, 32, 4, 32000, null, Ramp(3200), 0, 0, 0));
                probe("float32", WriteWavRaw(work, "v_float32.wav", 3, 1, 8000, 32, 4, 32000, new byte[0], Ramp(3200), 0, 0, 800));
                probe("msadpcm", WriteWavRaw(work, "v_msadpcm.wav", 2, 1, 8000, 4, 70, 8000 * 70 / 128,
                                             MsAdpcmExtension(128), Ramp(70 * 6), 0, 0, 6 * 128));
                probe("imaadpcm", WriteWavRaw(work, "v_imaadpcm.wav", 17, 1, 8000, 4, 256, 8000 * 256 / 505,
                                              new byte[] { 0xF9, 0x01 }, Ramp(256 * 4), 0, 0, 4 * 505));
                probe("extensible", WriteWavRaw(work, "v_extensible.wav", 0xFFFE, 2, 44100, 16, 4, 176400,
                                                ExtensibleExtension(16, 3), Ramp(1764), 0, 0, 0));
                probe("loop", WriteWavRaw(work, "v_loop.wav", 1, 1, 8000, 16, 2, 16000, null, Ramp(1600), 100, 200, 0));
                probe("odd_rate", WriteWavRaw(work, "v_oddrate.wav", 1, 2, 12345, 16, 4, 12345 * 4, null, Ramp(1600), 0, 0, 0));
                probe("empty_data", WriteWavRaw(work, "v_empty.wav", 1, 1, 8000, 16, 2, 16000, null, new byte[0], 0, 0, 0));
                return builder.ToString();
            });
            Record("wav/importer", () =>
            {
                string path = WriteWavRaw(work, "i_pcm16.wav", 1, 1, 8000, 16, 2, 16000, null, Ramp(1600), 0, 0, 0);
                var importer = new WavImporter();
                var context = new ProbeImporterContext();
                AudioContent audio = importer.Import(path, context);
                return "type=" + audio.GetType().Name + " dependencies=" + context.Dependencies.Count +
                       " identity=" + (audio.Identity == null ? "null" : Path.GetFileName(audio.Identity.SourceFilename ?? "") + "/" + (audio.Identity.SourceTool ?? "null")) +
                       " name=" + (audio.Name == null ? "null" : "\"" + audio.Name + "\"") + " " + Describe(audio);
            });
            Record("wav/importer_refusals", () =>
            {
                var builder = new StringBuilder();
                Action<string, string> probe = delegate(string label, string path)
                {
                    if (builder.Length > 0) builder.Append(' ');
                    try
                    {
                        var importer = new WavImporter();
                        AudioContent audio = importer.Import(path, new ProbeImporterContext());
                        builder.Append(label + "=" + (audio == null ? "null" : "accepted"));
                    }
                    catch (Exception error) { builder.Append(label + "=" + error.GetType().Name + ": " + error.Message); }
                };
                probe("missing", Path.Combine(work, "no_such.wav"));
                string garbage = Path.Combine(work, "i_garbage.wav");
                File.WriteAllBytes(garbage, new byte[] { 1, 2, 3, 4 });
                probe("garbage", garbage);
                return builder.ToString();
            });

            // ---- the audio and video processors ---------------------------------------------------
            Record("processors/SoundEffectProcessor", () =>
            {
                var processor = new SoundEffectProcessor();
                return "Quality=" + processor.Quality;
            });
            Record("processors/SongProcessor", () =>
            {
                var processor = new SongProcessor();
                return "Quality=" + processor.Quality;
            });
            Record("processors/VideoProcessor", () =>
            {
                var processor = new VideoProcessor();
                return "VideoSoundtrackType=" + processor.VideoSoundtrackType;
            });
            Record("enums/VideoSoundtrackType", () =>
            {
                var builder = new StringBuilder();
                foreach (Microsoft.Xna.Framework.Media.VideoSoundtrackType value in
                         Enum.GetValues(typeof(Microsoft.Xna.Framework.Media.VideoSoundtrackType)))
                    builder.Append((builder.Length == 0 ? "" : " ") + value + "=" + (int)value);
                return builder.ToString();
            });
            Record("soundeffectprocessor/process_best", () =>
            {
                using (var audio = new AudioContent(stereo, AudioFileType.Wav))
                {
                    var processor = new SoundEffectProcessor();
                    processor.Quality = ConversionQuality.Best;
                    SoundEffectContent content = processor.Process(audio, new ProbeContext());
                    return "output=" + (content == null ? "null" : content.GetType().Name) + " input=" + Describe(audio);
                }
            });
            Record("soundeffectprocessor/process_low", () =>
            {
                using (var audio = new AudioContent(stereo, AudioFileType.Wav))
                {
                    var processor = new SoundEffectProcessor();
                    processor.Quality = ConversionQuality.Low;
                    SoundEffectContent content = processor.Process(audio, new ProbeContext());
                    return "output=" + (content == null ? "null" : content.GetType().Name) + " input=" + Describe(audio);
                }
            });
            Record("soundeffectprocessor/process_medium", () =>
            {
                using (var audio = new AudioContent(stereo, AudioFileType.Wav))
                {
                    var processor = new SoundEffectProcessor();
                    processor.Quality = ConversionQuality.Medium;
                    SoundEffectContent content = processor.Process(audio, new ProbeContext());
                    return "output=" + (content == null ? "null" : content.GetType().Name) + " input=" + Describe(audio);
                }
            });
            // XNAPP-021, Phase 13: what a sound effect answers for each of XNA's targets.
            foreach (string leg in new string[] { "Windows/Reach", "Windows/HiDef", "Xbox360/Reach",
                                                 "Xbox360/HiDef", "WindowsPhone/Reach" })
            {
                string captured = leg;
                string[] parts = captured.Split('/');
                TargetPlatform legPlatform = (TargetPlatform)Enum.Parse(typeof(TargetPlatform), parts[0]);
                GraphicsProfile legProfile = (GraphicsProfile)Enum.Parse(typeof(GraphicsProfile), parts[1]);
                Record("soundeffectprofile/" + captured.Replace('/', '_'), () =>
                {
                    using (var audio = new AudioContent(stereo, AudioFileType.Wav))
                    {
                        var processor = new SoundEffectProcessor();
                        SoundEffectContent content = processor.Process(
                            audio, new ProbeContext(legPlatform, legProfile));
                        return "output=" + (content == null ? "null" : content.GetType().Name) +
                               " input=" + Describe(audio);
                    }
                });
            }

            Record("soundeffectprocessor/null_input", () =>
            {
                var processor = new SoundEffectProcessor();
                SoundEffectContent content = processor.Process(null, new ProbeContext());
                return "output=" + (content == null ? "null" : content.GetType().Name);
            });
            Record("soundeffectcontent/members", () =>
            {
                var builder = new StringBuilder();
                foreach (System.Reflection.MemberInfo member in typeof(SoundEffectContent).GetMembers(System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.DeclaredOnly))
                    builder.Append((builder.Length == 0 ? "" : " ") + member.MemberType + ":" + member.Name);
                return builder.Length == 0 ? "none" : builder.ToString();
            });
            Record("songcontent/members", () =>
            {
                var builder = new StringBuilder();
                foreach (System.Reflection.MemberInfo member in typeof(SongContent).GetMembers(System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.DeclaredOnly))
                    builder.Append((builder.Length == 0 ? "" : " ") + member.MemberType + ":" + member.Name);
                return builder.Length == 0 ? "none" : builder.ToString();
            });
            Record("songprocessor/null_input", () =>
            {
                var processor = new SongProcessor();
                SongContent content = processor.Process(null, new ProbeContext());
                return "output=" + (content == null ? "null" : content.GetType().Name);
            });

            // ---- what ConvertFormat does --------------------------------------------------------
            foreach (ConversionQuality quality in new ConversionQuality[]
                     { ConversionQuality.Low, ConversionQuality.Medium, ConversionQuality.Best })
            {
                ConversionQuality captured = quality;
                Record("convert/pcm_" + quality.ToString().ToLowerInvariant(), () =>
                {
                    using (var audio = new AudioContent(stereo, AudioFileType.Wav))
                    {
                        audio.ConvertFormat(ConversionFormat.Pcm, captured, null);
                        return Describe(audio);
                    }
                });
                Record("convert/adpcm_" + quality.ToString().ToLowerInvariant(), () =>
                {
                    using (var audio = new AudioContent(stereo, AudioFileType.Wav))
                    {
                        audio.ConvertFormat(ConversionFormat.Adpcm, captured, null);
                        return Describe(audio);
                    }
                });
            }
            // convert/pcm_mono_rates -- the same three qualities over the 8 kHz mono source -- is
            // not measured: like the Windows Media encoder, that conversion does not return under
            // this Wine prefix. The quality-to-rate rule is read from the stereo cases instead.
            // convert/pcm_twice -- converting an already-converted AudioContent a second time --
            // is not measured either: it does not return under this Wine prefix, the same way the
            // Windows Media encoder does not.
            Publish();
            Console.WriteLine("recorded " + Cases.Count + " measurements");
            return 0;
        }
    }
}
