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
static LARGE_INTEGER wallFrequency;
static ULONGLONG WallNow()
{
 LARGE_INTEGER tick;QueryPerformanceCounter(&tick);
 return static_cast<ULONGLONG>(tick.QuadPart)*1000/static_cast<ULONGLONG>(wallFrequency.QuadPart);
}
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
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);if(argc!=4)return 2;
 if(!QueryPerformanceFrequency(&wallFrequency))return 2;
 HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 2;
 HMODULE module=nullptr;IClassFactory* factory=nullptr;IXACT3Engine* engine=nullptr;
 IXACT3WaveBank* waves=nullptr;IXACT3SoundBank* sounds=nullptr;IXACT3Cue *peer=nullptr,*live=nullptr;
 bool initialized=false;int rc=0;std::unique_ptr<Buffer> xgs,xwb,xsb;
 try
 {
  xgs=std::make_unique<Buffer>(std::wstring(argv[2])+L"/clock.xgs");xwb=std::make_unique<Buffer>(std::wstring(argv[2])+L"/clock.xwb");xsb=std::make_unique<Buffer>(std::wstring(argv[2])+L"/clock.xsb");
  module=LoadLibraryW(argv[1]);if(!module)throw 2;wchar_t loaded[MAX_PATH]{},expected[MAX_PATH]{};
  if(!GetModuleFileNameW(module,loaded,MAX_PATH)||!GetFullPathNameW(argv[1],MAX_PATH,expected,nullptr))throw 2;for(wchar_t* p=expected;*p;++p)if(*p==L'/')*p=L'\\';if(_wcsicmp(loaded,expected))throw 2;printf("module=%ls\n",loaded);
  using FactoryFn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto getFactory=reinterpret_cast<FactoryFn>(GetProcAddress(module,"DllGetClassObject"));if(!getFactory)throw 2;
  CLSID retail36{};Require("retail36_clsid",CLSIDFromString(L"{248d8a3b-6256-44d3-a018-2ac96c459f47}",&retail36));Require("factory",getFactory(retail36,IID_IClassFactory,reinterpret_cast<void**>(&factory)));Require("engine",factory->CreateInstance(nullptr,IID_IXACT3Engine,reinterpret_cast<void**>(&engine)));factory->Release();factory=nullptr;Owner("create_soundbank",engine,9,module);
  XACT_RUNTIME_PARAMETERS params={};params.lookAheadTime=XACT_ENGINE_LOOKAHEAD_DEFAULT;params.pGlobalSettingsBuffer=xgs->bytes;params.globalSettingsBufferSize=xgs->size;Require("initialize",engine->Initialize(&params));initialized=true;
  Require("wavebank",engine->CreateInMemoryWaveBank(xwb->bytes,xwb->size,0,0,&waves));Require("soundbank",engine->CreateSoundBank(xsb->bytes,xsb->size,0,0,&sounds));Owner("cue_properties",sounds,2,module);
  const auto index=sounds->GetCueIndex("ClockCue");if(index==XACTINDEX_INVALID)throw 2;
  auto tick=[&](unsigned ms){const auto end=WallNow()+ms;do{const auto hr=engine->DoWork();if(FAILED(hr))Require("work",hr);Sleep(1);}while(WallNow()<end);};
  const auto begin=WallNow();
  auto snapshot=[&](const char* name)
  {
   XACT_CUE_PROPERTIES props={};const auto ph=sounds->GetCueProperties(index,&props);float a=-1,b=-1;DWORD ps=0,ls=0;
   const auto pi=peer->GetVariableIndex("NumCueInstances");const auto ah=peer->GetVariable(pi,&a);peer->GetState(&ps);
   HRESULT bh=S_OK;if(live){const auto li=live->GetVariableIndex("NumCueInstances");bh=live->GetVariable(li,&b);live->GetState(&ls);}
   printf("{\"snapshot\":\"%s\",\"wall\":%llu,\"properties_hr\":\"%08lx\",\"instances\":%u,\"peer_hr\":\"%08lx\",\"peer\":%.9g,\"peer_state\":%lu,\"live_hr\":\"%08lx\",\"live\":%.9g,\"live_state\":%lu}\n",name,WallNow()-begin,(unsigned long)ph,props.currentInstances,(unsigned long)ah,a,(unsigned long)ps,(unsigned long)bh,b,(unsigned long)ls);fflush(stdout);
  };
  Require("prepare_peer",sounds->Prepare(index,0,0,&peer));Require("prepare_live",sounds->Prepare(index,0,0,&live));Owner("get_variable",peer,7,module);snapshot("prepared");
  Require("play",live->Play());tick(10);snapshot("playing");
  if(wcscmp(argv[3],L"destroy")==0){Require("destroy_live",live->Destroy());live=nullptr;snapshot("destroyed_immediate");tick(150);snapshot("destroyed_settled");}
  else if(wcscmp(argv[3],L"authored")==0){Require("stop_authored",live->Stop(XACT_FLAG_CUE_STOP_RELEASE));snapshot("stop_requested");tick(20);snapshot("tail_after20ms");tick(150);snapshot("tail_settled");}
  else {tick(20);snapshot("natural_after30ms");tick(400);snapshot("natural_settled");}
  if(live){live->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE);live->Destroy();live=nullptr;}peer->Destroy();peer=nullptr;
  Require("soundbank_destroy",sounds->Destroy());sounds=nullptr;Require("wavebank_destroy",waves->Destroy());waves=nullptr;Require("shutdown",engine->ShutDown());initialized=false;engine->Release();engine=nullptr;
 }
 catch(int error){rc=error;}
 if(live){live->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE);live->Destroy();}if(peer)peer->Destroy();if(sounds)sounds->Destroy();if(waves)waves->Destroy();if(engine){if(initialized)engine->ShutDown();engine->Release();}if(factory)factory->Release();if(module)FreeLibrary(module);CoUninitialize();return rc;
}
