
# Task: Bundle SDL3, SDL_image, and SDL_mixer into the Project (No System Installation Required)

## Goal
Ensure that the project builds without requiring SDL libraries to be installed on the system.

## Description
Currently, the project depends on system-installed SDL libraries. This task aims to make the build self-contained by bundling:
- SDL3
- SDL_image
- SDL_mixer

The preferred approach is to include these dependencies directly in the repository using **git submodules**, with an optional fallback to system-installed libraries.

---

## Implementation Plan

### 1. Add Dependencies as Git Submodules

```bash
git submodule add https://github.com/libsdl-org/SDL external/SDL
git submodule add https://github.com/libsdl-org/SDL_image external/SDL_image
git submodule add https://github.com/libsdl-org/SDL_mixer external/SDL_mixer
git submodule update --init --recursive
````

---

### 2. Update CMake Configuration

Add an option to toggle between bundled and system libraries:

```cmake
option(USE_SYSTEM_SDL "Use system-installed SDL libraries" OFF)
```

#### If using system libraries:

```cmake
if(USE_SYSTEM_SDL)
    find_package(SDL3 REQUIRED)
    find_package(SDL3_image REQUIRED)
    find_package(SDL3_mixer REQUIRED)
endif()
```

#### If using bundled libraries:

```cmake
if(NOT USE_SYSTEM_SDL)
    add_subdirectory(external/SDL)
    add_subdirectory(external/SDL_image)
    add_subdirectory(external/SDL_mixer)
endif()
```

---

### 3. Link Libraries

```cmake
target_link_libraries(my_project PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
)
```

Optional (platform-dependent):

```cmake
target_link_libraries(my_project PRIVATE SDL3::SDL3main)
```

---

## Expected Result

* Project builds on a clean system without installing SDL manually
* Dependencies are version-controlled and reproducible
* Works consistently across Linux, Windows, macOS (and later Android/Web)

---

## Notes

* Prefer **git submodules** over `FetchContent` for:

    * Offline builds
    * Better IDE integration (CLion, etc.)
    * Easier debugging and patching
* Keep system SDL support as a fallback option
* Ensure submodules are initialized after cloning:

```bash
git clone --recurse-submodules <repo>
```

or:

```bash
git submodule update --init --recursive
```

---

## Acceptance Criteria

* [ ] Project builds without SDL installed on the system
* [ ] Submodules are properly initialized and used
* [ ] CMake option `USE_SYSTEM_SDL` works correctly
* [ ] Build works on at least Linux environment
* [ ] No SDL headers or libraries are required from the system
