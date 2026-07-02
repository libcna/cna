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

> **Poznámka (2026-07-02):** tato tabulka je snímek z **původního** auditu (před fázemi 1–5).
> Řádky se od té doby z velké části opravily — pro aktuální stav věř zaškrtávátkům v §4, ne
> sloupci „Stav" zde (výjimka: řádek 15 Microphone je opraven níže, protože se ho přímo týkal
> nejnovější audit). Nové nálezy z doplňkového auditu (2026-07-02) jsou ve **Fázi 7**.

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
| 15 | Microphone | 217 | Funkční, T-4A hotové s výhradou | reálný SDL3 capture (enumerace/Start/Stop/GetData) hotový; `GetSampleDuration`/`GetSampleSizeInBytes` **nedeleguje** na `SoundEffect` jak T-4A požadovalo (MC-1); zastaralý `friend class MicrophoneFactory` komentář (MC-2) |
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

- [x] **T-2E — Zpevnit walker track-eventů.** `XactParser.cpp:122-214`: místo `break` na neznámém eventu
  (202-209) přeskakovat PITCH/VOLUME/MARKER/repeat, ať se najde první PlayWave i ve víceeventovém tracku.
  *Accept:* `.xsb` s úvodním ne-play eventem vyřeší vlnu; regresní test pro jednoduchý jedno-event track. (B7)
  *Pozn.:* přesné délky PITCH/VOLUME(REPEATING)/MARKER(REPEATING) eventů ověřeny proti FAudio
  (`FACT_internal.c:2390-2432`); pouze skutečně neznámý typ eventu dál dělá `break`.

- [x] **T-2F — Odstranit mrtvý XGS first-pass a redundantní re-seek.**
  Smazat `XactParser.cpp:270-289` (ponechat 292-310); `variationOffset` brát z hlavičky (ř. 536) místo
  re-seeku na `0x32` (641-647).
  *Accept:* XGS test parsuje kategorie/proměnné identicky; bez změny chování. (B8)
  *Pozn.:* nový `XactParserTest.XgsParsesCategoryAndVariable` ověřen na PŮVODNÍM (pre-cleanup) i novém
  kódu se stejným výsledkem (`git stash`) — potvrzeno bez změny chování.

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

- [x] **T-4A — Reálný microphone capture přes SDL3 (Stub → Full).** *(hotovo 2026-07-02, commity
  `d63946d`/`75bbf4a`/`afcf63c`/`435ff76`, viz `NEXT.md` §3; jedno accept-kritérium ale zůstalo
  nesplněné — viz výhrada níže.)*
  `getAllProperty` enumerovat recording zařízení; `Start`/`Stop` otevřít/zavřít `SDL_AudioStream`
  (44100/mono/S16); `GetData` číst z `SDL_GetAudioStreamData`, `GetQueuedBytes` z `SDL_GetAudioStreamAvailable`;
  zachovat `CheckBuffer`/`BufferReady`. ~~`GetSampleDuration`/`GetSampleSizeInBytes` delegovat na `SoundEffect`.~~
  **Tato poslední věta implementována NEBYLA** — `Microphone::GetSampleDuration`/`GetSampleSizeInBytes`
  mají vlastní vzorec bez zaokrouhlení na celé ms jako `SoundEffect`, takže se numericky liší od FNA
  u nedělitelných hodnot. Sledováno jako **MC-1** ve Fázi 7 — dodělat tam, ne zde.
  *FNA:* Microphone.cs (FNAPlatform.GetMicrophones/GetMicrophoneSamples/.../Start/Stop).
  *Accept:* s capture zařízením je `All` neprázdné, `Start`→`GetData` vrací >0 B po zvuku, `State` přechází
  korektně; bez zařízení elegantně prázdné. Testy stavového automatu + bounds (capture-závislé asserty skippovatelné v CI). (C5)
  *Pozn.:* ostatní accept kritéria splněna a ověřena i na reálném hardwaru (2 skutečné mikrofony
  přes pulseaudio na vývojovém stroji), viz `NEXT.md` §3.

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

- [x] **T-6A — Tabulka akceptovaných odchylek v `CHECKLIST.md`** — doplnit body z §2 (3D HRTF, Doppler,
  streaming wavebank, hodnotová `CreateInstance`, GetHashCode int). *(hotovo 2026-07-02, viz §7 dodatek.)*
- [x] **T-6B — Aktualizovat `AUDIT.md`** — nahradit paušální „✅ / stub behavior" reálným stavem po každé fázi
  (per soubor: implementováno / akceptovaná odchylka / testy). *(hotovo 2026-07-02, viz §7 dodatek.)*
- [ ] **T-6C — Build & report** — `cmake --build cmake-build-debug --target CNA` a `--target CnaTests`
  zelené; krátký report (změněné soubory, odchylky, zbývající mezery) dle `CLAUDE.md` §Build and Report.

### Fáze 7 — Doplňkový audit (2026-07-02): nové nálezy nad rámec Fází 0–6

