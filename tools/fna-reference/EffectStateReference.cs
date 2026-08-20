// plans/plan_fx.md FX-005: emit the device state FNA itself installs when a compiled effect pass is
// applied, so CNA's state translation can be compared against ground truth produced by *running*
// FNA rather than by reading its source.
//
// This is the second half of the FX-005 oracle. The first half (EffectStateReference's sibling,
// EffectReflectionReference) compares the reflected object graph and touches no GraphicsDevice.
// A pass's *state* is different: FNA's Effect.INTERNAL_applyEffect walks the state changes
// MojoShader reports, folds them through PipelineCache, and assigns the results to the public
// GraphicsDevice.BlendState / DepthStencilState / RasterizerState / SamplerStates properties.
// CNA does the same thing in Effect::ApplyCompiledPassState, so those properties are directly
// comparable and are what this mode records.
//
// Unlike the reflection mode, this needs a real managed GraphicsDevice, because PipelineCache and
// the property assignments live on it. It is built directly from an SDL window rather than through
// FNA's Game stack, which keeps the tool free of windowing and content plumbing.
//
// Usage: mono FnaReference.exe --effect-states <directory-of-fxb> [output.json]

using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace CNA.FnaReference
{
	public static class EffectStateReference
	{
		[DllImport("FNA3D", CallingConvention = CallingConvention.Cdecl)]
		private static extern uint FNA3D_PrepareWindowAttributes();

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern int SDL_Init(uint flags);

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern void SDL_Quit();

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern IntPtr SDL_CreateWindow(byte[] title, int w, int h, ulong flags);

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern void SDL_DestroyWindow(IntPtr window);

		[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
		private static extern IntPtr SDL_GetError();

		private const uint SDL_INIT_VIDEO = 0x00000020;

		/// <summary>Number of sampler slots recorded per pass.</summary>
		private const int RecordedSamplerSlots = 4;

		/// <summary>Applies every pass of every effect and records the state FNA installs.</summary>
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
			byte[] title = System.Text.Encoding.UTF8.GetBytes("FNA state oracle\0");
			IntPtr window = SDL_CreateWindow(title, 64, 64, flags);
			if (window == IntPtr.Zero)
			{
				Console.Error.WriteLine("SDL_CreateWindow failed: " +
					Marshal.PtrToStringAnsi(SDL_GetError()));
				SDL_Quit();
				return 4;
			}

			GraphicsDevice device = null;
			try
			{
				PresentationParameters parameters = new PresentationParameters();
				parameters.BackBufferWidth = 64;
				parameters.BackBufferHeight = 64;
				parameters.BackBufferFormat = SurfaceFormat.Color;
				parameters.DepthStencilFormat = DepthFormat.Depth24Stencil8;
				parameters.DeviceWindowHandle = window;
				parameters.IsFullScreen = false;
				parameters.MultiSampleCount = 0;
				parameters.PresentationInterval = PresentInterval.Immediate;
				parameters.RenderTargetUsage = RenderTargetUsage.PreserveContents;

				device = new GraphicsDevice(
					GraphicsAdapter.DefaultAdapter,
					GraphicsProfile.HiDef,
					parameters
				);
			}
			catch (Exception error)
			{
				Console.Error.WriteLine("GraphicsDevice construction failed: " + error.Message);
				SDL_DestroyWindow(window);
				SDL_Quit();
				return 5;
			}

			var files = new List<string>(Directory.GetFiles(effectDirectory, "*.fxb"));
			files.Sort(StringComparer.Ordinal);

			var root = new JsonWriter()
				.Add("fnaAssemblyVersion", typeof(Vector3).Assembly.GetName().Version.ToString())
				.Add("recordedSamplerSlots", RecordedSamplerSlots);

			foreach (string file in files)
			{
				root.Add(Path.GetFileName(file), DescribeEffect(device, file));
			}

			string json = root.ToString();
			File.WriteAllText(outputPath, json);
			Console.WriteLine("Wrote " + outputPath);

			device.Dispose();
			SDL_DestroyWindow(window);
			SDL_Quit();
			return 0;
		}

		/// <summary>Applies each pass in order and records the resulting device state.</summary>
		private static JsonWriter DescribeEffect(GraphicsDevice device, string path)
		{
			Effect effect;
			try
			{
				effect = new Effect(device, File.ReadAllBytes(path));
			}
			catch (Exception error)
			{
				return new JsonWriter().Add("error", error.GetType().Name + ": " + error.Message);
			}

			var techniques = new JsonWriter();
			foreach (EffectTechnique technique in effect.Techniques)
			{
				effect.CurrentTechnique = technique;
				var passes = new JsonWriter();
				foreach (EffectPass pass in technique.Passes)
				{
					// Every pass starts from the same device state, so what a pass leaves behind
					// is its own doing and not an accumulation of the passes before it. This is
					// the same starting point CNA's conformance tests use.
					device.BlendState = BlendState.Opaque;
					device.DepthStencilState = DepthStencilState.Default;
					device.RasterizerState = RasterizerState.CullCounterClockwise;
					for (int slot = 0; slot < RecordedSamplerSlots; slot += 1)
					{
						device.SamplerStates[slot] = SamplerState.LinearWrap;
					}

					try
					{
						pass.Apply();
						passes.Add(pass.Name, DescribeDeviceState(device));
					}
					catch (Exception error)
					{
						passes.Add(pass.Name, new JsonWriter()
							.Add("error", error.GetType().Name + ": " + error.Message));
					}
				}
				techniques.Add(technique.Name, passes);
			}

			effect.Dispose();
			return new JsonWriter().Add("techniques", techniques);
		}

		/// <summary>Records the public state properties an applied pass may have replaced.</summary>
		private static JsonWriter DescribeDeviceState(GraphicsDevice device)
		{
			BlendState blend = device.BlendState;
			DepthStencilState depth = device.DepthStencilState;
			RasterizerState raster = device.RasterizerState;

			var blendJson = new JsonWriter()
				.Add("AlphaBlendFunction", (int) blend.AlphaBlendFunction)
				.Add("AlphaDestinationBlend", (int) blend.AlphaDestinationBlend)
				.Add("AlphaSourceBlend", (int) blend.AlphaSourceBlend)
				.Add("ColorBlendFunction", (int) blend.ColorBlendFunction)
				.Add("ColorDestinationBlend", (int) blend.ColorDestinationBlend)
				.Add("ColorSourceBlend", (int) blend.ColorSourceBlend)
				.Add("ColorWriteChannels", (int) blend.ColorWriteChannels)
				.Add("ColorWriteChannels1", (int) blend.ColorWriteChannels1)
				.Add("ColorWriteChannels2", (int) blend.ColorWriteChannels2)
				.Add("ColorWriteChannels3", (int) blend.ColorWriteChannels3)
				.Add("MultiSampleMask", blend.MultiSampleMask)
				.Add("BlendFactor_R", (int) blend.BlendFactor.R)
				.Add("BlendFactor_G", (int) blend.BlendFactor.G)
				.Add("BlendFactor_B", (int) blend.BlendFactor.B)
				.Add("BlendFactor_A", (int) blend.BlendFactor.A);

			var depthJson = new JsonWriter()
				.Add("DepthBufferEnable", depth.DepthBufferEnable)
				.Add("DepthBufferWriteEnable", depth.DepthBufferWriteEnable)
				.Add("DepthBufferFunction", (int) depth.DepthBufferFunction)
				.Add("StencilEnable", depth.StencilEnable)
				.Add("StencilFunction", (int) depth.StencilFunction)
				.Add("StencilPass", (int) depth.StencilPass)
				.Add("StencilFail", (int) depth.StencilFail)
				.Add("StencilDepthBufferFail", (int) depth.StencilDepthBufferFail)
				.Add("TwoSidedStencilMode", depth.TwoSidedStencilMode)
				.Add("CounterClockwiseStencilFunction", (int) depth.CounterClockwiseStencilFunction)
				.Add("CounterClockwiseStencilPass", (int) depth.CounterClockwiseStencilPass)
				.Add("CounterClockwiseStencilFail", (int) depth.CounterClockwiseStencilFail)
				.Add("CounterClockwiseStencilDepthBufferFail",
					(int) depth.CounterClockwiseStencilDepthBufferFail)
				.Add("StencilMask", depth.StencilMask)
				.Add("StencilWriteMask", depth.StencilWriteMask)
				.Add("ReferenceStencil", depth.ReferenceStencil);

			var rasterJson = new JsonWriter()
				.Add("CullMode", (int) raster.CullMode)
				.Add("FillMode", (int) raster.FillMode)
				.Add("ScissorTestEnable", raster.ScissorTestEnable)
				.Add("DepthBias", (double) raster.DepthBias)
				.Add("SlopeScaleDepthBias", (double) raster.SlopeScaleDepthBias)
				.Add("MultiSampleAntiAlias", raster.MultiSampleAntiAlias);

			var samplers = new JsonWriter();
			for (int slot = 0; slot < RecordedSamplerSlots; slot += 1)
			{
				SamplerState sampler = device.SamplerStates[slot];
				samplers.Add(slot.ToString(System.Globalization.CultureInfo.InvariantCulture),
					new JsonWriter()
						.Add("Filter", (int) sampler.Filter)
						.Add("AddressU", (int) sampler.AddressU)
						.Add("AddressV", (int) sampler.AddressV)
						.Add("AddressW", (int) sampler.AddressW)
						.Add("MaxAnisotropy", sampler.MaxAnisotropy)
						.Add("MaxMipLevel", sampler.MaxMipLevel)
						.Add("MipMapLevelOfDetailBias",
							(double) sampler.MipMapLevelOfDetailBias));
			}

			return new JsonWriter()
				.Add("BlendState", blendJson)
				.Add("DepthStencilState", depthJson)
				.Add("RasterizerState", rasterJson)
				.Add("SamplerStates", samplers);
		}
	}
}
