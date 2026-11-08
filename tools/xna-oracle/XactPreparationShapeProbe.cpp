// SPDX-License-Identifier: MS-PL
// Measurement harness extending XactBankLifetimeProbe and XactPauseClockProbe.
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
    LONGLONG begin{}, end{};
    XACT_NOTIFICATION notification{};
    HANDLE file{};
    DWORD bytes{}, transferred{}, error{};
    ULONGLONG offset{};
    BOOL result{}, wait{};
    unsigned kind{}; // 0 notification, 1 ReadFile, 2 GetOverlappedResult
};
static Event events[65536];
static unsigned eventCount = 0;
static bool overflow = false;
static SRWLOCK eventLock = SRWLOCK_INIT;
static void Save(const Event& event) {
    AcquireSRWLockExclusive(&eventLock);
    if (eventCount < 65536) events[eventCount++] = event;
    else overflow = true;
    ReleaseSRWLockExclusive(&eventLock);
}
static void __stdcall Notify(const XACT_NOTIFICATION* n) {
    Event e{}; e.begin = e.end = Now(); e.notification = *n; Save(e);
}
static BOOL __stdcall Read(HANDLE file, LPVOID data, DWORD bytes, LPDWORD transferred, LPOVERLAPPED over) {
    Event e{}; e.kind = 1; e.begin = Now(); e.file = file; e.bytes = bytes;
    if (over) e.offset = (ULONGLONG(over->OffsetHigh) << 32) | over->Offset;
    e.result = ReadFile(file, data, bytes, transferred, over); e.error = GetLastError(); e.end = Now();
    // On overlapped IO, only a synchronous success defines this output here.
    if (e.result && transferred) e.transferred = *transferred;
    Save(e); SetLastError(e.error); return e.result;
}
static BOOL __stdcall Result(HANDLE file, LPOVERLAPPED over, LPDWORD transferred, BOOL wait) {
    Event e{}; e.kind = 2; e.begin = Now(); e.file = file; e.wait = wait;
    if (over) e.offset = (ULONGLONG(over->OffsetHigh) << 32) | over->Offset;
    e.result = GetOverlappedResult(file, over, transferred, wait); e.error = GetLastError(); e.end = Now();
    if (e.result && transferred) e.transferred = *transferred;
    Save(e); SetLastError(e.error); return e.result;
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
        if (e.kind) {
            printf("{\"io\":%u,\"sequence\":%u,\"begin\":%lld,\"end\":%lld,\"file\":\"%p\",\"offset\":%llu,\"bytes\":%lu,\"transferred\":%lu,\"ok\":%d,\"error\":%lu,\"wait\":%d}\n",
                e.kind, i, e.begin, e.end, e.file, e.offset, (unsigned long)e.bytes,
                (unsigned long)e.transferred, e.result, (unsigned long)e.error, e.wait);
            continue;
        }
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
    // DLL, fixture/native directory, mode, streaming(0/1), lookahead ms, IO callbacks(0/1)
    if (argc != 7 || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 2;
    const std::wstring mode = argv[3], directory = argv[2];
    const std::vector<std::wstring> modes = {L"normal", L"no-props", L"missingA", L"missingB", L"lateB",
        L"lateB-playing", L"AtoB", L"BtoA", L"unplayed-stop", L"unplayed-destroy", L"destroy-playing",
        L"unregister", L"clone", L"same-soundbank", L"duplicateA"};
    bool known = false; for (const auto& m : modes) known |= m == mode; if (!known) return 2;
    const bool streaming = wcstoul(argv[4], nullptr, 10) != 0;
    const DWORD lookahead = wcstoul(argv[5], nullptr, 10);
    const bool io = wcstoul(argv[6], nullptr, 10) != 0;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    printf("{\"protocol\":1,\"frequency\":%lld,\"mode\":\"%ls\",\"streaming\":%s,\"lookahead_ms\":%lu,\"io_callbacks\":%s}\n",
        frequency.QuadPart, mode.c_str(), streaming ? "true" : "false", (unsigned long)lookahead, io ? "true" : "false");
    HMODULE module = nullptr; IClassFactory* factory = nullptr; IXACT3Engine* engine = nullptr;
    IXACT3WaveBank* banks[3]{}; IXACT3SoundBank* sound[2]{}; IXACT3Cue* cue[2]{};
    HANDLE files[3] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
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
        if (!streaming) for (unsigned i = 0; i < 2; ++i)
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
        XACT_RUNTIME_PARAMETERS params{}; params.lookAheadTime = lookahead;
        params.pGlobalSettingsBuffer = xgs->bytes; params.globalSettingsBufferSize = xgs->size;
        params.fnNotificationCallback = Notify;
        if (io) { params.fileIOCallbacks.readFileCallback = Read; params.fileIOCallbacks.getOverlappedResultCallback = Result; }
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
        auto loadBank = [&](unsigned slot, unsigned source) {
            auto b = Now(); HRESULT hr;
            if (streaming) {
                files[slot] = CreateFileW((directory + (source ? L"/B.xwb" : L"/A.xwb")).c_str(), GENERIC_READ,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, nullptr);
                if (files[slot] == INVALID_HANDLE_VALUE) throw 2;
                XACT_STREAMING_PARAMETERS p{}; p.file = files[slot]; p.packetSize = 16;
                b = Now(); hr = engine->CreateStreamingWaveBank(&p, &banks[slot]);
            } else hr = engine->CreateInMemoryWaveBank(waves[source]->bytes, waves[source]->size, 0, 0, &banks[slot]);
            Call("create_wavebank", b, hr);
            printf("{\"bank_identity\":%u,\"source\":%u,\"pointer\":\"%p\",\"file\":\"%p\"}\n", slot, source, banks[slot], files[slot]);
            Owner("bank_state", banks[slot], 7, module);
            // Streaming creation is asynchronous. Prepared is a precondition, with a bounded wait.
            const auto deadline = GetTickCount64() + 5000;
            DWORD state = 0;
            do { b = Now(); Call("bank_ready_state", b, banks[slot]->GetState(&state));
                if (state & XACT_STATE_PREPARED) break; pump(10);
            } while (GetTickCount64() < deadline);
            if (!(state & XACT_STATE_PREPARED)) throw 3;
            if (mode != L"no-props") for (XACTINDEX wi = 0; wi < (source ? 1 : 2); ++wi) {
                XACT_WAVE_PROPERTIES p{}; b = Now(); Call("wave_properties", b, banks[slot]->GetWaveProperties(wi, &p));
                printf("{\"bank_identity\":%u,\"wave_index\":%u,\"samples\":%lu,\"format\":%lu,\"streaming_property\":%s}\n",
                    slot, wi, (unsigned long)p.durationInSamples, (unsigned long)p.format.dwValue, p.streaming ? "true" : "false");
                if (bool(p.streaming) != streaming) throw 3;
            }
        };
        if (mode != L"missingA") loadBank(0, 0);
        if (mode != L"missingB" && mode != L"lateB" && mode != L"lateB-playing") loadBank(1, 1);
        if (mode == L"duplicateA") loadBank(2, 0);
        t = Now(); Call("create_soundbank", t, engine->CreateSoundBank(xsb->bytes, xsb->size, 0, 0, &sound[0]));
        Owner("prepare", sound[0], 3, module);
        const auto index = sound[0]->GetCueIndex("ShapeCue"); if (index == XACTINDEX_INVALID) throw 2;
        if (mode != L"no-props") {
            XACT_CUE_PROPERTIES p{}; t = Now(); Call("cue_properties", t, sound[0]->GetCueProperties(index, &p));
            printf("{\"cue_properties\":true,\"interactive\":%s,\"variations\":%u,\"variable_index\":%u}\n",
                p.interactive ? "true" : "false", p.numVariations, p.iaVariableIndex);
        }
        auto snapshot = [&](const char* phase) {
            const auto begin = Now(); DWORD bstates[3]{}, sstates[2]{}, cstates[2]{};
            HRESULT bh[3] = {S_FALSE, S_FALSE, S_FALSE}, sh[2] = {S_FALSE, S_FALSE}, ch[2] = {S_FALSE, S_FALSE};
            for (unsigned i = 0; i < 3; ++i) if (banks[i]) bh[i] = banks[i]->GetState(&bstates[i]);
            for (unsigned i = 0; i < 2; ++i) {
                if (sound[i]) sh[i] = sound[i]->GetState(&sstates[i]);
                if (cue[i]) ch[i] = cue[i]->GetState(&cstates[i]);
            }
            printf("{\"snapshot\":\"%s\",\"begin\":%lld,\"end\":%lld,\"bank_states\":[%lu,%lu,%lu],\"bank_hr\":[\"%08lx\",\"%08lx\",\"%08lx\"],\"sound_states\":[%lu,%lu],\"sound_hr\":[\"%08lx\",\"%08lx\"],\"cue_states\":[%lu,%lu],\"cue_hr\":[\"%08lx\",\"%08lx\"]}\n",
                phase, begin, Now(), (unsigned long)bstates[0], (unsigned long)bstates[1], (unsigned long)bstates[2],
                (unsigned long)bh[0], (unsigned long)bh[1], (unsigned long)bh[2],
                (unsigned long)sstates[0], (unsigned long)sstates[1], (unsigned long)sh[0], (unsigned long)sh[1],
                (unsigned long)cstates[0], (unsigned long)cstates[1], (unsigned long)ch[0], (unsigned long)ch[1]); fflush(stdout);
            for (auto hr : {bh[0], bh[1], bh[2], sh[0], sh[1], ch[0], ch[1]}) if (FAILED(hr)) throw 2;
            // Counts are supplementary; wave notifications and independent states are the observables.
            if (mode != L"no-props") for (unsigned i = 0; i < 2; ++i) if (sound[i]) {
                XACT_CUE_PROPERTIES p{}; const auto b = Now();
                Call("instance_properties", b, sound[i]->GetCueProperties(index, &p));
                printf("{\"counts_at\":\"%s\",\"sound_identity\":%u,\"instances\":%u}\n", phase, i, p.currentInstances);
            }
        };
        auto setBranch = [&](float value) {
            const auto vi = engine->GetGlobalVariableIndex("Branch"); if (vi == XACTVARIABLEINDEX_INVALID) throw 2;
            const auto b = Now(); Call(value == 0 ? "set_A" : "set_B", b, engine->SetGlobalVariable(vi, value));
        };
        if (mode == L"AtoB" || mode == L"BtoA") setBranch(mode == L"AtoB" ? 0.f : 1.f);
        if (mode == L"missingB" || mode == L"lateB" || mode == L"lateB-playing") setBranch(1.f);
        snapshot("initial"); t = Now(); const auto prepare = sound[0]->Prepare(index, 0, 0, &cue[0]);
        Call("prepare", t, prepare, false);
        printf("{\"cue_identity\":0,\"pointer\":\"%p\",\"sound\":\"%p\"}\n", cue[0], sound[0]);
        snapshot("after_prepare");
        // A shape failure is evidence, not a PASS assumption; retained logs distinguish its call stage.
        if (FAILED(prepare)) throw 4;
        if (!cue[0]) throw 2;
        Owner("cue_play", cue[0], 0, module); Owner("cue_state", cue[0], 2, module);
        pump(300); snapshot("prepared_300");
        if (mode == L"AtoB" || mode == L"BtoA") setBranch(mode == L"AtoB" ? 1.f : 0.f);
        if (mode == L"lateB") loadBank(1, 1);
        if (mode == L"unplayed-stop") { t = Now(); Call("stop_unplayed", t, cue[0]->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE)); }
        else if (mode == L"unplayed-destroy") { t = Now(); Call("destroy_unplayed", t, cue[0]->Destroy()); cue[0] = nullptr; }
        else { t = Now(); const auto hr = cue[0]->Play(); Call("play", t, hr, false); if (FAILED(hr)) { snapshot("play_failed"); throw 4; } }
        const auto playTime = GetTickCount64(); snapshot("after_play_or_unplayed_action");
        for (unsigned ms : {20, 100, 200, 400, 750, 950, 1050, 1250, 1750, 2050, 3050, 4500}) {
            if (GetTickCount64() < playTime + ms) pump(DWORD(playTime + ms - GetTickCount64()));
            if (ms == 100 && mode == L"unregister") unregister();
            if (ms == 100 && mode == L"destroy-playing") { t = Now(); Call("destroy_playing", t, cue[0]->Destroy()); cue[0] = nullptr; }
            if (ms == 100 && mode == L"lateB-playing") loadBank(1, 1);
            if (ms == 100 && (mode == L"clone" || mode == L"same-soundbank")) {
                if (mode == L"clone") {
                    t = Now(); Call("create_clone", t, engine->CreateSoundBank(xsb->bytes, xsb->size, 0, 0, &sound[1]));
                }
                auto* target = mode == L"clone" ? sound[1] : sound[0];
                t = Now(); Call("prepare_clone", t, target->Prepare(index, 0, 0, &cue[1]));
                printf("{\"cue_identity\":1,\"pointer\":\"%p\",\"sound\":\"%p\"}\n", cue[1], target);
                t = Now(); Call("play_clone", t, cue[1]->Play());
            }
            char phase[40]; snprintf(phase, sizeof(phase), "play_%u", ms); snapshot(phase);
        }
        for (auto*& c : cue) if (c) { t = Now(); Call("stop", t, c->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE)); }
        pump(50); snapshot("stopped_handles");
        for (auto*& c : cue) if (c) { t = Now(); Call("destroy_cue", t, c->Destroy()); c = nullptr; }
        pump(50); snapshot("destroyed_cues_50"); pump(1950); snapshot("destroyed_cues_2000");
        t = Now(); Call("destroy_soundbank", t, sound[0]->Destroy()); sound[0] = nullptr;
        pump(50); snapshot("destroyed_first_soundbank");
        if (sound[1]) { t = Now(); Call("destroy_clone", t, sound[1]->Destroy()); sound[1] = nullptr; pump(50); snapshot("destroyed_both_soundbanks"); }
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
    // Callback storage, resident buffers and streaming HANDLEs outlive all engine work.
    // Never force-destroy an INUSE wave bank, including on a failed preparation path.
    if (engine) {
        auto cleanup = [&](const char* name, LONGLONG t, HRESULT hr) {
            Call(name, t, hr, false); if (FAILED(hr)) rc = 2;
        };
        for (auto*& c : cue) if (c) {
            auto t = Now(); cleanup("cleanup_stop", t, c->Stop(XACT_FLAG_CUE_STOP_IMMEDIATE));
            t = Now(); cleanup("cleanup_cue", t, c->Destroy()); c = nullptr;
        }
        for (auto*& s : sound) if (s) { const auto t = Now(); cleanup("cleanup_soundbank", t, s->Destroy()); s = nullptr; }
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
    for (auto f : files) if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (module) FreeLibrary(module); CoUninitialize();
    printf("{\"process_result\":%d}\n", rc); return rc;
}
