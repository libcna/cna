# Direct2D backend — plán 2D parity s EasyGL

> **Rozhodnutý rozsah:** `DIRECT2D` je trvale **2D-only** backend. Nepřidává se do něj 3D
> pipeline jen proto, že Direct2D interně používá D3D11 device pro GPU akceleraci a prezentaci.
> Pro 3D slouží existující `D3D11` backend.
>
> **Stav:** níže jsou nové, neimplementované úkoly. `⬜` znamená neimplementováno, `🔬`
> technický spike před implementací. Každá hotová položka musí mít pixelový test na Windows/DXVK.

## Co EasyGL umí navíc a co je zde v rozsahu

Současný Direct2D backend již implementuje `Texture2D`, `RenderTarget2D`, `SpriteBatch`, běžné
presentation módy, standardní blend presety, scissor rectangle a staging readback **swap chain
backbufferu**. EasyGL je širší GLES3 backend; tento plán vybírá pouze rozdíly, které mají smysl
pro poctivý 2D Direct2D backend.

| Funkce EasyGL | Direct2D nyní | Rozhodnutí pro Direct2D |
|---|---|---|
| `RenderTarget2D::GetData` a readback aktivního cíle | RT `GetData` vrací `false`; readback vždy čte swap chain | **V rozsahu:** skutečný 2D readback |
| Tint, flip, Wrap/Mirror pro render target jako SpriteBatch zdroj | u RT hází výjimku, protože nemá CPU shadow | **V rozsahu:** Direct2D effect/brush cesta |
| Mipové levely `Texture2D` a `RenderTarget2D` | pouze level 0 | **V rozsahu:** 2D bitmapy po levelech, generování downsamplem |
| Point/linear + adresování 2D sprite zdrojů | základ funguje; RT omezený | **V rozsahu:** přesné 2D chování; anisotropie není cíl |
| Scissor enable a viewport | `SetScissorRect` vždy clipuje; `SetViewport` je no-op | **V rozsahu:** 2D clip/transform stav |
| Context loss/recovery | požaduje rekonstrukci celé hry | **V rozsahu:** obnova Direct2D zdrojů |
| MSAA | 0 | **Pouze spike:** nativní Direct2D antialias/supersampling; ne D3D11 3D/MSAA pipeline |
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
| D2D-1 | Zaveď samostatnou matici 2D parity testů: Texture2D, RenderTarget2D, SpriteBatch, clipping, presentation, resize a readback. Používej pouze veřejné 2D API. | ⬜ | CTest target pro Windows/DXVK; každá další položka přidá pixelový regresní test. |
| D2D-2 | Implementuj `Direct2DRenderTargetBackend::GetData`: validace regionu a levelu, bezpečný převod do CPU-readable 2D bitmapy/stagingu, BGRA→RGBA a top-left pořadí řádků. | ⬜ | Částečný i celý RT readback je byte-exact po Clear i SpriteBatch draw; chybný rozsah nezapíše částečná data. |
| D2D-3 | Oprav `ReadBackbuffer` při navázaném RT: čti aktivní 2D cíl, nebo veřejně vynucuj jeho odbind podle společného kontraktu. Nikdy tiše nečti swap chain, když hra kreslí jinam. | ⬜ | Backbuffer a menší RT vrátí rozdílné očekávané pixely; přepnutí cíle nezanechá otevřený `BeginDraw`. |
| D2D-4 | Doplň SpriteBatch pro `RenderTarget2D` jako zdroj: tint, flip, crop, non-premultiplied vstup a Wrap/Mirror bez CPU shadow/render-target readbacku. Použij Direct2D bitmap brush a effect graph. | ⬜ | Stejné varianty fungují pro obyčejnou texturu i dříve vykreslený RT; ve frame nevznikne CPU kopie RT. |
| D2D-5 | Uprav `ApplyRasterizerState` a `SetScissorRect`: ukládej `ScissorTestEnable`, clip zapínej jen pokud je povolený a počítej jej vůči aktivnímu RT/backbufferu. | ⬜ | Enabled a disabled scissor mají správně rozdílný výstup; přepnutí RT neprohodí osu ani výšku. |
| D2D-6 | Implementuj `SetViewport` pro 2D výstup jako zdokumentovaný transform+clip stav. Urči interakci s virtual resolution, presentation transformem, SpriteBatch transformem a RT. | ⬜ | Viewport s offsetem i viewport na RT funguje a stav není tichý no-op. |
| D2D-7 | Zaveď obnovu po `D2DERR_RECREATE_TARGET`/device removal: znovu vytvoř D3D/D2D/DXGI prezentační zdroje a registrované 2D bitmapy. Obyčejné textury obnov z CPU shadow; chování obsahu RT explicitně definuj podle content usage. | ⬜ | Po simulované obnově lze znovu kreslit, žádný zdroj nedrží starý COM objekt a poškozený RT obsah se nechová jako platný. |
| D2D-8 | Implementuj `SetContextRecoveryEnabled`, `DebugSimulateContextLoss` a `DebugRestoreContext` ve stejném veřejném kontraktu jako EasyGL, ale jen pro 2D zdroje. | ⬜ | Test ověří zapnutou/vypnutou obnovu a textury vytvořené před i po přepnutí. |
| D2D-9 | Průběžně aktualizuj capability reporting a dokumentaci 2D limitů; nikdy nehlaš 3D, custom effects, queries ani anisotropii. | ⬜ | `SupportsCapability()` a dokumentace souhlasí s testovaným chováním. |

