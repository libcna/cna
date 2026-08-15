// plan_fx.md FX-005: emit FNA's own reflection of a compiled Effect Framework binary as
// normalized JSON, so CNA's reflection can be compared against ground truth produced by *running*
// FNA rather than by reading its source.
//
// FNA builds that reflection in Effect.INTERNAL_parseEffectStruct, which reads the parsed
// MOJOSHADER_effect and fills the public Parameters/Techniques collections. That method touches
// no GraphicsDevice at all, so this tool does not need FNA's windowing or Game stack: it creates
// an FNA3D device directly through P/Invoke, asks FNA3D for the parsed effect, and then lets
// FNA's own code turn it into the public object graph.
//
// The native layer is deliberately the same one CNA uses -- the pinned FNA3D and MojoShader,
// including CNA's managed robustness patch. That is the point: the oracle is FNA's C# reflection
// mapping, not a second parser. A difference in this output is a difference in how CNA and FNA
// interpret an identical parse tree.
//
// Usage: mono FnaReference.exe --effects <directory-of-fxb> [output.json]

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using Microsoft.Xna.Framework.Graphics;

namespace CNA.FnaReference
{
	public static class EffectReflectionReference
	{
		[StructLayout(LayoutKind.Sequential)]
		private struct FNA3D_PresentationParameters
		{
			public int backBufferWidth;
			public int backBufferHeight;
			public int backBufferFormat;
			public int multiSampleCount;
			public IntPtr deviceWindowHandle;
			public byte isFullScreen;
			public int depthStencilFormat;
			public int presentationInterval;
			public int displayOrientation;
			public int renderTargetUsage;
		}

		[DllImport("FNA3D", CallingConvention = CallingConvention.Cdecl)]
		private static extern uint FNA3D_PrepareWindowAttributes();

		[DllImport("FNA3D", CallingConvention = CallingConvention.Cdecl)]
		private static extern IntPtr FNA3D_CreateDevice(
			ref FNA3D_PresentationParameters presentationParameters,
			byte debugMode
		);

		[DllImport("FNA3D", CallingConvention = CallingConvention.Cdecl)]
		private static extern void FNA3D_DestroyDevice(IntPtr device);

		[DllImport("FNA3D", CallingConvention = CallingConvention.Cdecl)]
		private static extern void FNA3D_CreateEffect(
			IntPtr device,
			byte[] effectCode,
			uint effectCodeLength,
			out IntPtr effect,
			out IntPtr effectData
		);

		[DllImport("FNA3D", CallingConvention = CallingConvention.Cdecl)]
		private static extern void FNA3D_AddDisposeEffect(IntPtr device, IntPtr effect);

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern int SDL_Init(uint flags);

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern void SDL_Quit();

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern IntPtr SDL_CreateWindow(
			byte[] title,   // UTF-8; .NET Framework 4.0's MarshalAs has no LPUTF8Str
			int w,
			int h,
			ulong flags
		);

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern void SDL_DestroyWindow(IntPtr window);

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern IntPtr SDL_GetError();

		private const uint SDL_INIT_VIDEO = 0x00000020;

		/// <summary>Runs FNA's own reflection over every .fxb in a directory.</summary>
		public static int Run(string effectDirectory, string outputPath)
		{
			if (!Directory.Exists(effectDirectory))
			{
				Console.Error.WriteLine("no such directory: " + effectDirectory);
				return 2;
			}

			if (SDL_Init(SDL_INIT_VIDEO) == 0)
			{
				Console.Error.WriteLine("SDL_Init failed: " +
					Marshal.PtrToStringAnsi(SDL_GetError()));
				return 3;
			}

			ulong flags = FNA3D_PrepareWindowAttributes();
			byte[] title = System.Text.Encoding.UTF8.GetBytes("FNA reflection oracle\0");
			IntPtr window = SDL_CreateWindow(title, 64, 64, flags);
			if (window == IntPtr.Zero)
			{
				Console.Error.WriteLine("SDL_CreateWindow failed: " +
					Marshal.PtrToStringAnsi(SDL_GetError()));
				SDL_Quit();
				return 4;
			}

			FNA3D_PresentationParameters parameters = new FNA3D_PresentationParameters();
			parameters.backBufferWidth = 64;
			parameters.backBufferHeight = 64;
			parameters.backBufferFormat = 0;   // FNA3D_SURFACEFORMAT_COLOR
			parameters.multiSampleCount = 0;
			parameters.deviceWindowHandle = window;
			parameters.isFullScreen = 0;
			parameters.depthStencilFormat = 2; // FNA3D_DEPTHFORMAT_D24S8
			parameters.presentationInterval = 0;
			parameters.displayOrientation = 0;
			parameters.renderTargetUsage = 1;  // PreserveContents

			IntPtr device = FNA3D_CreateDevice(ref parameters, 0);
			if (device == IntPtr.Zero)
			{
				Console.Error.WriteLine("FNA3D_CreateDevice failed");
				SDL_DestroyWindow(window);
				SDL_Quit();
				return 5;
			}

			var files = new List<string>(Directory.GetFiles(effectDirectory, "*.fxb"));
			files.Sort(StringComparer.Ordinal);

			var effects = new List<string>();
			foreach (string file in files)
			{
				Console.WriteLine("reflecting " + Path.GetFileName(file));
				effects.Add(ReflectOne(device, file));
			}

			var document = new JsonWriter()
				.Add("generator", "FnaReference --effects (plan_fx.md FX-005)")
				.Add("fnaAssembly", typeof(Effect).Assembly.FullName)
				.AddRaw("effects", "[" + string.Join(",", effects.ToArray()) + "]");

			File.WriteAllText(outputPath, document.ToString());
			Console.WriteLine("wrote " + outputPath);

			FNA3D_DestroyDevice(device);
			SDL_DestroyWindow(window);
			SDL_Quit();
			return 0;
		}

