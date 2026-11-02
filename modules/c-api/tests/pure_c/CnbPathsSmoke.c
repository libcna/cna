// SPDX-License-Identifier: MS-PL
#include <CNA/C/cna.h>
#include "CnaRuntimeSmoke.h"
#define SV(s) (CNA_StringView){s,sizeof(s)-1}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"paths:%d %s\n",__LINE__,#x); cna_smoke_error(); failed=1; goto cleanup; } } while (0)
#define EQ(a,b) CHECK(cna_smoke_operand(&smoke,#a,(uint64_t)(a))==cna_smoke_operand(&smoke,#b,(uint64_t)(b)))
#define OWN(call,h,destroy) EQ(cna_smoke_publish(&smoke,#call,(call),&(h),destroy,NULL,NULL),CNA_RESULT_SUCCESS)
int cna_test_cnb_write_path(const char*,const uint8_t*,uint64_t);
void cna_test_cnb_remove_path(const char*);
uint64_t cna_test_cnb_import_path(unsigned,const char*,uint8_t*,uint64_t);
static const unsigned char FixtureBmp[58] = {
    0x42U, 0x4DU, 0x3AU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x36U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U,
    0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U,
    0x00U, 0x00U, 0x01U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x13U, 0x0BU,
    0x00U, 0x00U, 0x13U, 0x0BU, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x1EU, 0x14U,
    0x0AU, 0x00U
};

static void push_u16(uint8_t* const data, size_t* const offset, const uint32_t value)
{
    data[(*offset)++] = (uint8_t)(value & UINT32_C(0xff));
    data[(*offset)++] = (uint8_t)((value >> 8U) & UINT32_C(0xff));
}

static void push_u32(uint8_t* const data, size_t* const offset, const uint32_t value)
{
    push_u16(data, offset, value & UINT32_C(0xffff));
    push_u16(data, offset, (value >> 16U) & UINT32_C(0xffff));
}

static void push_tag(uint8_t* const data, size_t* const offset, const char tag[4])
{
    for (size_t index = 0U; index < 4U; ++index) {
        data[(*offset)++] = (uint8_t)tag[index];
    }
}

static size_t build_minimal_cube_dds(uint8_t data[176])
{
    size_t offset = 0U;
    push_tag(data, &offset, "DDS ");
    push_u32(data, &offset, 124U);
    push_u32(data, &offset, UINT32_C(0x6));
    push_u32(data, &offset, 4U);
    push_u32(data, &offset, 4U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 1U);
    for (int index = 0; index < 11; ++index) { push_u32(data, &offset, 0U); }
    push_u32(data, &offset, 32U);
    push_u32(data, &offset, UINT32_C(0x4));
    push_tag(data, &offset, "DXT1");
    for (int index = 0; index < 5; ++index) { push_u32(data, &offset, 0U); }
    push_u32(data, &offset, UINT32_C(0x1000));
    push_u32(data, &offset, UINT32_C(0xfe00));
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    memset(data + offset, 0, 48U);
    return offset + 48U;
}

static size_t build_minimal_wav(uint8_t* const data, const uint16_t formatTag)
{
    size_t offset = 0U;
    push_tag(data, &offset, "RIFF");
    push_u32(data, &offset, 40U);
    push_tag(data, &offset, "WAVE");
    push_tag(data, &offset, "fmt ");
    push_u32(data, &offset, 16U);
    push_u16(data, &offset, formatTag);
    push_u16(data, &offset, 1U);
    push_u32(data, &offset, 8000U);
    push_u32(data, &offset, 16000U);
    push_u16(data, &offset, 2U);
    push_u16(data, &offset, 16U);
    push_tag(data, &offset, "data");
    push_u32(data, &offset, 4U);
    push_u16(data, &offset, UINT32_C(0x1234));
    push_u16(data, &offset, UINT32_C(0x5678));
    return offset;
}


