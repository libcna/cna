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
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if(argc!=4)return 2;
 const std::wstring mode=argv[3];
 if(mode!=L"none" && mode!=L"before" && mode!=L"after" && mode!=L"destroyed" && mode!=L"stopping" && mode!=L"paused" && mode!=L"no-wave" && mode!=L"evicted-only" && mode!=L"replay-order" && mode!=L"reverse-play")return 2;
 HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 2;
 HMODULE module=nullptr;IClassFactory* factory=nullptr;IXACT3Engine* engine=nullptr;
 IXACT3WaveBank* waves=nullptr;IXACT3SoundBank* sounds=nullptr;
 IXACT3Cue* actors[4]{};const char* labels[]{"P","L","I","J"};
 bool initialized=false;int rc=0;std::unique_ptr<Buffer> xgs,xwb,xsb;
 try
 {
  xgs=std::make_unique<Buffer>(std::wstring(argv[2])+L"/clock.xgs");
  xwb=std::make_unique<Buffer>(std::wstring(argv[2])+L"/clock.xwb");
  xsb=std::make_unique<Buffer>(std::wstring(argv[2])+L"/clock.xsb");
  module=LoadLibraryW(argv[1]);if(!module)throw 2;
  wchar_t loaded[MAX_PATH]{},expected[MAX_PATH]{};
  if(!GetModuleFileNameW(module,loaded,MAX_PATH)||!GetFullPathNameW(argv[1],MAX_PATH,expected,nullptr))throw 2;
  for(wchar_t* p=expected;*p;++p)if(*p==L'/')*p=L'\\';
  if(_wcsicmp(loaded,expected))throw 2;
  printf("module=%ls\n",loaded);
  using FactoryFn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
  auto getFactory=reinterpret_cast<FactoryFn>(GetProcAddress(module,"DllGetClassObject"));if(!getFactory)throw 2;
  CLSID retail36{};Require("retail36_clsid",CLSIDFromString(L"{248d8a3b-6256-44d3-a018-2ac96c459f47}",&retail36));
  Require("factory",getFactory(retail36,IID_IClassFactory,reinterpret_cast<void**>(&factory)));
  Require("engine",factory->CreateInstance(nullptr,IID_IXACT3Engine,reinterpret_cast<void**>(&engine)));
  factory->Release();factory=nullptr;Owner("create_soundbank",engine,9,module);
  XACT_RUNTIME_PARAMETERS params={};params.lookAheadTime=XACT_ENGINE_LOOKAHEAD_DEFAULT;
  params.pGlobalSettingsBuffer=xgs->bytes;params.globalSettingsBufferSize=xgs->size;
  Require("initialize",engine->Initialize(&params));initialized=true;
  if(mode!=L"no-wave")Require("wavebank",engine->CreateInMemoryWaveBank(xwb->bytes,xwb->size,0,0,&waves));
  Require("soundbank",engine->CreateSoundBank(xsb->bytes,xsb->size,0,0,&sounds));
  Owner("cue_properties",sounds,2,module);Owner("prepare",sounds,3,module);
  const auto index=sounds->GetCueIndex("ClockCue");if(index==XACTINDEX_INVALID)throw 2;
  auto tick=[&](unsigned ms)
  {
   const auto end=GetTickCount64()+ms;
   do {const auto hr=engine->DoWork();if(FAILED(hr))Require("work",hr);Sleep(1);}while(GetTickCount64()<end);
  };
  auto snapshot=[&](const char* step)
  {
   XACT_CUE_PROPERTIES p={};HRESULT hr=sounds->GetCueProperties(index,&p);
   DWORD bankState=0,waveState=0;const auto bankHr=sounds->GetState(&bankState);
   const auto waveHr=waves?waves->GetState(&waveState):S_FALSE;
   printf("{\"bank_snapshot\":\"%s\",\"sound_hr\":\"%08lx\",\"sound_state\":%lu,\"wave_hr\":\"%08lx\",\"wave_state\":%lu}\n",step,(unsigned long)bankHr,(unsigned long)bankState,(unsigned long)waveHr,(unsigned long)waveState);
   printf("{\"snapshot\":\"%s\",\"properties_hr\":\"%08lx\",\"instances\":%u,\"maximum\":%u,\"actors\":[",step,(unsigned long)hr,p.currentInstances,p.maxInstances);
   bool comma=false;
   for(unsigned i=0;i<4;++i)if(actors[i])
   {
    DWORD state=0;const auto sh=actors[i]->GetState(&state);float count=-1;
    const auto variable=actors[i]->GetVariableIndex("NumCueInstances");
    const auto vh=variable==XACTVARIABLEINDEX_INVALID?E_INVALIDARG:actors[i]->GetVariable(variable,&count);
    if(comma)printf(",");comma=true;
    printf("{\"name\":\"%s\",\"state_hr\":\"%08lx\",\"state\":%lu,\"variable_index\":%u,\"variable_hr\":\"%08lx\",\"variable\":%.9g}",labels[i],(unsigned long)sh,(unsigned long)state,variable,(unsigned long)vh,count);
   }
   printf("]}\n");fflush(stdout);
  };
  snapshot("initial");
  if(mode==L"before" || mode==L"destroyed" || mode==L"evicted-only" || mode==L"replay-order" || mode==L"reverse-play")
  {
   Require("prepare_P",sounds->Prepare(index,0,0,&actors[0]));snapshot("P_prepared_before_L");
   if(mode==L"reverse-play")tick(100);
  }
  Require("prepare_L",sounds->Prepare(index,0,0,&actors[1]));snapshot("L_prepared");
  if(mode==L"reverse-play")tick(100);
  Require("play_L",actors[1]->Play());tick(100);snapshot("L_settled");
  if(mode==L"reverse-play"){Require("play_P_reverse",actors[0]->Play());tick(100);snapshot("P_reverse_settled");}
  if(mode==L"after")
  {
   Require("prepare_P",sounds->Prepare(index,0,0,&actors[0]));snapshot("P_prepared_after_L");
  }
  if(mode==L"destroyed")
  {
   Require("destroy_P",actors[0]->Destroy());actors[0]=nullptr;snapshot("P_destroyed");
  }
  if(mode==L"paused")
  {
   Require("pause_L",actors[1]->Pause(TRUE));tick(30);snapshot("L_paused");
  }
  if(mode==L"stopping")
  {
   Require("stop_L_authored",actors[1]->Stop(XACT_FLAG_CUE_STOP_RELEASE));
   tick(20);snapshot("L_authored_after20ms");
  }
  const auto prepareHr=sounds->Prepare(index,0,0,&actors[2]);
  printf("{\"prepare_actor\":\"I\",\"hr\":\"%08lx\",\"has_cue\":%s}\n",(unsigned long)prepareHr,actors[2]?"true":"false");fflush(stdout);
  snapshot("I_prepared");
  if(actors[2])
  {
   const auto hr=actors[2]->Play();printf("{\"play_actor\":\"I\",\"hr\":\"%08lx\"}\n",(unsigned long)hr);fflush(stdout);
  }
  snapshot("I_play_requested");
  tick(20);snapshot("I_after20ms");tick(200);snapshot("I_settled");
  if(mode==L"paused")
  {
   Require("resume_L",actors[1]->Pause(FALSE));tick(200);snapshot("L_resumed_settled");
  }
  if(mode==L"evicted-only")
  {
   for(unsigned i=1;i<3;++i)if(actors[i])
   {
    Require("stop_playing",actors[i]->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));
    Require("destroy_playing",actors[i]->Destroy());actors[i]=nullptr;
   }
   tick(200);snapshot("only_evicted_P");
  }
  if(actors[0] && mode!=L"reverse-play")
  {
   const auto hr=actors[0]->Play();printf("{\"play_actor\":\"P_after_I\",\"hr\":\"%08lx\"}\n",(unsigned long)hr);fflush(stdout);
   snapshot("P_play_requested");tick(200);snapshot("P_after_I_settled");
  }
  if(mode==L"replay-order")
  {
   Require("prepare_J",sounds->Prepare(index,0,0,&actors[3]));
   Require("play_J",actors[3]->Play());tick(200);snapshot("J_after_P_replay");
  }
  for(unsigned i=0;i<4;++i)if(actors[i])
  {
   Require("stop_actor",actors[i]->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));
   Require("destroy_actor",actors[i]->Destroy());actors[i]=nullptr;
  }
  Require("soundbank_destroy",sounds->Destroy());sounds=nullptr;if(waves){Require("wavebank_destroy",waves->Destroy());waves=nullptr;}
  Require("shutdown",engine->ShutDown());initialized=false;engine->Release();engine=nullptr;
 }
 catch(int error){rc=error;}
 for(auto* p:actors)if(p){p->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE);p->Destroy();}
 if(sounds)sounds->Destroy();if(waves)waves->Destroy();
 if(engine){if(initialized)engine->ShutDown();engine->Release();}
 if(factory)factory->Release();if(module)FreeLibrary(module);
 CoUninitialize();return rc;
}
