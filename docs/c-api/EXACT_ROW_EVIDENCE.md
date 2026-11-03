# Exact-row C API evidence review — MTH-CABI-009

**ABI 0.60 owner-contract closure (2026-11-03):** [CLOSURE_060.md](CLOSURE_060.md) records
Robert Vokac's two component partial approvals, two transport-managed no-C-form approvals and the
three new routes for native shared machines and a logical-size provider. Current accounting is
7,512 implemented / 17 partial / 4 planned / 609 not applicable. All four planned rows require
Windows path qualification. Provider execution qualification is stated separately from inventory;
ABI 0.59 and earlier counts below are historical snapshots.

**ABI 0.59 Follow-up 1 (2026-11-03):** [CLOSURE_059.md](CLOSURE_059.md) records the native
cursor/mutable-resource implementations and retained owner execution. The focused private SDLGPU
run passed **6 / 0 failed / 0 skipped**, including the cube descriptor assertions and VideoSmoke's
positive decoded-video marker. The cube constructor and Video destructor are now individually
mapped, leaving **11 planned rows: seven owner decisions and four Windows path cases**. The
original 26-row review below is historical; the current exact accounting is COVERAGE.md.

Original CABI-CLOSE-A review, 2026-11-03: 35 existing-route evidence rows and eight newer GameComponentCollection enumerator declarations were reviewed. **26 rows were mapped; 17 reviewed rows remained planned** at that snapshot, reducing the original 60 planned rows to **34**. The 15 approved partials and 607 not-applicable rows were unchanged. That review changed no ABI, export, exclusion, route budget or owner approval. The ABI 0.59 follow-up below resolves the seventeen obligations; those historical counts are not the current inventory.

Coverage status is recorded by singleton `approved_symbols` in `tools/c-api/coverage_mappings.json`, with anchored header/name/signature/kind patterns. The new rules adopt only the IDs below; no prior approval set grows. Full inventory regeneration and the release gate continue to report **Not ready**.

## Execution provenance

The retained private SDLGPU owner run [`owner-fr2-sdltree.log`](../../../cnaplan/evidence/exit002/owner-fr2-sdltree.log) records **175 passed / 0 failed / 0 skipped**, including all 12 affected smoke suites. Its SHA-256 is `59b511b9ed3d60131f8bfff09b74452c7a4c9dc5535db6b21554e2e0402c0c24`. This is historical owner execution, not a new lane run. Review inspected actual calls and independent assertions inside the named functions; mentioning an export in source is insufficient. The C descriptor forms below replace reflection/template deduction without claiming a C++ type object or cursor identity.

Fresh exact-row runtime execution uses the window-free `CApi_ModelMeshCursorValues` CTest: **1 passed / 0 failed / 0 skipped**, with DISPLAY and WAYLAND_DISPLAY unset. It actually creates an empty mesh collection and checks ownerless/empty/restarted/exhausted/null-output/oversized-index/stale-token behavior and failure sentinels. The existing device-backed two-element traversal remains covered by the retained owner run. Added device assertions were subsequently exercised by the owner on 2026-11-03: the focused six-suite run and full-suite counts are recorded below. Eight additional window-free gate/tool CTests also pass, making nine distinct passing CTests; the generated-route Python suite passes all five tests.

## Accepted exact rows from the original review

