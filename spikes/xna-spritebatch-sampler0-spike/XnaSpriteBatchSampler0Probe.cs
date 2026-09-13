// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-194.
//
// Does SpriteBatch.Begin leave its SamplerState in GraphicsDevice.SamplerStates[0], and does a 3D
// draw AFTER the batch then sample with it?
//
// FNA's PrepRenderState assigns `GraphicsDevice.SamplerStates[0] = samplerState`
// (SpriteBatch.cs:1426). CNA carries the batch's sampler to the renderer down the
// ISpriteBatchRenderer path instead and never touches the device's collection, so a game that
// reads SamplerStates[0] sees the LinearWrap default -- and, the half with pixels attached, a 3D
// draw issued after a sprite batch samples with LinearWrap where XNA would use the batch's state.
//
// VULKAN-166 published slots 1..15 from the sprite path and deliberately left slot 0 alone,
// because the batch already writes it down its own route and giving one slot two writers with no
// ordering between them is how that row's sibling defect happened. This probe settles what the
// device-visible value is supposed to be, before anything is changed on twelve renderers at once.
//
//   A  SamplerStates[0] after Begin(..., PointWrap, ...)   -- is it PointWrap, or what it was?
//   B  ... and after End()                                 -- does it persist past the batch?
//   C  a 3D textured draw after End(), sampling OUTSIDE [0,1] -- WRAP (the batch's) or CLAMP (the
//      pre-set one)? This is the half a game would actually see.
//
// The sample point is u = 1.25, and the first draft's u = 1.5 was wrong in a way worth recording:
// on a 2x1 texture, Wrap(1.5) = 0.5 selects texel 1 and Clamp(1.5) = 1.0 selects texel 1 as well,
// so BOTH modes read green and the leg could not fail. u = 1.25 separates them -- Wrap gives 0.25
// -> texel 0 (RED), Clamp gives 1.0 -> texel 1 (GREEN) -- and legs C0/C1 below sample inside [0,1]
// first, so a leg that reads the wrong texel for any other reason says so instead of voting.

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

            var white = new Texture2D(dev, 1, 1);
            white.SetData(new Color[] { Color.White });

            // 2x1: red at u=0, green at u=1. At u=1.5 Clamp reads green, Wrap reads red.
            var strip = new Texture2D(dev, 2, 1);
            strip.SetData(new Color[] { new Color(255, 0, 0, 255), new Color(0, 255, 0, 255) });

            dev.SamplerStates[0] = SamplerState.PointClamp;
            Say("before Begin: SamplerStates[0] = " + Describe(dev.SamplerStates[0]));

            var sb = new SpriteBatch(dev);
            sb.Begin(SpriteSortMode.Deferred, BlendState.Opaque, SamplerState.PointWrap, null, null);
            sb.Draw(white, new Rectangle(0, 0, 4, 4), Color.White);
            string afterBegin = Describe(dev.SamplerStates[0]);
            sb.End();
            string afterEnd = Describe(dev.SamplerStates[0]);

            Say("after  Begin: SamplerStates[0] = " + afterBegin);
            Say("after  End  : SamplerStates[0] = " + afterEnd);

            // C0/C1 -- the controls. Inside [0,1] the address mode cannot matter, so these say
            // whether the texture, the sample point and the filter are doing what C assumes.
            Color inLeft  = Draw3DAtU(dev, strip, 0.25f);
            Color inRight = Draw3DAtU(dev, strip, 0.75f);
            Say("control: u = 0.25 -> " + T(inLeft) + "   u = 0.75 -> " + T(inRight)
                + "   (want red then green)");
            bool controlsOk = inLeft.R > 128 && inLeft.G < 128
                           && inRight.G > 128 && inRight.R < 128;

            Color px = Draw3DAtU(dev, strip, 1.25f);
            Say("3D draw after the batch, sampled at u = 1.25 -> " + T(px));

            bool wrap  = px.R > 128 && px.G < 128;
            bool clamp = px.G > 128 && px.R < 128;
            Say(!controlsOk
                ? "VERDICT: INCONCLUSIVE -- the controls inside [0,1] did not read red then green, "
                  + "so nothing outside [0,1] can be attributed to an address mode."
              : wrap  ? "VERDICT: the 3D draw used the BATCH's sampler (Wrap) -- the batch's state "
                        + "reaches the next draw."
              : clamp ? "VERDICT: the 3D draw used the PRE-SET sampler (Clamp) -- the batch's state "
                        + "did NOT reach the next draw."
              : "VERDICT: INCONCLUSIVE -- neither wrap nor clamp at u = 1.25.");
        }
        catch (Exception e) { Say("EXCEPTION: " + e); }
        File.WriteAllText("probe-output.txt", log.ToString());
        Exit();
    }

    Color Draw3DAtU(GraphicsDevice dev, Texture2D strip, float u)
    {
        var rt = new RenderTarget2D(dev, N, N, false, SurfaceFormat.Color, DepthFormat.None,
                                    0, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(new Color(0, 0, 255, 255));
        dev.BlendState = BlendState.Opaque;
        dev.DepthStencilState = DepthStencilState.None;
        dev.RasterizerState = RasterizerState.CullNone;

        // Every vertex at u = 1.5, so the whole quad samples the one texel the address mode picks.
        var quad = new VertexPositionTexture[] {
            new VertexPositionTexture(new Vector3(-1, 1,0), new Vector2(u, 0.5f)),
            new VertexPositionTexture(new Vector3(-1,-1,0), new Vector2(u, 0.5f)),
            new VertexPositionTexture(new Vector3( 1,-1,0), new Vector2(u, 0.5f)),
            new VertexPositionTexture(new Vector3(-1, 1,0), new Vector2(u, 0.5f)),
            new VertexPositionTexture(new Vector3( 1,-1,0), new Vector2(u, 0.5f)),
            new VertexPositionTexture(new Vector3( 1, 1,0), new Vector2(u, 0.5f)),
        };
        var fx = new BasicEffect(dev);
        fx.LightingEnabled = false;
        fx.VertexColorEnabled = false;
        fx.TextureEnabled = true;
        fx.Texture = strip;
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

    static string Describe(SamplerState s)
    {
        if (s == null) return "null";
        return (string.IsNullOrEmpty(s.Name) ? "(unnamed)" : s.Name)
             + " AddressU=" + s.AddressU + " Filter=" + s.Filter;
    }
    static string T(Color c) { return "(" + c.R + "," + c.G + "," + c.B + ")"; }
}

static class Entry
{
    [STAThread]
    static void Main(string[] a) { using (var g = new Probe()) g.Run(); }
}
