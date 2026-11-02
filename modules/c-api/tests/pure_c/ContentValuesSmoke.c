/* SPDX-License-Identifier: MS-PL */
#include "CnaRuntimeSmoke.h"
#include <stddef.h>
#define SV(s) ((CNA_StringView){s, sizeof(s)-1})
void cna_test_content_values_begin(void);
int cna_test_content_disposals(void);
int cna_test_content_worker(int (*test)(void));
int cna_test_content_native_shared(void);
int cna_test_content_native_scalar_existing(void);
int cna_test_content_native_stream(const uint8_t*, uint64_t);
int cna_test_content_native_inheritance(void);
int cna_test_content_native_value(const uint8_t*, uint64_t, unsigned, const void*, uint64_t);

typedef struct Bytes { uint8_t data[2048]; size_t size; } Bytes;
static void byte(Bytes* b, unsigned n) { b->data[b->size++] = (uint8_t)n; }
static void var(Bytes* b, unsigned n) { do { byte(b, (n & 127) | (n > 127 ? 128 : 0)); n >>= 7; } while (n); }
static void i32(Bytes* b, int32_t n) { for (unsigned i=0; i<4; ++i) byte(b, (uint32_t)n >> (i*8)); }
static void text(Bytes* b, const char* s) { size_t n=strlen(s); var(b, (unsigned)n); memcpy(b->data+b->size,s,n); b->size+=n; }
static CNA_StringView view(const char* s) { return (CNA_StringView){s, strlen(s)}; }
static void table(Bytes* b, const char* name, unsigned shared) { var(b,1); text(b,name); i32(b,0); var(b,shared); }
static void reflective_name(char* out, size_t capacity, const char* type) {
    (void)snprintf(out,capacity,"Microsoft.Xna.Framework.Content.ReflectiveReader`1[[%s]]",type);
}
static Bytes xnb(Bytes body) {
    Bytes b={{0},0}; byte(&b,'X'); byte(&b,'N'); byte(&b,'B'); byte(&b,'w'); byte(&b,5); byte(&b,0);
    i32(&b,(int32_t)(body.size+10)); memcpy(b.data+b.size,body.data,body.size); b.size+=body.size; return b;
}
typedef struct Source { Bytes bytes; unsigned calls; unsigned kind; CNA_Result failure; } Source;
static CNA_Result source(void* context, CNA_StringView name, CNA_StringView culture,
    uint32_t* kind, const uint8_t** data, uint64_t* count) {
    Source* s=(Source*)context; (void)name; (void)culture; ++s->calls;
    *kind=s->kind; *data=s->bytes.data; *count=s->bytes.size; return s->failure;
}
static CNA_Handle fixture_device;
static CNA_Result close_container(CNA_Handle handle) {
    CNA_Result r=cna_storage_container_destroy(handle);
    if(r==CNA_RESULT_SUCCESS) r=cna_storage_device_delete_container(fixture_device,SV("ContentValues"));
    return r;
}
static int fixture(CnaRuntimeSmoke* smoke, CNA_Handle* container) {
    CNA_Handle device=0;
    if(cna_storage_set_app_name_ext(SV("cna-content-values"))!=CNA_RESULT_SUCCESS) return 0;
    CNA_Result r=cna_storage_device_show_selector(0,0,&device);
    cna_smoke_track(smoke,&device,cna_storage_device_destroy);
    if(r!=CNA_RESULT_SUCCESS) return 0;
    fixture_device=device;
    r=cna_storage_device_delete_container(device,SV("ContentValues"));
    if(r!=CNA_RESULT_SUCCESS) return 0;
    r=cna_storage_container_open(device,SV("ContentValues"),0,0,container);
    cna_smoke_track(smoke,container,close_container);
    return r==CNA_RESULT_SUCCESS;
}
static int make_reader(CnaRuntimeSmoke* smoke, CNA_Handle container, CNA_Handle manager,
    const Bytes* bytes, CNA_Handle* reader) {
    static unsigned serial=0;
    char filename[32]; (void)snprintf(filename,sizeof(filename),"reader-%u.bin",++serial);
    CNA_Handle stream=0; int64_t position=99;
    CNA_Result r=cna_storage_container_create_file(container,view(filename),&stream);
    cna_smoke_track(smoke,&stream,cna_storage_stream_close);
    if(r!=CNA_RESULT_SUCCESS || cna_storage_stream_write(stream,bytes->data,bytes->size)!=CNA_RESULT_SUCCESS ||
        cna_storage_stream_seek(stream,0,CNA_SEEK_ORIGIN_BEGIN,&position)!=CNA_RESULT_SUCCESS) return 0;
    CNA_ContentReaderCreateInfo info={sizeof(info),1,manager,stream,{"dir/root",8},5,'w',{0,0,0}};
    r=cna_content_reader_create(&info,reader); cna_smoke_track(smoke,reader,cna_content_reader_destroy);
    return r==CNA_RESULT_SUCCESS;
}
static void* pointer_value(CNA_Handle handle) {
    void* p=NULL; uint64_t size=0;
    if(cna_content_value_copy_data_ext(handle,&p,sizeof(p),&size)!=CNA_RESULT_SUCCESS || size!=sizeof(p)) return NULL;
    return p;
}
typedef struct Node { int32_t number; struct Node* next; struct Node* alias; } Node;
typedef struct Objects { Node nodes[8]; unsigned created; unsigned retain; unsigned release; unsigned assigned; int fail_create; int fail_assign; } Objects;
static CNA_Result create_node(void* context, void** object) {
    Objects* objects=(Objects*)context;
    if(objects->fail_create || objects->created==8) return CNA_RESULT_INVALID_STATE;
    *object=&objects->nodes[objects->created++]; return CNA_RESULT_SUCCESS;
}
static void retain_node(void* context, void* object) { Objects* o=(Objects*)context; (void)object; ++o->retain; }
static void release_node(void* context, void* object) { Objects* o=(Objects*)context; (void)object; ++o->release; }
static CNA_Result assign_next(void* context, void* object, void* resource) {
    Objects* o=(Objects*)context; Node* n=(Node*)object;
    if(o->fail_assign) return CNA_RESULT_INVALID_STATE;
    n->next=(Node*)resource; ++o->assigned; return CNA_RESULT_SUCCESS;
}
static CNA_Result assign_alias(void* context, void* object, void* resource) {
    Objects* o=(Objects*)context; ((Node*)object)->alias=(Node*)resource; ++o->assigned; return CNA_RESULT_SUCCESS;
}
static int shared_test(void) {
    CnaRuntimeSmoke smoke={0}; int status=1; CNA_Handle builder=0,reader=0,value=0,container=0;
    Objects objects={0}; char name[128]; Bytes b={{0},0};
    RUNTIME_CHECK(cna_test_content_native_shared());
    RUNTIME_CHECK(fixture(&smoke,&container));
    RUNTIME_CHECK(SMOKE_OWN(cna_reflective_type_reader_builder_create(SV("Lane.Node"),create_node,&objects,&builder),&builder,cna_reflective_type_reader_builder_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_field(builder,CNA_CONTENT_FIELD_INT32,offsetof(Node,number)))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_shared_field_ext(builder,SV("Lane.Node"),assign_next,retain_node,release_node,&objects))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_shared_field_ext(builder,SV("Lane.Node"),assign_alias,retain_node,release_node,&objects))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_shared_field_ext(builder,SV("Lane.Node"),NULL,retain_node,release_node,&objects))==CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_register_shared(builder))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_reflective_type_reader_builder_destroy(builder),builder)==CNA_RESULT_SUCCESS);
    reflective_name(name,sizeof(name),"Lane.Node"); table(&b,name,1); var(&b,1); i32(&b,11); var(&b,1); var(&b,1);
    var(&b,1); i32(&b,22); var(&b,1); var(&b,0);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    Node* root=(Node*)pointer_value(value);
    RUNTIME_CHECK(root && SMOKE_VALUE(root->number)==11 && root->next && SMOKE_VALUE(root->next->number)==22 && root->next==root->alias && root->next->next==root->next);
    RUNTIME_CHECK(SMOKE_VALUE(objects.retain)==3 && SMOKE_VALUE(objects.release)==3 && SMOKE_VALUE(objects.assigned)==3);
    RUNTIME_CHECK(SMOKE_DROP(cna_content_reader_destroy(reader),reader)==CNA_RESULT_SUCCESS);
    /* Initialize then invoke directly: prove fixups remain deferred and cancel on teardown. */
    objects=(Objects){0}; b=(Bytes){{0},0}; table(&b,name,1); i32(&b,33); var(&b,1); var(&b,0);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    CNA_Handle type=0;
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_reader(view(name),&type),&type,cna_content_type_reader_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_reader_initialize_type_readers(reader))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_read_value_ext(type,reader,0,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(objects.retain)==1 && SMOKE_VALUE(objects.release)==0 && SMOKE_VALUE(objects.assigned)==0);
    RUNTIME_CHECK(SMOKE_DROP(cna_content_reader_destroy(reader),reader)==CNA_RESULT_SUCCESS && SMOKE_VALUE(objects.release)==1);
    /* Out-of-range shared index fails before any pin is acquired. */
    objects=(Objects){0}; b=(Bytes){{0},0}; table(&b,name,1); var(&b,1); i32(&b,0); var(&b,2); var(&b,0);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader)); value=99;
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(value)==0 && SMOKE_VALUE(objects.retain)==0);
    /* Wrong native shared type refuses assignment and releases its pin at reader teardown. */
    objects=(Objects){0}; b=(Bytes){{0},0}; var(&b,2); text(&b,name); i32(&b,0); text(&b,"Microsoft.Xna.Framework.Content.Int32Reader"); i32(&b,0);
    var(&b,1); var(&b,1); i32(&b,0); var(&b,1); var(&b,0); var(&b,2); i32(&b,7);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(objects.assigned)==0);
    RUNTIME_CHECK(SMOKE_DROP(cna_content_reader_destroy(reader),reader)==CNA_RESULT_SUCCESS && SMOKE_VALUE(objects.retain)==SMOKE_VALUE(objects.release));
    /* A validated reference whose C assignment fails still releases its lifetime pin. */
    objects=(Objects){0}; objects.fail_assign=1; b=(Bytes){{0},0}; table(&b,name,1);
    var(&b,1); i32(&b,0); var(&b,1); var(&b,0); var(&b,1); i32(&b,0); var(&b,0); var(&b,0);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(objects.release)==1);
    objects=(Objects){0};
    RUNTIME_CHECK(SMOKE_OWN(cna_reflective_type_reader_builder_create(SV("Lane.BadTarget"),create_node,&objects,&builder),&builder,cna_reflective_type_reader_builder_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_shared_field_ext(builder,SV("Other.Type"),assign_next,retain_node,release_node,&objects))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_register_shared(builder))==CNA_RESULT_SUCCESS);
    char bad_name[128]; reflective_name(bad_name,sizeof(bad_name),"Lane.BadTarget");
    b=(Bytes){{0},0}; var(&b,2); text(&b,bad_name); i32(&b,0); text(&b,name); i32(&b,0); var(&b,1);
    var(&b,1); var(&b,1); var(&b,2); i32(&b,1); var(&b,0); var(&b,0);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(objects.assigned)==0 && SMOKE_VALUE(objects.release)==1);
    status=0;
