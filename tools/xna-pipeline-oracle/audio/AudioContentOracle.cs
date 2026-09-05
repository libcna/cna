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
using Microsoft.Xna.Framework.Content.Pipeline.Audio;

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
