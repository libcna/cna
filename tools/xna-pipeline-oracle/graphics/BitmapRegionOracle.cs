// SPDX-License-Identifier: MS-PL
// Bounded black-box XNA bitmap overflow probe; the huge-width case validates only.
using System;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Content.Pipeline.Graphics;
class BitmapRegionOracle
{
    static void Run(string name, Action action)
    {
        try
        {
            action();
            Console.WriteLine(name + "=success");
        }
        catch (Exception error)
        {
            while (error is TargetInvocationException)
                error = error.InnerException;
            Console.WriteLine(
                name + "=" + error.GetType().FullName + ":" +
                (error is ArgumentException ? ((ArgumentException)error).ParamName : ""));
        }
    }
    static int Main()
    {
        foreach (var assembly in new[] { typeof(Color).Assembly, typeof(SurfaceFormat).Assembly,
                                         typeof(BitmapContent).Assembly })
        {
            using (var hash = SHA256.Create())
            using (var stream = File.OpenRead(assembly.Location))
                Console.WriteLine("assembly/" + assembly.GetName().Name + "=" +
                    BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "").ToLowerInvariant());
        }
        var source = new PixelBitmapContent<Color>(2, 2);
        var target = new PixelBitmapContent<Color>(2, 2);
        source.SetPixel(0, 0, Color.Red);
        source.SetPixel(1, 0, Color.Lime);
        source.SetPixel(0, 1, Color.Blue);
        source.SetPixel(1, 1, Color.White);
        var validate =
            typeof(BitmapContent)
                .GetMethod("ValidateCopyArguments", BindingFlags.Static | BindingFlags.NonPublic);
        var valid = new Rectangle(0, 0, 1, 1);
        string[] labels = { "src-x",           "src-y",       "dst-x",       "dst-y",
                            "src-large-width", "empty-src-x", "empty-dst-x", "outside-src-x" };
        Rectangle[] sources = { new Rectangle(int.MaxValue, 0, 1, 1),
                                new Rectangle(0, int.MaxValue, 1, 1),
                                valid,
                                valid,
                                new Rectangle(1, 0, int.MaxValue, 1),
                                new Rectangle(int.MaxValue, 0, 1, 0),
                                new Rectangle(0, 0, 1, 0),
                                new Rectangle(2, 0, 1, 1) };
        Rectangle[] targets = { valid,
                                valid,
                                new Rectangle(int.MaxValue, 0, 1, 1),
                                new Rectangle(0, int.MaxValue, 1, 1),
                                valid,
                                new Rectangle(0, 0, 1, 0),
                                new Rectangle(int.MaxValue, 0, 1, 0),
                                valid };
        for (int i = 0; i < labels.Length; i++)
        {
            int n = i;
            Console.WriteLine(labels[n] + "/edges=" + sources[n].Right + "," + sources[n].Bottom +
                              ";" + targets[n].Right + "," + targets[n].Bottom);
            Run(labels[n] + "/validate",
                () =>
                    validate.Invoke(null, new object[] { source, sources[n], target, targets[n] }));
            if (n != 4)
                Run(labels[n] + "/copy",
                    () => BitmapContent.Copy(source, sources[n], target, targets[n]));
            if (n != 4)
            {
                var converted = new PixelBitmapContent<Vector4>(2, 2);
                Run(labels[n] + "/convert",
                    () => BitmapContent.Copy(source, sources[n], converted, targets[n]));
                var resized = new PixelBitmapContent<Color>(2, 2);
                var region = targets[n];
                if (region.Width != 0)
                    region.Width = 2;
                Run(labels[n] + "/resize",
                    () =>
                    {
                        BitmapContent.Copy(source, sources[n], resized, region);
                        Console.WriteLine(labels[n] + "/pixels=" + resized.GetPixel(0, 0) + ";" +
                                          resized.GetPixel(1, 0));
                    });
            }
        }
        return 0;
    }
}