cleanup:
    status |= cna_smoke_cleanup(&smoke);
    if(cna_content_type_reader_manager_clear_type_creators()!=CNA_RESULT_SUCCESS) return 1;
    return status;
}

typedef struct Derived { int32_t padding; int32_t base; int32_t derived; } Derived;
static CNA_Result create_derived(void* context, void** object) { *object=context; return CNA_RESULT_SUCCESS; }
static int inheritance_test(void) {
    CnaRuntimeSmoke smoke={0}; int status=1; CNA_Handle base=0,derived=0,type=0,reader=0,value=0,container=0;
    Derived object={-1,0,0}; Bytes b={{0},0}; char base_name[128],derived_name[128];
    RUNTIME_CHECK(cna_test_content_native_inheritance());
    RUNTIME_CHECK(fixture(&smoke,&container));
    RUNTIME_CHECK(SMOKE_OWN(cna_reflective_type_reader_builder_create(SV("Lane.Base"),create_derived,&object,&base),&base,cna_reflective_type_reader_builder_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_field(base,CNA_CONTENT_FIELD_INT32,0))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_register_abstract_ext(base))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_reflective_type_reader_builder_create(SV("Lane.Derived"),create_derived,&object,&derived),&derived,cna_reflective_type_reader_builder_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_base_ext(derived,derived,0))==CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_base_ext(derived,base,UINT64_MAX))==CNA_RESULT_OVERFLOW);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_base_ext(derived,base,offsetof(Derived,base)))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_base_ext(derived,base,0))==CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_field(derived,CNA_CONTENT_FIELD_INT32,offsetof(Derived,derived)))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_register_shared(derived))==CNA_RESULT_SUCCESS);
    /* Changing and destroying the base cannot affect the registered derived snapshot. */
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_field(base,CNA_CONTENT_FIELD_INT32,4))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_reflective_type_reader_builder_destroy(base),base)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_reflective_type_reader_builder_destroy(derived),derived)==CNA_RESULT_SUCCESS);
    reflective_name(base_name,sizeof(base_name),"Lane.Base"); reflective_name(derived_name,sizeof(derived_name),"Lane.Derived");
    var(&b,2); text(&b,base_name); i32(&b,0); text(&b,derived_name); i32(&b,0); var(&b,0); var(&b,2); i32(&b,41); i32(&b,42);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(pointer_value(value)==&object && SMOKE_VALUE(object.base)==41 && SMOKE_VALUE(object.derived)==42 && object.padding==-1);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_reader(view(base_name),&type),&type,cna_content_type_reader_destroy)==CNA_RESULT_SUCCESS);
    CNA_Handle failure=99;
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_read_value_ext(type,reader,value,&failure),&failure,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(failure)==0 && SMOKE_VALUE(object.base)==41);
    uint64_t canonical_size=0; char canonical[128];
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_get_canonical_name_size(SV("Lane.Base"),&canonical_size))==CNA_RESULT_SUCCESS && SMOKE_VALUE(canonical_size)==strlen(base_name));
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_copy_canonical_name(SV("Lane.Base"),canonical,sizeof(canonical),&canonical_size))==CNA_RESULT_SUCCESS && memcmp(canonical,base_name,(size_t)canonical_size)==0);
    CNA_Handle derived_type=0, updated=0;
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_reader(view(derived_name),&derived_type),&derived_type,cna_content_type_reader_destroy)==CNA_RESULT_SUCCESS);
    b=(Bytes){{0},0}; i32(&b,71); i32(&b,72); RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_read_value_ext(derived_type,reader,value,&updated),&updated,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS && pointer_value(updated)==&object && SMOKE_VALUE(object.base)==71 && SMOKE_VALUE(object.derived)==72);
    /* The abstract entry also refuses direct root construction. */
    b=(Bytes){{0},0}; table(&b,base_name,0); var(&b,1);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&failure),&failure,cna_content_value_destroy_ext)==CNA_RESULT_IO);
    Objects refused={0}; refused.fail_create=1;
    RUNTIME_CHECK(SMOKE_OWN(cna_reflective_type_reader_builder_create(SV("Lane.Refused"),create_node,&refused,&derived),&derived,cna_reflective_type_reader_builder_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_register(derived))==CNA_RESULT_SUCCESS);
    reflective_name(derived_name,sizeof(derived_name),"Lane.Refused"); b=(Bytes){{0},0}; table(&b,derived_name,0); var(&b,1);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&failure),&failure,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(refused.created)==0);
    status=0;