> Po dokončení T-4A (real microphone capture, viz T-4A výše) proběhl **čerstvý** line-by-line audit
> celého `Microsoft::Xna::Framework::Audio` clusteru proti FNA — 4 paralelní kontroly (Core Playback,
> XACT, interní backend, Mic/data/enumy/výjimky). Cílem bylo jednak ověřit stav již otevřených úkolů
> (T-3F, T-3G, T-4B, T-4C, T-4D, T-6A, T-6B) proti aktuálnímu zdroji, jednak najít **nové**, dosud
> nezachycené bugy a mezery. Toto jsou pouze **nové** nálezy — položky T-3F/T-3G/T-4B/T-4C/T-4D se
> ukázaly stále aktuální beze změny a zůstávají zapsané výše, ne duplicitně zde.
>
> ID mají prefix podle clusteru (`CP`=core playback, `XA`=XACT, `IN`=interní backend,
> `MC`=mic/data/enumy/výjimky), aby nekolidovaly s `T-*`. Řazeno uvnitř clusteru podle závažnosti
> (reálné bugy → compliance/chování → testovací mezery). `AudioEmitter`, `AudioListener`,
> `AudioChannels`, `AudioStopOptions`, `MicrophoneState` a všechny 3 audio výjimky prošly auditem
> **bez nálezu** — zkontrolovány řádek po řádku proti FNA, plně kompatibilní, žádné nové úkoly.

**Nejzávažnější nálezy (rychlý přehled, detaily níže):**
- **IN-1** — špatné přeskakování DSP bloku v `.xsb` parseru může tiše rozjet čtení pozice pro
  *všechny* další zvuky v souboru (kaskádová korupce, ne pád).
- **CP-1** — `SoundEffectInstance::Play()` není idempotentní; opakované volání za běhu restartuje
  přehrávání od začátku místo no-opu.
- **CP-3** — `Apply3D` tiše přepisuje veřejné `Volume`/`Pan`; po zavolání už `getVolumeProperty()`
  nevrací hodnotu, kterou uživatel nastavil.
- **CP-7** — `SoundEffectInstance` drží syrový `const SoundEffect*` na rodiče bez správy životnosti
  → dangling pointer při běžném řetězení (`SoundEffect(...).CreateInstance()`).
- **XA-1** — `SoundBank::PlayCue` maže fire-and-forget cues podle uplynulého času (5 s), ne podle
  toho, jestli ještě hrají — dlouhé zvuky/hudba se nuceně přeruší.
- **XA-2** — `WaveBank::GetSoundEffect` leakuje heap-alokovaný `SoundEffect` pro 8-bit PCM a ADPCM
  položky.
- **IN-2** — over-read u non-compact `.xwb` entries s `entryMetaDataSize < 24` (čte cizí paměť).
- **MC-1** — T-4A ve skutečnosti nesplnilo vlastní accept-kritérium: `GetSampleDuration`/
  `GetSampleSizeInBytes` nedeleguje na `SoundEffect` jak bylo zadáno.

#### 7.1 Core Playback (SoundEffect, SoundEffectInstance, DynamicSoundEffectInstance)

- [ ] **CP-1 — `SoundEffectInstance::Play()` není idempotentní při opakovaném volání za běhu.**
  FNA má explicitní `if (State == SoundState.Playing) { return; }` na začátku `Play()`. CNA tuto
  kontrolu nemá — opakované `Play()` na již hrající instanci znovu volá `MIX_SetTrackAudio`/
  `MIX_PlayTrack`, čímž restartuje přehrávání od začátku místo no-opu.
  *FNA:* SoundEffectInstance.cs:282-285.
  *CNA:* SoundEffectInstance.cpp:143-232 (chybí guard).
  *Accept:* opakované `Play()` během `State==Playing` nezmění pozici přehrávání (test s odposlechem,
  že se track nepřehraje od nuly / že se nevolá znovu `MIX_SetTrackAudio`).

- [ ] **CP-2 — `SoundEffect::Play(volume, pitch, pan)` nevaliduje/neklampuje pan a pitch.**
  FNA interně dělá `instance.Pitch = pitch;` (klamp na [-1,1]) a `instance.Pan = pan;` (hází
  `ArgumentOutOfRangeException` při `|pan|>1`) ještě před `instance.Play()`. CNA počítá stereo
  gains a frequency ratio přímo ze syrových vstupů bez klampu/validace.
  *FNA:* SoundEffect.cs:338-352, SoundEffectInstance.cs:46-65,87-101.
  *CNA:* SoundEffect.cpp:254-310.
  *Accept:* `Play(v, pitch>1, pan>1)` hází `ArgumentOutOfRangeException` (pan) resp. klampuje pitch
  na [-1,1] stejně jako property setter; test na obě větve.

- [ ] **CP-3 — `Apply3D(listener, emitter)` přepisuje veřejné `Volume`/`Pan`, místo aby počítal jen
  samostatnou výstupní matici jako FNA.** FNA aktualizuje pouze interní `dspSettings`, getterů
  `Volume`/`Pan` se 3D pozicování vůbec nedotýká. CNA volá `setVolumeProperty(...)`/
  `setPanProperty(...)` přímo, takže po `Apply3D` už `getVolumeProperty()`/`getPanProperty()`
  nevrací hodnotu, kterou uživatel nastavil.
  *FNA:* SoundEffectInstance.cs:221-264 (bez zásahu do `INTERNAL_pan`/`INTERNAL_volume`).
  *CNA:* SoundEffectInstance.cpp:289-310.
  *Accept:* po `setVolumeProperty(X)` + `Apply3D(...)` je `getVolumeProperty()==X` (3D atenuace se
  aplikuje jinam, ne na veřejnou property); testy na obě.

