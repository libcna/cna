// SPDX-License-Identifier: MS-PL
// Reflection-only metadata export for the Microsoft XNA 4.0 DLLs. The XML
// documentation remains the census; this supplies shapes XML cannot encode.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;

internal static class XnaRuntimeMetadata
{
    private static string E(string value) { return Uri.EscapeDataString(value ?? ""); }

    private static string TypeName(Type type)
    {
        if (type.IsByRef) return TypeName(type.GetElementType()) + "@";
        if (type.IsPointer) return TypeName(type.GetElementType()) + "*";
        if (type.IsArray) return TypeName(type.GetElementType()) + "[]";
        if (type.IsGenericParameter)
            return (type.DeclaringMethod == null ? "`" : "``") + type.GenericParameterPosition;
        if (type.IsGenericType && !type.IsGenericTypeDefinition)
        {
            var name = Regex.Replace(type.GetGenericTypeDefinition().FullName.Replace('+', '.'), @"`\d+", "");
            return name + "{" + String.Join(",", type.GetGenericArguments().Select(TypeName)) + "}";
        }
        return (type.FullName ?? type.Name).Replace('+', '.');
    }

    private static string Owner(Type type) { return (type.FullName ?? type.Name).Replace('+', '.'); }
    private static string Visibility(MethodBase method)
    {
        if (method.IsPublic) return "public";
        if (method.IsFamily || method.IsFamilyOrAssembly) return "protected";
        return "nonpublic";
    }
    private static string Visibility(FieldInfo field)
    {
        if (field.IsPublic) return "public";
        if (field.IsFamily || field.IsFamilyOrAssembly) return "protected";
        return "nonpublic";
    }
    private static string Params(ParameterInfo[] parameters)
    {
        return String.Join(",", parameters.Select(p => TypeName(p.ParameterType)));
    }
    private static void Emit(params string[] values)
    {
        Console.WriteLine(String.Join("\t", values.Select(E)));
    }
    private static void Method(Type type, MethodBase method, string prefix)
    {
        var parameters = method.GetParameters();
        var name = method.IsConstructor ? "#ctor" : method.Name.Replace('.', '#');
        if (method.IsGenericMethodDefinition) name += "``" + method.GetGenericArguments().Length;
        var id = prefix + Owner(type) + "." + name;
        if (parameters.Length != 0) id += "(" + Params(parameters) + ")";
        string returnType = method is MethodInfo ? TypeName(((MethodInfo)method).ReturnType) : "System.Void";
        Emit(id, method.IsConstructor ? "constructor" : "method", returnType,
             method.IsStatic ? "static" : "instance", Visibility(method),
             Params(parameters), String.Join(",", parameters.Select(p => p.IsOut ? "out" : p.ParameterType.IsByRef ? "ref" : "value")),
             method.IsGenericMethodDefinition ? method.GetGenericArguments().Length.ToString() : "0", "", "", "", "");
    }

    private static void Export(Type type)
    {
        var flags = BindingFlags.DeclaredOnly | BindingFlags.Public | BindingFlags.NonPublic |
                    BindingFlags.Instance | BindingFlags.Static;
        foreach (var method in type.GetConstructors(flags)) Method(type, method, "M:");
        foreach (var method in type.GetMethods(flags))
        {
            if (method.IsSpecialName && !method.Name.StartsWith("op_")) continue;
            Method(type, method, "M:");
        }
        foreach (var prop in type.GetProperties(flags))
        {
            var get = prop.GetGetMethod(true);
            var set = prop.GetSetMethod(true);
            var explicitInterface = prop.Name.Contains(".");
            var visible = new[] { get, set }.Where(m => m != null &&
                (Visibility(m) != "nonpublic" || explicitInterface)).ToArray();
            if (visible.Length == 0) continue;
            var id = "P:" + Owner(type) + "." + prop.Name.Replace('.', '#');
            var parameters = prop.GetIndexParameters();
            if (parameters.Length != 0) id += "(" + Params(parameters) + ")";
            Emit(id, "property", TypeName(prop.PropertyType), visible[0].IsStatic ? "static" : "instance",
                 visible.Any(m => m.IsPublic) ? "public" : explicitInterface ? "explicit_interface" : "protected", Params(parameters),
                 String.Join(",", parameters.Select(p => p.IsOut ? "out" : p.ParameterType.IsByRef ? "ref" : "value")),
                 "0", get != null && (Visibility(get) != "nonpublic" || explicitInterface) ? "1" : "0",
                 set != null && (Visibility(set) != "nonpublic" || explicitInterface) ? "1" : "0", "", "");
        }
        foreach (var field in type.GetFields(flags))
        {
            if (Visibility(field) == "nonpublic") continue;
            Emit("F:" + Owner(type) + "." + field.Name, "field", TypeName(field.FieldType),
                 field.IsStatic ? "static" : "instance", Visibility(field), "", "", "0", "", "",
                 field.IsLiteral ? "const" : field.IsInitOnly ? "readonly" : "mutable",
                 type.IsEnum && field.IsLiteral ? Convert.ToInt64(field.GetRawConstantValue()).ToString() : "");
        }
        foreach (var evt in type.GetEvents(flags))
        {
            var add = evt.GetAddMethod(true);
            if (add == null || Visibility(add) == "nonpublic") continue;
            Emit("E:" + Owner(type) + "." + evt.Name.Replace('.', '#'), "event", TypeName(evt.EventHandlerType),
                 add.IsStatic ? "static" : "instance", Visibility(add), "", "", "0", "", "", "", "");
        }
    }

    public static int Main(string[] args)
    {
        if (args.Length < 3) { Console.Error.WriteLine("usage: metadata.exe DIRECTORY DOCUMENTED-TYPES-FILE DLL..."); return 2; }
        var directory = args[0];
        var documented = new HashSet<string>(File.ReadAllLines(args[1]));
        AppDomain.CurrentDomain.ReflectionOnlyAssemblyResolve += (sender, e) =>
        {
            var name = new AssemblyName(e.Name).Name + ".dll";
            var path = Path.Combine(directory, name);
            return File.Exists(path) ? Assembly.ReflectionOnlyLoadFrom(path) : Assembly.ReflectionOnlyLoad(e.Name);
        };
        foreach (var dll in args.Skip(2))
        {
            var assembly = Assembly.ReflectionOnlyLoadFrom(Path.Combine(directory, dll));
            Type[] types;
            try { types = assembly.GetTypes(); }
            catch (ReflectionTypeLoadException error) { types = error.Types.Where(t => t != null).ToArray(); }
            foreach (var type in types)
                if (documented.Contains(Owner(type))) Export(type);
        }
        return 0;
    }
}
