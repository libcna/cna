# XNA 4.0 runtime member coverage — baseline

Microsoft runtime XML defines the documented census. Matching Microsoft DLL metadata supplies
return types, static ownership, visibility, accessor shape, and enum values. Clang parses CNA
public headers. Content Pipeline XML is excluded. API representation does not establish behavior.

Runtime public types: **331 / 331** (100.00%)

| Member kind | Represented | Documented | Coverage |
| --- | ---: | ---: | ---: |
| Constructors | 236 | 253 | 93.28% |
| Methods | 1379 | 1518 | 90.84% |
| Properties | 1038 | 1040 | 99.81% |
| Fields | 748 | 753 | 99.34% |
| Events | 62 | 63 | 98.41% |
| Operators (subset) | 144 | 145 | 99.31% |
| Indexers (subset) | 29 | 29 | 100.00% |
| Enum Values (subset) | 658 | 661 | 99.55% |

Strict documented members: **3463 / 3627** (95.48%)
C++-applicable members: **3463 / 3627** (95.48%)

## Classifications

- EXACT_EQUIVALENT: 1658
- SEMANTIC_EQUIVALENT: 1747
- HOST_LANGUAGE_SUBSTITUTION: 58
- MISSING: 164
- NOT_APPLICABLE: 0
- NEEDS_REVIEW: 0

`EXACT_EQUIVALENT`, `SEMANTIC_EQUIVALENT`, and `HOST_LANGUAGE_SUBSTITUTION` count as represented.
Only `NOT_APPLICABLE` is removed from the C++-applicable denominator.
`NEEDS_REVIEW` is excluded from both numerators. Every exception requires a per-entry reason.

## Gap review

Tier A gaps remaining: **4**; Tier B: **153**; Tier C: **7**.
Tier B needs type-specific implementation and behavior tests. Tier C requires CLR
serialization/resources, historical device selection, or presentation architecture.
All remaining `MISSING` entries are real absent native contracts under the stated
normalization. Matcher false negatives were corrected before production changes.

## Remaining missing members

### Audio: `Microsoft.Xna.Framework.Audio.SoundEffectInstance`

- `Microsoft.Xna.Framework.Audio.SoundEffectInstance.Apply3D(Microsoft.Xna.Framework.Audio.AudioListener[],Microsoft.Xna.Framework.Audio.AudioEmitter)` — Tier B: Array-based spatialization needs multi-listener behavior and validation.
- `Microsoft.Xna.Framework.Audio.SoundEffectInstance.Dispose(System.Boolean)` — Tier B: Protected disposal hook must preserve the existing resource lifetime contract.

### Avatar: `Microsoft.Xna.Framework.GamerServices.AvatarDescription`

- `Microsoft.Xna.Framework.GamerServices.AvatarDescription.Changed` — Tier B: Avatar change notification needs event ownership and delivery semantics.

### Content: `Microsoft.Xna.Framework.Content.ContentLoadException`

- `Microsoft.Xna.Framework.Content.ContentLoadException.#ctor` — Tier B: Content reader, manager, or exception contract needs implementation and validation.
- `Microsoft.Xna.Framework.Content.ContentLoadException.#ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` — Tier C: Requires CLR serialization infrastructure and exception state restoration.

### Content: `Microsoft.Xna.Framework.Content.ContentManager`

- `Microsoft.Xna.Framework.Content.ContentManager.Dispose(System.Boolean)` — Tier B: Protected disposal hook must preserve the existing resource lifetime contract.
- `Microsoft.Xna.Framework.Content.ContentManager.OpenStream(System.String)` — Tier B: Content reader, manager, or exception contract needs implementation and validation.
- ``` Microsoft.Xna.Framework.Content.ContentManager.ReadAsset``1(System.String,System.Action{System.IDisposable}) ``` — Tier B: Content reader, manager, or exception contract needs implementation and validation.

### Content: `Microsoft.Xna.Framework.Content.ContentReader`

- ``` Microsoft.Xna.Framework.Content.ContentReader.ReadRawObject``1 ``` — Tier B: Content reader, manager, or exception contract needs implementation and validation.
- ``` Microsoft.Xna.Framework.Content.ContentReader.ReadRawObject``1(``0) ``` — Tier B: Content reader, manager, or exception contract needs implementation and validation.

### Content: `Microsoft.Xna.Framework.Content.ContentTypeReaderManager`

- `Microsoft.Xna.Framework.Content.ContentTypeReaderManager.GetTypeReader(System.Type)` — Tier B: Content reader, manager, or exception contract needs implementation and validation.

### Content: `` Microsoft.Xna.Framework.Content.ContentTypeReader`1 ``