- [ ] **CP-4 — `DynamicSoundEffectInstance::PendingBufferCount`/`Update()` mění sémantiku oproti FNA
  — počet se nuluje ihned po předání dat do SDL streamu, ne až po skutečném přehrání hardwarem.**
  `SubmitQueuedToStream()` vyprázdní `queuedBuffers_` při každém volání (i uvnitř `Update()`), takže
  `getPendingBufferCountProperty()` je těsně po submitu prakticky vždy 0 → `Update()` pak spouští
  `BufferNeeded` prakticky při každém tiku bez ohledu na skutečný stav bufferu.
  *FNA:* DynamicSoundEffectInstance.cs:23-29,290-322.
  *CNA:* DynamicSoundEffectInstance.cpp:47-51,312-331,374-395.
  *Accept:* `PendingBufferCount` odráží data ještě nespotřebovaná (dotaz na skutečnou frontu SDL
  streamu, ne lokální seznam); `BufferNeeded` se nespouští při dostatku dat ve streamu (test na
  frekvenci volání).

- [ ] **CP-5 — `Stop(bool immediate=false)` na `DynamicSoundEffectInstance` tiše no-opuje místo
  hození `InvalidOperationException`.** FNA v `Stop(bool)` explicitně hází, pokud
  `isDynamic && !immediate`. CNA `Stop(bool)` není virtuální a pracuje jen s bázovým `track_`, který
  dynamické instance nikdy nepoužívají.
  *FNA:* SoundEffectInstance.cs:404-439 (throw ř. ~435).
  *CNA:* SoundEffectInstance.cpp:239-261 (chybí `isDynamic`/virtual dispatch).
  *Accept:* `dynamicInstance.Stop(false)` hází `System::InvalidOperationException`; test.

- [ ] **CP-6 — `DynamicSoundEffectInstance::GetSampleDuration`/`GetSampleSizeInBytes` počítají
  skutečné bajty/vzorek (`isFloat_ ? 4 : 2`) místo FNA napevno předpokládaných 16 bitů.** FNA obě
  metody vždy deleguje na `SoundEffect.GetSampleDuration/GetSampleSizeInBytes`, které natvrdo
  dělí/násobí 2 bez ohledu na float-mód. CNA po `SubmitFloatBufferEXT` vrací jiný výsledek než FNA.
  *FNA:* DynamicSoundEffectInstance.cs:114-130; SoundEffect.cs:363-374.
  *CNA:* DynamicSoundEffectInstance.cpp:79-96,335-339.
  *Accept:* po `SubmitFloatBufferEXT` dá `GetSampleDuration`/`GetSampleSizeInBytes` číselně stejný
  výsledek jako FNA (stále děleno/násobeno 2, ne 4); regresní test na float instanci.

- [ ] **CP-7 — `SoundEffectInstance` drží syrový `const SoundEffect*` na rodiče bez správy
  životnosti — dangling pointer u běžného řetězení.** `SoundEffect(path).CreateInstance()` (nebo
  `SoundEffect::FromStream(s)->CreateInstance()` na dereferencovaném dočasném objektu) vytvoří
  instanci ukazující na již zaniklý `SoundEffect`; následné `Play()` čte
  `soundEffect_->getNativeAudioHandle()` na uvolněné paměti. Souvisí s T-3G (hodnotová sémantika),
  ale je to konkrétní bezpečnostní riziko nad rámec obecného textu T-3G.
  *FNA:* SoundEffect.cs:126,354 (reference semantics, GC drží objekt naživu).
  *CNA:* SoundEffectInstance.hpp:32; SoundEffectInstance.cpp:69-72.
  *Accept:* buď dokumentovaná kontraktová podmínka (owner musí přežít instanci) v Doxygenu
  `CreateInstance()`/ctoru, nebo oprava na sdílené vlastnictví; ASAN test na dangling scénář.

- [ ] **CP-8 — `SoundEffect` nededí `System::Object` a nemá `GetTypeName()`, na rozdíl od
  sourozeneckých tříd.** `SoundEffectInstance`, `DynamicSoundEffectInstance`, `AudioEngine`,
  `SoundBank`, `WaveBank`, `Cue` všechny dědí `System::Object` a mají `GetTypeNameHPP()`/
  `GetTypeNameCPP()`. `SoundEffect` dědí jen `System::IDisposable`.
  *FNA:* SoundEffect.cs:20 (implicitní `object`).
  *CNA:* SoundEffect.hpp:19.
  *Accept:* `SoundEffect : public System::Object, public System::IDisposable`;
  `GetTypeName()=="Microsoft.Xna.Framework.Audio.SoundEffect"`; test.

