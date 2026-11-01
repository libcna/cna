// SPDX-License-Identifier: MS-PL
// Black-box DateTime compiler output and genuine runtime reader observations.
using System;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using Microsoft.Xna.Framework.Content;
internal static class DateTimeOracle
{
    static int Main(string[] args)
    {
        if (args.Length != 2)
        {
            Console.Error.WriteLine(
                "Usage: DateTimeOracle <Microsoft pipeline DLL> <output directory>");
            return 2;
        }
        try
        {
            Run(args);
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    static void Run(string[] args)
    {
        Assembly runtime = typeof(ContentReader).Assembly;
        Console.WriteLine("runtime=" + runtime.FullName);
        Console.WriteLine("runtime-location=" + runtime.Location);
        using (var file = File.OpenRead(runtime.Location)) using (var hash = SHA256.Create())
        {
            Console.WriteLine(
                "runtime-sha256=" +
                BitConverter.ToString(hash.ComputeHash(file)).Replace("-", "").ToLowerInvariant());
        }
        Console.WriteLine("zone=" + TimeZoneInfo.Local.Id);
        Type type = runtime.GetType("Microsoft.Xna.Framework.Content.DateTimeReader", true);
        MethodInfo method =
            type.GetMethod("Read", BindingFlags.Instance | BindingFlags.NonPublic, null,
                           new Type[] { typeof(ContentReader), typeof(DateTime) }, null);
        long ticks = new DateTime(2026, 1, 15, 12, 0, 0).Ticks;
        for (int kind = 0; kind < 4; kind++)
        {
            ulong packed = (ulong)ticks | ((ulong)kind << 62);
            using (var stream = new MemoryStream(BitConverter.GetBytes(packed)))
            {
                object reader = Activator.CreateInstance(
                    typeof(ContentReader), BindingFlags.Instance | BindingFlags.NonPublic, null,
                    new object[] { null, stream, "datetime", null, 0 }, null);
                try
                {
                    DateTime value =
                        (DateTime)method.Invoke(Activator.CreateInstance(type, true),
                                                new object[] { reader, default(DateTime) });
                    Console.WriteLine("kind=" + kind + " inputTicks=" + ticks +
                                      " ticks=" + value.Ticks + " kindOut=" + value.Kind);
                }
                catch (TargetInvocationException e)
                {
                    Console.WriteLine("kind=" + kind +
                                      " error=" + e.InnerException.GetType().FullName);
                }
            }
        }
        if (args.Length == 2)
        {
            Assembly pipeline = Assembly.LoadFrom(args[0]);
            Console.WriteLine("pipeline=" + pipeline.FullName);
            using (var file = File.OpenRead(pipeline.Location)) using (var hash = SHA256.Create())
            {
                Console.WriteLine("pipeline-sha256=" + BitConverter.ToString(hash.ComputeHash(file))
                                                           .Replace("-", "")
                                                           .ToLowerInvariant());
            }
            Type compilerType = pipeline.GetType(
                "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentCompiler",
                true);
            object compiler = Activator.CreateInstance(compilerType, true);
            MethodInfo compile =
                compilerType.GetMethod("Compile", BindingFlags.Instance | BindingFlags.NonPublic);
            ParameterInfo[] parameters = compile.GetParameters();
            object platform = Enum.Parse(parameters[2].ParameterType, "Windows");
            object profile = Enum.Parse(parameters[3].ParameterType, "Reach");
            Directory.CreateDirectory(args[1]);
            for (int kind = 0; kind < 3; kind++)
            {
                DateTime value = DateTime.SpecifyKind(new DateTime(ticks), (DateTimeKind)kind);
                using (var output = new MemoryStream())
                {
                    compile.Invoke(compiler, new object[] { output, value, platform, profile, false,
                                                            args[1], args[1] });
                    byte[] bytes = output.ToArray();
                    File.WriteAllBytes(Path.Combine(args[1], "datetime_" + kind + ".xnb"), bytes);
                    using (var input = new BinaryReader(new MemoryStream(bytes)))
                    {
                        input.BaseStream.Position = 10;
                        int count = input.ReadByte();
                        for (int i = 0; i < count; i++)
                        {
                            input.ReadString();
                            input.ReadInt32();
                        }
                        int shared = input.ReadByte(), root = input.ReadByte();
                        ulong packed = input.ReadUInt64();
                        Console.WriteLine("writer kind=" + kind + " inputTicks=" + ticks +
                                          " packed=" + packed +
                                          " payloadTicks=" + (packed & 0x3fffffffffffffffUL) +
                                          " payloadKind=" + (packed >> 62) + " shared=" + shared +
                                          " root=" + root + " bytes=" + bytes.Length);
                    }
                }
            }
        }
    }
}
