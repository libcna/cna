# CNA Project Guidelines

## Project Overview

**CNA** is a C++ reimplementation of the XNA 4.0 programming model, built on SDL3 and a pluggable graphics backend
layer.

It is a framework/runtime and abstraction layer—not a game—designed to preserve XNA-style APIs (Microsoft::Xna::
Framework) while using modern C++ internals.

CNA demonstrates engine-level C++ architecture, graphics abstraction design, and backend-oriented systems engineering.

## Code Generation Rules

### XNA 4.0 API Compliance

When implementing code in the `Microsoft::Xna` namespace:

- **MUST** strictly adhere to the XNA 4.0 API specification
- **MUST** preserve original XNA 4.0 class names, method signatures, and behavior
- **MUST** use modern C++23 internally while maintaining XNA-style public APIs
- If implementing functionality that is **NOT** part of the XNA 4.0 API within the `Microsoft::Xna` namespace, you *
  *MUST** wrap it with the `NOXNA` macro

Example:
