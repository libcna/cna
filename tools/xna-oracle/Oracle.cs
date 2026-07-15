// plan_dx9.md Phase D9-A (D9-A3): scene-driven XNA 4.0 reference renderer. No content pipeline.
//
// Reads a ".scene" file (the same declarative format CnaOracleRender.cpp reads) and renders it
// with the real XNA 4.0 runtime, saving the result as a PNG -- the "oracle" half of the D9-A4
// diff harness. Do not hand-transcribe scene data into this file; every scene lives in
// tools/xna-oracle/scenes/*.scene and is parsed identically by both sides, or the harness drifts.
//
// Usage: Oracle.exe <scene-file> <output-png>
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

public enum SceneVertexFormat { PositionColor, PositionTexture, PositionNormalTexture }
public enum SceneEffectType { BasicEffect, AlphaTestEffect }

public class SceneLight
{
    public bool Enabled;
    public Vector3 Diffuse = Vector3.Zero;
    public Vector3 Direction = new Vector3(0, 0, -1);
}

public class Scene
{
    public int Width = 256;
    public int Height = 256;
    public GraphicsProfile Profile = GraphicsProfile.HiDef;
    public Color ClearColor = Color.CornflowerBlue;
    public SceneVertexFormat VertexFormat = SceneVertexFormat.PositionColor;
    public bool VertexColorEnabled;
    public bool LightingEnabled;
    public bool TextureEnabled;
    public int TextureWidth;
    public int TextureHeight;
    public bool TexturePointFilter = true;
    public Vector3 AmbientColor = Vector3.Zero;
    public SceneLight Light0 = new SceneLight();
    public SceneEffectType EffectType = SceneEffectType.BasicEffect;
    public CompareFunction AlphaFunction = CompareFunction.Always;
    public int ReferenceAlpha;
    public PrimitiveType Primitive = PrimitiveType.TriangleList;
    public List<VertexPositionColor> ColorVertices = new List<VertexPositionColor>();
    public List<VertexPositionTexture> TextureVertices = new List<VertexPositionTexture>();
    public List<VertexPositionNormalTexture> NormalTextureVertices = new List<VertexPositionNormalTexture>();
    public List<Color> TexturePixels = new List<Color>();

    public static Scene Load(string path)
    {
        var scene = new Scene();
        foreach (var rawLine in File.ReadAllLines(path))
        {
            var line = rawLine.Trim();
            if (line.Length == 0 || line[0] == '#') continue;

            int eq = line.IndexOf('=');
            if (eq < 0) continue;
            string key = line.Substring(0, eq).Trim();
            string value = line.Substring(eq + 1).Trim();

            switch (key)
            {
                case "width": scene.Width = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "height": scene.Height = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "profile":
                    scene.Profile = value == "Reach" ? GraphicsProfile.Reach : GraphicsProfile.HiDef;
                    break;
                case "clearcolor": scene.ClearColor = ParseColor(value); break;
                case "effect":
                    scene.EffectType = value == "AlphaTestEffect"
                        ? SceneEffectType.AlphaTestEffect : SceneEffectType.BasicEffect;
                    break;
                case "alphafunction": scene.AlphaFunction = ParseCompareFunction(value); break;
                case "referencealpha": scene.ReferenceAlpha = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "vertexformat":
                    if (value == "PositionTexture") scene.VertexFormat = SceneVertexFormat.PositionTexture;
                    else if (value == "PositionNormalTexture") scene.VertexFormat = SceneVertexFormat.PositionNormalTexture;
                    else scene.VertexFormat = SceneVertexFormat.PositionColor;
                    break;
                case "vertexcolor": scene.VertexColorEnabled = ParseBool(value); break;
                case "lighting": scene.LightingEnabled = ParseBool(value); break;
                case "texture": scene.TextureEnabled = ParseBool(value); break;
                case "texturewidth": scene.TextureWidth = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "textureheight": scene.TextureHeight = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "texturefilter": scene.TexturePointFilter = value == "Point"; break;
                case "texturepixel": scene.TexturePixels.Add(ParseColor(value)); break;
                case "ambientcolor": scene.AmbientColor = ParseVector3(value); break;
                case "light0enabled": scene.Light0.Enabled = ParseBool(value); break;
                case "light0diffuse": scene.Light0.Diffuse = ParseVector3(value); break;
                case "light0direction": scene.Light0.Direction = ParseVector3(value); break;
                case "primitive": scene.Primitive = ParsePrimitive(value); break;
                case "vertex":
                    if (scene.VertexFormat == SceneVertexFormat.PositionNormalTexture)
                        scene.NormalTextureVertices.Add(ParseNormalTextureVertex(value));
                    else if (scene.VertexFormat == SceneVertexFormat.PositionTexture)
                        scene.TextureVertices.Add(ParseTextureVertex(value));
                    else
                        scene.ColorVertices.Add(ParseColorVertex(value));
                    break;
                default:
                    throw new InvalidDataException("Scene: unknown key '" + key + "' in " + path);
            }
        }
        return scene;
    }

