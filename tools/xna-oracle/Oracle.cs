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
using System.Runtime.InteropServices;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

public enum SceneVertexFormat { PositionColor, PositionTexture, PositionNormalTexture, PositionDualTexture, PositionNormalTextureWeights }
public enum SceneEffectType { BasicEffect, AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect }

public class SceneLight
{
    public bool Enabled;
    public Vector3 Diffuse = Vector3.Zero;
    public Vector3 Direction = new Vector3(0, 0, -1);
}

// Real XNA has no built-in dual-UV vertex struct -- a game using DualTextureEffect defines its
// own IVertexType, exactly like this, matching CnaOracleRender.cpp's own explicit
// VertexDeclaration for the same stride-28 layout (Position+TexCoord0+TexCoord1).
// [StructLayout(LayoutKind.Sequential)] pins field order to declaration order -- C#'s default
// "auto" layout does not formally guarantee this (the CLR is free to reorder fields to minimize
// padding), and DrawUserPrimitives<T> marshals this struct's raw bytes directly against the
// explicit byte-offset VertexDeclaration below, so an auto-reordered layout would silently
// corrupt every attribute after the first.
[StructLayout(LayoutKind.Sequential)]
public struct VertexPositionDualTexture : IVertexType
{
    public Vector3 Position;
    public Vector2 TextureCoordinate0;
    public Vector2 TextureCoordinate1;

    public static readonly VertexDeclaration VertexDeclaration = new VertexDeclaration(
        new VertexElement(0, VertexElementFormat.Vector3, VertexElementUsage.Position, 0),
        new VertexElement(12, VertexElementFormat.Vector2, VertexElementUsage.TextureCoordinate, 0),
        new VertexElement(20, VertexElementFormat.Vector2, VertexElementUsage.TextureCoordinate, 1));

    public VertexPositionDualTexture(Vector3 position, Vector2 uv0, Vector2 uv1)
    {
        Position = position;
        TextureCoordinate0 = uv0;
        TextureCoordinate1 = uv1;
    }

    VertexDeclaration IVertexType.VertexDeclaration { get { return VertexDeclaration; } }
}

// Real XNA has no built-in skinned vertex struct either -- mirrors VertexPositionDualTexture's
// own precedent. Layout matches the existing stride-52 CNA vertex declaration byte-for-byte:
// Position(0)/Normal(12)/TexCoord(24)/BlendWeight(32, Vector4)/BlendIndices(48, Byte4).
[StructLayout(LayoutKind.Sequential)]
public struct VertexPositionNormalTextureWeights : IVertexType
{
    public Vector3 Position;
    public Vector3 Normal;
    public Vector2 TextureCoordinate;
    public Vector4 BlendWeight;
    public byte BlendIndex0, BlendIndex1, BlendIndex2, BlendIndex3;

    public static readonly VertexDeclaration VertexDeclaration = new VertexDeclaration(
        new VertexElement(0,  VertexElementFormat.Vector3, VertexElementUsage.Position, 0),
        new VertexElement(12, VertexElementFormat.Vector3, VertexElementUsage.Normal, 0),
        new VertexElement(24, VertexElementFormat.Vector2, VertexElementUsage.TextureCoordinate, 0),
        new VertexElement(32, VertexElementFormat.Vector4, VertexElementUsage.BlendWeight, 0),
        new VertexElement(48, VertexElementFormat.Byte4, VertexElementUsage.BlendIndices, 0));

    public VertexPositionNormalTextureWeights(Vector3 position, Vector3 normal, Vector2 uv,
                                               byte boneIndex, float boneWeight)
    {
        Position = position;
        Normal = normal;
        TextureCoordinate = uv;
        BlendWeight = new Vector4(boneWeight, 0, 0, 0);
        BlendIndex0 = boneIndex;
        BlendIndex1 = 0;
        BlendIndex2 = 0;
        BlendIndex3 = 0;
    }

