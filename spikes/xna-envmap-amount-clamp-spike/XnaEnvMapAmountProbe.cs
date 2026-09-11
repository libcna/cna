// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-196 (finding F-36).
//
// Measures, on the real XNA 4.0 runtime, what happens to `EnvironmentMapEffect.EnvironmentMapAmount`
// above 1. The property setter does not clamp (`EnvironmentMapEffect.cs:283`), the value is written
// to `vout.Specular.rgb` (`EnvironmentMapEffect.fx` ComputeEnvMapVSOutput), and `Structures.fxh`
// declares `Specular : COLOR1` -- so Direct3D 9's saturation of the colour output registers should
// clamp it to 1 before interpolation, and the fragment stage's `lerp(color, envmap, Specular.rgb)`
// should replace with the environment map rather than extrapolate past it.
//
// `plans/plan_fx.md` FX-123 established that saturation against real XNA frames for `oD0`/`oD1` in
// the LIT programs. This probe asks the same question of the env-map factor directly, on the effect
// that carries it, so VULKAN-196 rests on a measurement of its own subject rather than on an
// inherited one.
//
// The legs are relational, never absolute: the base colour a lit EnvironmentMapEffect produces is
// not this probe's subject.
//
//   A  amount 0.5 and 1.0 differ  -- the positive control; without it, B could pass on a stack that
//                                    ignores the amount entirely.
//   B  amount 1.0 and 2.0 are the same -- the saturate.
//   C  amount 1.0 and 3.0 are the same -- so B is not one value's coincidence.
//
// Caveat this prefix carries, stated rather than hidden: D3D9 here is DXVK. A "same" answer is
// strong (something clamped, and XNA's own shader contains no clamp, so it is the register
// semantic). A "different" answer would be inconclusive rather than proof XNA does not clamp.

using System;
using System.IO;
using System.Text;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

public class Probe : Game
{
    GraphicsDeviceManager gdm;
    StringBuilder log = new StringBuilder();
    bool done = false;

    const int N = 64;
    static readonly Color kTex  = new Color(60, 60, 60, 255);
    static readonly Color kCube = new Color(200, 200, 200, 255);

    public Probe()
    {
        gdm = new GraphicsDeviceManager(this);
        gdm.PreferredBackBufferWidth = N;
        gdm.PreferredBackBufferHeight = N;
    }

    void Say(string s) { Console.WriteLine(s); log.AppendLine(s); }

    protected override void Draw(GameTime gt)
    {
        if (done) return;
        done = true;
        try
        {
            Say("adapter: " + GraphicsAdapter.DefaultAdapter.Description);

            var dev = GraphicsDevice;
            dev.BlendState = BlendState.Opaque;
            dev.DepthStencilState = DepthStencilState.None;
            dev.RasterizerState = RasterizerState.CullNone;

            var tex = new Texture2D(dev, 1, 1);
            tex.SetData(new Color[] { kTex });

            var cube = new TextureCube(dev, 1, false, SurfaceFormat.Color);
            foreach (CubeMapFace f in Enum.GetValues(typeof(CubeMapFace)))
                cube.SetData(f, new Color[] { kCube });

            var quad = new VertexPositionNormalTexture[] {
                new VertexPositionNormalTexture(new Vector3(-1, 1,0), new Vector3(0,0,1), new Vector2(0,0)),
                new VertexPositionNormalTexture(new Vector3(-1,-1,0), new Vector3(0,0,1), new Vector2(0,1)),
                new VertexPositionNormalTexture(new Vector3( 1,-1,0), new Vector3(0,0,1), new Vector2(1,1)),
                new VertexPositionNormalTexture(new Vector3(-1, 1,0), new Vector3(0,0,1), new Vector2(0,0)),
                new VertexPositionNormalTexture(new Vector3( 1,-1,0), new Vector3(0,0,1), new Vector2(1,1)),
                new VertexPositionNormalTexture(new Vector3( 1, 1,0), new Vector3(0,0,1), new Vector2(1,0)),
            };

            Color half  = Render(tex, cube, quad, 0.5f);
            Color one   = Render(tex, cube, quad, 1.0f);
            Color two   = Render(tex, cube, quad, 2.0f);
            Color three = Render(tex, cube, quad, 3.0f);

            Say(string.Format("amount 0.5 -> {0}   1.0 -> {1}   2.0 -> {2}   3.0 -> {3}",
                              T(half), T(one), T(two), T(three)));
            if (Same(half, one))
            {
                Say("VERDICT: INCONCLUSIVE -- EnvironmentMapAmount changed nothing between 0.5 and "
                    + "1.0 on this stack, so nothing can be concluded about values above 1.");
            }
            else if (Same(one, two) && Same(one, three))
            {
                Say("VERDICT: XNA SATURATES the amount -- 2.0 and 3.0 render exactly as 1.0 does, "
                    + "which is D3D9 clamping the COLOR1 output register.");
            }
            else
            {
                Say("VERDICT: NOT saturated on this stack -- 2.0/3.0 differ from 1.0. Inconclusive "
                    + "rather than proof: DXVK may not emulate the vertex COLOR register clamp.");
            }
        }
        catch (Exception e) { Say("EXCEPTION: " + e); }
        File.WriteAllText("probe-output.txt", log.ToString());
        Exit();
    }

    Color Render(Texture2D tex, TextureCube cube, VertexPositionNormalTexture[] quad, float amount)
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, N, N, false, SurfaceFormat.Color, DepthFormat.None,
                                    0, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(new Color(0, 0, 0, 255));

        var fx = new EnvironmentMapEffect(dev);
        fx.Texture = tex;
        fx.EnvironmentMap = cube;
        fx.EmissiveColor = Vector3.Zero;
        fx.EnvironmentMapSpecular = Vector3.Zero;
        fx.FresnelFactor = 0f;              // Specular.rgb then carries the amount unmodified
        fx.EnvironmentMapAmount = amount;
        fx.World = Matrix.Identity;
        fx.View = Matrix.Identity;
        fx.Projection = Matrix.Identity;
        fx.CurrentTechnique.Passes[0].Apply();
        dev.DrawUserPrimitives(PrimitiveType.TriangleList, quad, 0, 2);
        dev.SetRenderTarget(null);

        var px = new Color[N * N];
        rt.GetData(px);
        return px[(N / 2) * N + (N / 2)];
    }

    static string T(Color c) { return "(" + c.R + "," + c.G + "," + c.B + ")"; }
    static bool Same(Color a, Color b)
    {
        return Math.Abs(a.R - b.R) <= 4 && Math.Abs(a.G - b.G) <= 4 && Math.Abs(a.B - b.B) <= 4;
    }
}

static class Entry
{
    [STAThread]
    static void Main(string[] a) { using (var g = new Probe()) g.Run(); }
}
