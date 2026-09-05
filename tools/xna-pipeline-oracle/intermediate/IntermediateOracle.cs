// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-074: the black-box oracle for XNA 4.0's intermediate XML
// serialization.
//
// WHAT THIS IS
//
// A program that hands a corpus of CNA-authored .NET types and values to the genuine
// Microsoft.Xna.Framework.Content.Pipeline IntermediateSerializer and records exactly what it
// writes, and what it accepts back. The XML files it produces are the specification CNA's C++
// IntermediateSerializer is measured against: every element name, attribute, type spelling,
// number format, shared-resource and external-reference convention comes from here, not from
// memory or from any other implementation's source.
//
// It observes behaviour only: it calls public API (IntermediateSerializer.Serialize/Deserialize,
// the ContentSerializer* attributes) on types this file defines. No method body of any Microsoft
// assembly is read. Run it with run-intermediate-oracle.sh (mcs + Wine .NET 4.0); the output goes
// to tests/reference/xna40/intermediate/.
//
// Usage: IntermediateOracle.exe <output-dir>

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Xml;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content;
using Microsoft.Xna.Framework.Content.Pipeline;
using Microsoft.Xna.Framework.Content.Pipeline.Graphics;
using Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate;

namespace Cna.Xna40.IntermediateOracle
{
    // ---- corpus types -----------------------------------------------------------------------

    public class Primitives
    {
        public bool Bool = true;
        public byte Byte = 200;
        public sbyte SByte = -5;
        public short Short = -30000;
        public ushort UShort = 60000;
        public int Int = -123456789;
        public uint UInt = 4000000000u;
        public long Long = -9000000000000000000L;
        public ulong ULong = 18000000000000000000UL;
        public float Float = 1.5f;
        public double Double = 2.25;
        public char Char = 'x';
        public string String = "hello world";
        public decimal Decimal = 12.34m;
        public TimeSpan TimeSpan = TimeSpan.FromMilliseconds(1500);
        public DateTime DateTime = new DateTime(2010, 9, 16, 12, 30, 45, DateTimeKind.Utc);
    }

    public class FloatEdges
    {
        public float NegativeZero = -0.0f;
        public float NaN = float.NaN;
        public float PositiveInfinity = float.PositiveInfinity;
        public float NegativeInfinity = float.NegativeInfinity;
        public float Tiny = 1.17549435E-38f;
        public float Precise = 0.1f;
        public float Third = 1.0f / 3.0f;
        public double DoublePrecise = 0.1;
        public double DoubleThird = 1.0 / 3.0;
        public float Large = 16777217f;
        public float Whole = 42f;
    }

    public class StringEdges
    {
        public string Empty = "";
        public string Null = null;
        public string Spaces = "  padded  ";
        public string Newlines = "line1\nline2\r\nline3";
        public string Escapes = "<tag> & \"quotes\" 'apostrophes'";
        public string Unicode = "caf\u00e9 \u4e2d\u6587 \U0001F600";
        public char Tab = '\t';
        public char Lt = '<';
        public char Amp = '&';
        public char Space = ' ';
    }

    public class NulCharacter
    {
        public char Nul = '\0';
    }

    public class MathTypes
    {
        public Vector2 Vector2 = new Vector2(1.5f, -2.5f);
        public Vector3 Vector3 = new Vector3(1, 2, 3);
        public Vector4 Vector4 = new Vector4(0.1f, 0.2f, 0.3f, 0.4f);
        public Matrix Matrix = Matrix.CreateTranslation(1, 2, 3);
        public Quaternion Quaternion = new Quaternion(0, 0.7071068f, 0, 0.7071068f);
        public Color Color = new Color(10, 20, 30, 40);
        public Rectangle Rectangle = new Rectangle(1, 2, 30, 40);
        public Point Point = new Point(-7, 8);
        public BoundingBox BoundingBox = new BoundingBox(new Vector3(-1, -2, -3), new Vector3(1, 2, 3));
        public BoundingSphere BoundingSphere = new BoundingSphere(new Vector3(1, 1, 1), 5);
        public Plane Plane = new Plane(new Vector3(0, 1, 0), 3);
        public Ray Ray = new Ray(new Vector3(0, 0, 0), new Vector3(0, 0, 1));
        public Curve Curve = MakeCurve();

        private static Curve MakeCurve()
        {
            var curve = new Curve();
            curve.PreLoop = CurveLoopType.Constant;
            curve.PostLoop = CurveLoopType.Linear;
            curve.Keys.Add(new CurveKey(0, 0, 0, 1, CurveContinuity.Smooth));
            curve.Keys.Add(new CurveKey(1, 2, 1, 0, CurveContinuity.Step));
            return curve;
        }
    }

    public enum Mood { Happy, Sad, Angry }

    [Flags]
    public enum Toppings { None = 0, Cheese = 1, Ham = 2, Olives = 4 }

    public class Enums
    {
        public Mood Mood = Mood.Sad;
        public Toppings One = Toppings.Ham;
        public Toppings Several = Toppings.Cheese | Toppings.Olives;
        public Toppings Zero = Toppings.None;
        public CurveLoopType FrameworkEnum = CurveLoopType.Oscillate;
    }

    public class Nested
    {
        public string Name = "nested";
        public int Value = 7;
    }

    public struct Vertex
    {
        public Vector3 Position;
        public float Weight;
    }

    public class Collections
    {
        public int[] IntArray = { 1, 2, 3 };
        public string[] StringArray = { "a", "b" };
        public float[] FloatArray = { 0.5f, 1.5f };
        public Vector3[] Vector3Array = { new Vector3(1, 2, 3), new Vector3(4, 5, 6) };
        public byte[] Bytes = { 0, 127, 255 };
        public List<int> IntList = new List<int> { 4, 5, 6 };
        public List<string> StringList = new List<string> { "x", "y" };
        public List<Vector3> Vector3List = new List<Vector3> { new Vector3(7, 8, 9) };
        public List<Nested> NestedList = new List<Nested> { new Nested(), new Nested { Name = "second", Value = 8 } };
        public List<Vertex> StructList = new List<Vertex> { new Vertex { Position = new Vector3(1, 1, 1), Weight = 0.25f } };
        public Dictionary<string, int> StringIntMap = new Dictionary<string, int> { { "one", 1 }, { "two", 2 } };
        public Dictionary<int, string> IntStringMap = new Dictionary<int, string> { { 10, "ten" } };
        public Dictionary<string, Nested> NestedMap = new Dictionary<string, Nested> { { "k", new Nested() } };
        public List<List<int>> ListOfLists = new List<List<int>> { new List<int> { 1 }, new List<int> { 2, 3 } };
        public Dictionary<string, List<int>> MapOfLists = new Dictionary<string, List<int>> { { "a", new List<int> { 9 } } };
        public int[] EmptyArray = new int[0];
        public List<int> EmptyList = new List<int>();
        public List<string> ListWithNull = new List<string> { "present", null };
        public List<int> NullList = null;
    }

    public class Nullables
    {
        public int? HasValue = 5;
        public int? NoValue = null;
        public Vector3? Vector = new Vector3(1, 2, 3);
        public Mood? Enum = Mood.Angry;
        public List<int?> ListOfNullable = new List<int?> { 1, null, 3 };
    }