    VertexDeclaration IVertexType.VertexDeclaration { get { return VertexDeclaration; } }
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
    public bool Texture2Enabled;
    public int Texture2Width;
    public int Texture2Height;
    public bool EnvironmentMapEnabled;
    public int EnvironmentMapSize;
    public float EnvironmentMapAmount = 1.0f;
    public Color EnvironmentMapPixel = Color.Black;
    public Vector3 DiffuseColor = Vector3.One;
    public Vector3 AmbientColor = Vector3.Zero;
    public SceneLight Light0 = new SceneLight();
    public SceneLight Light1 = new SceneLight();
    public SceneLight Light2 = new SceneLight();
    public SceneEffectType EffectType = SceneEffectType.BasicEffect;
    public CompareFunction AlphaFunction = CompareFunction.Always;
    public int ReferenceAlpha;
    public bool FogEnabled;
    public Vector3 FogColor = Vector3.Zero;
    public float FogStart;
    public float FogEnd = 1.0f;
    public PrimitiveType Primitive = PrimitiveType.TriangleList;
    public List<VertexPositionColor> ColorVertices = new List<VertexPositionColor>();
    public List<VertexPositionTexture> TextureVertices = new List<VertexPositionTexture>();
    public List<VertexPositionNormalTexture> NormalTextureVertices = new List<VertexPositionNormalTexture>();
    public List<VertexPositionDualTexture> DualTextureVertices = new List<VertexPositionDualTexture>();
    public List<VertexPositionNormalTextureWeights> SkinnedVertices = new List<VertexPositionNormalTextureWeights>();
    public List<Color> TexturePixels = new List<Color>();
    public List<Color> Texture2Pixels = new List<Color>();

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
                    if (value == "AlphaTestEffect") scene.EffectType = SceneEffectType.AlphaTestEffect;
                    else if (value == "DualTextureEffect") scene.EffectType = SceneEffectType.DualTextureEffect;
                    else if (value == "EnvironmentMapEffect") scene.EffectType = SceneEffectType.EnvironmentMapEffect;
                    else if (value == "SkinnedEffect") scene.EffectType = SceneEffectType.SkinnedEffect;
                    else scene.EffectType = SceneEffectType.BasicEffect;
                    break;
                case "environmentmap": scene.EnvironmentMapEnabled = ParseBool(value); break;
                case "environmentmapsize": scene.EnvironmentMapSize = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "environmentmapamount": scene.EnvironmentMapAmount = float.Parse(value, CultureInfo.InvariantCulture); break;
                case "environmentmappixel": scene.EnvironmentMapPixel = ParseColor(value); break;
                case "alphafunction": scene.AlphaFunction = ParseCompareFunction(value); break;
                case "referencealpha": scene.ReferenceAlpha = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "fogenabled": scene.FogEnabled = ParseBool(value); break;
                case "fogcolor": scene.FogColor = ParseVector3(value); break;
                case "fogstart": scene.FogStart = float.Parse(value, CultureInfo.InvariantCulture); break;
                case "fogend": scene.FogEnd = float.Parse(value, CultureInfo.InvariantCulture); break;
                case "vertexformat":
                    if (value == "PositionTexture") scene.VertexFormat = SceneVertexFormat.PositionTexture;
                    else if (value == "PositionNormalTexture") scene.VertexFormat = SceneVertexFormat.PositionNormalTexture;
                    else if (value == "PositionDualTexture") scene.VertexFormat = SceneVertexFormat.PositionDualTexture;
                    else if (value == "PositionNormalTextureWeights") scene.VertexFormat = SceneVertexFormat.PositionNormalTextureWeights;
                    else scene.VertexFormat = SceneVertexFormat.PositionColor;
                    break;
                case "vertexcolor": scene.VertexColorEnabled = ParseBool(value); break;
                case "lighting": scene.LightingEnabled = ParseBool(value); break;
                case "texture": scene.TextureEnabled = ParseBool(value); break;
                case "texturewidth": scene.TextureWidth = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "textureheight": scene.TextureHeight = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "texturefilter": scene.TexturePointFilter = value == "Point"; break;
                case "texturepixel": scene.TexturePixels.Add(ParseColor(value)); break;
                case "texture2": scene.Texture2Enabled = ParseBool(value); break;
                case "texture2width": scene.Texture2Width = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "texture2height": scene.Texture2Height = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "texture2pixel": scene.Texture2Pixels.Add(ParseColor(value)); break;
                case "diffusecolor": scene.DiffuseColor = ParseVector3(value); break;
                case "ambientcolor": scene.AmbientColor = ParseVector3(value); break;
                case "light0enabled": scene.Light0.Enabled = ParseBool(value); break;
                case "light0diffuse": scene.Light0.Diffuse = ParseVector3(value); break;
                case "light0direction": scene.Light0.Direction = ParseVector3(value); break;
                case "light1enabled": scene.Light1.Enabled = ParseBool(value); break;
                case "light1diffuse": scene.Light1.Diffuse = ParseVector3(value); break;
                case "light1direction": scene.Light1.Direction = ParseVector3(value); break;
                case "light2enabled": scene.Light2.Enabled = ParseBool(value); break;
                case "light2diffuse": scene.Light2.Diffuse = ParseVector3(value); break;
                case "light2direction": scene.Light2.Direction = ParseVector3(value); break;
                case "primitive": scene.Primitive = ParsePrimitive(value); break;
                case "vertex":
                    if (scene.VertexFormat == SceneVertexFormat.PositionNormalTextureWeights)
                        scene.SkinnedVertices.Add(ParseSkinnedVertex(value));
                    else if (scene.VertexFormat == SceneVertexFormat.PositionDualTexture)
                        scene.DualTextureVertices.Add(ParseDualTextureVertex(value));
                    else if (scene.VertexFormat == SceneVertexFormat.PositionNormalTexture)
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
        if (VertexFormat == SceneVertexFormat.PositionNormalTextureWeights) return SkinnedVertices.Count;
        if (VertexFormat == SceneVertexFormat.PositionDualTexture) return DualTextureVertices.Count;
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

