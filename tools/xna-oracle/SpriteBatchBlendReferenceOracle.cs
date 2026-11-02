// SPDX-License-Identifier: MS-PL
using System;
using System.IO;
using System.Security.Cryptography;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

public sealed class SpriteBatchBlendReferenceOracle : Game
{
    readonly GraphicsDeviceManager manager;
    int frame;
    bool failed;

    public SpriteBatchBlendReferenceOracle()
    {
        manager = new GraphicsDeviceManager(this);
        manager.PreferredBackBufferWidth = 64;
        manager.PreferredBackBufferHeight = 64;
        manager.GraphicsProfile = GraphicsProfile.HiDef;
    }

    static void Begin(SpriteBatch batch, SpriteSortMode mode, BlendState blend, int shape)
    {
        switch (shape)
        {
            case 0: batch.Begin(mode, blend); break;
            case 1: batch.Begin(mode, blend, null, null, null); break;
            case 2: batch.Begin(mode, blend, null, null, null, null); break;
            case 3: batch.Begin(mode, blend, null, null, null, null, Matrix.Identity); break;
        }
    }

    static string MutationResult(BlendState blend)
    {
        try { blend.ColorWriteChannels = ColorWriteChannels.All; return "success"; }
        catch (Exception error) { return error.GetType().FullName; }
    }

    protected override void Draw(GameTime time)
    {
        if (frame++ == 0) return;
        foreach (var assembly in new[] { typeof(Color).Assembly, typeof(GraphicsDevice).Assembly, typeof(Game).Assembly })
        using (var stream = File.OpenRead(assembly.Location))
        using (var hash = SHA256.Create())
            Console.WriteLine("assembly/" + assembly.GetName().Name + "="
                + BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "").ToLowerInvariant());

        using (var texture = new Texture2D(GraphicsDevice, 1, 1))
        using (var target = new RenderTarget2D(GraphicsDevice, 8, 8, false, SurfaceFormat.Color,
            DepthFormat.None, 0, RenderTargetUsage.PreserveContents))
        {
            texture.SetData(new[] { Color.White });
            for (int shape = 0; shape < 4; shape++)
            {
                using (var blend = new BlendState())
                using (var batch = new SpriteBatch(GraphicsDevice))
                {
                    GraphicsDevice.SetRenderTarget(target);
                    var clear = new Color(9, 17, 25, 255);
                    GraphicsDevice.Clear(clear);
                    Begin(batch, SpriteSortMode.Deferred, blend, shape);
                    blend.ColorWriteChannels = ColorWriteChannels.None;
                    batch.Draw(texture, new Rectangle(0, 0, 8, 8), Color.White);
                    batch.End();
                    GraphicsDevice.SetRenderTarget(null);
                    var pixels = new Color[64];
                    target.GetData(pixels);
                    var pixel = pixels[36];
                    var mutation = MutationResult(blend);
                    bool sameDevice = Object.ReferenceEquals(blend.GraphicsDevice, GraphicsDevice);
                    Console.WriteLine(shape + "/deferred=" + pixel.R + "," + pixel.G + ","
                        + pixel.B + "," + pixel.A + ";device=" + sameDevice + ";after-bind=" + mutation);
                    failed |= pixel != clear || !sameDevice || mutation != "System.InvalidOperationException";
                }
                using (var blend = new BlendState())
                using (var batch = new SpriteBatch(GraphicsDevice))
                {
                    blend.ColorSourceBlend = Blend.BlendFactor;
                    blend.ColorDestinationBlend = Blend.Zero;
                    blend.AlphaSourceBlend = Blend.One;
                    blend.AlphaDestinationBlend = Blend.Zero;
                    blend.BlendFactor = new Color(173, 61, 219, 255);
                    GraphicsDevice.SetRenderTarget(target);
                    GraphicsDevice.Clear(Color.Black);
                    Begin(batch, SpriteSortMode.Deferred, blend, shape);
                    batch.Draw(texture, new Rectangle(0, 0, 8, 8), Color.White);
                    blend.ColorSourceBlend = Blend.InverseBlendFactor;
                    blend.BlendFactor = new Color(7, 241, 19, 255);
                    GraphicsDevice.BlendFactor = new Color(251, 9, 101, 255);
                    batch.End();
                    GraphicsDevice.BlendFactor = new Color(233, 109, 3, 255);
                    GraphicsDevice.BlendState = BlendState.Opaque;
                    GraphicsDevice.SetRenderTarget(null);
                    var pixels = new Color[64];
                    target.GetData(pixels);
                    var pixel = pixels[36];
                    Console.WriteLine(shape + "/factor=" + pixel.R + "," + pixel.G + "," + pixel.B + "," + pixel.A);
                    failed |= pixel != new Color(248, 14, 236, 255);
                }
                using (var blend = new BlendState())
                using (var batch = new SpriteBatch(GraphicsDevice))
                {
                    Begin(batch, SpriteSortMode.Immediate, blend, shape);
                    var mutation = MutationResult(blend);
                    batch.End();
                    Console.WriteLine(shape + "/immediate=" + mutation);
                    failed |= mutation != "System.InvalidOperationException";
                }
                using (var blend = new BlendState())
                using (var batch = new SpriteBatch(GraphicsDevice))
                {
                    blend.Dispose();
                    string result;
                    try { Begin(batch, SpriteSortMode.Immediate, blend, shape); batch.End(); result = "success"; }
                    catch (Exception error) { result = error.GetType().FullName; }
                    Console.WriteLine(shape + "/disposed=" + result);
                    failed |= result != "System.ObjectDisposedException";
                }
            }
        }
        Exit();
    }

    public static int Main()
    {
        using (var game = new SpriteBatchBlendReferenceOracle())
        {
            game.Run();
            return game.failed ? 1 : 0;
        }
    }
}
