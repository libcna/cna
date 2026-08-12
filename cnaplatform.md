**Je to technicky možné**, ale SDL2 bude výrazně jednodušší než skutečné SDL 1.2.

Nejdůležitější je, aby ses nepokoušel vytvořit „společný SDL wrapper“ napodobující průnik všech tří API. Lepší architektura je:

```text
CNA Core
   │
   ├── CNA Platform API
   │      ├── SDL3 Platform
   │      ├── SDL2 Platform
   │      └── SDL 1.2 Platform
   │
   ├── CNA Graphics API
   │      ├── Vulkan
   │      ├── OpenGL
   │      ├── Software
   │      ├── GDI
   │      └── ...
   │
   ├── CNA Audio API
   └── CNA Input API
```

Platform API má popisovat **to, co potřebuje CNA**, nikoliv funkce SDL.

## SDL2: velmi realistické

SDL2 a SDL3 nejsou zdrojově totožné; SDL má samostatný rozsáhlý migrační návod a měnila se návratová pravidla, názvy funkcí, eventy i další části API. To ale nebrání tomu, aby obě implementovaly stejnou interní CNA platformní abstrakci. ([SDL Wiki][1])

SDL2 je vhodné zejména pro:

* starší Windows;
* starší Linuxové distribuce;
* platformy, kde SDL3 není praktické;
* hry, které nepotřebují SDL3 GPU API;
* dlouhodobou záložní platformní cestu.

Oficiální dokumentace SDL2 uvádí podporu Windows zpět až k Windows XP. ([SDL Wiki][2])

Odhadem by šla SDL2 implementace vytvořit bez zásadního zásahu do herního API, pokud nejprve odstraníš přímé volání SDL3 z core, inputu, audia a jednotlivých backendů.

## SDL 1.2: možné, ale jako omezený legacy profil

Klasické SDL 1.2 je oficiálně zastaralé a SDL tým upozorňuje, že už se aktivně nevyvíjí a bude postupně degradovat. Poslední oficiální vydání je 1.2.15. ([libsdl.org][3])

To ale neznamená, že jej CNA nemůže použít. Znamená to, že implementaci budeš dlouhodobě vlastnit ty.

SDL 1.2 platforma by pravděpodobně nabízela jen omezený profil:

* základní okno;
* klávesnice a myš;
* starší joystickové API;
* časování;
* základní audio;
* OpenGL nebo software surface;
* základní fullscreen režimy;
* omezený clipboard, text input, gamepad a HiDPI kontrakt.

Moderní funkce se musí deklarovat jako nepodporované, ne emulovat nebezpečnými hacky.

## Pozor na compatibility projekty

Existují oficiální projekty:

* `sdl2-compat`: poskytuje SDL2 API nad SDL3;
* `sdl12-compat`: poskytuje SDL 1.2 API nad SDL2;
* lze je dokonce řetězit až k SDL3. ([GitHub][4])

Ty jsou užitečné pro kompatibilitu aplikací, ale **neřeší tvůj cíl podporovat skutečně staré platformy**. Pokud `sdl12-compat` nakonec běží nad SDL2 nebo SDL3, nezískáš tím operační systém, na kterém spodní SDL neběží.

Pro CNA proto dávají smysl tři odlišné možnosti:

```text
CNA + skutečné SDL3
CNA + skutečné SDL2 Classic
CNA + skutečné SDL 1.2 Classic
```

Compatibility vrstvy mohou být navíc testovací konfigurace, nikoliv náhrada skutečných implementací.

## Co musí obsahovat CNA Platform API

Doporučil bych minimálně tyto části:

```cpp
namespace CNA::Platform {

class Application;
class Window;
class EventLoop;
class Keyboard;
class Mouse;
class Gamepad;
class Clipboard;
class Cursor;
class Timer;
class DisplayManager;
class DynamicLibrary;
class FileSystem;
class MessageBox;
class NativeWindowHandle;

}
```

Audio bych zvažoval oddělit:

```text
CNA.Audio.Platform
├── SDL3 Audio
├── SDL2 Audio
├── SDL 1.2 Audio
├── OpenAL
└── další budoucí implementace
```

Stejně tak gamepad může být vlastní modul, protože SDL1 joystick, SDL2 GameController a SDL3 Gamepad mají výrazně odlišné možnosti.

