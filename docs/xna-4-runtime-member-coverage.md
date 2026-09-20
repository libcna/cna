# XNA 4.0 runtime member coverage

Microsoft runtime XML defines the documented census. Matching Microsoft DLL metadata supplies
return types, static ownership, visibility, accessor shape, and enum values. Clang parses CNA
public headers. Content Pipeline XML is excluded. API representation does not establish behavior.

Runtime public types: **331 / 331** (100.00%)

| Member kind | Represented | Documented | Coverage |
| --- | ---: | ---: | ---: |
| Constructors | 239 | 253 | 94.47% |
| Methods | 1503 | 1518 | 99.01% |
| Properties | 1038 | 1040 | 99.81% |
| Fields | 753 | 753 | 100.00% |
| Events | 63 | 63 | 100.00% |
| Operators (subset) | 145 | 145 | 100.00% |
| Indexers (subset) | 29 | 29 | 100.00% |
| Enum Values (subset) | 661 | 661 | 100.00% |

Strict documented members: **3596 / 3627** (99.15%)
C++-applicable members: **3596 / 3627** (99.15%)

## Classifications

- EXACT_EQUIVALENT: 1732
- SEMANTIC_EQUIVALENT: 1806
- HOST_LANGUAGE_SUBSTITUTION: 58
- MISSING: 31
- NOT_APPLICABLE: 0
- NEEDS_REVIEW: 0

`EXACT_EQUIVALENT`, `SEMANTIC_EQUIVALENT`, and `HOST_LANGUAGE_SUBSTITUTION` count as represented.
Only `NOT_APPLICABLE` is removed from the C++-applicable denominator.
`NEEDS_REVIEW` is excluded from both numerators. Every exception requires a per-entry reason.

## Pre-fix baseline

Before production fixes: **3463 / 3627** (95.48%) represented; 164 missing.
See [the frozen per-member baseline](xna-4-runtime-member-coverage-baseline.md).
Newly represented entries: **133**.

- `Microsoft.Xna.Framework.BoundingBox.Equals(System.Object)`
- `Microsoft.Xna.Framework.BoundingFrustum.Equals(System.Object)`
- `Microsoft.Xna.Framework.BoundingSphere.Equals(System.Object)`
- `Microsoft.Xna.Framework.Color.Equals(System.Object)`
- `Microsoft.Xna.Framework.CurveKey.Equals(System.Object)`
- `Microsoft.Xna.Framework.Matrix.Equals(System.Object)`
- `Microsoft.Xna.Framework.Matrix.op_Multiply(System.Single,Microsoft.Xna.Framework.Matrix)`
- `Microsoft.Xna.Framework.Plane.Equals(System.Object)`
- `Microsoft.Xna.Framework.Point.Equals(System.Object)`
- `Microsoft.Xna.Framework.Quaternion.Equals(System.Object)`
- `Microsoft.Xna.Framework.Ray.Equals(System.Object)`
- `Microsoft.Xna.Framework.Rectangle.Equals(System.Object)`
- `Microsoft.Xna.Framework.Vector2.Equals(System.Object)`
- `Microsoft.Xna.Framework.Vector3.Equals(System.Object)`
- `Microsoft.Xna.Framework.Vector4.Equals(System.Object)`
- `Microsoft.Xna.Framework.Audio.SoundEffectInstance.Apply3D(Microsoft.Xna.Framework.Audio.AudioListener[],Microsoft.Xna.Framework.Audio.AudioEmitter)`
- `Microsoft.Xna.Framework.Audio.SoundEffectInstance.Dispose(System.Boolean)`
- `Microsoft.Xna.Framework.Design.BoundingBoxConverter.ConvertFrom(System.ComponentModel.ITypeDescriptorContext,System.Globalization.CultureInfo,System.Object)`
- `Microsoft.Xna.Framework.Design.BoundingSphereConverter.ConvertFrom(System.ComponentModel.ITypeDescriptorContext,System.Globalization.CultureInfo,System.Object)`
- `Microsoft.Xna.Framework.Design.MathTypeConverter.propertyDescriptions`
- `Microsoft.Xna.Framework.Design.MathTypeConverter.supportStringConvert`
- `Microsoft.Xna.Framework.Design.RayConverter.ConvertFrom(System.ComponentModel.ITypeDescriptorContext,System.Globalization.CultureInfo,System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.ToVector3`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Byte4)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.ToVector2`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.ToVector2`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Rg32)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.ToVector2`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Short2)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.ToString`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short2.ToVector2`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Short4)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short4.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short4.GetHashCode`
- `Microsoft.Xna.Framework.Graphics.PackedVector.Short4.ToString`
- `Microsoft.Xna.Framework.Input.GamePadButtons.Equals(System.Object)`
- `Microsoft.Xna.Framework.Input.GamePadButtons.ToString`
- `Microsoft.Xna.Framework.Input.GamePadDPad.Equals(System.Object)`
- `Microsoft.Xna.Framework.Input.GamePadDPad.ToString`
- `Microsoft.Xna.Framework.Input.GamePadState.Equals(System.Object)`
- `Microsoft.Xna.Framework.Input.GamePadThumbSticks.Equals(System.Object)`
- `Microsoft.Xna.Framework.Input.GamePadThumbSticks.ToString`
- `Microsoft.Xna.Framework.Input.GamePadTriggers.Equals(System.Object)`
- `Microsoft.Xna.Framework.Input.GamePadTriggers.ToString`
- `Microsoft.Xna.Framework.Input.GamePadType.BigButtonPad`
- `Microsoft.Xna.Framework.Input.KeyboardState.Equals(System.Object)`
- `Microsoft.Xna.Framework.Input.MouseState.Equals(System.Object)`
- `Microsoft.Xna.Framework.Media.Song.FromUri(System.String,System.Uri)`
- `Microsoft.Xna.Framework.GamerServices.AvatarDescription.Changed`
- `Microsoft.Xna.Framework.GraphicsDeviceInformation.Equals(System.Object)`
- `Microsoft.Xna.Framework.GraphicsDeviceInformation.GetHashCode`
- `Microsoft.Xna.Framework.GamerServices.PropertyDictionary.System#Collections#Generic#ICollection{T}#Add(System.Collections.Generic.KeyValuePair{System.String,System.Object})`
- `Microsoft.Xna.Framework.GamerServices.PropertyDictionary.System#Collections#Generic#ICollection{T}#Contains(System.Collections.Generic.KeyValuePair{System.String,System.Object})`
- `Microsoft.Xna.Framework.GamerServices.PropertyDictionary.System#Collections#Generic#ICollection{T}#Remove(System.Collections.Generic.KeyValuePair{System.String,System.Object})`
- `Microsoft.Xna.Framework.Graphics.BlendFunction.Max`
- `Microsoft.Xna.Framework.Graphics.BlendFunction.Min`
- `Microsoft.Xna.Framework.Graphics.DeviceLostException.#ctor(System.String,System.Exception)`
- `Microsoft.Xna.Framework.Graphics.DeviceNotResetException.#ctor(System.String,System.Exception)`
- `Microsoft.Xna.Framework.Graphics.GraphicsDevice.Dispose(System.Boolean)`
- `Microsoft.Xna.Framework.Graphics.NoSuitableGraphicsDeviceException.#ctor(System.String,System.Exception)`
- `Microsoft.Xna.Framework.Graphics.VertexElement.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.VertexPositionColor.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.VertexPositionColorTexture.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.VertexPositionNormalTexture.Equals(System.Object)`
- `Microsoft.Xna.Framework.Graphics.VertexPositionTexture.Equals(System.Object)`
- `Microsoft.Xna.Framework.Input.Touch.TouchLocation.Equals(System.Object)`
- `Microsoft.Xna.Framework.Audio.AudioCategory.Equals(System.Object)`
- `Microsoft.Xna.Framework.Audio.AudioCategory.ToString`
- `Microsoft.Xna.Framework.Audio.AudioEngine.Dispose(System.Boolean)`
- `Microsoft.Xna.Framework.Audio.RendererDetail.Equals(System.Object)`
- `Microsoft.Xna.Framework.Audio.SoundBank.Dispose(System.Boolean)`
- `Microsoft.Xna.Framework.Audio.WaveBank.Dispose(System.Boolean)`