- [ ] **CP-9 — Konstruktor `SoundEffectInstance(const SoundEffect&)` je veřejný, FNA má ekvivalentní
  ctor `internal`.** Podle `CLAUDE.md` (Visibility Mapping) má `internal` mapovat na
  `private`/`protected`/friend-scoped, ne na `public`. Třída už deklaruje `friend class SoundEffect;`,
  takže zprivátnění je bezbolestné.
  *FNA:* SoundEffectInstance.cs:174 (`internal SoundEffectInstance(...)`).
  *CNA:* SoundEffectInstance.hpp:45.
  *Accept:* ctor `private`/`protected`; `SoundEffect::CreateInstance()` (friend) se dál překládá;
  přímá konstrukce zvenčí se nepřekládá (kompilační negativní test/komentář).

- [ ] **CP-10 — Chybí test pro NOXNA ctor `SoundEffect(const std::string& assetName)` (načtení ze
  souboru).** Pokrytý je jen buffer-ctor a `FromStream`; ctor z cesty k souboru nemá žádný test.
  *CNA:* SoundEffect.hpp:48; tests/…/SoundEffectTests.cpp.
  *Accept:* test na prázdný string (no-op), neexistující soubor (throw, headless-safe pod
  `GTEST_SKIP` při chybějícím zařízení).

- [ ] **CP-11 — T-5A tvrdí pokrytí „validní wave" pro `FromStream`, ale test na úspěšné načtení
  chybí.** `SoundEffectTests.cpp` má jen testy na prázdný/garbage vstup (throw) — žádný test
  nenačítá skutečná platná WAV data a neověřuje úspěšný návrat/`Duration`.
  *CNA:* tests/…/SoundEffectTests.cpp:125-149.
  *Accept:* test s minimální validní WAV fixturou (in-memory PCM header) ověří úspěšné `FromStream`
  + `getDurationProperty() > 0`.

- [ ] **CP-12 — Chybí testy pro move-konstruktor a move-assignment `SoundEffectInstance` (NOXNA
  veřejné členy).** `CLAUDE.md` požaduje test pro každou veřejnou metodu/operátor.
  *CNA:* SoundEffectInstance.cpp:82-128; tests/…/SoundEffectInstanceTests.cpp.
  *Accept:* test ověří přenos `track_`/`State_`/vlastností a že zdrojový objekt po move je bezpečně
  disposed-like (žádný double-free při destrukci).

- [ ] **CP-13 — Chybí testy pro `Stop(false)` (non-immediate) na `SoundEffectInstance` i
  `DynamicSoundEffectInstance`.** Testováno je jen `Stop()`/`Stop(true)`.
  *CNA:* tests/…/SoundEffectInstanceTests.cpp, tests/…/DynamicSoundEffectInstanceTests.cpp.
  *Accept:* test na statické instanci (`Stop(false)` nechá doznít smyčku) a na dynamické
  (`Stop(false)` hází po opravě CP-5).

- [ ] **CP-14 — Chybí regresní test na opakované `Play()` během `State==Playing`.** Přímo by
  odhalil CP-1; dnešní test volá `Play()` jen jednou.
  *CNA:* tests/…/SoundEffectInstanceTests.cpp:122-130.
  *Accept:* test zavolá `Play()` dvakrát za sebou a ověří, že stav zůstává `Playing` bez restartu.

#### 7.2 XACT (AudioEngine, SoundBank, WaveBank, Cue, AudioCategory, RendererDetail)

- [ ] **XA-1 — `SoundBank::PlayCue` čistí fire-and-forget cues podle uplynulého času, ne podle
  stavu přehrávání — dlouhé zvuky se přeruší.** `fireAndForget_` se ve `PlayCue` maže podmínkou
  `now - faf.created >= 5s` bez ohledu na to, zda `faf.cue->getIsPlayingProperty()` je stále `true`
  (na rozdíl od `getIsInUseProperty()`, která správně kontroluje `IsPlaying`). Jakýkoli
  fire-and-forget cue/hudba delší než 5 s se při dalším `PlayCue` na stejné bance nuceně
  zastaví/zničí, i když ještě hraje.
  *FNA:* SoundBank.cs:28-36 (`IsInUse` dle skutečného stavu), SoundBank.cs:105-119 (destruktor drží
  objekt naživu, dokud `IsInUse`).
  *CNA:* SoundBank.cpp:116-135.
  *Accept:* sweep podmínka se změní na „již nehraje" (`!faf.cue->getIsPlayingProperty()`), případně
  kombinace se safety-net timeoutem (řádově minuty, ne 5 s); test simulující dlouho hrající cue
  prokazující, že se nezastaví předčasně.

- [ ] **XA-2 — `WaveBank::GetSoundEffect` uniká heap-alokovaný `SoundEffect` z `FromStream` pro
  8-bit PCM a ADPCM vlny.** `cached.emplace(*SoundEffect::FromStream(ss))` dereferencuje
  `SoundEffect*` vrácený z `new SoundEffect(...)` a zkopíruje ho do `std::optional`; původní heap
  objekt se nikdy neuvolní (`delete` chybí). Leak nastává při každém prvním přístupu k libovolné
  8-bit PCM nebo ADPCM položce ve wavebance.
  *CNA:* WaveBank.cpp:253, WaveBank.cpp:263.
  *Accept:* výsledek `FromStream` se buď obalí do `std::unique_ptr` a přesune/zkopíruje bez leaku,
  nebo se hned po zkopírování do `cached` smaže; test (ASan/leak-check) prokazující, že opakované
  `GetSoundEffect` na stejném indexu nedělá nový leak.