## Grafické backendy nesmějí automaticky záviset na SDL3

Dnes může backend sahat přímo na:

```cpp
SDL_Window*
SDL_PropertiesID
SDL_GetWindowProperties(...)
```

Po oddělení by měl dostat něco jako:

```cpp
struct NativeWindowHandle {
    NativeWindowSystem system;
    void* display;
    void* window;
    void* surface;
};
```

Nebo typově bezpečnější varianty pro:

* Win32 `HWND`;
* X11 `Display*` + `Window`;
* Wayland display/surface;
* Cocoa window/view;
* Android native window;
* web canvas.

Potom Vulkan, OpenGL, DirectX, GDI nebo Glide nebudou vědět, zda okno vytvořilo SDL3, SDL2, SDL1 nebo budoucí nativní platformní backend.

Výjimkou budou záměrně SDL-specifické grafické backendy:

```text
SDL GPU       → vyžaduje SDL3
SDL Renderer  → varianta podle SDL3/SDL2
SDL 1.2 Surface Renderer → pouze legacy cesta
```

SDL GPU je moderní API dostupné v SDL3, takže pro SDL2 nebo SDL1 jej nelze jednoduše zachovat jako stejnou cestu. ([SDL Wiki][5])

## Capability model platformy

Stejně jako u grafiky potřebuješ capability dotazy:

```cpp
struct PlatformCapabilities {
    bool supportsMultipleWindows;
    bool supportsHighDpi;
    bool supportsClipboard;
    bool supportsTextInput;
    bool supportsIme;
    bool supportsGamepadRumble;
    bool supportsGamepadSensors;
    bool supportsNativeFileDialog;
    bool supportsVulkanSurface;
    bool supportsOpenGLContext;
};
```

Hra nebo CNA vrstva pak nebude předpokládat, že každá platformní implementace umí vše.

## C++23 zůstává samostatný problém

Oddělení SDL3 je nutná podmínka pro staré systémy, ale **samo nestačí**.

Pro opravdu staré Windows budeš pravděpodobně potřebovat také:

* starší kompatibilní compiler/toolchain;
* nižší jazykový profil než plné C++23;
* omezení moderní standardní knihovny;
* odstranění závislostí na nových systémových API;
* případně C ABI mezi moderním CNA core a legacy hostem.

Praktický model může být:

```text
CNA Modern
    C++23
    SDL3 / SDL2
    všechny moderní funkce

CNA Legacy
    omezená kompatibilní podmnožina
    SDL2 nebo SDL 1.2
    vybrané grafické/audio backendy
    bez některých NoXNA rozšíření
```

Nemusíš nutně překládat všech 40 grafických backendů pro každý legacy profil.

## Nejbezpečnější implementační pořadí

1. **Inventář všech přímých použití SDL3.**
2. Definovat platformní API podle skutečných potřeb CNA.
3. Přesunout současné SDL3 chování za `Sdl3Platform`.
4. Dokázat behaviorální shodu bez přidávání SDL2.
5. Přidat `Sdl2Platform`.
6. Vytvořit platformní conformance suite.
7. Teprve potom navrhnout omezený `Sdl12Platform`.
8. Nakonec řešit legacy C++/toolchain profil.

Důležitá zásada:

> Nezačínat abstrakcí podle toho, co mají společného SDL1, SDL2 a SDL3. Začít kontraktem, který potřebuje CNA, a každé SDL nechat tento kontrakt implementovat podle svých možností.

Takže ano: **SDL3 + SDL2 je velmi realistický cíl. Skutečné SDL 1.2 je také možné, ale spíš jako historická a omezená platformní implementace, nikoliv plnohodnotná náhrada SDL3 pro všechny moderní funkce CNA.**

[1]: https://wiki.libsdl.org/SDL3/README-migration?utm_source=chatgpt.com "SDL3/README-migration"
[2]: https://wiki.libsdl.org/SDL2/Installation?utm_source=chatgpt.com "SDL2/Installation"
[3]: https://www.libsdl.org/download-1.2.php?utm_source=chatgpt.com "SDL version 1.2.15 (historic)"
[4]: https://github.com/libsdl-org/sdl2-compat?utm_source=chatgpt.com "An SDL2 compatibility layer that uses SDL3 behind ..."
[5]: https://wiki.libsdl.org/SDL3/CategoryGPU?utm_source=chatgpt.com "SDL3/CategoryGPU"



























