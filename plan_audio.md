# plan_audio.md — Dokončení a revize portu FNA Audio → CNA (C++ / XNA 4.0)

> **Rozsah:** výhradně `Microsoft::Xna::Framework::Audio` + interní `CNA::Internal::Audio`.
> **Media namespace NENÍ součástí tohoto plánu.**
>
> **Cíl:** projít audio soubor po souboru proti autoritativnímu FNA zdroji, doplnit chybějící
> implementaci, opravit chyby a odchylky, sladit s `CLAUDE.md`/`CHECKLIST.md` a doplnit **kompletní
> testy** (dnes pro audio neexistuje ani jeden test). Žádné stuby bez doloženého důvodu — každá
> vědomá odchylka od FNA musí být zdokumentovaná v `CHECKLIST.md` (tabulka akceptovaných odchylek).

- **FNA reference (autoritativní):** `/rv/data/library/github.com/FNA-XNA/FNA/src/Audio/`
- **Backend:** CNA audio běží na **SDL3_mixer**, nikoli na FAudio/FACT. XACT (.xgs/.xsb/.xwb) se
  parsuje vlastním `XactParser` a mixuje přes SDL_mixer. To je hlavní zdroj odchylek (3D HRTF,
  Doppler, streaming wavebanky, FACT DoWork).
- **Stav dle AUDIT.md:** vše „✅", ale s poznámkami „stub behavior" u AudioEngine/Cue/SoundBank/
  WaveBank — tento plán tyto poznámky rozpadá na konkrétní úkoly.

---

## 1. Inventář souborů a aktuální stav (jen Audio)

| # | Soubor | FNA ř. | Stav | Hlavní mezery |
|---|--------|-------:|------|---------------|
| 1 | SoundEffect | 821 | Funkční s odchylkami | žádný `Instances`/Dispose-kaskáda; `CreateInstance` vrací hodnotou; špatné typy výjimek; `Play()` obchází instance |
| 2 | SoundEffectInstance | 652 | Částečný | chybí interní DSP (reverb/filtry); Pan/Volume místo validace klampuje; veřejný ctor (FNA internal); špatné výjimky |
| 3 | SoundEffectI.hpp | — | Neopodstatněné rozšíření | non-XNA typ v XNA namespace bez NOXNA; žádné polymorfní využití |
| 4 | DynamicSoundEffectInstance | 326 | Částečný — **bugy** | `setIsLoopedProperty` neoverrideuje; chybí `Dispose()` override → leak; chybí guard `SubmitFloatBufferEXT`; duplicitní `disposed_` |
| 5 | SoundState.hpp | 19 | Úplný | OK |
| 6 | AudioEngine | 445 | Částečný (parse OK, mix stub) | špatné výjimky; `GetCategory`/`SetGlobalVariable` nehází na neznámé jméno; `Update()` no-op; `GetTypeName` s `::` |
| 7 | SoundBank | 284 | Částečný / stub | `IsInUse` natvrdo `false`; `GetCue` vrací stub místo throw; 3D `PlayCue` ignoruje listener/emitter; raw `Cue*`; špatné výjimky; `::` |
| 8 | WaveBank | 226 | Částečný / stub | `IsInUse` vždy `false`; streaming ctor ignoruje offset/packetSize; špatné výjimky; `::` |
| 9 | Cue | 305 | Částečný / stub | `Apply3D` no-op; `GetVariable`/`SetVariable` bez XSB validace; špatné výjimky; `::` |
| 10 | AudioCategory | 144 | Téměř úplný (sémantický drift) | `Equals` porovnává parent+index místo Name; chybí `Equals(Object)`; doxygen tvrdí „no-op", impl je funkční |
| 11 | AudioEmitter | 158 | Úplný (datová třída) | `DopplerScale` setter hází `std::out_of_range` |
| 12 | AudioListener | 118 | Úplný (datová třída) | bez chování — bez chyb |
| 13 | AudioChannels.hpp | enum | Úplný | OK |
| 14 | AudioStopOptions.hpp | enum | Úplný | OK |
| 15 | Microphone | 217 | **Stub** (capture neimplementován) | žádný SDL capture; chybí `GetTypeName()`; špatné výjimky |
| 16 | MicrophoneState.hpp | enum | Úplný | OK |
| 17 | RendererDetail | 79 | Částečný | chybí `Equals`; neúplný doxygen |
| 18 | InstancePlayLimitException | 35 | Částečný | dědí `std::runtime_error`, má být `ExternalException` |
| 19 | NoAudioHardwareException | 35 | Částečný | dědí `std::runtime_error`, má být `ExternalException` |
| 20 | NoMicrophoneConnectedException | 35 | Částečný | dědí `std::runtime_error`, má být `System::Exception` |
| I1 | Internal/Audio/XactTypes.hpp | — | Scaffold | chybí SPDX; `///` doc |
| I2 | Internal/Audio/XactParser.cpp | 809 | Rozsáhlý, **buggy** | chybí SPDX; mrtvý XGS first-pass; **compact-XWB `dataLength` bug**; track parser `break`uje na neznámé eventy |
| I3 | Internal/Audio/AudioMixer.hpp/.cpp | — | Tenký SDL3_mixer wrapper | chybí SPDX; jediné natvrdo 44100/stereo/S16 |

