// SPDX-License-Identifier: MS-PL
// Dependency audit 2026-10-26, FNA3D finding B2-04: does XNA 4.0 keep Direct3D 9's integer pixel
// centres on a MULTISAMPLED render target?
//
// FNA3D's XNA pixel-centre extension (and EasyGL's xnaPixelCenterScale_) displaces 3D geometry by
// 63/128 of a pixel so a host API that samples at half-integer pixel centres covers what D3D9,
// sampling at integer centres, covers (spikes/xna-pixel-center-spike measured that XNA does). All
// of them skip the displacement when the destination is multisampled. The audit says MSAA changes
// the sample pattern, not the origin, so the skip moves multisampled geometry away from XNA.
//
// The two readings predict different pictures for an axis-aligned quad covering exactly the
// target, [0,N] x [0,N] in window pixels, drawn with 4x multisampling:
//
//   integer centres (D3D9):      pixel i's samples sit around x = i, so column 0 and row 0 have
//                                half of their samples outside the quad -- 50% coverage, the
//                                corner 25% -- and every other pixel is fully covered.
//   half-integer centres (GL):   samples sit around x = i + 0.5, all inside: every pixel 100%.
//
// The second shape, a quad covering [0,N/2] x [0,N], puts an interior seam at x = N/2 where the
// same two readings again differ (column N/2 half covered, or not at all), so an answer cannot be
// an artefact of the target's border. The unmultisampled draws are the control: both readings
// cover every pixel of the full quad and exactly columns 0..N/2-1 of the half quad.
//
// Output: the red channel of every pixel, per configuration, and the granted sample count.

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

    const int N = 8;

    public Probe()
    {
        gdm = new GraphicsDeviceManager(this);
        gdm.PreferredBackBufferWidth = 64;
        gdm.PreferredBackBufferHeight = 64;
        gdm.GraphicsProfile = GraphicsProfile.HiDef;   // multisampled render targets need HiDef
    }

    void Say(string s) { Console.WriteLine(s); log.AppendLine(s); }

    protected override void Draw(GameTime gt)
    {
        if (done) return;
        done = true;
        try
        {
            Say("adapter: " + GraphicsAdapter.DefaultAdapter.Description);
            Say("profile: " + GraphicsDevice.GraphicsProfile);
            foreach (int samples in new int[] { 0, 4, 8 })
            {
                Render("FULL", samples, N);
                Render("HALF", samples, N / 2);
            }
        }
        catch (Exception e) { Say("EXCEPTION: " + e); }
        File.WriteAllText("probe-output.txt", log.ToString());
        Exit();
    }

    void Render(string label, int samples, int right)
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, N, N, false, SurfaceFormat.Color, DepthFormat.None,
                                    samples, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(Color.Black);
        dev.BlendState = BlendState.Opaque;
        dev.DepthStencilState = DepthStencilState.None;
        dev.RasterizerState = RasterizerState.CullNone;

        var fx = new BasicEffect(dev);
        fx.VertexColorEnabled = true;
        fx.World = Matrix.Identity;
        fx.View = Matrix.Identity;
        // Window pixels, y down: a vertex at (x, y) lands on window coordinate (x, y).
        fx.Projection = Matrix.CreateOrthographicOffCenter(0f, N, N, 0f, 0f, 1f);
        fx.CurrentTechnique.Passes[0].Apply();

        float r = right;
        var quad = new VertexPositionColor[] {
            new VertexPositionColor(new Vector3(0f, 0f, 0f), Color.White),
            new VertexPositionColor(new Vector3(r,  0f, 0f), Color.White),
            new VertexPositionColor(new Vector3(0f, N,  0f), Color.White),
            new VertexPositionColor(new Vector3(r,  0f, 0f), Color.White),
            new VertexPositionColor(new Vector3(r,  N,  0f), Color.White),
            new VertexPositionColor(new Vector3(0f, N,  0f), Color.White),
        };
        dev.DrawUserPrimitives(PrimitiveType.TriangleList, quad, 0, 2);
        dev.SetRenderTarget(null);

        var px = new Color[N * N];
        rt.GetData(px);
        Say(label + " quad [0," + right + "]x[0," + N + "] requested=" + samples +
            " granted=" + rt.MultiSampleCount);
        for (int y = 0; y < N; y++)
        {
            var row = new StringBuilder("    ");
            for (int x = 0; x < N; x++)
                row.Append(px[y * N + x].R.ToString().PadLeft(4));
            Say(row.ToString());
        }
        rt.Dispose();
        fx.Dispose();
    }
}

static class Entry
{
    [STAThread]
    static void Main(string[] a) { using (var g = new Probe()) g.Run(); }
}
