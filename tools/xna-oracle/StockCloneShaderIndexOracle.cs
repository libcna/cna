// SPDX-License-Identifier: MS-PL
// KI-GFX-SHADERINDEX: what genuine XNA 4.0 does when an EffectMaterial clone of a stock effect is
// applied and drawn with a ShaderIndex outside its stock shader tables (BasicEffect 32 entries,
// AlphaTestEffect 8, DualTextureEffect 4, EnvironmentMapEffect 16, SkinnedEffect 18).
//
// The clone runs the stock effect's own bytecode with its own (empty) OnApply, so nothing rewrites
// the raw ShaderIndex the game set; the pass selects its shaders by VSArray[VSIndices[ShaderIndex]]
// and PSArray[PSIndices[ShaderIndex]]. For each index the probe records Apply's outcome, a draw's
// outcome and the drawn pixel, then whether the device still draws through a valid clone.
//
// Usage: run-stock-clone-shader-index-oracle.sh <output-directory>
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct ProbeVertex : IVertexType
{
    public Vector3 Position;
    public Vector3 Normal;
    public Color Color;
    public Vector2 TextureCoordinate;
    public Vector2 TextureCoordinate1;

    public static readonly VertexDeclaration Declaration = new VertexDeclaration(
        new VertexElement(0, VertexElementFormat.Vector3, VertexElementUsage.Position, 0),
        new VertexElement(12, VertexElementFormat.Vector3, VertexElementUsage.Normal, 0),
        new VertexElement(24, VertexElementFormat.Color, VertexElementUsage.Color, 0),
        new VertexElement(28, VertexElementFormat.Vector2, VertexElementUsage.TextureCoordinate, 0),
        new VertexElement(36, VertexElementFormat.Vector2, VertexElementUsage.TextureCoordinate, 1));

    VertexDeclaration IVertexType.VertexDeclaration { get { return Declaration; } }

    public ProbeVertex(float x, float y, float u, float v)
    {
        Position = new Vector3(x, y, 0.0f);
        Normal = new Vector3(0.0f, 0.0f, 1.0f);
        Color = new Color(255, 128, 64, 255);
        TextureCoordinate = new Vector2(u, v);
        TextureCoordinate1 = new Vector2(u, v);
    }
}

public sealed class StockCloneShaderIndexOracle : Game
{
    readonly GraphicsDeviceManager manager;
    int frame;

    public StockCloneShaderIndexOracle()
    {
        manager = new GraphicsDeviceManager(this);
        manager.PreferredBackBufferWidth = 64;
        manager.PreferredBackBufferHeight = 64;
        manager.GraphicsProfile = GraphicsProfile.HiDef;
    }

    static string Describe(Exception error)
    {
        return error.GetType().FullName + "(" + error.Message.Replace("\r", " ").Replace("\n", " ") + ")";
    }

    string Probe(Effect source, int index, RenderTarget2D target, ProbeVertex[] quad, Texture2D white)
    {
        EffectMaterial clone;
        try { clone = new EffectMaterial(source); }
        catch (Exception error) { return "clone=" + Describe(error) + " at " + error.StackTrace.Replace("\r", " ").Replace("\n", " | "); }
        try
        {
            clone.Parameters["ShaderIndex"].SetValue(index);
            string apply = "ok";
            string draw = "ok";
            string pixel = "-";
            GraphicsDevice.SetRenderTarget(target);
            GraphicsDevice.Clear(new Color(7, 199, 53, 255));
            GraphicsDevice.Textures[0] = white;
            GraphicsDevice.Textures[1] = white;
            GraphicsDevice.SamplerStates[0] = SamplerState.PointClamp;
            GraphicsDevice.SamplerStates[1] = SamplerState.PointClamp;
            try { clone.CurrentTechnique.Passes[0].Apply(); }
            catch (Exception error) { apply = Describe(error); }
            try { GraphicsDevice.DrawUserPrimitives(PrimitiveType.TriangleList, quad, 0, 2); }
            catch (Exception error) { draw = Describe(error); }
            GraphicsDevice.SetRenderTarget(null);
            Color[] pixels = new Color[64];
            target.GetData(pixels);
            Color p = pixels[36];
            pixel = p.R + "," + p.G + "," + p.B + "," + p.A;
            return "apply=" + apply + " draw=" + draw + " pixel=" + pixel;
        }
        catch (Exception error)
        {
            GraphicsDevice.SetRenderTarget(null);
            return "outer=" + Describe(error);
        }
        finally
        {
            clone.Dispose();
        }
    }

