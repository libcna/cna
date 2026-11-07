// SPDX-License-Identifier: MS-PL
#define INITGUID
#ifndef __reserved
#define __reserved
#endif
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <memory>
#define __in
#define __out
#undef __uuidof
#include "xact3.h"

static_assert(sizeof(XACT_WAVE_PROPERTIES)==84);
static void Require(const char* stage,HRESULT hr)
{
 printf("{\"stage\":\"%s\",\"hr\":\"%08lx\"}\n",stage,(unsigned long)hr);fflush(stdout);
 if(FAILED(hr))throw 2;
}
static void Owner(const char* name,void* object,unsigned slot,HMODULE module)
{
 HMODULE owner=NULL;wchar_t path[MAX_PATH]{};
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
  reinterpret_cast<LPCWSTR>((*reinterpret_cast<void***>(object))[slot]),&owner) || owner!=module)throw 2;
 if(!GetModuleFileNameW(owner,path,MAX_PATH))throw 2;
 printf("owner_%s=%ls\n",name,path);fflush(stdout);
}
struct Buffer
{
 void* bytes=nullptr;DWORD size=0;
 explicit Buffer(const std::wstring& path)
 {
  FILE* f=_wfopen(path.c_str(),L"rb");if(!f)throw 2;
  if(fseek(f,0,SEEK_END)){fclose(f);throw 2;}
  long n=ftell(f);if(n<=0){fclose(f);throw 2;}rewind(f);size=static_cast<DWORD>(n);
  bytes=_aligned_malloc(size,2048);if(!bytes){fclose(f);throw 2;}
  const bool ok=fread(bytes,1,size,f)==size;fclose(f);
  if(!ok){_aligned_free(bytes);bytes=nullptr;throw 2;}
 }
 ~Buffer(){_aligned_free(bytes);}
};
int wmain(int argc,wchar_t** argv)
{
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);if(argc!=3)return 2;
 HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 2;
 HMODULE module=nullptr;IClassFactory* factory=nullptr;IXACT3Engine* engine=nullptr;
 IXACT3WaveBank* waves=nullptr;IXACT3SoundBank* sounds=nullptr;IXACT3Cue* actors[4]{};
 IXACT3SoundBank* secondBank=nullptr;IXACT3Cue* secondBankCue=nullptr;
 const char* labels[]{"observer","prepared_later","second_playing","other_definition"};
 bool initialized=false;int rc=0;std::unique_ptr<Buffer> xgs,xwb,xsb;
 try
 {
  xgs=std::make_unique<Buffer>(std::wstring(argv[2])+L"/clock.xgs");
  xwb=std::make_unique<Buffer>(std::wstring(argv[2])+L"/clock.xwb");
  xsb=std::make_unique<Buffer>(std::wstring(argv[2])+L"/clock.xsb");
  module=LoadLibraryW(argv[1]);if(!module)throw 2;
  wchar_t loaded[MAX_PATH]{},expected[MAX_PATH]{};
  if(!GetModuleFileNameW(module,loaded,MAX_PATH)||!GetFullPathNameW(argv[1],MAX_PATH,expected,nullptr))throw 2;
  for(wchar_t* p=expected;*p;++p)if(*p==L'/')*p=L'\\';if(_wcsicmp(loaded,expected))throw 2;printf("module=%ls\n",loaded);
  using FactoryFn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto getFactory=reinterpret_cast<FactoryFn>(GetProcAddress(module,"DllGetClassObject"));if(!getFactory)throw 2;
  CLSID retail36{};Require("retail36_clsid",CLSIDFromString(L"{248d8a3b-6256-44d3-a018-2ac96c459f47}",&retail36));
  Require("factory",getFactory(retail36,IID_IClassFactory,reinterpret_cast<void**>(&factory)));
  Require("engine",factory->CreateInstance(nullptr,IID_IXACT3Engine,reinterpret_cast<void**>(&engine)));factory->Release();factory=nullptr;
  Owner("create_soundbank",engine,9,module);
  XACT_RUNTIME_PARAMETERS params={};params.lookAheadTime=XACT_ENGINE_LOOKAHEAD_DEFAULT;params.pGlobalSettingsBuffer=xgs->bytes;params.globalSettingsBufferSize=xgs->size;
  Require("initialize",engine->Initialize(&params));initialized=true;
  Require("wavebank",engine->CreateInMemoryWaveBank(xwb->bytes,xwb->size,0,0,&waves));Require("soundbank",engine->CreateSoundBank(xsb->bytes,xsb->size,0,0,&sounds));
  Owner("cue_properties",sounds,2,module);const auto index=sounds->GetCueIndex("ClockCue"),foreign=sounds->GetCueIndex("OtherCue");if(index==XACTINDEX_INVALID||foreign==XACTINDEX_INVALID)throw 2;
  auto tick=[&](unsigned ms){const auto end=GetTickCount64()+ms;do{const auto hr=engine->DoWork();if(FAILED(hr))Require("work",hr);Sleep(1);}while(GetTickCount64()<end);};
  auto snapshot=[&](const char* step)
  {
   XACT_CUE_PROPERTIES mainProps={},foreignProps={};const auto a=sounds->GetCueProperties(index,&mainProps),b=sounds->GetCueProperties(foreign,&foreignProps);
   printf("{\"snapshot\":\"%s\",\"properties_hr\":\"%08lx\",\"instances\":%u,\"other_properties_hr\":\"%08lx\",\"other_instances\":%u",step,(unsigned long)a,mainProps.currentInstances,(unsigned long)b,foreignProps.currentInstances);
   if(secondBank)
   {
    XACT_CUE_PROPERTIES props={};const auto ph=secondBank->GetCueProperties(index,&props);
    printf(",\"second_bank_properties_hr\":\"%08lx\",\"second_bank_instances\":%u",(unsigned long)ph,props.currentInstances);
    if(secondBankCue)
    {
     const auto var=secondBankCue->GetVariableIndex("NumCueInstances");float value=-1;const auto vh=var==XACTVARIABLEINDEX_INVALID?E_INVALIDARG:secondBankCue->GetVariable(var,&value);
     printf(",\"second_bank_variable_hr\":\"%08lx\",\"second_bank_variable\":%.9g",(unsigned long)vh,value);
    }
   }
   printf(",\"actors\":[");
   bool comma=false;for(unsigned i=0;i<4;++i)if(actors[i])
   {
    DWORD state=0;const auto sh=actors[i]->GetState(&state);const auto variable=actors[i]->GetVariableIndex("NumCueInstances");float value=-1;const auto vh=variable==XACTVARIABLEINDEX_INVALID?E_INVALIDARG:actors[i]->GetVariable(variable,&value);
    if(comma)printf(",");comma=true;printf("{\"name\":\"%s\",\"state_hr\":\"%08lx\",\"state\":%lu,\"variable_index\":%u,\"variable_hr\":\"%08lx\",\"variable\":%.9g}",labels[i],(unsigned long)sh,(unsigned long)state,variable,(unsigned long)vh,value);
   }
   printf("]}\n");fflush(stdout);
  };
  Require("prepare_observer",sounds->Prepare(index,0,0,&actors[0]));snapshot("initial_prepared");
  Require("play_observer",actors[0]->Play());tick(50);snapshot("one_playing");
  Require("prepare_later",sounds->Prepare(index,0,0,&actors[1]));snapshot("fresh_prepare");tick(150);snapshot("fresh_prepare_after150ms");
  Require("prepare_foreign",sounds->Prepare(foreign,0,0,&actors[3]));Require("play_foreign",actors[3]->Play());tick(50);snapshot("other_definition_playing");
  Require("second_bank",engine->CreateSoundBank(xsb->bytes,xsb->size,0,0,&secondBank));
  Require("second_bank_prepare",secondBank->Prepare(index,0,0,&secondBankCue));snapshot("second_bank_prepared");
  Require("second_bank_play",secondBankCue->Play());tick(100);snapshot("same_name_second_bank_playing");
  Require("pause_observer",actors[0]->Pause(TRUE));tick(30);snapshot("observer_paused");
  Require("prepare_second",sounds->Prepare(index,0,0,&actors[2]));Require("play_second",actors[2]->Play());tick(150);snapshot("second_playing_while_observer_paused");
  Require("stop_second",actors[2]->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));tick(150);snapshot("second_stopped_while_observer_paused");
  Require("destroy_second",actors[2]->Destroy());actors[2]=nullptr;tick(30);snapshot("second_destroyed");
  Require("destroy_prepared",actors[1]->Destroy());actors[1]=nullptr;tick(30);snapshot("prepared_destroyed");
  Require("resume_observer",actors[0]->Pause(FALSE));Require("stop_observer",actors[0]->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));tick(100);snapshot("observer_stopped");
  Require("prepare_replacement",sounds->Prepare(index,0,0,&actors[1]));Require("play_replacement",actors[1]->Play());tick(100);snapshot("new_play_after_observer_stopped");
  for(auto& p:actors)if(p){p->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE);p->Destroy();p=nullptr;}
  Require("second_bank_cue_destroy",secondBankCue->Destroy());secondBankCue=nullptr;
  Require("second_bank_destroy",secondBank->Destroy());secondBank=nullptr;
  Require("soundbank_destroy",sounds->Destroy());sounds=nullptr;Require("wavebank_destroy",waves->Destroy());waves=nullptr;Require("shutdown",engine->ShutDown());initialized=false;engine->Release();engine=nullptr;
 }
 catch(int error){rc=error;}
 for(auto* p:actors)if(p){p->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE);p->Destroy();}
 if(secondBankCue){secondBankCue->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE);secondBankCue->Destroy();}
 if(secondBank)secondBank->Destroy();
 if(sounds)sounds->Destroy();if(waves)waves->Destroy();if(engine){if(initialized)engine->ShutDown();engine->Release();}if(factory)factory->Release();if(module)FreeLibrary(module);CoUninitialize();return rc;
}
