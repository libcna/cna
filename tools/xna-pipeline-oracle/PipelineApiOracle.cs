// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-010 / XNAPP-011: the Microsoft XNA Game Studio 4.0
// Content Pipeline public-API oracle.
//
// WHAT THIS IS
//
// A reflection program that loads the seven Microsoft.Xna.Framework.Content.Pipeline*.dll
// assemblies of a legally installed XNA Game Studio 4.0 and writes a deterministic JSON inventory
// of their PUBLIC and PROTECTED surface: every type, member, attribute, enum value, importer
// declaration (file extensions, default processor, display name), processor declaration
// (input/output types, public properties and their instantiated default values), and the
// framework's ContentSerializer* attributes that govern intermediate serialization.
//
// WHAT IT DELIBERATELY DOES NOT DO
//
// It never reads a method body. Nothing here touches System.Reflection.Emit, MethodBody,
// GetILAsByteArray or any decompiler. The only behaviour it observes is black-box: it constructs
// each built-in processor with its parameterless constructor and reads the public properties
// back, which is exactly what MSBuild's BuildContent task does before applying
// ProcessorParameters_* metadata. That is the sanctioned use in the provenance rules of
// plans/plan_xnapipeline_parity.md section 2.
//
// It references no XNA assembly at compile time, so it compiles with `mcs` on Linux or
// `csc.exe` on Windows with nothing but the base class library, and loads the assemblies from the
// directory given on the command line. Under Wine the same binary runs against the real
// .NET Framework 4.0, which is required because two importer assemblies are mixed-mode x86.
//
// The output is deterministic: every collection is sorted, every number is invariant-culture,
// every non-ASCII character is escaped, so two runs over the same assemblies produce identical
// bytes and `git diff` on the committed inventory is meaningful.
//
// Usage: PipelineApiOracle.exe <references-dir> <output.json>

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;

namespace Cna.Xna40.PipelineOracle
{
    /// <summary>An insertion-ordered JSON object; keys are written in the order they were added.</summary>
    internal sealed class JsonObject
    {
        public readonly List<KeyValuePair<string, object>> Pairs = new List<KeyValuePair<string, object>>();

        public JsonObject Add(string key, object value)
        {
            Pairs.Add(new KeyValuePair<string, object>(key, value));
            return this;
        }
    }

    /// <summary>A minimal deterministic JSON writer; no framework serializer is used so the output
    /// does not depend on the .NET version the oracle happens to run on.</summary>
    internal static class Json
    {
        public static string Serialize(object value)
        {
            var sb = new StringBuilder();
            Write(sb, value, 0);
            sb.Append('\n');
            return sb.ToString();
        }

        private static void Indent(StringBuilder sb, int depth)
        {
            for (int i = 0; i < depth; i++) { sb.Append("  "); }
        }

        private static void Write(StringBuilder sb, object value, int depth)
        {
            if (value == null) { sb.Append("null"); return; }
            var s = value as string;
            if (s != null) { WriteString(sb, s); return; }
            if (value is bool) { sb.Append((bool)value ? "true" : "false"); return; }
            if (value is int) { sb.Append(((int)value).ToString(CultureInfo.InvariantCulture)); return; }
            if (value is long) { sb.Append(((long)value).ToString(CultureInfo.InvariantCulture)); return; }
            if (value is ulong) { sb.Append(((ulong)value).ToString(CultureInfo.InvariantCulture)); return; }
            if (value is double)
            {
                double d = (double)value;
                if (double.IsNaN(d) || double.IsInfinity(d)) { WriteString(sb, d.ToString(CultureInfo.InvariantCulture)); }
                else { sb.Append(d.ToString("R", CultureInfo.InvariantCulture)); }
                return;
            }
            var obj = value as JsonObject;
            if (obj != null)
            {
                if (obj.Pairs.Count == 0) { sb.Append("{}"); return; }
                sb.Append("{\n");
                for (int i = 0; i < obj.Pairs.Count; i++)
                {
                    Indent(sb, depth + 1);
                    WriteString(sb, obj.Pairs[i].Key);
                    sb.Append(": ");
                    Write(sb, obj.Pairs[i].Value, depth + 1);
                    if (i + 1 < obj.Pairs.Count) { sb.Append(','); }
                    sb.Append('\n');
                }
                Indent(sb, depth);
                sb.Append('}');
                return;
            }
            var list = value as System.Collections.IList;
            if (list != null)
            {
                if (list.Count == 0) { sb.Append("[]"); return; }
                bool scalars = true;
                foreach (object item in list)
                {
                    if (item is JsonObject || item is System.Collections.IList) { scalars = false; break; }
                }
                if (scalars && list.Count <= 8)
                {
                    sb.Append('[');
                    for (int i = 0; i < list.Count; i++)
                    {
                        if (i > 0) { sb.Append(", "); }
                        Write(sb, list[i], depth + 1);
                    }
                    sb.Append(']');
                    return;
                }
                sb.Append("[\n");
                for (int i = 0; i < list.Count; i++)
                {
                    Indent(sb, depth + 1);
                    Write(sb, list[i], depth + 1);
                    if (i + 1 < list.Count) { sb.Append(','); }
                    sb.Append('\n');
                }
                Indent(sb, depth);
                sb.Append(']');
                return;
            }
            WriteString(sb, Convert.ToString(value, CultureInfo.InvariantCulture));
        }