cleanup:
    status |= cna_smoke_cleanup(&smoke);
    if(cna_content_type_reader_manager_clear_type_creators()!=CNA_RESULT_SUCCESS) return 1;
    return status;
}

static int erased_test(void) {
    CnaRuntimeSmoke smoke={0}; int status=1; CNA_Handle reader=0,value=0,container=0,manager=0;
    Bytes b={{0},0}; int32_t n=42,actual=0; uint64_t size=99; char buffer[16]; CNA_ContentValueKind kind=99;
    RUNTIME_CHECK(fixture(&smoke,&container));
    table(&b,"Microsoft.Xna.Framework.Content.Int32Reader",0); var(&b,1); i32(&b,n);
    RUNTIME_CHECK(cna_test_content_native_value(b.data,b.size,CNA_CONTENT_VALUE_INT32,&n,sizeof(n)));
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_get_kind_ext(value,&kind))==CNA_RESULT_SUCCESS && SMOKE_VALUE(kind)==CNA_CONTENT_VALUE_INT32);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_data_ext(value,&actual,sizeof(actual),&size))==CNA_RESULT_SUCCESS && SMOKE_VALUE(actual)==42 && SMOKE_VALUE(size)==4);
    actual=77;
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_data_ext(value,&actual,1,&size))==CNA_RESULT_BUFFER_TOO_SMALL && SMOKE_VALUE(actual)==77 && SMOKE_VALUE(size)==4);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_string_ext(value,buffer,sizeof(buffer),&size))==CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_dispose_ext(value))==CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_get_kind_ext(0,&kind))==CNA_RESULT_INVALID_HANDLE && SMOKE_VALUE(kind)==CNA_CONTENT_VALUE_INT32);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_get_kind_ext(0,NULL))==CNA_RESULT_INVALID_ARGUMENT);
    b=(Bytes){{0},0}; table(&b,"Microsoft.Xna.Framework.Content.StringReader",0); var(&b,1); text(&b,"hello");
    RUNTIME_CHECK(cna_test_content_native_value(b.data,b.size,CNA_CONTENT_VALUE_STRING,"hello",5));
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    memset(buffer,'!',sizeof(buffer));
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_string_ext(value,buffer,1,&size))==CNA_RESULT_BUFFER_TOO_SMALL && buffer[0]=='!' && SMOKE_VALUE(size)==5);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_string_ext(value,buffer,sizeof(buffer),&size))==CNA_RESULT_SUCCESS && memcmp(buffer,"hello",5)==0 && buffer[5]=='!');
    Source s={xnb(b),0,2,CNA_RESULT_SUCCESS};
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_create_bytes_ext(source,&s,CNA_FALSE,&manager),&manager,cna_content_manager_destroy)==CNA_RESULT_SUCCESS);
    b=(Bytes){{0},0}; text(&b,"../child");
    RUNTIME_CHECK(make_reader(&smoke,container,manager,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_external_reference_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_string_ext(value,buffer,sizeof(buffer),&size))==CNA_RESULT_SUCCESS && memcmp(buffer,"hello",5)==0 && SMOKE_VALUE(s.calls)==1);
    b=(Bytes){{0},0}; text(&b,""); RUNTIME_CHECK(make_reader(&smoke,container,manager,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_external_reference_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_get_kind_ext(value,&kind))==CNA_RESULT_SUCCESS && SMOKE_VALUE(kind)==CNA_CONTENT_VALUE_EMPTY && SMOKE_VALUE(s.calls)==1);
    b=(Bytes){{0},0}; text(&b,"../../escape"); RUNTIME_CHECK(make_reader(&smoke,container,manager,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_external_reference_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(value)==0);
    b=(Bytes){{0},0}; table(&b,"Microsoft.Xna.Framework.Content.Int32Reader",0); var(&b,2);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(value)==0);
    b=(Bytes){{0},0}; var(&b,0); var(&b,0); var(&b,0);
    RUNTIME_CHECK(cna_test_content_native_value(b.data,b.size,CNA_CONTENT_VALUE_EMPTY,NULL,0));
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_data_ext(value,NULL,0,&size))==CNA_RESULT_SUCCESS && SMOKE_VALUE(size)==0);
    RUNTIME_CHECK(SMOKE_DROP(cna_content_value_destroy_ext(value),value)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_destroy_ext(value))==CNA_RESULT_INVALID_HANDLE);
    /* Distinct scalar tags preserve their exact fixed C representations. */
    const char* scalar_readers[]={"Microsoft.Xna.Framework.Content.BooleanReader", "Microsoft.Xna.Framework.Content.SingleReader", "Microsoft.Xna.Framework.Content.DoubleReader"};
    const CNA_ContentValueKind scalar_kinds[]={CNA_CONTENT_VALUE_BOOLEAN,CNA_CONTENT_VALUE_SINGLE,CNA_CONTENT_VALUE_DOUBLE};
    for(unsigned i=0;i<3;++i) {
        b=(Bytes){{0},0}; table(&b,scalar_readers[i],0); var(&b,1);
        if(i==0) byte(&b,1);
        if(i==1) i32(&b,0x3f800000);
        if(i==2) { i32(&b,0); i32(&b,0x3ff00000); }
        RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
        RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_content_value_get_kind_ext(value,&kind))==CNA_RESULT_SUCCESS && SMOKE_VALUE(kind)==scalar_kinds[i]);
        if(i==0) { CNA_Bool boolean=99; RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_data_ext(value,&boolean,sizeof(boolean),&size))==CNA_RESULT_SUCCESS && SMOKE_VALUE(boolean)==CNA_TRUE); }
        if(i==1) { float single=0; RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_data_ext(value,&single,sizeof(single),&size))==CNA_RESULT_SUCCESS && single==1.0f); }
        if(i==2) { double real=0; RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_data_ext(value,&real,sizeof(real),&size))==CNA_RESULT_SUCCESS && real==1.0); }
    }
    /* A successfully read native type outside the registry is refused explicitly. */
    b=(Bytes){{0},0}; table(&b,"Microsoft.Xna.Framework.Content.Vector2Reader",0); var(&b,1); i32(&b,0); i32(&b,0);
    RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_asset_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_NOT_SUPPORTED && SMOKE_VALUE(value)==0);
    status=0;