    public class Animal
    {
        public string Name = "generic";
    }

    public class Dog : Animal
    {
        public int Tricks = 3;
    }

    public class Cat : Animal
    {
        public bool Indoor = true;
    }

    public abstract class Shape
    {
        public float Area = 1;
    }

    public class Circle : Shape
    {
        public float Radius = 2;
    }

    public class Polymorphism
    {
        public Animal DeclaredBase = new Dog { Name = "rex" };
        public Animal ExactType = new Animal { Name = "plain" };
        public Animal NullAnimal = null;
        public Shape ViaAbstract = new Circle();
        public object BoxedInt = 42;
        public object BoxedVector = new Vector3(1, 2, 3);
        public object BoxedString = "boxed";
        public object BoxedNested = new Nested();
        public object NullObject = null;
        public List<Animal> Mixed = new List<Animal> { new Dog(), new Cat(), new Animal() };
        public object[] Objects = { 1, "two", new Vector2(3, 3), null };
    }

    public class Referenced
    {
        public string Label = "shared";
        public int Count = 2;
    }

    public class SharedResources
    {
        [ContentSerializer(SharedResource = true)]
        public Referenced First;

        [ContentSerializer(SharedResource = true)]
        public Referenced Second;

        [ContentSerializer(SharedResource = true)]
        public Referenced Other;

        [ContentSerializer(SharedResource = true)]
        public Referenced NullShared;

        [ContentSerializer(SharedResource = true)]
        public List<Referenced> SharedList;

        public Referenced Inline;

        public SharedResources()
        {
            var one = new Referenced();
            First = one;
            Second = one;
            Other = new Referenced { Label = "other", Count = 3 };
            NullShared = null;
            SharedList = new List<Referenced> { one, Other };
            Inline = new Referenced { Label = "inline", Count = 1 };
        }
    }

    public class ExternalReferences
    {
        public ExternalReference<Texture2DContent> Texture = new ExternalReference<Texture2DContent>("Textures/wall.png");
        public ExternalReference<Texture2DContent> Again = new ExternalReference<Texture2DContent>("Textures/wall.png");
        public ExternalReference<Texture2DContent> Other = new ExternalReference<Texture2DContent>(@"..\Shared\other.dds");
        public ExternalReference<Texture2DContent> Null = null;
        public List<ExternalReference<Texture2DContent>> List = new List<ExternalReference<Texture2DContent>>
        {
            new ExternalReference<Texture2DContent>("Textures/a.png"),
            new ExternalReference<Texture2DContent>("Textures/b.png")
        };
    }

    [ContentSerializerCollectionItemName("Entry")]
    public class NamedItemCollection : List<int>
    {
    }

    public class PackedCollections
    {
        public List<bool> Bools = new List<bool> { true, false };
        public List<Mood> Enums = new List<Mood> { Mood.Happy, Mood.Sad };
        public List<Toppings> Flags = new List<Toppings> { Toppings.Cheese | Toppings.Olives, Toppings.None };
        public List<Color> Colors = new List<Color> { Color.Red, Color.CornflowerBlue };
        public List<TimeSpan> Spans = new List<TimeSpan> { TimeSpan.FromSeconds(1.5), TimeSpan.FromMinutes(2) };
        public List<DateTime> Dates = new List<DateTime> { new DateTime(2010, 9, 16, 12, 30, 45, DateTimeKind.Utc) };
        public List<decimal> Decimals = new List<decimal> { 1.5m, 2m };
        public List<double> Doubles = new List<double> { 0.1, 1e300 };
        public List<long> Longs = new List<long> { -1L, long.MaxValue };
        public List<ulong> Ulongs = new List<ulong> { ulong.MaxValue };
        public List<short> Shorts = new List<short> { -3, 3 };
        public List<ushort> Ushorts = new List<ushort> { 65535 };
        public List<sbyte> Sbytes = new List<sbyte> { -128, 127 };
        public List<uint> Uints = new List<uint> { 4000000000u };
        public List<Vector2> Vector2s = new List<Vector2> { new Vector2(1, 2), new Vector2(3, 4) };
        public List<Vector4> Vector4s = new List<Vector4> { new Vector4(1, 2, 3, 4) };
        public List<Quaternion> Quaternions = new List<Quaternion> { Quaternion.Identity };
        public List<Rectangle> Rectangles = new List<Rectangle> { new Rectangle(1, 2, 3, 4), new Rectangle(5, 6, 7, 8) };
        public List<Point> Points = new List<Point> { new Point(1, 2) };
        public List<Matrix> Matrices = new List<Matrix> { Matrix.Identity };
        public List<Plane> Planes = new List<Plane> { new Plane(0, 1, 0, 3) };
        public List<BoundingBox> Boxes = new List<BoundingBox> { new BoundingBox(Vector3.Zero, Vector3.One) };
        public List<BoundingSphere> Spheres = new List<BoundingSphere> { new BoundingSphere(Vector3.Zero, 2) };
        public List<Ray> Rays = new List<Ray> { new Ray(Vector3.Zero, Vector3.UnitZ) };
        public List<Curve> Curves = new List<Curve> { new Curve() };
        public List<Vector3?> NullableVectors = new List<Vector3?> { Vector3.One, null };
        public List<object> BoxedPrimitives = new List<object> { 1L, (short)2, (byte)3, 'c', 1.5m, 2.5, true, Mood.Sad, TimeSpan.FromSeconds(1), (ulong)4, (ushort)5, (sbyte)6, (uint)7, 8f };
        public Dictionary<int, int> IntIntMap = new Dictionary<int, int> { { 1, 2 } };
        public Dictionary<Mood, Vector3> EnumVectorMap = new Dictionary<Mood, Vector3> { { Mood.Happy, Vector3.One } };
        public bool[] BoolArray = { false, true };
        public char[] CharArray = { 'a', 'b' };
        public string[] StringWithSpaces = { "a b", " c " };
    }

    public class Both
    {
        [ContentSerializer(SharedResource = true)]
        public Referenced Shared = new Referenced();

        public ExternalReference<Texture2DContent> Texture = new ExternalReference<Texture2DContent>("Textures/wall.png");
    }

    public class Node
    {
        public string Name = "node";

        [ContentSerializer(SharedResource = true)]
        public Node Next;
    }

    public class Attributes
    {
        [ContentSerializer(ElementName = "Renamed")]
        public int Original = 1;

        [ContentSerializer(Optional = true)]
        public string OptionalPresent = "here";

        [ContentSerializer(Optional = true)]
        public string OptionalNull = null;

        [ContentSerializer(Optional = true)]
        public int OptionalDefault = 0;

        [ContentSerializer(AllowNull = false)]
        public string NeverNull = "value";

        [ContentSerializer(FlattenContent = true)]
        public Nested Flattened = new Nested();

        [ContentSerializer(FlattenContent = true)]
        public List<int> FlattenedList = new List<int> { 1, 2 };

        [ContentSerializer(CollectionItemName = "Number")]
        public List<int> RenamedItems = new List<int> { 3, 4 };