| Stable ID | Native declaration | Existing C form | Called strict-C witness |
|---|---|---|---|
| `CPP-2E063F1CF1F8` | `Audio::AudioCategory::ToString() const` | `cna_audio_category_get_name_size / cna_audio_category_copy_name` | `XactSmoke.c::validate_categories` |
| `CPP-A4BCC1C104ED` | `Audio::SoundEffectInstance::Apply3D(const std::vector< AudioListener > &listeners, const AudioEmitter &emitter)` | `cna_sound_effect_instance_apply_3d_multi_ext` | `Audio3DSmoke.c::validate_multi_listener` |
| `CPP-A572EF0F2249` | `GamerServices::GamerServicesComponent::~GamerServicesComponent() override` | `cna_gamer_services_component_create / cna_game_component_get_type_name_size / cna_game_component_copy_type_name / cna_game_component_destroy` | `GuideSmoke.c::validate_component_routes` |
| `CPP-53A5A6F26E3E` | `GamerServices::GamerServicesComponent::GetTypeName() const override` | `cna_gamer_services_component_create / cna_game_component_get_type_name_size / cna_game_component_copy_type_name / cna_game_component_destroy` | `GuideSmoke.c::validate_component_routes` |
| `CPP-B9D3EA8F3EFF` | `Graphics::DynamicIndexBuffer::DynamicIndexBuffer(GraphicsDevice &device, const System::Type &indexType, int indexCount, BufferUsage bufferUsage)` | `CNA_IndexBufferCreateInfo.index_element_size and dynamic; cna_index_buffer_create` | `IndexBufferSmoke.c::validate_transfer / validate_lifecycle_and_failures` |
| `CPP-651E15F3420B` | `Graphics::DynamicVertexBuffer::DynamicVertexBuffer(GraphicsDevice &device, const System::Type &vertexType, int vertexCount, BufferUsage bufferUsage)` | `CNA_VertexBufferCreateInfo.declaration and dynamic; cna_vertex_declaration_create_with_stride / cna_vertex_buffer_create` | `VertexBufferSmoke.c::validate_transfer / validate_lifecycle_and_failures` |
| `CPP-89825A7FFECA` | `Graphics::GraphicsDevice::DrawUserPrimitives template<typename TVertex> (PrimitiveType primitiveType, const std::vector< TVertex > &vertexData, int vertexOffset, int primitiveCount)` | `cna_graphics_device_draw_user_primitives / cna_graphics_device_draw_user_indexed_primitives with CNA_UserPrimitives / CNA_UserIndices` | `Draw3DSmoke.c::validate_user_overloads / validate_hi_def_user_overloads` |
| `CPP-B4A8189737E1` | `Graphics::GraphicsDevice::DrawUserPrimitives template<typename TVertex> (PrimitiveType primitiveType, const std::vector< TVertex > &vertexData, int vertexOffset, int primitiveCount, const VertexDeclaration &vertexDeclaration)` | `cna_graphics_device_draw_user_primitives / cna_graphics_device_draw_user_indexed_primitives with CNA_UserPrimitives / CNA_UserIndices` | `Draw3DSmoke.c::validate_user_overloads / validate_hi_def_user_overloads` |
| `CPP-454178B2557F` | `Graphics::GraphicsDevice::DrawUserIndexedPrimitives template<typename TVertex> (PrimitiveType primitiveType, const std::vector< TVertex > &vertexData, int vertexOffset, int numVertices, const std::vector< std::int16_t > &indexData, int indexOffset, int primitiveCount)` | `cna_graphics_device_draw_user_primitives / cna_graphics_device_draw_user_indexed_primitives with CNA_UserPrimitives / CNA_UserIndices` | `Draw3DSmoke.c::validate_user_overloads / validate_hi_def_user_overloads` |
| `CPP-D8C1999D9FC4` | `Graphics::GraphicsDevice::DrawUserIndexedPrimitives template<typename TVertex> (PrimitiveType primitiveType, const std::vector< TVertex > &vertexData, int vertexOffset, int numVertices, const std::vector< std::int16_t > &indexData, int indexOffset, int primitiveCount, const VertexDeclaration &vertexDeclaration)` | `cna_graphics_device_draw_user_primitives / cna_graphics_device_draw_user_indexed_primitives with CNA_UserPrimitives / CNA_UserIndices` | `Draw3DSmoke.c::validate_user_overloads / validate_hi_def_user_overloads` |
| `CPP-A68399F971EC` | `Graphics::GraphicsDevice::DrawUserIndexedPrimitives template<typename TVertex> (PrimitiveType primitiveType, const std::vector< TVertex > &vertexData, int vertexOffset, int numVertices, const std::vector< std::int32_t > &indexData, int indexOffset, int primitiveCount)` | `cna_graphics_device_draw_user_primitives / cna_graphics_device_draw_user_indexed_primitives with CNA_UserPrimitives / CNA_UserIndices` | `Draw3DSmoke.c::validate_user_overloads / validate_hi_def_user_overloads` |
| `CPP-4EA17F872861` | `Graphics::GraphicsDevice::DrawUserIndexedPrimitives template<typename TVertex> (PrimitiveType primitiveType, const std::vector< TVertex > &vertexData, int vertexOffset, int numVertices, const std::vector< std::int32_t > &indexData, int indexOffset, int primitiveCount, const VertexDeclaration &vertexDeclaration)` | `cna_graphics_device_draw_user_primitives / cna_graphics_device_draw_user_indexed_primitives with CNA_UserPrimitives / CNA_UserIndices` | `Draw3DSmoke.c::validate_user_overloads / validate_hi_def_user_overloads` |
| `CPP-27AEEF16EED8` | `Graphics::IndexBuffer::IndexBuffer(GraphicsDevice &device, const System::Type &indexType, int indexCount, BufferUsage bufferUsage)` | `CNA_IndexBufferCreateInfo.index_element_size and dynamic; cna_index_buffer_create` | `IndexBufferSmoke.c::validate_transfer / validate_lifecycle_and_failures` |
| `CPP-3C7D35284F00` | `Graphics::ModelMeshCollection::Enumerator` | `cna_model_mesh_collection_get_count / cna_model_mesh_collection_get_at and a caller-owned captured-count index` | `ModelMeshSmoke.c::validate_index_cursors` |
| `CPP-4B8A53BA6E49` | `Graphics::ModelMeshCollection::Enumerator::Enumerator()=default` | `cna_model_mesh_collection_get_count / cna_model_mesh_collection_get_at and a caller-owned captured-count index` | `ModelMeshSmoke.c::validate_index_cursors` |
| `CPP-FB47CDD31409` | `Graphics::ModelMeshCollection::Enumerator::Current() const override` | `cna_model_mesh_collection_get_count / cna_model_mesh_collection_get_at and a caller-owned captured-count index` | `ModelMeshSmoke.c::validate_index_cursors` |
| `CPP-CF017196F608` | `Graphics::ModelMeshCollection::Enumerator::MoveNext() override` | `cna_model_mesh_collection_get_count / cna_model_mesh_collection_get_at and a caller-owned captured-count index` | `ModelMeshSmoke.c::validate_index_cursors` |
| `CPP-335EA189FB93` | `Graphics::ModelMeshCollection::Enumerator::Reset() override` | `cna_model_mesh_collection_get_count / cna_model_mesh_collection_get_at and a caller-owned captured-count index` | `ModelMeshSmoke.c::validate_index_cursors` |
| `CPP-79371A180F0F` | `Graphics::ModelMeshCollection::Enumerator::Dispose() override` | `cna_model_mesh_collection_get_count / cna_model_mesh_collection_get_at and a caller-owned captured-count index` | `ModelMeshSmoke.c::validate_index_cursors` |
| `CPP-3A533AF65263` | `Graphics::ModelMeshCollection::GetEnumerator() const` | `cna_model_mesh_collection_get_count / cna_model_mesh_collection_get_at and a caller-owned captured-count index` | `ModelMeshSmoke.c::validate_index_cursors` |
| `CPP-709095C23509` | `Graphics::RenderTargetBinding::RenderTargetBinding(RenderTarget2D *renderTarget)` | `CNA_RenderTargetBinding with cna_graphics_device_set_render_targets / cna_graphics_device_copy_render_targets` | `GraphicsSurfaceSmoke.c::create_render_targets` |
| `CPP-0FBA5A160FD6` | `Graphics::SamplerStateCollection::operator()(int index, const SamplerState &value)` | `cna_graphics_device_set_sampler_state / cna_graphics_device_get_sampler_state` | `GraphicsSurfaceSmoke.c::validate_sampler_setter` |
| `CPP-DD1947B1CBC8` | `Graphics::Texture2D::DDSFromStreamEXT(GraphicsDevice &graphicsDevice, System::IO::Stream &stream)` | `cna_texture2d_create_from_encoded_memory with optional CNA_Texture2DDecodeInfo` | `TextureSmoke.c::validate_dds_routes` |
| `CPP-828593548EC5` | `Graphics::Texture2D::DDSFromStreamEXT(GraphicsDevice &graphicsDevice, System::IO::Stream &stream, int width, int height, bool zoom)` | `cna_texture2d_create_from_encoded_memory with optional CNA_Texture2DDecodeInfo` | `TextureSmoke.c::validate_dds_routes` |
| `CPP-C4B082503D27` | `Graphics::VertexBuffer::VertexBuffer(GraphicsDevice &device, const System::Type &vertexType, int vertexCount, BufferUsage bufferUsage)` | `CNA_VertexBufferCreateInfo.declaration and dynamic; cna_vertex_declaration_create_with_stride / cna_vertex_buffer_create` | `VertexBufferSmoke.c::validate_transfer / validate_lifecycle_and_failures` |
| `CPP-E2A9DC7BA273` | `Media::Song::FromUri(const std::string &name, const System::Uri &uri)` | `cna_song_create_from_uri` | `MediaSmoke.c::validate_song_family` |