		private static string ReflectOne(IntPtr device, string file)
		{
			byte[] bytes = File.ReadAllBytes(file);
			IntPtr glEffect;
			IntPtr effectData;
			FNA3D_CreateEffect(device, bytes, (uint) bytes.Length, out glEffect, out effectData);
			if (glEffect == IntPtr.Zero || effectData == IntPtr.Zero)
			{
				return new JsonWriter()
					.Add("file", Path.GetFileName(file))
					.Add("error", "FNA3D_CreateEffect produced no effect")
					.ToString();
			}

			// FNA's own reflection, invoked directly: INTERNAL_parseEffectStruct reads only the
			// native struct, so an Effect that never ran its constructor is enough to host it.
			Effect effect = (Effect) FormatterServices.GetUninitializedObject(typeof(Effect));

			// GetUninitializedObject skips field initializers, so any reference field the parse
			// method writes into has to be created here. samplerMap is the one it uses.
			foreach (FieldInfo field in typeof(Effect).GetFields(
				BindingFlags.Instance | BindingFlags.NonPublic))
			{
				if (!field.FieldType.IsClass || field.FieldType == typeof(string)) continue;
				if (field.GetValue(effect) != null) continue;
				if (!field.FieldType.IsGenericType) continue;
				if (field.FieldType.GetGenericTypeDefinition() != typeof(Dictionary<,>)) continue;
				field.SetValue(effect, Activator.CreateInstance(field.FieldType));
			}

			MethodInfo parse = typeof(Effect).GetMethod(
				"INTERNAL_parseEffectStruct",
				BindingFlags.Instance | BindingFlags.NonPublic
			);
			if (parse == null)
			{
				throw new InvalidOperationException(
					"FNA's Effect.INTERNAL_parseEffectStruct is not present in this FNA build");
			}
			parse.Invoke(effect, new object[] { effectData });

			var parameters = new List<string>();
			foreach (EffectParameter parameter in effect.Parameters)
				parameters.Add(DescribeParameter(parameter, 0));

			var techniques = new List<string>();
			foreach (EffectTechnique technique in effect.Techniques)
			{
				var passes = new List<string>();
				foreach (EffectPass pass in technique.Passes)
				{
					passes.Add(new JsonWriter()
						.Add("name", pass.Name)
						.AddRaw("annotations", DescribeAnnotations(pass.Annotations))
						.ToString());
				}
				techniques.Add(new JsonWriter()
					.Add("name", technique.Name)
					.AddRaw("annotations", DescribeAnnotations(technique.Annotations))
					.AddRaw("passes", "[" + string.Join(",", passes.ToArray()) + "]")
					.ToString());
			}

			FNA3D_AddDisposeEffect(device, glEffect);

			return new JsonWriter()
				.Add("file", Path.GetFileName(file))
				.Add("byteCount", bytes.Length)
				.AddRaw("parameters", "[" + string.Join(",", parameters.ToArray()) + "]")
				.AddRaw("techniques", "[" + string.Join(",", techniques.ToArray()) + "]")
				.ToString();
		}

		private static string DescribeParameter(EffectParameter parameter, int depth)
		{
			var writer = new JsonWriter()
				.Add("name", parameter.Name)
				.Add("semantic", parameter.Semantic)
				.Add("class", parameter.ParameterClass.ToString())
				.Add("type", parameter.ParameterType.ToString())
				.Add("rowCount", parameter.RowCount)
				.Add("columnCount", parameter.ColumnCount)
				.AddRaw("annotations", DescribeAnnotations(parameter.Annotations));

			if (depth < 8)
			{
				// FNA leaves these collections null where it has nothing to report, rather than
				// empty; that difference is itself worth recording, so null is emitted as null.
				var elements = new List<string>();
				if (parameter.Elements != null)
				{
					foreach (EffectParameter element in parameter.Elements)
						elements.Add(DescribeParameter(element, depth + 1));
					writer.AddRaw("elements", "[" + string.Join(",", elements.ToArray()) + "]");
				}
				else writer.AddRaw("elements", "null");

				var members = new List<string>();
				if (parameter.StructureMembers != null)
				{
					foreach (EffectParameter member in parameter.StructureMembers)
						members.Add(DescribeParameter(member, depth + 1));
					writer.AddRaw("structureMembers",
						"[" + string.Join(",", members.ToArray()) + "]");
				}
				else writer.AddRaw("structureMembers", "null");
			}
			return writer.ToString();
		}

		private static string DescribeAnnotations(EffectAnnotationCollection annotations)
		{
			if (annotations == null) return "null";
			var described = new List<string>();
			foreach (EffectAnnotation annotation in annotations)
			{
				described.Add(new JsonWriter()
					.Add("name", annotation.Name)
					.Add("semantic", annotation.Semantic)
					.Add("class", annotation.ParameterClass.ToString())
					.Add("type", annotation.ParameterType.ToString())
					.Add("rowCount", annotation.RowCount)
					.Add("columnCount", annotation.ColumnCount)
					.ToString());
			}
			return "[" + string.Join(",", described.ToArray()) + "]";
		}
	}
}