- [ ] **XA-3 — `Cue::Play` ignoruje autorské `weightMin`/`weightMax` a typ výběru variace, vždy
  vybírá uniformně náhodně.** `XsbVariEntry::weightMin/weightMax` jsou parserem načtené, ale
  `Cue::Play` je nikde nepoužívá — vždy použije `std::uniform_int_distribution` přes všechny
  entries místo vážené pravděpodobnosti. Chybí i rozlišení ostatních XACT variation types
  (Ordered/OrderedFromRandom/RandomNoImmediateRepeats/Shuffle) — `var.lastSelected` je deklarováno,
  ale nikde se nečte ani nezapisuje.
  *CNA:* Cue.cpp:139-197, XactTypes.hpp:88-105.
  *Accept:* výběr respektuje `weightMin`/`weightMax` (vážený náhodný výběr) alespoň pro náhodné
  typy; pro ne-náhodné typy implementace nebo zdokumentovaná odchylka; test ověřující, že entry s
  vahou blížící se 100 je vybírána statisticky výrazně častěji.

- [ ] **XA-4 — `AudioEngine` dvouparametrový konstruktor tiše zahazuje `lookAheadTime` i
  `rendererId` bez zdokumentované odchylky.** Oba parametry jsou zakomentované a nikam se
  nepoužijí, ale doxygen je popisuje, jako by měly efekt.
  *FNA:* AudioEngine.cs:112-225.
  *CNA:* AudioEngine.cpp:46-54; AudioEngine.hpp:43-52.
  *Accept:* buď `rendererId` použít k výběru mezi budoucími backend-rendery, nebo minimálně doplnit
  `//` komentář v `.cpp` a upravit doxygen v `.hpp`; test ověřující, že konstruktor s libovolným
  `rendererId`/`lookAheadTime` nehází a chová se stejně jako jednoparametrový ctor.

- [ ] **XA-5 — Testy `AudioCategory`/`Cue` neověřují reálný efekt `Pause`/`Resume`/`Stop`/
  `SetVolume` na běžící cue.** Testováno je jen `EXPECT_NO_THROW` bez jakéhokoli aktivního `Cue` v
  kategorii — přestože `AudioCategory.hpp` explicitně dokumentuje, že tyto metody „route to every
  currently active Cue... and have a real, immediate effect on playback".
  *CNA:* tests/.../AudioCategoryTests.cpp:130-160.
  *Accept:* nový test vytvoří `SoundBank`+`Cue` ve fixture s kategorií, zavolá `cue->Play()`, pak
  `category.Pause()`/`.Stop()`/`.SetVolume()` a ověří přes `getIsPausedProperty()`/
  `getIsStoppedProperty()`, že efekt skutečně nastal.

#### 7.3 Interní backend (XactParser, XactTypes, AudioMixer)

> `AudioMixer.cpp`'s natvrdo 44100 Hz/stereo/S16 pro sdílené mixer zařízení byl výslovně
> prověřen a **není to bug** — `MIX_LoadRawAudio`/`MIX_LoadAudio_IO` nastavují per-audio
> `SDL_AudioSpec.freq` na skutečný `entry.sampleRate` a SDL3_mixer převzorkovává za chodu.

- [x] **IN-1 — Chybné přeskakování DSP bloku ve `ParseXsb`.** *(hotovo 2026-07-02.)* Parser čte 2bajtové pole a přeskakuje
  `dspLen - 2` bajtů, jako by šlo o self-inclusive délku (stejný vzor jako u RPC bloku o pár řádků
  výš, kde to je správně). Ale podle FAudia (`FACT_internal.c:2650-2661`) DSP blok **nikdy**
  nepoužívá počáteční pole jako délku ke skoku — je explicitně označené "unused"; místo toho se čte
  `dspCodeCount` (1 B) a pak `dspCodeCount*4` B kódů. Pokud reálné `.xsb` soubory mají
  `SOUND_FLAG_HAS_DSP` (0x10) nastavený a hodnota pole neodpovídá `1+4*count`, kurzor se rozjede a
  **všechny další zvuky v souboru** se čtou z posunuté pozice — tichá, kaskádovitá korupce, ne pád.
  Cesta navíc není vůbec pokryta testem.
  *Soubor:* src/CNA/Internal/Audio/XactParser.cpp:650-656 (srov. FAudio/src/FACT_internal.c:2650-2661).
  *Accept:* přepsat na `count = sc.u8(); for(count) sc.u32();` (zahodit prvních 2 B jako unused);
  regresní test s `SOUND_FLAG_HAS_DSP` nastaveným ověřující, že další zvuk v souboru se naparsuje
  správně.
  *Pozn.:* nový `XactParserTest.DspBlockIsSkippedByCodeCountNotByLengthField` ověřen na PŮVODNÍM
  (pre-fix) kódu přes `git stash` — bez opravy test selže (dokonce hodí "read past end", ne jen
  špatná data), s opravou projde. `dspCodeCount`-based skip nahradil starý délkově-založený skip.