    public int VertexCount()
    {
        if (VertexFormat == SceneVertexFormat.PositionNormalTexture) return NormalTextureVertices.Count;
        if (VertexFormat == SceneVertexFormat.PositionTexture) return TextureVertices.Count;
        return ColorVertices.Count;
    }

    public int PrimitiveCount()
    {
        int n = VertexCount();
        switch (Primitive)
        {
            case PrimitiveType.TriangleList: return n / 3;
            case PrimitiveType.TriangleStrip: return n - 2;
            case PrimitiveType.LineList: return n / 2;
            case PrimitiveType.LineStrip: return n - 1;
        }
        throw new InvalidOperationException("Scene: unhandled PrimitiveType " + Primitive);
    }

    static bool ParseBool(string s) { return s == "true"; }

    static Color ParseColor(string s)
    {
        var p = s.Split(',');
        return new Color(byte.Parse(p[0], CultureInfo.InvariantCulture),
                          byte.Parse(p[1], CultureInfo.InvariantCulture),
                          byte.Parse(p[2], CultureInfo.InvariantCulture),
                          byte.Parse(p[3], CultureInfo.InvariantCulture));
    }

    static Vector3 ParseVector3(string s)
    {
        var p = s.Split(',');
        return new Vector3(float.Parse(p[0], CultureInfo.InvariantCulture),
                            float.Parse(p[1], CultureInfo.InvariantCulture),
                            float.Parse(p[2], CultureInfo.InvariantCulture));
    }

    static CompareFunction ParseCompareFunction(string s)
    {
        switch (s)
        {
            case "Always": return CompareFunction.Always;
            case "Never": return CompareFunction.Never;
            case "Less": return CompareFunction.Less;
            case "LessEqual": return CompareFunction.LessEqual;
            case "Equal": return CompareFunction.Equal;
            case "GreaterEqual": return CompareFunction.GreaterEqual;
            case "Greater": return CompareFunction.Greater;
            case "NotEqual": return CompareFunction.NotEqual;
        }
        throw new InvalidDataException("Scene: unknown compare function '" + s + "'");
    }

    static PrimitiveType ParsePrimitive(string s)
    {
        switch (s)
        {
            case "TriangleList": return PrimitiveType.TriangleList;
            case "TriangleStrip": return PrimitiveType.TriangleStrip;
            case "LineList": return PrimitiveType.LineList;
            case "LineStrip": return PrimitiveType.LineStrip;
        }
        throw new InvalidDataException("Scene: unknown primitive '" + s + "'");
    }

    static VertexPositionColor ParseColorVertex(string s)
    {
        var p = s.Split(',');
        var pos = new Vector3(
            float.Parse(p[0], CultureInfo.InvariantCulture),
            float.Parse(p[1], CultureInfo.InvariantCulture),
            float.Parse(p[2], CultureInfo.InvariantCulture));
        var color = new Color(byte.Parse(p[3], CultureInfo.InvariantCulture),
                               byte.Parse(p[4], CultureInfo.InvariantCulture),
                               byte.Parse(p[5], CultureInfo.InvariantCulture),
                               byte.Parse(p[6], CultureInfo.InvariantCulture));
        return new VertexPositionColor(pos, color);
    }

    static VertexPositionTexture ParseTextureVertex(string s)
    {
        var p = s.Split(',');
        var pos = new Vector3(
            float.Parse(p[0], CultureInfo.InvariantCulture),
            float.Parse(p[1], CultureInfo.InvariantCulture),
            float.Parse(p[2], CultureInfo.InvariantCulture));
        var uv = new Vector2(
            float.Parse(p[3], CultureInfo.InvariantCulture),
            float.Parse(p[4], CultureInfo.InvariantCulture));
        return new VertexPositionTexture(pos, uv);
    }

