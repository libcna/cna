// SPDX-License-Identifier: MS-PL
// Observes genuine XNA boxed Current without allocating graphics or gamer resources.
using System;
using System.Collections;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
internal static class CollectionEnumeratorOracle
{
    static void ReportAssembly(Assembly assembly)
    {
        Console.WriteLine("assembly=" + assembly.FullName);
        Console.WriteLine("location=" + assembly.Location);
        using (var file = File.OpenRead(assembly.Location)) using (var hash = SHA256.Create())
            Console.WriteLine(
                "sha256=" +
                BitConverter.ToString(hash.ComputeHash(file)).Replace("-", "").ToLowerInvariant());
    }
    static void PrintCurrent(string stage, IEnumerator cursor)
    {
        try
        {
            Console.WriteLine(stage + " Current=" + (cursor.Current == null ? "null" : "value"));
        }
        catch (Exception e)
        {
            Console.WriteLine(stage + " error=" + e.GetType().FullName);
        }
    }
    static object GetEnumerator(object collection)
    {
        foreach (var m in collection.GetType().GetMethods(
                     BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly))
            if (m.Name == "GetEnumerator" && m.GetParameters().Length == 0)
                return m.Invoke(collection, null);
        throw new MissingMethodException("GetEnumerator");
    }
    static void Probe(Type type)
    {
        Console.WriteLine("type=" + type.FullName);
        object collection = Activator.CreateInstance(type, true);
        object cursor = GetEnumerator(collection);
        Console.WriteLine("enumerator=" + cursor.GetType().FullName);
        PrintCurrent("default", (IEnumerator)Activator.CreateInstance(cursor.GetType()));
        var empty = (IEnumerator)cursor;
        PrintCurrent("empty-before", empty);
        Console.WriteLine("empty-move=" + empty.MoveNext());
        PrintCurrent("empty-after", empty);
        empty.Reset();
        PrintCurrent("empty-reset", empty);
        var list =
            (IList)type.GetField("wrappedList", BindingFlags.Instance | BindingFlags.NonPublic)
                .GetValue(collection);
        list.Add(null);
        var one = (IEnumerator)GetEnumerator(collection);
        PrintCurrent("one-before", one);
        Console.WriteLine("one-move=" + one.MoveNext());
        PrintCurrent("one-valid", one);
        Console.WriteLine("one-end=" + one.MoveNext());
        PrintCurrent("one-after", one);
        one.Reset();
        PrintCurrent("one-reset", one);
    }
    static int Main(string[] args)
    {
        if (args.Length != 2)
        {
            Console.Error.WriteLine(
                "Usage: CollectionEnumeratorOracle <Graphics DLL> <GamerServices DLL>");
            return 2;
        }
        try
        {
            var graphics = Assembly.LoadFrom(args[0]);
            var gamers = Assembly.LoadFrom(args[1]);
            ReportAssembly(graphics);
            ReportAssembly(gamers);
            Probe(graphics.GetType("Microsoft.Xna.Framework.Graphics.ModelEffectCollection", true));
            var generic =
                gamers.GetType("Microsoft.Xna.Framework.GamerServices.GamerCollection`1", true);
            Probe(generic.MakeGenericType(
                gamers.GetType("Microsoft.Xna.Framework.GamerServices.Gamer", true)));
            return 0;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine(e);
            return 1;
        }
    }
}