Při správném návrhu půjde výkon dolů **prakticky neměřitelně**. U běžné CNA hry bych očekával přibližně:

| Návrh platformní abstrakce                                 | Odhad dopadu na výkon celé hry |
| ---------------------------------------------------------- | -----------------------------: |
| Dobře navržená, hrubozrnná abstrakce                       |                    **0–0,3 %** |
| Více virtuálních volání, ale jen pro okna a eventy         |                      **0–1 %** |
| Špatně navržená abstrakce v často volaných cestách         |                      **1–5 %** |
| Abstrakce volaná pro každý pixel, audio sample nebo vertex |     potenciálně výrazný propad |

Pro CNA bude pravděpodobně důležitější dopad na **složitost kódu, buildy a testování** než na runtime výkon.

## Proč bude dopad malý

Platformní vrstva typicky obsluhuje:

* vytvoření a zrušení okna;
* zpracování eventů;
* klávesnici, myš a gamepady;
* clipboard;
* kurzor;
* časování;
* fullscreen a změny rozlišení;
* získání nativního window handle;
* případně audio zařízení.

Většina těchto operací proběhne jednou za frame nebo ještě méně často. I kdyby každá vedla přes virtuální funkci nebo function pointer, proti renderování, fyzice, audio mixingu a herní logice je cena zanedbatelná.

Například místo přímého:

```cpp
SDL_PollEvent(&event);
```

může CNA dělat:

```cpp
platform->PollEvents(eventQueue);
```

Jedno nepřímé volání za frame nebude mít prakticky žádný dopad. Samotné systémové zpracování eventů je mnohem dražší než výběr implementace přes vtable.

## Kde by se výkon opravdu mohl zhoršit

### Volání platformní vrstvy pro každý jednotlivý event

Horší návrh:

```cpp
while (platform->PollSingleEvent(event))
{
    ProcessEvent(event);
}
```

Lepší návrh:

```cpp
platform->PollEvents(eventBatch);

for (const auto& event : eventBatch)
{
    ProcessEvent(event);
}
```

Rozdíl bude i tak obvykle malý, ale batch rozhraní snižuje počet nepřímých volání a lépe odděluje SDL datové struktury od CNA.

### Audio po jednotlivých samplech

Tohle by bylo špatně:

```cpp
for (std::size_t i = 0; i < sampleCount; ++i)
{
    output[i] = audioPlatform->MixOneSample();
}
```

Správně:

```cpp
audioPlatform->FillBuffer(output, sampleCount);
```

Platformní dispatch jednou na celý audio buffer je zanedbatelný. Dispatch jednou na sample už může být výrazný.

### Přesměrování každého grafického draw callu přes platformní API

Grafický backend nesmí dělat něco jako:

```cpp
platform->GraphicsDraw(...);
```

Platformní API má grafice pouze předat:

* velikost okna;
* native handle;
* surface informace;
* DPI;
* lifecycle eventy.

Samotné draw cally patří přímo do zvoleného grafického backendu:

```text
Game
  ↓
GraphicsDevice
  ↓
Vulkan / Bgfx / OpenGL / GDI / Glide
```

Ne:

```text
Game
  ↓
GraphicsDevice
  ↓
Platform API
  ↓
SDL implementation
  ↓
Graphics backend
```

Tím se vyhneš další vrstvě v nejčastěji volané cestě.

## Doporučená struktura

```cpp
class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual std::unique_ptr<IWindow>
    CreateWindow(const WindowDescription& description) = 0;

    virtual void PollEvents(std::vector<PlatformEvent>& destination) = 0;

    virtual KeyboardState GetKeyboardState() const = 0;
    virtual MouseState GetMouseState() const = 0;

    virtual std::uint64_t GetPerformanceCounter() const = 0;
    virtual std::uint64_t GetPerformanceFrequency() const = 0;

    virtual PlatformCapabilities GetCapabilities() const = 0;
};
```

Implementace:

