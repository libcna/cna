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
 if(mode!=L"keep" && mode!=L"destroy-finished" && mode!=L"missingA" && mode!=L"missingB" && mode!=L"stop" && mode!=L"no-props" && mode!=L"unplayed-stop" && mode!=L"unplayed-destroy" && mode!=L"retryB")return 2;
 if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
 HMODULE module=nullptr;IClassFactory* factory=nullptr;IXACT3Engine* engine=nullptr;
 IXACT3WaveBank* banks[2]{};IXACT3SoundBank* sounds=nullptr;IXACT3Cue* cue=nullptr;
 bool initialized=false;int rc=0;std::unique_ptr<Buffer> xgs,xsb,waves[2];
 try
 {
  xgs=std::make_unique<Buffer>(std::wstring(argv[2])+L"/lifetime.xgs");
  xsb=std::make_unique<Buffer>(std::wstring(argv[2])+L"/lifetime.xsb");
  waves[0]=std::make_unique<Buffer>(std::wstring(argv[2])+L"/A.xwb");
  waves[1]=std::make_unique<Buffer>(std::wstring(argv[2])+L"/B.xwb");
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
  for(unsigned i=0;i<2;++i)
  {
   if((i==0 && mode==L"missingA") || (i==1 && (mode==L"missingB" || mode==L"retryB")))continue;
   Require(i==0?"bank_A":"bank_B",engine->CreateInMemoryWaveBank(waves[i]->bytes,waves[i]->size,0,0,&banks[i]));
   if(mode!=L"no-props")
   {
   XACT_WAVE_PROPERTIES properties{};
   Require("wave_properties",banks[i]->GetWaveProperties(0,&properties));
   printf("{\"wave\":\"%c\",\"samples\":%lu,\"format\":%lu}\n",char('A'+i),(unsigned long)properties.durationInSamples,(unsigned long)properties.format.dwValue);
   }
  }
  Require("soundbank",engine->CreateSoundBank(xsb->bytes,xsb->size,0,0,&sounds));
  Owner("cue_properties",sounds,2,module);Owner("prepare",sounds,3,module);
  const auto index=sounds->GetCueIndex("LifetimeCue");if(index==XACTINDEX_INVALID)throw 2;
  auto tickUntil=[&](ULONGLONG deadline)
  {
   do {const auto hr=engine->DoWork();if(FAILED(hr))Require("work",hr);Sleep(1);}while(GetTickCount64()<deadline);
  };
  const auto begin=GetTickCount64();
  auto snapshot=[&](const char* stage)
  {
   DWORD soundState=0,cueState=0,states[2]{};
   const HRESULT sh=sounds?sounds->GetState(&soundState):S_FALSE;
   const HRESULT ch=cue?cue->GetState(&cueState):S_FALSE;
   HRESULT wh[2];for(unsigned i=0;i<2;++i)wh[i]=banks[i]?banks[i]->GetState(&states[i]):S_FALSE;
   XACT_CUE_PROPERTIES properties{};const auto ph=sounds?sounds->GetCueProperties(index,&properties):S_FALSE;
   printf("{\"snapshot\":\"%s\",\"elapsed_ms\":%llu,\"sound_hr\":\"%08lx\",\"sound_state\":%lu,\"cue_hr\":\"%08lx\",\"cue_state\":%lu,\"properties_hr\":\"%08lx\",\"instances\":%u,\"A_hr\":\"%08lx\",\"A_state\":%lu,\"B_hr\":\"%08lx\",\"B_state\":%lu}\n",
    stage,(unsigned long long)(GetTickCount64()-begin),(unsigned long)sh,(unsigned long)soundState,(unsigned long)ch,(unsigned long)cueState,(unsigned long)ph,properties.currentInstances,(unsigned long)wh[0],(unsigned long)states[0],(unsigned long)wh[1],(unsigned long)states[1]);fflush(stdout);
  };
  snapshot("initial");
  auto prepareHr=sounds->Prepare(index,0,0,&cue);
  printf("{\"prepare_hr\":\"%08lx\",\"has_cue\":%s}\n",(unsigned long)prepareHr,cue?"true":"false");fflush(stdout);
  if(FAILED(prepareHr))
  {
   snapshot("prepare_failed");
   if(mode!=L"retryB" || prepareHr!=static_cast<HRESULT>(0x8ac70013u) || cue)throw 2;
   Require("retry_B",engine->CreateInMemoryWaveBank(waves[1]->bytes,waves[1]->size,0,0,&banks[1]));
   snapshot("B_loaded_after_failure");
   prepareHr=sounds->Prepare(index,0,0,&cue);
   printf("{\"prepare_retry_hr\":\"%08lx\",\"has_cue\":%s}\n",(unsigned long)prepareHr,cue?"true":"false");fflush(stdout);
   if(FAILED(prepareHr))throw 2;
  }
  snapshot("prepared");tickUntil(GetTickCount64()+200);snapshot("prepared_wait");
  const bool didPlay=mode!=L"unplayed-stop" && mode!=L"unplayed-destroy";
  if(mode==L"unplayed-stop"){Require("stop_unplayed",cue->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));snapshot("unplayed_stopped");}
  if(mode==L"unplayed-destroy"){Require("destroy_unplayed",cue->Destroy());cue=nullptr;snapshot("unplayed_destroyed");}
  if(didPlay)Require("play",cue->Play());
  const auto playTime=GetTickCount64();snapshot(didPlay?"play_requested":"unplayed_start");
  tickUntil(playTime+40);snapshot("phase_40");
  if(mode==L"stop"){Require("stop_immediate",cue->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));snapshot("stopped_immediate");}
  tickUntil(playTime+250);snapshot("phase_250");
  if(mode==L"destroy-finished")
  {
   DWORD state=0;Require("A_before_destroy",banks[0]->GetState(&state));
   if((state & XACT_STATE_INUSE)==0)
   {Require("destroy_A",banks[0]->Destroy());banks[0]=nullptr;snapshot("A_destroyed");}
   else {printf("{\"destroy_A_skipped\":true,\"state\":%lu}\n",(unsigned long)state);fflush(stdout);}
  }
  tickUntil(playTime+600);snapshot("phase_600");tickUntil(playTime+1700);snapshot("phase_1700");
  if(cue){Require("destroy_cue",cue->Destroy());cue=nullptr;}
  const auto destroyTime=GetTickCount64();
  tickUntil(destroyTime+50);snapshot("cue_destroyed_50");
  tickUntil(destroyTime+2000);snapshot("cue_destroyed_2000");
  Require("soundbank_destroy",sounds->Destroy());sounds=nullptr;
  tickUntil(GetTickCount64()+100);snapshot("soundbank_destroyed");
  for(auto*& bank:banks)if(bank)
  {
   DWORD state=0;Require("bank_before_cleanup",bank->GetState(&state));
   if((state & XACT_STATE_INUSE)==0){Require("bank_destroy",bank->Destroy());bank=nullptr;}
  }
  Require("shutdown",engine->ShutDown());initialized=false;
  banks[0]=banks[1]=nullptr;engine->Release();engine=nullptr;
 }
 catch(int error){rc=error;}
 if(cue){cue->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE);cue->Destroy();}
 if(sounds)sounds->Destroy();
 for(auto* bank:banks)if(bank){DWORD state=0;bank->GetState(&state);if((state & XACT_STATE_INUSE)==0)bank->Destroy();}
 if(engine){if(initialized)engine->ShutDown();engine->Release();}
 if(factory)factory->Release();if(module)FreeLibrary(module);CoUninitialize();return rc;
}
