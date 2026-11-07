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
#include <math.h>
#include <stddef.h>
#include <tlhelp32.h>
#include <vector>
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
struct Marker { DWORD id; LONG nativeTime; ULONGLONG wallTime; };
struct Timeline { CRITICAL_SECTION lock; Marker markers[16]{}; unsigned count=0; };
static void __stdcall Notify(const XACT_NOTIFICATION* notification)
{
 if(notification->type!=XACTNOTIFICATIONTYPE_MARKER || !notification->pvContext)return;
 auto& line=*static_cast<Timeline*>(notification->pvContext);
 EnterCriticalSection(&line.lock);
 if(line.count<16)line.markers[line.count++]={notification->marker.marker,notification->timeStamp,WallNow()};
 LeaveCriticalSection(&line.lock);
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
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if(argc!=4)return 2;
 if(!QueryPerformanceFrequency(&wallFrequency) || wallFrequency.QuadPart<=0)return 2; // DLL, corpus directory, control|pause|repeat
 const bool pause=wcscmp(argv[3],L"control")!=0;
 const bool between=wcscmp(argv[3],L"between")==0;
 const bool twice=wcscmp(argv[3],L"twice")==0;
 const bool unbalanced=wcscmp(argv[3],L"unbalanced")==0;
 const bool repeat=wcscmp(argv[3],L"repeat")==0 || unbalanced;
 HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 2;
 HMODULE module=nullptr;IClassFactory* factory=nullptr;IXACT3Engine* engine=nullptr;
 IXACT3WaveBank* waves=nullptr;IXACT3SoundBank* sounds=nullptr;IXACT3Cue* cue=nullptr;
 bool initialized=false,registered=false;int rc=0;
 std::unique_ptr<Buffer> xgs,xwb,xsb;
 Timeline line;InitializeCriticalSection(&line.lock);
 XACT_NOTIFICATION_DESCRIPTION description={};
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
  factory->Release();factory=nullptr;Owner("create_soundbank",engine,9,module);Owner("do_work",engine,8,module);
  XACT_RUNTIME_PARAMETERS params={};params.lookAheadTime=XACT_ENGINE_LOOKAHEAD_DEFAULT;
  params.pGlobalSettingsBuffer=xgs->bytes;params.globalSettingsBufferSize=xgs->size;params.fnNotificationCallback=Notify;
  Require("initialize",engine->Initialize(&params));initialized=true;
  Require("wavebank",engine->CreateInMemoryWaveBank(xwb->bytes,xwb->size,0,0,&waves));
  Require("soundbank",engine->CreateSoundBank(xsb->bytes,xsb->size,0,0,&sounds));
  Owner("prepare",sounds,3,module);
  const auto index=sounds->GetCueIndex("ClockCue");if(index==XACTINDEX_INVALID)throw 2;
  Require("prepare",sounds->Prepare(index,0,0,&cue));Owner("pause",cue,8,module);Owner("state",cue,2,module);Owner("play",cue,0,module);
  description.type=XACTNOTIFICATIONTYPE_MARKER;description.flags=XACT_FLAG_NOTIFICATION_PERSIST;
  description.pCue=cue;description.cueIndex=XACTINDEX_INVALID;description.waveIndex=XACTINDEX_INVALID;description.pvContext=&line;
  Require("register_marker",engine->RegisterNotification(&description));registered=true;
  const auto start=WallNow();Require("play",cue->Play());
  bool paused=false,resumed=false,duplicateSent=false;ULONGLONG pauseAt=0,resumeAt=0,totalPause=0,pauseBegin=0;
  unsigned span=0;const unsigned spans=!pause?0:twice?2:1;
  while(WallNow()-start<6500)
  {
   const HRESULT workHr=engine->DoWork();if(FAILED(workHr))Require("work",workHr);
   const auto now=WallNow();
   if(span<spans && !paused && now-start-totalPause>=(between?700u:span==0?200u:700u))
   {
    pauseBegin=WallNow();const HRESULT pauseHr=cue->Pause(TRUE);pauseAt=WallNow();
    Require("pause",pauseHr);if(repeat && !unbalanced)Require("pause_again",cue->Pause(TRUE));
    paused=true;DWORD state=0;Require("state_paused",cue->GetState(&state));
    printf("{\"stage\":\"pause_wall\",\"wall\":%llu,\"state\":%lu}\n",pauseAt-start,(unsigned long)state);fflush(stdout);
   }
   if(unbalanced && paused && !duplicateSent && now-pauseAt>=400)
   { Require("pause_again_later",cue->Pause(TRUE));duplicateSent=true; }
   if(paused && !resumed && now-pauseAt>=(span==0?1200u:600u))
   {
    const auto resumeBegin=WallNow();const HRESULT resumeHr=cue->Pause(FALSE);resumeAt=WallNow();
    Require("resume",resumeHr);if(repeat && !unbalanced)Require("resume_again",cue->Pause(FALSE));
    printf("{\"stage\":\"hold_interval\",\"min_ms\":%llu,\"max_ms\":%llu}\n",resumeBegin-pauseAt,resumeAt-pauseBegin);
    resumed=true;totalPause+=resumeAt-pauseAt;++span;DWORD state=0;Require("state_resumed",cue->GetState(&state));
    printf("{\"stage\":\"resume_wall\",\"wall\":%llu,\"state\":%lu}\n",resumeAt-start,(unsigned long)state);fflush(stdout);
   }
   if(resumed){paused=false;resumed=false;}
   EnterCriticalSection(&line.lock);const unsigned count=line.count;LeaveCriticalSection(&line.lock);
   if(count>=4)break;Sleep(1);
  }
  Require("unregister_marker",engine->UnRegisterNotification(&description));registered=false;
  EnterCriticalSection(&line.lock);
  printf("{\"stage\":\"markers\",\"mode\":\"%ls\",\"count\":%u,\"pause_ms\":%llu}\n",argv[3],line.count,totalPause);
  for(unsigned i=0;i<line.count;++i)printf("{\"stage\":\"marker\",\"id\":%lu,\"native_time\":%ld,\"wall\":%llu}\n",(unsigned long)line.markers[i].id,line.markers[i].nativeTime,line.markers[i].wallTime-start);
  LeaveCriticalSection(&line.lock);fflush(stdout);
  if(line.count!=4)rc=3;
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
  if(snapshot==INVALID_HANDLE_VALUE)throw 2;
  MODULEENTRY32W entry={};entry.dwSize=sizeof(entry);
  if(Module32FirstW(snapshot,&entry))do
  {
   if(_wcsnicmp(entry.szModule,L"xact",4)==0 || _wcsnicmp(entry.szModule,L"xaudio",6)==0 || _wcsnicmp(entry.szModule,L"x3daudio",8)==0)
    printf("audio_module=%ls\n",entry.szExePath);
  }while(Module32NextW(snapshot,&entry));
  CloseHandle(snapshot);
  Require("stop",cue->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));Require("cue_destroy",cue->Destroy());cue=nullptr;
  Require("soundbank_destroy",sounds->Destroy());sounds=nullptr;Require("wavebank_destroy",waves->Destroy());waves=nullptr;
  Require("shutdown",engine->ShutDown());initialized=false;engine->Release();engine=nullptr;
 }
 catch(int error){rc=error;}
 if(registered && engine)engine->UnRegisterNotification(&description);
 if(cue){cue->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE);cue->Destroy();}
 if(sounds)sounds->Destroy();if(waves)waves->Destroy();
 if(engine){if(initialized)engine->ShutDown();engine->Release();}
 if(factory)factory->Release();if(module)FreeLibrary(module);
 DeleteCriticalSection(&line.lock);CoUninitialize();return rc;
}