```cpp
class Sdl3Platform final : public IPlatform {};
class Sdl2Platform final : public IPlatform {};
class Sdl12Platform final : public IPlatform {};
```

Platforma se vybere jednou při spuštění:

```cpp
std::unique_ptr<IPlatform> platform =
    PlatformFactory::Create(configuration.platformBackend);
```

Pak už se pointer nemění. CPU branch predictor si nepřímý cíl obvykle dobře zapamatuje.

## Ještě rychlejší varianta

Pokud bude platforma zvolená při kompilaci, lze se virtuálním voláním úplně vyhnout:

```cpp
using ActivePlatform = Sdl3Platform;
```

nebo pomocí CMake:

```text
CNA_PLATFORM=SDL3
CNA_PLATFORM=SDL2
CNA_PLATFORM=SDL12
```

Výsledná binárka bude obsahovat jen jednu implementaci. Kompilátor může část volání inlineovat a runtime overhead bude prakticky nulový.

Dynamický výběr ale může být užitečný například pro jednu binárku podporující více platformních vrstev. Ani tehdy nebude režie významná, pokud se dispatch nedostane do vnitřních smyček.

## Native window handle bez dlouhého řetězce volání

Grafickému backendu bych nepředával celé `IPlatform`. Předal bych mu při inicializaci hotový popis nativního okna:

```cpp
struct NativeWindowHandle {
    NativeWindowSystem system;

    void* display;
    void* window;
    void* surface;
};
```

Backend pak handle uloží:

```cpp
vulkanBackend.Initialize(platformWindow.GetNativeHandle());
```

Nebude při každém frame znovu volat několik vrstev platformního API.

## Input snapshots

Místo tisíců platformních dotazů:

```cpp
platform->IsKeyDown(Key::A);
platform->IsKeyDown(Key::B);
platform->IsKeyDown(Key::C);
```

je lepší jednou za frame vytvořit snapshot:

```cpp
platform->UpdateInput();

const KeyboardState keyboard = platform->GetKeyboardState();
```

A hra pak čte lokální bitovou sadu. To může být dokonce rychlejší než současná přímá SDL cesta, pokud nynější implementace provádí opakované konverze.

## Capabilities necpat do hot path

Nevolat opakovaně:

```cpp
if (platform->GetCapabilities().supportsHighDpi)
```

Capability strukturu načíst jednou:

```cpp
const PlatformCapabilities capabilities = platform->GetCapabilities();
```

A pak ji držet v `GraphicsDeviceManager`, `GameWindow` nebo odpovídajícím subsystému.

## Největší náklady budou jinde

Přidání SDL3/SDL2/SDL1 implementací pravděpodobně zvýší:

* počet build konfigurací;
* velikost zdrojového stromu;
* počet CI kombinací;
* počet platformních fixtures;
* složitost native handle interop;
* množství conditional capabilities;
* údržbu input a audio rozdílů.

Runtime výkon se téměř nezmění. Mnohem větším rizikem je, že jedna implementace bude mít jiné event semantics, DPI, fullscreen, gamepad nebo timing chování než ostatní.

## Audio oddělit zvlášť

Platformní abstrakce a audio backend by neměly být nutně jedna věc:

```text
Platform:
  SDL3
  SDL2
  SDL1
  Native Win32
  Headless

Audio:
  SDL3 Audio
  SDL2 Audio
  SDL1 Audio
  OpenAL
  WASAPI
  ALSA
  Null Audio
```

Audio callback pak dostane celý buffer. Nebude pro každý sample procházet obecným platformním rozhraním.

## Doporučený výkonový kontrakt

Při modularizaci bych stanovil jednoduché pravidlo:

> Platformní abstrakce nesmí být volána uvnitř per-pixel, per-vertex, per-fragment, per-audio-sample ani jiné elementární smyčky.

Povolené jsou hlavně:

* jednou při inicializaci;
* jednou nebo několikrát za frame;
* jednou na celý event batch;
* jednou na celý audio buffer;
* při skutečné změně okna nebo zařízení.

Při dodržení tohoto pravidla čekám u CNA pokles výkonu **typicky pod 0,5 % a pravděpodobně pod hranicí stabilně měřitelného rozdílu**. Některé cesty se mohou po centralizaci eventů a input snapshots dokonce mírně zrychlit.



