// SPDX-License-Identifier: MS-PL
// HND-XACT-05 measurement harness, derived from XactPreparationShapeProbe.cpp.
// One resident cue, Prepare + Play, then an 8 s observation window long enough for
// five one-second waves. Records every wave lifecycle notification and samples the
// cue's own track properties (loopCount, duration, active wave variation).
// Callback payloads are copied, never dereferenced after their native lifetime.
#define INITGUID
#ifndef __reserved
#define __reserved
#endif
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <memory>
#include <cstring>
#include <vector>
#define __in
#define __out
#undef __uuidof
#include "xact3.h"

static_assert(sizeof(XACT_WAVE_PROPERTIES) == 84);
static LARGE_INTEGER frequency;
static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
// Fixed storage avoids allocation/logging in native callbacks. Overflow invalidates the run.
struct Event {
    LONGLONG begin{};
    XACT_NOTIFICATION notification{};
};
static Event events[65536];
static unsigned eventCount = 0;
static bool overflow = false;
static SRWLOCK eventLock = SRWLOCK_INIT;
static void __stdcall Notify(const XACT_NOTIFICATION* n) {
    Event e{}; e.begin = Now(); e.notification = *n;
    AcquireSRWLockExclusive(&eventLock);
    if (eventCount < 65536) events[eventCount++] = e;
    else overflow = true;
    ReleaseSRWLockExclusive(&eventLock);
}
static void Call(const char* stage, LONGLONG begin, HRESULT hr, bool required = true) {
    const auto end = Now();
    printf("{\"call\":\"%s\",\"begin\":%lld,\"end\":%lld,\"hr\":\"%08lx\"}\n",
           stage, begin, end, (unsigned long)hr); fflush(stdout);
    if (required && FAILED(hr)) throw 2;
}
static void Owner(const char* name, void* object, unsigned slot, HMODULE module) {
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>((*reinterpret_cast<void***>(object))[slot]), &owner) || owner != module) throw 2;
    printf("{\"owner\":\"%s\",\"slot\":%u,\"supplied_module\":true}\n", name, slot);
}
struct Buffer {
    void* bytes = nullptr; DWORD size = 0;
    explicit Buffer(const std::wstring& path) {
        FILE* f = _wfopen(path.c_str(), L"rb"); if (!f) throw 2;
        if (fseek(f, 0, SEEK_END)) { fclose(f); throw 2; }
        const long n = ftell(f); if (n <= 0) { fclose(f); throw 2; } rewind(f);
        size = static_cast<DWORD>(n); bytes = _aligned_malloc(size, 2048);
        if (!bytes) { fclose(f); throw 2; }
        const bool ok = fread(bytes, 1, size, f) == size; fclose(f);
        if (!ok) { _aligned_free(bytes); bytes = nullptr; throw 2; }
    }
    ~Buffer() { _aligned_free(bytes); }
};
static void DumpEvents() {
    AcquireSRWLockExclusive(&eventLock);
    for (unsigned i = 0; i < eventCount; ++i) {
        const auto& e = events[i]; const auto& n = e.notification;
        void* bank = nullptr; void* sound = nullptr; void* cue = nullptr; void* wave = nullptr;
        unsigned waveIndex = XACTINDEX_INVALID, cueIndex = XACTINDEX_INVALID;
        DWORD marker = 0;
        if (n.type >= 1 && n.type <= 4) { sound = n.cue.pSoundBank; cue = n.cue.pCue; cueIndex = n.cue.cueIndex; }
        else if (n.type == 5) { sound = n.marker.pSoundBank; cue = n.marker.pCue; cueIndex = n.marker.cueIndex; marker = n.marker.marker; }
        else if (n.type == 6) sound = n.soundBank.pSoundBank;
        else if (n.type == 7 || n.type == 17 || n.type == 18) bank = n.waveBank.pWaveBank;
        else if (n.type >= 12 && n.type <= 16) {
            bank = n.wave.pWaveBank; wave = n.wave.pWave; waveIndex = n.wave.waveIndex;
            // Prepare/Destroy payloads do not promise cue/sound fields.
            if (n.type >= 13 && n.type <= 15) {
                sound = n.wave.pSoundBank; cue = n.wave.pCue; cueIndex = n.wave.cueIndex;
            }
        }
        printf("{\"notification\":%u,\"sequence\":%u,\"qpc\":%lld,\"native_ms\":%ld,\"bank\":\"%p\",\"sound\":\"%p\",\"cue\":\"%p\",\"wave\":\"%p\",\"wave_index\":%u,\"cue_index\":%u,\"marker\":%lu}\n",
            n.type, i, e.begin, (long)n.timeStamp, bank, sound, cue, wave, waveIndex, cueIndex, (unsigned long)marker);
    }
    printf("{\"trace_end\":true,\"events\":%u,\"overflow\":%s}\n", eventCount, overflow ? "true" : "false");
    ReleaseSRWLockExclusive(&eventLock); fflush(stdout);
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    // DLL, fixture/native directory, observation window in ms after Play.
    if (argc != 4 || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 2;
    const std::wstring directory = argv[2];
    const DWORD window = wcstoul(argv[3], nullptr, 10);
    if (window < 1000 || window > 20000) return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    printf("{\"protocol\":\"hnd-xact-05-cardinality-1\",\"frequency\":%lld,\"window_ms\":%lu,\"lookahead_ms\":250}\n",
        frequency.QuadPart, (unsigned long)window);
    HMODULE module = nullptr; IClassFactory* factory = nullptr; IXACT3Engine* engine = nullptr;
    IXACT3WaveBank* banks[2]{}; IXACT3SoundBank* sound = nullptr; IXACT3Cue* cue = nullptr;
    std::unique_ptr<Buffer> xgs, xsb, waves[2];
    std::vector<XACT_NOTIFICATION_DESCRIPTION> registered;
    bool initialized = false; int rc = 0;
    auto unregister = [&]() {
        while (!registered.empty()) {
            const auto t = Now(); const auto hr = engine->UnRegisterNotification(&registered.back());
            registered.pop_back(); Call("unregister", t, hr);
        }
    };
    try {
        xgs = std::make_unique<Buffer>(directory + L"/shapes.xgs");
        xsb = std::make_unique<Buffer>(directory + L"/shapes.xsb");
        for (unsigned i = 0; i < 2; ++i)
            waves[i] = std::make_unique<Buffer>(directory + (i ? L"/B.xwb" : L"/A.xwb"));
        module = LoadLibraryW(argv[1]); if (!module) throw 2;
        wchar_t loaded[MAX_PATH]{}, expected[MAX_PATH]{};
        if (!GetModuleFileNameW(module, loaded, MAX_PATH) || !GetFullPathNameW(argv[1], MAX_PATH, expected, nullptr)) throw 2;
        for (wchar_t* p = expected; *p; ++p) if (*p == L'/') *p = L'\\';
        if (_wcsicmp(loaded, expected)) throw 2;
        printf("module=%ls\n", loaded);
        using FactoryFn = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
        const auto getFactory = reinterpret_cast<FactoryFn>(GetProcAddress(module, "DllGetClassObject")); if (!getFactory) throw 2;
        CLSID retail36{}; auto t = Now();
        Call("retail36_clsid", t, CLSIDFromString(L"{248d8a3b-6256-44d3-a018-2ac96c459f47}", &retail36));
        IID refreshEngine{}; t = Now();
        Call("refresh_iid", t, IIDFromString(L"{b1ee676a-d9cd-4d2a-89a8-fa53eb9e480b}", &refreshEngine));
        if (!IsEqualIID(refreshEngine, IID_IXACT3Engine)) throw 2;
        printf("{\"clsid\":\"248d8a3b-6256-44d3-a018-2ac96c459f47\",\"iid\":\"b1ee676a-d9cd-4d2a-89a8-fa53eb9e480b\"}\n");
        t = Now(); Call("factory", t, getFactory(retail36, IID_IClassFactory, reinterpret_cast<void**>(&factory)));
        t = Now(); Call("engine", t, factory->CreateInstance(nullptr, IID_IXACT3Engine, reinterpret_cast<void**>(&engine)));
        factory->Release(); factory = nullptr;
        Owner("create_soundbank", engine, 9, module); Owner("work", engine, 8, module);
        XACT_RUNTIME_PARAMETERS params{}; params.lookAheadTime = 250;
        params.pGlobalSettingsBuffer = xgs->bytes; params.globalSettingsBufferSize = xgs->size;
        params.fnNotificationCallback = Notify;
        t = Now(); Call("initialize", t, engine->Initialize(&params)); initialized = true;
        for (BYTE type : {1, 2, 3, 4, 5, 6, 7, 12, 13, 14, 15, 16, 17, 18}) {
            XACT_NOTIFICATION_DESCRIPTION d{}; d.type = type; d.flags = XACT_FLAG_NOTIFICATION_PERSIST;
            d.cueIndex = d.waveIndex = XACTINDEX_INVALID;
            t = Now(); const auto hr = engine->RegisterNotification(&d); Call("register", t, hr);
            registered.push_back(d);
        }
        auto pump = [&](DWORD ms) {
            const ULONGLONG end = GetTickCount64() + ms;
            do { const auto hr = engine->DoWork(); if (FAILED(hr)) Call("work_failed", Now(), hr); Sleep(1); }
            while (GetTickCount64() < end);
        };
        for (unsigned slot = 0; slot < 2; ++slot) {
            auto b = Now();
            Call("create_wavebank", b, engine->CreateInMemoryWaveBank(waves[slot]->bytes, waves[slot]->size, 0, 0, &banks[slot]));
            printf("{\"bank_identity\":%u,\"source\":%u,\"pointer\":\"%p\"}\n", slot, slot, banks[slot]);
            Owner("bank_state", banks[slot], 7, module);
            DWORD state = 0; b = Now(); Call("bank_ready_state", b, banks[slot]->GetState(&state));
            if (!(state & XACT_STATE_PREPARED)) throw 3;
            for (XACTINDEX wi = 0; wi < (slot ? 1 : 2); ++wi) {
                XACT_WAVE_PROPERTIES p{}; b = Now(); Call("wave_properties", b, banks[slot]->GetWaveProperties(wi, &p));
                printf("{\"bank_identity\":%u,\"wave_index\":%u,\"samples\":%lu,\"format\":%lu,\"loop_start\":%lu,\"loop_length\":%lu,\"streaming_property\":%s}\n",
                    slot, wi, (unsigned long)p.durationInSamples, (unsigned long)p.format.dwValue,
                    (unsigned long)p.loopRegion.dwStartSample, (unsigned long)p.loopRegion.dwTotalSamples,
                    p.streaming ? "true" : "false");
            }
        }
        t = Now(); Call("create_soundbank", t, engine->CreateSoundBank(xsb->bytes, xsb->size, 0, 0, &sound));
        Owner("prepare", sound, 3, module);
        const auto index = sound->GetCueIndex("ShapeCue"); if (index == XACTINDEX_INVALID) throw 2;
        {
            XACT_CUE_PROPERTIES p{}; t = Now(); Call("cue_properties", t, sound->GetCueProperties(index, &p));
            printf("{\"cue_properties\":true,\"interactive\":%s,\"variations\":%u,\"variable_index\":%u}\n",
                p.interactive ? "true" : "false", p.numVariations, p.iaVariableIndex);
        }
        auto snapshot = [&](const char* phase) {
            const auto begin = Now(); DWORD bstates[2]{}, sstate = 0, cstate = 0;
            HRESULT bh[2] = {S_FALSE, S_FALSE}, sh = S_FALSE, ch = S_FALSE;
            for (unsigned i = 0; i < 2; ++i) if (banks[i]) bh[i] = banks[i]->GetState(&bstates[i]);
            if (sound) sh = sound->GetState(&sstate);
            if (cue) ch = cue->GetState(&cstate);
            printf("{\"snapshot\":\"%s\",\"begin\":%lld,\"end\":%lld,\"bank_states\":[%lu,%lu],\"bank_hr\":[\"%08lx\",\"%08lx\"],\"sound_state\":%lu,\"sound_hr\":\"%08lx\",\"cue_state\":%lu,\"cue_hr\":\"%08lx\"}\n",
                phase, begin, Now(), (unsigned long)bstates[0], (unsigned long)bstates[1],
                (unsigned long)bh[0], (unsigned long)bh[1], (unsigned long)sstate, (unsigned long)sh,
                (unsigned long)cstate, (unsigned long)ch); fflush(stdout);
            for (auto hr : {bh[0], bh[1], sh, ch}) if (FAILED(hr)) throw 2;
            if (sound) {
                XACT_CUE_PROPERTIES p{}; const auto b = Now();
                Call("instance_properties", b, sound->GetCueProperties(index, &p));
                printf("{\"counts_at\":\"%s\",\"instances\":%u}\n", phase, p.currentInstances);
            }
            if (cue) {
                // The instance properties belong to XACT's allocator; this short-lived
                // process deliberately does not free them rather than guess the deallocator.
                LPXACT_CUE_INSTANCE_PROPERTIES p = nullptr; const auto b = Now();
                Call("cue_instance_properties", b, cue->GetProperties(&p));
                if (!p) throw 2;
                const auto& s = p->activeVariationProperties.soundProperties;
                printf("{\"track_at\":\"%s\",\"num_tracks\":%u", phase, (unsigned)s.numTracks);
                if (s.numTracks >= 1) {
                    const auto& tr = s.arrTrackProperties[0];
                    printf(",\"duration_ms\":%ld,\"num_wave_variations\":%u,\"channels\":%u,\"wave_variation\":%u,\"loop_count\":%u",
                        (long)tr.duration, (unsigned)tr.numVariations, (unsigned)tr.numChannels,
                        (unsigned)tr.waveVariation, (unsigned)tr.loopCount);
                }
                printf("}\n"); fflush(stdout);
            }
        };
        snapshot("initial"); t = Now(); const auto prepare = sound->Prepare(index, 0, 0, &cue);
        Call("prepare", t, prepare, false);
        printf("{\"cue_identity\":0,\"pointer\":\"%p\",\"sound\":\"%p\"}\n", cue, sound);
        if (FAILED(prepare)) { snapshot("prepare_failed"); throw 4; }
        if (!cue) throw 2;
        Owner("cue_play", cue, 0, module); Owner("cue_state", cue, 2, module); Owner("cue_get_properties", cue, 9, module);
        snapshot("after_prepare");
        pump(300); snapshot("prepared_300");
        t = Now(); const auto hr = cue->Play(); Call("play", t, hr, false); if (FAILED(hr)) { snapshot("play_failed"); throw 4; }
        const auto playTime = GetTickCount64(); snapshot("after_play");
        std::vector<unsigned> marks = {20, 100, 200, 400};
        for (unsigned ms = 600; ms < window; ms += 250) marks.push_back(ms);
        marks.push_back(window);
        for (unsigned ms : marks) {
            if (GetTickCount64() < playTime + ms) pump(DWORD(playTime + ms - GetTickCount64()));
            char phase[40]; snprintf(phase, sizeof(phase), "play_%u", ms); snapshot(phase);
        }
        t = Now(); Call("stop", t, cue->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));
        pump(50); snapshot("stopped_handle");
        t = Now(); Call("destroy_cue", t, cue->Destroy()); cue = nullptr;
        pump(50); snapshot("destroyed_cue_50"); pump(1950); snapshot("destroyed_cue_2000");
        t = Now(); Call("destroy_soundbank", t, sound->Destroy()); sound = nullptr;
        pump(50); snapshot("destroyed_soundbank");
        for (auto*& b : banks) if (b) {
            DWORD state = 0; t = Now(); Call("bank_cleanup_state", t, b->GetState(&state));
            if (state & XACT_STATE_INUSE) { printf("{\"bank_destroy_skipped_inuse\":true}\n"); continue; }
            t = Now(); Call("destroy_wavebank", t, b->Destroy()); b = nullptr;
        }
        pump(50); unregister();
        HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
        if (modules == INVALID_HANDLE_VALUE) throw 2;
        MODULEENTRY32W m{}; m.dwSize = sizeof(m);
        if (Module32FirstW(modules, &m)) do {
            if (!_wcsnicmp(m.szModule, L"xact", 4) || !_wcsnicmp(m.szModule, L"xaudio", 6) || !_wcsnicmp(m.szModule, L"x3daudio", 8))
                printf("audio_module=%ls\n", m.szExePath);
        } while (Module32NextW(modules, &m));
        CloseHandle(modules);
    } catch (int error) { rc = error; }
    // Callback storage and resident buffers outlive all engine work.
    // Never force-destroy an INUSE wave bank, including on a failed preparation path.
    if (engine) {
        auto cleanup = [&](const char* name, LONGLONG t, HRESULT hr) {
            Call(name, t, hr, false); if (FAILED(hr)) rc = 2;
        };
        if (cue) {
            auto t = Now(); cleanup("cleanup_stop", t, cue->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));
            t = Now(); cleanup("cleanup_cue", t, cue->Destroy()); cue = nullptr;
        }
        if (sound) { const auto t = Now(); cleanup("cleanup_soundbank", t, sound->Destroy()); sound = nullptr; }
        for (auto*& b : banks) if (b) {
            DWORD state = XACT_STATE_INUSE; auto t = Now(); const auto hr = b->GetState(&state);
            cleanup("cleanup_bank_state", t, hr);
            if (SUCCEEDED(hr) && !(state & XACT_STATE_INUSE)) { t = Now(); cleanup("cleanup_bank", t, b->Destroy()); }
            else printf("{\"cleanup_bank_skipped_inuse_or_unknown\":true}\n");
            b = nullptr;
        }
        if (initialized) { const auto t = Now(); cleanup("shutdown", t, engine->ShutDown()); }
        engine->Release();
    }
    if (factory) factory->Release();
    DumpEvents(); if (overflow && !rc) rc = 3;
    if (module) FreeLibrary(module); CoUninitialize();
    printf("{\"process_result\":%d}\n", rc); return rc;
}