---

## 2. Akceptované odchylky (NEopravujeme — pouze dokumentujeme v CHECKLIST.md)

Tyto vyplývají z volby backendu SDL3_mixer a z C++ hodnotové sémantiky. Cílem je je **zdokumentovat**,
ne odstranit (pokud se nerozhodne jinak v úkolu, který je explicitně zmiňuje):

1. **3D HRTF / poziční audio** — SDL_mixer nemá FAudio F3DAudio. `Apply3D` / 3D `PlayCue` lze
   aproximovat jen pan + distance-attenuací (`Mix_SetPosition`), bez výškové informace. (viz T-F2)
2. **Doppler** — SDL_mixer nepodporuje per-channel pitch shift dle rychlosti. `DopplerScale`/`Velocity`
   se ukládá, ale neaplikuje.
3. **`GetHashCode()` → `int`/`size_t`** přes `std::hash` — hodnota se neshoduje s C# `String.GetHashCode`,
   ale je konzistentní (akceptováno dle CHECKLIST.md).
4. **FACT `DoWork` / streaming wavebanky** — celý wavebank se načítá do paměti; skutečný streaming
   (offset/packetSize) není implementován. (úkol T-3F to buď doplní, nebo natvrdo zdokumentuje)
5. **`CreateInstance`/`FromStream` hodnotová vs. heap-reference sémantika** — řeší úkol T-3G
   (buď instance-tracking, nebo doložená odchylka).

> Každá z těchto odchylek musí mít řádek v tabulce odchylek v `CHECKLIST.md` (úkol T-6A).

---

## 3. Průřezové vady (jeden úkol, mnoho souborů)

Tyto se objevují napříč clusterem a řeší se hromadně:

- **X1 — Mapování výjimek `std::*` → `System::*`.** Sharp-runtime už poskytuje
  `ArgumentNullException`, `ArgumentException`, `ArgumentOutOfRangeException`,
  `InvalidOperationException`, `ObjectDisposedException`, `NotSupportedException`,
  `NotImplementedException`, `Runtime::InteropServices::ExternalException`. Audio je nepoužívá vůbec.
- **X2 — `GetTypeName()` používá `::` místo `.`** v AudioEngine/SoundBank/WaveBank/Cue; konvence
  projektu je tečková (`Microsoft.Xna.Framework.Audio.X`), jak má `SoundEffectInstance`.
- **X3 — Chybí SPDX** (`// SPDX-License-Identifier: MS-PL`) ve všech 4 interních souborech.
- **X4 — Nulové testy** — adresář `tests/Microsoft/Xna/Framework/Audio/` neexistuje. Testy se do
  `CnaTests` přibírají přes `GLOB_RECURSE tests/*.cpp` (CMakeLists.txt:1157), takže stačí zakládat soubory.

---

## 4. Úkoly (po fázích)