The per-rule mapping text records the success values, applicable refusals and ownership evidence. In particular: Draw3DSmoke executes both implicit/explicit declarations and 16/32-bit indices in HiDef, with Reach refusal and pixel checks; TextureSmoke uses actual DDS bytes and independently expected dimensions/blocks/red pixels; GuideSmoke asserts the exact derived type name and stale release; buffer smokes execute actual construction, transfer, descriptor refusal, disposal and stale handles. Mesh iteration captures an immutable C collection/count/index, destroys each owned element view, restarts locally, and discards the local cursor without destroying the collection.

## ABI 0.59 resolution of the seventeen reviewed obligations

The seventeen rows retained as planned by the original review now have complete C forms and
executed witnesses; no partial disposition was approved or needed:

| Exact IDs | Resolution and strict-C witness |
|---|---|
| `CPP-AE1613EEB65F`, `CPP-A634E297F83B`, `CPP-D64BC09621CC`, `CPP-9C62086987CC`, `CPP-1D8E6057AE30`, `CPP-5AB5145FFC46` | Native ModelEffectCollection cursor routes supply saved versions, cached Current, default/reset/exhaustion and release; ModelMeshSmoke passed in the focused owner run. The two separately tracked MoveNext/Reset semantic-gap IDs `CPP-27C2C4E19A90` and `CPP-0895ED3EBC19` are also implemented and exercised. |
| `CPP-65A7EF5BD183`, `CPP-053ABC01A2AC`, `CPP-9BADF5034E9E`, `CPP-38C968C51B1F`, `CPP-5A307E3EAFD7`, `CPP-2E664D30BB16`, `CPP-4178D51DF83A`, `CPP-6DBF2786215A` | Native GameComponentCollection cursors supply mutation versions (including empty Clear), typed/boxed Current and owned cached-component views surviving original-token/cursor destruction; RuntimeComponentsSmoke passed in the focused owner run. Shared collection-owned Add/Insert remain separate owner decisions. |
| `CPP-09107F5792D9` | Live mutable declaration view with native assignment/disposal, second-view and buffer readback, and final-view release guard; VertexBufferSmoke passed in the focused owner run. The former layout-copy-only partial proposal is superseded by the full resource view. |
| `CPP-A0B649C7CD0A` | Cube RenderTargetBinding constructor through CNA_RenderTargetBinding and set/copy-render-target routes; GraphicsSurfaceSmoke passed descriptor NEGATIVE_Z, zero slice and exact target-identity readback. New singleton implementation mapping added by Follow-up 1. |
| `CPP-B6C659DC3E4D` | cna_video_destroy on a real decoded Video, repeat destruction refusal, and stale-token width sentinel; VideoSmoke passed and explicitly printed `real frame identity: exercised against a decoded video`. New singleton implementation mapping added by Follow-up 1. |