int main(void)
{
    int failed=0;
    CnaRuntimeSmoke smoke={0};
    const char* paths[]={"cabi005_\xc5\xbe\xc5\x99\xc3\xad.bmp","cabi005_\xc5\xbe\xc5\x99\xc3\xad.dds","cabi005_\xc5\xbe\xc5\x99\xc3\xad.wav","cabi005_\xc5\xbe\xc5\x99\xc3\xad.cnj","cabi005_v.bin","cabi005_i.bin"};
    uint8_t dds[176],wav[48],vertex[96]={0},indices[6]={0,0,1,0,2,0};
    EQ(build_minimal_cube_dds(dds),sizeof(dds)); EQ(build_minimal_wav(wav,1),sizeof(wav));
    const char cnj[]="{\"cnjVersion\":1,\"type\":\"Model\",\"meshes\":[{\"name\":\"Hull\",\"vertices\":\"cabi005_v.bin\",\"indices\":\"cabi005_i.bin\",\"vertexStride\":32,\"effect\":\"BasicEffect\"}]}";
    CHECK(cna_test_cnb_write_path(paths[0],FixtureBmp,sizeof(FixtureBmp)));
    CHECK(cna_test_cnb_write_path(paths[1],dds,sizeof(dds)));
    CHECK(cna_test_cnb_write_path(paths[2],wav,sizeof(wav)));
    CHECK(cna_test_cnb_write_path(paths[3],(const uint8_t*)cnj,sizeof(cnj)-1));
    CHECK(cna_test_cnb_write_path(paths[4],vertex,sizeof(vertex)));
    CHECK(cna_test_cnb_write_path(paths[5],indices,sizeof(indices)));
    for (unsigned kind=0;kind<4;++kind) {
        CNA_StringView path={paths[kind],strlen(paths[kind])};
        uint8_t actual[32768],expected[32768]; uint64_t count=0;
        uint64_t native_count=cna_test_cnb_import_path(kind,paths[kind],expected,sizeof(expected)); CHECK(native_count>0 && native_count<=sizeof(expected));
        if (kind<2) {
            CNA_CnbTextureDataHandle texture=0;
            if (kind==0) { OWN(cna_cnb_import_image_as_texture2d(path,NULL,&texture),texture,cna_cnb_texture_data_destroy); }
            else { OWN(cna_cnb_import_dds_as_texture_cube(path,&texture),texture,cna_cnb_texture_data_destroy); }
            if (kind==0) EQ(cna_cnb_encode_texture2d(texture,SV("path"),actual,sizeof(actual),&count),CNA_RESULT_SUCCESS);
            else EQ(cna_cnb_encode_texture_cube(texture,SV("path"),actual,sizeof(actual),&count),CNA_RESULT_SUCCESS);
        } else if (kind==2) {
            CNA_CnbSoundEffectDataHandle sound=0;
            OWN(cna_cnb_import_wav_as_sound_effect(path,&sound),sound,cna_cnb_sound_effect_data_destroy);
            EQ(cna_cnb_encode_sound_effect(sound,SV("path"),actual,sizeof(actual),&count),CNA_RESULT_SUCCESS);
        } else {
            CNA_CnbModelFromCnjHandle result=0; CNA_CnbModelDataHandle model=0;
            OWN(cna_cnb_build_model_from_cnj(path,SV("."),&result),result,cna_cnb_model_from_cnj_destroy);
            OWN(cna_cnb_model_from_cnj_take_model(result,&model),model,cna_cnb_model_destroy);
            EQ(cna_cnb_encode_model(model,SV("path"),actual,sizeof(actual),&count),CNA_RESULT_SUCCESS);
        }
        EQ(count,native_count); CHECK(memcmp(actual,expected,(size_t)count)==0);
    }
    CNA_Handle rejected=99;
    EQ(cna_cnb_import_image_as_texture2d(SV("bad\0.bmp"),NULL,&rejected),CNA_RESULT_ENCODING); EQ(rejected,0);
    EQ(cna_cnb_import_dds_as_texture_cube(SV("\xff"),&rejected),CNA_RESULT_ENCODING); EQ(rejected,0);
    EQ(cna_cnb_import_wav_as_sound_effect(SV("bad\0.wav"),&rejected),CNA_RESULT_ENCODING); EQ(rejected,0);
    EQ(cna_cnb_build_model_from_cnj(SV("bad\0.cnj"),SV("."),&rejected),CNA_RESULT_ENCODING); EQ(rejected,0);
    puts("CnbPathsSmoke: four non-ASCII paths match native encoded bytes; invalid text refused PASS");
cleanup:
    if (failed) fputs(smoke.trace,stderr);
    for (unsigned i=0;i<6;++i) cna_test_cnb_remove_path(paths[i]);
    return cna_smoke_cleanup(&smoke) || failed;
}