- `` Microsoft.Xna.Framework.Content.ContentTypeReader`1.#ctor `` — Tier B: Content reader, manager, or exception contract needs implementation and validation.
- `` Microsoft.Xna.Framework.Content.ContentTypeReader`1.Read(Microsoft.Xna.Framework.Content.ContentReader,System.Object) `` — Tier B: Content reader, manager, or exception contract needs implementation and validation.

### Content: `Microsoft.Xna.Framework.Content.ResourceContentManager`

- `Microsoft.Xna.Framework.Content.ResourceContentManager.#ctor(System.IServiceProvider,System.Resources.ResourceManager)` — Tier C: Requires System.Resources.ResourceManager support in Sharp Runtime.

### Core / Math: `Microsoft.Xna.Framework.BoundingBox`

- `Microsoft.Xna.Framework.BoundingBox.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.BoundingFrustum`

- `Microsoft.Xna.Framework.BoundingFrustum.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.BoundingSphere`

- `Microsoft.Xna.Framework.BoundingSphere.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.Color`

- `Microsoft.Xna.Framework.Color.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.CurveKey`

- `Microsoft.Xna.Framework.CurveKey.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector`

- `Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Core / Math: `Microsoft.Xna.Framework.Matrix`

- `Microsoft.Xna.Framework.Matrix.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.
- `Microsoft.Xna.Framework.Matrix.op_Multiply(System.Single,Microsoft.Xna.Framework.Matrix)` — Tier A: Scalar-left multiplication forwards to the existing Matrix::Multiply implementation.

### Core / Math: `Microsoft.Xna.Framework.Plane`

- `Microsoft.Xna.Framework.Plane.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.Point`

- `Microsoft.Xna.Framework.Point.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.Quaternion`

- `Microsoft.Xna.Framework.Quaternion.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.Ray`

- `Microsoft.Xna.Framework.Ray.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.Rectangle`

- `Microsoft.Xna.Framework.Rectangle.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.Vector2`

- `Microsoft.Xna.Framework.Vector2.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.Vector3`

- `Microsoft.Xna.Framework.Vector3.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Core / Math: `Microsoft.Xna.Framework.Vector4`

- `Microsoft.Xna.Framework.Vector4.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Design: `Microsoft.Xna.Framework.Design.BoundingBoxConverter`

- `Microsoft.Xna.Framework.Design.BoundingBoxConverter.ConvertFrom(System.ComponentModel.ITypeDescriptorContext,System.Globalization.CultureInfo,System.Object)` — Tier B: Type-converter contract needs context and culture behavior.

### Design: `Microsoft.Xna.Framework.Design.BoundingSphereConverter`

- `Microsoft.Xna.Framework.Design.BoundingSphereConverter.ConvertFrom(System.ComponentModel.ITypeDescriptorContext,System.Globalization.CultureInfo,System.Object)` — Tier B: Type-converter contract needs context and culture behavior.

### Design: `Microsoft.Xna.Framework.Design.MathTypeConverter`

- `Microsoft.Xna.Framework.Design.MathTypeConverter.propertyDescriptions` — Tier B: Type-converter contract needs context and culture behavior.
- `Microsoft.Xna.Framework.Design.MathTypeConverter.supportStringConvert` — Tier B: Type-converter contract needs context and culture behavior.

### Design: `Microsoft.Xna.Framework.Design.RayConverter`

- `Microsoft.Xna.Framework.Design.RayConverter.ConvertFrom(System.ComponentModel.ITypeDescriptorContext,System.Globalization.CultureInfo,System.Object)` — Tier B: Type-converter contract needs context and culture behavior.

### GamerServices: `Microsoft.Xna.Framework.GamerServices.PropertyDictionary`

- `Microsoft.Xna.Framework.GamerServices.PropertyDictionary.System#Collections#Generic#ICollection{T}#Add(System.Collections.Generic.KeyValuePair{System.String,System.Object})` — Tier B: Event or collection contract needs subscription or key-value semantics.
- `Microsoft.Xna.Framework.GamerServices.PropertyDictionary.System#Collections#Generic#ICollection{T}#Contains(System.Collections.Generic.KeyValuePair{System.String,System.Object})` — Tier B: Event or collection contract needs subscription or key-value semantics.
- `Microsoft.Xna.Framework.GamerServices.PropertyDictionary.System#Collections#Generic#ICollection{T}#Remove(System.Collections.Generic.KeyValuePair{System.String,System.Object})` — Tier B: Event or collection contract needs subscription or key-value semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.AlphaTestEffect`

- `Microsoft.Xna.Framework.Graphics.AlphaTestEffect.#ctor(Microsoft.Xna.Framework.Graphics.AlphaTestEffect)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.BasicEffect`

