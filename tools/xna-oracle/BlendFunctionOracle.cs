// SPDX-License-Identifier: MS-PL
using System;
using System.IO;
using System.Security.Cryptography;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

public sealed class BlendFunctionOracle : Game
{
    readonly GraphicsDeviceManager manager;
    int frame;
    public BlendFunctionOracle()
    {
        manager = new GraphicsDeviceManager(this);
        manager.PreferredBackBufferWidth = 64;
        manager.PreferredBackBufferHeight = 64;
        manager.GraphicsProfile = GraphicsProfile.HiDef;
    }
    protected override void Draw(GameTime time)
    {
        if (frame++ == 0) return;
        foreach (var assembly in new[] { typeof(Color).Assembly, typeof(GraphicsDevice).Assembly, typeof(Game).Assembly })
        using (var stream = File.OpenRead(assembly.Location))
        using (var hash = SHA256.Create())
            Console.WriteLine("assembly/" + assembly.GetName().Name + "=" + BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "").ToLowerInvariant());
        var functions = new[] { BlendFunction.Add, BlendFunction.Subtract, BlendFunction.ReverseSubtract, BlendFunction.Min, BlendFunction.Max };
        using (var texture = new Texture2D(GraphicsDevice, 1, 1, false, SurfaceFormat.Color))
        using (var target = new RenderTarget2D(GraphicsDevice, 8, 8, false, SurfaceFormat.Color, DepthFormat.None, 0, RenderTargetUsage.PreserveContents))
        {
            texture.SetData(new[] { new Color(191, 128, 64, 128) });
            foreach (var color in functions)
            foreach (var alpha in functions)
            using (var batch = new SpriteBatch(GraphicsDevice))
            using (var blend = new BlendState())
            {
                blend.ColorSourceBlend = Blend.One; blend.ColorDestinationBlend = Blend.Zero;
                blend.AlphaSourceBlend = Blend.One; blend.AlphaDestinationBlend = Blend.Zero;
                blend.ColorBlendFunction = color; blend.AlphaBlendFunction = alpha;
                blend.BlendFactor = new Color(17, 53, 89, 117);
                try
                {
                    GraphicsDevice.SetRenderTarget(target);
                    GraphicsDevice.Clear(new Color(64, 64, 64, 64));
                    batch.Begin(SpriteSortMode.Immediate, blend, SamplerState.PointClamp, null, null);
                    GraphicsDevice.BlendFactor = new Color(70, 140, 210, 200);
                    batch.Draw(texture, new Rectangle(0, 0, 8, 8), Color.White);
                    batch.End();
                    GraphicsDevice.SetRenderTarget(null);
                    var pixels = new Color[64]; target.GetData(pixels);
                    var pixel = pixels[36];
                    Console.WriteLine(color + "/" + alpha + "=" + pixel.R + "," + pixel.G + "," + pixel.B + "," + pixel.A);
                }
                catch (Exception error)
                {
                    Console.WriteLine(color + "/" + alpha + "=" + error.GetType().FullName);
                    GraphicsDevice.SetRenderTarget(null);
                }
            }
            using (var batch = new SpriteBatch(GraphicsDevice))
            using (var blend = new BlendState())
            {
                blend.ColorSourceBlend = Blend.BlendFactor; blend.ColorDestinationBlend = Blend.Zero;
                blend.AlphaSourceBlend = Blend.BlendFactor; blend.AlphaDestinationBlend = Blend.Zero;
                blend.BlendFactor = new Color(17, 53, 89, 117);
                GraphicsDevice.SetRenderTarget(target);
                GraphicsDevice.Clear(new Color(64, 64, 64, 64));
                batch.Begin(SpriteSortMode.Immediate, blend, SamplerState.PointClamp, null, null);
                batch.Draw(texture, new Rectangle(0, 0, 8, 8), Color.White);
                batch.End();
                GraphicsDevice.SetRenderTarget(null);
                var pixels = new Color[64]; target.GetData(pixels);
                var pixel = pixels[36];
                Console.WriteLine("Weighted=" + pixel.R + "," + pixel.G + "," + pixel.B + "," + pixel.A);
            }
        }
        Exit();
    }
    public static void Main() { using (var game = new BlendFunctionOracle()) game.Run(); }
}
