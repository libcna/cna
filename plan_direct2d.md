# Direct2D backend — plán 2D parity s EasyGL

> **Rozhodnutý rozsah:** `DIRECT2D` je trvale **2D-only** backend. Nepřidává se do něj 3D
> pipeline jen proto, že Direct2D interně používá D3D11 device pro GPU akceleraci a prezentaci.
> Pro 3D slouží existující `D3D11` backend.
>
> **Stav:** `⬜` znamená neimplementováno, `🟨` znamená rozpracovaný kód bez dokončeného
> Windows/DXVK pixelového ověření a `🔬` technický spike před implementací. Každá hotová položka
> musí mít pixelový test na Windows/DXVK.

## Co EasyGL umí navíc a co je zde v rozsahu

Současný Direct2D backend již implementuje `Texture2D`, `RenderTarget2D`, `SpriteBatch`, běžné
presentation módy, standardní blend presety, scissor rectangle a staging readback **swap chain
backbufferu**. EasyGL je širší GLES3 backend; tento plán vybírá pouze rozdíly, které mají smysl
pro poctivý 2D Direct2D backend.

| Funkce EasyGL | Direct2D nyní | Rozhodnutí pro Direct2D |
|---|---|---|
| `RenderTarget2D::GetData` a readback aktivního cíle | RT `GetData` vrací `false`; readback vždy čte swap chain | **V rozsahu:** skutečný 2D readback |
| Tint, flip, Wrap/Mirror pro render target jako SpriteBatch zdroj | u RT hází výjimku, protože nemá CPU shadow | **V rozsahu:** Direct2D effect/brush cesta |
| Mipové levely `Texture2D` a `RenderTarget2D` | Texture2D má explicitně nahrané levely; RT generuje GPU chain po odbindu | **V rozsahu:** 2D bitmapy po levelech, generování downsamplem |
| Point/linear + adresování 2D sprite zdrojů | základ funguje; RT omezený | **V rozsahu:** přesné 2D chování; anisotropie není cíl |
| Scissor enable a viewport | `SetScissorRect` vždy clipuje; `SetViewport` je no-op | **V rozsahu:** 2D clip/transform stav |
| Context loss/recovery | požaduje rekonstrukci celé hry | **V rozsahu:** obnova Direct2D zdrojů |
| MSAA | 0 | **Vyřešeno:** trvalé `0`/`false`; Direct2D primitive antialiasing není `MultiSampleCount` a D3D multisample/resolve pipeline je mimo rozsah |
| Obecné blend faktory, blend equation, write/sample mask | jen čtyři standardní presety | **Mimo rozsah:** Direct2D API je neumí přesně; nepřibližovat je D3D11 passem |
| 3D buffery/draw, depth/stencil, MRT, cube/volume textury, queries, custom shader effects | nepodporováno | **Mimo rozsah:** patří do `D3D11` backendu |

## Pevné hranice

Následující EasyGL schopnosti nejsou úkoly pro Direct2D a zůstávají explicitně nepodporované:

- vertex/index buffery (16- i 32-bit), `Draw*`, `Draw*Ex` a instancing;
- `BasicEffect` a ostatní stock 3D efekty, custom `Effect`/GLSL shader pipeline;
- depth/stencil resources a state, 3D clear operace, culling, wireframe a depth bias;
- `Texture3D`, `TextureCube`, `RenderTargetCube`, environment mapping a skinning;
- MRT, occlusion queries a obecné D3D11 sampler state včetně anisotropie;
- obecné `BlendState`, constant blend factor, color-write a multisample mask.

`SupportsCapability()` musí pro tyto funkce zůstat `false`, 3D vstupy musí dál selhávat
pojmenovanou výjimkou a dokumentace má uživatele odkázat na `CNA_GRAPHICS_BACKEND=D3D11`.

## Fáze D2D-1 — základní 2D kompatibilita