- `Microsoft.Xna.Framework.Graphics.BasicEffect.#ctor(Microsoft.Xna.Framework.Graphics.BasicEffect)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.BlendFunction`

- `Microsoft.Xna.Framework.Graphics.BlendFunction.Max` — Tier A: Correct the documented enum value to 4 and translate renderer ordinals.
- `Microsoft.Xna.Framework.Graphics.BlendFunction.Min` — Tier A: Correct the documented enum value to 3 and translate renderer ordinals.

### Graphics: `Microsoft.Xna.Framework.Graphics.DeviceLostException`

- `Microsoft.Xna.Framework.Graphics.DeviceLostException.#ctor(System.String,System.Exception)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.DeviceNotResetException`

- `Microsoft.Xna.Framework.Graphics.DeviceNotResetException.#ctor(System.String,System.Exception)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.DualTextureEffect`

- `Microsoft.Xna.Framework.Graphics.DualTextureEffect.#ctor(Microsoft.Xna.Framework.Graphics.DualTextureEffect)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.DynamicIndexBuffer`

- `Microsoft.Xna.Framework.Graphics.DynamicIndexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.DynamicVertexBuffer`

- `Microsoft.Xna.Framework.Graphics.DynamicVertexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect`

- `Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect.#ctor(Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.GraphicsAdapter`

- `Microsoft.Xna.Framework.Graphics.GraphicsAdapter.UseNullDevice` — Tier C: Controls the historical Microsoft graphics device selection path.
- `Microsoft.Xna.Framework.Graphics.GraphicsAdapter.UseReferenceDevice` — Tier C: Controls the historical Microsoft reference-device selection path.

### Graphics: `Microsoft.Xna.Framework.Graphics.GraphicsDevice`

- `Microsoft.Xna.Framework.Graphics.GraphicsDevice.Dispose(System.Boolean)` — Tier B: Protected disposal hook must preserve the existing resource lifetime contract.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int16[],System.Int32,System.Int32) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int16[],System.Int32,System.Int32,Microsoft.Xna.Framework.Graphics.VertexDeclaration) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int32[],System.Int32,System.Int32) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int32[],System.Int32,System.Int32,Microsoft.Xna.Framework.Graphics.VertexDeclaration) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,Microsoft.Xna.Framework.Graphics.VertexDeclaration) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- `Microsoft.Xna.Framework.Graphics.GraphicsDevice.Present(System.Nullable{Microsoft.Xna.Framework.Rectangle},System.Nullable{Microsoft.Xna.Framework.Rectangle},System.IntPtr)` — Tier C: Requires a native window-handle and presentation contract across renderers.

### Graphics: `Microsoft.Xna.Framework.Graphics.IndexBuffer`

- `Microsoft.Xna.Framework.Graphics.IndexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.NoSuitableGraphicsDeviceException`

- `Microsoft.Xna.Framework.Graphics.NoSuitableGraphicsDeviceException.#ctor(System.String,System.Exception)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.ToVector3` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Byte4`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Byte4)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle`

- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2`

- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4`

- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2`

- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.ToVector2` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4`

- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2`

- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.ToVector2` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4`

- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Rg32)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.ToVector2` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Short2`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Short2)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.ToVector2` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.PackedVector.Short4`

- `Microsoft.Xna.Framework.Graphics.PackedVector.Short4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Short4)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short4.Equals(System.Object)` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short4.GetHashCode` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short4.ToString` — Tier B: Packed value conversion and object-contract methods need per-format semantics and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.SkinnedEffect`

- `Microsoft.Xna.Framework.Graphics.SkinnedEffect.#ctor(Microsoft.Xna.Framework.Graphics.SkinnedEffect)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.VertexBuffer`

- `Microsoft.Xna.Framework.Graphics.VertexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.VertexElement`

- `Microsoft.Xna.Framework.Graphics.VertexElement.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.VertexPositionColor`

- `Microsoft.Xna.Framework.Graphics.VertexPositionColor.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.VertexPositionColorTexture`

- `Microsoft.Xna.Framework.Graphics.VertexPositionColorTexture.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.VertexPositionNormalTexture`

- `Microsoft.Xna.Framework.Graphics.VertexPositionNormalTexture.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Graphics: `Microsoft.Xna.Framework.Graphics.VertexPositionTexture`

- `Microsoft.Xna.Framework.Graphics.VertexPositionTexture.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Input: `Microsoft.Xna.Framework.Input.GamePadButtons`

