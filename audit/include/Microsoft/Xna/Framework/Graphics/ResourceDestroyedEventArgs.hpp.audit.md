# Audit: include/Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp` (35 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/ResourceDestroyedEventArgs.cs`
- Main related tests: not independently located in this pass

## Purpose
Event data for `GraphicsDevice.ResourceDestroyed`, carrying the destroyed resource's name and tag.

## Executive Verdict
Correct, with the same minor, low-risk constructor-default-widening pattern as the sibling
`ResourceCreatedEventArgs` in this same batch.

## Checklist Results
- Doxygen coverage: complete.

## Detailed Findings

### LOW — constructor has default parameter values not present in FNA's real (internal) constructor
Real FNA's `internal ResourceDestroyedEventArgs(string name, object tag)` requires both arguments
explicitly. This port's constructor defaults both (`const std::string& name = {}, System::Object*
tag = nullptr`), allowing default construction. Low severity, same reasoning as the sibling
`ResourceCreatedEventArgs` finding.

## Cross-File Observations
Shares the identical pattern with `ResourceCreatedEventArgs` (audited in this same batch) — both
event-args types in `GraphicsDevice`'s resource-lifecycle event pair widen their FNA-internal,
required-argument constructors into optionally-default-constructible ones.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`getNameProperty()`/`getTagProperty()` correctly match FNA's real `Name`/`Tag` read-only property
shapes.

## Final Assessment
One LOW finding.
