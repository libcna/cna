# Task: Implement Content System with ContentTypeReader and ShaderEffect

## Goal

Rozšířit `Microsoft::Xna::Framework::Content` o plnohodnotný extensible content loading systém
bez XNB — s vlastními type readery, cachováním a podporou GLSL shaderů přes JSON descriptor.

---

## Background

`ContentManager` existuje v:
- `include/Microsoft/Xna/Framework/Content/ContentManager.hpp`
- `src/Microsoft/Xna/Framework/Content/ContentManager.cpp`

Aktuálně podporuje pouze `Texture2D` a `SoundEffect` — typy jsou hardcoded v `if constexpr`.
Žádné cachování, žádná rozšiřitelnost, žádná `ContentLoadException`.

FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Content/`

**Důležité:** CNA **nepoužívá XNB**. `ContentReader` (BinaryReader pro XNB stream),
`ContentTypeReaderManager` (C# reflexe), `LzxDecoder` a `ContentSerializerAttribute`
se **neimplementují**.

---

## Implementation Plan

### Krok 1 — `ContentLoadException`

Nový soubor: `include/Microsoft/Xna/Framework/Content/ContentLoadException.hpp`
a `src/Microsoft/Xna/Framework/Content/ContentLoadException.cpp`

```cpp
namespace Microsoft::Xna::Framework::Content {

class ContentLoadException : public std::runtime_error {
public:
    explicit ContentLoadException(const std::string& message);
    ContentLoadException(const std::string& message, const std::exception& inner);
};

}
```

---

### Krok 2 — `ContentTypeReader<T>`

Nový soubor: `include/Microsoft/Xna/Framework/Content/ContentTypeReader.hpp`

```cpp
namespace Microsoft::Xna::Framework::Content {

class ContentManager; // forward

template <typename T>
class ContentTypeReader {
public:
    virtual ~ContentTypeReader() = default;
    // path = plná cesta k souboru (sestavena ContentManagerem)
    // cm   = zpětný přístup pro rekurzivní načítání (loader fontu → Texture2D)
    virtual T Read(const std::string& path, ContentManager& cm) = 0;
};

}
```

---

### Krok 3 — Rozšíření `ContentManager`

Upravit stávající `ContentManager.hpp` / `.cpp`:

**Přidat:**
- `void Unload()` — uvolní cache
- `template <typename T> void RegisterTypeReader(std::unique_ptr<ContentTypeReader<T>> reader)`
- interní cache: `std::unordered_map<std::string, std::any> loadedAssets_`
- interní mapa loaderů: `std::unordered_map<std::type_index, std::any> typeReaders_`
- privátní `std::string NormalizeKey(const std::string& assetName) const` — lowercase + `/` separátor

**Logika `Load<T>`:**
1. Normalizuj klíč (`NormalizeKey`)
2. Zkontroluj `loadedAssets_` — pokud hit, vrať
3. Hledej registrovaný `ContentTypeReader<T>` v `typeReaders_`
4. Pokud nalezen: `reader.Read(BuildAssetPath(assetName), *this)`, ulož do cache, vrať
5. Pokud ne: fallback na stávající vestavěné typy (Texture2D, SoundEffect)
6. Pokud ani fallback, vyhoď `ContentLoadException`

**V konstruktoru** auto-registrovat vestavěný loader pro `Effect` (viz krok 5).

---

### Krok 4 — `ShaderEffect` (NOXNA)

Nové soubory:
- `include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp`
- `src/Microsoft/Xna/Framework/Graphics/ShaderEffect.cpp`

```cpp
namespace Microsoft::Xna::Framework::Graphics {

// NOXNA — není součástí XNA 4.0 API
NOXNA class ShaderEffect : public Effect {
public:
    // vertSrc, fragSrc = obsah GLSL zdrojových souborů (ne cesty)
    NOXNA ShaderEffect(GraphicsDevice& device,
                       const std::string& vertSrc,
                       const std::string& fragSrc);

protected:
    void OnApply() override;
};

}
```

`OnApply()` předá vertex + fragment GLSL source existujícímu graphics backendu.
Způsob integrace s backendem (EasyGL / Vulkan) zkontroluj v:
- `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`
- `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`

---

### Krok 5 — Vestavěný loader pro `Effect`

Formát souboru: `.shader.json`

```json
{
  "vertex": "Shaders/MyEffect.vert",
  "fragment": "Shaders/MyEffect.frag"
}
```

Cesty v JSON jsou relativní k `RootDirectory` ContentManageru.

Loader (vnitřní třída nebo lambda registrovaná v konstruktoru `ContentManager`):
1. Přečte JSON soubor (lze použít jednoduchý string parser nebo třetí stranu — viz `third_party/`)
2. Sestaví plné cesty k `.vert` a `.frag`
3. Načte jejich textový obsah
4. Vytvoří a vrátí `ShaderEffect`

Fallback přípona v `Load<Effect>("Effects/MyFx")` bez tečky → hledej `Effects/MyFx.shader.json`.

---

### Krok 6 — CMakeLists.txt

Přidat nové `.cpp` soubory do targetu `CNA` (nebo příslušné library):
- `src/Microsoft/Xna/Framework/Content/ContentLoadException.cpp`
- `src/Microsoft/Xna/Framework/Graphics/ShaderEffect.cpp`

---

## Invarianty — neporušovat

- **Žádné XNB** — nikdy, ani stub, ani placeholder
- `ShaderEffect` musí mít makro `NOXNA` — není součástí XNA 4.0 API
- Cache klíč = `NormalizeKey`: lowercase + `/` jako separátor (shodné s XNA chováním)
- `ContentTypeReader<T>::Read` dostává **plnou cestu** (ne assetName)
- Dodržuj `.junie/guide.md`: namespace, viditelnost, split `.hpp`/`.cpp`, stavba po změnách

---

## Files to Create / Modify

| Soubor | Akce |
|---|---|
| `include/Microsoft/Xna/Framework/Content/ContentLoadException.hpp` | vytvořit |
| `src/Microsoft/Xna/Framework/Content/ContentLoadException.cpp` | vytvořit |
| `include/Microsoft/Xna/Framework/Content/ContentTypeReader.hpp` | vytvořit |
| `include/Microsoft/Xna/Framework/Content/ContentManager.hpp` | upravit |
| `src/Microsoft/Xna/Framework/Content/ContentManager.cpp` | upravit |
| `include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp` | vytvořit |
| `src/Microsoft/Xna/Framework/Graphics/ShaderEffect.cpp` | vytvořit |
| `CMakeLists.txt` | upravit (přidat nové .cpp) |

---

## Acceptance Criteria

- [ ] `ContentLoadException` existuje a lze vyhodit / zachytit
- [ ] `ContentTypeReader<T>` je abstraktní šablona s `Read(path, cm)` 
- [ ] `ContentManager::RegisterTypeReader<T>()` funguje pro vlastní typy
- [ ] `ContentManager::Load<T>` cachuje načtené assety (druhý `Load` stejného assetName nevede k I/O)
- [ ] `ContentManager::Unload()` vyčistí cache
- [ ] `ShaderEffect` existuje, má `NOXNA`, dědí z `Effect`, přijímá GLSL source strings
- [ ] `ContentManager::Load<Effect>` načte `.shader.json` a vrátí `ShaderEffect`
- [ ] Projekt se zkompiluje bez nových chyb
- [ ] Stávající `Load<Texture2D>` a `Load<SoundEffect>` zůstávají funkční