- `Microsoft.Xna.Framework.Input.GamePadButtons.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.
- `Microsoft.Xna.Framework.Input.GamePadButtons.ToString` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Input: `Microsoft.Xna.Framework.Input.GamePadDPad`

- `Microsoft.Xna.Framework.Input.GamePadDPad.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.
- `Microsoft.Xna.Framework.Input.GamePadDPad.ToString` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Input: `Microsoft.Xna.Framework.Input.GamePadState`

- `Microsoft.Xna.Framework.Input.GamePadState.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Input: `Microsoft.Xna.Framework.Input.GamePadThumbSticks`

- `Microsoft.Xna.Framework.Input.GamePadThumbSticks.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.
- `Microsoft.Xna.Framework.Input.GamePadThumbSticks.ToString` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Input: `Microsoft.Xna.Framework.Input.GamePadTriggers`

- `Microsoft.Xna.Framework.Input.GamePadTriggers.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.
- `Microsoft.Xna.Framework.Input.GamePadTriggers.ToString` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Input: `Microsoft.Xna.Framework.Input.GamePadType`

- `Microsoft.Xna.Framework.Input.GamePadType.BigButtonPad` — Tier A: Correct the documented enum value to 768.

### Input: `Microsoft.Xna.Framework.Input.KeyboardState`

- `Microsoft.Xna.Framework.Input.KeyboardState.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Input: `Microsoft.Xna.Framework.Input.MouseState`

- `Microsoft.Xna.Framework.Input.MouseState.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Media: `Microsoft.Xna.Framework.Media.Song`

- `Microsoft.Xna.Framework.Media.Song.FromUri(System.String,System.Uri)` — Tier B: URI song construction needs file and metadata behavior.

### Net: `Microsoft.Xna.Framework.Net.NetworkSessionJoinException`

- `Microsoft.Xna.Framework.Net.NetworkSessionJoinException.GetObjectData(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` — Tier C: Requires CLR serialization infrastructure.

### Runtime: `Microsoft.Xna.Framework.GraphicsDeviceInformation`

- `Microsoft.Xna.Framework.GraphicsDeviceInformation.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.
- `Microsoft.Xna.Framework.GraphicsDeviceInformation.GetHashCode` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### Storage: `Microsoft.Xna.Framework.Storage.StorageDeviceNotConnectedException`

- `Microsoft.Xna.Framework.Storage.StorageDeviceNotConnectedException.#ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` — Tier C: Requires CLR serialization infrastructure and exception state restoration.

### Touch: `Microsoft.Xna.Framework.Input.Touch.TouchLocation`

- `Microsoft.Xna.Framework.Input.Touch.TouchLocation.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### XACT: `Microsoft.Xna.Framework.Audio.AudioCategory`

- `Microsoft.Xna.Framework.Audio.AudioCategory.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.
- `Microsoft.Xna.Framework.Audio.AudioCategory.ToString` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### XACT: `Microsoft.Xna.Framework.Audio.AudioEngine`

- `Microsoft.Xna.Framework.Audio.AudioEngine.Dispose(System.Boolean)` — Tier B: Protected disposal hook must preserve the existing resource lifetime contract.

### XACT: `Microsoft.Xna.Framework.Audio.RendererDetail`

- `Microsoft.Xna.Framework.Audio.RendererDetail.Equals(System.Object)` — Tier B: Object-contract overload or formatting requires type-specific behavior and tests.

### XACT: `Microsoft.Xna.Framework.Audio.SoundBank`

- `Microsoft.Xna.Framework.Audio.SoundBank.Dispose(System.Boolean)` — Tier B: Protected disposal hook must preserve the existing resource lifetime contract.

### XACT: `Microsoft.Xna.Framework.Audio.WaveBank`

- `Microsoft.Xna.Framework.Audio.WaveBank.Dispose(System.Boolean)` — Tier B: Protected disposal hook must preserve the existing resource lifetime contract.

## Entries needing review

None.

## Methodology

The reference set is the ten runtime XML files from the XNA 4.0 SDK; the build-time
Content Pipeline XML is excluded. The XML entry is the unit of measurement, including
enum fields and explicit interface members. CLR generic arity, overload parameters,
ref/out markers, static ownership, return/type metadata, property accessors, and enum
numeric values are retained. The CNA model uses public/protected Clang AST declarations.
C++ `const T&` maps an input value, mutable `T&` maps CLR byref, pointer and collection
shapes are considered only through explicit normalization. Property accessor pairs use
CNA's `getXProperty` / `setXProperty` convention. Ambiguity remains `NEEDS_REVIEW`.
Eight explicit `GetEnumerator` XML records are absent from the corresponding
Microsoft DLL metadata; they remain in the denominator and have per-entry notes.
Behavior, exceptions, event delivery, renderer results, and historical online-service
availability require separate validation.
The machine-readable JSON contains each reference entry, match, and classification.