        private static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20 || c > 0x7e)
                        {
                            sb.Append("\\u");
                            sb.Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        }
                        else { sb.Append(c); }
                        break;
                }
            }
            sb.Append('"');
        }
    }

    internal static class Names
    {
        /// <summary>C#-style display name: generic arguments expanded, nested types joined with '+'.</summary>
        public static string Display(Type t)
        {
            if (t == null) { return null; }
            if (t.IsGenericParameter) { return t.Name; }
            if (t.IsByRef) { return Display(t.GetElementType()); }
            if (t.IsPointer) { return Display(t.GetElementType()) + "*"; }
            if (t.IsArray)
            {
                int rank = t.GetArrayRank();
                return Display(t.GetElementType()) + "[" + new string(',', rank - 1) + "]";
            }
            if (t.IsGenericType)
            {
                Type def = t.IsGenericTypeDefinition ? t : t.GetGenericTypeDefinition();
                string baseName = RawName(def);
                var args = t.GetGenericArguments();
                var parts = new List<string>();
                foreach (Type a in args) { parts.Add(Display(a)); }
                return StripArity(baseName) + "<" + string.Join(", ", parts.ToArray()) + ">";
            }
            return RawName(t);
        }

        private static string RawName(Type t)
        {
            if (t.IsNested && !t.IsGenericParameter)
            {
                return RawName(t.DeclaringType) + "+" + t.Name;
            }
            return t.FullName ?? t.Name;
        }

        private static string StripArity(string name)
        {
            // Strip every `N arity marker, including on outer generic types (Outer`1+Inner`1).
            var sb = new StringBuilder();
            int i = 0;
            while (i < name.Length)
            {
                if (name[i] == '`')
                {
                    i++;
                    while (i < name.Length && char.IsDigit(name[i])) { i++; }
                    continue;
                }
                sb.Append(name[i]);
                i++;
            }
            return sb.ToString();
        }

        /// <summary>The CLR metadata name (with `N arity) for a type definition.</summary>
        public static string Clr(Type t)
        {
            if (t.IsGenericType && !t.IsGenericTypeDefinition) { t = t.GetGenericTypeDefinition(); }
            return t.FullName ?? t.Name;
        }

        public static string Assembly(Type t)
        {
            if (t.IsGenericParameter) { return null; }
            if (t.HasElementType) { return Assembly(t.GetElementType()); }
            return t.Assembly.GetName().Name;
        }
    }

    internal static class Oracle
    {
        private const string PipelineNamespaceRoot = "Microsoft.Xna.Framework.Content.Pipeline";
        private static readonly string[] PipelineAssemblyNames =
        {
            "Microsoft.Xna.Framework.Content.Pipeline",
            "Microsoft.Xna.Framework.Content.Pipeline.AudioImporters",
            "Microsoft.Xna.Framework.Content.Pipeline.EffectImporter",
            "Microsoft.Xna.Framework.Content.Pipeline.FBXImporter",
            "Microsoft.Xna.Framework.Content.Pipeline.TextureImporter",
            "Microsoft.Xna.Framework.Content.Pipeline.VideoImporters",
            "Microsoft.Xna.Framework.Content.Pipeline.XImporter",
        };

        private static string referencesDirectory;
        private static readonly SortedDictionary<string, string> externalTypes = new SortedDictionary<string, string>(StringComparer.Ordinal);
        private static readonly List<string> notes = new List<string>();

        private static Assembly Resolve(object sender, ResolveEventArgs args)
        {
            string name = new AssemblyName(args.Name).Name + ".dll";
            string path = Path.Combine(referencesDirectory, name);
            return File.Exists(path) ? Assembly.LoadFrom(path) : null;
        }

        private static string Sha256(string path)
        {
            using (var sha = new SHA256Managed())
            using (var stream = File.OpenRead(path))
            {
                byte[] hash = sha.ComputeHash(stream);
                var sb = new StringBuilder();
                foreach (byte b in hash) { sb.Append(b.ToString("x2", CultureInfo.InvariantCulture)); }
                return sb.ToString();
            }
        }

        /// <summary>A type is API only if it and every declaring type around it are reachable from
        /// outside the assembly: a public delegate nested in an internal class is not.</summary>
        private static bool IsVisibleType(Type t)
        {
            if (t.IsPublic) { return true; }
            if (!(t.IsNestedPublic || t.IsNestedFamily || t.IsNestedFamORAssem)) { return false; }
            return t.DeclaringType != null && IsVisibleType(t.DeclaringType);
        }

        private static string TypeVisibility(Type t)
        {
            if (t.IsPublic) { return "public"; }
            if (t.IsNestedPublic) { return "nested public"; }
            if (t.IsNestedFamily) { return "nested protected"; }
            if (t.IsNestedFamORAssem) { return "nested protected internal"; }
            return "non-public";
        }

        private static bool IsVisible(MethodBase m)
        {
            return m != null && (m.IsPublic || m.IsFamily || m.IsFamilyOrAssembly);
        }

        private static string Visibility(MethodBase m)
        {
            if (m == null) { return null; }
            if (m.IsPublic) { return "public"; }
            if (m.IsFamily) { return "protected"; }
            if (m.IsFamilyOrAssembly) { return "protected internal"; }
            if (m.IsAssembly) { return "internal"; }
            if (m.IsFamilyAndAssembly) { return "private protected"; }
            return "private";
        }

        private static string Visibility(FieldInfo f)
        {
            if (f.IsPublic) { return "public"; }
            if (f.IsFamily) { return "protected"; }
            if (f.IsFamilyOrAssembly) { return "protected internal"; }
            return "non-public";
        }

        private static string Kind(Type t)
        {
            if (t.IsEnum) { return "enum"; }
            if (t.IsInterface) { return "interface"; }
            if (typeof(Delegate).IsAssignableFrom(t) && t != typeof(Delegate) && t != typeof(MulticastDelegate)) { return "delegate"; }
            if (t.IsValueType) { return "struct"; }
            if (t.IsClass) { return "class"; }
            return "unknown";
        }

        private static void NoteExternal(Type t)
        {
            if (t == null || t.IsGenericParameter) { return; }
            while (t.HasElementType) { t = t.GetElementType(); }
            if (t.IsGenericType)
            {
                foreach (Type a in t.GetGenericArguments()) { NoteExternal(a); }
                t = t.GetGenericTypeDefinition();
            }
            string asm = t.Assembly.GetName().Name;
            if (Array.IndexOf(PipelineAssemblyNames, asm) >= 0) { return; }
            string key = Names.Clr(t);
            if (!externalTypes.ContainsKey(key)) { externalTypes[key] = asm; }
        }

        private static string FormatConstant(object v)
        {
            if (v == null) { return "null"; }
            if (v is string) { return "\"" + (string)v + "\""; }
            if (v is bool) { return (bool)v ? "true" : "false"; }
            if (v is Enum) { return v.GetType().Name + "." + v.ToString(); }
            if (v is float) { return ((float)v).ToString("R", CultureInfo.InvariantCulture); }
            if (v is double) { return ((double)v).ToString("R", CultureInfo.InvariantCulture); }
            if (v is Type) { return "typeof(" + Names.Display((Type)v) + ")"; }
            var array = v as System.Collections.IEnumerable;
            if (array != null && !(v is string))
            {
                var parts = new List<string>();
                foreach (object item in array) { parts.Add(FormatConstant(item)); }
                return "[" + string.Join(", ", parts.ToArray()) + "]";
            }
            return Convert.ToString(v, CultureInfo.InvariantCulture);
        }

        private static object FormatAttributeArgument(CustomAttributeTypedArgument arg)
        {
            var array = arg.Value as System.Collections.ObjectModel.ReadOnlyCollection<CustomAttributeTypedArgument>;
            if (array != null)
            {
                var items = new List<object>();
                foreach (CustomAttributeTypedArgument item in array) { items.Add(FormatAttributeArgument(item)); }
                return items;
            }
            if (arg.Value == null) { return null; }
            if (arg.ArgumentType.IsEnum)
            {
                return arg.ArgumentType.Name + "." + Enum.ToObject(arg.ArgumentType, arg.Value).ToString();
            }
            if (arg.Value is Type) { return "typeof(" + Names.Display((Type)arg.Value) + ")"; }
            if (arg.Value is string || arg.Value is bool) { return arg.Value; }
            return FormatConstant(arg.Value);
        }

        private static List<object> Attributes(IList<CustomAttributeData> data)
        {
            var list = new List<JsonObject>();
            foreach (CustomAttributeData a in data)
            {
                Type at = a.Constructor.DeclaringType;
                // Compiler bookkeeping attributes say nothing about the API contract.
                if (at.Namespace == "System.Runtime.CompilerServices" || at.Namespace == "System.Diagnostics") { continue; }
                var o = new JsonObject();
                o.Add("type", Names.Display(at));
                var ctorArgs = new List<object>();
                foreach (CustomAttributeTypedArgument arg in a.ConstructorArguments) { ctorArgs.Add(FormatAttributeArgument(arg)); }
                o.Add("arguments", ctorArgs);
                var named = new SortedDictionary<string, object>(StringComparer.Ordinal);
                foreach (CustomAttributeNamedArgument n in a.NamedArguments) { named[n.MemberInfo.Name] = FormatAttributeArgument(n.TypedValue); }
                var namedObj = new JsonObject();
                foreach (var kv in named) { namedObj.Add(kv.Key, kv.Value); }
                o.Add("named", namedObj);
                list.Add(o);
            }
            list.Sort((x, y) => string.CompareOrdinal(Json.Serialize(x), Json.Serialize(y)));
            var result = new List<object>();
            foreach (JsonObject o in list) { result.Add(o); }
            return result;
        }

        private static List<object> Parameters(ParameterInfo[] parameters)
        {
            var list = new List<object>();
            foreach (ParameterInfo p in parameters)
            {
                var o = new JsonObject();
                o.Add("name", p.Name);
                Type pt = p.ParameterType;
                o.Add("type", Names.Display(pt));
                NoteExternal(pt);
                string direction = "in";
                if (pt.IsByRef) { direction = p.IsOut ? "out" : "ref"; }
                o.Add("direction", direction);
                bool isParams = false;
                foreach (CustomAttributeData a in CustomAttributeData.GetCustomAttributes(p))
                {
                    if (a.Constructor.DeclaringType == typeof(ParamArrayAttribute)) { isParams = true; }
                }
                if (isParams) { o.Add("params", true); }
                if (p.IsOptional || (p.Attributes & ParameterAttributes.HasDefault) != 0)
                {
                    o.Add("optional", true);
                    object dv = p.DefaultValue;
                    o.Add("default", dv == DBNull.Value ? "(none)" : FormatConstant(dv));
                }
                list.Add(o);
            }
            return list;
        }

        private static string Signature(string name, MethodBase m)
        {
            var sb = new StringBuilder(name);
            if (m.IsGenericMethod)
            {
                var names = new List<string>();
                foreach (Type g in m.GetGenericArguments()) { names.Add(g.Name); }
                sb.Append('<').Append(string.Join(", ", names.ToArray())).Append('>');
            }
            sb.Append('(');
            var ps = m.GetParameters();
            for (int i = 0; i < ps.Length; i++)
            {
                if (i > 0) { sb.Append(", "); }
                Type pt = ps[i].ParameterType;
                if (pt.IsByRef) { sb.Append(ps[i].IsOut ? "out " : "ref "); }
                sb.Append(Names.Display(pt));
            }
            sb.Append(')');
            return sb.ToString();
        }

        private static List<object> GenericParameters(Type[] args)
        {
            var list = new List<object>();
            foreach (Type g in args)
            {
                var o = new JsonObject();
                o.Add("name", g.Name);
                var constraints = new List<object>();
                GenericParameterAttributes ga = g.GenericParameterAttributes;
                if ((ga & GenericParameterAttributes.ReferenceTypeConstraint) != 0) { constraints.Add("class"); }
                if ((ga & GenericParameterAttributes.NotNullableValueTypeConstraint) != 0) { constraints.Add("struct"); }
                var typeConstraints = new List<string>();
                foreach (Type c in g.GetGenericParameterConstraints())
                {
                    if (c == typeof(ValueType)) { continue; }
                    typeConstraints.Add(Names.Display(c));
                    NoteExternal(c);
                }
                typeConstraints.Sort(StringComparer.Ordinal);
                foreach (string c in typeConstraints) { constraints.Add(c); }
                if ((ga & GenericParameterAttributes.DefaultConstructorConstraint) != 0
                    && (ga & GenericParameterAttributes.NotNullableValueTypeConstraint) == 0) { constraints.Add("new()"); }
                o.Add("constraints", constraints);
                list.Add(o);
            }
            return list;
        }

        private static JsonObject Method(MethodInfo m)
        {
            var o = new JsonObject();
            o.Add("kind", m.IsSpecialName && m.Name.StartsWith("op_", StringComparison.Ordinal) ? "operator" : "method");
            o.Add("name", m.Name);
            o.Add("signature", Signature(m.Name, m));
            o.Add("visibility", Visibility(m));
            o.Add("returnType", Names.Display(m.ReturnType));
            NoteExternal(m.ReturnType);
            o.Add("parameters", Parameters(m.GetParameters()));
            if (m.IsGenericMethodDefinition) { o.Add("genericParameters", GenericParameters(m.GetGenericArguments())); }
            o.Add("static", m.IsStatic);
            o.Add("abstract", m.IsAbstract);
            o.Add("virtual", m.IsVirtual && !m.IsFinal);
            bool isOverride = m.IsVirtual && (m.Attributes & MethodAttributes.NewSlot) == 0;
            o.Add("override", isOverride);
            if (isOverride)
            {
                MethodInfo baseDefinition = m.GetBaseDefinition();
                if (baseDefinition != null && baseDefinition.DeclaringType != m.DeclaringType) { o.Add("overrides", Names.Display(baseDefinition.DeclaringType)); }
            }
            o.Add("sealed", m.IsVirtual && m.IsFinal);
            var interfaces = new List<string>();
            if (!m.IsStatic && !m.DeclaringType.IsInterface)
            {
                foreach (Type iface in m.DeclaringType.GetInterfaces())
                {
                    InterfaceMapping map;
                    try { map = m.DeclaringType.GetInterfaceMap(iface); } catch (ArgumentException) { continue; }
                    for (int i = 0; i < map.TargetMethods.Length; i++)
                    {
                        if (map.TargetMethods[i] == m) { interfaces.Add(Names.Display(iface) + "." + map.InterfaceMethods[i].Name); }
                    }
                }
            }
            interfaces.Sort(StringComparer.Ordinal);
            if (interfaces.Count > 0) { o.Add("implements", interfaces); }
            o.Add("attributes", Attributes(CustomAttributeData.GetCustomAttributes(m)));
            return o;
        }

        private static JsonObject Constructor(ConstructorInfo c)
        {
            var o = new JsonObject();
            o.Add("kind", "constructor");
            o.Add("name", c.DeclaringType.Name);
            o.Add("signature", Signature(".ctor", c));
            o.Add("visibility", Visibility(c));
            o.Add("parameters", Parameters(c.GetParameters()));
            o.Add("static", c.IsStatic);
            o.Add("attributes", Attributes(CustomAttributeData.GetCustomAttributes(c)));
            return o;
        }

        private static JsonObject Property(PropertyInfo p)
        {
            var o = new JsonObject();
            MethodInfo getter = p.GetGetMethod(true);
            MethodInfo setter = p.GetSetMethod(true);
            bool indexer = p.GetIndexParameters().Length > 0;
            o.Add("kind", indexer ? "indexer" : "property");
            o.Add("name", p.Name);
            var sb = new StringBuilder(p.Name);
            if (indexer)
            {
                sb.Append('[');
                var ps = p.GetIndexParameters();
                for (int i = 0; i < ps.Length; i++)
                {
                    if (i > 0) { sb.Append(", "); }
                    sb.Append(Names.Display(ps[i].ParameterType));
                }
                sb.Append(']');
            }
            o.Add("signature", sb.ToString());
            o.Add("type", Names.Display(p.PropertyType));
            NoteExternal(p.PropertyType);
            if (indexer) { o.Add("parameters", Parameters(p.GetIndexParameters())); }
            o.Add("get", IsVisible(getter) ? Visibility(getter) : null);
            o.Add("set", IsVisible(setter) ? Visibility(setter) : null);
            MethodBase accessor = IsVisible(getter) ? (MethodBase)getter : setter;
            o.Add("static", accessor != null && accessor.IsStatic);
            o.Add("abstract", accessor != null && accessor.IsAbstract);
            o.Add("virtual", accessor != null && accessor.IsVirtual && !accessor.IsFinal);
            bool isOverride = accessor != null && accessor.IsVirtual && (accessor.Attributes & MethodAttributes.NewSlot) == 0;
            o.Add("override", isOverride);
            o.Add("attributes", Attributes(CustomAttributeData.GetCustomAttributes(p)));
            return o;
        }

        private static JsonObject Field(FieldInfo f)
        {
            var o = new JsonObject();
            o.Add("kind", f.IsLiteral ? "constant" : "field");
            o.Add("name", f.Name);
            o.Add("signature", f.Name);
            o.Add("visibility", Visibility(f));
            o.Add("type", Names.Display(f.FieldType));
            NoteExternal(f.FieldType);
            o.Add("static", f.IsStatic);
            o.Add("readonly", f.IsInitOnly);
            if (f.IsLiteral)
            {
                object v = f.GetRawConstantValue();
                o.Add("value", FormatConstant(v));
            }
            o.Add("attributes", Attributes(CustomAttributeData.GetCustomAttributes(f)));
            return o;
        }

        private static JsonObject Event(EventInfo e)
        {
            var o = new JsonObject();
            o.Add("kind", "event");
            o.Add("name", e.Name);
            o.Add("signature", e.Name);
            MethodInfo add = e.GetAddMethod(true);
            o.Add("visibility", Visibility(add));
            o.Add("type", Names.Display(e.EventHandlerType));
            NoteExternal(e.EventHandlerType);
            o.Add("static", add != null && add.IsStatic);
            o.Add("attributes", Attributes(CustomAttributeData.GetCustomAttributes(e)));
            return o;
        }

        private static int MemberOrder(JsonObject o)
        {
            string kind = (string)o.Pairs[0].Value;
            switch (kind)
            {
                case "constant": return 0;
                case "field": return 1;
                case "constructor": return 2;
                case "property": return 3;
                case "indexer": return 4;
                case "event": return 5;
                case "method": return 6;
                case "operator": return 7;
                default: return 8;
            }
        }

        private static string MemberSignature(JsonObject o)
        {
            foreach (var kv in o.Pairs) { if (kv.Key == "signature") { return (string)kv.Value; } }
            return "";
        }

        private static List<object> Members(Type t)
        {
            var members = new List<JsonObject>();
            const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
            foreach (ConstructorInfo c in t.GetConstructors(flags))
            {
                if (IsVisible(c)) { members.Add(Constructor(c)); }
            }
            foreach (MethodInfo m in t.GetMethods(flags))
            {
                if (!IsVisible(m)) { continue; }
                if (m.IsSpecialName && !m.Name.StartsWith("op_", StringComparison.Ordinal)) { continue; } // accessors
                members.Add(Method(m));
            }
            foreach (PropertyInfo p in t.GetProperties(flags))
            {
                if (IsVisible(p.GetGetMethod(true)) || IsVisible(p.GetSetMethod(true))) { members.Add(Property(p)); }
            }
            foreach (FieldInfo f in t.GetFields(flags))
            {
                if (t.IsEnum) { continue; } // enum values are reported separately; value__ is CLR storage, not API
                if (f.IsPublic || f.IsFamily || f.IsFamilyOrAssembly) { members.Add(Field(f)); }
            }
            foreach (EventInfo e in t.GetEvents(flags))
            {
                if (IsVisible(e.GetAddMethod(true))) { members.Add(Event(e)); }
            }
            members.Sort((x, y) =>
            {
                int k = MemberOrder(x).CompareTo(MemberOrder(y));
                if (k != 0) { return k; }
                return string.CompareOrdinal(MemberSignature(x), MemberSignature(y));
            });
            var result = new List<object>();
            foreach (JsonObject o in members) { result.Add(o); }
            return result;
        }

        private static List<object> EnumValues(Type t)
        {
            var list = new List<object>();
            var entries = new List<KeyValuePair<string, ulong>>();
            foreach (FieldInfo f in t.GetFields(BindingFlags.Public | BindingFlags.Static))
            {
                object raw = f.GetRawConstantValue();
                ulong value = Convert.ToUInt64(Convert.ToInt64(raw, CultureInfo.InvariantCulture) & unchecked((long)0xFFFFFFFFFFFFFFFFUL));
                entries.Add(new KeyValuePair<string, ulong>(f.Name, value));
            }
            entries.Sort((x, y) =>
            {
                int c = x.Value.CompareTo(y.Value);
                return c != 0 ? c : string.CompareOrdinal(x.Key, y.Key);
            });
            foreach (var e in entries)
            {
                var o = new JsonObject();
                o.Add("name", e.Key);
                long signed = unchecked((long)e.Value);
                o.Add("value", signed);
                list.Add(o);
            }
            return list;
        }

        private static JsonObject TypeEntry(Type t)
        {
            var o = new JsonObject();
            o.Add("fullName", Names.Clr(t));
            o.Add("displayName", Names.Display(t));
            o.Add("namespace", t.Namespace);
            o.Add("name", t.Name);
            o.Add("assembly", t.Assembly.GetName().Name);
            o.Add("kind", Kind(t));
            o.Add("visibility", TypeVisibility(t));
            if (t.IsNested) { o.Add("declaringType", Names.Display(t.DeclaringType)); }
            o.Add("abstract", t.IsAbstract && !t.IsInterface && !(t.IsAbstract && t.IsSealed));
            o.Add("sealed", t.IsSealed && !t.IsValueType && !(t.IsAbstract && t.IsSealed));
            o.Add("static", t.IsAbstract && t.IsSealed && t.IsClass);
            if (t.IsGenericTypeDefinition) { o.Add("genericParameters", GenericParameters(t.GetGenericArguments())); }
            if (t.BaseType != null && t.BaseType != typeof(object) && t.BaseType != typeof(ValueType) && t.BaseType != typeof(Enum) && t.BaseType != typeof(MulticastDelegate))
            {
                o.Add("baseType", Names.Display(t.BaseType));
                NoteExternal(t.BaseType);
            }
            else { o.Add("baseType", null); }
            var interfaces = new List<string>();
            foreach (Type i in t.GetInterfaces())
            {
                interfaces.Add(Names.Display(i));
                NoteExternal(i);
            }
            interfaces.Sort(StringComparer.Ordinal);
            o.Add("interfaces", interfaces);
            if (t.IsEnum)
            {
                o.Add("underlyingType", Names.Display(Enum.GetUnderlyingType(t)));
                o.Add("flags", t.IsDefined(typeof(FlagsAttribute), false));
                o.Add("values", EnumValues(t));
            }
            if (Kind(t) == "delegate")
            {
                MethodInfo invoke = t.GetMethod("Invoke");
                if (invoke != null)
                {
                    o.Add("delegateReturnType", Names.Display(invoke.ReturnType));
                    o.Add("delegateParameters", Parameters(invoke.GetParameters()));
                }
            }
            var nested = new List<string>();
            foreach (Type n in t.GetNestedTypes(BindingFlags.Public | BindingFlags.NonPublic))
            {
                if (IsVisibleType(n)) { nested.Add(Names.Display(n)); }
            }
            nested.Sort(StringComparer.Ordinal);
            o.Add("nestedTypes", nested);
            o.Add("attributes", Attributes(CustomAttributeData.GetCustomAttributes(t)));
            o.Add("members", Members(t));
            return o;
        }

        private static Type[] SafeGetTypes(Assembly asm)
        {
            try { return asm.GetTypes(); }
            catch (ReflectionTypeLoadException ex)
            {
                notes.Add(asm.GetName().Name + ": " + ex.LoaderExceptions.Length + " type(s) failed to load and are omitted");
                var list = new List<Type>();
                foreach (Type t in ex.Types) { if (t != null) { list.Add(t); } }
                return list.ToArray();
            }
        }

        private static Type FindBase(Type t, string genericDefinitionFullName)
        {
            for (Type b = t; b != null; b = b.BaseType)
            {
                if (b.IsGenericType && b.GetGenericTypeDefinition().FullName == genericDefinitionFullName) { return b; }
            }
            return null;
        }

        private static bool DerivesFrom(Type t, string fullName)
        {
            for (Type b = t.BaseType; b != null; b = b.BaseType)
            {
                string n = b.IsGenericType ? b.GetGenericTypeDefinition().FullName : b.FullName;
                if (n == fullName) { return true; }
            }
            return false;
        }

        /// <summary>Finds an attribute of the named type or of any type derived from it; the built-in
        /// components carry internal Localized*Attribute subclasses whose properties resolve the
        /// resource-backed display name, and reading those properties is the black-box view MSBuild gets.</summary>
        private static object AttributeInstance(Type t, string attributeFullName)
        {
            foreach (object a in t.GetCustomAttributes(false))
            {
                for (Type at = a.GetType(); at != null; at = at.BaseType)
                {
                    if (at.FullName == attributeFullName) { return a; }
                }
            }
            return null;
        }

        /// <summary>C++/CLI emits public managed shims for native headers it compiled (`std`, the FBX
        /// SDK namespace, `&lt;CrtImplementationDetails&gt;`). They are toolchain artifacts with no XML
        /// documentation and no XNA meaning, so they are inventoried under their own key and excluded
        /// from the API denominator rather than silently dropped.</summary>
        private static bool IsToolchainArtifact(Type t)
        {
            string ns = t.Namespace ?? "";
            return ns == "std" || ns == "<CrtImplementationDetails>" || ns.StartsWith("fbxsdk_", StringComparison.Ordinal)
                || ns == "<CppImplementationDetails>";
        }

        private static object ReadProperty(object instance, string name)
        {
            PropertyInfo p = instance.GetType().GetProperty(name);
            return p == null ? null : p.GetValue(instance, null);
        }

        private static object InstantiatedValue(object value)
        {
            if (value == null) { return null; }
            if (value is string) { return value; }
            if (value is bool) { return value; }
            if (value is char) { return "'\\u" + ((int)(char)value).ToString("x4", CultureInfo.InvariantCulture) + "' (" + ((int)(char)value).ToString(CultureInfo.InvariantCulture) + ")"; }
            if (value is Enum) { return value.GetType().Name + "." + value.ToString(); }
            if (value is float) { return ((float)value).ToString("R", CultureInfo.InvariantCulture); }
            if (value is double) { return ((double)value).ToString("R", CultureInfo.InvariantCulture); }
            if (value is int || value is long || value is short || value is byte || value is uint || value is ulong || value is ushort || value is sbyte)
            {
                return Convert.ToString(value, CultureInfo.InvariantCulture);
            }
            // Framework value types (Color, Vector3, ...) print through their own ToString.
            return value.GetType().Name + ":" + Convert.ToString(value, CultureInfo.InvariantCulture);
        }

        private static JsonObject ImporterEntry(Type t, object attribute)
        {
            var o = new JsonObject();
            o.Add("type", Names.Display(t));
            o.Add("assembly", t.Assembly.GetName().Name);
            o.Add("visibility", TypeVisibility(t));
            var extensions = new List<string>();
            var fe = ReadProperty(attribute, "FileExtensions") as System.Collections.IEnumerable;
            if (fe != null) { foreach (object e in fe) { extensions.Add(Convert.ToString(e, CultureInfo.InvariantCulture)); } }
            o.Add("fileExtensions", extensions);
            o.Add("fileExtensionsOrder", "as declared in the attribute");
            o.Add("defaultProcessor", ReadProperty(attribute, "DefaultProcessor"));
            o.Add("displayName", ReadProperty(attribute, "DisplayName"));
            o.Add("cacheImportedData", ReadProperty(attribute, "CacheImportedData"));
            Type generic = FindBase(t, PipelineNamespaceRoot + ".ContentImporter`1");
            o.Add("outputType", generic == null ? null : Names.Display(generic.GetGenericArguments()[0]));
            if (generic != null) { NoteExternal(generic.GetGenericArguments()[0]); }
            o.Add("abstract", t.IsAbstract);
            return o;
        }

        private static JsonObject ProcessorEntry(Type t, object attribute)
        {
            var o = new JsonObject();
            o.Add("type", Names.Display(t));
            o.Add("assembly", t.Assembly.GetName().Name);
            o.Add("visibility", TypeVisibility(t));
            o.Add("hasContentProcessorAttribute", attribute != null);
            o.Add("displayName", attribute == null ? null : ReadProperty(attribute, "DisplayName"));
            Type generic = FindBase(t, PipelineNamespaceRoot + ".ContentProcessor`2");
            if (generic != null)
            {
                Type[] args = generic.GetGenericArguments();
                o.Add("inputType", Names.Display(args[0]));
                o.Add("outputType", Names.Display(args[1]));
                NoteExternal(args[0]);
                NoteExternal(args[1]);
            }
            else
            {
                o.Add("inputType", null);
                o.Add("outputType", null);
            }
            o.Add("abstract", t.IsAbstract);
            object instance = null;
            string instantiation = null;
            if (!t.IsAbstract && !t.IsGenericTypeDefinition)
            {
                try
                {
                    instance = Activator.CreateInstance(t);
                    instantiation = "ok";
                }
                catch (Exception ex)
                {
                    Exception inner = ex is TargetInvocationException && ex.InnerException != null ? ex.InnerException : ex;
                    instantiation = inner.GetType().FullName + ": " + inner.Message;
                }
            }
            else { instantiation = "not instantiable"; }
            o.Add("instantiation", instantiation);
            var properties = new List<JsonObject>();
            foreach (PropertyInfo p in t.GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (p.GetIndexParameters().Length > 0) { continue; }
                MethodInfo getter = p.GetGetMethod(false);
                MethodInfo setter = p.GetSetMethod(false);
                if (getter == null && setter == null) { continue; }
                var po = new JsonObject();
                po.Add("name", p.Name);
                po.Add("type", Names.Display(p.PropertyType));
                NoteExternal(p.PropertyType);
                po.Add("declaringType", Names.Display(p.DeclaringType));
                po.Add("get", getter != null);
                po.Add("set", setter != null);
                po.Add("configurable", getter != null && setter != null);
                po.Add("attributes", Attributes(CustomAttributeData.GetCustomAttributes(p)));
                if (instance != null && getter != null)
                {
                    try { po.Add("defaultValue", InstantiatedValue(getter.Invoke(instance, null))); }
                    catch (Exception ex)
                    {
                        Exception inner = ex is TargetInvocationException && ex.InnerException != null ? ex.InnerException : ex;
                        po.Add("defaultValueError", inner.GetType().FullName + ": " + inner.Message);
                    }
                }
                properties.Add(po);
            }
            properties.Sort((x, y) => string.CompareOrdinal((string)x.Pairs[0].Value, (string)y.Pairs[0].Value));
            var propertyList = new List<object>();
            foreach (JsonObject p in properties) { propertyList.Add(p); }
            o.Add("properties", propertyList);
            o.Add("defaultValueSource", "parameterless constructor, public getter read back (black-box)");
            return o;
        }

        private static int Main(string[] args)
        {
            if (args.Length != 2)
            {
                Console.Error.WriteLine("usage: PipelineApiOracle.exe <references-dir> <output.json>");
                return 2;
            }
            referencesDirectory = Path.GetFullPath(args[0]);
            string output = args[1];
            AppDomain.CurrentDomain.AssemblyResolve += Resolve;

            var root = new JsonObject();
            root.Add("schema", "cna.xna40.content-pipeline-api/1");
            root.Add("generator", "tools/xna-pipeline-oracle/PipelineApiOracle.cs");
            root.Add("scope", "public and protected surface of the Microsoft XNA Game Studio 4.0 Content Pipeline assemblies, read from assembly metadata only; processor default values observed by constructing each processor and reading its public properties");
            root.Add("clrRuntime", Environment.Version.ToString());

            var assemblies = new List<object>();
            var loaded = new List<Assembly>();
            foreach (string name in PipelineAssemblyNames)
            {
                string path = Path.Combine(referencesDirectory, name + ".dll");
                if (!File.Exists(path))
                {
                    Console.Error.WriteLine("missing assembly: " + path);
                    return 3;
                }
                Assembly asm = Assembly.LoadFrom(path);
                loaded.Add(asm);
                var a = new JsonObject();
                AssemblyName an = asm.GetName();
                a.Add("name", an.Name);
                a.Add("version", an.Version.ToString());
                a.Add("culture", string.IsNullOrEmpty(an.CultureInfo.Name) ? "neutral" : an.CultureInfo.Name);
                var token = new StringBuilder();
                foreach (byte b in an.GetPublicKeyToken()) { token.Append(b.ToString("x2", CultureInfo.InvariantCulture)); }
                a.Add("publicKeyToken", token.ToString());
                a.Add("fileName", Path.GetFileName(path));
                a.Add("fileSize", new FileInfo(path).Length);
                a.Add("sha256", Sha256(path));
                a.Add("mvid", asm.ManifestModule.ModuleVersionId.ToString());
                a.Add("imageRuntimeVersion", asm.ImageRuntimeVersion);
                PortableExecutableKinds peKind;
                ImageFileMachine machine;
                asm.ManifestModule.GetPEKind(out peKind, out machine);
                a.Add("peKind", peKind.ToString());
                a.Add("machine", machine.ToString());
                var refs = new List<string>();
                foreach (AssemblyName r in asm.GetReferencedAssemblies()) { refs.Add(r.Name + ", " + r.Version); }
                refs.Sort(StringComparer.Ordinal);
                a.Add("references", refs);
                assemblies.Add(a);
            }
            root.Add("assemblies", assemblies);

            var typeEntries = new List<JsonObject>();
            var importers = new List<JsonObject>();
            var processors = new List<JsonObject>();
            var writers = new List<string>();
            var serializers = new List<string>();
            int nonPublicAttributed = 0;
            var toolchainArtifacts = new List<string>();
            var unreachableNested = new List<string>();
            var typeCountsByAssembly = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var typeCountsByNamespace = new SortedDictionary<string, int>(StringComparer.Ordinal);
            foreach (Assembly asm in loaded)
            {
                foreach (Type t in SafeGetTypes(asm))
                {
                    object importerAttribute = AttributeInstance(t, PipelineNamespaceRoot + ".ContentImporterAttribute");
                    object processorAttribute = AttributeInstance(t, PipelineNamespaceRoot + ".ContentProcessorAttribute");
                    if (!IsVisibleType(t))
                    {
                        if (importerAttribute != null || processorAttribute != null) { nonPublicAttributed++; }
                        if (t.IsNestedPublic || t.IsNestedFamily || t.IsNestedFamORAssem) { unreachableNested.Add(asm.GetName().Name + ": " + Names.Display(t)); }
                        continue;
                    }
                    if (IsToolchainArtifact(t))
                    {
                        toolchainArtifacts.Add(asm.GetName().Name + ": " + Names.Display(t));
                        continue;
                    }
                    typeEntries.Add(TypeEntry(t));
                    string asmName = asm.GetName().Name;
                    typeCountsByAssembly[asmName] = (typeCountsByAssembly.ContainsKey(asmName) ? typeCountsByAssembly[asmName] : 0) + 1;
                    string ns = t.Namespace ?? "";
                    typeCountsByNamespace[ns] = (typeCountsByNamespace.ContainsKey(ns) ? typeCountsByNamespace[ns] : 0) + 1;
                    if (importerAttribute != null) { importers.Add(ImporterEntry(t, importerAttribute)); }
                    bool isProcessor = DerivesFrom(t, PipelineNamespaceRoot + ".ContentProcessor`2") && !t.IsGenericTypeDefinition;
                    if (processorAttribute != null || (isProcessor && !t.IsAbstract)) { processors.Add(ProcessorEntry(t, processorAttribute)); }
                    if (DerivesFrom(t, PipelineNamespaceRoot + ".Serialization.Compiler.ContentTypeWriter")) { writers.Add(Names.Display(t)); }
                    if (DerivesFrom(t, PipelineNamespaceRoot + ".Serialization.Intermediate.ContentTypeSerializer")) { serializers.Add(Names.Display(t)); }
                }
            }
            typeEntries.Sort((x, y) => string.CompareOrdinal((string)x.Pairs[0].Value, (string)y.Pairs[0].Value));
            importers.Sort((x, y) => string.CompareOrdinal((string)x.Pairs[0].Value, (string)y.Pairs[0].Value));
            processors.Sort((x, y) => string.CompareOrdinal((string)x.Pairs[0].Value, (string)y.Pairs[0].Value));
            writers.Sort(StringComparer.Ordinal);
            serializers.Sort(StringComparer.Ordinal);

            // The runtime assembly's content-serialization attributes are part of the pipeline
            // programming model (IntermediateSerializer and the automatic writers honour them), so
            // they are inventoried as an auxiliary section rather than silently left out.
            var auxiliary = new List<JsonObject>();
            string frameworkPath = Path.Combine(referencesDirectory, "Microsoft.Xna.Framework.dll");
            if (File.Exists(frameworkPath))
            {
                Assembly framework = Assembly.LoadFrom(frameworkPath);
                foreach (Type t in SafeGetTypes(framework))
                {
                    if (!IsVisibleType(t)) { continue; }
                    if (t.Namespace == "Microsoft.Xna.Framework.Content" && t.Name.StartsWith("ContentSerializer", StringComparison.Ordinal))
                    {
                        auxiliary.Add(TypeEntry(t));
                    }
                }
                auxiliary.Sort((x, y) => string.CompareOrdinal((string)x.Pairs[0].Value, (string)y.Pairs[0].Value));
            }
            else { notes.Add("Microsoft.Xna.Framework.dll not beside the pipeline assemblies; auxiliary ContentSerializer* attributes omitted"); }

            var counts = new JsonObject();
            counts.Add("publicTypes", typeEntries.Count);
            int memberCount = 0;
            foreach (JsonObject t in typeEntries)
            {
                foreach (var kv in t.Pairs) { if (kv.Key == "members") { memberCount += ((List<object>)kv.Value).Count; } }
            }
            counts.Add("publicAndProtectedMembers", memberCount);
            counts.Add("importers", importers.Count);
            int extensionCount = 0;
            var allExtensions = new SortedDictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
            foreach (JsonObject imp in importers)
            {
                string typeName = (string)imp.Pairs[0].Value;
                foreach (var kv in imp.Pairs)
                {
                    if (kv.Key != "fileExtensions") { continue; }
                    foreach (string e in (List<string>)kv.Value)
                    {
                        extensionCount++;
                        if (!allExtensions.ContainsKey(e)) { allExtensions[e] = new List<string>(); }
                        allExtensions[e].Add(typeName);
                    }
                }
            }
            counts.Add("importerExtensionDeclarations", extensionCount);
            counts.Add("distinctExtensions", allExtensions.Count);
            counts.Add("processors", processors.Count);
            int propertyCount = 0;
            foreach (JsonObject p in processors)
            {
                foreach (var kv in p.Pairs) { if (kv.Key == "properties") { propertyCount += ((List<object>)kv.Value).Count; } }
            }
            counts.Add("processorProperties", propertyCount);
            counts.Add("nonPublicTypesCarryingImporterOrProcessorAttribute", nonPublicAttributed);
            counts.Add("excludedToolchainArtifactTypes", toolchainArtifacts.Count);
            counts.Add("excludedUnreachableNestedPublicTypes", unreachableNested.Count);
            var byAssembly = new JsonObject();
            foreach (var kv in typeCountsByAssembly) { byAssembly.Add(kv.Key, kv.Value); }
            counts.Add("publicTypesByAssembly", byAssembly);
            var byNamespace = new JsonObject();
            foreach (var kv in typeCountsByNamespace) { byNamespace.Add(kv.Key, kv.Value); }
            counts.Add("publicTypesByNamespace", byNamespace);
            root.Add("counts", counts);

            var extensionIndex = new JsonObject();
            foreach (var kv in allExtensions)
            {
                kv.Value.Sort(StringComparer.Ordinal);
                extensionIndex.Add(kv.Key, kv.Value);
            }
            root.Add("extensionIndex", extensionIndex);

            var importerList = new List<object>(); foreach (JsonObject i in importers) { importerList.Add(i); }
            var processorList = new List<object>(); foreach (JsonObject p in processors) { processorList.Add(p); }
            var typeList = new List<object>(); foreach (JsonObject t in typeEntries) { typeList.Add(t); }
            var auxiliaryList = new List<object>(); foreach (JsonObject t in auxiliary) { auxiliaryList.Add(t); }
            root.Add("importers", importerList);
            root.Add("processors", processorList);
            root.Add("contentTypeWriterDerivedPublicTypes", writers);
            root.Add("contentTypeSerializerDerivedPublicTypes", serializers);
            root.Add("types", typeList);
            root.Add("auxiliaryFrameworkTypes", auxiliaryList);
            var external = new JsonObject();
            foreach (var kv in externalTypes) { external.Add(kv.Key, kv.Value); }
            root.Add("externalTypesReferencedByPublicSurface", external);
            toolchainArtifacts.Sort(StringComparer.Ordinal);
            root.Add("excludedToolchainArtifactTypes", toolchainArtifacts);
            unreachableNested.Sort(StringComparer.Ordinal);
            root.Add("excludedUnreachableNestedPublicTypes", unreachableNested);
            root.Add("excludedUnreachableNestedPublicRule", "a public or protected type nested inside a non-public declaring type cannot be named by any consumer, so it is not API; the ones found are listed above rather than dropped silently");
            root.Add("excludedToolchainArtifactRule", "public types in the namespaces std, <CrtImplementationDetails>, <CppImplementationDetails> and fbxsdk_* are C++/CLI compiler shims for native headers inside the mixed-mode importer assemblies; they carry no XML documentation and are not XNA Content Pipeline API");
            notes.Sort(StringComparer.Ordinal);
            root.Add("notes", notes);

            File.WriteAllText(output, Json.Serialize(root), new UTF8Encoding(false));
            Console.WriteLine("types: " + typeEntries.Count + "  members: " + memberCount + "  importers: " + importers.Count + "  extensions: " + allExtensions.Count + "  processors: " + processors.Count + "  properties: " + propertyCount);
            return 0;
        }
    }
}