cleanup:
    status |= cna_smoke_cleanup(&smoke);
    if(cna_content_type_reader_manager_clear_type_creators()!=CNA_RESULT_SUCCESS) return 1;
    return status;
}

static CNA_Handle thread_value;
static int wrong_thread(void) { CNA_ContentValueKind kind=99; return cna_content_value_get_kind_ext(thread_value,&kind)==CNA_RESULT_THREAD && kind==99 && cna_content_value_destroy_ext(thread_value)==CNA_RESULT_THREAD; }
static int target_test(void) {
    CnaRuntimeSmoke smoke={0}; int status=1; CNA_Handle container=0,reader=0,value=0,existing=0,type=0,builder=0;
    Bytes b={{0},0}; int32_t number=0; uint64_t size=0; Objects objects={0}; char name[128];
    RUNTIME_CHECK(fixture(&smoke,&container));
    RUNTIME_CHECK(SMOKE_API(cna_content_type_reader_manager_associate_target_ext(CNA_CONTENT_VALUE_INT32,SV("No.Factory")))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_target_reader_ext(CNA_CONTENT_VALUE_INT32,&type),&type,cna_content_type_reader_destroy)==CNA_RESULT_NOT_SUPPORTED);
    RUNTIME_CHECK(SMOKE_API(cna_content_type_reader_manager_associate_target_ext(CNA_CONTENT_VALUE_INT32,SV("Microsoft.Xna.Framework.Content.Int32Reader")))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_target_reader_ext(CNA_CONTENT_VALUE_INT32,&type),&type,cna_content_type_reader_destroy)==CNA_RESULT_SUCCESS);
    i32(&b,123); i32(&b,456); RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_raw_value_ext(reader,CNA_CONTENT_VALUE_INT32,0,&existing),&existing,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_data_ext(existing,&number,sizeof(number),&size))==CNA_RESULT_SUCCESS && SMOKE_VALUE(number)==123);
    RUNTIME_CHECK(cna_test_content_native_scalar_existing());
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_raw_value_ext(reader,CNA_CONTENT_VALUE_INT32,existing,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_INVALID_STATE && SMOKE_VALUE(value)==0);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_data_ext(existing,&number,sizeof(number),&size))==CNA_RESULT_SUCCESS && SMOKE_VALUE(number)==123);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_raw_value_ext(reader,CNA_CONTENT_VALUE_STRING,existing,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_INVALID_ARGUMENT);
    thread_value=existing; RUNTIME_CHECK(cna_test_content_worker(wrong_thread));
    RUNTIME_CHECK(SMOKE_OWN(cna_reflective_type_reader_builder_create(SV("Lane.Mutable"),create_node,&objects,&builder),&builder,cna_reflective_type_reader_builder_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_add_field(builder,CNA_CONTENT_FIELD_INT32,offsetof(Node,number)))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_register(builder))==CNA_RESULT_SUCCESS);
    reflective_name(name,sizeof(name),"Lane.Mutable");
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_reader(view(name),&type),&type,cna_content_type_reader_destroy)==CNA_RESULT_SUCCESS);
    /* A target name is associated by successful creation, independently from the factory key. */
    RUNTIME_CHECK(SMOKE_API(cna_content_type_reader_manager_associate_target_ext(CNA_CONTENT_VALUE_FOREIGN_OBJECT,SV("Lane.Mutable")))==CNA_RESULT_SUCCESS);
    b=(Bytes){{0},0}; i32(&b,10); i32(&b,20); i32(&b,30); RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_raw_value_ext(reader,CNA_CONTENT_VALUE_FOREIGN_OBJECT,0,&existing),&existing,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_read_value_ext(type,reader,existing,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(pointer_value(existing)==pointer_value(value) && SMOKE_VALUE(objects.created)==1 && SMOKE_VALUE(objects.nodes[0].number)==20);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_raw_value_ext(reader,CNA_CONTENT_VALUE_FOREIGN_OBJECT,existing,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS && SMOKE_VALUE(objects.nodes[0].number)==30);
    RUNTIME_CHECK(SMOKE_API(cna_content_type_reader_manager_associate_target_ext(99,SV("x")))==CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_content_type_reader_manager_associate_target_ext(CNA_CONTENT_VALUE_INT32,SV("")))==CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_content_type_reader_manager_clear_type_creators())==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_target_reader_ext(CNA_CONTENT_VALUE_FOREIGN_OBJECT,&type),&type,cna_content_type_reader_destroy)==CNA_RESULT_NOT_SUPPORTED);
    RUNTIME_CHECK(SMOKE_API(cna_reflective_type_reader_builder_register(builder))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_reader(view(name),&type),&type,cna_content_type_reader_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_target_reader_ext(CNA_CONTENT_VALUE_FOREIGN_OBJECT,&type),&type,cna_content_type_reader_destroy)==CNA_RESULT_SUCCESS);
    b=(Bytes){{0},0}; i32(&b,99); RUNTIME_CHECK(make_reader(&smoke,container,0,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_raw_value_ext(reader,CNA_CONTENT_VALUE_FOREIGN_OBJECT,0,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS && SMOKE_VALUE(objects.created)==2 && SMOKE_VALUE(objects.nodes[1].number)==99);
    CNA_Handle int_type=0;
    cna_test_content_values_begin();
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_manager_create_reader(SV("Microsoft.Xna.Framework.Content.Int32Reader"),&int_type),&int_type,cna_content_type_reader_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_type_reader_read_value_ext(int_type,reader,value,&existing),&existing,cna_content_value_destroy_ext)==CNA_RESULT_INVALID_ARGUMENT && SMOKE_VALUE(existing)==0);
    status=0;
