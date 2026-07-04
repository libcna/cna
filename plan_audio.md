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
| 15 | Microphone | 217 | Funkční, T-4A hotové | reálný SDL3 capture (enumerace/Start/Stop/GetData) hotový; `GetSampleDuration`/`GetSampleSizeInBytes` teď deleguje na `SoundEffect` (MC-1, hotovo 2026-07-03); zastaralý `friend class MicrophoneFactory` komentář zbývá (MC-2) |
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
4. **FACT `DoWork`** — kategorie fade a instance-limity z FACT `DoWork` nejsou implementovány
   (per-cue re-apply hlasitosti kategorie ale ano, viz T-4D). Streaming `WaveBank` byl doplněn
   (T-3F, 2026-07-04): streamující ctor čte hlavičku/metadata z disku a data položky líné, ne
   celý soubor eager.
5. **`CreateInstance`/`FromStream` zůstávají hodnotové** (žádná heap-reference sémantika), ale
   `SoundEffect` teď má instance-tracking + Dispose-kaskádu a je move-only (T-3G, 2026-07-04).

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

- [x] **T-3F — Streaming WaveBank: offset/packetSize.**
  Buď implementovat skutečný streaming ctor (`WaveBank.cpp:149-155` dnes deleguje na in-memory a
  ignoruje offset/packetSize), nebo natvrdo zdokumentovat jako akceptovanou odchylku (vše do paměti).
  *FNA:* WaveBank.cs:104-143.
  *Accept:* doložené rozhodnutí; pokud implementováno, test na korektní offsetované čtení. (B z #3)
  *Pozn.:* Hotovo 2026-07-04 -- implementován skutečný streaming (rozhodnutí padlo pro "implementovat",
  ne pro doložení odchylky). Nová `ParseXwbStreamingHeader(path)` (XactParser.cpp) čte z disku jen
  segmenty 0-3 (bankdata/entrymetadata/seektables/entrynames), segment 4 (wave data, typicky
  zdaleka největší část souboru) se nenačítá; `XwbData` má nové `streaming`/`sourcePath` pole.
  `WaveBank::GetSoundEffect()` pak pro streamující banku čte data konkrétní položky líné, přímo
  z disku (`sourcePath`, seek na `entry.dataOffset`, přečte `entry.dataLength` bajtů), místo
  slice z `fileData` (které u streamující banky obsahuje jen hlavičku/metadata). Non-streaming ctor
  se chová beze změny (celý soubor eager, jako FNA). `offset`/`packetSize` zůstávají nepoužité --
  ověřeno na FNA referenci (`WaveBank.cs:104-143`), že FNA samo tyto dva parametry nikdy nekopíruje
  do `FACTStreamingParameters` (jen `.file`), takže shoda s FNA znamená, že mají zůstat mrtvé i
  v CNA. Testy: `WaveBankTest.NonStreamingCtorLoadsEntireFileIntoMemory`,
  `StreamingCtorDoesNotLoadWaveDataSegmentIntoMemory` (paměťová stopa přes nový
  `WaveBankTestAccess`/`StreamingInternal`/`ResidentFileBytesInternal`),
  `StreamingGetSoundEffectReadsCorrectPerEntryOffsetAndLength` (dvou-entry fixtura s různou
  délkou/offsetem -- chytí i "čte špatný, ale stejně dlouhý rozsah" bug, ne jen "nečte vůbec").
  Ověřeno ASan+LeakSanitizer buildem (žádný leak/UB v novém file-I/O kódu) a `git stash`
  metodikou (bez opravy `WaveBankTestAccess` ani nejde zkompilovat, protože `StreamingInternal`
  atd. neexistují -- genuine compile failure, ne jen selhávající assert).

- [x] **T-3G — SoundEffect: instance-tracking + Dispose-kaskáda (rozhodnutí).**
  Buď evidovat živé instance (weak refs) a `SoundEffect::Dispose()` je zastaví/disposne (FNA),
  nebo formálně zdokumentovat hodnotovou odchylku (bez kaskády) v CHECKLIST. Provázáno s rozhodnutím
  `CreateInstance` hodnotou vs. heap-ref a `FromStream` ownership.
  *FNA:* SoundEffect.cs:126,315-323,354,389.
  *Accept:* doložené rozhodnutí; pokud tracking, dispose SoundEffectu zastaví jeho instanci (test). (A7)
  *Pozn.:* Hotovo 2026-07-04 -- rozhodnutí padlo pro "implementovat" (uživatel), ne pro doložení
  odchylky. `SoundEffect::Impl` má nový `std::vector<SoundEffectInstance*> instances` (raw,
  neowning); `SoundEffectInstance` se registruje ve svém ctoru (`SoundEffect::RegisterInstance`)
  a odregistruje v `Dispose()` (`UnregisterInstance` + uvolní vlastní `soundEffectKeepAlive_`,
  takže poslední instance + `SoundEffect` skutečně uvolní `MIX_Audio` deterministicky, blíž FNA
  eager-release). `SoundEffect::Dispose()` iteruje snapshot `impl_->instances` a zavolá `Dispose()`
  na každou živou instanci (FNA `Instances.ToArray()` + foreach), než `impl_.reset()`.
  Vedlejší, ale nutná změna: **`SoundEffect` je teď move-only** (copy ctor/assignment `= delete`) --
  bez jediného vlastníka na resource by dvě nezávislé kopie mohly nesouhlasit v tom, čí `Dispose()`
  je autoritativní. Ověřeno přes Explore agenta, že žádné volající místo v `src/`/`tests/`/`examples/`
  nespoléhá na kopírovatelnost `SoundEffect` -- **kromě** `ContentManager::Load<T>()`, jehož
  generický `std::any`-cache vyžaduje `CopyConstructible`; opraveno novou explicitní specializací
  `Load<Audio::SoundEffect>` (viz `ContentManager.hpp`/`.cpp`), která tento typ vůbec necachuje
  (sdílení jedné instance mezi nesouvisejícími volajícími by teď bylo vyloženě špatně -- Dispose
  jednoho volajícího by tiše zastavil přehrávání jiného). Move ctor/assignment
  `SoundEffectInstance` teď navíc **re-pointují** cascade-tracking (odregistrují `&other`,
  zaregistrují `this`) -- bez toho by `SoundEffect::Dispose()` po přesunuté instanci volal
  `Dispose()` na starou (možná už mimo scope) adresu. Ověřeno reálným segfaultem: dočasné
  vypnutí jen repoint-bloků (ne celé feature) a spuštění nového testu
  `DisposeAfterInstanceMovedOutOfScopeDisposesTheMovedToInstance` skutečně spadlo (SIGSEGV), ne
  jen selhal assert -- silnější důkaz než obvyklá `git stash` metodika. Testy: 5 nových v
  `SoundEffectTests.cpp` (cascade na 1/N instancí, already-disposed instance přeskočena bez
  throw, moved-to instance přes move ctor i move assignment) + 4 `static_assert` (move-only).
  Ověřeno ASan+LeakSanitizer (celá sada, ne jen nové testy) a `git stash` (bez opravy testy
  vůbec nejdou zkompilovat -- `RegisterInstance` atd. neexistují). Celá sada 2029/2029 zelená,
  `cna_demo_sound`/`cna_demo_2d` (jediná volající místa `Load<SoundEffect>`) se překládají čistě.

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

- [x] **T-4B — 3D pan/attenuace pro SDL_mixer (Apply3D / 3D PlayCue / Cue::Apply3D).**
  Místo no-opů odvodit pan + distance-attenuaci z geometrie listener/emitter (`Mix_SetPosition`).
  Z `Cue::Apply3D` (`Cue.cpp:79-83`) odstranit disposed-`std::runtime_error` ve prospěch `ObjectDisposedException`.
  Doppler zůstává neaplikovaný (akceptovaná odchylka §2.2).
  *FNA:* SoundBank.cs:248-263, Cue.cs:166-186, SoundEffectInstance.cs:266-298.
  *Accept:* Apply3D/3D-PlayCue na validním vstupu nehází a nekrachuje; volume/pan se mění se vzdáleností/úhlem (test geometrie). (B9, A-Apply3D)
  *Pozn.:* Hotovo 2026-07-04. `ObjectDisposedException` byl už na místě (žádná zbylá práce na tom
  bodu). `Cue::Apply3D` teď iteruje `active_` a volá `pi.instance->Apply3D(listener, emitter)` na
  každý živý `SoundEffectInstance` -- prostě deleguje na už fungující `SoundEffectInstance::Apply3D`
  (CP-3), žádná nová pan/attenuace matematika. `SoundBank::PlayCue(name, listener, emitter)` byl
  úplný no-op (jen volal 2-arg přetížení); refaktorováno na sdílený privátní
  `PlayCueInternal(name, listener*, emitter*)`, který po `cue->Play()` volá `cue->Apply3D(...)`
  než cue uloží do `fireAndForget_` (odpovídá FNA, kde `FACT3DCalculate` běží před
  `FACTSoundBank_Play3D` -- synchronní volání ve stejném vlákně, žádný pozorovatelný rozdíl proti
  "pozicuj až po Play()"). Test geometrie: žádná ze stávajících fixtur (`MakeCue()`,
  `SharedWeightedVariationBank()`, `SoundBankTests.cpp`'s "Explosion") nemá reálný WaveBank, takže
  `Cue::active_` zůstává prázdné -- přidány nové WaveBank-backed fixtury do `CueTests.cpp` i
  `SoundBankTests.cpp` (`Apply3DCue`/`Apply3DWaveBank`), plus sdílený
  `SoundEffectInstanceTestAccess.hpp` (extrahováno z `SoundEffectInstanceTests.cpp`) a nová
  `CueTestAccess::ActiveInstance()`/`SoundBankTestAccess::LastFireAndForgetCue()`, aby test mohl
  přečíst skutečný `MIX_GetTrackGain()` a ověřit, že se gain mění se vzdáleností (SDL3_mixer
  nemá getter pro stereo pan, takže se ověřuje jen attenuace, ne pan -- stejné omezení, jaké má
  i CP-3's původní `SoundEffectInstance::Apply3D` pokrytí). Ověřeno `git stash` metodikou (oba
  nové testy skutečně selžou -- `farGain == nearGain == 1` -- proti no-op kódu, ne jen
  kompilační chyba tentokrát, protože scaffolding nezávisí na samotné opravě). Ověřeno
  ASan+LeakSanitizer. Celá sada 2031/2031 zelená.

- [x] **T-4C — Interní DSP filtry/reverb routing v SoundEffectInstance.**
  Přidat `INTERNAL_applyReverb`/`applyLowPassFilter`/`applyHighPassFilter`/`applyBandPassFilter`
  (private/detail), aby volající z Cue/AudioEngine seděli; implementovat přes SDL_mixer kde to jde,
  jinak doložený no-op.
  *FNA:* SoundEffectInstance.cs:488,518,536,554.
  *Accept:* volající se překládají; chování (reálné/no-op) v tabulce odchylek. (A8)
  *Pozn.:* Hotovo 2026-07-04 -- rozhodnutí padlo pro "implementovat reálné filtry, reverb necháme
  no-op" (uživatel). Zjištění před implementací: **žádný caller neexistuje ani ve FNA samotném**
  (grep přes celý FNA zdroj) -- FACT aplikuje XACT RPC/filter routing nativně, C# vrstva tato
  `INTERNAL_*` volá nikdy. "Volající z Cue/AudioEngine" z accept kritéria tedy neodpovídá žádnému
  reálnému volajícímu ani v referenci; úkol se redukuje na "metody existují, správně se chovají".
  Filtry ale reálně JDOU implementovat: SDL3_mixer's `MIX_SetTrackCookedCallback` dává přístup
  k syrovému float PCM po gain/pan/3D, těsně před mixováním -- implementován FAudio's přesný
  state-variable filter (Chamberlin SVF, viz `FAudio_internal.c`'s `FAudio_INTERNAL_FilterVoice`)
  v tomto callbacku. Reverb zůstává doložený no-op -- SDL3_mixer nemá aux-send/return bus
  (žádný ekvivalent FAudio's sdíleného `ReverbVoice`), skutečná implementace by byla
  neúměrně velký rozsah proti zbytku úkolu.
  Thread-safety: callback běží na SDL_mixer's mixovacím vlákně (dle dokumentace SDL3_mixer).
  Koeficienty (`kind`/`frequency`/`oneOverQ`) se zapisují v setteru pod `MIX_LockMixer`/
  `UnlockMixer`, čtou se v callbacku BEZ zamykání -- SDL3_mixer dokumentuje, že mixovací vlákno
  už tento zámek drží po dobu mixování, takže druhý zámek by byl zbytečný. Rekurzivní stav filtru
  (`yl`/`yb`) čte/píše výhradně mixovací vlákno, žádná synchronizace není potřeba.
  Filter stav (`FilterState`) je heap-alokovaný přes `unique_ptr` -- při přesunu
  `SoundEffectInstance` (move ctor/assignment) se přesune jen vlastnictví ukazatele, ne adresa
  objektu, takže SDL3_mixer callback (jehož `userdata` je právě tento ukazatel) zůstává platný
  bez nutnosti re-registrace. Bez tohoto by přesun instance s aktivním filtrem nechal callback
  mířit na cizí/zastaralou adresu -- stejná třída chyby jako u T-3G's instance-tracking repointu.
  Testy: 9 nových v `SoundEffectInstanceTests.cpp`, včetně no-op-před-Play() testu,
  přesných jedno-vzorkových testů (z čerstvého nulového stavu je první výstup filtru přesně
  spočítatelný: `Yl(1)=0`, `Yh(1)=x`, `Yb(1)=F*x`), konvergenčních testů (konstantní signál musí
  konvergovat k unity gain u lowpass / nule u highpass) a testu přežití move. Testy volají
  matematiku filtru PŘÍMO a synchronně (`ProcessFilterSamplesForTest`), ne přes skutečný
  SDL3_mixer callback -- ten by se spouštěl asynchronně z mixovacího vlákna, což by test buď
  zpomalilo (reálné čekání), nebo udělalo nedeterministickým (flaky). **Omezení testování:**
  skutečná souběžnost (setter na hlavním vlákně současně s callbackem na mixovacím vlákně) není
  timing-přesně ověřena (žádný ThreadSanitizer běh, žádné reálné (non-dummy) audio zařízení v
  tomto prostředí) -- zámkování odpovídá SDL3_mixer's vlastní zdokumentované doporučené praxi,
  ale nebylo empiricky stresováno.
  Ověřeno `git stash` (testy se bez opravy nezkompilují -- `INTERNAL_applyLowPassFilter` atd.
  neexistují) a ASan+LeakSanitizer (celá sada). Celá sada 2039/2039 zelená.

- [x] **T-4D — AudioEngine `Update()` / per-cue přepočet kategorií.**
  Posoudit, co z FACT `DoWork` je potřeba (fade kategorií, instance-limity); minimálně dokončit
  re-apply smyčku hlasitosti kategorie (`AudioEngine.cpp:219-223` má prázdné tělo), aby změna hlasitosti
  ovlivnila i běžící cue, ne jen budoucí.
  *FNA:* AudioEngine.cs:337 (+ kategorie/fade).
  *Accept:* změna hlasitosti kategorie ovlivní právě hrající cue (test); dokumentováno, co zůstává no-op. (B z AudioEngine §1)
  *Pozn.:* Hotovo -- `AudioEngine::SetCategoryVolumeInternal` teď volá nové `Cue::ApplyCategoryVolume`
  pro každý aktivní cue v dané kategorii; `Cue::PlaybackInstance` si pamatuje `baseVolume`
  (waveRef.volume, jak byl zkombinován s track/sound volume při parsování), takže re-apply
  přepočítá `clamp(baseVolume * newCatVol, 0, 1)` stejným vzorcem jako `Play()`. Fade
  kategorií a instance-limity (zbytek FACT `DoWork`) zůstávají mimo rozsah -- nebyly součástí
  accept kritéria. Ověřeno regresním testem `AudioCategoryTest.SetVolumeReappliesToAlreadyPlayingCueInstance`
  (nová fixtura s reálným WaveBank+SoundEffectInstance, na rozdíl od stávající
  `PauseResumeStopRouteToRealActiveCueInCategory`, jejíž cue nemá žádný wavebank, takže
  `active_` zůstává prázdné a nešlo by u ní hlasitost pozorovat); potvrzeno přes `git stash`
  metodiku, že test bez opravy skutečně selže (1 == 1, žádná změna).

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
- [x] **T-6C — Build & report** — `cmake --build cmake-build-debug --target CNA` a `--target CnaTests`
  zelené; krátký report (změněné soubory, odchylky, zbývající mezery) dle `CLAUDE.md` §Build and Report.
  *Pozn.:* Hotovo 2026-07-04. Build: `CNA` i `CnaTests` zelené (nic k přebudování, oba už aktuální
  z předchozích commitů), `CnaTests` **2031/2031** zelené (`SDL_AUDIODRIVER=dummy`).
  **Changed files** (tato session, commity `d468dc4`..`feb6eda`, 4 úkoly T-4D/T-3F/T-3G/T-4B):
  `AudioEngine.{hpp,cpp}`, `Cue.{hpp,cpp}`, `SoundBank.{hpp,cpp}`, `WaveBank.{hpp,cpp}`,
  `SoundEffect.{hpp,cpp}`, `SoundEffectInstance.cpp`, `XactTypes.hpp`, `XactParser.cpp`,
  `ContentManager.{hpp,cpp}` (kolaterální oprava pro T-3G), plus testy
  (`AudioCategoryTests.cpp`, `CueTests.cpp`, `SoundBankTests.cpp`, `WaveBankTests.cpp`,
  `SoundEffectTests.cpp`, `SoundEffectInstanceTests.cpp`, `XactParserTests.cpp`) a dva nové
  sdílené test-access headery (`CueTestAccess.hpp`, `SoundEffectInstanceTestAccess.hpp`).
  **Přidané stuby:** žádné. **Chybějící závislosti:** žádné.
  **Úmyslné odchylky** (viz `CHECKLIST.md`, řádky `Audio:`): 3D poziční audio jen pan+attenuace
  bez elevace; Doppler uložen, nikdy aplikován; `GetHashCode()` přes `std::hash` místo .NET
  algoritmu; streaming `WaveBank`'s `offset`/`packetSize` nepoužité (shoda s FNA); `SoundEffect`
  move-only; `ContentManager::Load<SoundEffect>()` necachuje; interaktivní (`type==3`) variation
  tabulky uniform-pick místo variable-driven.
  **Zbývající mezery** (viz `NEXT.md` §5): v době psaní tohoto reportu byl jediný zbývající úkol
  `T-4C` (DSP filtry/reverb na `SoundEffectInstance`) -- **od té doby (stále 2026-07-04) uzavřen
  také**, viz jeho vlastní `*Pozn.:*` výše. K okamžiku psaní TOHOTO odstavce (T-6C) tedy zbytek
  jsou jen úmyslné, zdokumentované odchylky výše -- žádný otevřený úkol nezůstává.

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

- [x] **CP-1 — `SoundEffectInstance::Play()` není idempotentní při opakovaném volání za běhu.** *(hotovo 2026-07-02.)*
  FNA má explicitní `if (State == SoundState.Playing) { return; }` na začátku `Play()`. CNA tuto
  kontrolu nemá — opakované `Play()` na již hrající instanci znovu volá `MIX_SetTrackAudio`/
  `MIX_PlayTrack`, čímž restartuje přehrávání od začátku místo no-opu.
  *FNA:* SoundEffectInstance.cs:282-285.
  *CNA:* SoundEffectInstance.cpp:143-232 (chybí guard).
  *Accept:* opakované `Play()` během `State==Playing` nezmění pozici přehrávání (test s odposlechem,
  že se track nepřehraje od nuly / že se nevolá znovu `MIX_SetTrackAudio`).
  *Pozn.:* přidán `SoundEffectInstanceTestAccess` (friend, mirror `MicrophoneTestAccess`) pro čtení
  `track_` v testu. Nový `SoundEffectInstanceTest.RepeatedPlayWhileAlreadyPlayingDoesNotRestartTrack`
  nastaví `MIX_SetTrackPlaybackPosition` na nenulovou pozici, zavolá `Play()` znovu a ověří
  `MIX_GetTrackPlaybackPosition` zůstává >= té pozice; ověřeno přes `git stash`, že bez opravy
  test selže (pozice se resetuje na 0), s opravou projde. `DynamicSoundEffectInstance::Play()`
  má vlastní override s guardem už hotovým — CP-1 se ho netýkal.

- [x] **CP-2 — `SoundEffect::Play(volume, pitch, pan)` nevaliduje/neklampuje pan a pitch.** *(hotovo
  2026-07-03.)* FNA interně dělá `instance.Pitch = pitch;` (klamp na [-1,1]) a
  `instance.Pan = pan;` (hází `ArgumentOutOfRangeException` při `|pan|>1`) ještě před
  `instance.Play()`. CNA počítala stereo gains a frequency ratio přímo ze syrových vstupů bez
  klampu/validace.
  *FNA:* SoundEffect.cs:338-352, SoundEffectInstance.cs:46-65,87-101.
  *CNA:* SoundEffect.cpp:254-310.
  *Accept:* `Play(v, pitch>1, pan>1)` hází `ArgumentOutOfRangeException` (pan) resp. klampuje pitch
  na [-1,1] stejně jako property setter; test na obě větve.
  *Pozn.:* `Play(volume,pitch,pan)` teď na začátku (před jakoukoliv práci s mixerem) validuje/
  klampuje přesně jako `SoundEffectInstance::setPanProperty`/`setPitchProperty` — `pan` mimo
  [-1,1] hází `System::ArgumentOutOfRangeException`, `pitch` se klampuje na [-1,1]. Nové testy
  `PlayThrowsOnPanOutOfRange` (ověřen přes `git stash` — bez opravy selže, `throws nothing`, s
  opravou projde) a `PlayClampsPitchInsteadOfThrowing` (prochází v obou verzích — extrémní pitch
  nepadal ani předtím, jen se neklampoval; test pinuje klamp do budoucna). Celá sada
  2002/2002 testů zelená.

- [x] **CP-3 — `Apply3D(listener, emitter)` přepisuje veřejné `Volume`/`Pan`, místo aby počítal jen
  samostatnou výstupní matici jako FNA.** *(hotovo 2026-07-02.)* FNA aktualizuje pouze interní
  `dspSettings`, getterů `Volume`/`Pan` se 3D pozicování vůbec nedotýká. CNA volá
  `setVolumeProperty(...)`/`setPanProperty(...)` přímo, takže po `Apply3D` už
  `getVolumeProperty()`/`getPanProperty()` nevrací hodnotu, kterou uživatel nastavil.
  *FNA:* SoundEffectInstance.cs:221-264 (bez zásahu do `INTERNAL_pan`/`INTERNAL_volume`).
  *CNA:* SoundEffectInstance.cpp:289-310.
  *Accept:* po `setVolumeProperty(X)` + `Apply3D(...)` je `getVolumeProperty()==X` (3D atenuace se
  aplikuje jinam, ne na veřejnou property); testy na obě.
  *Pozn.:* opraveno aplikací 3D útlumu/panu přímo na `MIX_Track` (sdílený `ApplyTrackProperties`
  helper, `atten * Volume_` — multiplikativně s Volume, přesně jako FNA kombinuje `INTERNAL_volume`
  s `dspSettings` matricí na úrovni audio enginu), místo volání `setVolumeProperty`/`setPanProperty`.
  Při refaktoru objeven vedlejší nález: `Apply3D` dosud neházel `ObjectDisposedException` explicitně
  (fungovalo to jen nepřímo přes `setPanProperty`'s disposed-check, který by refaktorem zmizel) —
  přidán explicitní disposed-check + `@throws` doxygen + `Apply3DAfterDisposeThrows` test, aby se
  toto chování při refaktoru nezregresovalo. Ověřeno přes `git stash`, že
  `Apply3DDoesNotModifyVolumeOrPanProperties` bez opravy selže (Volume/Pan se přepíšou), s opravou
  projde.

- [x] **CP-4 — `DynamicSoundEffectInstance::PendingBufferCount`/`Update()` mění sémantiku oproti FNA
  — počet se nuluje ihned po předání dat do SDL streamu, ne až po skutečném přehrání hardwarem.**
  *(hotovo 2026-07-03.)* `SubmitQueuedToStream()` vyprázdnilo `queuedBuffers_` při každém volání
  (i uvnitř `Update()`), takže `getPendingBufferCountProperty()` bylo těsně po submitu prakticky
  vždy 0 → `Update()` pak spouštělo `BufferNeeded` prakticky při každém tiku bez ohledu na
  skutečný stav bufferu.
  *FNA:* DynamicSoundEffectInstance.cs:23-29,290-322.
  *CNA:* DynamicSoundEffectInstance.cpp:47-51,312-331,374-395.
  *Accept:* `PendingBufferCount` odráží data ještě nespotřebovaná (dotaz na skutečnou frontu SDL
  streamu, ne lokální seznam); `BufferNeeded` se nespouští při dostatku dat ve streamu (test na
  frekvenci volání).
  *Pozn.:* přidán nový privátní člen `std::deque<std::size_t> submittedChunkSizes_` — velikost
  (v bajtech) každého chunku předaného do `audioStream_`, od nejstaršího. `SubmitQueuedToStream()`
  teď při předání dat do streamu zároveň zaznamená velikost chunku do `submittedChunkSizes_`
  (místo pouhého vyprázdnění `queuedBuffers_`). `getPendingBufferCountProperty()` vrací
  `queuedBuffers_.size() + submittedChunkSizes_.size()`. `Update()` po `SubmitQueuedToStream()`
  zavolá `SDL_GetAudioStreamQueued(stream)` (skutečný počet bajtů, které stream stále drží jako
  nespotřebovaný vstup — SDL3 analog FNA's `FAudioSourceVoice_GetState().BuffersQueued`) a
  odstraňuje nejstarší chunky z `submittedChunkSizes_`, dokud součet zbylých přesahuje tuto
  hodnotu — přesně stejný algoritmus jako FNA's `while (PendingBufferCount > state.BuffersQueued)
  RemoveAt(0)`, jen v bajtové granularitě místo diskrétních bufferů. `ClearBuffers()` teď maže i
  `submittedChunkSizes_`. Nový test `BufferNeededDoesNotFireWhenStreamHasEnoughData` (3 buffery
  před/při `Play()`, `Update()` hned poté — nic nemohlo být reálně spotřebováno, `BufferNeeded`
  nesmí vystřelit ani jednou) ověřen přes `git stash` — bez opravy konzistentně selže (fired>0
  i s plným bufferem), s opravou konzistentně `fired==0` (5x opakováno). Celá sada
  2004/2004 testů zelená (3x opakovaně, bez flakiness).

- [x] **CP-5 — `Stop(bool immediate=false)` na `DynamicSoundEffectInstance` tiše no-opuje místo
  hození `InvalidOperationException`.** *(hotovo 2026-07-03.)* FNA v `Stop(bool)` explicitně hází,
  pokud `isDynamic && !immediate`. CNA `Stop(bool)` nebylo virtuální a pracovalo jen s bázovým
  `track_`, který dynamické instance nikdy nepoužívají.
  *FNA:* SoundEffectInstance.cs:404-439 (throw ř. ~435).
  *CNA:* SoundEffectInstance.cpp:239-261 (chybí `isDynamic`/virtual dispatch).
  *Accept:* `dynamicInstance.Stop(false)` hází `System::InvalidOperationException`; test.
  *Pozn.:* `SoundEffectInstance::Stop(bool)` je teď `virtual`; `DynamicSoundEffectInstance`
  přidává override, který napřed replikuje FNA's `handle==0` early-return gate (žádný aktivní
  track → tichý no-op, i pro `immediate=false` — matchuje FNA přesně: `Stop(false)` před prvním
  `Play()` nehází), a teprve pokud track existuje a `!immediate`, hází
  `System::InvalidOperationException`. Nové testy `StopFalseWhileNeverPlayedIsSafeNoOp` (no-op
  case) a `StopFalseAfterPlayingThrowsInvalidOperation` (throw case, po `tryStartHeadless`).
  Ověřeno přes `git stash`: PŮVODNÍ kód se s testy vůbec nezkompiluje (`d.Stop(false)` — C++
  name-hiding: derived `Stop()` bez parametru skrývá base `Stop(bool)` overload, dokud derived
  třída nedeklaruje vlastní `Stop(bool)`) — silnější důkaz bugu než běžné "throws nothing". S
  opravou vše projde. Celá sada 1999/1999 testů zelená.

- [x] **CP-6 — `DynamicSoundEffectInstance::GetSampleDuration`/`GetSampleSizeInBytes` počítají
  skutečné bajty/vzorek (`isFloat_ ? 4 : 2`) místo FNA napevno předpokládaných 16 bitů.** *(hotovo
  2026-07-03.)* FNA obě metody vždy deleguje na `SoundEffect.GetSampleDuration/GetSampleSizeInBytes`,
  které natvrdo dělí/násobí 2 bez ohledu na float-mód. CNA po `SubmitFloatBufferEXT` vracela jiný
  výsledek než FNA.
  *FNA:* DynamicSoundEffectInstance.cs:114-130; SoundEffect.cs:363-374.
  *CNA:* DynamicSoundEffectInstance.cpp:79-96,335-339.
  *Accept:* po `SubmitFloatBufferEXT` dá `GetSampleDuration`/`GetSampleSizeInBytes` číselně stejný
  výsledek jako FNA (stále děleno/násobeno 2, ne 4); regresní test na float instanci.
  *Pozn.:* obě metody teď jednořádkově delegují na `SoundEffect::GetSampleDuration`/
  `GetSampleSizeInBytes(…, sampleRate_, channels_)` — přesně jako FNA. Nyní mrtvý privátní
  helper `getBytesPerSampleFrame()` (jediný jeho volající kód) smazán (deklarace i definice).
  Nový test `GetSampleDurationIgnoresFloatFormatMatchingFNA` (po `SubmitFloatBufferEXT` musí
  1s stereo @ 44100Hz pořád dát 176400 B, ne 352800 B) ověřen přes `git stash` — bez opravy
  selže přesně na `352800 != 176400`, s opravou projde. Celá sada 2003/2003 testů zelená.

- [x] **CP-7 — `SoundEffectInstance` drží syrový `const SoundEffect*` na rodiče bez správy
  životnosti — dangling pointer u běžného řetězení.** *(hotovo 2026-07-03.)*
  `SoundEffect(path).CreateInstance()` (nebo `SoundEffect::FromStream(s)->CreateInstance()` na
  dereferencovaném dočasném objektu) vytvořilo instanci ukazující na již zaniklý `SoundEffect`;
  následné `Play()` četlo `soundEffect_->getNativeAudioHandle()` na uvolněné paměti. Souvisí
  s T-3G (hodnotová sémantika), ale je to konkrétní bezpečnostní riziko nad rámec obecného textu
  T-3G.
  *FNA:* SoundEffect.cs:126,354 (reference semantics, GC drží objekt naživu).
  *CNA:* SoundEffectInstance.hpp:32; SoundEffectInstance.cpp:69-72.
  *Accept:* buď dokumentovaná kontraktová podmínka (owner musí přežít instanci) v Doxygenu
  `CreateInstance()`/ctoru, nebo oprava na sdílené vlastnictví; ASAN test na dangling scénář.
  *Pozn.:* zvolena oprava na sdílené vlastnictví (ne jen dokumentace). Syrový `const SoundEffect*
  soundEffect_` nahrazen dvěma členy: `std::shared_ptr<void> soundEffectKeepAlive_` (type-erased
  kopie `SoundEffect::impl_`, protože `SoundEffect::Impl` je private a definovaná jen v
  SoundEffect.cpp — ale konverze `shared_ptr<Impl> → shared_ptr<void>` funguje i s neúplným
  typem) a `void* nativeAudioHandle_` (MIX_Audio*, zachycený v konstruktoru dokud byl
  `soundEffect` ještě živý). `Play()` už nikdy nedereferencuje `SoundEffect*` — jen čte
  `nativeAudioHandle_`. Nový test `PlaySucceedsAfterOriginatingSoundEffectTemporaryIsDestroyed`
  (`SoundEffect(pcm,...).CreateInstance()` — dočasný `SoundEffect` je zničen před `Play()`)
  ověřen pod skutečným ASan buildem (`cmake-build-asan`, dle NEXT.md receptu, smazán po ověření):
  **před opravou** ASan nahlásí přesný `stack-use-after-scope` v
  `SoundEffectInstance::Play()` → `SoundEffect::getNativeAudioHandle()`; **po opravě** celá
  audio test suite (168 testů) prochází pod ASan+LeakSanitizer beze stop. Celá sada
  2005/2005 testů zelená (normální build).

- [x] **CP-8 — `SoundEffect` nededí `System::Object` a nemá `GetTypeName()`, na rozdíl od
  sourozeneckých tříd.** *(hotovo 2026-07-03.)* `SoundEffectInstance`, `DynamicSoundEffectInstance`,
  `AudioEngine`, `SoundBank`, `WaveBank`, `Cue` všechny dědí `System::Object` a mají
  `GetTypeNameHPP()`/`GetTypeNameCPP()`. `SoundEffect` dědila jen `System::IDisposable`.
  *FNA:* SoundEffect.cs:20 (implicitní `object`).
  *CNA:* SoundEffect.hpp:19.
  *Accept:* `SoundEffect : public System::Object, public System::IDisposable`;
  `GetTypeName()=="Microsoft.Xna.Framework.Audio.SoundEffect"`; test.
  *Pozn.:* `SoundEffect` teď dědí `public System::Object, public System::IDisposable` (stejné
  pořadí jako sourozenecké třídy), přidán `GetTypeNameHPP()`/`GetTypeNameCPP(SoundEffect, …)`.
  Nový test `SoundEffectTest.GetTypeNameIsDottedXnaName` ověřen na PŮVODNÍM (pre-fix) kódu přes
  `git stash` — bez opravy nejde ani zkompilovat (`has no member named 'GetTypeName'`), s opravou
  projde. Celá sada 1987/1987 testů zelená.

- [x] **CP-9 — Konstruktor `SoundEffectInstance(const SoundEffect&)` je veřejný, FNA má ekvivalentní
  ctor `internal`.** *(hotovo 2026-07-03.)* Podle `CLAUDE.md` (Visibility Mapping) má `internal`
  mapovat na `private`/`protected`/friend-scoped, ne na `public`. Třída už deklarovala
  `friend class SoundEffect;`, takže zprivátnění bylo bezbolestné.
  *FNA:* SoundEffectInstance.cs:174 (`internal SoundEffectInstance(...)`).
  *CNA:* SoundEffectInstance.hpp:45.
  *Accept:* ctor `private`/`protected`; `SoundEffect::CreateInstance()` (friend) se dál překládá;
  přímá konstrukce zvenčí se nepřekládá (kompilační negativní test/komentář).
  *Pozn.:* ctor přesunut do `private:` sekce s doxygen poznámkou, že jde o `internal`-ekvivalent
  volaný jen z `SoundEffect::CreateInstance()`. Ověřeno oběma směry: `cmake --build` prochází
  beze změny (CreateInstance() se dál překládá), a scratch soubor s přímou konstrukcí zvenčí
  (`SoundEffectInstance inst(fx);`) selže s `'...SoundEffectInstance(...)' is private within
  this context` — přesně dle accept-kritéria. Celá sada 1988/1988 testů zelená (beze změny počtu,
  jde čistě o viditelnost).

- [x] **CP-10 — Chybí test pro NOXNA ctor `SoundEffect(const std::string& assetName)` (načtení ze
  souboru).** *(hotovo 2026-07-03.)* Pokrytý byl jen buffer-ctor a `FromStream`; ctor z cesty k
  souboru neměl žádný test.
  *CNA:* SoundEffect.hpp:48; tests/…/SoundEffectTests.cpp.
  *Accept:* test na prázdný string (no-op), neexistující soubor (throw, headless-safe pod
  `GTEST_SKIP` při chybějícím zařízení).
  *Pozn.:* žádná změna produkčního kódu — ctor už se choval správně, jen chyběly testy. Přidány
  `SoundEffectTest.ConstructFromEmptyPathIsNoOp` (prázdný string → žádný throw, `IsDisposed==
  false`, `Duration==0`) a `...ConstructFromNonexistentPathThrowsNotSupported` (neexistující
  cesta → `System::NotSupportedException`, se stejným try/catch/`GTEST_SKIP` idiomem jako
  `FromStreamGarbageThrowsNotSupported`). Oba testy proběhly bez skipnutí (dummy audio zařízení
  v tomto prostředí funguje) a reálně prošly load-path větví. Celá sada 1992/1992 testů zelená.

- [x] **CP-11 — T-5A tvrdí pokrytí „validní wave" pro `FromStream`, ale test na úspěšné načtení
  chybí.** *(hotovo 2026-07-03.)* `SoundEffectTests.cpp` měl jen testy na prázdný/garbage vstup
  (throw) — žádný test nenačítal skutečná platná WAV data a neověřoval úspěšný návrat/`Duration`.
  *CNA:* tests/…/SoundEffectTests.cpp:125-149.
  *Accept:* test s minimální validní WAV fixturou (in-memory PCM header) ověří úspěšné `FromStream`
  + `getDurationProperty() > 0`.
  *Pozn.:* žádná změna produkčního kódu. Přidán `BuildMinimalWavBytes()` (16-bit mono PCM, 0.1 s
  ticha, ručně sestavený RIFF/WAVE/fmt/data header) a
  `SoundEffectTest.FromStreamValidWavSucceedsAndReportsNonzeroDuration` — proběhl bez skipnutí
  (dummy audio zařízení funguje), `FromStream` úspěšně vrátil non-null `SoundEffect` a
  `getDurationProperty() > 0`. Celá sada 1993/1993 testů zelená.

- [x] **CP-12 — Chybí testy pro move-konstruktor a move-assignment `SoundEffectInstance` (NOXNA
  veřejné členy).** *(hotovo 2026-07-03.)* `CLAUDE.md` požaduje test pro každou veřejnou
  metodu/operátor.
  *CNA:* SoundEffectInstance.cpp:82-128; tests/…/SoundEffectInstanceTests.cpp.
  *Accept:* test ověří přenos `track_`/`State_`/vlastností a že zdrojový objekt po move je bezpečně
  disposed-like (žádný double-free při destrukci).
  *Pozn.:* žádná změna produkčního kódu. Přidány `MoveConstructorTransfersTrackAndProperties`
  (přehrávající instanci přesune, ověří stejný `MIX_Track*`, `Volume`, `State==Playing`, a že
  zdrojová instance po move má `IsDisposed==true` a `track_==nullptr`) a
  `MoveAssignmentTransfersTrackAndDestroysPreviousOne` (cílová instance už vlastní vlastní track,
  který se má zahodit před převzetím zdrojova). Celá sada 1995/1995 testů zelená, bez pádu (což
  by double-free typicky projevilo).

- [x] **CP-13 — Chybí testy pro `Stop(false)` (non-immediate) na `SoundEffectInstance` i
  `DynamicSoundEffectInstance`.** *(hotovo 2026-07-03.)* Testováno bylo jen `Stop()`/`Stop(true)`.
  *CNA:* tests/…/SoundEffectInstanceTests.cpp, tests/…/DynamicSoundEffectInstanceTests.cpp.
  *Accept:* test na statické instanci (`Stop(false)` nechá doznít smyčku) a na dynamické
  (`Stop(false)` hází po opravě CP-5).
  *Pozn.:* dynamická část už byla pokrytá jako součást CP-5 opravy
  (`StopFalseWhileNeverPlayedIsSafeNoOp`/`StopFalseAfterPlayingThrowsInvalidOperation`). Přidán
  chybějící statický test `SoundEffectInstanceTest.StopFalseDoesNotCutOffLoopedPlaybackImmediately`
  — nastaví `IsLooped=true`, `Play()`, pak `Stop(false)` a ověří, že `State` zůstává `Playing`
  (smyčka se jen ukončí pro příští cyklus, přehrávání se okamžitě nezastaví). Celá sada
  2000/2000 testů zelená.

- [x] **CP-14 — Chybí regresní test na opakované `Play()` během `State==Playing`.** *(hotovo
  2026-07-03 — vyřešeno jako vedlejší efekt CP-1.)* Přímo by odhalil CP-1; dnešní test volal
  `Play()` jen jednou.
  *CNA:* tests/…/SoundEffectInstanceTests.cpp:122-130.
  *Accept:* test zavolá `Play()` dvakrát za sebou a ověří, že stav zůstává `Playing` bez restartu.
  *Pozn.:* CP-1's fix (2026-07-02) už přidal přesně tento test —
  `SoundEffectInstanceTest.RepeatedPlayWhileAlreadyPlayingDoesNotRestartTrack` volá `Play()`
  dvakrát, ověřuje `State==Playing` a navíc (silněji než accept vyžaduje) přes
  `MIX_GetTrackPlaybackPosition`, že se přehrávání nerestartovalo od začátku. Žádná nová práce
  nebyla potřeba, jen dodatečné zaškrtnutí položky v backlogu.

#### 7.2 XACT (AudioEngine, SoundBank, WaveBank, Cue, AudioCategory, RendererDetail)

- [x] **XA-1 — `SoundBank::PlayCue` čistí fire-and-forget cues podle uplynulého času, ne podle
  stavu přehrávání — dlouhé zvuky se přeruší.** *(hotovo 2026-07-02.)* `fireAndForget_` se ve
  `PlayCue` mazal podmínkou `now - faf.created >= 5s` bez ohledu na to, zda
  `faf.cue->getIsPlayingProperty()` je stále `true` (na rozdíl od `getIsInUseProperty()`, která
  správně kontroluje `IsPlaying`). Jakýkoli fire-and-forget cue/hudba delší než 5 s se při dalším
  `PlayCue` na stejné bance nuceně zastavil/zničil, i když ještě hrál.
  *FNA:* SoundBank.cs:28-36 (`IsInUse` dle skutečného stavu), SoundBank.cs:105-119 (destruktor drží
  objekt naživu, dokud `IsInUse`).
  *CNA:* SoundBank.cpp:116-135.
  *Accept:* sweep podmínka se změní na „již nehraje" (`!faf.cue->getIsPlayingProperty()`), případně
  kombinace se safety-net timeoutem (řádově minuty, ne 5 s); test simulující dlouho hrající cue
  prokazující, že se nezastaví předčasně.
  *Pozn.:* sweep teď maže jen dokončené cues (`!IsPlaying`) plus cokoliv za `kFireAndForgetSafetyNet`
  = 5 minut (D6 default), i kdyby pořád „hrálo" — pojistka proti neomezenému růstu, pokud volající
  jen pořád `PlayCue`uje smyčkovaný/velmi dlouhý cue. Přidán `SoundBankTestAccess` (friend, mirror
  `SoundEffectInstanceTestAccess`/`MicrophoneTestAccess`) s `FireAndForgetCount`/
  `BackdateLastFireAndForget` — protože `Cue` se sám nikdy nevrací ze stavu `Playing` bez explicitního
  `Stop()` (žádná reálná detekce dohrání), jediný způsob, jak rychle a deterministicky (bez
  reálného čekání) otestovat 5s/5min hranice, je uměle „posunout" čas vzniku záznamu. Nový
  `FireAndForgetCueSurvivesSweepPastOldFiveSecondThresholdWhileStillPlaying` ověřen přes `git stash`
  — bez opravy selže (staré chování smaže 30s starý, stále hrající záznam), s opravou projde.
  Druhý test `FireAndForgetCueIsForceSweptPastSafetyNetEvenIfStillPlaying` ověřuje samotnou
  pojistku (nediskriminuje staré/nové chování, obě smetou 10 min starý záznam).

- [x] **XA-2 — `WaveBank::GetSoundEffect` uniká heap-alokovaný `SoundEffect` z `FromStream` pro
  8-bit PCM a ADPCM vlny.** *(hotovo 2026-07-02.)* `cached.emplace(*SoundEffect::FromStream(ss))`
  dereferencuje `SoundEffect*` vrácený z `new SoundEffect(...)` a zkopíruje ho do `std::optional`;
  původní heap objekt se nikdy neuvolní (`delete` chybí). Leak nastává při každém prvním přístupu
  k libovolné 8-bit PCM nebo ADPCM položce ve wavebance.
  *CNA:* WaveBank.cpp:253, WaveBank.cpp:263.
  *Accept:* výsledek `FromStream` se buď obalí do `std::unique_ptr` a přesune/zkopíruje bez leaku,
  nebo se hned po zkopírování do `cached` smaže; test (ASan/leak-check) prokazující, že opakované
  `GetSoundEffect` na stejném indexu nedělá nový leak.
  *Pozn.:* obě místa (8-bit PCM ř. 253, ADPCM ř. 263 — identický vzor) obalena do
  `std::unique_ptr<SoundEffect>`, hodnota přesunuta (`std::move`) do `cached`. Nový
  `WaveBankTest.GetSoundEffectFor8BitPcmEntrySucceeds` (fixture builder zparametrizován o
  `bankName`/`eightBitPcm`, resp. `wavebankName`/`cueName`, nová jména aby nekolidovala s
  existující `"TestWaveBank"` registrací ve sdíleném `AudioEngine`) reálně přehraje 8-bit PCM
  vlnu. Leak empiricky ověřen samostatným ASan+LeakSanitizer buildem (`cmake-build-asan`,
  smazán po ověření, není součástí repa): **před opravou** LeakSanitizer nahlásí přesně
  `WaveBank.cpp:253` → `SoundEffect::FromStream` (136 B, 3 alokace); **po opravě** beze stop.
  ADPCM (ř. 263) používá identický fix, ale nemá vlastní fixture (ADPCM parsing nemá žádné testy
  vůbec — sledováno pod IN-6, ne znovu zde).

- [x] **XA-3 — `Cue::Play` ignoruje autorské `weightMin`/`weightMax` a typ výběru variace, vždy
  vybírá uniformně náhodně.** *(hotovo 2026-07-03.)* `XsbVariEntry::weightMin/weightMax` jsou
  parserem načtené, ale `Cue::Play` je nikde nepoužívala — vždy použila
  `std::uniform_int_distribution` přes všechny entries místo vážené pravděpodobnosti. Chybělo i
  rozlišení ostatních XACT variation types (Ordered/OrderedFromRandom/RandomNoImmediateRepeats/
  Shuffle) — `var.lastSelected` je deklarováno, ale nikde se nečte ani nezapisuje.
  *CNA:* Cue.cpp:139-197, XactTypes.hpp:88-105.
  *Accept:* výběr respektuje `weightMin`/`weightMax` (vážený náhodný výběr) alespoň pro náhodné
  typy; pro ne-náhodné typy implementace nebo zdokumentovaná odchylka; test ověřující, že entry s
  vahou blížící se 100 je vybírána statisticky výrazně častěji.
  *Pozn.:* FAudio (`get_active_variation_index`, FACT_internal.c:467-525) používá **stejný**
  vážený algoritmus pro VŠECHNY non-interaktivní typy (wave/sound/compact_wave) — žádné
  Ordered/Shuffle/RandomNoImmediateRepeats FAudio samo neimplementuje, takže to není součástí
  reálného FNA chování k dohnání. `Cue::Play` teď počítá `totalWeight = Σ(weightMax-weightMin)`
  a vybírá entry váženou loterií 1:1 podle FAudio algoritmu (sken od posledního entry, `value >
  (remaining - weight)`); degenerovaný případ (`totalWeight==0` — dnes jen interaktivní typ 3,
  kde se `var_min`/`var_max` zatím neparsují do `XsbVariEntry`) padá zpět na uniformní výběr,
  zdokumentováno v CHECKLIST.md. Nový test
  `CueTest.PlayWeightedVariationFavorsHigherWeightEntryStatistically` (2 sound entries, váhy 1 a
  99 ze 100, 200x `Play()`, práh 80 %) ověřen na PŮVODNÍM (pre-fix) kódu přes `git stash` — bez
  opravy konzistentně kolem 45-55 % (uniform), s opravou konzistentně >90 %. Celá sada
  1988/1988 testů zelená.

- [x] **XA-4 — `AudioEngine` dvouparametrový konstruktor tiše zahazuje `lookAheadTime` i
  `rendererId` bez zdokumentované odchylky.** *(hotovo 2026-07-03.)* Oba parametry byly
  zakomentované a nikam se nepoužívaly, ale doxygen je popisoval, jako by měly efekt.
  *FNA:* AudioEngine.cs:112-225.
  *CNA:* AudioEngine.cpp:46-54; AudioEngine.hpp:43-52.
  *Accept:* buď `rendererId` použít k výběru mezi budoucími backend-rendery, nebo minimálně doplnit
  `//` komentář v `.cpp` a upravit doxygen v `.hpp`; test ověřující, že konstruktor s libovolným
  `rendererId`/`lookAheadTime` nehází a chová se stejně jako jednoparametrový ctor.
  *Pozn.:* zvolena zdokumentovaná odchylka (ne implementace výběru rendereru) — CNA má jediný
  backend (SDL3_mixer), takže není mezi čím vybírat. Doxygen v `.hpp` teď explicitně říká, že oba
  parametry jsou přijímány jen kvůli API kompatibilitě a nemají efekt (libovolná hodnota, včetně
  neznámého `rendererId`, se chová stejně jako jednoparametrový ctor); `.cpp` má `//` komentář se
  stejným vysvětlením. Existující test `TwoArgConstructorLoadsFixtureWithRendererAndLookAhead`
  testoval jen "rozumné" hodnoty (`TimeSpan::Zero`, `"SDL3_mixer"`) — přidán nový
  `ThreeArgConstructorWithArbitraryRendererAndLookAheadBehavesLikeSingleArg` s nesmyslným
  `rendererId` a nenulovým `lookAheadTime`, ověřující `!IsDisposed`, neprázdné
  `RendererDetails` a funkční `GetCategory("Default")`. Celá sada 1996/1996 testů zelená.

- [x] **XA-5 — Testy `AudioCategory`/`Cue` neověřují reálný efekt `Pause`/`Resume`/`Stop`/
  `SetVolume` na běžící cue.** *(hotovo 2026-07-03.)* Testováno bylo jen `EXPECT_NO_THROW` bez
  jakéhokoli aktivního `Cue` v kategorii — přestože `AudioCategory.hpp` explicitně dokumentuje,
  že tyto metody „route to every currently active Cue... and have a real, immediate effect on
  playback".
  *CNA:* tests/.../AudioCategoryTests.cpp:130-160.
  *Accept:* nový test vytvoří `SoundBank`+`Cue` ve fixture s kategorií, zavolá `cue->Play()`, pak
  `category.Pause()`/`.Stop()`/`.SetVolume()` a ověří přes `getIsPausedProperty()`/
  `getIsStoppedProperty()`, že efekt skutečně nastal.
  *Pozn.:* žádná změna produkčního kódu. Přidána minimální `.xsb` fixture (jeden simple cue,
  sound s `categoryIndex=0` = "Default", žádná wavebanka potřeba — `Cue::Play()` nastaví
  `categoryIdx_`/`state_` a zaregistruje se do `activeCues` bez ohledu na to, jestli se najde
  reálná wavebanka) a `SharedBank()`. Nový test
  `PauseResumeStopRouteToRealActiveCueInCategory` reálně ověřuje `Pause→IsPaused`,
  `Resume→IsPlaying`, `Stop→IsStopped` na skutečném zaregistrovaném Cue. **Vedlejší nález** (mimo
  rozsah XA-5, nezaznamenáno jako nová položka): `AudioEngine::SetCategoryVolumeInternal`
  (AudioEngine.cpp:224-234) má komentář „Cue would need to re-apply volume — skipped for
  simplicity" — `SetVolume()` tedy ve skutečnosti **nic nedělá** na aktivních cues, jen uloží
  hodnotu do `categoryVolumes`; test proto `SetVolume` jen ověřuje `EXPECT_NO_THROW`, ne reálný
  efekt (na rozdíl od Pause/Resume/Stop). Celá sada 2006/2006 testů zelená.

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

- [x] **IN-2 — Over-read u non-compact XWB entry s `entryMetaDataSize < 24`.** *(hotovo 2026-07-03.)*
  Kód vždy provede všech 6 `u32()` čtení (24 B) *před* kontrolou `entryMetaDataSize < 24`, teprve
  pak kurzor přesune zpět. Pro starší (užší) formáty tak `loopStart`/`loopTotal` obsahují bajty
  z cizí paměti (dat další entry, nebo mimo segment u posledního záznamu — riziko výjimky
  "read past end" u konce bufferu). FAudio čte přesně `dwEntryMetaDataElementSize` bajtů a zbytek
  nechává vynulovaný.
  *Soubor:* src/CNA/Internal/Audio/XactParser.cpp:458-476.
  *Accept:* číst pole podmíněně/omezeně na `entryMetaDataSize`, ne natvrdo všech 24 B; test s
  non-compact `.xwb` fixture kde `entryMetaDataSize < 24` (dnešní testy pokrývají jen compact
  formát).
  *Pozn.:* každé pole (`flagsAndDuration`/`fmt`/`playOffset`/`playLength`/`loopStart`/`loopTotal`)
  se teď čte podmíněně podle prahu (`>=4`, `>=8`, ... `>=24`), chybějící pole zůstávají 0 (shoda
  s FAudio zero-init + partial read). Kurzor se posouvá přesně o `entryMetaDataSize` (`ctx.skip`),
  ne pevně o 24 B — pokryje i teoretický `entryMetaDataSize > 24` případ. Nový test
  `XactParserTest.NonCompactWaveBankWithNarrowEntryMetaDataDoesNotReadForeignBytes` (fixture s
  `entryMetaDataSize=12`, poslední entry končí přesně na konci souboru) ověřen na PŮVODNÍM
  (pre-fix) kódu přes `git stash` — bez opravy test selže přesně s "read past end", s opravou
  projde. Celá sada 1982/1982 testů zelená.

- [x] **IN-3 — Integer underflow v compact-XWB `dataLength` může obejít bounds-check ve
  `WaveBank.cpp`.** *(hotovo 2026-07-03.)* Výpočet `rawOffsetUnits[i+1]*alignment - offset -
  deviations[i]` je `uint32_t` aritmetika bez guardu — u poškozeného/adverzního souboru může
  podteče na hodnotu blízkou `UINT32_MAX`. Navazující bounds-check ve `WaveBank.cpp` je taky
  `uint32_t` součet, který může sám přetéct → heap over-read/pád při stavbě výsledného
  `std::vector`.
  *Soubor:* src/CNA/Internal/Audio/XactParser.cpp:449-452, downstream
  src/Microsoft/Xna/Framework/Audio/WaveBank.cpp:221-228.
  *Accept:* saturující/checked odečet (clamp na 0 nebo throw) při výpočtu `dataLength`; test s
  uměle vytvořenou deviací větší než mezera k dalšímu offsetu.
  *Pozn.:* podle D7 (throw, ne tiché clampování) — oba underflow případy (mezera k další entry
  minus deviace; poslední entry přesahující wave-data segment) teď počítají v `uint64_t` a
  vyhodí `std::runtime_error`, pokud by odečet podtekl. Downstream bounds-check ve
  `WaveBank.cpp:222` taky přepsán na `uint64_t` součet, aby nemohl sám přetéct. Nové testy
  `XactParserTest.CompactWaveBankThrowsWhenDeviationExceedsGapToNextEntry` a
  `...ThrowsWhenLastEntryOffsetExceedsWaveDataSegment` ověřeny na PŮVODNÍM (pre-fix) kódu přes
  `git stash` — bez opravy obě selžou (`throws nothing` — dřívější kód tiše podtekl místo
  vyhození výjimky), s opravou projdou. Celá sada 1984/1984 testů zelená.

- [x] **IN-4 — Nesprávný komentář a chybějící case pro variation-table typ 2.** *(hotovo
  2026-07-03.)* Komentář `// INTERACTIVE (type==2)` byl zavádějící: podle FAudia je `INTERACTIVE`
  typ **3**, typ **2** je `CLIP` (FAudio ho sám nepodporuje). Catch-all `else` větev v CNA tak
  potichu naparsovala i typ 2/5/6/7 stejným 16bajtovým layoutem jako typ 3.
  *Soubor:* include/CNA/Internal/Audio/XactTypes.hpp:101, src/CNA/Internal/Audio/XactParser.cpp:746,775-783.
  *Accept:* opravit komentář (INTERACTIVE=3, CLIP=2 nepodporováno), přidat explicitní kontrolu na
  type 0/1/3/4 a u neznámého typu throw/log místo tichého domýšlení layoutu.
  *Pozn.:* `XactTypes.hpp:101` komentář opraven (0=wave, 1=sound, 2=clip nepodporováno, 3=
  interactive, 4=compact_wave). `XactParser.cpp` má teď explicitní `else if (var.type == 3)`
  větev pro INTERACTIVE (16 B: code+var_min+var_max+linger) a samostatnou `else` větev pro
  cokoliv jiného (2/5/6/7), která vyhodí `std::runtime_error` — shoda s FAudiem, jehož vlastní
  switch (`FACT_internal.c:2798-2845`) taky pokrývá jen 0/1/3/4 a na `default` asertuje. Nové
  testy `XactParserTest.VariationTypeInteractiveParsesSixteenByteEntry` (pozitivní, typ 3) a
  `...VariationTypeClipThrows` (typ 2) ověřeny na PŮVODNÍM (pre-fix) kódu přes `git stash` —
  `ClipThrows` bez opravy selže (`throws nothing` — starý catch-all typ 2 tiše přijal), s opravou
  projde; `Interactive...` prochází v obou verzích (byte layout pro typ 3 byl už dřív správný,
  jen špatně pojmenovaný/dosažený). Celá sada 1990/1990 testů zelená.

- [x] **IN-5 — `XactTypes.hpp` stále používá holé `///` komentáře místo Doxygen bloků.** *(hotovo
  2026-07-03.)* V rozporu s `CLAUDE.md` („Never use bare `///` comments on public API members")
  — SPDX bylo doplněno (T-1A), ale `///`→`/** @brief */` konverze nikdy nebyla samostatným úkolem.
  *Soubor:* include/CNA/Internal/Audio/XactTypes.hpp (celý soubor).
  *Accept:* převést všechny `///`/`//` popisky členů struktur na `/** @brief … */` bloky.
  *Pozn.:* celý soubor přepsán — každá struktura/enum i každý člen má teď `/** @brief … */` blok
  (i dříve zcela nekomentované členy, kvůli konzistenci s pravidlem „every .hpp file"); `ParseXgs`/
  `ParseXwb`/`ParseXsb` mají plný blok s `@param`/`@return`. Čistě dokumentační změna — žádné
  chování se nemění. Celá sada 1996/1996 testů zelená (beze změny počtu testů).

- [x] **IN-6 — Tenké testovací pokrytí `XactParser` (4 testy) nekryje bezpečnostně/funkčně
  kritické větve.** *(hotovo 2026-07-03.)* Chybělo: poškozené/zkrácené hlavičky a špatná magic
  čísla pro všechny 3 formáty, non-compact `.xwb` (jen compact byl pokryt), `entryMetaDataSize<24`
  non-compact fallback (IN-2), `SOUND_FLAG_HAS_RPC`/`SOUND_FLAG_HAS_DSP` (IN-1), ADPCM formát,
  všechny 4 typy variation table, `RAMP` forma PITCH/VOLUME eventů (jen "equation" forma byla
  testovaná). Mezera v testech přímo korelovala s neodhalenými bugy IN-1/IN-2.
  *Soubor:* tests/CNA/Internal/Audio/XactParserTests.cpp (celý soubor).
  *Accept:* přidat fixtures/testy alespoň pro: zkrácený soubor → throw (všechny 3 formáty), špatné
  magic → throw, non-compact `.xwb` s `entryMetaDataSize==24` i `<24`, `HAS_DSP`/`HAS_RPC` sound,
  ADPCM entry, všechny 4 variation-table typy, RAMP-formu PITCH eventu.
  *Pozn.:* žádná změna produkčního kódu — čistě rozšíření pokrytí (8→22 testů v tomto souboru).
  Přidáno: 6 testů zkrácený/špatný magic (po jednom pro `ParseXgs`/`ParseXwb`/`ParseXsb` ×
  2 druhy), `BuildNonCompactAdpcmXwbFixture` (jeden plný `entryMetaDataSize==24` ADPCM entry —
  pokrývá zároveň standardní 24B layout i `blockAlign`/`samplesPerBlock` odvození z
  `wBlockAlign`), `BuildXsbWithRpcThenSecondSound` (mirror IN-1's DSP testu, ale pro
  `SOUND_FLAG_HAS_RPC`), `BuildPitchRampEventBytes` + test na RAMP formu PITCH eventu,
  a `BuildXsbWithVariationOfType` rozšířen o typy 0 (WAVE) a 1 (SOUND) — spolu s existujícími
  typy 3 (INTERACTIVE, IN-4) a 4 (COMPACT_WAVE, nově přidán) tak všechny 4 typy mají přímé
  parser-level testy. Celá sada 2018/2018 testů zelená.

#### 7.4 Microphone, datové třídy, enumy, výjimky

> `AudioEmitter`, `AudioListener`, `AudioChannels`, `AudioStopOptions`, `MicrophoneState` a všechny
> 3 audio výjimky (`InstancePlayLimitException`, `NoAudioHardwareException`,
> `NoMicrophoneConnectedException`) zkontrolovány řádek po řádku — plně kompatibilní, žádné nové
> úkoly.

- [x] **MC-1 — `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` nedeleguje na `SoundEffect`,
  vlastní vzorec má jinou přesnost než FNA.** *(hotovo 2026-07-03.)* FNA volá
  `SoundEffect.GetSampleDuration`/`GetSampleSizeInBytes(sizeInBytes, SampleRate,
  AudioChannels.Mono)`, které zaokrouhlují na celé milisekundy. CNA `Microphone::GetSampleDuration`
  místo toho počítala vlastní `seconds = sizeInBytes/(SAMPLERATE*channels*2)` bez zaokrouhlení —
  pro nedělitelné hodnoty (např. 100 B @ 44100 Hz mono) vracela jinou hodnotu než FNA. Toto bylo
  přesně nesplněné accept-kritérium T-4A (viz poznámka u T-4A výše) — `GetSampleSizeInBytes` byla
  numericky ekvivalentní, ale duplicitní.
  *FNA:* Microphone.cs:172-188; SoundEffect.cs:363-387.
  *CNA:* Microphone.cpp:161-176 (vlastní vzorec); SoundEffect.cpp:334-363 (správná implementace,
  na kterou by se mělo delegovat).
  *Accept:* `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` volají
  `SoundEffect::GetSampleDuration(sizeInBytes, SAMPLERATE, AudioChannels::Mono)`/
  `GetSampleSizeInBytes(...)` (žádná duplicitní matematika). Test na neceločíselnou hranici (100 B)
  ověřující useknutí na celé ms shodné s FNA.
  *Pozn.:* obě metody teď jednořádkově volají `SoundEffect::GetSampleDuration`/
  `GetSampleSizeInBytes(…, getSampleRateProperty(), AudioChannels::Mono)` — žádná duplicitní
  matematika, přesně jako FNA (Microphone.cs:172-188). Nový test
  `GetSampleDurationDelegatesToSoundEffectWithMonoAndSampleRate` (100 B → 1,133 ms useknuto na
  1 ms) ověřen na PŮVODNÍM (pre-fix) kódu přes `git stash` — bez opravy selže (staré vlastní
  vzorec vrátí neuseknutou hodnotu), s opravou projde. Doprovodný test pro
  `GetSampleSizeInBytes` numericky nerozlišuje staré/nové chování (obě jsou ekvivalentní), ale
  pinuje delegaci do budoucna. Celá sada 1986/1986 testů zelená.

- [x] **MC-2 — Zastaralý komentář a mrtvá deklarace `friend class MicrophoneFactory` v
  `Microphone.hpp`.** *(hotovo 2026-07-03.)* `MicrophoneFactory` nikde v repozitáři neexistovala
  a od T-4A reálný SDL3 capture backend existuje — instance se vytvářejí přímo v
  `Microphone::getAllProperty()` (`new Microphone(...)`), ne přes žádnou factory. Komentář
  „Production Microphone instances only come from a real capture backend, which does not exist
  yet" byl teď nepravdivý.
  *CNA:* Microphone.hpp:141-145.
  *Accept:* odstranit nepoužívanou `friend class MicrophoneFactory;` a přepsat komentář tak, aby
  odpovídal aktuálnímu stavu.
  *Pozn.:* `friend class MicrophoneFactory;` odstraněn (nikde jinde v repu se nepoužíval),
  komentář přepsán na popis skutečného stavu (instance vytváří `getAllProperty()` přímo;
  `MicrophoneTestAccess` obchází enumeraci pro izolované testy). Čistě úklidová změna, celá sada
  1996/1996 testů zelená.

- [x] **MC-3 — `GetData()` při nedostupnosti dat přepíše celý požadovaný rozsah bufferu nulami,
  FNA buffer vůbec nemění.** *(hotovo 2026-07-03.)* FNA vrací jen počet skutečně přečtených
  bajtů a zbylou část bufferu nechává beze změny. CNA při `read <= 0` (žádný stream, nic
  k přečtení, i chyba SDL vracející záporné číslo) vždy vyplnila celý požadovaný rozsah nulami —
  chybový stav byl tak potichu maskován jako "0 bajtů, buffer vynulován", a zároveň to přepsalo
  případná stará platná data volajícího.
  *FNA:* Microphone.cs:149-170.
  *CNA:* Microphone.cpp:128-159 (zejména 144-158).
  *Accept:* buď formálně zdokumentovat jako schválenou odchylku v `CHECKLIST.md`, nebo zúžit
  nulování jen na "žádný otevřený stream" případ a při chybě SDL nechat buffer nedotčený; test.
  *Pozn.:* zvolena D8 preferovaná varianta — sladit s FNA (žádné nulování vůbec, ne jen zúžení).
  `GetData` teď při `read <= 0` (žádný stream, nic k přečtení, i chyba SDL) jen vrátí 0 a buffer
  nechá zcela nedotčený, přesně jako FNA (`Microphone.GetData` deleguje přímo na platformu bez
  jakéhokoliv fallback nulování). Odstraněn i teď nepoužívaný `#include <algorithm>`. Nový test
  `GetDataLeavesBufferUntouchedWhenNoDataAvailable` (buffer předvyplněný `0xAB`, po `GetData`
  musí zůstat `0xAB`) ověřen přes `git stash` — bez opravy selže (`0x00 != 0xAB`, staré chování
  buffer vynulovalo), s opravou projde. Celá sada 2019/2019 testů zelená.

- [x] **MC-4 — Chybí test, že `BufferReady` skutečně vystřelí při reálném zachytávání dat
  přesahujících `BufferDuration`.** *(hotovo 2026-07-03.)* Existující testy ověřovaly jen, že
  volání nespadne. Vzhledem k tomu, že `GetQueuedBytes`/`CheckBuffer` byly právě přepojeny na
  reálná SDL data (dříve vždy vracely 0, tedy `BufferReady` nikdy nemohlo vystřelit), bylo tohle
  nejrizikovější a nejméně prověřené chování — přímo souvisí s MC-1 (špatná `GetSampleDuration`
  mohla threshold posunout, aniž by to jakýkoli test odhalil).
  *FNA:* Microphone.cs:206-213.
  *CNA:* Microphone.cpp:218-224; tests/.../MicrophoneTests.cpp:220-229.
  *Accept:* nový test (analogicky k `MicrophoneCaptureTest`) — nastavit malé `BufferDuration`,
  zaregistrovat `BufferReady` handler, `Start()`, opakovaně volat `CheckBuffer()` v smyčce s
  timeoutem, ověřit, že handler byl zavolán alespoň jednou.
  *Pozn.:* žádná změna produkčního kódu. Nový `MicrophoneCaptureTest.
  BufferReadyFiresWhenQueuedDataExceedsBufferDuration` nastaví `BufferDuration=100ms` (minimum
  povolené), zaregistruje handler, `Start()`, a v cyklu (max 40× po 50ms) volá `CheckBuffer()`,
  dokud handler nevystřelí — ověřeno stabilně 3× po sobě bez flakiness. Zároveň opraven latentní
  test-infra problém: `MicrophoneCaptureTest::TearDown()` teď navíc volá `BufferReady.Clear()` —
  bez toho by testem lokálně zachycená lambda (odkazující na lokální proměnnou mimo scope)
  zůstala navěky připojená ke sdílenému `getDefaultProperty()` singletonu a mohla by být volána
  (use-after-scope) i pozdějšími testy ve stejném binárce. Celá sada 2020/2020 testů zelená.

- [x] **MC-5 — `GetData` nemá samostatný test pro záporný `count`, jen pro `count == 0`.** *(hotovo
  2026-07-03.)* Validace je `count <= 0`, ale existující test `GetDataZeroOrNegativeCountThrows`
  (navzdory názvu) testoval jen `count == 0`.
  *CNA:* Microphone.cpp:139-142; tests/.../MicrophoneTests.cpp:204-209.
  *Accept:* přidat `GetDataNegativeCountThrows` test s `count < 0`, ověřující `System::ArgumentException`.
  *Pozn.:* žádná změna produkčního kódu. Přidán `MicrophoneTest.GetDataNegativeCountThrows`
  (`count=-5`) hned za existující test. Celá sada 1997/1997 testů zelená.

### Fáze 8 — Druhý doplňkový audit (2026-07-04): nové nálezy nad rámec Fáze 7

> Po uzavření celého zbývajícího backlogu (T-4D, T-3F, T-3G, T-4B, T-6C, T-4C — viz jejich `*Pozn.:*`
> výše) proběhl na žádost uživatele **druhý čerstvý** line-by-line audit celého
> `Microsoft::Xna::Framework::Audio` clusteru proti FNA, strukturovaný stejně jako Fáze 7 — 4
> paralelní kontroly (Core Playback, XACT, interní backend, Mic/data/enumy/výjimky), každá jako
> samostatný agent s vlastním, nezávislým průchodem zdrojem. Nejzávažnější dva nálezy (IN-7, IN-8)
> byly navíc ručně ověřeny přímo proti `FAudio/src/FACT_internal.c` (ne jen převzaty z reportu
> agenta), stejně jako XA-6/XA-7/CP-15 přímo proti aktuálnímu zdroji CNA — viz jejich `*Pozn.:*`.
>
> ID pokračují ve stávajících prefixech z Fáze 7 (`CP`/`XA`/`IN`/`MC`), aby se nálezy daly řadit
> podle clusteru bez kolize s T-* ani s Fáze-7 čísly. Řazeno uvnitř clusteru podle závažnosti
> (reálné bugy → compliance/chování/doložené odchylky → testovací mezery).

**Nejzávažnější nálezy (rychlý přehled, detaily níže):**
- **IN-7** — kanálový počet (`nChannels`) se u KAŽDÉ `.xwb` položky (compact i non-compact) čte s
  chybným `+1` — mono se hraje jako stereo a naopak. Ověřeno přímo proti `FACT_internal.c`.
- **IN-8** — u COMPLEX zvuku s RPC nebo DSP flagem se per-track metadata (volume/code/filter/
  frequency) čtou PŘED RPC/DSP blokem místo PO něm — obrácené pořadí proti FACT, kaskádová
  korupce parsování zbytku souboru. Ověřeno přímo proti `FACT_internal.c`.
- **XA-6** — `Cue::Stop(AudioStopOptions::AsAuthored)` se chová identicky jako `Stop(Immediate)` —
  `StopInternal` vždy zavolá `active_.clear()`, což tvrdě zničí i instance, které měly jen doznít.
- **XA-7** — `SoundBank`'s fire-and-forget sweep smaže i cue, který je jen PAUSED (kontroluje jen
  `IsPlaying`), takže `category.Pause()` na fire-and-forget cue → další `PlayCue()` na stejné bance
  ho tiše zničí.
- **CP-15** — `DynamicSoundEffectInstance::Pause()`/`Resume()` jsou mrtvý kód — dědí se z base třídy,
  která pracuje s `track_`, ale `DynamicSoundEffectInstance` vždy používá vlastní `dynamicTrack_`.
- **CP-16** — `SoundEffect::MasterVolume` nemá žádný efekt na už hrající zvuky, jen na budoucí `Play()`.

#### 8.1 Core Playback (SoundEffect, SoundEffectInstance, DynamicSoundEffectInstance)

- [x] **CP-15 — `DynamicSoundEffectInstance::Pause()`/`Resume()` jsou mrtvý kód.**
  `Pause()`/`Resume()` (SoundEffectInstance.cpp:373-397) nejsou `virtual` a vždy pracují s
  chráněným `track_`. `DynamicSoundEffectInstance::Play()`/`Stop()` mají vlastní override, ale
  pracují výhradně s vlastním `dynamicTrack_` (`track_` u dynamic instance zůstává navždy
  `nullptr`) — volání `Pause()`/`Resume()` na `DynamicSoundEffectInstance` je tedy vždy tichý no-op.
  *FNA:* SoundEffectInstance.cs:375-397 (sdílené `handle` pole pro static i dynamic).
  *CNA:* SoundEffectInstance.hpp:88-92 (nevirtuální); DynamicSoundEffectInstance.hpp/.cpp (žádný
  override, žádná reference na `track_`).
  *Accept:* `DynamicSoundEffectInstance::Pause()`/`Resume()` (přes `virtual` na base, nebo sdílenou
  abstrakcí handle) skutečně pozastaví/obnoví `dynamicTrack_`; test: `Play()`→`Pause()`→assert
  `State==Paused`→`Resume()`→assert `State==Playing`, pod dummy driverem.
  *Pozn.:* `Pause()`/`Resume()` teď `virtual` na base; `DynamicSoundEffectInstance` má vlastní
  override operující na `dynamicTrack_` (stejný vzorec jako existující `Play()`/`Stop()`
  override). Přidány 2 testy (`Play→Pause→Resume` přímo i přes `SoundEffectInstance&` base
  referenci, ověřující virtual dispatch). `git stash` potvrdil selhání obou proti staré
  (nevirtuální) implementaci.

- [x] **CP-16 — `SoundEffect::MasterVolume` neovlivňuje už hrající zvuky.**
  `MasterVolume_` je statický float násobený do gain jen v okamžiku `Play()`/`setVolumeProperty()`.
  Změna `MasterVolume` po spuštění zvuku nemá na již hrající instance (ani na fire-and-forget
  `SoundEffect::Play()` tracky) žádný vliv — SDL3_mixer přitom má reálný globální mixer gain
  (`MIX_SetMixerGain`/`MIX_GetMixerGain`), který se nikde nepoužívá.
  *FNA:* SoundEffect.cs:51-70 (`MasterVolume` jde přímo na sdílený mastering voice).
  *CNA:* SoundEffect.cpp:210-223,298-311; SoundEffectInstance.cpp:314,541.
  *Accept:* `setMasterVolumeProperty` použije `MIX_SetMixerGain` (nebo re-aplikuje gain na všechny
  živé tracky); test ověří `MIX_GetTrackGain` po změně `MasterVolume` na již hrající instanci.
  *Pozn.:* `getMasterVolumeProperty`/`setMasterVolumeProperty` teď čtou/píší přímo
  `MIX_GetMixerGain`/`MIX_SetMixerGain` (živě, žádná lokální cache — stejně jako FNA vždy
  dotazuje/nastavuje reálný FAudio master voice). Aby nedošlo ke dvojímu započtení, master volume
  se přestalo násobit do gain jednotlivých tracků (`ApplyTrackProperties` ztratil parametr
  `masterVolume`; `SoundEffect::Play()`'s fire-and-forget cesta a `SoundEffectInstance::
  setVolumeProperty`/`DynamicSoundEffectInstance::Play()` už ho také nenásobí) — mixer gain je
  teď jediný mechanismus a aplikuje se živě na všechny tracky včetně už hrajících, bez nutnosti
  cokoliv ručně znovu-aplikovat. Přidán test ověřující `MIX_GetMixerGain` (ne `MIX_GetTrackGain`,
  který zůstává záměrně konstantní) po změně `MasterVolume` na už hrající instanci — první verze
  testu omylem kontrolovala jen `getMasterVolumeProperty()`, což by prošlo i proti staré
  (nefunkční) implementaci, protože ta taky jen round-tripuje přes statické pole; opraveno na
  přímé `MIX_GetMixerGain` volání, `git stash` pak potvrdil selhání proti staré implementaci.

- [x] **CP-17 — `SoundEffect`'s loop region (`loopStart`/`loopLength`) se zachytí, ale nikdy nepoužije.**
  Bufferová konstrukce s explicitním loop rozsahem uloží `loopStart_`/`loopLength_`, ale nic je
  nikdy nečte. `FromStream` navíc vůbec neparsuje WAV `smpl` chunk. `Play()` vždy loopuje celý
  buffer (`MIX_PROP_PLAY_LOOPS_NUMBER`), nikdy jen autorský loop rozsah.
  *FNA:* SoundEffect.cs:476-513 (`smpl` chunk v `FromStream`); SoundEffectInstance.cs:350-361
  (`LoopBegin`/`LoopLength` nastaveny před submitem bufferu).
  *CNA:* SoundEffect.hpp:35-36,82-88; SoundEffect.cpp:118-129,406-452; SoundEffectInstance.cpp:314-337.
  *Accept:* `FromStream` parsuje `smpl` loop pointy jako FNA; `Play()` aplikuje `loopStart_`/
  `loopLength_` přes SDL3_mixer's `MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER` (a délku, pokud to jde);
  test s nenulovým loop rozsahem ověří, že se loopuje jen daný úsek, ne celý buffer.
  *Pozn.:* `SoundEffectInstance` teď při konstrukci kopíruje `loopStart_`/`loopLength_` ze
  `SoundEffect` (stejný vzorec jako `nativeAudioHandle_` — CP-7 zakazuje držet si `SoundEffect&`).
  `Play()` nastaví `MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER`; pro délku SDL3_mixer nemá žádnou
  "loop end" vlastnost odlišnou od "konec celé stopy" — použito `MIX_PROP_PLAY_MAX_FRAME_NUMBER`,
  což ale (na rozdíl od FNA/XAudio2's `LoopBegin`/`LoopLength`) zkrátí i úplně první, před-loop
  přehrání na `loopStart_+loopLength_`, ne jen další iterace — zdokumentováno jako přijatá
  odchylka v CHECKLIST.md. `FromStream` teď navíc nezávisle na `MIX_LoadAudio_IO` skenuje syrové
  WAV bajty pro `smpl` chunk (`TryParseWavSmplChunk`) — čistě bajtový parser, žádné SDL typy.
  Přidány 4 testy (buffer-ctor propagace do instance, `FromStream` s/bez `smpl` chunku,
  zkrácený/poškozený `smpl` chunk nesmí spadnout) přes nový `SoundEffectInstanceTestAccess::
  LoopStart/LoopLength` (SDL3_mixer nemá způsob, jak zpětně přečíst loop-start/max-frame hodnoty
  předané `MIX_PlayTrack`, takže skutečný namixovaný efekt nelze black-box ověřit bez dekódování
  reálného zvukového výstupu). `git stash` potvrdil selhání (kompilační, kvůli chybějícím polím)
  ve všech testovacích souborech sdílejících `SoundEffectInstanceTestAccess.hpp`. Čisté pod
  ASan+LeakSanitizer.

- [x] **CP-18 — Chybějící audio hardware hlásí `std::runtime_error`, nikdy `NoAudioHardwareException`.**
  `AudioMixer::GetMixer()` (viz i IN-11) throwuje `std::runtime_error`, který dědí z `std::exception`,
  ne z `System::Exception` — kód dělající `catch (const System::Exception&)` ho vůbec nezachytí.
  `NoAudioHardwareException` existuje a je otestovaná izolovaně, ale nikde se skutečně nethrowuje.
  *FNA:* SoundEffect.cs:784-817 (`Device()` throwuje `NoAudioHardwareException`).
  *CNA:* src/CNA/Internal/Audio/AudioMixer.cpp:15-36; volající SoundEffect.cpp:86,143,298,428,
  SoundEffectInstance.cpp:292.
  *Accept:* `GetMixer()` (nebo volající) throwuje `Microsoft::Xna::Framework::Audio::
  NoAudioHardwareException` místo `std::runtime_error`; test simulující selhání vytvoření mixeru
  ověří správný typ výjimky. (Provázáno s XA-9 — řešit spolu.)
  *Pozn.:* konzultováno s uživatelem spolu s XA-9 (stejná otázka, stejná odpověď) — zvolena
  zdokumentovaná odchylka. Viz XA-9's `*Pozn.:*` pro plné odůvodnění (přepis `SharedEngine()` ve
  4 test souborech by byl nutný i pro tuhle část, protože `AudioEngine::NoAudioHardwareException`
  by teoreticky mělo jít throwovat i z `AudioEngine`'s vlastního konstruktoru, ne jen z
  `AudioMixer::GetMixer()`). Zdokumentováno v CHECKLIST.md.

- [x] **CP-19 — Pan u stereo zdroje ztlumí celý opačný kanál místo crossfeed blendu.**
  `ApplyTrackProperties`'s pan vzorec (`left=(pan<0)?1:(1-pan)`) u `Pan=1.0` (tvrdě doprava) dá
  `left gain=0` — u stereo zdroje tak úplně zmizí levý kanál. FNA má explicitní komentář, že tvrdé
  panování NEMÁ eliminovat celý kanál, a používá plnou 4-koeficientovou matici pro stereo→stereo.
  U MONO zdrojů je CNA vzorec bit-přesně stejný jako FNA — problém je jen u stereo obsahu.
  *FNA:* SoundEffectInstance.cs:606-648 (`SetPanMatrixCoefficients`).
  *CNA:* SoundEffectInstance.cpp:51-67; SoundEffect.cpp:313-316 (duplicitní vzorec).
  *Accept:* buď doložit jako akceptovanou odchylku v CHECKLIST.md (SDL3_mixer's `MIX_StereoGains`
  nemá crossfeed API), nebo ručně mixovat stereo pan; test porovnávající CNA gains proti FNA matici
  pro stereo zdroj při několika pan hodnotách.
  *Pozn.:* konzultováno s uživatelem — zvolena zdokumentovaná odchylka, ne implementace. Reálný
  ruční mix by musel sdílet SDL3_mixer's JEDINÝ per-track "cooked callback" slot s T-4C's už
  hotovým DSP filtrem (sloučení pan-crossfeed matematiky a filtru do jednoho callbacku +
  registrace pro každou stereo instanci, ne jen filtrovanou) — reálné riziko regrese v už
  odladěném a otestovaném filter kódu, na rozdíl od předchozích "implement" rozhodnutí této session
  (T-3F/T-3G/T-4C), která nezasahovala do sdílené, už fungující infrastruktury. Zdokumentováno v
  CHECKLIST.md.

- [x] **CP-20 — `setPanProperty()` ignoruje aktivní `Apply3D` stav.**
  FNA má `is3D` latch — jakmile byl `Apply3D` alespoň jednou zavolán, `Pan` setter už jen aktualizuje
  hodnotu property, nezasahuje do skutečné výstupní matice (ta se přepočítá až dalším `Apply3D`).
  CNA nemá žádný ekvivalent `is3D_` — `setPanProperty()` vždy okamžitě přepíše track's stereo gains,
  takže manuální `setPanProperty()` mezi dvěma `Apply3D()` voláními (nebo po jediném `Apply3D()`)
  přepíše 3D pozicování špatnou hodnotou. Stejná třída chyby jako už opravené CP-3, ale v opačném
  pořadí volání.
  *FNA:* SoundEffectInstance.cs:52-84 (`Pan` setter: `if (is3D) return;`).
  *CNA:* SoundEffectInstance.cpp:556-583 (`setPanProperty`); `Apply3D` na řádcích 471-506 (žádný
  `is3D_`-ekvivalent flag).
  *Accept:* přidat `is3D_`-ekvivalentní flag; `setPanProperty` po `Apply3D()` už jen aktualizuje
  property, nezapisuje do tracku; test: `Apply3D(...)` → `setPanProperty(x)` → ověřit, že skutečné
  track gains pořád odpovídají 3D pozici, ne `x`.
  *Pozn.:* přidán `is3D_` (nikdy se nereseituje, stejně jako FNA); `setPanProperty` po nastavení
  `Pan_` vrací dřív, pokud `is3D_==true`. SDL3_mixer nemá stereo-pan getter (stejné omezení jako
  u CP-3/T-4B), takže test ověřuje přímo `is3D_` stav přes nový `SoundEffectInstanceTestAccess::
  Is3D`, ne skutečné track gains. `git stash` potvrdil kompilační selhání (chybějící pole) ve
  všech souborech sdílejících `SoundEffectInstanceTestAccess.hpp`.

- [x] **CP-21 — `AudioCategory::SetVolume`'s doc v hlavičce `AudioCategory.hpp` odpovídá starému,
  už opravenému chování (drobný nález, patří spíš do XA — viz XA-10, zmíněno zde pro úplnost, ne
  duplicitně řešeno).**
  *Pozn.:* vyřešeno spolu s XA-10 (viz jeho `*Pozn.:*` výše).

- [x] **CP-22 — Test-mezera: `SoundEffect`'s move ctor/move-assignment nemá vlastní test.**
  `static_assert` ověřuje jen move-constructibility/assignability; žádný test skutečně nepřesune
  `SoundEffect` a neověří, že instance vytvořená před přesunem (přes `impl_` keep-alive) dál funguje.
  *CNA:* SoundEffect.hpp:105-109; SoundEffectTests.cpp:27-30 (jen `static_assert`).
  *Accept:* přidat `SoundEffectTest.MoveConstructor.../MoveAssignment...` testy analogicky k CP-12
  (u `SoundEffectInstance`), ověřující že `SoundEffectInstance` vytvořená z přesunutého `SoundEffect`
  dál přehrává správně.
  *Pozn.:* přidány `MoveConstructedEffectStillCreatesAWorkingInstance`/
  `MoveAssignedEffectStillCreatesAWorkingInstance` — vytvoří instanci z přesunutého `SoundEffect`,
  zavolají `Play()` a ověří `State==Playing`. Čistě testovací doplněk, žádná změna produkčního
  kódu (proto bez `git stash` kroku).

- [x] **CP-23 — Test-mezera: bufferová konstrukce s `loopStart`/`loopLength` nemá success-path test.**
  Existující test pokrývá jen exception path pro špatný rozsah, ne skutečný efekt platného loop
  rozsahu na přehrávání — přesně to by odhalilo CP-17 dřív.
  *CNA:* SoundEffectTests.cpp:171-179.
  *Accept:* přidat test s nenulovým `loopStart`/`loopLength` až po opravě CP-17 (nebo test
  dokumentující dnešní "mrtvé pole" chování, pokud se CP-17 odloží).
  *Pozn.:* vyřešeno spolu s CP-17 — `BufferRangeConstructorPropagatesLoopRegionToInstance` (viz
  CP-17's `*Pozn.:*` výše pro plný seznam nových testů).

#### 8.2 XACT (AudioEngine, AudioCategory, Cue, SoundBank, WaveBank)

- [x] **XA-6 — `Cue::Stop(AudioStopOptions::AsAuthored)` se chová identicky jako `Stop(Immediate)`.**
  Ověřeno přímo ve zdroji: `StopInternal` nejdřív správně zavolá `pi.instance->Stop(immediate)`
  (pro `immediate=false` jen `MIX_SetTrackLoops(track,0)`, track zůstává hrát), ale HNED další
  řádek je bezpodmínečné `active_.clear()`, které zničí `unique_ptr<SoundEffectInstance>` →
  `~SoundEffectInstance()` → `Dispose()` → `DestroyTrackSafe()` → tvrdý stop. `AsAuthored` tak
  nikdy nenechá znít release/loop tail.
  *FNA:* Cue.cs:257-265 (`FACT_FLAG_STOP_RELEASE` nechá voice doznít asynchronně, cue se netvrdě
  neničí).
  *CNA:* Cue.cpp:292-306 (`StopInternal`).
  *Accept:* u reálné WaveBank-backed cue (např. `Apply3DCue`/`VolCue`-styl fixtura) `Stop(AsAuthored)`
  na loopované instanci nechá track hrát dál hned po volání (jen ukončí loop), zatímco
  `Stop(Immediate)` ho tvrdě zastaví okamžitě — test čte skutečný `MIX_Track*` přes
  `SoundEffectInstanceTestAccess`, ne jen `Cue`'s vlastní state.
  *Pozn.:* `active_.clear()` teď volá jen pro `immediate==true`; pro `AsAuthored` `active_` zůstává
  netknuté (instance se zničí až při `Cue::Dispose()`, matches FNA — nativní engine taky drží voice
  naživu, dokud release skutečně nedoběhne). Test potřeboval delší (1s) fixturu — původní sdílené
  fixtury (200 B ≈ 1ms) byly příliš krátké pro spolehlivou živou kontrolu `MIX_TrackPlaying`, nová
  fixtura přidána jako `SharedLongBank`/`"LongCue"`. `git stash` potvrdil selhání proti staré
  logice. Čisté pod ASan+LeakSanitizer. Vědomě neřešeno: `SoundBank`'s fire-and-forget sweep
  (XA-1/XA-7) může takovou "releasing" (Stopped, ale ne Paused) cue smést dřív, než release
  doopravdy doběhne — mimo rozsah tohoto nálezu, audit ho nezmínil.

- [x] **XA-7 — Fire-and-forget sweep v `SoundBank::PlayCueInternal` smaže i cue, který je jen PAUSED.**
  Ověřeno přímo ve zdroji: sweep predikát kontroluje jen `getIsPlayingProperty()` — pokud je cue
  paused (`state_==Paused`), `getIsPlayingProperty()` vrací `false`, takže se cue smete
  bezpodmínečně (`return true;`) hned při dalším `PlayCue()` na stejné bance. `getIsInUseProperty()`
  na `SoundBank` i `WaveBank` má identickou chybu.
  *FNA:* SoundBank.cs:28-36 (`IsInUse` odráží `FACT_STATE_INUSE`, které zůstává nastavené i při pauze).
  *CNA:* SoundBank.cpp:142-154 (sweep), :83-89 (`getIsInUseProperty`); WaveBank.cpp:204-210.
  *Accept:* sweep predikát (a `IsInUse`) musí brát `IsPlaying || IsPaused` (nebo obecněji "ještě ne
  `IsStopped`") jako živé; test: pauznout kategorii obsahující fire-and-forget cue, spustit další
  `PlayCue()` na stejné bance, ověřit že paused cue přežije a jde ho ještě `Resume()`.
  *Pozn.:* sweep predikát i obě `getIsInUseProperty()` (SoundBank i WaveBank) teď berou
  `IsPlaying || IsPaused` jako živé. Testy volají `Cue::Pause()` přímo (ne přes
  `AudioCategory::Pause()`) — cue-level stavový přechod je nezávislý na tom, jestli má cue reálný
  WaveBank (`Pause()`'s guard běží i s prázdným `active_`), takže postačí wavebank-less
  "Explosion" fixtura stejná jako sousední sweep testy. `git stash` potvrdil selhání všech 3
  nových testů proti staré logice.

- [x] **XA-8 — `AudioEngine::Dispose()` nekaskáduje do už zkonstruovaných `SoundBank`/`WaveBank`/`Cue`.**
  FNA přes native `OnXACTNotification` (WAVEBANKDESTROYED/SOUNDBANKDESTROYED/CUEDESTROYED) okamžitě
  nastaví `IsDisposed=true` na každém závislém wrapperu, jakmile native engine zanikne. CNA's
  `AudioEngine::Dispose()` jen resetuje vlastní `xactImpl_` — žádný `WaveBank`/`Cue` (a pro
  `SoundBank` neexistuje registr vůbec) se nedozví, že engine zanikl.
  *FNA:* AudioEngine.cs:382-432; WaveBank.cs:204-222; SoundBank.cs:270-280; Cue.cs:271-276.
  *CNA:* AudioEngine.cpp:176-185 (`Dispose`); AudioEngine.hpp:112-147 (žádný `SoundBank` registr).
  *Accept:* po `AudioEngine::Dispose()` každý `WaveBank`/`SoundBank`/`Cue` z něj vytvořený hlásí
  `getIsDisposedProperty()==true` (test pro všechny tři) — vyžaduje `SoundBank` registr symetrický
  k existujícímu `WaveBank` registru; nebo doložit jako akceptovanou odchylku v CHECKLIST.md, pokud
  je to úmyslně mimo rozsah.
  *Pozn.:* přidán `SoundBank` registr (`RegisterSoundBank`/`UnregisterSoundBank`, symetrický k
  `WaveBank`'s). `Dispose()` teď nejdřív odloží snapshoty všech tří registrů (`WaveBank*`,
  `SoundBank*`, `Cue*`) do lokálních vektorů, PAK resetuje `xactImpl_` (aby reentrantní
  `Unregister*` volání z jejich vlastních `Dispose()` byla bezpečná no-op), a teprve pak volá
  `Dispose()` na každém — v pořadí cues → soundbanks → wavebanks. Idempotentní `Dispose()` vzorec
  (`if (!isDisposed_)`) už v kódu existoval, takže žádné dvojité uvolnění, i když např. `SoundBank`
  svůj `fireAndForget_` cue později znovu zkusí disposnout. Přidán test s vlastním (ne sdíleným)
  `AudioEngine`, protože test engine disponuje. `git stash` potvrdil selhání proti staré logice.
  Čisté pod ASan+LeakSanitizer. Vědomě neřešeno: cue, který byl jen `GetCue()`ovaný, ale nikdy
  `Play()`ovaný, se do registru vůbec nedostane (stejné omezení jako existující
  `RegisterCue`/`UnregisterCue` mechanismus) — mimo rozsah tohoto nálezu.

- [x] **XA-9 — `AudioEngine`/`SoundBank`/`WaveBank` konstruktory tiše polykají chybějící soubor i
  parse chybu místo throw; `NoAudioHardwareException` se nikdy nethrowuje z `AudioEngine`.**
  Všechny tři konstruktory: nejde otevřít soubor → `cerr` + return; parse throwne → `cerr` +
  polknuto. Objekt zůstane v tichém "stub" stavu (bez kategorií/cues/waves); pozdější lookupy
  hlásí jen obecný `InvalidOperationException`, ne signál z doby konstrukce.
  `AudioEngine`'s ctor navíc nikdy nezjišťuje reálnou dostupnost audio hardware —
  `rendererDetails_` má vždy přesně jeden natvrdo daný `RendererDetail`, takže
  `NoAudioHardwareException` z `AudioEngine` nikdy nemůže vystřelit (viz i CP-18).
  *FNA:* AudioEngine.cs:117-184 (`TitleContainer.ReadToPointer` throwuje na chybějící soubor;
  `rendererCount==0` → `throw new NoAudioHardwareException()`); SoundBank.cs:73-74; WaveBank.cs:84-87.
  *CNA:* AudioEngine.cpp:64-109 (`Init`); SoundBank.cpp:42-72; WaveBank.cpp:148-192.
  *Accept:* buď (a) konstruktory throwují odpovídající `System::` výjimku na chybějící soubor/
  poškozená data podle FNA kontraktu (test s neexistující cestou a s poškozeným-ale-přítomným
  souborem pro každou třídu), nebo (b) "tiché stub" chování se zapíše do CHECKLIST.md jako
  vědomě rozhodnutá odchylka (upozornění: existující testovací fixtury, např.
  `SoundBankTests.cpp`'s `SharedEngine()`, na tomto stub chování aktivně stavějí — varianta (a)
  by je musela upravit).
  *Pozn.:* konzultováno s uživatelem — zvolena varianta (b), zdokumentovaná odchylka. `SharedEngine()`
  je nezávisle definovaná ve 4 test souborech (`CueTests.cpp`, `WaveBankTests.cpp`,
  `SoundBankTests.cpp`, `AudioCategoryTests.cpp`) a záměrně ukazuje na neexistující `.xgs` cestu;
  varianta (a) by ji musela ve všech čtyřech přepsat na reálnou fixturu a znovu ověřit ~80+ testů,
  které na ní stojí — široký, průřezový zásah do sdíleného základu kvůli okrajovému případu
  (chybějící/poškozený content soubor, chybějící audio hardware), ne uživatelsky viditelné chybě
  přehrávání. Zdokumentováno v CHECKLIST.md (spolu s CP-18).

- [x] **XA-10 — `AudioCategory.hpp`'s Doxygen odporuje skutečnému (správnému) chování `SetVolume`.**
  Doc tvrdí, že `SetVolume` neovlivní už hrající cues — od T-4D opravy to už neplatí (`SetVolume`
  se retroaktivně aplikuje, ověřeno passing testem `SetVolumeReappliesToAlreadyPlayingCueInstance`).
  *CNA:* AudioCategory.hpp:16-20,33-38 (doc); AudioEngine.cpp:224-232 (skutečné chování).
  *Accept:* přepsat oba Doxygen bloky tak, aby odpovídaly skutečnému, správnému chování (stejně
  přesně jako sousední `Pause`/`Resume`/`Stop` doc bloky).
  *Pozn.:* oba bloky (třídní doc i `SetVolume`'s vlastní) přepsány. Čistě dokumentační, žádná
  změna chování; existující `SetVolumeReappliesToAlreadyPlayingCueInstance` test dál prochází
  beze změny.

- [x] **XA-11 — Kategorie `instanceLimit`/`fadeInMS`/`fadeOutMS` se parsují, ale nikde se
  nevynucují ani neaplikují — mezera nezapsaná v CHECKLIST.md.**
  Toto bylo vědomě odloženo u T-4D (viz jeho `*Pozn.:*`), ale rozhodnutí se nikdy nepropsalo do
  CHECKLIST.md's tabulky odchylek, na rozdíl od každého jiného podobného rozhodnutí (D1-D8).
  *CNA:* XactTypes.hpp:26-30 (pole existují); XactParser.cpp:309-322 (parsují se); žádný
  spotřebitel v AudioEngine.cpp/AudioCategory.cpp/Cue.cpp.
  *Accept:* přidat řádek do CHECKLIST.md dokumentující, že fade in/out kategorie a instance-limit
  vynucování jsou mimo rozsah (nebo je implementovat).
  *Pozn.:* řádek přidán do CHECKLIST.md. Čistě dokumentační, žádná změna kódu.

- [x] **XA-12 — `AudioEngine::ContentVersion` používá syrový `int` místo `SharpRuntime::intcs`.**
  Jediná veřejná integer konstanta v celém Audio clusteru, která nepoužívá projektový alias
  (CLAUDE.md's typová tabulka).
  *CNA:* AudioEngine.hpp:31.
  *Accept:* změnit na `static constexpr SharpRuntime::intcs ContentVersion = 46;`; existující
  `ContentVersionIs46` test beze změny dál projde.
  *Pozn.:* hotovo přesně dle accept kritéria; `SharpRuntime/SharpRuntimeHelper.hpp` přidán do
  `AudioEngine.hpp`'s includes. Žádná změna testu potřeba.

- [x] **XA-13 — Test-mezera: žádný test nekonstruuje `AudioEngine`/`SoundBank`/`WaveBank` proti
  existujícímu, ale poškozenému `.xgs`/`.xsb`/`.xwb` souboru.**
  Existující testy pokrývají jen "soubor neexistuje" a "validní fixtura" — nikdy "soubor existuje,
  ale obsahuje odpad" na úrovni wrapper konstruktoru (na rozdíl od `XactParserTests.cpp`, který
  tohle testuje na úrovni samotného parseru, ne wrapperu).
  *CNA:* AudioEngineTests.cpp; SoundBankTests.cpp; WaveBankTests.cpp.
  *Accept:* přidat po jednom testu pro každou třídu s existujícím-ale-poškozeným souborem,
  ověřujícím aktuální (nebo nově rozhodnuté po XA-9) chování explicitně.
  *Pozn.:* XA-9 rozhodl ponechat současné "tiché stub" chování, takže testy uzamykají PRÁVĚ TOTO
  chování (ne nové). Přidán po jednom testu pro každou třídu: konstruktor s existujícím, ale
  poškozeným souborem nevyhodí výjimku a objekt zůstane v tichém stub stavu (`AudioEngine`/
  `SoundBank`: následné `GetCategory`/`GetCue` na libovolné jméno hodí `InvalidOperationException`,
  stejně jako pro chybějící soubor; `WaveBank`: `getIsPreparedProperty()==false`). Čistě testovací
  doplněk, žádná změna produkčního kódu.

#### 8.3 Interní backend (AudioMixer, XactParser, XactTypes)

- [x] **IN-7 — `nChannels` se u KAŽDÉ `.xwb` položky (compact i non-compact) čte s chybným `+1`.**
  Ověřeno ručně proti `FAudio/src/FACT_internal.c:1782,1793,1855,1866` — `entry->Format.nChannels`
  se používá PŘÍMO jako násobitel v byte-size matematice, žádné `+1` nikde. Raw 3bitové pole na
  disku už JE skutečný počet kanálů (1=mono, 2=stereo), ne "počet kanálů minus jedna". CNA přičítá
  `+1` na obou místech (`compactFormat`, `fmt`), takže mono se rozparsuje jako stereo (a stereo
  jako 3-kanálové) pro KAŽDOU reálnou `.xwb` položku, ne jen poškozená data.
  *FAudio ref:* FACT_internal.c:1782,1793,1855,1866 (přímé použití bez úpravy).
  *CNA:* XactParser.cpp:455 (compact), :520 (non-compact).
  *Accept:* odstranit `+1` na obou místech; regresní test se syntetickou compact i non-compact
  fixturou s raw `nChannels` polem `1` a `2`, ověřující `entry.channels==1`/`==2` — proti nezávisle
  odvozené očekávané hodnotě, ne proti existujícím fixturám, které samy předpokládají dnešní
  (chybnou) konvenci.
  *Pozn.:* `+1` odstraněno na obou místech (`XactParser.cpp:454`, `:519`). Všech 9 existujících
  fixtur, které kódovaly mono/stereo přes starou "pole = kanály minus jedna" konvenci
  (`XactParserTests.cpp` ×3, `SoundBankTests.cpp`, `CueTests.cpp`, `AudioCategoryTests.cpp`,
  `WaveBankTests.cpp` ×2), opraveno na raw hodnotu přímo. Přidány 4 nové testy s nezávisle
  odvozenou hodnotou (compact stereo, non-compact stereo, plus mono přes existující ADPCM/compact
  testy) — `git stash` na `XactParser.cpp` potvrdil selhání 2 z nich proti staré `+1` logice.
  Celá sada 2045/2045 testů zelená, čisté pod ASan+LeakSanitizer.

- [x] **IN-8 — U COMPLEX zvuku s RPC nebo DSP flagem se per-track metadata čtou PŘED RPC/DSP
  blokem místo PO něm — obrácené pořadí proti FACT.**
  Ověřeno ručně proti `FAudio/src/FACT_internal.c:2580-2704` — skutečné pořadí je: `trackCount`
  (jen to) → RPC blok (pokud `SOUND_FLAG_RPC_MASK`) → DSP blok (pokud `SOUND_FLAG_HAS_DSP`) →
  teprve PAK per-track `vol/code/filterData/frequency` smyčka + track-event pole. CNA čte per-track
  smyčku HNED po `trackCount`, před kontrolou RPC/DSP flagů — pro complex zvuk s RPC/DSP flagem se
  tak čtou RPC/DSP bajty jako by byly track metadata a naopak, `track.code` (absolutní offset pro
  seek na track's event pole) skončí s odpadem. Tohle přežilo Fáze 7's IN-1 opravu (ta řešila jen
  špatnou interpretaci DÉLKY DSP bloku, ne tohle strukturální pořadí).
  *FAudio ref:* FACT_internal.c:2580 (trackCount), :2621-2661 (RPC+DSP bloky), :2668-2696 (per-track
  metadata + track-event pole, AŽ TEĎ).
  *CNA:* XactParser.cpp:688-741 (celá "Sound parsing" smyčka pro `SOUND_FLAG_COMPLEX`).
  *Accept:* přesunout per-track `vol/code/filterData/frequency` smyčku (a track-event parsing) AŽ
  ZA RPC-skip a DSP-skip bloky; regresní test s COMPLEX zvukem (`SOUND_FLAG_COMPLEX|
  SOUND_FLAG_HAS_RPC`, a zvlášť `|SOUND_FLAG_HAS_DSP`) následovaným druhým, odlišitelným zvukem —
  ověřit, že druhý zvuk se rozparsuje správně (analogicky ke stávajícím
  `BuildXsbWithDspThenSecondSound`/`BuildXsbWithRpcThenSecondSound` fixturám, ale s PRVNÍM zvukem
  COMPLEX místo simple).
  *Pozn.:* per-track metadata smyčka (a track-event parsing) přesunuta až za RPC-skip a DSP-skip
  bloky (`XactParser.cpp`'s Sound parsing smyčka). Přidány `BuildXsbWithComplexRpcThenSecondSound`/
  `BuildXsbWithComplexDspThenSecondSound` — track-event data musí ležet MIMO souvislý proud
  sound-hlaviček (referencováno jen absolutním offsetem), takže hlavička sound 1 následuje hned
  za per-track metadaty sound 0, a event pole je až za sound 1 (odhaleno až při psaní testu — první
  pokus s event polem hned za metadaty způsobil "read past end", protože `ParseFirstPlayWave`
  hlavní kurzor po seek+read vrací zpět, takže formát musí mít hlavičky souvislé, ne event data).
  `git stash` na `XactParser.cpp` potvrdil selhání obou nových testů proti staré logice.

- [x] **IN-9 — Streaming `WaveBank::GetSoundEffect` může zkusit neomezenou alokaci z
  poškozením/útokem kontrolovatelné `dataLength`, bez try/catch.**
  `audioLen = entry.dataLength` (odvozeno z parsovaného `.xwb` headeru) se u STREAMING cesty nikde
  neporovná proti skutečné velikosti souboru (na rozdíl od non-streaming cesty, která má přesně
  tuhle kontrolu). `streamedBytes.resize(audioLen)` navíc běží PŘED `try` blokem, takže
  `std::length_error`/`std::bad_alloc` propadne nezachyceno až z `Cue::Play()`.
  *CNA:* WaveBank.cpp:267-269 (resize+read před try na řádku 291); volající Cue.cpp:244 (žádný
  try/catch v řetězci až po `Cue::Play`).
  *Accept:* před `resize` zkontrolovat `entry.dataLength` proti rozumné mezi (skutečná zbývající
  velikost souboru přes `seekg(0,end)`/`tellg()`), a/nebo přesunout resize+read do existujícího
  try/catch a vrátit `nullptr` při selhání; regresní test se streaming fixturou, jejíž entry
  `dataLength` přesahuje reálnou velikost souboru, ověřující `GetSoundEffect` vrátí `nullptr`
  místo throw/crash.
  *Pozn.:* `WaveBank::GetSoundEffect`'s streaming větev teď před `resize` ověří
  `dataOffset+dataLength` proti skutečné velikosti souboru na disku (`seekg(0,end)`/`tellg()`),
  a `resize` samotný je navíc obalen try/catch. Přidán `WaveBankTest.
  StreamingGetSoundEffectRejectsEntryLengthExceedingRealFileSize` — pro zvolenou (mírně)
  nadsazenou délku (1 MB) vrací `nullptr` i stará i nová cesta (stará přes post-read
  `gcount()` kontrolu), takže `git stash` tenhle konkrétní test nerozliší; skutečný přínos opravy
  (zabránění pokusu o alokaci u řádově větších — GB — hodnot) není bezpečně testovatelný v
  rychlém unit testu — zdokumentováno přímo u testu. Sada zelená, čisté pod ASan+LeakSanitizer.

- [x] **IN-10 — Compact-format `.xwb` položky nikdy neodvodí ADPCM `samplesPerBlock`/`blockAlign`
  — jen non-compact cesta to dělá.**
  Non-compact větev správně počítá `samplesPerBlock=(wBlockAlign+16)*2`/`blockAlign=
  (wBlockAlign+22)*channels` pro `fmtTag==2` (ADPCM). Compact větev nemá žádnou format-tag
  podmínku vůbec — `blockAlign` zůstane syrové `wBlockAlign`, `samplesPerBlock` zůstane na
  výchozí `0`, i když compact banka celá kóduje ADPCM (validní, i když méně častá kombinace).
  *FAudio ref:* FACT_internal.c:1790-1793,1863-1866 (aplikuje vzorec bez ohledu na zdroj formátu).
  *CNA:* XactParser.cpp:449-481 (compact — chybí větev), :535-546 (non-compact — správně).
  *Accept:* aplikovat stejnou `fmtTag==2` větev (nebo sdílenou helper funkci) i v compact smyčce;
  regresní test s compact fixturou kódující ADPCM, ověřující stejný vzorec jako existující
  `NonCompactAdpcmEntryComputesBlockAlignAndSamplesPerBlock`.
  *Pozn.:* compact smyčka teď před hlavní `for` počítá `compactSamplesPerBlock`/
  `compactBlockAlign` stejným vzorcem jako non-compact větev (sdílené pro celou banku, protože
  formát je u compact bank společný pro všechny položky). Přidán `XactParserTest.
  CompactAdpcmEntryComputesBlockAlignAndSamplesPerBlock`; `git stash` na `XactParser.cpp`
  potvrdil selhání proti staré (chybějící) logice.

- [x] **IN-11 — `AudioMixer::GetMixer()` leakuje `MIX_Init()` refcount, když `MIX_CreateMixerDevice`
  selže.**
  `MIX_Init()`/`MIX_Quit()` jsou reference-counted. `GetMixer()` zavolá `MIX_Init()`, a pokud
  `MIX_CreateMixerDevice()` selže, throwne `std::runtime_error` BEZ vyrovnávacího `MIX_Quit()`.
  `g_mixer` zůstane `nullptr`, takže každé další volání (~10 míst v `SoundEffect.cpp`/
  `SoundEffectInstance.cpp`/`DynamicSoundEffectInstance.cpp`/`MediaPlayer.cpp`) zavolá `GetMixer()`
  znovu a dál nabaluje nevyrovnaný počet, dokud audio hardware nechybí.
  *CNA:* src/CNA/Internal/Audio/AudioMixer.cpp:17-36.
  *Accept:* při selhání `MIX_CreateMixerDevice` zavolat `MIX_Quit()` před throw (nebo obalit celou
  inicializační sekvenci tak, aby každá chybová cesta vyrovnala `MIX_Init`); test není prakticky
  proveditelný bez reálného/mockovaného SDL audio subsystému — oprava je ale přímočará defenzivní
  úprava.
  *Pozn.:* `MIX_Quit()` přidáno do chybové větve před `throw`. Bez automatizovaného testu
  (dle accept kritéria samotného — vyžadovalo by reálný/mockovaný SDL audio subsystém); pokryto
  jen manuální revizí + `AudioMixerTests.cpp`'s "no tests" komentářem (IN-12).

- [x] **IN-12 — Test-mezery v `XactParserTests.cpp` (22 testů) a úplná absence testů zaměřených na
  `AudioMixer`/`ParseXwbStreamingHeader`.**
  Žádný test nekombinuje `SOUND_FLAG_COMPLEX` s RPC/DSP flagy (proto IN-8 unikl); `RPC_internal`
  Existující RPC/DSP fixtury (`BuildXsbWithRpcThenSecondSound`/`BuildXsbWithDspThenSecondSound`)
  používají výslovně SIMPLE zvuky. `ParseXwbStreamingHeader` nemá žádný přímý unit test (jen
  nepřímo přes `WaveBankTests.cpp`'s malé validní fixtury) — chybí truncated/malformed header,
  zero-entry streaming banka, streaming entry s `dataLength` přesahující reálnou velikost souboru
  (viz IN-9). Žádný test neuzamkne správnou (ne off-by-one) hodnotu kanálů proti nezávisle
  ověřené hodnotě (viz IN-7 — stávající fixtury by prošly beze změny i po špatné i po správné
  opravě). Žádný test pro compact-format ADPCM položku (IN-10). `AudioMixer` nemá testovací
  soubor vůbec (rozumné vzhledem k závislosti na reálném/mockovaném SDL audio zařízení, ale bez
  komentáře podle CHECKLIST.md's konvence pro netestovatelné třídy).
  *CNA:* tests/CNA/Internal/Audio/XactParserTests.cpp (celý soubor); žádný `AudioMixerTests.cpp`.
  *Accept:* přidat fixtury popsané v accept kritériích IN-7/IN-8/IN-9/IN-10; pro `AudioMixer`
  přidat aspoň jednořádkový komentář (podle CHECKLIST.md's konvence) vysvětlující, proč netestován.
  *Pozn.:* fixtury pro IN-7 (compact+non-compact stereo), IN-8 (COMPLEX+RPC, COMPLEX+DSP), IN-9
  (streaming oversized-length), IN-10 (compact ADPCM) všechny přidány u svých vlastních položek
  výše. `tests/CNA/Internal/Audio/AudioMixerTests.cpp` přidán jako prázdný "no tests" stub
  (stejná konvence jako `GameComponentTests.cpp` apod.). `XactParserTests.cpp` teď 27 testů
  (bylo 22).

#### 8.4 Mic/data/enumy/výjimky

- [x] **MC-6 — `Microphone::CheckBuffer()` je veřejná, i když nemusí být — zbytečně rozšiřuje API
  povrch a odporuje vlastnímu T-1H accept kritériu.**
  FNA má `CheckBuffer()` jako `internal` — nejde zavolat mimo assembly. CNA ji má `public`
  (označenou `NOXNA`), přímo proti CLAUDE.md's Visibility Mapping ("C# `internal` ... by se
  neměl stát public C++ API metodou") a proti T-1H's vlastnímu accept kritériu ("žádné public
  interní členy"). `CheckAllBuffers()` (nový, sanctioned NOXNA most pro `FrameworkDispatcher`) je
  `static` metoda stejné třídy, takže má už private-member přístup k `CheckBuffer()` bez nutnosti
  být `CheckBuffer()` sama veřejná.
  *FNA:* Microphone.cs:204-213 (`internal void CheckBuffer()`).
  *CNA:* Microphone.hpp:133-134; Microphone.cpp:210-230.
  *Accept:* `CheckBuffer()` přesunout do `private` (ponechat `CheckAllBuffers()` veřejnou dle T-1H);
  `MicrophoneTestAccess` rozšířit o tenký static wrapper, aby `MicrophoneTests.cpp`'s přímá volání
  `mic.CheckBuffer()` dál fungovala.
  *Pozn.:* hotovo přesně dle accept kritéria. `MicrophoneTestAccess::CheckBuffer(mic)` přidán,
  oba přímé testovací call sites přepsány. Čistě viditelnostní změna — žádný chování se nemění,
  proto bez `git stash` kroku (samotný úspěšný build je důkaz správné enkapsulace).

- [x] **MC-7 — Test-mezera: žádný deterministický test, že `BufferReady` mlčí, když queued duration
  je pod `BufferDuration`.**
  Existující testy pokrývají jen "no subscriber → nethrowuje" (zkratkuje se na `Empty()` kontrole,
  nikdy nezacvičí samotné `>` porovnání) a "reálný capture, dost času → časem vystřelí" (jen
  pozitivní cesta, potřebuje SDL dummy driver a až 2s pollingu). Chybí deterministický,
  instance-izolovaný test pro negativní případ.
  *CNA:* MicrophoneTests.cpp:267-276,357-374; Microphone.cpp:210-216.
  *Accept:* nový test s izolovaným (nikdy `Start()`-nutým) `Microphone` přes `MicrophoneTestAccess`,
  zaregistrovat počítající lambda na `BufferReady`, zavolat `CheckBuffer()` přímo, ověřit že
  counter zůstane `0`; bonus: ověřit že `sender` argument předaný handleru je sama mic instance.
  *Pozn.:* přidán `CheckBufferDoesNotRaiseWhenQueuedDurationIsBelowBufferDuration` — izolovaná
  (nikdy `Start()`-nutá) instance má `GetQueuedBytes()==0` (`captureStream_` je null), takže `>`
  porovnání skutečně proběhne a musí vyjít false. Bonus (sender identity) nešlo ve stejném testu
  ověřit smysluplně, protože event nikdy nevystřelí (žádný pozitivní call k porovnání) — test
  místo toho ověřuje, že `sender` zůstává `nullptr` (lambda se vůbec nezavolala). Čistě testovací
  doplněk, žádná změna produkčního kódu.

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
| D1 | ~~`CreateInstance`/`FromStream`: hodnota vs. heap-reference + instance-tracking (T-3G)~~ | **Rozhodnuto a implementováno 2026-07-04** — instance-tracking + Dispose-kaskáda; `SoundEffect` je nově move-only (jediný vlastník resource), `SoundEffectInstance` se registruje/odregistruje/re-pointuje při přesunu. `CreateInstance`/`FromStream` zůstávají hodnotové (žádná heap-ref sémantika), ale `SoundEffect` už nelze kopírovat. |
| D2 | Pan/Volume klamp vs. throw/pass-through (T-3C) | Sladit s FNA (throw na range, pass-through volume); klamp jen vědomě + do CHECKLIST |
| D3 | ~~Streaming WaveBank (T-3F)~~ | **Rozhodnuto a implementováno 2026-07-04** — skutečný streaming: `ParseXwbStreamingHeader` čte jen hlavičku/metadata z disku, `WaveBank::GetSoundEffect` čte data položky líné přímo ze souboru. Non-streaming ctor beze změny (celý soubor eager, jako FNA). |
| D4 | ~~Rozsah `AudioEngine::Update` / FACT DoWork (T-4D)~~ | **Rozhodnuto a implementováno 2026-07-04** — minimální rozsah: `SetCategoryVolumeInternal` teď volá `Cue::ApplyCategoryVolume` pro re-apply na aktivní instance; fade kategorií a instance-limity (zbytek FACT `DoWork`) zůstávají dokumentovaně mimo rozsah. |
| D5 | ~~Vlastnictví `SoundEffect` vs. `SoundEffectInstance` — dangling-safe kontrakt vs. sdílené vlastnictví (CP-7)~~ | **Rozhodnuto a implementováno 2026-07-03** — sdílené vlastnictví: `SoundEffectInstance` drží type-erased `shared_ptr<void>` na `SoundEffect::impl_` plus cache'ovaný native handle, žádná dereference syrového `SoundEffect*` za konstrukcí. Ověřeno reálným ASan buildem. |
| D6 | ~~Fire-and-forget cue cleanup: čas vs. stav přehrávání (XA-1)~~ | **Rozhodnuto a implementováno 2026-07-02** — mazat podle `!IsPlaying`, časový safety-net (5 min) jen jako krajní pojistka. |
| D7 | ~~Chování parseru na poškozená/adverzní XACT data — throw vs. saturující clamp (IN-2, IN-3)~~ | **Rozhodnuto a implementováno 2026-07-03** — throw (`std::runtime_error`) na podteklé/poškozené hodnoty místo tichého clampování; sladí se s projektovým pravidlem „no silent data corruption". |
| D8 | ~~`Microphone::GetData` chování bufferu při chybě/no-op — nulovat vs. nechat nedotčené (MC-3)~~ | **Rozhodnuto a implementováno 2026-07-03** — sladěno s FNA: `GetData` vrací 0 a buffer nechává zcela nedotčený, žádné nulování. |

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
