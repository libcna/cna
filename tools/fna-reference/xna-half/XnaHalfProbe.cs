// SPDX-License-Identifier: MS-PL
using System;
using System.Globalization;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics.PackedVector;
class XnaHalfProbe
{
    static float F(uint bits) { return BitConverter.ToSingle(BitConverter.GetBytes(bits), 0); }
    static void Scalar(string path, float value) {
        Console.WriteLine(path + "\tf:" + BitConverter.ToUInt32(BitConverter.GetBytes(value), 0).ToString("x8"));
    }
    static void Vector(string path, Vector4 v) {
        Scalar(path+"/X",v.X); Scalar(path+"/Y",v.Y); Scalar(path+"/Z",v.Z); Scalar(path+"/W",v.W);
    }
    static void Word(string path, ulong v) { Console.WriteLine(path+"\tw:"+v.ToString("x16")); }
    static void Run<T,S>(string p, Vector4 input, T value, T vector, ulong raw) where T:struct,IPackedVector<S> {
        Word(p+"/constructor",Convert.ToUInt64(value.PackedValue));
        Word(p+"/vectorConstructor",Convert.ToUInt64(vector.PackedValue));
        Vector(p+"/unpack",value.ToVector4()); Vector(p+"/vectorUnpack",vector.ToVector4());
        T packed=new T(); packed.PackFromVector4(input);
        Word(p+"/pack",Convert.ToUInt64(packed.PackedValue)); Vector(p+"/packUnpack",packed.ToVector4());
        packed.PackedValue=(S)Convert.ChangeType(raw,typeof(S));
        Word(p+"/raw",Convert.ToUInt64(packed.PackedValue)); Vector(p+"/rawUnpack",packed.ToVector4());
        Vector4 expanded=packed.ToVector4(); packed.PackFromVector4(expanded);
        Word(p+"/repackedRaw",Convert.ToUInt64(packed.PackedValue));
    }
    static int Main() {
        Console.Error.WriteLine(typeof(HalfSingle).Assembly.FullName);
        Console.Error.WriteLine(typeof(HalfSingle).Assembly.Location);
        { Vector4 input=new Vector4(F(0x00000000U),F(0x00000000U),F(0x00000000U),F(0x00000000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/zero", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x80000000U),F(0x00000000U),F(0x80000000U),F(0x00000000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/signedZero", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000000001UL); }
        { Vector4 input=new Vector4(F(0x3f800000U),F(0x3f800000U),F(0x3f800000U),F(0x3f800000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/one", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x000000000000ffffUL); }
        { Vector4 input=new Vector4(F(0xbf800000U),F(0xbf800000U),F(0xbf800000U),F(0xbf800000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/negative", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000008000UL); }
        { Vector4 input=new Vector4(F(0x3e800000U),F(0x3f000000U),F(0x3f400000U),F(0x3f800000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/quarters", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000007fffUL); }
        { Vector4 input=new Vector4(F(0x3f000000U),F(0x3fc00000U),F(0x40200000U),F(0x40600000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/ties", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000008000UL); }
        { Vector4 input=new Vector4(F(0x3effffffU),F(0x3fbfffffU),F(0x401fffffU),F(0x405fffffU)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/belowTies", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x3f000001U),F(0x3fc00001U),F(0x40200001U),F(0x40600001U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/aboveTies", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000000001UL); }
        { Vector4 input=new Vector4(F(0xbf900000U),F(0xbf800000U),F(0xbe000000U),F(0x00000000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/clampLow", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x000000000000ffffUL); }
        { Vector4 input=new Vector4(F(0x00000000U),F(0x3f600000U),F(0x3f800000U),F(0x3f900000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/clampHigh", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000008000UL); }
        { Vector4 input=new Vector4(F(0xbf000000U),F(0x3f000000U),F(0x437e8000U),F(0x437f8000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/byteBoundary", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000007fffUL); }
        { Vector4 input=new Vector4(F(0xc7000100U),F(0xc7000000U),F(0x46fffe00U),F(0x47000000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/shortBoundary", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000008000UL); }
        { Vector4 input=new Vector4(F(0x3f801000U),F(0x3f803000U),F(0x477fe000U),F(0x477ff000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/halfBoundary", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x33000000U),F(0x33c00000U),F(0x33800000U),F(0x387fc000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/halfTiny", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000000001UL); }
        { Vector4 input=new Vector4(F(0x7fc00000U),F(0x7f800000U),F(0xff800000U),F(0x80000000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/nonfinite", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x000000000000ffffUL); }
        { Vector4 input=new Vector4(F(0xbf480000U),F(0xbe100000U),F(0x3fb00000U),F(0x3ec80000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/seeded0", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000008000UL); }
        { Vector4 input=new Vector4(F(0xbfae0000U),F(0x3fd40000U),F(0xbc800000U),F(0x3fa20000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/seeded1", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000007fffUL); }
        { Vector4 input=new Vector4(F(0x3ec80000U),F(0xbc800000U),F(0x3dc00000U),F(0x3faa0000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/seeded2", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000008000UL); }
        { Vector4 input=new Vector4(F(0xbfe20000U),F(0xbe000000U),F(0x3f100000U),F(0xbf580000U)); Run<HalfSingle,ushort>("/PackingEdges/packedTypes/HalfSingle/seeded3", input, new HalfSingle(input.X), new HalfSingle(input.X), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x00000000U),F(0x00000000U),F(0x00000000U),F(0x00000000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/zero", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x80000000U),F(0x00000000U),F(0x80000000U),F(0x00000000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/signedZero", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000000000001UL); }
        { Vector4 input=new Vector4(F(0x3f800000U),F(0x3f800000U),F(0x3f800000U),F(0x3f800000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/one", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x00000000ffffffffUL); }
        { Vector4 input=new Vector4(F(0xbf800000U),F(0xbf800000U),F(0xbf800000U),F(0xbf800000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/negative", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000080008000UL); }
        { Vector4 input=new Vector4(F(0x3e800000U),F(0x3f000000U),F(0x3f400000U),F(0x3f800000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/quarters", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x000000007fff7fffUL); }
        { Vector4 input=new Vector4(F(0x3f000000U),F(0x3fc00000U),F(0x40200000U),F(0x40600000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/ties", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x00000000fc008000UL); }
        { Vector4 input=new Vector4(F(0x3effffffU),F(0x3fbfffffU),F(0x401fffffU),F(0x405fffffU)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/belowTies", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x3f000001U),F(0x3fc00001U),F(0x40200001U),F(0x40600001U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/aboveTies", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000000000001UL); }
        { Vector4 input=new Vector4(F(0xbf900000U),F(0xbf800000U),F(0xbe000000U),F(0x00000000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/clampLow", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x00000000ffffffffUL); }
        { Vector4 input=new Vector4(F(0x00000000U),F(0x3f600000U),F(0x3f800000U),F(0x3f900000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/clampHigh", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000080008000UL); }
        { Vector4 input=new Vector4(F(0xbf000000U),F(0x3f000000U),F(0x437e8000U),F(0x437f8000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/byteBoundary", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x000000007fff7fffUL); }
        { Vector4 input=new Vector4(F(0xc7000100U),F(0xc7000000U),F(0x46fffe00U),F(0x47000000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/shortBoundary", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x00000000fc008000UL); }
        { Vector4 input=new Vector4(F(0x3f801000U),F(0x3f803000U),F(0x477fe000U),F(0x477ff000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/halfBoundary", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x33000000U),F(0x33c00000U),F(0x33800000U),F(0x387fc000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/halfTiny", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000000000001UL); }
        { Vector4 input=new Vector4(F(0x7fc00000U),F(0x7f800000U),F(0xff800000U),F(0x80000000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/nonfinite", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x00000000ffffffffUL); }
        { Vector4 input=new Vector4(F(0xbf480000U),F(0xbe100000U),F(0x3fb00000U),F(0x3ec80000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/seeded0", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000080008000UL); }
        { Vector4 input=new Vector4(F(0xbfae0000U),F(0x3fd40000U),F(0xbc800000U),F(0x3fa20000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/seeded1", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x000000007fff7fffUL); }
        { Vector4 input=new Vector4(F(0x3ec80000U),F(0xbc800000U),F(0x3dc00000U),F(0x3faa0000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/seeded2", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x00000000fc008000UL); }
        { Vector4 input=new Vector4(F(0xbfe20000U),F(0xbe000000U),F(0x3f100000U),F(0xbf580000U)); Run<HalfVector2,uint>("/PackingEdges/packedTypes/HalfVector2/seeded3", input, new HalfVector2(input.X,input.Y), new HalfVector2(new Vector2(input.X,input.Y)), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x00000000U),F(0x00000000U),F(0x00000000U),F(0x00000000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/zero", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x80000000U),F(0x00000000U),F(0x80000000U),F(0x00000000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/signedZero", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x0000000000000001UL); }
        { Vector4 input=new Vector4(F(0x3f800000U),F(0x3f800000U),F(0x3f800000U),F(0x3f800000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/one", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0xffffffffffffffffUL); }
        { Vector4 input=new Vector4(F(0xbf800000U),F(0xbf800000U),F(0xbf800000U),F(0xbf800000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/negative", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x8000800080008000UL); }
        { Vector4 input=new Vector4(F(0x3e800000U),F(0x3f000000U),F(0x3f400000U),F(0x3f800000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/quarters", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x7fff7fff7fff7fffUL); }
        { Vector4 input=new Vector4(F(0x3f000000U),F(0x3fc00000U),F(0x40200000U),F(0x40600000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/ties", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x7e007c00fc008000UL); }
        { Vector4 input=new Vector4(F(0x3effffffU),F(0x3fbfffffU),F(0x401fffffU),F(0x405fffffU)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/belowTies", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x3f000001U),F(0x3fc00001U),F(0x40200001U),F(0x40600001U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/aboveTies", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x0000000000000001UL); }
        { Vector4 input=new Vector4(F(0xbf900000U),F(0xbf800000U),F(0xbe000000U),F(0x00000000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/clampLow", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0xffffffffffffffffUL); }
        { Vector4 input=new Vector4(F(0x00000000U),F(0x3f600000U),F(0x3f800000U),F(0x3f900000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/clampHigh", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x8000800080008000UL); }
        { Vector4 input=new Vector4(F(0xbf000000U),F(0x3f000000U),F(0x437e8000U),F(0x437f8000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/byteBoundary", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x7fff7fff7fff7fffUL); }
        { Vector4 input=new Vector4(F(0xc7000100U),F(0xc7000000U),F(0x46fffe00U),F(0x47000000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/shortBoundary", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x7e007c00fc008000UL); }
        { Vector4 input=new Vector4(F(0x3f801000U),F(0x3f803000U),F(0x477fe000U),F(0x477ff000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/halfBoundary", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x0000000000000000UL); }
        { Vector4 input=new Vector4(F(0x33000000U),F(0x33c00000U),F(0x33800000U),F(0x387fc000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/halfTiny", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x0000000000000001UL); }
        { Vector4 input=new Vector4(F(0x7fc00000U),F(0x7f800000U),F(0xff800000U),F(0x80000000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/nonfinite", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0xffffffffffffffffUL); }
        { Vector4 input=new Vector4(F(0xbf480000U),F(0xbe100000U),F(0x3fb00000U),F(0x3ec80000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/seeded0", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x8000800080008000UL); }
        { Vector4 input=new Vector4(F(0xbfae0000U),F(0x3fd40000U),F(0xbc800000U),F(0x3fa20000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/seeded1", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x7fff7fff7fff7fffUL); }
        { Vector4 input=new Vector4(F(0x3ec80000U),F(0xbc800000U),F(0x3dc00000U),F(0x3faa0000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/seeded2", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x7e007c00fc008000UL); }
        { Vector4 input=new Vector4(F(0xbfe20000U),F(0xbe000000U),F(0x3f100000U),F(0xbf580000U)); Run<HalfVector4,ulong>("/PackingEdges/packedTypes/HalfVector4/seeded3", input, new HalfVector4(input.X,input.Y,input.Z,input.W), new HalfVector4(input), 0x0000000000000000UL); }
        return 0;
    }
}
