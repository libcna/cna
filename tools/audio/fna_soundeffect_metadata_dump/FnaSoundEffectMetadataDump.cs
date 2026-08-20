// plans/plan_audio.md AUD-06-021: standalone differential-testing tool, NOT part of the CMake build.
//
// Parses a real, uncompressed .xnb file's SoundEffectReader body using FNA's own real field-
// reading logic (the exact same order/types as FNA's Content/ContentReaders/SoundEffectReader.cs
// and the type-reader-table preamble logic in Content/ContentReader.cs /
// Content/ContentTypeReaderManager.cs -- reusing .NET's own BinaryReader.Read7BitEncodedInt() /
// ReadString() for the preamble exactly like FNA's real ContentReader does, not a reimplementation
// of them -- MiniReader below subclasses BinaryReader for the same reason FNA's real
// ContentReader does: Read7BitEncodedInt() is `protected`), and prints every WAVEFORMATEX field
// plus loop points and stored duration as "key=value" lines to stdout.
//
// This is a genuine differential-testing tool: it runs FNA's actual parsing logic (via mono, a
// completely independent C# runtime/toolchain from CNA's C++/SDL3 stack) against the same real
// fixture bytes CNA's own SoundEffectContentTypeReaderTests.cpp loads, so the two field sets are
// produced by two independent implementations reading the same file, not one implementation
// checked against a hand-authored expectation. It intentionally does NOT depend on FAudio (no
// actual audio decode/construction) -- see plans/plan_audio.md AUD-06-021 for why decoded-*sample*
// differential testing is a documented, separate limitation in this environment.
//
// Build:  mcs FnaSoundEffectMetadataDump.cs -out:FnaSoundEffectMetadataDump.exe
// Run:    mono FnaSoundEffectMetadataDump.exe <path-to.xnb>

using System;
using System.IO;

internal sealed class MiniReader : BinaryReader
{
    public MiniReader(Stream input) : base(input) { }
    public int Read7Bit() { return Read7BitEncodedInt(); }
}

internal static class Program
{
    // FNA's SoundEffectReader.cs Swap() methods, copied verbatim (MS-PL permits this -- same
    // license as CNA itself).
    private static ushort Swap(bool swap, ushort x)
    {
        return !swap ? x : (ushort)(
            ((x >> 8) & 0x00FF) |
            ((x << 8) & 0xFF00)
        );
    }

    private static uint Swap(bool swap, uint x)
    {
        return !swap ? x : (
            ((x >> 24) & 0x000000FF) |
            ((x >> 8) & 0x0000FF00) |
            ((x << 8) & 0x00FF0000) |
            ((x << 24) & 0xFF000000)
        );
    }

    private static int Main(string[] args)
    {
        if (args.Length != 1)
        {
            Console.Error.WriteLine("usage: FnaSoundEffectMetadataDump.exe <path-to.xnb>");
            return 2;
        }

        using (FileStream file = new FileStream(args[0], FileMode.Open, FileAccess.Read))
        using (MiniReader reader = new MiniReader(file))
        {
            // --- Real XNB header (matches ContentManager.ReadAsset's header parsing) ---
            byte x = reader.ReadByte();
            byte n = reader.ReadByte();
            byte b = reader.ReadByte();
            char platform = (char)reader.ReadByte();
            byte version = reader.ReadByte();
            byte flags = reader.ReadByte();
            uint totalLength = reader.ReadUInt32();
            if (x != 'X' || n != 'N' || b != 'B')
            {
                Console.Error.WriteLine("not an XNB file");
                return 1;
            }
            bool compressed = (flags & 0x80) != 0 || (flags & 0x40) != 0;
            if (compressed)
            {
                Console.Error.WriteLine("compressed .xnb not supported by this tool (AUD-06-021 scope: uncompressed fixtures only)");
                return 1;
            }

            // --- Type-reader-table preamble (ContentTypeReaderManager.LoadAssetReaders, real
            // .NET BinaryReader methods, not reimplemented) ---
            int numberOfReaders = reader.Read7Bit();
            string readerTypeString = null;
            for (int i = 0; i < numberOfReaders; i += 1)
            {
                readerTypeString = reader.ReadString();
                reader.ReadInt32(); // reader version number
            }
            int sharedResourceCount = reader.Read7Bit();
            // ReadObject<T>: reads a 7-bit-encoded typeReaderIndex; 0 means null, else index+1
            // into typeReaders (real ContentReader.ReadObjectInternal logic).
            int typeReaderIndex = reader.Read7Bit();

            Console.WriteLine("platform=" + platform);
            Console.WriteLine("version=" + version);
            Console.WriteLine("totalLength=" + totalLength);
            Console.WriteLine("readerTypeString=" + readerTypeString);
            Console.WriteLine("sharedResourceCount=" + sharedResourceCount);
            Console.WriteLine("typeReaderIndex=" + typeReaderIndex);

            // --- SoundEffectReader.Read(), copied verbatim (field order/types/swap logic) ---
            bool se = platform == 'x';

            uint formatLength = reader.ReadUInt32();
            ushort wFormatTag = Swap(se, reader.ReadUInt16());
            ushort nChannels = Swap(se, reader.ReadUInt16());
            uint nSamplesPerSec = Swap(se, reader.ReadUInt32());
            uint nAvgBytesPerSec = Swap(se, reader.ReadUInt32());
            ushort nBlockAlign = Swap(se, reader.ReadUInt16());
            ushort wBitsPerSample = Swap(se, reader.ReadUInt16());

            if (formatLength > 16)
            {
                reader.ReadUInt16(); // cbSize (not swapped in FNA's real branch either)
                reader.ReadBytes((int)(formatLength - 18));
            }

            byte[] data = reader.ReadBytes(reader.ReadInt32());

            int loopStart = reader.ReadInt32();
            int loopLength = reader.ReadInt32();
            uint durationMs = reader.ReadUInt32();

            Console.WriteLine("formatLength=" + formatLength);
            Console.WriteLine("wFormatTag=" + wFormatTag);
            Console.WriteLine("nChannels=" + nChannels);
            Console.WriteLine("nSamplesPerSec=" + nSamplesPerSec);
            Console.WriteLine("nAvgBytesPerSec=" + nAvgBytesPerSec);
            Console.WriteLine("nBlockAlign=" + nBlockAlign);
            Console.WriteLine("wBitsPerSample=" + wBitsPerSample);
            Console.WriteLine("dataLength=" + data.Length);
            Console.WriteLine("loopStart=" + loopStart);
            Console.WriteLine("loopLength=" + loopLength);
            Console.WriteLine("durationMs=" + durationMs);
        }

        return 0;
    }
}
