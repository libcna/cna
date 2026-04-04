# CNA

This is a C++ reimplementation of XNA 4.0 built on top of SDL 3.

## How to build

```bash
cmake -S . -B build
cmake --build ./build/
```

## Implementation Status Convention

This project uses a simple documentation-only status convention to describe how complete individual classes and functions are.

The following status values are used in Doxygen comments:

* `Todo` — not implemented yet
* `Stub` — skeleton only, fails or returns a placeholder
* `Partial` — partially working
* `Implemented` — functionally complete
* `Verified` — complete and checked against expected XNA behavior

These statuses are **not** represented by a real C++ enum in the codebase. They are used only as source-level documentation.

### Comment format

Classes and functions may include a Doxygen comment in this form:

```cpp
/**
 * @note Status: Todo
 */

/**
 * @note Status: Stub
 */

/**
 * @note Status: Partial
 */

/**
 * @note Status: Implemented
 */

/**
 * @note Status: Verified
 */
```

Example:

```cpp
/**
 * @brief Provides 2D sprite rendering functionality.
 *
 * @note Status: Partial
 */
class SpriteBatch
{
public:
    /**
     * @brief Begins a sprite drawing batch.
     *
     * @note Status: Implemented
     */
    void Begin();

    /**
     * @brief Draws a texture at the specified position.
     *
     * @note Status: Partial
     */
    void Draw(Texture2D& texture, Vector2 position, Color color);

    /**
     * @brief Ends a sprite drawing batch.
     *
     * @note Status: Todo
     */
    void End();
};
```

The goal is to keep API names clean and XNA-like while making implementation progress explicit in the source code and visible in Doxygen-compatible documentation tools.
