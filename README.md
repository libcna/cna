# CNA

This is a C++ reimplementation of XNA 4.0 built on top of SDL 3.

## How to build

```bash
cmake -S . -B build
cmake --build ./build/
````

## Implementation Status Convention

This project uses a simple documentation-only status convention to describe how complete individual classes and functions are.

The following status values are used in comments:

* `TODO` — not implemented yet
* `STUB` — skeleton only, fails or returns a placeholder
* `PARTIAL` — partially working
* `IMPLEMENTED` — functionally complete
* `VERIFIED` — complete and checked against expected XNA behavior

These statuses are **not** represented by a real C++ enum in the codebase. They are used only as source-level documentation.

### Comment format

Classes and functions may include a comment in this form:

```cpp
// STATUS=TODO
// STATUS=STUB
// STATUS=PARTIAL
// STATUS=IMPLEMENTED
// STATUS=VERIFIED
```

Example:

```cpp
// STATUS=PARTIAL
class SpriteBatch
{
public:
    // STATUS=IMPLEMENTED
    void Begin();

    // STATUS=PARTIAL
    void Draw(Texture2D& texture, Vector2 position, Color color);

    // STATUS=TODO
    void End();
};
```

The goal is to keep API names clean and XNA-like while making implementation progress explicit in the source code.