    static VertexPositionDualTexture ParseDualTextureVertex(string s)
    {
        var p = s.Split(',');
        var pos = new Vector3(
            float.Parse(p[0], CultureInfo.InvariantCulture),
            float.Parse(p[1], CultureInfo.InvariantCulture),
            float.Parse(p[2], CultureInfo.InvariantCulture));
        var uv0 = new Vector2(
            float.Parse(p[3], CultureInfo.InvariantCulture),
            float.Parse(p[4], CultureInfo.InvariantCulture));
        var uv1 = new Vector2(
            float.Parse(p[5], CultureInfo.InvariantCulture),
            float.Parse(p[6], CultureInfo.InvariantCulture));
        return new VertexPositionDualTexture(pos, uv0, uv1);
    }

    static VertexPositionNormalTextureWeights ParseSkinnedVertex(string s)
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
        byte boneIndex = byte.Parse(p[8], CultureInfo.InvariantCulture);
        float boneWeight = float.Parse(p[9], CultureInfo.InvariantCulture);
        return new VertexPositionNormalTextureWeights(pos, normal, uv, boneIndex, boneWeight);
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
        Texture2D texture2 = null;
        if (scene.Texture2Enabled)
        {
            texture2 = new Texture2D(dev, scene.Texture2Width, scene.Texture2Height);
            texture2.SetData(scene.Texture2Pixels.ToArray());
        }
        TextureCube environmentMap = null;
        if (scene.EnvironmentMapEnabled)
        {
            environmentMap = new TextureCube(dev, scene.EnvironmentMapSize, false, SurfaceFormat.Color);
            var faceData = new Color[scene.EnvironmentMapSize * scene.EnvironmentMapSize];
            for (int i = 0; i < faceData.Length; i++) faceData[i] = scene.EnvironmentMapPixel;
            foreach (CubeMapFace face in Enum.GetValues(typeof(CubeMapFace)))
                environmentMap.SetData(face, faceData);
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
            atfx.FogEnabled = scene.FogEnabled;
            atfx.FogColor = scene.FogColor;
            atfx.FogStart = scene.FogStart;
            atfx.FogEnd = scene.FogEnd;
            fx = atfx;
        }
        else if (scene.EffectType == SceneEffectType.DualTextureEffect)
        {
            var dtfx = new DualTextureEffect(dev);
            dtfx.VertexColorEnabled = scene.VertexColorEnabled;
            dtfx.Texture = texture;
            dtfx.Texture2 = texture2;
            dtfx.DiffuseColor = scene.DiffuseColor;
            dtfx.World = Matrix.Identity;
            dtfx.View = Matrix.Identity;
            dtfx.Projection = Matrix.Identity;
            dtfx.FogEnabled = scene.FogEnabled;
            dtfx.FogColor = scene.FogColor;
            dtfx.FogStart = scene.FogStart;
            dtfx.FogEnd = scene.FogEnd;
            fx = dtfx;
        }
        else if (scene.EffectType == SceneEffectType.EnvironmentMapEffect)
        {
            var emfx = new EnvironmentMapEffect(dev);
            emfx.Texture = texture;
            emfx.EnvironmentMap = environmentMap;
            emfx.EnvironmentMapAmount = scene.EnvironmentMapAmount;
            emfx.World = Matrix.Identity;
            emfx.View = Matrix.Identity;
            emfx.Projection = Matrix.Identity;
            // EnvironmentMapEffect implements IEffectLights.LightingEnabled via EXPLICIT interface
            // implementation in real XNA/FNA -- it is not a public member of the concrete class (a
            // real CS1061 compile error found live), and the setter throws if given false anyway
            // (lighting is always on for this effect, matching FNA's own NotSupportedException).
            // Do not set it here; a real game using this effect cannot either.
            if (scene.LightingEnabled)
            {
                emfx.AmbientLightColor = scene.AmbientColor;
                emfx.DirectionalLight0.Enabled = scene.Light0.Enabled;
                emfx.DirectionalLight0.DiffuseColor = scene.Light0.Diffuse;
                emfx.DirectionalLight0.Direction = scene.Light0.Direction;
                emfx.DirectionalLight1.Enabled = scene.Light1.Enabled;
                emfx.DirectionalLight1.DiffuseColor = scene.Light1.Diffuse;
                emfx.DirectionalLight1.Direction = scene.Light1.Direction;
                emfx.DirectionalLight2.Enabled = scene.Light2.Enabled;
                emfx.DirectionalLight2.DiffuseColor = scene.Light2.Diffuse;
                emfx.DirectionalLight2.Direction = scene.Light2.Direction;
            }
            emfx.FogEnabled = scene.FogEnabled;
            emfx.FogColor = scene.FogColor;
            emfx.FogStart = scene.FogStart;
            emfx.FogEnd = scene.FogEnd;
            fx = emfx;
        }
        else if (scene.EffectType == SceneEffectType.SkinnedEffect)
        {
            var skfx = new SkinnedEffect(dev);
            skfx.Texture = texture;
            skfx.World = Matrix.Identity;
            skfx.View = Matrix.Identity;
            skfx.Projection = Matrix.Identity;
            // A single Identity bone at 100% vertex weight -- skinning is a mathematical no-op,
            // matching D9-82f's own D3D9_DrawEx CTest discipline, while still genuinely exercising
            // the real per-vertex BLENDWEIGHT0/BLENDINDICES0 upload and the full bone-matrix-array
            // path end to end (not skipped).
            skfx.SetBoneTransforms(new Matrix[] { Matrix.Identity });
            // Same explicit-interface-implementation LightingEnabled carve-out as
            // EnvironmentMapEffect (see that branch's own comment) -- confirmed against FNA's own
            // SkinnedEffect.cs source too.
            if (scene.LightingEnabled)
            {
                skfx.AmbientLightColor = scene.AmbientColor;
                skfx.DirectionalLight0.Enabled = scene.Light0.Enabled;
                skfx.DirectionalLight0.DiffuseColor = scene.Light0.Diffuse;
                skfx.DirectionalLight0.Direction = scene.Light0.Direction;
                skfx.DirectionalLight1.Enabled = scene.Light1.Enabled;
                skfx.DirectionalLight1.DiffuseColor = scene.Light1.Diffuse;
                skfx.DirectionalLight1.Direction = scene.Light1.Direction;
                skfx.DirectionalLight2.Enabled = scene.Light2.Enabled;
                skfx.DirectionalLight2.DiffuseColor = scene.Light2.Diffuse;
                skfx.DirectionalLight2.Direction = scene.Light2.Direction;
            }
            skfx.FogEnabled = scene.FogEnabled;
            skfx.FogColor = scene.FogColor;
            skfx.FogStart = scene.FogStart;
            skfx.FogEnd = scene.FogEnd;
            fx = skfx;
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
                bfx.DirectionalLight1.Enabled = scene.Light1.Enabled;
                bfx.DirectionalLight1.DiffuseColor = scene.Light1.Diffuse;
                bfx.DirectionalLight1.Direction = scene.Light1.Direction;
                bfx.DirectionalLight2.Enabled = scene.Light2.Enabled;
                bfx.DirectionalLight2.DiffuseColor = scene.Light2.Diffuse;
                bfx.DirectionalLight2.Direction = scene.Light2.Direction;
            }
            if (scene.TextureEnabled) bfx.Texture = texture;
            bfx.FogEnabled = scene.FogEnabled;
            bfx.FogColor = scene.FogColor;
            bfx.FogStart = scene.FogStart;
            bfx.FogEnd = scene.FogEnd;
            fx = bfx;
        }

        foreach (var pass in fx.CurrentTechnique.Passes)
        {
            pass.Apply();
            if (scene.VertexFormat == SceneVertexFormat.PositionNormalTextureWeights)
                dev.DrawUserPrimitives(scene.Primitive, scene.SkinnedVertices.ToArray(), 0, scene.PrimitiveCount());
            else if (scene.VertexFormat == SceneVertexFormat.PositionDualTexture)
                dev.DrawUserPrimitives(scene.Primitive, scene.DualTextureVertices.ToArray(), 0, scene.PrimitiveCount());
            else if (scene.VertexFormat == SceneVertexFormat.PositionNormalTexture)
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
