// SPDX-License-Identifier: MS-PL
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace CNA.FnaReference
{
    // The pins are supplied by the build/run recipe, independently of assembly resolution.
    internal static class ReferenceMetadata
    {
        private static string sourceDirectory, revision, assemblyPath, assemblyHash;
        private static Assembly reference;
        private static readonly Dictionary<string, IntPtr> handles = new();
        private static readonly Dictionary<string, JsonWriter> native = new();
        private static readonly Dictionary<string, string> nativePaths = new();
        private static readonly Dictionary<string, string> nativeHashes = new();

        internal static string Required(string name)
        {
            string value = Environment.GetEnvironmentVariable(name);
            if (string.IsNullOrWhiteSpace(value))
                throw new InvalidOperationException("Required reference setting is unset: " + name);
            return value;
        }

        internal static string Hash(string path)
        {
            using var stream = File.OpenRead(path);
            return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
        }

        private static string Git(params string[] arguments)
        {
            var start = new ProcessStartInfo("git") { RedirectStandardOutput = true,
                RedirectStandardError = true, UseShellExecute = false };
            start.ArgumentList.Add("-C");
            start.ArgumentList.Add(sourceDirectory);
            foreach (string argument in arguments) start.ArgumentList.Add(argument);
            using var process = Process.Start(start);
            string output = process.StandardOutput.ReadToEnd();
            string error = process.StandardError.ReadToEnd();
            process.WaitForExit();
            if (process.ExitCode != 0)
                throw new InvalidOperationException("FNA source validation failed (git " +
                    string.Join(" ", arguments) + ", exit " + process.ExitCode + "): " + error);
            return output.Trim();
        }

        internal static void Initialize(bool effects)
        {
            sourceDirectory = Path.GetFullPath(Required("FNA_SOURCE_DIR"));
            revision = Git("rev-parse", "HEAD");
            string expectedRevision = Required("FNA_EXPECTED_REVISION");
            if (!Regex.IsMatch(revision, "^[0-9a-f]{40}$") || revision != expectedRevision)
                throw new InvalidOperationException("Unexpected FNA source revision: " + revision);
            Git("diff", "--quiet", "--no-ext-diff", "--ignore-submodules=none", "HEAD", "--");
            assemblyPath = Path.GetFullPath(Required("FNA_ASSEMBLY_PATH"));
            assemblyHash = Hash(assemblyPath);
            if (assemblyHash != Required("FNA_EXPECTED_SHA256"))
                throw new InvalidOperationException("Unexpected FNA.dll SHA-256: " + assemblyHash);
            reference = Assembly.Load("FNA");
            if (reference.GetName().Name != "FNA" || Hash(reference.Location) != assemblyHash)
                throw new InvalidOperationException("Loaded FNA.dll differs from the configured reference");

            // FNA's module initializer installs its own resolver when this file exists.
            // Refuse that competing policy rather than silently permitting a fallback library.
            if (File.Exists(Path.Combine(AppContext.BaseDirectory, "FNA.dll.config")))
                throw new InvalidOperationException("Remove FNA.dll.config from the harness output; explicit native pins are required");
            RuntimeHelpers.RunModuleConstructor(reference.ManifestModule.ModuleHandle);
            NativeLibrary.SetDllImportResolver(reference, Resolve);
            NativeLibrary.SetDllImportResolver(typeof(ReferenceMetadata).Assembly, Resolve);
            if (effects)
            {
                if (!OperatingSystem.IsLinux())
                    throw new PlatformNotSupportedException("Native provenance currently requires Linux dladdr and /proc/self/maps");
                if (Environment.GetEnvironmentVariable("FNA_PLATFORM_BACKEND") == "SDL2")
                    throw new InvalidOperationException("The effect probes require SDL3, not SDL2");
                LoadNative("SDL3", "SDL_GetVersion");
                LoadNative("FNA3D", "FNA3D_LinkedVersion");
                Version version = reference.GetName().Version;
                int expected = version.Major * 10000 + version.Minor * 100 + version.Build;
                if (VersionOf("FNA3D", "FNA3D_LinkedVersion") != expected)
                    throw new InvalidOperationException("FNA/FNA3D version mismatch");
                int sdl = VersionOf("SDL3", "SDL_GetVersion");
                if (sdl / 1000000 != 3)
                    throw new InvalidOperationException("The resolved SDL library is not SDL3");
                CheckNativeMappings();
            }
        }

        private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? search)
        {
            if (handles.TryGetValue(name, out IntPtr handle)) return handle;
            // libdl is used only to identify the library owning an actual resolved export.
            if (name == "libdl.so.2") return NativeLibrary.Load(name);
            throw new DllNotFoundException("Unpinned native dependency requested: " + name);
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int NativeVersion();

        [StructLayout(LayoutKind.Sequential)]
        private struct DlInfo
        {
            internal IntPtr FileName, BaseAddress, SymbolName, SymbolAddress;
        }

        [DllImport("libdl.so.2", CallingConvention = CallingConvention.Cdecl)]
        private static extern int dladdr(IntPtr address, out DlInfo info);

        private static int VersionOf(string name, string symbol) =>
            Marshal.GetDelegateForFunctionPointer<NativeVersion>(NativeLibrary.GetExport(handles[name], symbol))();

        private static void LoadNative(string name, string versionSymbol)
        {
            string path = Path.GetFullPath(Required(name + "_LIBRARY_PATH"));
            string hash = Hash(path);
            if (hash != Required(name + "_EXPECTED_SHA256"))
                throw new InvalidOperationException("Unexpected " + name + " SHA-256");
            IntPtr handle = NativeLibrary.Load(path);
            handles.Add(name, handle);
            IntPtr symbol = NativeLibrary.GetExport(handle, versionSymbol);
            if (dladdr(symbol, out DlInfo info) == 0)
                throw new InvalidOperationException("Cannot identify resolved " + name + " export");
            string actualPath = Path.GetFullPath(Marshal.PtrToStringUTF8(info.FileName));
            if (Hash(actualPath) != hash)
                throw new InvalidOperationException("Resolved " + name + " differs from its configured hash");
            native.Add(name, new JsonWriter().Add("status", "loaded").Add("sha256", hash)
                .Add("version", VersionOf(name, versionSymbol)));
            nativePaths.Add(name, actualPath);
            nativeHashes.Add(name, hash);
        }

        private static void CheckNativeMappings()
        {
            // A transitive loader dependency may bypass the managed resolver. Refuse a second
            // SDL/FNA3D identity; a matching filename alone is never compatibility evidence.
            foreach (string line in File.ReadLines("/proc/self/maps"))
            {
                int slash = line.IndexOf('/');
                if (slash < 0) continue;
                string path = line.Substring(slash);
                string file = Path.GetFileName(path);
                string name = file.StartsWith("libFNA3D") ? "FNA3D" :
                    file.StartsWith("libSDL") ? "SDL3" : null;
                if (name != null && (!nativePaths.ContainsKey(name) ||
                    Hash(path) != nativeHashes[name]))
                    throw new InvalidOperationException("Unexpected transitive native library: " + path);
            }
        }

        internal static JsonWriter Capture()
        {
            foreach (var entry in nativePaths)
                if (Hash(entry.Value) != nativeHashes[entry.Key])
                    throw new InvalidOperationException("Native library changed during probe: " + entry.Key);
            if (OperatingSystem.IsLinux()) CheckNativeMappings();
            if (Git("rev-parse", "HEAD") != revision || Hash(reference.Location) != assemblyHash)
                throw new InvalidOperationException("Reference changed during probe execution");
            Git("diff", "--quiet", "--no-ext-diff", "--ignore-submodules=none", "HEAD", "--");
            var libraries = new JsonWriter();
            var paths = new JsonWriter();
            foreach (string name in new[] { "FNA3D", "SDL3" })
            {
                libraries.Add(name, native.TryGetValue(name, out var identity) ? identity :
                    new JsonWriter().Add("status", "not_loaded"));
                if (nativePaths.TryGetValue(name, out string path)) paths.Add(name, path);
            }
            return new JsonWriter().Add("reference", new JsonWriter()
                .Add("schemaVersion", 1).Add("sourceRevision", revision)
                .Add("assemblySha256", assemblyHash)
                .Add("assemblyVersion", reference.GetName().Version.ToString())
                .Add("nativeLibraries", libraries))
                .Add("sourceDirectory", sourceDirectory).Add("assemblyPath", reference.Location)
                .Add("nativePaths", paths);
        }
    }
}