| # | Úkol | Stav | Akceptace |
|---|---|---|---|
| D2D-1 | Zaveď samostatnou matici 2D parity testů: Texture2D, RenderTarget2D, SpriteBatch, clipping, presentation, resize a readback. Používej pouze veřejné 2D API. | 🟨 | Přidán `Direct2D_2DParity`: Clear/readback, point Texture2D, flip, opaque tint, RT→SpriteBatch, clipping, viewport, veřejný `GraphicsDeviceManager::ApplyChanges` resize (40×24 → nový swap chain, viewport i pixel) a debug device recovery, vše přes veřejné 2D API. Wine i Proton Experimental/DXVK prošly všechny dostupné render/sampling/readback pixely včetně RT a mipů; Proton má nyní reprodukovatelný CTest runner. `cna_demo_2d --smoke 3` i plné interaktivní demo se sestavily a spustily přes Direct2D/Proton z čistého prefixu; MinGW balení nyní staticky linkuje C++ runtime a přibalí `libwinpthread-1.dll`. Zbývá pixelová presentation-matrix na fyzickém okně s jiným poměrem stran a `ColorMatrix` effect subset na skutečném Windows Direct2D. |
| D2D-2 | Implementuj `Direct2DRenderTargetBackend::GetData`: validace regionu a levelu, bezpečný převod do CPU-readable 2D bitmapy/stagingu, BGRA→RGBA a top-left pořadí řádků. | ✅ | Primární Direct2D cesta je `CopyFromRenderTarget` → `CPU_READ` bitmapa → `Map(READ)`. Pokud runtime vrátí `E_NOTIMPL`, backend ze stejného aktuálního target bitmapu použije čistě 2D `CopyFromBitmap` fallback. `Direct2D_2DParity` ve Wine pixelově ověřil částečný i celý level-0 readback (red marker + blue background), BGRA→RGBA i top-left řádky. |
| D2D-3 | Oprav `ReadBackbuffer` při navázaném RT: čti aktivní 2D cíl, nebo veřejně vynucuj jeho odbind podle společného kontraktu. Nikdy tiše nečti swap chain, když hra kreslí jinam. | ✅ | `ReadBackbuffer` při aktivním `RenderTarget2D` čte jeho bitmapu a kontroluje jeho vlastní rozměry. Veřejný Wine pixelový probe nyní aktivní RT skutečně přečetl a ověřil red marker; `CopyFromBitmap` fallback pokrývá WineD3D `CopyFromRenderTarget=E_NOTIMPL`. |
| D2D-4 | Doplň SpriteBatch pro `RenderTarget2D` jako zdroj: tint, flip, crop, non-premultiplied vstup a Wrap/Mirror bez CPU shadow/render-target readbacku. Použij Direct2D bitmap brush a effect graph. | 🟨 | Implementován GPU-only `ID2D1ImageBrush` pro RT, s `ColorMatrix` tintem, `Premultiply` pro NonPremultiplied, flip transformem a Wrap/Mirror extend modes; drží COM graph do `EndDraw` a nevytváří CPU kopii. Přidány pixelové probes tint/flip/Wrap/Mirror. Wine ani Proton Experimental (2026-08-01) nemají `ColorMatrix`/`Premultiply` zaregistrován (`CreateEffect(Premultiply) = 0x88990028`), proto jejich byte-exact běh zbývá na nativním Windows; Clamp mimo source rectangle zůstává explicitně pro D2D-13. |
| D2D-5 | Uprav `ApplyRasterizerState` a `SetScissorRect`: ukládej `ScissorTestEnable`, clip zapínej jen pokud je povolený a počítej jej vůči aktivnímu RT/backbufferu. | ✅ | Samostatná brána `ScissorTestEnable`; nulový rectangle je prázdný clip, nikoli tiché vypnutí. Izolovaný Wine pixelový test prošel pro enabled/disabled backbuffer i menší RT po odbindu. |
| D2D-6 | Implementuj `SetViewport` pro 2D výstup jako zdokumentovaný transform+clip stav. Urči interakci s virtual resolution, presentation transformem, SpriteBatch transformem a RT. | ✅ | `SpriteBatch transform → viewport-local offset+clip → presentation transform`; scissor se s viewport clipem protíná v targetových souřadnicích a RT reset přichází z `GraphicsDevice`. Izolovaný Wine pixelový test prošel na backbufferu i RT. |
| D2D-7 | Zaveď obnovu po `D2DERR_RECREATE_TARGET`/device removal: znovu vytvoř D3D/D2D/DXGI prezentační zdroje a registrované 2D bitmapy. Obyčejné textury obnov z CPU shadow; chování obsahu RT explicitně definuj podle content usage. | ✅ | `EndDraw` a `Present` rozpoznají Direct2D/DXGI reset, znovu vytvoří kompletní D3D11/DXGI/Direct2D doménu a vyžádají překreslení snímku. Debug recovery rehydratuje registrované `Texture2D` z RGBA shadow, RT znovu alokuje jako transparentní a obnoví jen stále registrovaný aktivní RT. Izolovaný Wine pixelový test potvrzuje kreslení po resetu a že obsah RT po resetu není dál platný. |
| D2D-8 | Implementuj `SetContextRecoveryEnabled`, `DebugSimulateContextLoss` a `DebugRestoreContext` ve stejném veřejném kontraktu jako EasyGL, ale jen pro 2D zdroje. | ✅ | Factory přebírá počáteční volbu z `GraphicsBackendCreateArgs`; přepnutí ovlivňuje registraci nových 2D zdrojů. Veřejný test ověřuje obnovu zdroje vytvořeného při zapnutí, odmítnutí stale textury vytvořené při vypnutí a funkčnost nového zdroje po resetu. |
| D2D-9 | Průběžně aktualizuj capability reporting a dokumentaci 2D limitů; nikdy nehlaš 3D, custom effects, queries ani anisotropii. | ✅ | Nový `docs/direct2d-backend.md` popisuje hranici 2D/D3D11 a všechny explicitní limity. `Direct2D_2DParity` pixelově ověřuje, že všech devět hodnot současného `GraphicsCapability` (3D, depth/stencil, MSAA, MRT, anisotropie, wireframe, query, custom effect, Texture3D) vrací `false`. |