## Fáze D2D-2 — mipy a vzorkování zůstávající uvnitř 2D API

Direct2D neposkytuje veřejnou obecnou 3D sampler pipeline ani nativní mip-chain objekt analogický
GLES. Pokud se níže uvedené úkoly ukážou jako nutné, implementace zůstane 2D: per-level bitmapy,
Direct2D draw/downsample a výběr vhodného levelu pro SpriteBatch. Nesmí přerůst ve sdílený
D3D11 3D renderer.

| # | Úkol | Stav | Akceptace |
|---|---|---|---|
| D2D-10 | 🔬 Ověř vhodný čistě 2D návrh storage: primární bitmapa, CPU readback cesta, volitelné per-mip `ID2D1Bitmap1` a jejich lifetime při resize/device recovery. | 🔬 | Spike vykreslí do RT, znovu jej vzorkuje jako sprite a přečte CPU data bez změny 3D capability contractu. |
| D2D-11 | Implementuj mipové levely `Texture2D`: alokace/validace levelů, `UpdatePixelsLevel` a volba levelu při minifikaci SpriteBatch zdroje. | ⬜ | Zápis a následné vzorkování každého platného levelu má ověřitelný pixelový výsledek; bez neúplného/černého výsledku. |
| D2D-12 | Implementuj mipové `RenderTarget2D`: při odpojení vygeneruj nižší levely Direct2D downsample drawem a dovol jejich `GetData`. | ⬜ | Vykreslený level 0 vytvoří očekávané nižší levely a ty lze číst i vzorkovat. |
| D2D-13 | Dokonči přesnou 2D sampler politiku: Point/Linear, Clamp/Wrap/Mirror, source rectangles mimo obraz a jejich stejný výsledek pro Texture2D i RT. | ⬜ | Sdílená SpriteBatch testovací sada pokrývá všechny kombinace; nepodporovaná kombinace má explicitní chybu. |
| D2D-14 | 🔬 Prozkoumej, zda Direct2D primitive antialiasing nebo řízený supersampling dokáže užitečně splnit 2D MultiSampleCount kontrakt. Nepoužívej k tomu D3D11 3D render pass. | 🔬 | Závěr měřený pixelovým testem: buď skutečná, dokumentovaná 2D implementace a pravdivá capability, nebo explicitní trvalé `0`/`false`. |

## Fáze D2D-3 — přesnost a omezení compositingu

Direct2D umí `SourceOver`, `Copy` a `Add`; to pokrývá standardní SpriteBatch presety, nikoliv
plný XNA/D3D blend model. Tento backend nebude obcházet omezení vlastním D3D11 compositing
pasem, protože by se tím stal druhým D3D11 backendem.

| # | Úkol | Stav | Akceptace |
|---|---|---|---|
| D2D-20 | Zpřesni a pixelově otestuj nativní mapování Opaque, AlphaBlend, NonPremultiplied a Additive, včetně Color.A, premultiplied textur a render-target zdrojů. | ⬜ | Každý preset má očekávané pixely na backbufferu i RT; žádný stav nezůstane mezi `SpriteBatch::Begin`/`End`. |
| D2D-21 | Pro všechny ostatní `BlendState` kombinace, blend factor, color-write channels a multisample mask zaveď deterministickou named exception a popis v dokumentaci. | ⬜ | Žádná nepřesná tichá mapa; unit test pokrývá odmítnuté kombinace a capability zůstává poctivá. |

## Pořadí a pravidla ověření

1. Nejprve D2D-1 až D2D-9: nejvyšší 2D přínos bez změny rozsahu backendu.
2. D2D-10 je brána před skutečnými mip úkoly D2D-11/D2D-12.
3. D2D-20/D2D-21 uzavírají compositing; plný blend model zůstává úkolem existujícího `D3D11` backendu.
4. Při každé změně ověř čistý unit test i skutečný Windows/DXVK pixelový CTest. Budoucí kompilace a testy spouštěj nanejvýš se dvěma souběžnými joby (`-j2` / `--parallel 2`).
