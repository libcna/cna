// plans/plan_fx.md FX-005: render deterministic pixels through FNA's public compiled Effect API.
//
// Every committed compiler-produced fixture is loaded as an ordinary Effect, configured with the
// same flat textures, transforms, and vertex data, and rendered one technique/pass at a time into
// an 8x8 render target. The JSON records three interior pixels and the number of pixels changed
// from the clear colour. Flat inputs keep the oracle independent of interpolation and screen
// orientation while still proving that FNA selected, applied, and drew each compiled shader.
//
// Usage: mono FnaReference.exe --effect-pixels <directory-of-fxb> [output.json]

using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace CNA.FnaReference
{
	public static class EffectPixelReference
	{
		[DllImport("FNA3D", CallingConvention = CallingConvention.Cdecl)]
		private static extern uint FNA3D_PrepareWindowAttributes();

		[DllImport("FNA3D", CallingConvention = CallingConvention.Cdecl)]
		private static extern uint FNA3D_LinkedVersion();

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
		private const int TargetSize = 8;
		private static readonly Color Background = new Color(9, 19, 29, 255);
		private static readonly Color PrimaryTextureColor = new Color(160, 80, 40, 255);
		private static readonly Color SecondaryTextureColor = new Color(64, 192, 128, 255);
		private static readonly Color EnvironmentTextureColor = new Color(30, 100, 220, 255);

		[StructLayout(LayoutKind.Sequential, Pack = 1)]
		private struct OracleVertex
		{
			public Vector3 Position;
			public Color VertexColor;
			public Vector3 Normal;
			public Vector2 TextureCoordinate0;
			public Vector2 TextureCoordinate1;
			public Vector4 BlendWeight;
			public uint BlendIndices;

			public OracleVertex(float x, float y)
			{
				Position = new Vector3(x, y, 0.5f);
				VertexColor = new Color(200, 140, 80, 255);
				Normal = new Vector3(0.0f, 0.0f, 1.0f);
				TextureCoordinate0 = new Vector2(0.5f, 0.5f);
				TextureCoordinate1 = new Vector2(0.5f, 0.5f);
				BlendWeight = new Vector4(1.0f, 0.0f, 0.0f, 0.0f);
				BlendIndices = 0;
			}
		}

		/// <summary>Renders every pass of every compiled fixture and writes normalized JSON.</summary>
		public static int Run(string effectDirectory, string outputPath)
		{
			if (!Directory.Exists(effectDirectory))
			{
				Console.Error.WriteLine("no such directory: " + effectDirectory);
				return 2;
			}

			Version assemblyVersion = typeof(Vector3).Assembly.GetName().Version;
			uint expectedVersion = ExpectedFna3DVersion();
			uint linkedVersion = FNA3D_LinkedVersion();
			if (linkedVersion != expectedVersion)
			{
				Console.Error.WriteLine(
					"FNA/FNA3D version mismatch: FNA " + assemblyVersion + " requires FNA3D " +
					expectedVersion + ", but the loaded library reports " + linkedVersion);
				return 3;
			}

			if (SDL_Init(SDL_INIT_VIDEO) == 0)
			{
				Console.Error.WriteLine("SDL_Init failed: " +
					Marshal.PtrToStringAnsi(SDL_GetError()));
				return 4;
			}

			IntPtr window = IntPtr.Zero;
			GraphicsDevice device = null;
			try
			{
				ulong flags = FNA3D_PrepareWindowAttributes();
				byte[] title = System.Text.Encoding.UTF8.GetBytes("FNA pixel oracle\0");
				window = SDL_CreateWindow(title, 64, 64, flags);
				if (window == IntPtr.Zero)
				{
					Console.Error.WriteLine("SDL_CreateWindow failed: " +
						Marshal.PtrToStringAnsi(SDL_GetError()));
					return 5;
				}

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

				JsonWriter effects = DescribeEffects(device, effectDirectory);
				var root = new JsonWriter()
					.Add("generator", "FnaReference --effect-pixels (plans/plan_fx.md FX-005)")
					.Add("fnaAssemblyVersion", assemblyVersion.ToString())
					.Add("fna3dLinkedVersion", (int) linkedVersion)
					.Add("renderTargetSize", TargetSize)
					.Add("background", DescribeColor(Background))
					.Add("primaryTexture", DescribeColor(PrimaryTextureColor))
					.Add("secondaryTexture", DescribeColor(SecondaryTextureColor))
					.Add("environmentTexture", DescribeColor(EnvironmentTextureColor))
					.Add("effects", effects);

				string json = root.ToString();
				File.WriteAllText(outputPath, json);
				Console.WriteLine("Wrote " + outputPath);
				return 0;
			}
			catch (Exception error)
			{
				Console.Error.WriteLine("effect pixel oracle failed: " + error);
				return 6;
			}
			finally
			{
				if (device != null)
				{
					device.Dispose();
				}
				if (window != IntPtr.Zero)
				{
					SDL_DestroyWindow(window);
				}
				SDL_Quit();
			}
		}

		/// <summary>Loads and renders the seven committed compiler-produced effects in name order.</summary>
		private static JsonWriter DescribeEffects(GraphicsDevice device, string effectDirectory)
		{
			var files = new List<string>(Directory.GetFiles(effectDirectory, "*.fxb"));
			files.Sort(StringComparer.Ordinal);
			var effects = new JsonWriter();

			Texture2D primary = CreateFlatTexture(device, PrimaryTextureColor);
			Texture2D secondary = CreateFlatTexture(device, SecondaryTextureColor);
			TextureCube environment = CreateFlatCube(device, EnvironmentTextureColor);
			VertexDeclaration declaration = CreateVertexDeclaration();
			OracleVertex[] vertices = CreateVertices();

			try
			{
				foreach (string file in files)
				{
					using (Effect effect = new Effect(device, File.ReadAllBytes(file)))
					{
						ConfigureEffect(effect, Path.GetFileName(file), primary, secondary, environment);
						effects.Add(Path.GetFileName(file),
							DescribeEffect(device, effect, declaration, vertices));
					}
				}
			}
			finally
			{
				declaration.Dispose();
				environment.Dispose();
				secondary.Dispose();
				primary.Dispose();
			}

			return effects;
		}

		/// <summary>Renders every pass from a clean target and device-state baseline.</summary>
		private static JsonWriter DescribeEffect(
			GraphicsDevice device,
			Effect effect,
			VertexDeclaration declaration,
			OracleVertex[] vertices)
		{
			var techniques = new JsonWriter();
			foreach (EffectTechnique technique in effect.Techniques)
			{
				effect.CurrentTechnique = technique;
				var passes = new JsonWriter();
				int unnamedPass = 0;
				foreach (EffectPass pass in technique.Passes)
				{
					string passName = pass.Name.Length == 0
						? "<unnamed:" + unnamedPass.ToString() + ">"
						: pass.Name;
					passes.Add(passName, RenderPass(device, pass, declaration, vertices));
					unnamedPass += 1;
				}
				techniques.Add(technique.Name, passes);
			}
			return new JsonWriter().Add("techniques", techniques);
		}

		/// <summary>Draws one full-screen pass and records stable interior samples.</summary>
		private static JsonWriter RenderPass(
			GraphicsDevice device,
			EffectPass pass,
			VertexDeclaration declaration,
			OracleVertex[] vertices)
		{
			using (RenderTarget2D target = new RenderTarget2D(
				device, TargetSize, TargetSize, false, SurfaceFormat.Color, DepthFormat.None))
			{
				device.SetRenderTarget(target);
				try
				{
					device.BlendState = BlendState.Opaque;
					device.DepthStencilState = DepthStencilState.None;
					device.RasterizerState = RasterizerState.CullNone;
					device.SamplerStates[0] = SamplerState.PointClamp;
					device.SamplerStates[1] = SamplerState.PointClamp;
					device.Clear(Background);
					pass.Apply();
					device.DrawUserPrimitives(
						PrimitiveType.TriangleList, vertices, 0, 2, declaration);
				}
				finally
				{
					device.SetRenderTarget(null);
				}

				Color[] pixels = new Color[TargetSize * TargetSize];
				target.GetData(pixels);
				int changed = 0;
				for (int i = 0; i < pixels.Length; i += 1)
				{
					if (pixels[i] != Background)
					{
						changed += 1;
					}
				}
				if (changed == 0)
				{
					throw new InvalidOperationException("pass produced no pixels distinct from the clear colour");
				}

				return new JsonWriter()
					.Add("changedPixelCount", changed)
					.Add("upperLeft", DescribeColor(pixels[2 * TargetSize + 2]))
					.Add("center", DescribeColor(pixels[4 * TargetSize + 4]))
					.Add("lowerRight", DescribeColor(pixels[6 * TargetSize + 6]));
			}
		}

		/// <summary>Sets deterministic values for every shader input used by the committed effects.</summary>
		private static void ConfigureEffect(
			Effect effect,
			string fileName,
			Texture2D primary,
			Texture2D secondary,
			TextureCube environment)
		{
			SetMatrix(effect, "Transform", Matrix.Identity);
			SetMatrix(effect, "MatrixTransform", Matrix.Identity);
			SetMatrix(effect, "World", Matrix.Identity);
			SetMatrix(effect, "WorldInverseTranspose", Matrix.Identity);
			SetMatrix(effect, "WorldViewProj", Matrix.Identity);

			EffectParameter bones = effect.Parameters["Bones"];
			if (bones != null)
			{
				Matrix[] identities = new Matrix[bones.Elements.Count];
				for (int i = 0; i < identities.Length; i += 1)
				{
					identities[i] = Matrix.Identity;
				}
				bones.SetValue(identities);
			}

			SetVector4(effect, "DiffuseColor", new Vector4(0.7f, 0.8f, 0.9f, 1.0f));
			SetVector3(effect, "EmissiveColor", new Vector3(0.1f, 0.2f, 0.3f));
			SetVector3(effect, "SpecularColor", Vector3.Zero);
			SetSingle(effect, "SpecularPower", 4.0f);
			SetVector3(effect, "EyePosition", new Vector3(0.0f, 0.0f, 1.0f));
			SetVector3(effect, "FogColor", Vector3.Zero);
			SetVector4(effect, "FogVector", Vector4.Zero);
			SetVector4(effect, "AlphaTest", new Vector4(0.0f, 1.0f, 1.0f, 1.0f));

			for (int i = 0; i < 3; i += 1)
			{
				SetVector3(effect, "DirLight" + i + "Direction", new Vector3(0.0f, 0.0f, -1.0f));
				SetVector3(effect, "DirLight" + i + "DiffuseColor", Vector3.Zero);
				SetVector3(effect, "DirLight" + i + "SpecularColor", Vector3.Zero);
			}

			SetVector3(effect, "EnvironmentMapSpecular", Vector3.Zero);
			SetSingle(effect, "FresnelFactor", 1.0f);
			SetSingle(effect, "EnvironmentMapAmount", 0.5f);

			SetTexture(effect, "Texture", primary);
			SetTexture(effect, "Texture2", secondary);
			SetTexture(effect, "FxTexture", primary);
			SetTexture(effect, "EnvironmentMap", environment);

			EffectParameter shaderIndex = effect.Parameters["ShaderIndex"];
			if (shaderIndex != null)
			{
				int value = 0;
				if (fileName == "AlphaTestEffect.fxb") value = 3;
				else if (fileName == "BasicEffect.fxb") value = 7;
				else if (fileName == "DualTextureEffect.fxb") value = 3;
				else if (fileName == "EnvironmentMapEffect.fxb") value = 1;
				else if (fileName == "SkinnedEffect.fxb") value = 5;
				shaderIndex.SetValue(value);
			}
		}

		private static VertexDeclaration CreateVertexDeclaration()
		{
			return new VertexDeclaration(64,
				new VertexElement(0, VertexElementFormat.Vector3, VertexElementUsage.Position, 0),
				new VertexElement(12, VertexElementFormat.Color, VertexElementUsage.Color, 0),
				new VertexElement(16, VertexElementFormat.Vector3, VertexElementUsage.Normal, 0),
				new VertexElement(28, VertexElementFormat.Vector2, VertexElementUsage.TextureCoordinate, 0),
				new VertexElement(36, VertexElementFormat.Vector2, VertexElementUsage.TextureCoordinate, 1),
				new VertexElement(44, VertexElementFormat.Vector4, VertexElementUsage.BlendWeight, 0),
				new VertexElement(60, VertexElementFormat.Byte4, VertexElementUsage.BlendIndices, 0));
		}

		private static OracleVertex[] CreateVertices()
		{
			return new OracleVertex[] {
				new OracleVertex(-1.0f, 1.0f),
				new OracleVertex(-1.0f, -1.0f),
				new OracleVertex(1.0f, -1.0f),
				new OracleVertex(-1.0f, 1.0f),
				new OracleVertex(1.0f, -1.0f),
				new OracleVertex(1.0f, 1.0f)
			};
		}

		private static Texture2D CreateFlatTexture(GraphicsDevice device, Color color)
		{
			Texture2D texture = new Texture2D(device, 1, 1);
			texture.SetData(new Color[] { color });
			return texture;
		}

		private static TextureCube CreateFlatCube(GraphicsDevice device, Color color)
		{
			TextureCube texture = new TextureCube(device, 1, false, SurfaceFormat.Color);
			Color[] pixel = new Color[] { color };
			foreach (CubeMapFace face in Enum.GetValues(typeof(CubeMapFace)))
			{
				texture.SetData(face, pixel);
			}
			return texture;
		}

		private static JsonWriter DescribeColor(Color color)
		{
			return new JsonWriter()
				.Add("r", (int) color.R)
				.Add("g", (int) color.G)
				.Add("b", (int) color.B)
				.Add("a", (int) color.A);
		}

		/// <summary>Converts FNA's assembly version to FNA3D's packed version integer.</summary>
		private static uint ExpectedFna3DVersion()
		{
			Version version = typeof(Vector3).Assembly.GetName().Version;
			return (uint) (version.Major * 10000 + version.Minor * 100 + version.Build);
		}

		private static void SetMatrix(Effect effect, string name, Matrix value)
		{
			EffectParameter parameter = effect.Parameters[name];
			if (parameter != null) parameter.SetValue(value);
		}

		private static void SetSingle(Effect effect, string name, float value)
		{
			EffectParameter parameter = effect.Parameters[name];
			if (parameter != null) parameter.SetValue(value);
		}

		private static void SetVector3(Effect effect, string name, Vector3 value)
		{
			EffectParameter parameter = effect.Parameters[name];
			if (parameter != null) parameter.SetValue(value);
		}

		private static void SetVector4(Effect effect, string name, Vector4 value)
		{
			EffectParameter parameter = effect.Parameters[name];
			if (parameter != null) parameter.SetValue(value);
		}

		private static void SetTexture(Effect effect, string name, Texture value)
		{
			EffectParameter parameter = effect.Parameters[name];
			if (parameter != null) parameter.SetValue(value);
		}
	}
}
