// SPDX-License-Identifier: MS-PL
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Security.Cryptography;
using Microsoft.Xna.Framework.Audio;

class MicrophoneStartOracle
{
    static int Main()
    {
        var type = typeof(Microphone);
        using (var stream = File.OpenRead(type.Assembly.Location))
        using (var hash = SHA256.Create())
            Console.WriteLine("assembly=" + BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "").ToLowerInvariant());

        // Exercise the shipped native-error path without opening a physical capture device.
        var mic = (Microphone)FormatterServices.GetUninitializedObject(type);
        GC.SuppressFinalize(mic);
        var handle = type.GetField("Handle", BindingFlags.Instance | BindingFlags.NonPublic);
        var microphoneLock = type.GetField("microphoneLock", BindingFlags.Instance | BindingFlags.NonPublic);
        if (handle == null || microphoneLock == null)
        {
            Console.Error.WriteLine("The installed Microphone layout differs from this probe.");
            return 2;
        }
        handle.SetValue(mic, UInt32.MaxValue);
        microphoneLock.SetValue(mic, new object());
        Console.WriteLine("start-input-handle=" + handle.GetValue(mic));
        try
        {
            mic.Start();
            Console.WriteLine("start=success");
            return 1;
        }
        catch (Exception error)
        {
            Console.WriteLine("start=" + error.GetType().FullName
                + ";hr=" + Marshal.GetHRForException(error).ToString("X8")
                + ";message=" + error.Message);
            return error.GetType() == typeof(ArgumentException) ? 0 : 1;
        }
    }
}