- [ ] **IN-2 — Over-read u non-compact XWB entry s `entryMetaDataSize < 24`.** Kód vždy provede
  všech 6 `u32()` čtení (24 B) *před* kontrolou `entryMetaDataSize < 24`, teprve pak kurzor přesune
  zpět. Pro starší (užší) formáty tak `loopStart`/`loopTotal` obsahují bajty z cizí paměti (dat
  další entry, nebo mimo segment u posledního záznamu — riziko výjimky "read past end" u konce
  bufferu). FAudio čte přesně `dwEntryMetaDataElementSize` bajtů a zbytek nechává vynulovaný.
  *Soubor:* src/CNA/Internal/Audio/XactParser.cpp:458-476.
  *Accept:* číst pole podmíněně/omezeně na `entryMetaDataSize`, ne natvrdo všech 24 B; test s
  non-compact `.xwb` fixture kde `entryMetaDataSize < 24` (dnešní testy pokrývají jen compact
  formát).

- [ ] **IN-3 — Integer underflow v compact-XWB `dataLength` může obejít bounds-check ve
  `WaveBank.cpp`.** Výpočet `rawOffsetUnits[i+1]*alignment - offset - deviations[i]` je `uint32_t`
  aritmetika bez guardu — u poškozeného/adverzního souboru může podteče na hodnotu blízkou
  `UINT32_MAX`. Navazující bounds-check ve `WaveBank.cpp` je taky `uint32_t` součet, který může sám
  přetéct → heap over-read/pád při stavbě výsledného `std::vector`.
  *Soubor:* src/CNA/Internal/Audio/XactParser.cpp:449-452, downstream
  src/Microsoft/Xna/Framework/Audio/WaveBank.cpp:221-228.
  *Accept:* saturující/checked odečet (clamp na 0 nebo throw) při výpočtu `dataLength`; test s
  uměle vytvořenou deviací větší než mezera k dalšímu offsetu.

- [ ] **IN-4 — Nesprávný komentář a chybějící case pro variation-table typ 2.** Komentář
  `// INTERACTIVE (type==2)` je zavádějící: podle FAudia je `INTERACTIVE` typ **3**, typ **2** je
  `CLIP` (FAudio ho sám nepodporuje). Catch-all `else` větev v CNA tak potichu naparsuje i typ
  2/5/6/7 stejným 16bajtovým layoutem jako typ 3.
  *Soubor:* include/CNA/Internal/Audio/XactTypes.hpp:101, src/CNA/Internal/Audio/XactParser.cpp:746,775-783.
  *Accept:* opravit komentář (INTERACTIVE=3, CLIP=2 nepodporováno), přidat explicitní kontrolu na
  type 0/1/3/4 a u neznámého typu throw/log místo tichého domýšlení layoutu.