The remaining SoundEffect stream and gamer/default/current semantic gaps from the original review
are implemented in ABI 0.59: AudioSoundEffectSmoke passed in the focused owner run; the gamer
regressions passed the lane's window-free suite. [CLOSURE_059.md](CLOSURE_059.md) contains the
full exact-ID table. These implementations supersede the original index-cursor partial proposals;
no partial approval is inferred.

## Retained 2026-11-03 owner execution

[owner-focus.log](/tmp/claude-1000/-rv-data-development-github-com-libcna-cna-tmp/8bc64a95-3ef2-4a9a-9163-86efed75e51e/scratchpad/codex/cabiclose-b/owner-focus.log) records six focused private SDLGPU suites passed,
zero failures and zero skips, including the positive decoded-video marker and cube descriptor
assertions. SHA-256: `c328b745d7292b1bd9c5aa3475b05b0b574f00fd779886586c339be4739c36ab`.

[owner-summary.txt](/tmp/claude-1000/-rv-data-development-github-com-libcna-cna-tmp/8bc64a95-3ef2-4a9a-9163-86efed75e51e/scratchpad/codex/cabiclose-b/owner-summary.txt) reports the full CApi_ suite at **176/176** in
cmake-build-sdlgpu and **174/174 each** under runtime OPENGLES3, OPENGL33, SDLGPU, WEBGPU,
FNA3D and SOFTWARE in cmake-build-multi. SHA-256: `cd97f88c352072ee935d097d06864463ff9b9ea73c83affa22ccb487aa4a5c24`.
These broader counts are the owner's retained summary; the focused verbose log was inspected
separately. No GPU/window test was launched by this lane.

The seven reserved owner-decision rows stay planned, as do the four Windows filesystem-path
qualification rows. Coverage, limitations and the release record are regenerated at **11 planned**;
the experimental verdict remains **Not ready**. No ABI, export, layout, exclusion, budget,
existing partial approval or no-C-form decision changes in Follow-up 1.