        [ContentSerializer(FlattenContent = true, CollectionItemName = "Loose")]
        public List<int> FlattenedRenamed = new List<int> { 5, 6 };

        [ContentSerializerIgnore]
        public int Ignored = 99;

        public NamedItemCollection Named = new NamedItemCollection { 7, 8 };

        public int PublicProperty { get; set; }

        public int ReadOnlyProperty { get { return 5; } }

        public List<int> GetOnlyList { get { return getOnlyList; } }
        private readonly List<int> getOnlyList = new List<int> { 11, 12 };

        private int privateField = 3;
        internal int internalField = 4;
        protected int protectedField = 5;
        public static int StaticField = 6;
        public const int Constant = 7;
        public readonly int ReadOnlyField = 8;

        public Attributes()
        {
            PublicProperty = 2;
            if (privateField + internalField + protectedField == 0) { StaticField = 0; }
        }
    }

    public class Runtime
    {
        public Vector3 Value = Vector3.One;
    }

    [ContentSerializerRuntimeType("MyGame.RuntimeShape, MyGame")]
    public class WithRuntimeType
    {
        public int Sides = 3;
    }

    [ContentSerializerTypeVersion(4)]
    public class WithVersion
    {
        public int Field = 1;
    }

    public class Deep
    {
        public Deep Child;
        public int Depth;
        public static Deep Build(int levels)
        {
            var root = new Deep { Depth = 0 };
            Deep current = root;
            for (int i = 1; i < levels; i++)
            {
                current.Child = new Deep { Depth = i };
                current = current.Child;
            }
            return root;
        }
    }

    public class ArraysOfArrays
    {
        public int[][] Jagged = { new[] { 1, 2 }, new[] { 3 } };
        public Vector2[] Empty = new Vector2[0];
    }

    public class Rectangular
    {
        public int[,] Grid = { { 1, 2 }, { 3, 4 } };
    }

    // ---- driver -----------------------------------------------------------------------------

    internal static class Program
    {
        private static readonly List<string> Manifest = new List<string>();

        private static string Escape(string text)
        {
            return text.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\n", "\\n");
        }

        private static void Record(string name, string rootType, string status, string note)
        {
            Manifest.Add("  {\"case\": \"" + name + "\", \"rootType\": \"" + Escape(rootType) + "\", \"status\": \"" + status +
                         "\", \"note\": \"" + Escape(note ?? "") + "\"}");
        }

        private static void Serialize<T>(string directory, string name, T value, string referenceRelocationPath)
        {
            string path = Path.Combine(directory, name + ".xml");
            string status = "written";
            string note = "";
            try
            {
                var settings = new XmlWriterSettings { Indent = true, Encoding = new UTF8Encoding(false) };
                using (var writer = XmlWriter.Create(path, settings))
                {
                    IntermediateSerializer.Serialize(writer, value, referenceRelocationPath);
                }
                // Round trip: the genuine deserializer must accept what the genuine serializer wrote,
                // and re-serializing must reproduce the same text.
                T back;
                using (var reader = XmlReader.Create(path))
                {
                    back = IntermediateSerializer.Deserialize<T>(reader, referenceRelocationPath);
                }
                string again;
                using (var stringWriter = new StringWriter())
                {
                    using (var writer = XmlWriter.Create(stringWriter, settings))
                    {
                        IntermediateSerializer.Serialize(writer, back, referenceRelocationPath);
                    }
                    again = stringWriter.ToString();
                }
                // The re-serialization goes through a StringWriter, whose XML declaration says utf-16;
                // compare the documents without their declarations.
                string first = StripDeclaration(File.ReadAllText(path));
                if (first != StripDeclaration(again))
                {
                    status = "round-trip-differs";
                    File.WriteAllText(Path.Combine(directory, name + ".roundtrip.xml"), again);
                    note = "re-serializing the deserialized object produced different text; both files kept";
                }
            }
            catch (Exception error)
            {
                status = "failed";
                note = error.GetType().Name + ": " + error.Message;
                File.WriteAllText(Path.Combine(directory, name + ".error.txt"), error.ToString());
            }
            Record(name, typeof(T).AssemblyQualifiedName, status, note);
            Console.WriteLine(status.PadRight(20) + name + (note.Length > 0 ? "  -- " + note : ""));
        }

        private static string StripDeclaration(string xml)
        {
            xml = xml.TrimStart('\uFEFF');
            if (xml.StartsWith("<?xml"))
            {
                int end = xml.IndexOf("?>", StringComparison.Ordinal);
                if (end >= 0) { xml = xml.Substring(end + 2).TrimStart('\r', '\n'); }
            }
            return xml;
        }

        private static void Accept<T>(string directory, string name, string xml)
        {
            Accept<T>(directory, name, xml, null);
        }

        private static void Accept<T>(string directory, string name, string xml, string referenceRelocationPath)
        {
            string status;
            string note = "";
            try
            {
                using (var reader = XmlReader.Create(new StringReader(xml)))
                {
                    T value = IntermediateSerializer.Deserialize<T>(reader, referenceRelocationPath);
                    status = "accepted";
                    var settings = new XmlWriterSettings { Indent = true, Encoding = new UTF8Encoding(false) };
                    using (var writer = XmlWriter.Create(Path.Combine(directory, name + ".normalized.xml"), settings))
                    {
                        IntermediateSerializer.Serialize(writer, value, referenceRelocationPath);
                    }
                }
            }
            catch (Exception error)
            {
                status = "rejected";
                note = error.GetType().Name + ": " + error.Message;
            }
            File.WriteAllText(Path.Combine(directory, name + ".input.xml"), xml);
            Record(name, typeof(T).AssemblyQualifiedName, status, note);
            Console.WriteLine(status.PadRight(20) + name + (note.Length > 0 ? "  -- " + note : ""));
        }