## Gap review

Tier A gaps remaining: **0**; Tier B: **24**; Tier C: **7**.
Tier B needs type-specific implementation and behavior tests. Tier C requires CLR
serialization/resources, historical device selection, or presentation architecture.
All remaining `MISSING` entries are real absent native contracts under the stated
normalization. Matcher false negatives were corrected before production changes.

## Remaining missing members

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

### Graphics: `Microsoft.Xna.Framework.Graphics.AlphaTestEffect`

- `Microsoft.Xna.Framework.Graphics.AlphaTestEffect.#ctor(Microsoft.Xna.Framework.Graphics.AlphaTestEffect)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.BasicEffect`

- `Microsoft.Xna.Framework.Graphics.BasicEffect.#ctor(Microsoft.Xna.Framework.Graphics.BasicEffect)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

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

- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int16[],System.Int32,System.Int32) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int16[],System.Int32,System.Int32,Microsoft.Xna.Framework.Graphics.VertexDeclaration) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int32[],System.Int32,System.Int32) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int32[],System.Int32,System.Int32,Microsoft.Xna.Framework.Graphics.VertexDeclaration) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- ``` Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,Microsoft.Xna.Framework.Graphics.VertexDeclaration) ``` — Tier B: Generic array draw contract needs typed vertex and index validation and forwarding.
- `Microsoft.Xna.Framework.Graphics.GraphicsDevice.Present(System.Nullable{Microsoft.Xna.Framework.Rectangle},System.Nullable{Microsoft.Xna.Framework.Rectangle},System.IntPtr)` — Tier C: Requires a native window-handle and presentation contract across renderers.

### Graphics: `Microsoft.Xna.Framework.Graphics.IndexBuffer`

- `Microsoft.Xna.Framework.Graphics.IndexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.SkinnedEffect`

- `Microsoft.Xna.Framework.Graphics.SkinnedEffect.#ctor(Microsoft.Xna.Framework.Graphics.SkinnedEffect)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Graphics: `Microsoft.Xna.Framework.Graphics.VertexBuffer`

- `Microsoft.Xna.Framework.Graphics.VertexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)` — Tier B: Graphics constructor needs resource, copy, or inner-exception semantics.

### Net: `Microsoft.Xna.Framework.Net.NetworkSessionJoinException`

- `Microsoft.Xna.Framework.Net.NetworkSessionJoinException.GetObjectData(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` — Tier C: Requires CLR serialization infrastructure.

### Storage: `Microsoft.Xna.Framework.Storage.StorageDeviceNotConnectedException`

- `Microsoft.Xna.Framework.Storage.StorageDeviceNotConnectedException.#ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` — Tier C: Requires CLR serialization infrastructure and exception state restoration.

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