cleanup:
    status |= cna_smoke_cleanup(&smoke);
    if(cna_content_type_reader_manager_clear_type_creators()!=CNA_RESULT_SUCCESS) return 1;
    return status;
}

static CNA_ContentValueHandle adopted;
static int reject_recorder;
static CNA_Result record(void* context,CNA_ContentValueHandle value) { (void)context; if(reject_recorder) return CNA_RESULT_INVALID_STATE; adopted=value; return CNA_RESULT_SUCCESS; }
static int stream_test(void) {
    CnaRuntimeSmoke smoke={0}; int status=1; CNA_Handle manager=0,value=0,container=0,reader=0;
    Bytes b={{0},0}; table(&b,"Microsoft.Xna.Framework.Content.StringReader",0); var(&b,1); text(&b,"cached");
    Source s={xnb(b),0,2,CNA_RESULT_SUCCESS}; char buffer[16]; uint64_t size=0;
    RUNTIME_CHECK(cna_test_content_native_stream(s.bytes.data,s.bytes.size));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_create_bytes_ext(source,&s,CNA_FALSE,&manager),&manager,cna_content_manager_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_manager_set_stream_routing_ext(manager,CNA_FALSE))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_manager_set_stream_routing_ext(manager,CNA_TRUE))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_manager_set_stream_routing_ext(manager,99))==CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_load_value_ext(manager,SV("dir/asset"),CNA_CONTENT_VALUE_STRING,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS && SMOKE_VALUE(s.calls)==1);
    s.failure=CNA_RESULT_INVALID_STATE;
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_load_value_ext(manager,SV("dir/asset"),CNA_CONTENT_VALUE_STRING,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS && SMOKE_VALUE(s.calls)==1);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_load_value_ext(manager,SV("dir/asset"),CNA_CONTENT_VALUE_INT32,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(value)==0 && SMOKE_VALUE(s.calls)==1);
    RUNTIME_CHECK(fixture(&smoke,&container));
    b=(Bytes){{0},0}; text(&b,"asset"); RUNTIME_CHECK(make_reader(&smoke,container,manager,&b,&reader));
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_external_reference_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS && SMOKE_VALUE(s.calls)==1);
    RUNTIME_CHECK(SMOKE_API(cna_content_manager_unload(manager))==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_load_value_ext(manager,SV("dir/asset"),CNA_CONTENT_VALUE_STRING,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(s.calls)==2);
    s.failure=CNA_RESULT_SUCCESS;
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_read_value_ext(manager,SV("fresh"),CNA_CONTENT_VALUE_STRING,NULL,NULL,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS && SMOKE_VALUE(s.calls)==3);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_read_value_ext(manager,SV("fresh"),CNA_CONTENT_VALUE_STRING,NULL,NULL,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS && SMOKE_VALUE(s.calls)==4);
    /* A retained reader keeps the disposed manager memory alive; it cannot load after destroy. */
    b=(Bytes){{0},0}; text(&b,"asset"); RUNTIME_CHECK(make_reader(&smoke,container,manager,&b,&reader));
    RUNTIME_CHECK(SMOKE_DROP(cna_content_manager_destroy(manager),manager)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_reader_read_external_reference_value_ext(reader,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_INVALID_STATE && SMOKE_VALUE(value)==0);
    /* ResourceContentManager performs its own missing/nonbinary validation, no file fallback. */
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_create_bytes_ext(source,&s,CNA_TRUE,&manager),&manager,cna_content_manager_destroy)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_load_value_ext(manager,SV("embedded"),CNA_CONTENT_VALUE_STRING,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_copy_string_ext(value,buffer,sizeof(buffer),&size))==CNA_RESULT_SUCCESS && memcmp(buffer,"cached",6)==0);
    s.kind=0;
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_load_value_ext(manager,SV("missing"),CNA_CONTENT_VALUE_STRING,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO);
    s.kind=1;
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_load_value_ext(manager,SV("nonbinary"),CNA_CONTENT_VALUE_STRING,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO);
    s.kind=2;
    /* Native nested IDisposable production exercises adoption independently of foreign pointees. */
    b=(Bytes){{0},0}; var(&b,2); text(&b,"Lane.RecordRootReader"); i32(&b,0); text(&b,"Lane.DisposableReader"); i32(&b,0); var(&b,0); var(&b,1); var(&b,2); s.bytes=xnb(b);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_read_value_ext(manager,SV("record"),CNA_CONTENT_VALUE_FOREIGN_OBJECT,record,NULL,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS && SMOKE_VALUE(adopted)!=0);
    cna_smoke_track(&smoke,&adopted,cna_content_value_destroy_ext);
    CNA_ContentValueKind adopted_kind=99;
    RUNTIME_CHECK(SMOKE_API(cna_content_value_get_kind_ext(adopted,&adopted_kind))==CNA_RESULT_SUCCESS && SMOKE_VALUE(adopted_kind)==CNA_CONTENT_VALUE_DISPOSABLE);
    RUNTIME_CHECK(SMOKE_VALUE(cna_test_content_disposals())==0);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_dispose_ext(adopted))==CNA_RESULT_SUCCESS && SMOKE_VALUE(cna_test_content_disposals())==1);
    RUNTIME_CHECK(SMOKE_API(cna_content_value_dispose_ext(adopted))==CNA_RESULT_SUCCESS && SMOKE_VALUE(cna_test_content_disposals())==1);
    reject_recorder=1;
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_read_value_ext(manager,SV("record"),CNA_CONTENT_VALUE_FOREIGN_OBJECT,record,NULL,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_IO && SMOKE_VALUE(value)==0);
    RUNTIME_CHECK(SMOKE_OWN(cna_content_manager_read_value_ext(manager,SV("record"),CNA_CONTENT_VALUE_FOREIGN_OBJECT,NULL,NULL,&value),&value,cna_content_value_destroy_ext)==CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_content_manager_unload(manager))==CNA_RESULT_SUCCESS && SMOKE_VALUE(cna_test_content_disposals())==2);
    status=0;
cleanup:
    status |= cna_smoke_cleanup(&smoke);
    if(cna_content_type_reader_manager_clear_type_creators()!=CNA_RESULT_SUCCESS) return 1;
    return status;
}
int main(int argc,char** argv) {
    cna_test_content_values_begin();
    int result=1;
    if(argc==2) {
        if(strcmp(argv[1],"shared")==0) result=shared_test();
        if(strcmp(argv[1],"inheritance")==0) result=inheritance_test();
        if(strcmp(argv[1],"erased")==0) result=erased_test();
        if(strcmp(argv[1],"target")==0) result=target_test();
        if(strcmp(argv[1],"stream")==0) result=stream_test();
    }
    if(cna_content_type_reader_manager_clear_type_creators()!=CNA_RESULT_SUCCESS) result=1;
    if(result==0) printf("content-%s: PASS\n",argv[1]);
    return result;
}