## Fáze D2D-2 — mipy a vzorkování zůstávající uvnitř 2D API

Direct2D neposkytuje veřejnou obecnou 3D sampler pipeline ani nativní mip-chain objekt analogický
GLES. Pokud se níže uvedené úkoly ukážou jako nutné, implementace zůstane 2D: per-level bitmapy,
Direct2D draw/downsample a výběr vhodného levelu pro SpriteBatch. Nesmí přerůst ve sdílený
D3D11 3D renderer.

| # | Úkol | Stav | Akceptace |
|---|---|---|---|
| D2D-10 | Ověř vhodný čistě 2D návrh storage: primární bitmapa, CPU readback cesta, volitelné per-mip `ID2D1Bitmap1` a jejich lifetime při resize/device recovery. | ✅ | Spike je zaznamenán v `docs/direct2d-mip-storage-spike.md`: samostatné Direct2D bitmapy po levelech, Direct2D downsample při odbindu RT, level-scoped readback a recovery. `CopyFromBitmap` fallback odblokoval ve Wine level-0 i per-level CPU readback, takže návrh je pixelově ověřen bez D3D11 rendereru. |
| D2D-11 | Implementuj mipové levely `Texture2D`: alokace/validace levelů, `UpdatePixelsLevel` a volba levelu při minifikaci SpriteBatch zdroje. | ✅ | `Direct2DTextureBackend` drží per-level `ID2D1Bitmap1` a RGBA shadow, validuje rozměry `UpdatePixelsLevel`, při minifikaci volí nejbližší inicializovaný level a při recovery obnoví všechny zapsané mipy. Izolovaný Wine pixelový test ověřil zápis a výběr levelů 0/1/2 i nižší mip po device recovery. |
| D2D-12 | Implementuj mipové `RenderTarget2D`: při odpojení vygeneruj nižší levely Direct2D downsample drawem a dovol jejich `GetData`. | ✅ | `mipMap=true` alokuje target-capable `ID2D1Bitmap1` až do 1×1. Po odbindu se GPU-only lineárním Direct2D downsamplem vytvoří chain a SpriteBatch vybírá level podle minifikace; `GetData(level)` validuje rozměry a dočasně čte správnou bitmapu. Izolovaný Wine pixelový test ověřil vzorkování úrovní 0/1/2 i přesný `GetData` 1×1 regionu levelů 1/2 přes `CopyFromBitmap` fallback. |
| D2D-13 | Dokonči přesnou 2D sampler politiku: Point/Linear, Clamp/Wrap/Mirror, source rectangles mimo obraz a jejich stejný výsledek pro Texture2D i RT. | 🟨 | Zvolen a implementován EasyGL/FNA kontrakt: UV mimo obraz zůstávají v sampleru, `Clamp` je clamp-to-edge. Netintované/ne-straight-alpha zdroje sdílejí GPU `ID2D1ImageBrush`; jeho transform kompenzuje Direct2D ořez záporného počátku před flipem. `Direct2D_2DParity` pixelově prošel ve Wine i Proton Experimental/DXVK pro Point Clamp/Wrap/Mirror v obou osách, Clamp + FlipH/FlipV a lineární Clamp/Wrap/Mirror v obou osách včetně FlipH/FlipV, vždy přímým porovnáním Texture2D proti RT. Zbývá nativní Windows matice tint/NonPremultiplied, protože Proton má stejný chybějící `ColorMatrix` effect jako Wine. |
| D2D-14 | Prozkoumej, zda Direct2D primitive antialiasing nebo řízený supersampling dokáže užitečně splnit 2D MultiSampleCount kontrakt. Nepoužívej k tomu D3D11 3D render pass. | ✅ | Rozhodnuto a zakotveno jako explicitní 2D limit: `D2D1_ANTIALIAS_MODE_PER_PRIMITIVE` nevyjadřuje počet vzorků a DXGI MSAA by vyžadovalo D3D multisampled backing surface + resolve, což je mimo rozsah. `ApplyMultiSampleCount` i RT backend vrací `0`, capability zůstává `false` a `Direct2D_2DParity` veřejně ověřuje RT požadující 4× MSAA → 0. |