        private static int Main(string[] args)
        {
            if (args.Length != 1)
            {
                Console.Error.WriteLine("usage: IntermediateOracle.exe <output-dir>");
                return 2;
            }
            string directory = args[0];
            Directory.CreateDirectory(directory);
            System.Threading.Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;

            Serialize(directory, "primitives", new Primitives(), null);
            Serialize(directory, "float_edges", new FloatEdges(), null);
            Serialize(directory, "string_edges", new StringEdges(), null);
            Serialize(directory, "nul_character", new NulCharacter(), null);
            Serialize(directory, "math_types", new MathTypes(), null);
            Serialize(directory, "enums", new Enums(), null);
            Serialize(directory, "collections", new Collections(), null);
            Serialize(directory, "nullables", new Nullables(), null);
            Serialize(directory, "polymorphism", new Polymorphism(), null);
            Serialize(directory, "shared_resources", new SharedResources(), null);
            Serialize(directory, "external_references", new ExternalReferences(), @"C:\Content\Levels\level.xml");
            Serialize(directory, "external_references_norelocation", new ExternalReferences(), null);
            Serialize(directory, "external_references_samedrive", new ExternalReferences(), Path.Combine(Environment.CurrentDirectory, "Levels", "level.xml"));
            Serialize(directory, "external_references_samedir", new ExternalReferences(), Path.Combine(Environment.CurrentDirectory, "level.xml"));
            Serialize(directory, "attributes", new Attributes(), null);
            Serialize(directory, "packed_collections", new PackedCollections(), null);
            Serialize(directory, "both_sections", new Both(), null);
            var a = new Node { Name = "a" }; var b = new Node { Name = "b" }; a.Next = b; b.Next = a;
            Serialize(directory, "shared_cycle", a, null);
            var self = new Node { Name = "self" }; self.Next = self;
            Serialize(directory, "shared_self", self, null);
            Serialize(directory, "runtime_type", new WithRuntimeType(), null);
            Serialize(directory, "type_version", new WithVersion(), null);
            Serialize(directory, "deep", Deep.Build(20), null);
            Serialize(directory, "arrays_of_arrays", new ArraysOfArrays(), null);
            Serialize(directory, "rectangular", new Rectangular(), null);
            Serialize(directory, "root_int", 42, null);
            Serialize(directory, "root_string", "just a string", null);
            Serialize(directory, "root_vector3", new Vector3(1, 2, 3), null);
            Serialize(directory, "root_list_int", new List<int> { 1, 2, 3 }, null);
            Serialize(directory, "root_array_string", new[] { "a", "b" }, null);
            Serialize(directory, "root_dictionary", new Dictionary<string, Vector2> { { "a", new Vector2(1, 2) } }, null);
            Serialize(directory, "root_nested", new Nested(), null);
            Serialize(directory, "root_struct", new Vertex { Position = new Vector3(1, 2, 3), Weight = 1 }, null);
            Serialize(directory, "root_enum", Mood.Happy, null);
            Serialize(directory, "root_null_string", (string)null, null);
            Serialize(directory, "root_object_int", (object)7, null);
            Serialize(directory, "root_dog_as_animal", (Animal)new Dog(), null);
            Serialize(directory, "root_color", Color.CornflowerBlue, null);
            Serialize(directory, "root_matrix", Matrix.Identity, null);
            Serialize(directory, "root_bytes", new byte[] { 1, 2, 3, 250 }, null);
            Serialize(directory, "root_char_list", new List<char> { 'a', ' ', '<' }, null);
            Serialize(directory, "root_bool", true, null);
            Serialize(directory, "root_float_nan", float.NaN, null);
            Serialize(directory, "root_timespan", TimeSpan.FromSeconds(90), null);
            Serialize(directory, "root_datetime", new DateTime(2000, 1, 2, 3, 4, 5, DateTimeKind.Utc), null);
            Serialize(directory, "root_decimal", 1.5m, null);
            Serialize(directory, "root_uint", 3000000000u, null);
            Serialize(directory, "root_long", -5L, null);
            Serialize(directory, "root_ulong", ulong.MaxValue, null);
            Serialize(directory, "root_short", (short)-7, null);
            Serialize(directory, "root_ushort", (ushort)7, null);
            Serialize(directory, "root_sbyte", (sbyte)-8, null);
            Serialize(directory, "root_byte", (byte)9, null);
            Serialize(directory, "root_object_string", (object)"boxed", null);
            Serialize(directory, "root_object_enum", (object)Mood.Sad, null);
            Serialize(directory, "root_object_vector3", (object)Vector3.One, null);
            Serialize(directory, "root_list_object", new List<object> { 1, "s" }, null);
            Serialize(directory, "root_float_negzero", -0.0f, null);
            Serialize(directory, "root_double_third", 1.0 / 3.0, null);
            Serialize(directory, "root_float_third", 1.0f / 3.0f, null);
            Serialize(directory, "root_string_whitespace", "  keep  ", null);
            Serialize(directory, "root_string_empty", "", null);
            Serialize(directory, "root_double", 2.5, null);
            Serialize(directory, "root_char", 'q', null);
            Serialize(directory, "root_nullable_int", (int?)5, null);
            Serialize(directory, "root_rectangle", new Rectangle(1, 2, 3, 4), null);
            Serialize(directory, "root_point", new Point(5, 6), null);
            Serialize(directory, "root_quaternion", Quaternion.Identity, null);
            Serialize(directory, "root_vector2", new Vector2(1, 2), null);
            Serialize(directory, "root_vector4", new Vector4(1, 2, 3, 4), null);
            Serialize(directory, "root_boundingbox", new BoundingBox(Vector3.Zero, Vector3.One), null);
            Serialize(directory, "root_boundingsphere", new BoundingSphere(Vector3.Zero, 2), null);
            Serialize(directory, "root_plane", new Plane(Vector3.Up, 1), null);
            Serialize(directory, "root_ray", new Ray(Vector3.Zero, Vector3.Forward), null);
            Serialize(directory, "root_curve", new MathTypes().Curve, null);
            Serialize(directory, "root_runtime", new Runtime(), null);

            // Acceptance: variants a human might author, fed to the genuine deserializer.
            Accept<Nested>(directory, "accept_plain",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_reordered",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Value>3</Value><Name>n</Name></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_missing_member",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_unknown_member",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value><Extra>1</Extra></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_no_type_attribute",
                "<XnaContent><Asset><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_wrong_type_attribute",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Primitives\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_assembly_qualified_type",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested, IntermediateOracle\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_whitespace_value",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>  n  </Name><Value>  3  </Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_bad_int",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>three</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_no_root",
                "<Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset>");
            Accept<Nested>(directory, "accept_namespace_alias",
                "<XnaContent xmlns:o=\"Cna.Xna40.IntermediateOracle\"><Asset Type=\"o:Nested\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_short",
                "<XnaContent><Asset Type=\"Vector3\">1 2 3</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_full",
                "<XnaContent><Asset Type=\"Microsoft.Xna.Framework.Vector3\">1 2 3</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_framework_alias",
                "<XnaContent xmlns:ns=\"Microsoft.Xna.Framework\"><Asset Type=\"ns:Vector3\">1 2 3</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_commas",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Vector3\">1, 2, 3</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_newlines",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Vector3\">\n  1\n  2\n  3\n</Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_short",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List[int]\"><Item>1</Item><Item>2</Item></Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_full",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List[System.Int32]\"><Item>1</Item><Item>2</Item></Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_clr",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List`1[[System.Int32]]\"><Item>1</Item><Item>2</Item></Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_packed",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List[int]\">1 2 3</Asset></XnaContent>");
            Accept<int[]>(directory, "accept_int_array_packed",
                "<XnaContent><Asset Type=\"int[]\">1 2 3</Asset></XnaContent>");
            Accept<int[]>(directory, "accept_int_array_items",
                "<XnaContent><Asset Type=\"int[]\"><Item>1</Item><Item>2</Item></Asset></XnaContent>");
            Accept<Mood>(directory, "accept_enum_name",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Mood\">Angry</Asset></XnaContent>");
            Accept<Mood>(directory, "accept_enum_number",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Mood\">2</Asset></XnaContent>");
            Accept<Toppings>(directory, "accept_flags_combo",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Toppings\">Cheese Olives</Asset></XnaContent>");
            Accept<Toppings>(directory, "accept_flags_comma",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Toppings\">Cheese, Olives</Asset></XnaContent>");
            Accept<bool>(directory, "accept_bool_case",
                "<XnaContent><Asset Type=\"bool\">True</Asset></XnaContent>");
            Accept<Color>(directory, "accept_color_hex",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Color\">FF0080FF</Asset></XnaContent>");
            Accept<Color>(directory, "accept_color_components",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Color\">255 0 128 255</Asset></XnaContent>");
            Accept<Animal>(directory, "accept_polymorphic_root",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Dog\"><Name>d</Name><Tricks>1</Tricks></Asset></XnaContent>");
            Accept<Polymorphism>(directory, "accept_member_type_attribute",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Cna.Xna40.IntermediateOracle.Polymorphism\"><DeclaredBase Type=\"Cna.Xna40.IntermediateOracle.Cat\"><Name>c</Name><Indoor>false</Indoor></DeclaredBase><ExactType><Name>p</Name></ExactType><NullAnimal Null=\"true\" /><ViaAbstract Type=\"Cna.Xna40.IntermediateOracle.Circle\"><Area>1</Area><Radius>2</Radius></ViaAbstract><BoxedInt Type=\"int\">1</BoxedInt><BoxedVector Type=\"Framework:Vector3\">1 2 3</BoxedVector><BoxedString Type=\"string\">s</BoxedString><BoxedNested Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>1</Value></BoxedNested><NullObject Null=\"true\" /><Mixed /><Objects /></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_null_attribute_on_string",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name Null=\"true\" /><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_empty_string_element",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name></Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_comments_and_pi",
                "<?xml version=\"1.0\"?><!-- c --><XnaContent><!-- c --><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><!-- c --><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_cdata",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name><![CDATA[n<>]]></Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_doctype",
                "<!DOCTYPE XnaContent [<!ENTITY e \"n\">]><XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>&e;</Name><Value>3</Value></Asset></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_forward_reference",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First>#Resource1</First><Second>#Resource1</Second><Other>#Resource2</Other><NullShared Null=\"true\" /><SharedList><Item>#Resource1</Item></SharedList><Inline><Label>i</Label><Count>1</Count></Inline></Asset><Resources><Resource ID=\"#Resource1\" Type=\"Cna.Xna40.IntermediateOracle.Referenced\"><Label>s</Label><Count>2</Count></Resource><Resource ID=\"#Resource2\" Type=\"Cna.Xna40.IntermediateOracle.Referenced\"><Label>o</Label><Count>3</Count></Resource></Resources></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_missing_resource",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First>#Resource9</First><Second Null=\"true\" /><Other Null=\"true\" /><NullShared Null=\"true\" /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Asset><Resources /></XnaContent>");
            Accept<Attributes>(directory, "accept_optional_omitted",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><GetOnlyList /><Renamed>1</Renamed><NeverNull>v</NeverNull><Name>f</Name><Value>2</Value>1 2<RenamedItems>3 4</RenamedItems>5 6<Named>7 8</Named></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_required_omitted",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><GetOnlyList /><NeverNull>v</NeverNull></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_ignored_member_present",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><GetOnlyList /><Renamed>1</Renamed><NeverNull>v</NeverNull><Ignored>5</Ignored><Name>f</Name><Value>2</Value><RenamedItems /><Named /></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_readonly_field_present",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><GetOnlyList /><Renamed>1</Renamed><NeverNull>v</NeverNull><Name>f</Name><Value>2</Value>1 2<RenamedItems>3 4</RenamedItems>5 6<Named>7 8</Named><ReadOnlyField>8</ReadOnlyField></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_readonly_property_present",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><ReadOnlyProperty>5</ReadOnlyProperty><GetOnlyList /><Renamed>1</Renamed><NeverNull>v</NeverNull><Name>f</Name><Value>2</Value><RenamedItems /><Named /></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_flattened_list_text_present",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><GetOnlyList /><Renamed>1</Renamed><NeverNull>v</NeverNull><Name>f</Name><Value>2</Value>1 2<RenamedItems>3 4</RenamedItems>5 6<Named>7 8</Named></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_optional_null_explicit",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><GetOnlyList /><Renamed>1</Renamed><OptionalPresent>p</OptionalPresent><OptionalNull Null=\"true\" /><OptionalDefault>0</OptionalDefault><NeverNull>v</NeverNull><Name>f</Name><Value>2</Value>1 2<RenamedItems>3 4</RenamedItems>5 6<Named>7 8</Named></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_nevernull_null",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><GetOnlyList /><Renamed>1</Renamed><NeverNull Null=\"true\" /><Name>f</Name><Value>2</Value><RenamedItems /><Named /></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_fields_before_properties",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><Renamed>1</Renamed><NeverNull>v</NeverNull><Name>f</Name><Value>2</Value><RenamedItems /><Named /><PublicProperty>2</PublicProperty><GetOnlyList /></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_flattened_empty_list",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><GetOnlyList /><Renamed>1</Renamed><NeverNull>v</NeverNull><Name>f</Name><Value>2</Value><RenamedItems>3 4</RenamedItems>5 6<Named>7 8</Named></Asset></XnaContent>");
            Accept<Attributes>(directory, "accept_collection_item_name_ignored_for_packed",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Attributes\"><PublicProperty>2</PublicProperty><GetOnlyList /><Renamed>1</Renamed><NeverNull>v</NeverNull><Name>f</Name><Value>2</Value>1 2<RenamedItems><Number>3</Number></RenamedItems>5 6<Named>7 8</Named></Asset></XnaContent>");
            Accept<Color>(directory, "accept_color_hex_lower",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Color\">ff0080ff</Asset></XnaContent>");
            Accept<Color>(directory, "accept_color_hex_short",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Color\">80FF</Asset></XnaContent>");
            Accept<Color>(directory, "accept_color_hex_prefixed",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Color\">0xFF0080FF</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_two_components",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Vector3\">1 2</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_four_components",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Vector3\">1 2 3 4</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_tabs",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Vector3\">1\t2\t3</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_exponent",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Vector3\">1e2 -2.5E-1 +3</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_inf_nan",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Vector3\">INF -INF NaN</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_infinity_word",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Vector3\">Infinity 1 1</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_comma_decimal",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Vector3\">1,5 2 3</Asset></XnaContent>");
            Accept<int>(directory, "accept_int_whitespace",
                "<XnaContent><Asset Type=\"int\">  42  </Asset></XnaContent>");
            Accept<int>(directory, "accept_int_plus_sign",
                "<XnaContent><Asset Type=\"int\">+42</Asset></XnaContent>");
            Accept<int>(directory, "accept_int_hex",
                "<XnaContent><Asset Type=\"int\">0x2A</Asset></XnaContent>");
            Accept<int>(directory, "accept_int_thousands",
                "<XnaContent><Asset Type=\"int\">1,000</Asset></XnaContent>");
            Accept<int>(directory, "accept_int_overflow",
                "<XnaContent><Asset Type=\"int\">3000000000</Asset></XnaContent>");
            Accept<bool>(directory, "accept_bool_one",
                "<XnaContent><Asset Type=\"bool\">1</Asset></XnaContent>");
            Accept<bool>(directory, "accept_bool_spaces",
                "<XnaContent><Asset Type=\"bool\"> true </Asset></XnaContent>");
            Accept<float>(directory, "accept_float_exponent",
                "<XnaContent><Asset Type=\"float\">1e3</Asset></XnaContent>");
            Accept<float>(directory, "accept_float_infinity_word",
                "<XnaContent><Asset Type=\"float\">Infinity</Asset></XnaContent>");
            Accept<float>(directory, "accept_float_hex",
                "<XnaContent><Asset Type=\"float\">0x10</Asset></XnaContent>");
            Accept<float>(directory, "accept_float_overflow",
                "<XnaContent><Asset Type=\"float\">1e39</Asset></XnaContent>");
            Accept<double>(directory, "accept_double_underflow",
                "<XnaContent><Asset Type=\"double\">1e-400</Asset></XnaContent>");
            Accept<TimeSpan>(directory, "accept_timespan_dotnet",
                "<XnaContent><Asset Type=\"System.TimeSpan\">00:01:30</Asset></XnaContent>");
            Accept<DateTime>(directory, "accept_datetime_no_zone",
                "<XnaContent><Asset Type=\"System.DateTime\">2000-01-02T03:04:05</Asset></XnaContent>");
            Accept<DateTime>(directory, "accept_datetime_offset",
                "<XnaContent><Asset Type=\"System.DateTime\">2000-01-02T03:04:05+02:00</Asset></XnaContent>");
            Accept<Mood>(directory, "accept_enum_case",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Mood\">happy</Asset></XnaContent>");
            Accept<Mood>(directory, "accept_enum_unknown_name",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Mood\">Bored</Asset></XnaContent>");
            Accept<Mood>(directory, "accept_enum_out_of_range_number",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Mood\">99</Asset></XnaContent>");
            Accept<Toppings>(directory, "accept_flags_no_space",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Toppings\">Cheese,Olives</Asset></XnaContent>");
            Accept<Toppings>(directory, "accept_flags_number",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Toppings\">3</Asset></XnaContent>");
            Accept<char>(directory, "accept_char_two",
                "<XnaContent><Asset Type=\"System.Char\">ab</Asset></XnaContent>");
            Accept<char>(directory, "accept_char_empty",
                "<XnaContent><Asset Type=\"System.Char\"></Asset></XnaContent>");
            Accept<char>(directory, "accept_char_keyword_type",
                "<XnaContent><Asset Type=\"char\">a</Asset></XnaContent>");
            Accept<Vector3>(directory, "accept_vector3_full_name",
                "<XnaContent><Asset Type=\"Microsoft.Xna.Framework.Vector3\">1 2 3</Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_generic_full_alias",
                "<XnaContent xmlns:Generic=\"System.Collections.Generic\"><Asset Type=\"Generic:List[System.Int32]\">1 2 3</Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_full_full",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List[int]\">1 2 3</Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_space_in_generic",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List[ int ]\">1 2 3</Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_trailing_space",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List[int]\">1 2 3 </Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_newlines",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List[int]\">\n 1\n 2\n 3\n</Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_empty_text",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List[int]\"></Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_int_bad_token",
                "<XnaContent><Asset Type=\"System.Collections.Generic.List[int]\">1 x 3</Asset></XnaContent>");
            Accept<List<Vector3>>(directory, "accept_list_vector3_odd_count",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"System.Collections.Generic.List[Framework:Vector3]\">1 2 3 4</Asset></XnaContent>");
            Accept<string[]>(directory, "accept_string_array_packed",
                "<XnaContent><Asset Type=\"string[]\">a b</Asset></XnaContent>");
            Accept<Dictionary<string, int>>(directory, "accept_dictionary_duplicate_key",
                "<XnaContent xmlns:Generic=\"System.Collections.Generic\"><Asset Type=\"Generic:Dictionary[string,int]\"><Item><Key>a</Key><Value>1</Value></Item><Item><Key>a</Key><Value>2</Value></Item></Asset></XnaContent>");
            Accept<Dictionary<string, int>>(directory, "accept_dictionary_value_before_key",
                "<XnaContent xmlns:Generic=\"System.Collections.Generic\"><Asset Type=\"Generic:Dictionary[string,int]\"><Item><Value>1</Value><Key>a</Key></Item></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_comment_between_members",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><!-- c --><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_processing_instruction",
                "<XnaContent><?pi x?><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_root_attribute_extra",
                "<XnaContent version=\"1\"><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_asset_attribute_extra",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\" Extra=\"1\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_member_attribute_extra",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name Extra=\"1\">n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_null_false",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name Null=\"false\">n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_null_on_value_type",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value Null=\"true\" /></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_null_with_content",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name Null=\"true\">n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_two_assets",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_resources_before_asset",
                "<XnaContent><Resources /><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_trailing_junk_element",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><Junk /></XnaContent>");
            Accept<Nested>(directory, "accept_asset_renamed",
                "<XnaContent><Item Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Item></XnaContent>");
            Accept<Nested>(directory, "accept_xnacontent_lowercase",
                "<xnacontent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset></xnacontent>");
            Accept<Nested>(directory, "accept_default_namespace",
                "<XnaContent xmlns=\"urn:x\"><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_alias_on_asset_element",
                "<XnaContent><Asset xmlns:o=\"Cna.Xna40.IntermediateOracle\" Type=\"o:Nested\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Animal>(directory, "accept_abstract_without_type",
                "<XnaContent><Asset><Name>n</Name></Asset></XnaContent>");
            Accept<Shape>(directory, "accept_abstract_shape_without_type",
                "<XnaContent><Asset><Area>1</Area></Asset></XnaContent>");
            Accept<Polymorphism>(directory, "accept_polymorphic_member_undeclared_alias",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Polymorphism\"><DeclaredBase Type=\"o:Cat\"><Name>c</Name><Indoor>false</Indoor></DeclaredBase><ExactType><Name>p</Name></ExactType><NullAnimal Null=\"true\" /><ViaAbstract Type=\"Cna.Xna40.IntermediateOracle.Circle\"><Area>1</Area><Radius>2</Radius></ViaAbstract><BoxedInt Type=\"int\">1</BoxedInt><BoxedVector Type=\"Microsoft.Xna.Framework.Vector3\">1 2 3</BoxedVector><BoxedString Type=\"string\">s</BoxedString><BoxedNested Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>1</Value></BoxedNested><NullObject Null=\"true\" /><Mixed /><Objects /></Asset></XnaContent>");
            Accept<Polymorphism>(directory, "accept_boxed_without_type",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Polymorphism\"><DeclaredBase><Name>c</Name></DeclaredBase><ExactType><Name>p</Name></ExactType><NullAnimal Null=\"true\" /><ViaAbstract Type=\"Cna.Xna40.IntermediateOracle.Circle\"><Area>1</Area><Radius>2</Radius></ViaAbstract><BoxedInt>1</BoxedInt><BoxedVector Type=\"Microsoft.Xna.Framework.Vector3\">1 2 3</BoxedVector><BoxedString Type=\"string\">s</BoxedString><BoxedNested Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>1</Value></BoxedNested><NullObject Null=\"true\" /><Mixed /><Objects /></Asset></XnaContent>");
            Accept<Polymorphism>(directory, "accept_abstract_member_without_type",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Polymorphism\"><DeclaredBase><Name>c</Name></DeclaredBase><ExactType><Name>p</Name></ExactType><NullAnimal Null=\"true\" /><ViaAbstract><Area>1</Area></ViaAbstract><BoxedInt Type=\"int\">1</BoxedInt><BoxedVector Type=\"Microsoft.Xna.Framework.Vector3\">1 2 3</BoxedVector><BoxedString Type=\"string\">s</BoxedString><BoxedNested Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>1</Value></BoxedNested><NullObject Null=\"true\" /><Mixed /><Objects /></Asset></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_inline_instead_of_reference",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First><Label>x</Label><Count>1</Count></First><Second Null=\"true\" /><Other Null=\"true\" /><NullShared /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Asset></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_null_attribute",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First Null=\"true\" /><Second Null=\"true\" /><Other Null=\"true\" /><NullShared Null=\"true\" /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Asset></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_resource_without_type",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First>#R</First><Second Null=\"true\" /><Other Null=\"true\" /><NullShared /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Asset><Resources><Resource ID=\"#R\"><Label>x</Label><Count>1</Count></Resource></Resources></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_resource_custom_id",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First>#Bob</First><Second>#Bob</Second><Other Null=\"true\" /><NullShared /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Asset><Resources><Resource ID=\"#Bob\" Type=\"Cna.Xna40.IntermediateOracle.Referenced\"><Label>x</Label><Count>1</Count></Resource></Resources></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_resource_no_hash",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First>Bob</First><Second Null=\"true\" /><Other Null=\"true\" /><NullShared /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Asset><Resources><Resource ID=\"Bob\" Type=\"Cna.Xna40.IntermediateOracle.Referenced\"><Label>x</Label><Count>1</Count></Resource></Resources></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_resource_wrong_type",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First>#R</First><Second Null=\"true\" /><Other Null=\"true\" /><NullShared /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Asset><Resources><Resource ID=\"#R\" Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>x</Name><Value>1</Value></Resource></Resources></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_resource_unused",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First Null=\"true\" /><Second Null=\"true\" /><Other Null=\"true\" /><NullShared /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Asset><Resources><Resource ID=\"#R\" Type=\"Cna.Xna40.IntermediateOracle.Referenced\"><Label>x</Label><Count>1</Count></Resource></Resources></XnaContent>");
            Accept<ExternalReferences>(directory, "accept_external_reference_relative",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent\">Textures\\wall.png</ExternalReference></ExternalReferences></XnaContent>");
            Accept<ExternalReferences>(directory, "accept_external_reference_missing_id",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences /></XnaContent>");
            Accept<ExternalReferences>(directory, "accept_external_reference_no_target_type",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\">Textures\\wall.png</ExternalReference></ExternalReferences></XnaContent>");
            Accept<ExternalReferences>(directory, "accept_external_reference_inline_filename",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture>Textures\\wall.png</Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset></XnaContent>");
            Accept<ExternalReferences>(directory, "accept_external_reference_empty_element",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture /><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset></XnaContent>");
            Accept<Deep>(directory, "accept_deep_nesting_200",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Deep\">" + string.Concat(Enumerable.Repeat("<Child>", 200)) + "<Child Null=\"true\" /><Depth>0</Depth>" + string.Concat(Enumerable.Repeat("</Child><Depth>0</Depth>", 200)) + "</Asset></XnaContent>");
            string relocation = Path.Combine(Environment.CurrentDirectory, "Levels", "level.xml");
            Accept<ExternalReferences>(directory, "accept_external_relocated_relative",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent\">Textures\\wall.png</ExternalReference></ExternalReferences></XnaContent>", relocation);
            Accept<ExternalReferences>(directory, "accept_external_relocated_parent",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent\">..\\Textures\\wall.png</ExternalReference></ExternalReferences></XnaContent>", relocation);
            Accept<ExternalReferences>(directory, "accept_external_relocated_forward_slashes",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent\">../Textures/wall.png</ExternalReference></ExternalReferences></XnaContent>", relocation);
            Accept<ExternalReferences>(directory, "accept_external_relocated_absolute",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent\">C:\\Other\\wall.png</ExternalReference></ExternalReferences></XnaContent>", relocation);
            Accept<ExternalReferences>(directory, "accept_external_relocated_empty_filename",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent\"></ExternalReference></ExternalReferences></XnaContent>", relocation);
            Accept<ExternalReferences>(directory, "accept_external_relocated_wrong_target_type",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent\">a.png</ExternalReference></ExternalReferences></XnaContent>", relocation);
            Accept<ExternalReferences>(directory, "accept_external_relocated_unknown_target_type",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture><Reference>#E</Reference></Texture><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"No.Such.Type\">a.png</ExternalReference></ExternalReferences></XnaContent>", relocation);
            Accept<ExternalReferences>(directory, "accept_external_relocated_unused_entry",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.ExternalReferences\"><Texture Null=\"true\" /><Again Null=\"true\" /><Other Null=\"true\" /><Null Null=\"true\" /><List /></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent\">a.png</ExternalReference></ExternalReferences></XnaContent>", relocation);
            Accept<Nested>(directory, "accept_externals_before_resources",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><ExternalReferences /><Resources /></XnaContent>");
            Accept<Nested>(directory, "accept_resources_then_externals",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><Resources /><ExternalReferences /></XnaContent>");
            Accept<Nested>(directory, "accept_resources_twice",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><Resources /><Resources /></XnaContent>");
            Accept<Nested>(directory, "accept_resource_without_id",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><Resources><Resource Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Resource></Resources></XnaContent>");
            Accept<Nested>(directory, "accept_resource_duplicate_id",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><Resources><Resource ID=\"#A\" Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Resource><Resource ID=\"#A\" Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Resource></Resources></XnaContent>");
            Accept<SharedResources>(directory, "accept_shared_self_reference",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First>#A</First><Second Null=\"true\" /><Other Null=\"true\" /><NullShared /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Asset><Resources><Resource ID=\"#A\" Type=\"Cna.Xna40.IntermediateOracle.SharedResources\"><First>#A</First><Second Null=\"true\" /><Other Null=\"true\" /><NullShared /><SharedList /><Inline><Label>i</Label><Count>1</Count></Inline></Resource></Resources></XnaContent>");
            Accept<bool>(directory, "accept_bool_zero",
                "<XnaContent><Asset Type=\"bool\">0</Asset></XnaContent>");
            Accept<byte>(directory, "accept_byte_negative",
                "<XnaContent><Asset Type=\"byte\">-1</Asset></XnaContent>");
            Accept<float>(directory, "accept_float_trailing_f",
                "<XnaContent><Asset Type=\"float\">1.5f</Asset></XnaContent>");
            Accept<float>(directory, "accept_float_leading_dot",
                "<XnaContent><Asset Type=\"float\">.5</Asset></XnaContent>");
            Accept<float>(directory, "accept_float_nan_lower",
                "<XnaContent><Asset Type=\"float\">nan</Asset></XnaContent>");
            Accept<float>(directory, "accept_float_negative_infinity_word",
                "<XnaContent><Asset Type=\"float\">-Infinity</Asset></XnaContent>");
            Accept<Color>(directory, "accept_color_hex_nine_digits",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Color\">1FF0080FF</Asset></XnaContent>");
            Accept<Color>(directory, "accept_color_decimal",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Color\">4278190335</Asset></XnaContent>");
            Accept<Rectangle>(directory, "accept_rectangle_floats",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Rectangle\">1.5 2 3 4</Asset></XnaContent>");
            Accept<Curve>(directory, "accept_curve_keys_short",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Curve\"><PreLoop>Constant</PreLoop><PostLoop>Constant</PostLoop><Keys>0 0 0 1</Keys></Asset></XnaContent>");
            Accept<Curve>(directory, "accept_curve_keys_bad_continuity",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Curve\"><PreLoop>Constant</PreLoop><PostLoop>Constant</PostLoop><Keys>0 0 0 1 Bouncy</Keys></Asset></XnaContent>");
            Accept<Curve>(directory, "accept_curve_missing_loops",
                "<XnaContent xmlns:Framework=\"Microsoft.Xna.Framework\"><Asset Type=\"Framework:Curve\"><Keys>0 0 0 1 Smooth</Keys></Asset></XnaContent>");
            Accept<TimeSpan>(directory, "accept_timespan_days",
                "<XnaContent><Asset Type=\"System.TimeSpan\">P1DT2H</Asset></XnaContent>");
            Accept<TimeSpan>(directory, "accept_timespan_negative",
                "<XnaContent><Asset Type=\"System.TimeSpan\">-PT1S</Asset></XnaContent>");
            Accept<DateTime>(directory, "accept_datetime_date_only",
                "<XnaContent><Asset Type=\"System.DateTime\">2000-01-02</Asset></XnaContent>");
            Accept<DateTime>(directory, "accept_datetime_fraction",
                "<XnaContent><Asset Type=\"System.DateTime\">2000-01-02T03:04:05.1234567Z</Asset></XnaContent>");
            Accept<decimal>(directory, "accept_decimal_exponent",
                "<XnaContent><Asset Type=\"System.Decimal\">1e2</Asset></XnaContent>");
            Accept<string>(directory, "accept_string_type_full",
                "<XnaContent><Asset Type=\"System.String\">s</Asset></XnaContent>");
            Accept<int>(directory, "accept_int_type_full",
                "<XnaContent><Asset Type=\"System.Int32\">1</Asset></XnaContent>");
            Accept<object>(directory, "accept_object_root_int",
                "<XnaContent><Asset Type=\"int\">1</Asset></XnaContent>");
            Accept<object>(directory, "accept_object_root_no_type",
                "<XnaContent><Asset>1</Asset></XnaContent>");
            Accept<Nested>(directory, "accept_asset_null",
                "<XnaContent><Asset Null=\"true\" /></XnaContent>");
            Accept<int[]>(directory, "accept_int_array_from_list_type",
                "<XnaContent xmlns:Generic=\"System.Collections.Generic\"><Asset Type=\"Generic:List[int]\">1 2</Asset></XnaContent>");
            Accept<List<int>>(directory, "accept_list_from_array_type",
                "<XnaContent><Asset Type=\"int[]\">1 2</Asset></XnaContent>");
            Accept<Nested>(directory, "accept_bom_and_declaration",
                "\uFEFF<?xml version=\"1.0\" encoding=\"utf-8\"?><XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_entity_numeric",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>&#x41;&#66;</Name><Value>3</Value></Asset></XnaContent>");
            Accept<Nested>(directory, "accept_int_entity_spaces",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>&#32;3&#32;</Value></Asset></XnaContent>");
            Accept<Both>(directory, "accept_both_resources_then_externals", "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Both\"><Shared>#S</Shared><Texture><Reference>#E</Reference></Texture></Asset><Resources><Resource ID=\"#S\" Type=\"Cna.Xna40.IntermediateOracle.Referenced\"><Label>x</Label><Count>1</Count></Resource></Resources><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent\">a.png</ExternalReference></ExternalReferences></XnaContent>", relocation);
            Accept<Both>(directory, "accept_both_externals_then_resources", "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Both\"><Shared>#S</Shared><Texture><Reference>#E</Reference></Texture></Asset><ExternalReferences><ExternalReference ID=\"#E\" TargetType=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent\">a.png</ExternalReference></ExternalReferences><Resources><Resource ID=\"#S\" Type=\"Cna.Xna40.IntermediateOracle.Referenced\"><Label>x</Label><Count>1</Count></Resource></Resources></XnaContent>", relocation);
            Accept<Nested>(directory, "accept_empty_resources_selfclosing", "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><Resources /></XnaContent>");
            Accept<Nested>(directory, "accept_empty_resources_expanded", "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><Resources></Resources></XnaContent>");
            Accept<Nested>(directory, "accept_empty_externals_selfclosing", "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><ExternalReferences /></XnaContent>");
            Accept<Nested>(directory, "accept_empty_externals_expanded", "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><ExternalReferences></ExternalReferences></XnaContent>");
            Accept<Nested>(directory, "accept_expanded_resources_then_externals", "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Nested\"><Name>n</Name><Value>3</Value></Asset><Resources></Resources><ExternalReferences></ExternalReferences></XnaContent>");
            Accept<Node>(directory, "accept_shared_cycle_read",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Node\"><Name>a</Name><Next>#B</Next></Asset><Resources><Resource ID=\"#B\" Type=\"Cna.Xna40.IntermediateOracle.Node\"><Name>b</Name><Next>#A</Next></Resource><Resource ID=\"#A\" Type=\"Cna.Xna40.IntermediateOracle.Node\"><Name>a2</Name><Next>#B</Next></Resource></Resources></XnaContent>");
            Accept<Node>(directory, "accept_shared_self_cycle_read",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Node\"><Name>a</Name><Next>#A</Next></Asset><Resources><Resource ID=\"#A\" Type=\"Cna.Xna40.IntermediateOracle.Node\"><Name>self</Name><Next>#A</Next></Resource></Resources></XnaContent>");
            Accept<Node>(directory, "accept_shared_resource_referencing_undefined",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Node\"><Name>a</Name><Next>#B</Next></Asset><Resources><Resource ID=\"#B\" Type=\"Cna.Xna40.IntermediateOracle.Node\"><Name>b</Name><Next>#Z</Next></Resource></Resources></XnaContent>");
            Accept<Node>(directory, "accept_shared_reference_whitespace",
                "<XnaContent><Asset Type=\"Cna.Xna40.IntermediateOracle.Node\"><Name>a</Name><Next> #B </Next></Asset><Resources><Resource ID=\"#B\" Type=\"Cna.Xna40.IntermediateOracle.Node\"><Name>b</Name><Next /></Resource></Resources></XnaContent>");

            File.WriteAllText(Path.Combine(directory, "manifest.json"),
                "{\n \"producer\": \"Microsoft XNA Game Studio 4.0 IntermediateSerializer, driven by tools/xna-pipeline-oracle/intermediate/IntermediateOracle.cs\",\n \"runtime\": \"" +
                Environment.Version + "\",\n \"pipelineAssembly\": \"" + typeof(IntermediateSerializer).Assembly.FullName + "\",\n \"cases\": [\n" +
                string.Join(",\n", Manifest.ToArray()) + "\n ]\n}\n");
            return 0;
        }
    }
}