- [ ] **IN-5 — `XactTypes.hpp` stále používá holé `///` komentáře místo Doxygen bloků.** V rozporu
  s `CLAUDE.md` („Never use bare `///` comments on public API members") — SPDX bylo doplněno
  (T-1A), ale `///`→`/** @brief */` konverze nikdy nebyla samostatným úkolem.
  *Soubor:* include/CNA/Internal/Audio/XactTypes.hpp (celý soubor).
  *Accept:* převést všechny `///`/`//` popisky členů struktur na `/** @brief … */` bloky.

- [ ] **IN-6 — Tenké testovací pokrytí `XactParser` (4 testy) nekryje bezpečnostně/funkčně
  kritické větve.** Chybí: poškozené/zkrácené hlavičky a špatná magic čísla pro všechny 3 formáty,
  non-compact `.xwb` (jen compact je pokryt), `entryMetaDataSize<24` non-compact fallback (IN-2),
  `SOUND_FLAG_HAS_RPC`/`SOUND_FLAG_HAS_DSP` (IN-1), ADPCM formát, všechny 4 typy variation table,
  `RAMP` forma PITCH/VOLUME eventů (jen "equation" forma je testovaná), `STOP`/`MARKER`/
  `*REPEATING` eventy. Mezera v testech přímo koreluje s neodhalenými bugy IN-1/IN-2.
  *Soubor:* tests/CNA/Internal/Audio/XactParserTests.cpp (celý soubor).
  *Accept:* přidat fixtures/testy alespoň pro: zkrácený soubor → throw (všechny 3 formáty), špatné
  magic → throw, non-compact `.xwb` s `entryMetaDataSize==24` i `<24`, `HAS_DSP`/`HAS_RPC` sound,
  ADPCM entry, všechny 4 variation-table typy, RAMP-formu PITCH eventu.

#### 7.4 Microphone, datové třídy, enumy, výjimky

> `AudioEmitter`, `AudioListener`, `AudioChannels`, `AudioStopOptions`, `MicrophoneState` a všechny
> 3 audio výjimky (`InstancePlayLimitException`, `NoAudioHardwareException`,
> `NoMicrophoneConnectedException`) zkontrolovány řádek po řádku — plně kompatibilní, žádné nové
> úkoly.

- [ ] **MC-1 — `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` nedeleguje na `SoundEffect`,
  vlastní vzorec má jinou přesnost než FNA.** FNA volá `SoundEffect.GetSampleDuration`/
  `GetSampleSizeInBytes(sizeInBytes, SampleRate, AudioChannels.Mono)`, které zaokrouhlují na celé
  milisekundy. CNA `Microphone::GetSampleDuration` místo toho počítá vlastní
  `seconds = sizeInBytes/(SAMPLERATE*channels*2)` bez zaokrouhlení — pro nedělitelné hodnoty (např.
  100 B @ 44100 Hz mono) vrátí jinou hodnotu než FNA. Toto je přesně nesplněné accept-kritérium
  T-4A (viz poznámka u T-4A výše) — `GetSampleSizeInBytes` je numericky ekvivalentní, ale duplicitní.
  *FNA:* Microphone.cs:172-188; SoundEffect.cs:363-387.
  *CNA:* Microphone.cpp:161-176 (vlastní vzorec); SoundEffect.cpp:334-363 (správná implementace,
  na kterou by se mělo delegovat).
  *Accept:* `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` volají
  `SoundEffect::GetSampleDuration(sizeInBytes, SAMPLERATE, AudioChannels::Mono)`/
  `GetSampleSizeInBytes(...)` (žádná duplicitní matematika). Test na neceločíselnou hranici (100 B)
  ověřující useknutí na celé ms shodné s FNA.

- [ ] **MC-2 — Zastaralý komentář a mrtvá deklarace `friend class MicrophoneFactory` v
  `Microphone.hpp`.** `MicrophoneFactory` nikde v repozitáři neexistuje a od T-4A reálný SDL3
  capture backend existuje — instance se vytvářejí přímo v `Microphone::getAllProperty()`
  (`new Microphone(...)`), ne přes žádnou factory. Komentář „Production Microphone instances only
  come from a real capture backend, which does not exist yet" je teď nepravdivý.
  *CNA:* Microphone.hpp:141-145.
  *Accept:* odstranit nepoužívanou `friend class MicrophoneFactory;` a přepsat komentář tak, aby
  odpovídal aktuálnímu stavu.

- [ ] **MC-3 — `GetData()` při nedostupnosti dat přepíše celý požadovaný rozsah bufferu nulami,
  FNA buffer vůbec nemění.** FNA vrací jen počet skutečně přečtených bajtů a zbylou část bufferu
  nechává beze změny. CNA při `read <= 0` (žádný stream, nic k přečtení, i chyba SDL vracející
  záporné číslo) vždy vyplní celý požadovaný rozsah nulami — chybový stav je tak potichu maskován
  jako "0 bajtů, buffer vynulován", a zároveň to přepíše případná stará platná data volajícího.
  *FNA:* Microphone.cs:149-170.
  *CNA:* Microphone.cpp:128-159 (zejména 144-158).
  *Accept:* buď formálně zdokumentovat jako schválenou odchylku v `CHECKLIST.md`, nebo zúžit
  nulování jen na "žádný otevřený stream" případ a při chybě SDL nechat buffer nedotčený; test.

- [ ] **MC-4 — Chybí test, že `BufferReady` skutečně vystřelí při reálném zachytávání dat
  přesahujících `BufferDuration`.** Existující testy ověřují jen, že volání nespadne. Vzhledem k
  tomu, že `GetQueuedBytes`/`CheckBuffer` byly právě přepojeny na reálná SDL data (dříve vždy
  vracely 0, tedy `BufferReady` nikdy nemohlo vystřelit), je tohle nejrizikovější a nejméně
  prověřené chování — přímo souvisí s MC-1 (špatná `GetSampleDuration` mohla threshold posunout,
  aniž by to jakýkoli test odhalil).
  *FNA:* Microphone.cs:206-213.
  *CNA:* Microphone.cpp:218-224; tests/.../MicrophoneTests.cpp:220-229.
  *Accept:* nový test (analogicky k `MicrophoneCaptureTest`) — nastavit malé `BufferDuration`,
  zaregistrovat `BufferReady` handler, `Start()`, opakovaně volat `CheckBuffer()` v smyčce s
  timeoutem, ověřit, že handler byl zavolán alespoň jednou.

- [ ] **MC-5 — `GetData` nemá samostatný test pro záporný `count`, jen pro `count == 0`.** Validace
  je `count <= 0`, ale existující test `GetDataZeroOrNegativeCountThrows` (navzdory názvu) testuje
  jen `count == 0`.
  *CNA:* Microphone.cpp:139-142; tests/.../MicrophoneTests.cpp:204-209.
  *Accept:* přidat `GetDataNegativeCountThrows` test s `count < 0`, ověřující `System::ArgumentException`.

---

## 5. Doporučené pořadí a milníky

1. **M0 (rozjezd):** T-0A.
2. **M1 (compliance, rychlé výhry):** T-1A…T-1H — typy výjimek, `GetTypeName`, SPDX, viditelnost.
   *Nízké riziko, čistě mechanické, odemkne smysluplné testy.*
3. **M2 (reálné bugy):** T-2A…T-2G — override/Dispose v Dynamic, float guard, XACT parser bugy, AudioCategory.
   *Nejvyšší hodnota — opravuje skutečně vadné chování.*
4. **M3 (věrnost API):** T-3A…T-3G — throw-on-invalid-name, IsInUse, Pan/Volume, RendererDetail, SoundEffectI, rozhodnutí o sémantice.
5. **M4 (funkce):** T-4A…T-4D — mic capture (**T-4A hotové**, s výhradou MC-1), 3D pan/attenuace,
   DSP routing, AudioEngine Update.
6. **M5 (testy):** T-5A…T-5O — kompletní pokrytí (průběžně už od M1, finální konsolidace zde).
7. **M6 (uzávěrka):** T-6A…T-6C.
8. **M7 (doplňkový audit, 2026-07-02):** CP-1…CP-14, XA-1…XA-5, IN-1…IN-6, MC-1…MC-5 (Fáze 7).
   *Doporučené pořadí uvnitř M7:* nejdřív bezpečnostně/funkčně kritické bugy s reálným dopadem na
   běžící hru — IN-1 (tichá korupce parsování), CP-1/CP-3 (playback bugy), XA-1/XA-2 (přerušení
   dlouhých zvuků / leak), CP-7 (dangling pointer) — pak zbytek clusteru CP/XA/IN, pak MC (mic je
   nová, méně používaná funkce), testovací mezery nakonec (ale ne odloženě donekonečna).

> Doporučení: testy nepsat až v M5 — ke každému úkolu z M1–M4 přidávat příslušné testy z fáze 5 hned
> (princip „make and forget" z `CLAUDE.md` — soubor hotový v jednom průchodu, včetně testů). Totéž
> platí pro M7: každý CP/XA/IN/MC úkol už má accept-kritérium s testem v zadání.

---

## 6. Souhrn rizik / otevřená rozhodnutí

| ID | Otázka k rozhodnutí | Default doporučení |
|----|---------------------|--------------------|
| D1 | `CreateInstance`/`FromStream`: hodnota vs. heap-reference + instance-tracking (T-3G) | Ponechat hodnotu, **zdokumentovat** odchylku; tracking jen pokud demo/hra vyžaduje Dispose-kaskádu |
| D2 | Pan/Volume klamp vs. throw/pass-through (T-3C) | Sladit s FNA (throw na range, pass-through volume); klamp jen vědomě + do CHECKLIST |
| D3 | Streaming WaveBank (T-3F) | Zatím doložená odchylka (vše do paměti); skutečný streaming jako pozdější úkol |
| D4 | Rozsah `AudioEngine::Update` / FACT DoWork (T-4D) | Minimálně per-cue volume re-apply; zbytek dokumentovaně no-op |
| D5 | Vlastnictví `SoundEffect` vs. `SoundEffectInstance` — dangling-safe kontrakt vs. sdílené vlastnictví (CP-7) | Provázáno s D1/T-3G; pokud D1 zůstane „hodnota bez trackingu", alespoň zdokumentovat kontrakt v Doxygenu; sdílené vlastnictví (`shared_ptr`) jen pokud D1 padne na tracking |
| D6 | Fire-and-forget cue cleanup: čas vs. stav přehrávání (XA-1) | Sladit s FNA — mazat podle `!IsPlaying`, ne podle uplynulého času; ponechat časový safety-net jen jako krajní pojistku (řádově minuty) |
| D7 | Chování parseru na poškozená/adverzní XACT data — throw vs. saturující clamp (IN-2, IN-3) | Throw (`InvalidOperationException`/`ArgumentException`) je bezpečnější než tiché clampování na 0 — sladí se s projektovým pravidlem „no silent data corruption"; zdokumentovat v CHECKLIST, pokud se zvolí clamp |
| D8 | `Microphone::GetData` chování bufferu při chybě/no-op — nulovat vs. nechat nedotčené (MC-3) | Sladit s FNA (nechat nedotčené) kvůli předvídatelnosti API; pokud zůstane nulování, zapsat do CHECKLIST jako vědomou odchylku |

---

## 7. Dodatek: audit 2026-07-02

Fáze 7 (§4) vznikla ze 4 paralelních line-by-line auditů proti FNA po dokončení T-4A — pokrývá
core playback, XACT, interní backend a mic/data/enumy/výjimky. Kromě samotných CP/XA/IN/MC úkolů
audit odhalil i zastaralost průvodní dokumentace, opravenou přímo (mimo tento soubor):

- **`AUDIT.md`** (`Microsoft::Xna::Framework::Audio` tabulka) — řádky pro `Microphone`, `AudioEngine`,
  `Cue`, `SoundBank`, `WaveBank` byly „(stub behavior)"; opraveno na skutečný stav (viz git historie
  `AUDIT.md`).
- **`CHECKLIST.md`** („Known acceptable C++ deviations") — doplněno 5 audio-specifických řádků
  odpovídajících §2 zde (T-6A tím splněno).
- **`NEXT.md`** — odstraněn zastaralý řádek v §5 tvrdící, že Microphone capture je stub (odporoval
  zbytku téhož souboru po T-4A).

T-6B (aktualizace `AUDIT.md`) je tímto splněno. T-6A (deviation table) je tímto splněno. T-6C
(build & report) zůstává na příští session, až se vyřeší alespoň první vlna Fáze 7 bugů.

---

*Vygenerováno na základě tří paralelních line-by-line auditů proti FNA (clustery: core playback,
XACT, 3D/mic/enumy/výjimky). Pokrývá pouze `Microsoft::Xna::Framework::Audio` + `CNA::Internal::Audio`.
Fáze 7 (2026-07-02) přidala další 4 paralelní audity nad již opraveným kódem — viz dodatek výše.*
