// Task 472: reference values for FNA's non-rendering Graphics APIs -- enum numeric values and
// the built-in BlendState/DepthStencilState/RasterizerState/SamplerState presets. All 4 state
// classes have a plain parameterless constructor (confirmed by reading
// src/Graphics/States/{BlendState,DepthStencilState,RasterizerState,SamplerState}.cs directly)
// and their presets are `public static readonly` fields -- none of this needs a live
// GraphicsDevice, unlike Task 474's BasicEffect (see this project's own README.md).
//
// Deliberately reflection-based rather than hand-transcribing each enum member's numeric value
// or each preset's field values: several of these enums use explicit, non-sequential values
// (ClearOptions/ColorWriteChannels are bit flags; PresentInterval/SpriteSortMode/SpriteEffects
// assign explicit numbers) -- reading the real, running FNA enum via Enum.GetValues/
// Convert.ToInt32 is exactly the point of this whole harness (Task 471's own header comment:
// verified by *running* the real reference implementation, not just reading/transcribing it),
// and removes any chance of a hand-copied value being wrong.
//
// "Exceptions" (the third item in this task's own one-line description, plans/plan_graphics.md Task
// 472) is deliberately NOT covered here: every Graphics-namespace class whose validation-guard
// exceptions would be worth capturing (Texture2D, VertexBuffer, RenderTarget2D, ...) needs a
// live GraphicsDevice to construct at all -- the same GraphicsDevice/windowing problem flagged
// in Task 471's own README.md as not yet investigated on the FNA/mono side. Deferred, not
// silently skipped.
using System;
using System.Collections.Generic;
using System.Reflection;
using Microsoft.Xna.Framework.Graphics;

namespace CNA.FnaReference
{
    public static class NonRenderingApiReference
    {
        // Every Graphics-namespace enum relevant to non-rendering state -- confirmed against
        // the real src/Graphics/**/*.cs enum declarations, not assumed from memory.
        private static readonly Type[] Enums = new Type[]
        {
            typeof(Blend),
            typeof(BlendFunction),
            typeof(ColorWriteChannels),
            typeof(CompareFunction),
            typeof(CullMode),
            typeof(FillMode),
            typeof(StencilOperation),
            typeof(TextureAddressMode),
            typeof(TextureFilter),
            typeof(PrimitiveType),
            typeof(SurfaceFormat),
            typeof(ClearOptions),
            typeof(BufferUsage),
            typeof(SetDataOptions),
            typeof(IndexElementSize),
            typeof(RenderTargetUsage),
            typeof(PresentInterval),
            typeof(GraphicsProfile),
            typeof(DepthFormat),
            typeof(SpriteSortMode),
            typeof(SpriteEffects),
        };

        public static JsonWriter Generate()
        {
            var enumsOut = new JsonWriter();
            foreach (Type enumType in Enums)
            {
                enumsOut.Add(enumType.Name, DumpEnum(enumType));
            }

            var presetsOut = new JsonWriter()
                .Add("BlendState_Additive", DumpProperties(BlendState.Additive))
                .Add("BlendState_AlphaBlend", DumpProperties(BlendState.AlphaBlend))
                .Add("BlendState_NonPremultiplied", DumpProperties(BlendState.NonPremultiplied))
                .Add("BlendState_Opaque", DumpProperties(BlendState.Opaque))
                .Add("DepthStencilState_Default", DumpProperties(DepthStencilState.Default))
                .Add("DepthStencilState_DepthRead", DumpProperties(DepthStencilState.DepthRead))
                .Add("DepthStencilState_None", DumpProperties(DepthStencilState.None))
                .Add("RasterizerState_CullClockwise", DumpProperties(RasterizerState.CullClockwise))
                .Add("RasterizerState_CullCounterClockwise", DumpProperties(RasterizerState.CullCounterClockwise))
                .Add("RasterizerState_CullNone", DumpProperties(RasterizerState.CullNone))
                .Add("SamplerState_AnisotropicClamp", DumpProperties(SamplerState.AnisotropicClamp))
                .Add("SamplerState_AnisotropicWrap", DumpProperties(SamplerState.AnisotropicWrap))
                .Add("SamplerState_LinearClamp", DumpProperties(SamplerState.LinearClamp))
                .Add("SamplerState_LinearWrap", DumpProperties(SamplerState.LinearWrap))
                .Add("SamplerState_PointClamp", DumpProperties(SamplerState.PointClamp))
                .Add("SamplerState_PointWrap", DumpProperties(SamplerState.PointWrap));

            return new JsonWriter()
                .Add("Enums", enumsOut)
                .Add("StatePresets", presetsOut);
        }

        private static JsonWriter DumpEnum(Type enumType)
        {
            var writer = new JsonWriter();
            foreach (object value in Enum.GetValues(enumType))
            {
                writer.Add(value.ToString(), Convert.ToInt32(value));
            }
            return writer;
        }

        // Generic public-instance-property dump: enum-valued properties are written as their
        // real underlying int (via Enum.GetValues's own runtime type, not assumed), everything
        // else via its natural ToString(). Deliberately generic rather than hand-picking each
        // state class's own property names, for the same "don't transcribe by hand" reason as
        // DumpEnum above -- a state class member that's easy to miss when hand-listing (or that
        // gets added/renamed later) is picked up automatically.
        private static JsonWriter DumpProperties(object instance)
        {
            var writer = new JsonWriter();
            Type type = instance.GetType();
            foreach (PropertyInfo prop in type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (prop.GetIndexParameters().Length > 0) continue; // skip indexers
                object value;
                try
                {
                    value = prop.GetValue(instance, null);
                }
                catch (TargetInvocationException)
                {
                    continue; // property genuinely not readable in this state; skip rather than crash
                }
                if (value == null) continue;

                if (value is Enum)
                {
                    writer.Add(prop.Name, Convert.ToInt32(value));
                }
                else if (value is bool)
                {
                    writer.Add(prop.Name, (bool)value);
                }
                else if (value is float || value is double)
                {
                    writer.Add(prop.Name, Convert.ToDouble(value));
                }
                else if (value is int || value is short || value is byte || value is long)
                {
                    writer.Add(prop.Name, Convert.ToInt32(value));
                }
                else
                {
                    writer.Add(prop.Name, value.ToString());
                }
            }
            return writer;
        }
    }
}
