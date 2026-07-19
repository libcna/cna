# Audit: src/Microsoft/Xna/Framework/Input/Mouse.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- Audit status: AUDITED (full read, 189 lines)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/Mouse.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `GetState()`/`SetPosition()`'s logical-to-window coordinate transform,
`SetCursor()`'s disposed-cursor guard, relative-mouse-mode toggling, and the EXT
capture/global-position/warp methods.

## Executive Verdict
Correct, and a well-reasoned architectural adaptation. The class comment (lines 76-83) clearly
explains CNA's coordinate model versus FNA's: FNA scales by a fixed window<->backbuffer ratio
("faux-backbuffer"); CNA solves the general window<->logical problem through the graphics backend
(`SDL_RenderCoordinatesToWindow` for `SDL_Renderer`, `IGraphicsBackend::TransformLogicalToWindow`
for others), correctly citing a specific verified task (858) for the SDL_Renderer letterbox-offset
case. `SetCursor()`'s disposed-cursor no-op guard correctly avoids a real SDL footgun
(`SDL_SetCursor(NULL)` does not clear the cursor, it forces a redraw of the current one — passing a
disposed cursor through unguarded would silently keep the old cursor while appearing to have
changed).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`SetCursor()` correctly guards using `MouseCursor::GetSDLCursor()` (audited separately).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Careful, correctly-reasoned null-window and disposed-cursor guards throughout; the coordinate-
transform design is a well-justified generalization of FNA's simpler fixed-ratio model.

## Final Assessment
No findings.