    void Run(string name, Effect source, int count, RenderTarget2D target, ProbeVertex[] quad, Texture2D white)
    {
        try
        {
            // The source's Apply writes its derived parameters, which each clone then copies.
            source.CurrentTechnique.Passes[0].Apply();
            int[] indices = { 0, 1, 2, count - 1, count, count + 1, 2 * count, 1000, -1, -2, -3, -count, -count - 1, int.MaxValue, int.MinValue, int.MinValue + 1 };
            foreach (int index in indices)
                Console.WriteLine(name + "/ShaderIndex=" + index + " " + Probe(source, index, target, quad, white));
            Console.WriteLine(name + "/after " + Probe(source, 0, target, quad, white));
        }
        catch (Exception error)
        {
            Console.WriteLine(name + "/setup=" + Describe(error) + " at " + error.StackTrace.Replace("\r", " ").Replace("\n", " | "));
        }
    }

    protected override void Draw(GameTime time)
    {
        if (frame++ == 0) return;
        foreach (var assembly in new[] { typeof(Color).Assembly, typeof(GraphicsDevice).Assembly, typeof(Game).Assembly })
        using (var stream = File.OpenRead(assembly.Location))
        using (var hash = SHA256.Create())
            Console.WriteLine("assembly/" + assembly.GetName().Name + "=" + BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "").ToLowerInvariant());

        ProbeVertex[] quad = {
            new ProbeVertex(-1, 1, 0, 0), new ProbeVertex(1, 1, 1, 0), new ProbeVertex(-1, -1, 0, 1),
            new ProbeVertex(1, 1, 1, 0), new ProbeVertex(1, -1, 1, 1), new ProbeVertex(-1, -1, 0, 1) };
        GraphicsDevice.RasterizerState = RasterizerState.CullNone;
        GraphicsDevice.DepthStencilState = DepthStencilState.None;
        GraphicsDevice.BlendState = BlendState.Opaque;
        using (var white = new Texture2D(GraphicsDevice, 1, 1, false, SurfaceFormat.Color))
        using (var cube = new TextureCube(GraphicsDevice, 1, false, SurfaceFormat.Color))
        using (var target = new RenderTarget2D(GraphicsDevice, 8, 8, false, SurfaceFormat.Color, DepthFormat.None, 0, RenderTargetUsage.PreserveContents))
        {
            white.SetData(new[] { Color.White });
            for (int face = 0; face < 6; ++face)
                cube.SetData((CubeMapFace)face, new[] { Color.White });

            var basic = new BasicEffect(GraphicsDevice);
            basic.TextureEnabled = true;
            basic.Texture = white;
            Run("BasicEffect", basic, 32, target, quad, white);

            var alpha = new AlphaTestEffect(GraphicsDevice);
            alpha.Texture = white;
            Run("AlphaTestEffect", alpha, 8, target, quad, white);

            var dual = new DualTextureEffect(GraphicsDevice);
            dual.Texture = white;
            dual.Texture2 = white;
            Run("DualTextureEffect", dual, 4, target, quad, white);

            var environment = new EnvironmentMapEffect(GraphicsDevice);
            environment.Texture = white;
            environment.EnvironmentMap = cube;
            Run("EnvironmentMapEffect", environment, 16, target, quad, white);

            var skinned = new SkinnedEffect(GraphicsDevice);
            skinned.Texture = white;
            Run("SkinnedEffect", skinned, 18, target, quad, white);
        }
        Exit();
    }

    public static void Main() { using (var game = new StockCloneShaderIndexOracle()) game.Run(); }
}