> Konvence: každý úkol má **ID**, dotčené soubory, **FNA ref**, popis, **akceptační kritéria vč. testů**
> a odkaz na zdrojový audit (A#/B#/C#). Odškrtávej `[ ]` → `[x]`.

### Fáze 0 — Bootstrap testů a build

- [x] **T-0A — Založit audio test infrastrukturu.**
  Vytvořit `tests/Microsoft/Xna/Framework/Audio/` + první kostru (`CMakeLists` netřeba měnit — GLOB).
  Ověřit, že prázdný `*_test.cpp` se zaregistruje do `CnaTests` a projde build.
  *Accept:* `cmake --build cmake-build-debug --target CnaTests` přeloží a spustí novou (zatím triviální) test fixturu. (zdroj: A10/B11/C6)

### Fáze 1 — Compliance sweep (nízká námaha, vysoká hodnota)

- [x] **T-1A — SPDX do interních audio souborů.** `XactTypes.hpp`, `AudioMixer.hpp`, `XactParser.cpp`,
  `AudioMixer.cpp` → 1. řádek `// SPDX-License-Identifier: MS-PL`.
  *Accept:* všechny 4 začínají SPDX; build beze změny. (B1, X3)

- [x] **T-1B — `GetTypeName()` tečková konvence.** `AudioEngine.cpp:268`, `SoundBank.cpp:151`,
  `WaveBank.cpp:263`, `Cue.cpp:249`: `::` → `.`.
  *Accept:* test ověří `GetTypeName()=="Microsoft.Xna.Framework.Audio.<Class>"` pro všechny 4. (B2, X2)

- [x] **T-1C — Doplnit `GetTypeName()` do `Microphone`.** Třída dědí `System::Object`, ale override
  chybí. Přidat `GetTypeNameHPP()` v hpp a `GetTypeNameCPP(Microphone, "Microsoft.Xna.Framework.Audio.Microphone")` v cpp.
  *Accept:* `mic->GetTypeName()=="Microsoft.Xna.Framework.Audio.Microphone"`. (C2)

- [x] **T-1D — Mapování výjimek na `System::*` (datové/3D/mic třídy).**
  `AudioEmitter::setDopplerScaleProperty` (`AudioEmitter.cpp:26`) → `ArgumentOutOfRangeException` — hotovo.
  `Microphone::setBufferDurationProperty` → `ArgumentOutOfRangeException`;
  oba `Microphone::GetData` bounds-checky → `ArgumentException` — hotovo (2026-07-01);
  `Microphone.cpp:64,98,103` už nehází `std::out_of_range`.
  *FNA:* AudioEmitter.cs:33, Microphone.cs:62,171-179.
  *Accept:* testy ověří přesný `System::` typ pro každou chybovou větev; validní vstup nehází. (C3, X1)

- [x] **T-1E — Mapování výjimek na `System::*` (core playback).**
  SoundEffect.cpp:232,246 → `ArgumentOutOfRangeException`; :393,400,409 → `NotSupportedException`;
  :132 → `ArgumentException`. SoundEffectInstance.cpp:138 → `ObjectDisposedException` (nebo odebrat);
  :308 → `NotSupportedException`; :400 → `InvalidOperationException`.
  DynamicSoundEffectInstance.cpp:104 → `ObjectDisposedException`; :215,249 → `ArgumentOutOfRangeException`.
  *FNA:* SoundEffect.cs:82,98,412; SoundEffectInstance.cs:39,277.
  *Accept:* testy na přesný typ u každé větve. (A3, X1)

- [x] **T-1F — Mapování výjimek na `System::*` (XACT).**
  `std::invalid_argument`→`ArgumentNullException`; disposed→`ObjectDisposedException`;
  invalid-name→`InvalidOperationException`. Sites: AudioEngine.cpp:48,119,121,139,147,155;
  SoundBank.cpp:35,37,83,85,108; WaveBank.cpp:119,121; Cue.cpp:65,67,73,81,89.
  *FNA:* AudioEngine.cs:119/273, SoundBank.cs:66/174, WaveBank.cs:77, Cue.cs:170/202.
  *Accept:* testy na přesný typ (null/empty arg, invalid name, use-after-dispose). (B3, X1)

- [x] **T-1G — Rebáze tří audio výjimek na sharp-runtime báze.**
  `InstancePlayLimitException`, `NoAudioHardwareException` → `System::Runtime::InteropServices::ExternalException`;
  `NoMicrophoneConnectedException` → `System::Exception`. Odstranit ručně psané `innerException_`/
  `InnerException()` (báze už má `getInnerExceptionProperty()`); 3 ctory (default, `(message)`,
  `(message, inner)`) forwardovat do báze; default ctor bez vlastní zprávy (dle FNA).
  *FNA:* *.cs:24 u všech tří.
  *Accept:* test/`static_assert` `is_base_of<System::Exception, T>` (+ `ExternalException` u prvních dvou);
  `getMessageProperty()`/`getInnerExceptionProperty()` fungují; `catch(const System::Exception&)` chytí. (C1, X1)

- [x] **T-1H — Microphone: NOXNA/viditelnost interních členů.** `micList` a `SAMPLERATE` (FNA `internal`)
  zprivátnit a odstranit z nich `NOXNA` (jsou to FNA internals, ne CNA rozšíření). Přístup pro
  FrameworkDispatcher řešeno **bez friend** — nová veřejná `NOXNA static void CheckAllBuffers()` na
  `Microphone` zapouzdřuje null-check + iteraci + `CheckBuffer()`; `FrameworkDispatcher.cpp` teď volá
  jen `Audio::Microphone::CheckAllBuffers()` místo ručního procházení `micList`.
  *FNA:* Microphone.cs:104,120,142. *Accept:* překládá se pro všechny volající; žádné public interní členy. (C7)

### Fáze 2 — Opravy reálných chyb

- [x] **T-2A — Opravit override `DynamicSoundEffectInstance::setIsLoopedProperty`.**
  Derived signatura musí přesně odpovídat bázovým virtuálům (`const bool&` a NOXNA `bool&&`) + `override`,
  nebo sjednotit bázi na jeden kanonický setter. Dnes signatura `bool` hodnotou **skrývá** místo override,
  takže přes `SoundEffectInstance&` se volá bázová verze, která při přehrávání hází.
  *FNA:* DynamicSoundEffectInstance.cs:31-41.
  *Accept:* přes `SoundEffectInstance&` je set IsLooped na dynamické instanci no-op a `getIsLoopedProperty()==false`,
  i během přehrávání (nehází). (A1)

- [x] **T-2B — `DynamicSoundEffectInstance::Dispose()` override + sjednocení disposed flagu.**
  Override `Dispose()`: Stop → `DestroyStream()` → odregistrovat z `FrameworkDispatcher::Streams` → chain báze.
  Zrušit samostatný `disposed_`; `getIsDisposedProperty()` opřít o bázový flag.
  *FNA:* DynamicSoundEffectInstance.cs:237-247.
  *Accept:* po `Dispose()` jsou `dynamicTrack_`/`audioStream_` null a `getIsDisposedProperty()==true`;
  druhé `Dispose()` je bezpečné (idempotentní). (A2)

- [x] **T-2C — `SubmitFloatBufferEXT` guard + přepnutí formátu float/stream.**
  Házet `InvalidOperationException("Submit a float buffer before Playing!")`, pokud `State != Stopped`
  a stream je stále int-formát; `EnsureStream` musí reflektovat `isFloat_` (re-create streamu při změně
  formátu před prvním Play). Dnes S16 stream krmený F32 byty → mismatch.
  *FNA:* DynamicSoundEffectInstance.cs:194-199.
  *Accept:* float submit po Play na int-instanci hází; float-před-Play vytvoří F32LE stream; testy obě větve. (A4)

- [x] **T-2D — Opravit compact-XWB `dataLength`.** `XactParser.cpp:424-426`: délku počítat z po sobě
  jdoucích offsetů minus deviace, ne z `dev` samotného; poslední entry `segLength[4]-offset`. Také
  zkontrolovat větev `entryMetaDataSize < 24` (439-449), která dnes všem entry nastavuje celý datový segment.
  *Accept:* parser unit test na známém compact `.xwb` dá správné délky (počet PCM vzorků sedí). (B6)

- [ ] **T-2E — Zpevnit walker track-eventů.** `XactParser.cpp:122-214`: místo `break` na neznámém eventu
  (202-209) přeskakovat PITCH/VOLUME/MARKER/repeat, ať se najde první PlayWave i ve víceeventovém tracku.
  *Accept:* `.xsb` s úvodním ne-play eventem vyřeší vlnu; regresní test pro jednoduchý jedno-event track. (B7)

- [ ] **T-2F — Odstranit mrtvý XGS first-pass a redundantní re-seek.**
  Smazat `XactParser.cpp:270-289` (ponechat 292-310); `variationOffset` brát z hlavičky (ř. 536) místo
  re-seeku na `0x32` (641-647).
  *Accept:* XGS test parsuje kategorie/proměnné identicky; bez změny chování. (B8)

- [x] **T-2G — AudioCategory: opravit `Equals` + doplnit `Equals(Object)` + opravit doxygen.**
  `Equals` porovnávat dle `name_` (jako FNA name-hash), ne `parent_+index_` (`AudioCategory.cpp:43`).
  Aktualizovat hpp doxygen (`AudioCategory.hpp:25-36`) — popsat reálné směrování kategorií, ne „no-op".
  *FNA:* AudioCategory.cs:109-126.
  *Accept:* equality testy equal/unequal dle jména; konzistence `==`/`!=`/`GetHashCode`. (B10)
  *Pozn.:* `Equals(Object)` záměrně nedoplněno — `AudioCategory` nedědí `System::Object` (stejně jako
  `Vector2` a další `IEquatable`-only hodnotové typy v projektu), takže C# `Equals(object)` override
  zde nemá ekvivalent.

### Fáze 3 — Věrnost API a chování

- [x] **T-3A — Throw na neplatné jméno (XACT lookupy).**
  `AudioEngine::GetCategory` (`:131`) a `SoundBank::GetCue` (`:95-98`) místo stub-návratu házet
  `InvalidOperationException`; `AudioEngine::SetGlobalVariable` (`:158`) a `Cue::SetVariable`/`GetVariable`
  validovat jméno proti parsované sadě.
  *FNA:* AudioEngine.cs:271-276/321-326, SoundBank.cs:174, Cue.cs:200-243.
  *Accept:* test valid→úspěch, invalid→`InvalidOperationException`; `Cue::GetVariable("Distance")` na
  vestavěné proměnné nehází (doplnit built-in cue proměnné). (B4)

- [x] **T-3B — Reálné `IsInUse` pro SoundBank a WaveBank.**
  `SoundBank.cpp:71` → true, dokud hraje vlastněný cue/fire-and-forget; `WaveBank.cpp:166` → true,
  dokud hraje jím vyprodukovaná `SoundEffectInstance`.
  *FNA:* SoundBank.cs:28-36, WaveBank.cs:39-47.
  *Accept:* test: přehraj cue → `IsInUse==true`; po stop/elapse → `false`. (B5)

- [x] **T-3C — Pan/Volume sémantika v SoundEffectInstance.**
  Pan setter: `ObjectDisposedException` při disposed a `ArgumentOutOfRangeException` při `|value|>1`
  (místo tichého klampu); rozhodnout Volume klamp vs. pass-through (FNA pass-through). Pokud klamp
  zůstane jako vědomá SDL-volba, zapsat do tabulky odchylek.
  *FNA:* SoundEffectInstance.cs:46-83,124-142.
  *Accept:* chování dle rozhodnutí; testy in-range, out-of-range, disposed. (A5)

- [x] **T-3D — `RendererDetail::Equals` + doplnit doxygen.**
  Přidat `[[nodiscard]] bool Equals(const RendererDetail&) const` (dle `rendererId_`, konzistentní s `operator==`);
  doplnit `@return`/`@param` u `ToString`/`GetHashCode`/`operator==`/`operator!=`.
  *FNA:* RendererDetail.cs:48-52.
  *Accept:* `Equals` true pro stejný `RendererId`, jinak false; test equal/unequal. (C4)

- [x] **T-3E — Vyřešit `SoundEffectI`.**
  Preferováno: smazat `SoundEffectI.hpp`, `CreateInstance()` dát přímo na `SoundEffect` (bez pure-virtual),
  upravit include v SoundEffect.hpp a Cue.cpp. Pokud zachovat, přesunout do `CNA::` namespace a otagovat `NOXNA`.
  *Důvod:* není XNA API a jediný volající (`Cue.cpp:186`) drží konkrétní `SoundEffect*`.
  *Accept:* žádný neobalený non-XNA abstraktní typ v XNA namespace; build zelený; Cue se překládá. (A6)

- [ ] **T-3F — Streaming WaveBank: offset/packetSize.**
  Buď implementovat skutečný streaming ctor (`WaveBank.cpp:149-155` dnes deleguje na in-memory a
  ignoruje offset/packetSize), nebo natvrdo zdokumentovat jako akceptovanou odchylku (vše do paměti).
  *FNA:* WaveBank.cs:104-143.
  *Accept:* doložené rozhodnutí; pokud implementováno, test na korektní offsetované čtení. (B z #3)

- [ ] **T-3G — SoundEffect: instance-tracking + Dispose-kaskáda (rozhodnutí).**
  Buď evidovat živé instance (weak refs) a `SoundEffect::Dispose()` je zastaví/disposne (FNA),
  nebo formálně zdokumentovat hodnotovou odchylku (bez kaskády) v CHECKLIST. Provázáno s rozhodnutím
  `CreateInstance` hodnotou vs. heap-ref a `FromStream` ownership.
  *FNA:* SoundEffect.cs:126,315-323,354,389.
  *Accept:* doložené rozhodnutí; pokud tracking, dispose SoundEffectu zastaví jeho instanci (test). (A7)

### Fáze 4 — Dokončení funkcí (žádné stuby bez důvodu)

- [ ] **T-4A — Reálný microphone capture přes SDL3 (Stub → Full).**
  `getAllProperty` enumerovat recording zařízení; `Start`/`Stop` otevřít/zavřít `SDL_AudioStream`
  (44100/mono/S16); `GetData` číst z `SDL_GetAudioStreamData`, `GetQueuedBytes` z `SDL_GetAudioStreamAvailable`;
  zachovat `CheckBuffer`/`BufferReady`. `GetSampleDuration`/`GetSampleSizeInBytes` delegovat na `SoundEffect`.
  *FNA:* Microphone.cs (FNAPlatform.GetMicrophones/GetMicrophoneSamples/.../Start/Stop).
  *Accept:* s capture zařízením je `All` neprázdné, `Start`→`GetData` vrací >0 B po zvuku, `State` přechází
  korektně; bez zařízení elegantně prázdné. Testy stavového automatu + bounds (capture-závislé asserty skippovatelné v CI). (C5)

- [ ] **T-4B — 3D pan/attenuace pro SDL_mixer (Apply3D / 3D PlayCue / Cue::Apply3D).**
  Místo no-opů odvodit pan + distance-attenuaci z geometrie listener/emitter (`Mix_SetPosition`).
  Z `Cue::Apply3D` (`Cue.cpp:79-83`) odstranit disposed-`std::runtime_error` ve prospěch `ObjectDisposedException`.
  Doppler zůstává neaplikovaný (akceptovaná odchylka §2.2).
  *FNA:* SoundBank.cs:248-263, Cue.cs:166-186, SoundEffectInstance.cs:266-298.
  *Accept:* Apply3D/3D-PlayCue na validním vstupu nehází a nekrachuje; volume/pan se mění se vzdáleností/úhlem (test geometrie). (B9, A-Apply3D)

- [ ] **T-4C — Interní DSP filtry/reverb routing v SoundEffectInstance.**
  Přidat `INTERNAL_applyReverb`/`applyLowPassFilter`/`applyHighPassFilter`/`applyBandPassFilter`
  (private/detail), aby volající z Cue/AudioEngine seděli; implementovat přes SDL_mixer kde to jde,
  jinak doložený no-op.
  *FNA:* SoundEffectInstance.cs:488,518,536,554.
  *Accept:* volající se překládají; chování (reálné/no-op) v tabulce odchylek. (A8)

- [ ] **T-4D — AudioEngine `Update()` / per-cue přepočet kategorií.**
  Posoudit, co z FACT `DoWork` je potřeba (fade kategorií, instance-limity); minimálně dokončit
  re-apply smyčku hlasitosti kategorie (`AudioEngine.cpp:219-223` má prázdné tělo), aby změna hlasitosti
  ovlivnila i běžící cue, ne jen budoucí.
  *FNA:* AudioEngine.cs:337 (+ kategorie/fade).
  *Accept:* změna hlasitosti kategorie ovlivní právě hrající cue (test); dokumentováno, co zůstává no-op. (B z AudioEngine §1)

### Fáze 5 — Kompletní testovací sada (Google Test)

> Pravidla z `CLAUDE.md`/`CHECKLIST.md`: každá veřejná metoda, **každý overload**, operátor a konstanta
> má ≥1 test; out-ref/array overloady testovat zvlášť; `==`/`!=`/`Equals` equal i unequal; `ToString`
> formát; `GetHashCode` konzistence; `GetTypeName` přesnou hodnotu.

- [x] **T-5A — SoundEffectTests** — oba ctory; `GetSampleDuration`/`GetSampleSizeInBytes` (round-trip + nula);
  `Duration`; `Name` get/set (+ move overload); `IsDisposed`; static `MasterVolume`/`DistanceScale`(≤0 hází)/
  `DopplerScale`(<0 hází)/`SpeedOfSound`; `CreateInstance`; `Play()`/`Play(v,p,pan)`; `Dispose` idempotentní;
  `FromStream` (validní wave + ne-wave `NotSupportedException` + prázdný). (A10)

- [x] **T-5B — SoundEffectInstanceTests** — `Play`/`Pause`/`Resume`/`Stop`/`Stop(bool)`; přechody `State`;
  `Volume`/`Pan`/`Pitch` get/set (+ move overloady, range); `IsLooped` get/set (+ throw-while-started);
  `Apply3D(listener,emitter)`; `Apply3D(array,count)` (count==1 + >1 `NotSupportedException`); `IsDisposed`;
  `Dispose` idempotentní; `GetTypeName`. (A10)

- [x] **T-5C — DynamicSoundEffectInstanceTests** — ctor; `PendingBufferCount`; `IsLooped` vždy false + setter
  no-op (přes bázovou ref, viz T-2A); `GetSampleDuration`/`GetSampleSizeInBytes`; `Play`/`Stop`;
  `SubmitBuffer` ×2 (+ range); `SubmitFloatBufferEXT` ×2 (+ range + `InvalidOperationException` guard, T-2C);
  `BufferNeeded` při hladovění; `Dispose` (T-2B); `GetTypeName`; `Update`. (A10)

- [x] **T-5D — SoundStateTests** — hodnoty a pořadí Playing/Paused/Stopped. (A10)

- [x] **T-5E — AudioEngineTests** — `ContentVersion`; oba ctory; `IsDisposed`; `RendererDetails`;
  `GetCategory` (valid+invalid); `GetGlobalVariable`/`SetGlobalVariable` (valid+invalid); `Update`;
  `Dispose`+`Disposing` event; `GetTypeName`. (B11)

- [x] **T-5F — SoundBankTests** — ctor (valid/null/empty); `IsDisposed`; `IsInUse`; `GetCue` (valid+invalid);
  `PlayCue` (2-arg) a `PlayCue` (3-arg) zvlášť; `Dispose`+event; `GetTypeName`. (B11)

- [x] **T-5G — WaveBankTests** — oba ctory zvlášť; `IsDisposed`/`IsPrepared`/`IsInUse`; `Dispose`+event; `GetTypeName`. (B11)

- [x] **T-5H — CueTests** — všech 9 stav/Name properties; `Apply3D`; `GetVariable`/`SetVariable` (valid+invalid,
  mimo sadu); `Play`/`Pause`/`Resume`/`Stop(AsAuthored)` a `Stop(Immediate)` zvlášť; `Dispose`+event; `GetTypeName`. (B11)

- [x] **T-5I — AudioCategoryTests** — `Name`; `Pause`/`Resume`/`SetVolume`/`Stop` (obě options);
  `Equals` equal+unequal; `GetHashCode` konzistence; `operator==`/`operator!=`. (B11)

- [x] **T-5J — AudioEmitterTests / AudioListenerTests** — defaulty (DopplerScale 1.0, Forward/Up/Zero);
  každý getter/setter round-trip; negativní DopplerScale hází. (C6)

- [x] **T-5K — Enum testy** — `AudioChannels`, `AudioStopOptions`, `MicrophoneState` (číselné hodnoty). (C6)

- [x] **T-5L — RendererDetailTests** — `ToString`, `GetHashCode` konzistence, `operator==`/`!=`, `Equals` equal+unequal. (C6)

- [x] **T-5M — MicrophoneTests** — `Default` null-safe při prázdném `All`; `BufferDuration` valid/invalid;
  `GetSampleDuration`/`GetSampleSizeInBytes` round-trip; `GetData` bounds; `GetTypeName`; stavový automat (T-4A). (C6)
  *Pozn.:* stavový automat testován jen v rozsahu dnešní implementace (Start/Stop mění `State`); reálný
  capture-driven stavový přechod čeká na T-4A.

- [x] **T-5N — Exception testy** — pro `InstancePlayLimitException`/`NoAudioHardwareException`/
  `NoMicrophoneConnectedException`: všechny 3 ctory, base-of asserty, inner-exception round-trip. (C1)

- [x] **T-5O — XactParser testy (NOXNA internal)** — round-trip parse minimálních `.xgs`/`.xsb`/`.xwb`
  fixtur: počty kategorií/proměnných/cue/entry + jedna délka PCM vzorku (regrese na T-2D/T-2E). (B11)

### Fáze 6 — Dokumentace a uzávěrka

- [ ] **T-6A — Tabulka akceptovaných odchylek v `CHECKLIST.md`** — doplnit body z §2 (3D HRTF, Doppler,
  streaming wavebank, hodnotová `CreateInstance`, GetHashCode int).
- [ ] **T-6B — Aktualizovat `AUDIT.md`** — nahradit paušální „✅ / stub behavior" reálným stavem po každé fázi
  (per soubor: implementováno / akceptovaná odchylka / testy).
- [ ] **T-6C — Build & report** — `cmake --build cmake-build-debug --target CNA` a `--target CnaTests`
  zelené; krátký report (změněné soubory, odchylky, zbývající mezery) dle `CLAUDE.md` §Build and Report.

---

## 5. Doporučené pořadí a milníky

1. **M0 (rozjezd):** T-0A.
2. **M1 (compliance, rychlé výhry):** T-1A…T-1H — typy výjimek, `GetTypeName`, SPDX, viditelnost.
   *Nízké riziko, čistě mechanické, odemkne smysluplné testy.*
3. **M2 (reálné bugy):** T-2A…T-2G — override/Dispose v Dynamic, float guard, XACT parser bugy, AudioCategory.
   *Nejvyšší hodnota — opravuje skutečně vadné chování.*
4. **M3 (věrnost API):** T-3A…T-3G — throw-on-invalid-name, IsInUse, Pan/Volume, RendererDetail, SoundEffectI, rozhodnutí o sémantice.
5. **M4 (funkce):** T-4A…T-4D — mic capture, 3D pan/attenuace, DSP routing, AudioEngine Update.
6. **M5 (testy):** T-5A…T-5O — kompletní pokrytí (průběžně už od M1, finální konsolidace zde).
7. **M6 (uzávěrka):** T-6A…T-6C.

> Doporučení: testy nepsat až v M5 — ke každému úkolu z M1–M4 přidávat příslušné testy z fáze 5 hned
> (princip „make and forget" z `CLAUDE.md` — soubor hotový v jednom průchodu, včetně testů).

---

## 6. Souhrn rizik / otevřená rozhodnutí

| ID | Otázka k rozhodnutí | Default doporučení |
|----|---------------------|--------------------|
| D1 | `CreateInstance`/`FromStream`: hodnota vs. heap-reference + instance-tracking (T-3G) | Ponechat hodnotu, **zdokumentovat** odchylku; tracking jen pokud demo/hra vyžaduje Dispose-kaskádu |
| D2 | Pan/Volume klamp vs. throw/pass-through (T-3C) | Sladit s FNA (throw na range, pass-through volume); klamp jen vědomě + do CHECKLIST |
| D3 | Streaming WaveBank (T-3F) | Zatím doložená odchylka (vše do paměti); skutečný streaming jako pozdější úkol |
| D4 | Rozsah `AudioEngine::Update` / FACT DoWork (T-4D) | Minimálně per-cue volume re-apply; zbytek dokumentovaně no-op |

---

*Vygenerováno na základě tří paralelních line-by-line auditů proti FNA (clustery: core playback,
XACT, 3D/mic/enumy/výjimky). Pokrývá pouze `Microsoft::Xna::Framework::Audio` + `CNA::Internal::Audio`.*