    static VertexPositionNormalTexture ParseNormalTextureVertex(string s)
    {
        var p = s.Split(',');
        var pos = new Vector3(
            float.Parse(p[0], CultureInfo.InvariantCulture),
            float.Parse(p[1], CultureInfo.InvariantCulture),
            float.Parse(p[2], CultureInfo.InvariantCulture));
        var normal = new Vector3(
            float.Parse(p[3], CultureInfo.InvariantCulture),
            float.Parse(p[4], CultureInfo.InvariantCulture),
            float.Parse(p[5], CultureInfo.InvariantCulture));
        var uv = new Vector2(
            float.Parse(p[6], CultureInfo.InvariantCulture),
            float.Parse(p[7], CultureInfo.InvariantCulture));
        return new VertexPositionNormalTexture(pos, normal, uv);
    }
}

public class Oracle : Game
{
    readonly GraphicsDeviceManager gdm;
    readonly Scene scene;
    readonly string outputPath;
    int frame = 0;

    public Oracle(Scene scene, string outputPath)
    {
        this.scene = scene;
        this.outputPath = outputPath;
        gdm = new GraphicsDeviceManager(this);
        gdm.PreferredBackBufferWidth = scene.Width;
        gdm.PreferredBackBufferHeight = scene.Height;
        gdm.GraphicsProfile = scene.Profile;
    }

    protected override void Draw(GameTime gameTime)
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, scene.Width, scene.Height, false, SurfaceFormat.Color, DepthFormat.Depth24);
        dev.SetRenderTarget(rt);
        dev.Clear(scene.ClearColor);

        Texture2D texture = null;
        if (scene.TextureEnabled)
        {
            texture = new Texture2D(dev, scene.TextureWidth, scene.TextureHeight);
            texture.SetData(scene.TexturePixels.ToArray());
            dev.SamplerStates[0] = scene.TexturePointFilter ? SamplerState.PointClamp : SamplerState.LinearClamp;
        }

        Effect fx;
        if (scene.EffectType == SceneEffectType.AlphaTestEffect)
        {
            var atfx = new AlphaTestEffect(dev);
            atfx.VertexColorEnabled = scene.VertexColorEnabled;
            atfx.Texture = texture;
            atfx.AlphaFunction = scene.AlphaFunction;
            atfx.ReferenceAlpha = scene.ReferenceAlpha;
            atfx.World = Matrix.Identity;
            atfx.View = Matrix.Identity;
            atfx.Projection = Matrix.Identity;
            fx = atfx;
        }
        else
        {
            var bfx = new BasicEffect(dev);
            bfx.VertexColorEnabled = scene.VertexColorEnabled;
            bfx.LightingEnabled = scene.LightingEnabled;
            bfx.TextureEnabled = scene.TextureEnabled;
            bfx.World = Matrix.Identity;
            bfx.View = Matrix.Identity;
            bfx.Projection = Matrix.Identity;
            if (scene.LightingEnabled)
            {
                bfx.AmbientLightColor = scene.AmbientColor;
                bfx.DirectionalLight0.Enabled = scene.Light0.Enabled;
                bfx.DirectionalLight0.DiffuseColor = scene.Light0.Diffuse;
                bfx.DirectionalLight0.Direction = scene.Light0.Direction;
            }
            if (scene.TextureEnabled) bfx.Texture = texture;
            fx = bfx;
        }

        foreach (var pass in fx.CurrentTechnique.Passes)
        {
            pass.Apply();
            if (scene.VertexFormat == SceneVertexFormat.PositionNormalTexture)
                dev.DrawUserPrimitives(scene.Primitive, scene.NormalTextureVertices.ToArray(), 0, scene.PrimitiveCount());
            else if (scene.VertexFormat == SceneVertexFormat.PositionTexture)
                dev.DrawUserPrimitives(scene.Primitive, scene.TextureVertices.ToArray(), 0, scene.PrimitiveCount());
            else
                dev.DrawUserPrimitives(scene.Primitive, scene.ColorVertices.ToArray(), 0, scene.PrimitiveCount());
        }

        dev.SetRenderTarget(null);

        using (var fs = File.Create(outputPath))
            rt.SaveAsPng(fs, scene.Width, scene.Height);

        Console.WriteLine("XNA-ORACLE-OK profile=" + dev.GraphicsProfile
                          + " adapter=" + GraphicsAdapter.DefaultAdapter.Description
                          + " out=" + outputPath);
        if (++frame >= 1) Exit();
        base.Draw(gameTime);
    }

    static void Main(string[] args)
    {
        if (args.Length != 2)
        {
            Console.Error.WriteLine("Usage: Oracle.exe <scene-file> <output-png>");
            Environment.Exit(1);
            return;
        }
        var scene = Scene.Load(args[0]);
        using (var g = new Oracle(scene, args[1])) g.Run();
    }
}
