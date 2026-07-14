// D9-A1/D9-A2 spike: minimal XNA 4.0 reference renderer. No content pipeline.
using System;
using System.IO;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

public class Oracle : Game
{
    GraphicsDeviceManager gdm;
    int frame = 0;

    public Oracle()
    {
        gdm = new GraphicsDeviceManager(this);
        gdm.PreferredBackBufferWidth = 256;
        gdm.PreferredBackBufferHeight = 256;
        gdm.GraphicsProfile = GraphicsProfile.HiDef;
    }

    protected override void Draw(GameTime gameTime)
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, 256, 256, false, SurfaceFormat.Color, DepthFormat.Depth24);
        dev.SetRenderTarget(rt);
        dev.Clear(Color.CornflowerBlue);

        var fx = new BasicEffect(dev);
        fx.VertexColorEnabled = true;
        fx.LightingEnabled = false;
        fx.World = Matrix.Identity;
        fx.View = Matrix.Identity;
        fx.Projection = Matrix.Identity;

        var v = new VertexPositionColor[3] {
            new VertexPositionColor(new Vector3(-0.6f, -0.6f, 0f), Color.Red),
            new VertexPositionColor(new Vector3( 0.0f,  0.7f, 0f), Color.Lime),
            new VertexPositionColor(new Vector3( 0.6f, -0.6f, 0f), Color.Blue),
        };
        foreach (var pass in fx.CurrentTechnique.Passes) {
            pass.Apply();
            dev.DrawUserPrimitives(PrimitiveType.TriangleList, v, 0, 1);
        }

        dev.SetRenderTarget(null);

        using (var fs = File.Create("xna_out.png"))
            rt.SaveAsPng(fs, 256, 256);

        Console.WriteLine("XNA-ORACLE-OK profile=" + dev.GraphicsProfile
                          + " adapter=" + GraphicsAdapter.DefaultAdapter.Description);
        if (++frame >= 1) Exit();
        base.Draw(gameTime);
    }

    static void Main() { using (var g = new Oracle()) g.Run(); }
}