## Fáze D2D-3 — přesnost a omezení compositingu

Direct2D umí `SourceOver`, `Copy` a `Add`; to pokrývá standardní SpriteBatch presety, nikoliv
plný XNA/D3D blend model. Tento backend nebude obcházet omezení vlastním D3D11 compositing
pasem, protože by se tím stal druhým D3D11 backendem.

| # | Úkol | Stav | Akceptace |
|---|---|---|---|
| D2D-20 | Zpřesni a pixelově otestuj nativní mapování Opaque, AlphaBlend, NonPremultiplied a Additive, včetně Color.A, premultiplied textur a render-target zdrojů. | 🟨 | Nedekorované sprite zdroje používají `DrawImage` s explicitním `SOURCE_OVER`/`PLUS`/`SOURCE_COPY`, nikoli `DrawBitmap` závislý na mutable primitive blend state. `Direct2D_2DParity` ve Wine i Proton Experimental/DXVK ověřuje premultiplied AlphaBlend, straight-alpha NonPremultiplied, `Color.A` pro oba a RT jako AlphaBlend zdroj. Proton přesto pro šest Additive/Opaque texture/RT probes vrátil `SOURCE_OVER` hodnotu (např. `(138,20,40,255)` místo `(148,40,80,255)`), a `ColorMatrix`/`Premultiply` effect graph končí `0x88990028`; byte-exact dokončení proto zůstává nativní Windows gate. |
| D2D-21 | Pro všechny ostatní `BlendState` kombinace, blend factor, color-write channels a multisample mask zaveď deterministickou named exception a popis v dokumentaci. | ✅ | `ApplyBlendState` odmítá ne-Add/symetrické faktory, ColorWriteChannels masky a MultiSampleMask; samostatný nenulový `GraphicsDevice.BlendFactor` také hází named exception. `Direct2D_2DParity` ověřuje všechny čtyři veřejné odmítací cesty, bez tiché aproximace. |

## Pořadí a pravidla ověření

1. Nejprve D2D-1 až D2D-9: nejvyšší 2D přínos bez změny rozsahu backendu.
2. D2D-10 je brána před skutečnými mip úkoly D2D-11/D2D-12.
3. D2D-20/D2D-21 uzavírají compositing; plný blend model zůstává úkolem existujícího `D3D11` backendu.
4. Při každé změně ověř čistý unit test i skutečný Windows/DXVK pixelový CTest. Budoucí kompilace a testy spouštěj nanejvýš se dvěma souběžnými joby (`-j2` / `--parallel 2`).
